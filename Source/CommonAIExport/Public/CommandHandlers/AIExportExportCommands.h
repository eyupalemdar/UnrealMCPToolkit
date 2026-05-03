// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::Export
{
FString HandleExportWidget(TSharedPtr<FJsonObject> Params);
FString HandleExportBlueprint(TSharedPtr<FJsonObject> Params);
FString HandleListSupportedTypes();
}
