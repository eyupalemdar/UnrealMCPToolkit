// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTAnimationAssetCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleAnimationAssetInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AnimationAsset::HandleAnimationAssetInfo(Params);
}
