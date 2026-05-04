// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> BuildPhysicsCollisionDiagnostics(TSharedPtr<FJsonObject> Params);
}
