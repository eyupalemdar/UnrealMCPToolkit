// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::AnimBlueprint
{
FString HandleCreateAnimBlueprint(TSharedPtr<FJsonObject> Params);
FString HandleGetAnimBlueprintInfo(TSharedPtr<FJsonObject> Params);
}
