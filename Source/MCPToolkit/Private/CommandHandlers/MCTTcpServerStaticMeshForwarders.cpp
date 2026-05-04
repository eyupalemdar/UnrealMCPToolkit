// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTStaticMeshCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleStaticMeshInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::StaticMesh::HandleStaticMeshInfo(Params);
}
