// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTAnimBlueprintCommands.h"
#include "CommandHandlers/MCTAssetCommands.h"
#include "CommandHandlers/MCTAssetLifecycleCommands.h"
#include "CommandHandlers/MCTExportCDOCommands.h"
#include "CommandHandlers/MCTDataTableCommands.h"
#include "CommandHandlers/MCTDataAssetCommands.h"
#include "CommandHandlers/MCTImportCommands.h"
#include "CommandHandlers/MCTInputCommands.h"
#include "CommandHandlers/MCTReflectionCommands.h"
#include "CommandHandlers/MCTWidgetPreviewCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleSaveDataAsset(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::DataAsset::HandleSaveDataAsset(Params);
}

FString FMCTTcpServer::HandleImportTexture(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Import::HandleImportTexture(Params);
}

FString FMCTTcpServer::HandleImportFont(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Import::HandleImportFont(Params);
}

FString FMCTTcpServer::HandleSetCDOProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::CDO::HandleSetCDOProperty(Params);
}

FString FMCTTcpServer::HandleGetCDOProperties(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::CDO::HandleGetCDOProperties(Params);
}

FString FMCTTcpServer::HandleAddCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::CDO::HandleAddCDOArrayElement(Params);
}

FString FMCTTcpServer::HandleSetCDOArrayElementProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::CDO::HandleSetCDOArrayElementProperty(Params);
}

FString FMCTTcpServer::HandleRemoveCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::CDO::HandleRemoveCDOArrayElement(Params);
}

FString FMCTTcpServer::HandleGetCDOArrayLength(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::CDO::HandleGetCDOArrayLength(Params);
}

FString FMCTTcpServer::HandleObjectQuery(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleObjectQuery(Params);
}

FString FMCTTcpServer::HandleObjectGetProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleObjectGetProperty(Params);
}

FString FMCTTcpServer::HandleObjectSetProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleObjectSetProperty(Params);
}

FString FMCTTcpServer::HandleObjectCallFunction(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleObjectCallFunction(Params);
}

FString FMCTTcpServer::HandleReflectClass(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleReflectClass(Params);
}

FString FMCTTcpServer::HandleReflectStruct(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleReflectStruct(Params);
}

FString FMCTTcpServer::HandleReflectEnum(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleReflectEnum(Params);
}

FString FMCTTcpServer::HandleListClasses(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleListClasses(Params);
}

FString FMCTTcpServer::HandleListGameplayTags(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Reflection::HandleListGameplayTags(Params);
}

FString FMCTTcpServer::HandleCreateAsset(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleCreateAsset(Params);
}

FString FMCTTcpServer::HandleSetAssetProperty(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleSetAssetProperty(Params);
}

FString FMCTTcpServer::HandleGetAssetProperties(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleGetAssetProperties(Params);
}

FString FMCTTcpServer::HandleAssetExists(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleAssetExists(Params);
}

FString FMCTTcpServer::HandleScanAssetPaths(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleScanAssetPaths(Params);
}

FString FMCTTcpServer::HandleAssetSearch(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleAssetSearch(Params);
}

FString FMCTTcpServer::HandleAssetValidateLight(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleAssetValidateLight(Params);
}

FString FMCTTcpServer::HandleSaveAsset(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleSaveAsset(Params);
}

FString FMCTTcpServer::HandleRenameAsset(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleRenameAsset(Params);
}

FString FMCTTcpServer::HandleGetReferencers(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleGetReferencers(Params);
}

FString FMCTTcpServer::HandleGetDependencies(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleGetDependencies(Params);
}

FString FMCTTcpServer::HandleDeleteAsset(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleDeleteAsset(Params);
}

FString FMCTTcpServer::HandleListRedirectors(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleListRedirectors(Params);
}

FString FMCTTcpServer::HandleFixupRedirectors(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Asset::HandleFixupRedirectors(Params);
}

FString FMCTTcpServer::HandleAddInputMapping(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Input::HandleAddInputMapping(Params);
}

FString FMCTTcpServer::HandleRemoveInputMapping(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Input::HandleRemoveInputMapping(Params);
}

FString FMCTTcpServer::HandleGetInputMappings(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Input::HandleGetInputMappings(Params);
}

FString FMCTTcpServer::HandleCreateAnimBlueprint(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AnimBlueprint::HandleCreateAnimBlueprint(Params);
}

FString FMCTTcpServer::HandleGetAnimBlueprintInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AnimBlueprint::HandleGetAnimBlueprintInfo(Params);
}

FString FMCTTcpServer::HandleCaptureWidgetPreview(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::WidgetPreview::HandleCaptureWidgetPreview(Params);
}

FString FMCTTcpServer::HandleReloadAsset(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::AssetLifecycle::HandleReloadAsset(Params);
}

FString FMCTTcpServer::HandleCreateDataTable(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::DataTable::HandleCreateDataTable(Params);
}

FString FMCTTcpServer::HandleGetDataTableInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::DataTable::HandleGetDataTableInfo(Params);
}

FString FMCTTcpServer::HandleReadDataTableRows(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::DataTable::HandleReadDataTableRows(Params);
}

FString FMCTTcpServer::HandleAddDataTableRow(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::DataTable::HandleAddDataTableRow(Params);
}

FString FMCTTcpServer::HandleRemoveDataTableRow(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::DataTable::HandleRemoveDataTableRow(Params);
}

FString FMCTTcpServer::HandleImportDataTableCsv(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::DataTable::HandleImportDataTableCsv(Params);
}
