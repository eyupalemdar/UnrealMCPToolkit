// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTLevelStructureCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleLevelStructureInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::LevelStructure::HandleLevelStructureInfo(Params);
}
