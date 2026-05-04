// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportStaticMeshCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleStaticMeshInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::StaticMesh::HandleStaticMeshInfo(Params);
}
