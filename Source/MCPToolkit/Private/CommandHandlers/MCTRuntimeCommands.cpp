// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTRuntimeCommands.h"
#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeAI.h"
#include "RuntimeDiagnostics/MCTRuntimeAudio.h"
#include "RuntimeDiagnostics/MCTRuntimeComponents.h"
#include "RuntimeDiagnostics/MCTRuntimeCore.h"
#include "RuntimeDiagnostics/MCTRuntimeEQS.h"
#include "RuntimeDiagnostics/MCTRuntimeFramework.h"
#include "RuntimeDiagnostics/MCTRuntimeGameplay.h"
#include "RuntimeDiagnostics/MCTRuntimeNavigation.h"
#include "RuntimeDiagnostics/MCTRuntimePhysics.h"
#include "RuntimeDiagnostics/MCTRuntimeScheduling.h"
#include "RuntimeDiagnostics/MCTRuntimeStreaming.h"
#include "RuntimeDiagnostics/MCTRuntimeUI.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"

namespace MCPToolkit::CommandHandlers::Runtime
{
namespace
{
FString RunRuntimeDataCommand(
	TFunction<TSharedPtr<FJsonObject>()>&& BuildData,
	const TCHAR* UnavailableError,
	const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, BuildData = MoveTemp(BuildData), UnavailableError]()
	{
		TSharedPtr<FJsonObject> Data = BuildData();
		Promise->SetValue(Data.IsValid() ? CreateSuccessResponse(Data) : CreateErrorResponse(UnavailableError));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}

FString RunRuntimeSuccessCommand(
	TFunction<TSharedPtr<FJsonObject>()>&& BuildData,
	const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, BuildData = MoveTemp(BuildData)]()
	{
		Promise->SetValue(CreateSuccessResponse(BuildData()));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleRuntimeWorldInfo(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeDataCommand([Params]() { return RuntimeDiagnostics::BuildWorldInfo(Params); }, TEXT("Runtime world is not available"), TEXT("Runtime world info timed out"));
}

FString HandleRuntimePlayerList(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeDataCommand([Params]() { return RuntimeDiagnostics::BuildPlayerList(Params); }, TEXT("Runtime world is not available"), TEXT("Runtime player list timed out"));
}

FString HandleRuntimeComponentList(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeDataCommand([Params]() { return RuntimeDiagnostics::BuildComponentList(Params); }, TEXT("Runtime component list target is not available"), TEXT("Runtime component list timed out"));
}

FString HandleRuntimeDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeDataCommand([Params]() { return RuntimeDiagnostics::BuildRuntimeDiagnostics(Params); }, TEXT("Runtime diagnostics world is not available"), TEXT("Runtime diagnostics timed out"));
}

FString HandleRuntimeInputRouting(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildInputRoutingDiagnostics(Params); }, TEXT("Runtime input routing timed out"));
}

FString HandleRuntimeGameplayTagsDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildGameplayTagsDiagnostics(Params); }, TEXT("Runtime gameplay tags diagnostics timed out"));
}

FString HandleRuntimeCommonUIDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildCommonUIDiagnostics(Params); }, TEXT("Runtime CommonUI diagnostics timed out"));
}

FString HandleRuntimeAudioDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildAudioDiagnostics(Params); }, TEXT("Runtime audio diagnostics timed out"));
}

FString HandleRuntimeNavigationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildNavigationDiagnostics(Params); }, TEXT("Runtime navigation diagnostics timed out"));
}

FString HandleRuntimeAssetStreamingDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildAssetStreamingDiagnostics(Params); }, TEXT("Runtime asset streaming diagnostics timed out"));
}

FString HandleRuntimeAsyncLoadDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildAsyncLoadDiagnostics(Params); }, TEXT("Runtime async load diagnostics timed out"));
}

FString HandleRuntimeGameInstanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildGameInstanceDiagnostics(Params); }, TEXT("Runtime game instance diagnostics timed out"));
}

FString HandleRuntimeLevelTravelDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildLevelTravelDiagnostics(Params); }, TEXT("Runtime level travel diagnostics timed out"));
}

FString HandleRuntimeMultiplayerConnectionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildMultiplayerConnectionDiagnostics(Params); }, TEXT("Runtime multiplayer connection diagnostics timed out"));
}

FString HandleRuntimeTickTimerLatentDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildTickTimerLatentDiagnostics(Params); }, TEXT("Runtime tick/timer/latent diagnostics timed out"));
}

FString HandleRuntimeSchedulerPerformanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildSchedulerPerformanceDiagnostics(Params); }, TEXT("Runtime scheduler/performance diagnostics timed out"));
}

FString HandleRuntimePhysicsCollisionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildPhysicsCollisionDiagnostics(Params); }, TEXT("Runtime physics/collision diagnostics timed out"));
}

FString HandleRuntimeReplicationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildReplicationDiagnostics(Params); }, TEXT("Runtime replication diagnostics timed out"));
}

FString HandleRuntimeAbilitySystemDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildAbilitySystemDiagnostics(Params); }, TEXT("Runtime ability system diagnostics timed out"));
}

FString HandleRuntimeAIPerceptionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildAIPerceptionDiagnostics(Params); }, TEXT("Runtime AI perception diagnostics timed out"));
}

FString HandleRuntimeAIControllerDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildAIControllerDiagnostics(Params); }, TEXT("Runtime AI controller diagnostics timed out"));
}

FString HandleRuntimeEQSDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return RunRuntimeSuccessCommand([Params]() { return RuntimeDiagnostics::BuildEQSDiagnostics(Params); }, TEXT("Runtime EQS diagnostics timed out"));
}
}
