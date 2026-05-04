// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportFoliageCommands.h"
#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleFoliageInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Foliage::HandleFoliageInfo(Params);
}

FString FAIExportTCPServer::HandleFoliageSampleInstances(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Foliage::HandleFoliageSampleInstances(Params);
}

FString FAIExportTCPServer::HandleFoliageTypeSettings(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Foliage::HandleFoliageTypeSettings(Params);
}
