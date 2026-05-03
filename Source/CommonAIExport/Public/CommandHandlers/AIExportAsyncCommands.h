// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandHandlers/AIExportAsyncJobStore.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::AsyncCommands
{
struct FAIExportAsyncCommandDescriptor
{
	FString Name;
	bool bRequiresParams = false;
	bool bMutating = false;
	bool bAsyncCandidate = false;
	int32 TimeoutSeconds = 0;
};

struct FAIExportAsyncSubmitCallbacks
{
	TFunction<bool(const FString& CommandName, FAIExportAsyncCommandDescriptor& OutDescriptor)> ResolveCommand;
	TFunction<FString(const FString& CommandName, TSharedPtr<FJsonObject> Meta)> ValidateCommand;
	TFunction<FString(const FString& CommandName, TSharedPtr<FJsonObject> Meta)> CreateDryRunResponse;
	TFunction<FString(const FString& CommandName, TSharedPtr<FJsonObject> Params)> DispatchCommand;
	TFunction<bool()> IsStopRequested;
};

FString HandleTaskSubmit(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store, const FAIExportAsyncSubmitCallbacks& Callbacks);
FString HandleTaskStatus(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store);
FString HandleTaskResult(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store);
FString HandleTaskEvents(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store);
FString HandleTaskEventsWait(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store, TFunctionRef<bool()> IsStopRequested);
FString HandleTaskCancel(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store);
}
