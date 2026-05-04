// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportLevelStructureCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleLevelStructureInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::LevelStructure::HandleLevelStructureInfo(Params);
}
