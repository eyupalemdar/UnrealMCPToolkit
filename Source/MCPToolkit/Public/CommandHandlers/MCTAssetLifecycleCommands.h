// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::AssetLifecycle
{
FString HandleReloadAsset(TSharedPtr<FJsonObject> Params);
}
