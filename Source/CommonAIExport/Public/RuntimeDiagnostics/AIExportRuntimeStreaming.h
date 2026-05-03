// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> BuildAssetStreamingDiagnostics(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildAsyncLoadDiagnostics(TSharedPtr<FJsonObject> Params);
}
