// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTFoliageCommands.h"
#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleFoliageInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Foliage::HandleFoliageInfo(Params);
}

FString FMCTTcpServer::HandleFoliageSampleInstances(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Foliage::HandleFoliageSampleInstances(Params);
}

FString FMCTTcpServer::HandleFoliageTypeSettings(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Foliage::HandleFoliageTypeSettings(Params);
}
