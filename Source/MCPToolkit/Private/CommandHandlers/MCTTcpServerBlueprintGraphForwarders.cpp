// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTBlueprintGraphCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleAddEventNode(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddEventNode(Params);
}

FString FMCTTcpServer::HandleAddCustomEvent(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddCustomEvent(Params);
}

FString FMCTTcpServer::HandleAddFunctionCallNode(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddFunctionCallNode(Params);
}

FString FMCTTcpServer::HandleAddVariableGetNode(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddVariableGetNode(Params);
}

FString FMCTTcpServer::HandleAddVariableSetNode(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddVariableSetNode(Params);
}

FString FMCTTcpServer::HandleAddMakeStructNode(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddMakeStructNode(Params);
}

FString FMCTTcpServer::HandleAddBranchNode(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddBranchNode(Params);
}

FString FMCTTcpServer::HandleAddCallParentFunction(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddCallParentFunction(Params);
}

FString FMCTTcpServer::HandleEnsureFunctionGraph(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleEnsureFunctionGraph(Params);
}

FString FMCTTcpServer::HandleConnectPins(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleConnectPins(Params);
}

FString FMCTTcpServer::HandleSetPinDefault(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleSetPinDefault(Params);
}

FString FMCTTcpServer::HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleRemoveGraphNode(Params);
}

FString FMCTTcpServer::HandleGetGraph(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleGetGraph(Params);
}

FString FMCTTcpServer::HandleListGraphs(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleListGraphs(Params);
}

FString FMCTTcpServer::HandleAddVariable(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleAddVariable(Params);
}

FString FMCTTcpServer::HandleSetVariableDefault(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleSetVariableDefault(Params);
}

FString FMCTTcpServer::HandleRemoveVariable(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleRemoveVariable(Params);
}

FString FMCTTcpServer::HandleGetVariables(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::BlueprintGraph::HandleGetVariables(Params);
}
