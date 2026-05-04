// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportLandscapeCommands.h"
#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleLandscapeInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Landscape::HandleLandscapeInfo(Params);
}

FString FAIExportTCPServer::HandleLandscapeSampleHeight(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Landscape::HandleLandscapeSampleHeight(Params);
}
