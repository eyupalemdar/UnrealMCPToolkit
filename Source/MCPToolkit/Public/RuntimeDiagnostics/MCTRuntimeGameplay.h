// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> BuildGameplayTagsDiagnostics(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildAbilitySystemDiagnostics(TSharedPtr<FJsonObject> Params);
}
