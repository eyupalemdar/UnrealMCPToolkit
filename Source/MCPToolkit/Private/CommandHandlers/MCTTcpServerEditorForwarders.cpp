// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTEditorCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleEditorWorldInfo()
{
	return MCPToolkit::CommandHandlers::Editor::HandleEditorWorldInfo();
}

FString FMCTTcpServer::HandleActorList(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Editor::HandleActorList(Params);
}

FString FMCTTcpServer::HandleActorSpawn(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Editor::HandleActorSpawn(Params);
}

FString FMCTTcpServer::HandleActorSetTransform(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Editor::HandleActorSetTransform(Params);
}

FString FMCTTcpServer::HandleActorDelete(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Editor::HandleActorDelete(Params);
}

FString FMCTTcpServer::HandleLevelOpen(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Editor::HandleLevelOpen(Params);
}

FString FMCTTcpServer::HandleLevelSaveCurrent()
{
	return MCPToolkit::CommandHandlers::Editor::HandleLevelSaveCurrent();
}

FString FMCTTcpServer::HandlePIEStatus()
{
	return MCPToolkit::CommandHandlers::Editor::HandlePIEStatus();
}

FString FMCTTcpServer::HandlePIEStart()
{
	return MCPToolkit::CommandHandlers::Editor::HandlePIEStart();
}

FString FMCTTcpServer::HandlePIEStop()
{
	return MCPToolkit::CommandHandlers::Editor::HandlePIEStop();
}

FString FMCTTcpServer::HandleEditorConsoleCommand(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Editor::HandleEditorConsoleCommand(Params);
}

FString FMCTTcpServer::HandleViewportCapture(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Editor::HandleViewportCapture(Params);
}
