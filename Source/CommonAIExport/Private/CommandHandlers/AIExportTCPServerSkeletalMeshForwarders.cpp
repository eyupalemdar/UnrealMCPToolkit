// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportSkeletalMeshCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleSkeletalMeshInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::SkeletalMesh::HandleSkeletalMeshInfo(Params);
}
