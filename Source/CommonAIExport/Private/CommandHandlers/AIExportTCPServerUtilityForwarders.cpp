// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportExportCommands.h"
#include "CommandHandlers/AIExportUtilityCommands.h"
#include "CommandHandlers/AIExportWorkflowCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandlePing()
{
	return CommonAIExport::CommandHandlers::Utility::HandlePing(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleListCommands()
{
	return CommonAIExport::CommandHandlers::Utility::HandleListCommands(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleServerStatus()
{
	return CommonAIExport::CommandHandlers::Utility::HandleServerStatus(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleEditorIdentity()
{
	return CommonAIExport::CommandHandlers::Utility::HandleEditorIdentity(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleCommandManifestExport(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Utility::HandleCommandManifestExport(Params, BuildUtilityContext());
}

FString FAIExportTCPServer::HandleProjectStatus()
{
	return CommonAIExport::CommandHandlers::Utility::HandleProjectStatus(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleSourceControlStatus(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlStatus(Params);
}

FString FAIExportTCPServer::HandleSourceControlLog(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlLog(Params);
}

FString FAIExportTCPServer::HandleSourceControlShow(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlShow(Params);
}

FString FAIExportTCPServer::HandleSourceControlDiff(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlDiff(Params);
}

FString FAIExportTCPServer::HandleBuildProject(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleBuildProject(Params);
}

FString FAIExportTCPServer::HandleGenerateProjectFiles(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleGenerateProjectFiles(Params);
}

FString FAIExportTCPServer::HandleCookProject(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleCookProject(Params);
}

FString FAIExportTCPServer::HandleListTests(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleListTests(Params);
}

FString FAIExportTCPServer::HandleRunTests(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleRunTests(Params);
}

FString FAIExportTCPServer::HandleGetTestLog(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleGetTestLog(Params);
}

FString FAIExportTCPServer::HandleEditorLogRead(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleEditorLogRead(Params);
}

FString FAIExportTCPServer::HandleExportWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Export::HandleExportWidget(Params);
}

FString FAIExportTCPServer::HandleExportBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Export::HandleExportBlueprint(Params);
}

FString FAIExportTCPServer::HandleListSupportedTypes()
{
	return CommonAIExport::CommandHandlers::Export::HandleListSupportedTypes();
}
