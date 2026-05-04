// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::RuntimeDiagnostics
{
TSharedPtr<FJsonObject> BuildGameInstanceDiagnostics(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildLevelTravelDiagnostics(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildMultiplayerConnectionDiagnostics(TSharedPtr<FJsonObject> Params);
TSharedPtr<FJsonObject> BuildReplicationDiagnostics(TSharedPtr<FJsonObject> Params);
}
