// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandDispatch/MCTCommandDispatch.h"
#include "CommandHandlers/MCTCommandResponse.h"
#include "MCTModule.h"

#include "Dom/JsonObject.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MCPToolkit::CommandDispatch
{
using MCPToolkit::CommandHandlers::CreateErrorResponse;
using MCPToolkit::CommandHandlers::CreateSuccessResponse;

FMCTCommandContext BuildCommandContext(TSharedPtr<FJsonObject> RootObject, const FMCTCommandDescriptor& Descriptor)
{
	FMCTCommandContext Context;
	Context.RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Context.TimeoutSeconds = Descriptor.TimeoutSeconds;

	const TSharedPtr<FJsonObject>* MetaObject = nullptr;
	if (RootObject.IsValid() && RootObject->TryGetObjectField(TEXT("meta"), MetaObject) && MetaObject && MetaObject->IsValid())
	{
		(*MetaObject)->TryGetStringField(TEXT("request_id"), Context.RequestId);
		(*MetaObject)->TryGetStringField(TEXT("client_id"), Context.ClientId);
		(*MetaObject)->TryGetStringField(TEXT("session_id"), Context.SessionId);
		(*MetaObject)->TryGetStringField(TEXT("scope"), Context.Scope);
		(*MetaObject)->TryGetBoolField(TEXT("dry_run"), Context.bDryRun);
		(*MetaObject)->TryGetBoolField(TEXT("cancel_requested"), Context.bCancellationRequested);

		double RequestedTimeout = 0.0;
		if ((*MetaObject)->TryGetNumberField(TEXT("timeout_seconds"), RequestedTimeout) && RequestedTimeout > 0.0)
		{
			Context.TimeoutSeconds = static_cast<int32>(RequestedTimeout);
		}
	}

	Context.Scope = Context.Scope.ToLower();
	return Context;
}

bool ValidateCommandScope(const FMCTCommandDescriptor& Descriptor, const FMCTCommandContext& Context, FString& OutError)
{
	const FString RequiredScope = Descriptor.RequiredScope.IsEmpty() ? TEXT("read") : Descriptor.RequiredScope.ToLower();
	const FString EffectiveScope = Context.Scope.IsEmpty() ? TEXT("write") : Context.Scope.ToLower();

	auto ScopeRank = [](const FString& Scope) -> int32
	{
		if (Scope == TEXT("read")) return 0;
		if (Scope == TEXT("write")) return 1;
		if (Scope == TEXT("destructive")) return 2;
		return -1;
	};

	const int32 RequiredRank = ScopeRank(RequiredScope);
	const int32 EffectiveRank = ScopeRank(EffectiveScope);
	if (EffectiveRank < 0)
	{
		OutError = FString::Printf(TEXT("Invalid command scope '%s'. Expected one of: read, write, destructive"), *EffectiveScope);
		return false;
	}

	if (RequiredRank < 0)
	{
		OutError = FString::Printf(TEXT("Command '%s' has invalid required scope '%s'"), *Descriptor.Name, *RequiredScope);
		return false;
	}

	if (EffectiveRank < RequiredRank)
	{
		OutError = FString::Printf(
			TEXT("Command '%s' requires '%s' scope; request provided '%s' scope. Pass top-level meta.scope='%s' only after explicit user approval."),
			*Descriptor.Name,
			*RequiredScope,
			*EffectiveScope,
			*RequiredScope);
		return false;
	}

	return true;
}

FString CreateDryRunResponse(const FMCTCommandDescriptor& Descriptor, const FMCTCommandContext& Context)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("dry_run"), true);
	Data->SetBoolField(TEXT("would_execute"), true);
	Data->SetStringField(TEXT("command"), Descriptor.Name);
	Data->SetStringField(TEXT("category"), Descriptor.Category);
	Data->SetStringField(TEXT("request_id"), Context.RequestId);
	Data->SetStringField(TEXT("required_scope"), Descriptor.RequiredScope.IsEmpty() ? TEXT("read") : Descriptor.RequiredScope);
	Data->SetStringField(TEXT("effective_scope"), Context.Scope.IsEmpty() ? TEXT("write") : Context.Scope);
	Data->SetStringField(TEXT("message"), TEXT("Dry-run accepted. No Unreal Editor state was changed."));
	return CreateSuccessResponse(Data);
}

