// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace CommonAIExport::CommandDispatch
{
struct FAIExportCommandDescriptor
{
	FString Name;
	FString Category;
	bool bRequiresParams = false;
	bool bMutating = false;
	int32 TimeoutSeconds = 0;
	FString RequiredScope = TEXT("read");
	bool bSupportsDryRun = false;
	bool bAsyncCandidate = false;
};

struct FAIExportCommandContext
{
	FString RequestId;
	FString ClientId;
	FString SessionId;
	FString Scope;
	int32 TimeoutSeconds = 0;
	bool bDryRun = false;
	bool bCancellationRequested = false;
};

struct FAIExportCommandProcessorCallbacks
{
	TFunction<bool(const FString& CommandName, FAIExportCommandDescriptor& OutDescriptor)> ResolveCommand;
	TFunction<FString(const FString& CommandName, TSharedPtr<class FJsonObject> Params)> DispatchCommand;
};

FAIExportCommandContext BuildCommandContext(TSharedPtr<class FJsonObject> RootObject, const FAIExportCommandDescriptor& Descriptor);
bool ValidateCommandScope(const FAIExportCommandDescriptor& Descriptor, const FAIExportCommandContext& Context, FString& OutError);
FString CreateDryRunResponse(const FAIExportCommandDescriptor& Descriptor, const FAIExportCommandContext& Context);
FString ProcessCommandEnvelope(const FString& JsonCommand, const FAIExportCommandProcessorCallbacks& Callbacks);
FString ValidateCommandInvocation(
	const FString& CommandName,
	TSharedPtr<class FJsonObject> Meta,
	const TFunction<bool(const FString& CommandName, FAIExportCommandDescriptor& OutDescriptor)>& ResolveCommand);
FString CreateDryRunResponseForInvocation(
	const FString& CommandName,
	TSharedPtr<class FJsonObject> Meta,
	const TFunction<bool(const FString& CommandName, FAIExportCommandDescriptor& OutDescriptor)>& ResolveCommand);
}
