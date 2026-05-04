// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandDispatch/AIExportCommandDispatch.h"
#include "CommandHandlers/AIExportAsyncCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleTaskSubmit(TSharedPtr<FJsonObject> Params)
{
	auto ResolveDispatchDescriptor = [](const FString& CommandName, CommonAIExport::CommandDispatch::FAIExportCommandDescriptor& OutDescriptor)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		if (!Descriptor)
		{
			return false;
		}

		OutDescriptor = BuildDispatchDescriptor(*Descriptor);
		return true;
	};

	CommonAIExport::CommandHandlers::AsyncCommands::FAIExportAsyncSubmitCallbacks Callbacks;
	Callbacks.ResolveCommand = [](const FString& CommandName, CommonAIExport::CommandHandlers::AsyncCommands::FAIExportAsyncCommandDescriptor& OutDescriptor)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		if (!Descriptor)
		{
			return false;
		}
		OutDescriptor.Name = Descriptor->Name;
		OutDescriptor.bRequiresParams = Descriptor->bRequiresParams;
		OutDescriptor.bMutating = Descriptor->bMutating;
		OutDescriptor.bAsyncCandidate = Descriptor->bAsyncCandidate;
		OutDescriptor.TimeoutSeconds = Descriptor->TimeoutSeconds;
		return true;
	};
	Callbacks.ValidateCommand = [ResolveDispatchDescriptor](const FString& CommandName, TSharedPtr<FJsonObject> Meta)
	{
		return CommonAIExport::CommandDispatch::ValidateCommandInvocation(CommandName, Meta, ResolveDispatchDescriptor);
	};
	Callbacks.CreateDryRunResponse = [ResolveDispatchDescriptor](const FString& CommandName, TSharedPtr<FJsonObject> Meta)
	{
		return CommonAIExport::CommandDispatch::CreateDryRunResponseForInvocation(CommandName, Meta, ResolveDispatchDescriptor);
	};
	Callbacks.DispatchCommand = [this](const FString& CommandName, TSharedPtr<FJsonObject> CommandParams)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		return Descriptor ? DispatchCommand(*Descriptor, CommandParams) : CreateErrorResponse(FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName));
	};
	Callbacks.IsStopRequested = [this]() { return bStopRequested.Load(); };
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskSubmit(Params, AsyncJobStore, Callbacks);
}

FString FAIExportTCPServer::HandleTaskStatus(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskStatus(Params, AsyncJobStore);
}

FString FAIExportTCPServer::HandleTaskResult(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskResult(Params, AsyncJobStore);
}

FString FAIExportTCPServer::HandleTaskEvents(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskEvents(Params, AsyncJobStore);
}

FString FAIExportTCPServer::HandleTaskEventsWait(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskEventsWait(Params, AsyncJobStore, [this]() { return bStopRequested.Load(); });
}

FString FAIExportTCPServer::HandleTaskCancel(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskCancel(Params, AsyncJobStore);
}
