// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::Input
{
FString HandleAddInputMapping(TSharedPtr<FJsonObject> Params);
FString HandleRemoveInputMapping(TSharedPtr<FJsonObject> Params);
FString HandleGetInputMappings(TSharedPtr<FJsonObject> Params);
}
