// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportSplineCommands.h"
#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleSplineActorCreate(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Spline::HandleSplineActorCreate(Params);
}

FString FAIExportTCPServer::HandleSplineComponentInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Spline::HandleSplineComponentInfo(Params);
}

FString FAIExportTCPServer::HandleSplineComponentSetPoints(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Spline::HandleSplineComponentSetPoints(Params);
}
