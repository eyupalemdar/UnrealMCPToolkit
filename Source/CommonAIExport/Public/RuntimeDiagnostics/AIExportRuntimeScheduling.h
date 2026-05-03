// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> BuildTickTimerLatentDiagnostics(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildSchedulerPerformanceDiagnostics(TSharedPtr<FJsonObject> Params);
}
