// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportAnimationAssetCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleAnimationAssetInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AnimationAsset::HandleAnimationAssetInfo(Params);
}
