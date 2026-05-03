// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandHandlers/AIExportAsyncJobStore.h"
#include "Dom/JsonObject.h"
#include "HttpMcp/AIExportHttpMcpServer.h"

namespace CommonAIExport::CommandHandlers::Utility
{
struct FAIExportUtilityCommandDescriptor
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

struct FAIExportUtilityContext
{
	int32 ServerPort = 0;
	int32 ActiveClientConnections = 0;
	FString EditorInstanceId;
	FString EditorRegistryFilePath;
	FString ServerStartedAtUtc;
	FString PortFilePath;
	FString ManifestSource;
	TArray<FAIExportUtilityCommandDescriptor> Commands;
	HttpMcp::FAIExportHttpMcpStatus HttpStatus;
	FAIExportAsyncJobCounts TaskCounts;
};

TSharedPtr<FJsonObject> BuildEditorIdentityJson(const FAIExportUtilityContext& Context);
TSharedPtr<FJsonObject> BuildCommandManifestJson(const FAIExportUtilityContext& Context);

FString HandlePing(const FAIExportUtilityContext& Context);
FString HandleListCommands(const FAIExportUtilityContext& Context);
FString HandleServerStatus(const FAIExportUtilityContext& Context);
FString HandleEditorIdentity(const FAIExportUtilityContext& Context);
FString HandleCommandManifestExport(TSharedPtr<FJsonObject> Params, const FAIExportUtilityContext& Context);
FString HandleProjectStatus(const FAIExportUtilityContext& Context);
}
