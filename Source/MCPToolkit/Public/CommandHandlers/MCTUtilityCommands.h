// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandHandlers/MCTAsyncJobStore.h"
#include "Dom/JsonObject.h"
#include "HttpMcp/MCTHttpMcpServer.h"

namespace MCPToolkit::CommandHandlers::Utility
{
struct FMCTUtilityCommandDescriptor
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

struct FMCTUtilityContext
{
	int32 ServerPort = 0;
	int32 ActiveClientConnections = 0;
	FString EditorInstanceId;
	FString EditorRegistryFilePath;
	FString ServerStartedAtUtc;
	FString PortFilePath;
	FString ManifestSource;
	TArray<FMCTUtilityCommandDescriptor> Commands;
	HttpMcp::FMCTHttpMcpStatus HttpStatus;
	FMCTAsyncJobCounts TaskCounts;
};

TSharedPtr<FJsonObject> BuildEditorIdentityJson(const FMCTUtilityContext& Context);
TSharedPtr<FJsonObject> BuildCommandManifestJson(const FMCTUtilityContext& Context);

FString HandlePing(const FMCTUtilityContext& Context);
FString HandleListCommands(const FMCTUtilityContext& Context);
FString HandleServerStatus(const FMCTUtilityContext& Context);
FString HandleEditorIdentity(const FMCTUtilityContext& Context);
FString HandleCommandManifestExport(TSharedPtr<FJsonObject> Params, const FMCTUtilityContext& Context);
FString HandleProjectStatus(const FMCTUtilityContext& Context);
}
