// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTSplineCommands.h"
#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleSplineActorCreate(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Spline::HandleSplineActorCreate(Params);
}

FString FMCTTcpServer::HandleSplineComponentInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Spline::HandleSplineComponentInfo(Params);
}

FString FMCTTcpServer::HandleSplineComponentSetPoints(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Spline::HandleSplineComponentSetPoints(Params);
}
