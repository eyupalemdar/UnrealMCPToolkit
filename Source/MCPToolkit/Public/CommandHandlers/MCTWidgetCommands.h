// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::Widget
{
FString HandleCreateWidgetBlueprint(TSharedPtr<FJsonObject> Params);
FString HandleAddWidget(TSharedPtr<FJsonObject> Params);
FString HandleRemoveWidget(TSharedPtr<FJsonObject> Params);
FString HandleMoveWidget(TSharedPtr<FJsonObject> Params);
FString HandleSetWidgetProperty(TSharedPtr<FJsonObject> Params);
FString HandleSetSlotProperty(TSharedPtr<FJsonObject> Params);
FString HandleSetCanvasSlotLayout(TSharedPtr<FJsonObject> Params);
FString HandleSetWidgetProperties(TSharedPtr<FJsonObject> Params);
FString HandleReparentBlueprint(TSharedPtr<FJsonObject> Params);
FString HandleCompileAndSave(TSharedPtr<FJsonObject> Params);
FString HandleGetWidgetTree(TSharedPtr<FJsonObject> Params);
FString HandleListWidgetClasses();
}
