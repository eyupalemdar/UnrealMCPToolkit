// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::Foliage
{
FString HandleFoliageInfo(TSharedPtr<FJsonObject> Params);
FString HandleFoliageSampleInstances(TSharedPtr<FJsonObject> Params);
FString HandleFoliageTypeSettings(TSharedPtr<FJsonObject> Params);
}
