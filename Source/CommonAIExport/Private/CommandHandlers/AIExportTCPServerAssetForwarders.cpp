// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportAnimBlueprintCommands.h"
#include "CommandHandlers/AIExportAssetCommands.h"
#include "CommandHandlers/AIExportAssetLifecycleCommands.h"
#include "CommandHandlers/AIExportCDOCommands.h"
#include "CommandHandlers/AIExportDataAssetCommands.h"
#include "CommandHandlers/AIExportImportCommands.h"
#include "CommandHandlers/AIExportInputCommands.h"
#include "CommandHandlers/AIExportWidgetPreviewCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleSaveDataAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::DataAsset::HandleSaveDataAsset(Params);
}

FString FAIExportTCPServer::HandleImportTexture(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Import::HandleImportTexture(Params);
}

FString FAIExportTCPServer::HandleImportFont(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Import::HandleImportFont(Params);
}

FString FAIExportTCPServer::HandleSetCDOProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleSetCDOProperty(Params);
}

FString FAIExportTCPServer::HandleGetCDOProperties(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleGetCDOProperties(Params);
}

FString FAIExportTCPServer::HandleAddCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleAddCDOArrayElement(Params);
}

FString FAIExportTCPServer::HandleSetCDOArrayElementProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleSetCDOArrayElementProperty(Params);
}

FString FAIExportTCPServer::HandleRemoveCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleRemoveCDOArrayElement(Params);
}

FString FAIExportTCPServer::HandleGetCDOArrayLength(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleGetCDOArrayLength(Params);
}

FString FAIExportTCPServer::HandleCreateAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleCreateAsset(Params);
}

FString FAIExportTCPServer::HandleSetAssetProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleSetAssetProperty(Params);
}

FString FAIExportTCPServer::HandleGetAssetProperties(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleGetAssetProperties(Params);
}

FString FAIExportTCPServer::HandleAssetExists(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleAssetExists(Params);
}

FString FAIExportTCPServer::HandleScanAssetPaths(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleScanAssetPaths(Params);
}

FString FAIExportTCPServer::HandleAssetSearch(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleAssetSearch(Params);
}

FString FAIExportTCPServer::HandleAssetValidateLight(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleAssetValidateLight(Params);
}

FString FAIExportTCPServer::HandleSaveAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleSaveAsset(Params);
}

FString FAIExportTCPServer::HandleRenameAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleRenameAsset(Params);
}

FString FAIExportTCPServer::HandleGetReferencers(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleGetReferencers(Params);
}

FString FAIExportTCPServer::HandleGetDependencies(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleGetDependencies(Params);
}

FString FAIExportTCPServer::HandleDeleteAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleDeleteAsset(Params);
}

FString FAIExportTCPServer::HandleListRedirectors(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleListRedirectors(Params);
}

FString FAIExportTCPServer::HandleFixupRedirectors(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleFixupRedirectors(Params);
}

FString FAIExportTCPServer::HandleAddInputMapping(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Input::HandleAddInputMapping(Params);
}

FString FAIExportTCPServer::HandleRemoveInputMapping(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Input::HandleRemoveInputMapping(Params);
}

FString FAIExportTCPServer::HandleGetInputMappings(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Input::HandleGetInputMappings(Params);
}

FString FAIExportTCPServer::HandleCreateAnimBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AnimBlueprint::HandleCreateAnimBlueprint(Params);
}

FString FAIExportTCPServer::HandleGetAnimBlueprintInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AnimBlueprint::HandleGetAnimBlueprintInfo(Params);
}

FString FAIExportTCPServer::HandleCaptureWidgetPreview(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::WidgetPreview::HandleCaptureWidgetPreview(Params);
}

FString FAIExportTCPServer::HandleReloadAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AssetLifecycle::HandleReloadAsset(Params);
}
