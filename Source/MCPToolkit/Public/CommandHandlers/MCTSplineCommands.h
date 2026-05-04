// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::Spline
{
FString HandleSplineActorCreate(TSharedPtr<FJsonObject> Params);
FString HandleSplineComponentInfo(TSharedPtr<FJsonObject> Params);
FString HandleSplineComponentSetPoints(TSharedPtr<FJsonObject> Params);
}
