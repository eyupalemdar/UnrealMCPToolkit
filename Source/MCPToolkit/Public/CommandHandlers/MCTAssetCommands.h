// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::Asset
{
FString HandleCreateAsset(TSharedPtr<FJsonObject> Params);
FString HandleSetAssetProperty(TSharedPtr<FJsonObject> Params);
FString HandleGetAssetProperties(TSharedPtr<FJsonObject> Params);
FString HandleAssetExists(TSharedPtr<FJsonObject> Params);
FString HandleScanAssetPaths(TSharedPtr<FJsonObject> Params);
FString HandleAssetSearch(TSharedPtr<FJsonObject> Params);
FString HandleAssetValidateLight(TSharedPtr<FJsonObject> Params);
FString HandleSaveAsset(TSharedPtr<FJsonObject> Params);
FString HandleRenameAsset(TSharedPtr<FJsonObject> Params);
FString HandleDuplicateAsset(TSharedPtr<FJsonObject> Params);
FString HandleGetReferencers(TSharedPtr<FJsonObject> Params);
FString HandleGetDependencies(TSharedPtr<FJsonObject> Params);
FString HandleDeleteAsset(TSharedPtr<FJsonObject> Params);
FString HandleListRedirectors(TSharedPtr<FJsonObject> Params);
FString HandleFixupRedirectors(TSharedPtr<FJsonObject> Params);
}
