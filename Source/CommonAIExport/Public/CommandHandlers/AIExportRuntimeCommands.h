// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace CommonAIExport::CommandHandlers::Runtime
{
FString HandleRuntimeWorldInfo(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimePlayerList(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeComponentList(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeInputRouting(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeGameplayTagsDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeCommonUIDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeAudioDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeNavigationDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeAssetStreamingDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeAsyncLoadDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeGameInstanceDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeLevelTravelDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeMultiplayerConnectionDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeTickTimerLatentDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeSchedulerPerformanceDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimePhysicsCollisionDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeReplicationDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeAbilitySystemDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeAIPerceptionDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeAIControllerDiagnostics(TSharedPtr<class FJsonObject> Params);
FString HandleRuntimeEQSDiagnostics(TSharedPtr<class FJsonObject> Params);
}