FString ProcessCommandEnvelope(const FString& JsonCommand, const FMCTCommandProcessorCallbacks& Callbacks)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	TSharedPtr<FJsonObject> RootObject;

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid JSON format"));
	}

	FString CommandType;
	if (!RootObject->TryGetStringField(TEXT("type"), CommandType))
	{
		return CreateErrorResponse(TEXT("Missing 'type' field"));
	}

	TSharedPtr<FJsonObject> Params;
	if (RootObject->HasField(TEXT("params")))
	{
		const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
		if (!RootObject->TryGetObjectField(TEXT("params"), ParamsObject) || !ParamsObject || !ParamsObject->IsValid())
		{
			return CreateErrorResponse(TEXT("'params' must be a JSON object when provided"));
		}
		Params = *ParamsObject;
	}

	UE_LOG(LogMCT, Log, TEXT("Processing command: %s"), *CommandType);

	FMCTCommandDescriptor Descriptor;
	if (!Callbacks.ResolveCommand || !Callbacks.ResolveCommand(CommandType, Descriptor))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown command: %s"), *CommandType));
	}

	if (Descriptor.bRequiresParams && !Params.IsValid())
	{
		return CreateErrorResponse(FString::Printf(TEXT("Command '%s' requires a 'params' object"), *CommandType));
	}

	const FMCTCommandContext Context = BuildCommandContext(RootObject, Descriptor);
	FString ScopeError;
	if (!ValidateCommandScope(Descriptor, Context, ScopeError))
	{
		return CreateErrorResponse(ScopeError);
	}

	if (Context.bDryRun && Descriptor.bMutating)
	{
		return CreateDryRunResponse(Descriptor, Context);
	}

	if (!Callbacks.DispatchCommand)
	{
		return CreateErrorResponse(TEXT("Command dispatch callback is not configured"));
	}

	return Callbacks.DispatchCommand(CommandType, Params);
}

FString ValidateCommandInvocation(
	const FString& CommandName,
	TSharedPtr<FJsonObject> Meta,
	const TFunction<bool(const FString& CommandName, FMCTCommandDescriptor& OutDescriptor)>& ResolveCommand)
{
	FMCTCommandDescriptor Descriptor;
	if (!ResolveCommand || !ResolveCommand(CommandName, Descriptor))
	{
		return FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName);
	}

	TSharedPtr<FJsonObject> SyntheticRoot = MakeShared<FJsonObject>();
	SyntheticRoot->SetStringField(TEXT("type"), CommandName);
	SyntheticRoot->SetObjectField(TEXT("meta"), Meta.IsValid() ? Meta : MakeShared<FJsonObject>());

	const FMCTCommandContext Context = BuildCommandContext(SyntheticRoot, Descriptor);
	FString ScopeError;
	if (!ValidateCommandScope(Descriptor, Context, ScopeError))
	{
		return ScopeError;
	}

	return FString();
}

FString CreateDryRunResponseForInvocation(
	const FString& CommandName,
	TSharedPtr<FJsonObject> Meta,
	const TFunction<bool(const FString& CommandName, FMCTCommandDescriptor& OutDescriptor)>& ResolveCommand)
{
	FMCTCommandDescriptor Descriptor;
	if (!ResolveCommand || !ResolveCommand(CommandName, Descriptor))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName));
	}

	TSharedPtr<FJsonObject> SyntheticRoot = MakeShared<FJsonObject>();
	SyntheticRoot->SetStringField(TEXT("type"), CommandName);
	SyntheticRoot->SetObjectField(TEXT("meta"), Meta.IsValid() ? Meta : MakeShared<FJsonObject>());

	return CreateDryRunResponse(Descriptor, BuildCommandContext(SyntheticRoot, Descriptor));
}
}
