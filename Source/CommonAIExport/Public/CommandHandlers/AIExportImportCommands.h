// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::Import
{
FString HandleImportTexture(TSharedPtr<FJsonObject> Params);
FString HandleImportFont(TSharedPtr<FJsonObject> Params);
}
