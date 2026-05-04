// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandHandlers/MCTAsyncJobStore.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::AsyncCommands
{
struct FMCTAsyncCommandDescriptor
{
	FString Name;
	bool bRequiresParams = false;
	bool bMutating = false;
	bool bAsyncCandidate = false;
	int32 TimeoutSeconds = 0;
};

struct FMCTAsyncSubmitCallbacks
{
	TFunction<bool(const FString& CommandName, FMCTAsyncCommandDescriptor& OutDescriptor)> ResolveCommand;
	TFunction<FString(const FString& CommandName, TSharedPtr<FJsonObject> Meta)> ValidateCommand;
	TFunction<FString(const FString& CommandName, TSharedPtr<FJsonObject> Meta)> CreateDryRunResponse;
	TFunction<FString(const FString& CommandName, TSharedPtr<FJsonObject> Params)> DispatchCommand;
	TFunction<bool()> IsStopRequested;
};

FString HandleTaskSubmit(TSharedPtr<FJsonObject> Params, FMCTAsyncJobStore& Store, const FMCTAsyncSubmitCallbacks& Callbacks);
FString HandleTaskStatus(TSharedPtr<FJsonObject> Params, FMCTAsyncJobStore& Store);
FString HandleTaskResult(TSharedPtr<FJsonObject> Params, FMCTAsyncJobStore& Store);
FString HandleTaskEvents(TSharedPtr<FJsonObject> Params, FMCTAsyncJobStore& Store);
FString HandleTaskEventsWait(TSharedPtr<FJsonObject> Params, FMCTAsyncJobStore& Store, TFunctionRef<bool()> IsStopRequested);
FString HandleTaskCancel(TSharedPtr<FJsonObject> Params, FMCTAsyncJobStore& Store);
}
