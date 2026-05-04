// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTMaterialCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleCreateMaterial(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleCreateMaterial(Params);
}

FString FMCTTcpServer::HandleSetMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleSetMaterialProperty(Params);
}

FString FMCTTcpServer::HandleAddExpression(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleAddExpression(Params);
}

FString FMCTTcpServer::HandleSetExpressionProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleSetExpressionProperty(Params);
}

FString FMCTTcpServer::HandleConnectExpressions(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleConnectExpressions(Params);
}

FString FMCTTcpServer::HandleConnectToMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleConnectToMaterialProperty(Params);
}

FString FMCTTcpServer::HandleDisconnectInput(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleDisconnectInput(Params);
}

FString FMCTTcpServer::HandleRemoveExpression(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleRemoveExpression(Params);
}

FString FMCTTcpServer::HandleCompileMaterial(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleCompileMaterial(Params);
}

FString FMCTTcpServer::HandleGetMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleGetMaterialGraph(Params);
}

FString FMCTTcpServer::HandleListExpressionClasses()
{
	return MCPToolkit::CommandHandlers::Material::HandleListExpressionClasses();
}

FString FMCTTcpServer::HandleCreateMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleCreateMaterialInstance(Params);
}

FString FMCTTcpServer::HandleSetInstanceParameter(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleSetInstanceParameter(Params);
}

FString FMCTTcpServer::HandleSaveMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleSaveMaterialInstance(Params);
}

FString FMCTTcpServer::HandleGetMaterialInstanceInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Material::HandleGetMaterialInstanceInfo(Params);
}
