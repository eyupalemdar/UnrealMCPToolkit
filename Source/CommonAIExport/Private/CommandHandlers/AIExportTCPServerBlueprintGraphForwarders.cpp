// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportBlueprintGraphCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleAddEventNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddEventNode(Params);
}

FString FAIExportTCPServer::HandleAddCustomEvent(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddCustomEvent(Params);
}

FString FAIExportTCPServer::HandleAddFunctionCallNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddFunctionCallNode(Params);
}

FString FAIExportTCPServer::HandleAddVariableGetNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddVariableGetNode(Params);
}

FString FAIExportTCPServer::HandleAddVariableSetNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddVariableSetNode(Params);
}

FString FAIExportTCPServer::HandleAddMakeStructNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddMakeStructNode(Params);
}

FString FAIExportTCPServer::HandleAddBranchNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddBranchNode(Params);
}

FString FAIExportTCPServer::HandleAddCallParentFunction(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddCallParentFunction(Params);
}

FString FAIExportTCPServer::HandleEnsureFunctionGraph(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleEnsureFunctionGraph(Params);
}

FString FAIExportTCPServer::HandleConnectPins(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleConnectPins(Params);
}

FString FAIExportTCPServer::HandleSetPinDefault(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleSetPinDefault(Params);
}

FString FAIExportTCPServer::HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleRemoveGraphNode(Params);
}

FString FAIExportTCPServer::HandleGetGraph(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleGetGraph(Params);
}

FString FAIExportTCPServer::HandleListGraphs(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleListGraphs(Params);
}

FString FAIExportTCPServer::HandleAddVariable(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddVariable(Params);
}

FString FAIExportTCPServer::HandleSetVariableDefault(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleSetVariableDefault(Params);
}

FString FAIExportTCPServer::HandleRemoveVariable(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleRemoveVariable(Params);
}

FString FAIExportTCPServer::HandleGetVariables(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleGetVariables(Params);
}
