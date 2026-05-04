// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportMaterialCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleCreateMaterial(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleCreateMaterial(Params);
}

FString FAIExportTCPServer::HandleSetMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSetMaterialProperty(Params);
}

FString FAIExportTCPServer::HandleAddExpression(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleAddExpression(Params);
}

FString FAIExportTCPServer::HandleSetExpressionProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSetExpressionProperty(Params);
}

FString FAIExportTCPServer::HandleConnectExpressions(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleConnectExpressions(Params);
}

FString FAIExportTCPServer::HandleConnectToMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleConnectToMaterialProperty(Params);
}

FString FAIExportTCPServer::HandleDisconnectInput(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleDisconnectInput(Params);
}

FString FAIExportTCPServer::HandleRemoveExpression(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleRemoveExpression(Params);
}

FString FAIExportTCPServer::HandleCompileMaterial(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleCompileMaterial(Params);
}

FString FAIExportTCPServer::HandleGetMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleGetMaterialGraph(Params);
}

FString FAIExportTCPServer::HandleListExpressionClasses()
{
	return CommonAIExport::CommandHandlers::Material::HandleListExpressionClasses();
}

FString FAIExportTCPServer::HandleCreateMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleCreateMaterialInstance(Params);
}

FString FAIExportTCPServer::HandleSetInstanceParameter(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSetInstanceParameter(Params);
}

FString FAIExportTCPServer::HandleSaveMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSaveMaterialInstance(Params);
}

FString FAIExportTCPServer::HandleGetMaterialInstanceInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleGetMaterialInstanceInfo(Params);
}
