// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTSkeletalMeshCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleSkeletalMeshInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::SkeletalMesh::HandleSkeletalMeshInfo(Params);
}
