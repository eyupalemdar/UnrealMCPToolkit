// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::BlueprintComponent
{
FString HandleBlueprintComponentList(TSharedPtr<FJsonObject> Params);
FString HandleBlueprintComponentAdd(TSharedPtr<FJsonObject> Params);
FString HandleBlueprintComponentRemove(TSharedPtr<FJsonObject> Params);
FString HandleBlueprintComponentSetProperty(TSharedPtr<FJsonObject> Params);
}
