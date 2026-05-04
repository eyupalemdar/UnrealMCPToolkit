// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTBlueprintComponentCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleBlueprintComponentList(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintComponent::HandleBlueprintComponentList(Params);
}

FString FMCTTcpServer::HandleBlueprintComponentAdd(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintComponent::HandleBlueprintComponentAdd(Params);
}

FString FMCTTcpServer::HandleBlueprintComponentRemove(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintComponent::HandleBlueprintComponentRemove(Params);
}

FString FMCTTcpServer::HandleBlueprintComponentSetProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintComponent::HandleBlueprintComponentSetProperty(Params);
}
