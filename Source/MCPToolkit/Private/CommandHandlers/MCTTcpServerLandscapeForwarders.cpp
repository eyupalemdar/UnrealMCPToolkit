// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTLandscapeCommands.h"
#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleLandscapeInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Landscape::HandleLandscapeInfo(Params);
}

FString FMCTTcpServer::HandleLandscapeSampleHeight(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Landscape::HandleLandscapeSampleHeight(Params);
}
