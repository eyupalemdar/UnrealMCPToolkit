// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportBlueprintComponentCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleBlueprintComponentList(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintComponent::HandleBlueprintComponentList(Params);
}

FString FAIExportTCPServer::HandleBlueprintComponentAdd(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintComponent::HandleBlueprintComponentAdd(Params);
}

FString FAIExportTCPServer::HandleBlueprintComponentRemove(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintComponent::HandleBlueprintComponentRemove(Params);
}

FString FAIExportTCPServer::HandleBlueprintComponentSetProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintComponent::HandleBlueprintComponentSetProperty(Params);
}
