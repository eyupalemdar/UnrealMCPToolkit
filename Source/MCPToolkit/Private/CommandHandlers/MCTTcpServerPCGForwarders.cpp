// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTPCGCommands.h"
#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandlePCGGraphInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::PCG::HandlePCGGraphInfo(Params);
}

FString FMCTTcpServer::HandlePCGComponentInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::PCG::HandlePCGComponentInfo(Params);
}
