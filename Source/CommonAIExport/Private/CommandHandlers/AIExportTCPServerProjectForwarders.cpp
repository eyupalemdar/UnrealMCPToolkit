// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportProjectCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleProjectInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectInfo(Params);
}

FString FAIExportTCPServer::HandleProjectPluginList(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectPluginList(Params);
}

FString FAIExportTCPServer::HandleProjectPluginSetEnabled(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectPluginSetEnabled(Params);
}

FString FAIExportTCPServer::HandleProjectModuleList(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectModuleList(Params);
}

FString FAIExportTCPServer::HandleProjectConfigGet(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectConfigGet(Params);
}

FString FAIExportTCPServer::HandleProjectConfigSet(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectConfigSet(Params);
}

FString FAIExportTCPServer::HandleProjectConfigDelete(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectConfigDelete(Params);
}

FString FAIExportTCPServer::HandleProjectConfigListSections(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectConfigListSections(Params);
}

FString FAIExportTCPServer::HandleProjectConfigListKeys(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Project::HandleProjectConfigListKeys(Params);
}
