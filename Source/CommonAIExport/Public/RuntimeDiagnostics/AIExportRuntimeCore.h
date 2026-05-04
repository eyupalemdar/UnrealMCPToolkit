// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> BuildWorldInfo(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildPlayerList(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildRuntimeDiagnostics(TSharedPtr<FJsonObject> Params);
}
