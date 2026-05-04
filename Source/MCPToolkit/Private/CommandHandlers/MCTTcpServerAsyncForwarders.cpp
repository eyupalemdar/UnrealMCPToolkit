// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandDispatch/MCTCommandDispatch.h"
#include "CommandHandlers/MCTAsyncCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleTaskSubmit(TSharedPtr<FJsonObject> Params)
{
	auto ResolveDispatchDescriptor = [](const FString& CommandName, MCPToolkit::CommandDispatch::FMCTCommandDescriptor& OutDescriptor)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		if (!Descriptor)
		{
			return false;
		}

		OutDescriptor = BuildDispatchDescriptor(*Descriptor);
		return true;
	};

	MCPToolkit::CommandHandlers::AsyncCommands::FMCTAsyncSubmitCallbacks Callbacks;
	Callbacks.ResolveCommand = [](const FString& CommandName, MCPToolkit::CommandHandlers::AsyncCommands::FMCTAsyncCommandDescriptor& OutDescriptor)
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
		return MCPToolkit::CommandDispatch::ValidateCommandInvocation(CommandName, Meta, ResolveDispatchDescriptor);
	};
	Callbacks.CreateDryRunResponse = [ResolveDispatchDescriptor](const FString& CommandName, TSharedPtr<FJsonObject> Meta)
	{
		return MCPToolkit::CommandDispatch::CreateDryRunResponseForInvocation(CommandName, Meta, ResolveDispatchDescriptor);
	};
	Callbacks.DispatchCommand = [this](const FString& CommandName, TSharedPtr<FJsonObject> CommandParams)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		return Descriptor ? DispatchCommand(*Descriptor, CommandParams) : CreateErrorResponse(FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName));
	};
	Callbacks.IsStopRequested = [this]() { return bStopRequested.Load(); };
	return MCPToolkit::CommandHandlers::AsyncCommands::HandleTaskSubmit(Params, AsyncJobStore, Callbacks);
}

FString FMCTTcpServer::HandleTaskStatus(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AsyncCommands::HandleTaskStatus(Params, AsyncJobStore);
}

FString FMCTTcpServer::HandleTaskResult(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AsyncCommands::HandleTaskResult(Params, AsyncJobStore);
}

FString FMCTTcpServer::HandleTaskEvents(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AsyncCommands::HandleTaskEvents(Params, AsyncJobStore);
}

FString FMCTTcpServer::HandleTaskEventsWait(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AsyncCommands::HandleTaskEventsWait(Params, AsyncJobStore, [this]() { return bStopRequested.Load(); });
}

FString FMCTTcpServer::HandleTaskCancel(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AsyncCommands::HandleTaskCancel(Params, AsyncJobStore);
}
