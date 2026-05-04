// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::Landscape
{
FString HandleLandscapeInfo(TSharedPtr<FJsonObject> Params);
FString HandleLandscapeSampleHeight(TSharedPtr<FJsonObject> Params);
}
