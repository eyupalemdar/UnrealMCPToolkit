// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTProjectCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleProjectInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectInfo(Params);
}

FString FMCTTcpServer::HandleProjectPluginList(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectPluginList(Params);
}

FString FMCTTcpServer::HandleProjectPluginSetEnabled(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectPluginSetEnabled(Params);
}

FString FMCTTcpServer::HandleProjectModuleList(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectModuleList(Params);
}

FString FMCTTcpServer::HandleProjectConfigGet(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectConfigGet(Params);
}

FString FMCTTcpServer::HandleProjectConfigSet(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectConfigSet(Params);
}

FString FMCTTcpServer::HandleProjectConfigDelete(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectConfigDelete(Params);
}

FString FMCTTcpServer::HandleProjectConfigListSections(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectConfigListSections(Params);
}

FString FMCTTcpServer::HandleProjectConfigListKeys(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Project::HandleProjectConfigListKeys(Params);
}
