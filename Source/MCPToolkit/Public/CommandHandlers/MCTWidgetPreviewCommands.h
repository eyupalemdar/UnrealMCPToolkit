// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::WidgetPreview
{
FString HandleCaptureWidgetPreview(TSharedPtr<FJsonObject> Params);
}
