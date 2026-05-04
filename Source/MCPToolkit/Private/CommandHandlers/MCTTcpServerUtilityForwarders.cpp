// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTExportCommands.h"
#include "CommandHandlers/MCTUtilityCommands.h"
#include "CommandHandlers/MCTWorkflowCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandlePing()
{
	return MCPToolkit::CommandHandlers::Utility::HandlePing(BuildUtilityContext());
}

FString FMCTTcpServer::HandleListCommands()
{
	return MCPToolkit::CommandHandlers::Utility::HandleListCommands(BuildUtilityContext());
}

FString FMCTTcpServer::HandleServerStatus()
{
	return MCPToolkit::CommandHandlers::Utility::HandleServerStatus(BuildUtilityContext());
}

FString FMCTTcpServer::HandleEditorIdentity()
{
	return MCPToolkit::CommandHandlers::Utility::HandleEditorIdentity(BuildUtilityContext());
}

FString FMCTTcpServer::HandleCommandManifestExport(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Utility::HandleCommandManifestExport(Params, BuildUtilityContext());
}

FString FMCTTcpServer::HandleProjectStatus()
{
	return MCPToolkit::CommandHandlers::Utility::HandleProjectStatus(BuildUtilityContext());
}

FString FMCTTcpServer::HandleSourceControlStatus(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleSourceControlStatus(Params);
}

FString FMCTTcpServer::HandleSourceControlLog(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleSourceControlLog(Params);
}

FString FMCTTcpServer::HandleSourceControlShow(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleSourceControlShow(Params);
}

FString FMCTTcpServer::HandleSourceControlDiff(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleSourceControlDiff(Params);
}

FString FMCTTcpServer::HandleBuildProject(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleBuildProject(Params);
}

FString FMCTTcpServer::HandleGenerateProjectFiles(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleGenerateProjectFiles(Params);
}

FString FMCTTcpServer::HandleCookProject(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleCookProject(Params);
}

FString FMCTTcpServer::HandleListTests(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleListTests(Params);
}

FString FMCTTcpServer::HandleRunTests(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleRunTests(Params);
}

FString FMCTTcpServer::HandleGetTestLog(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleGetTestLog(Params);
}

FString FMCTTcpServer::HandleEditorLogRead(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Workflow::HandleEditorLogRead(Params);
}

FString FMCTTcpServer::HandleExportWidget(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Export::HandleExportWidget(Params);
}

FString FMCTTcpServer::HandleExportBlueprint(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Export::HandleExportBlueprint(Params);
}

FString FMCTTcpServer::HandleListSupportedTypes()
{
	return MCPToolkit::CommandHandlers::Export::HandleListSupportedTypes();
}
