// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::Material
{
FString HandleCreateMaterial(TSharedPtr<FJsonObject> Params);
FString HandleSetMaterialProperty(TSharedPtr<FJsonObject> Params);
FString HandleAddExpression(TSharedPtr<FJsonObject> Params);
FString HandleSetExpressionProperty(TSharedPtr<FJsonObject> Params);
FString HandleConnectExpressions(TSharedPtr<FJsonObject> Params);
FString HandleConnectToMaterialProperty(TSharedPtr<FJsonObject> Params);
FString HandleDisconnectInput(TSharedPtr<FJsonObject> Params);
FString HandleRemoveExpression(TSharedPtr<FJsonObject> Params);
FString HandleCompileMaterial(TSharedPtr<FJsonObject> Params);
FString HandleGetMaterialGraph(TSharedPtr<FJsonObject> Params);
FString HandleListExpressionClasses();
FString HandleCreateMaterialInstance(TSharedPtr<FJsonObject> Params);
FString HandleSetInstanceParameter(TSharedPtr<FJsonObject> Params);
FString HandleSaveMaterialInstance(TSharedPtr<FJsonObject> Params);
FString HandleGetMaterialInstanceInfo(TSharedPtr<FJsonObject> Params);
}
