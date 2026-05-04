// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::PCG
{
FString HandlePCGGraphInfo(TSharedPtr<FJsonObject> Params);
FString HandlePCGComponentInfo(TSharedPtr<FJsonObject> Params);
}
