// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace MCPToolkit::CommandDispatch
{
struct FMCTCommandDescriptor
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

struct FMCTCommandContext
{
	FString RequestId;
	FString ClientId;
	FString SessionId;
	FString Scope;
	int32 TimeoutSeconds = 0;
	bool bDryRun = false;
	bool bCancellationRequested = false;
};

struct FMCTCommandProcessorCallbacks
{
	TFunction<bool(const FString& CommandName, FMCTCommandDescriptor& OutDescriptor)> ResolveCommand;
	TFunction<FString(const FString& CommandName, TSharedPtr<class FJsonObject> Params)> DispatchCommand;
};

FMCTCommandContext BuildCommandContext(TSharedPtr<class FJsonObject> RootObject, const FMCTCommandDescriptor& Descriptor);
bool ValidateCommandScope(const FMCTCommandDescriptor& Descriptor, const FMCTCommandContext& Context, FString& OutError);
FString CreateDryRunResponse(const FMCTCommandDescriptor& Descriptor, const FMCTCommandContext& Context);
FString ProcessCommandEnvelope(const FString& JsonCommand, const FMCTCommandProcessorCallbacks& Callbacks);
FString ValidateCommandInvocation(
	const FString& CommandName,
	TSharedPtr<class FJsonObject> Meta,
	const TFunction<bool(const FString& CommandName, FMCTCommandDescriptor& OutDescriptor)>& ResolveCommand);
FString CreateDryRunResponseForInvocation(
	const FString& CommandName,
	TSharedPtr<class FJsonObject> Meta,
	const TFunction<bool(const FString& CommandName, FMCTCommandDescriptor& OutDescriptor)>& ResolveCommand);
}
