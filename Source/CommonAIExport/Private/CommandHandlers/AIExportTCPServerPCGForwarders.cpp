// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportPCGCommands.h"
#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandlePCGGraphInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::PCG::HandlePCGGraphInfo(Params);
}

FString FAIExportTCPServer::HandlePCGComponentInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::PCG::HandlePCGComponentInfo(Params);
}
