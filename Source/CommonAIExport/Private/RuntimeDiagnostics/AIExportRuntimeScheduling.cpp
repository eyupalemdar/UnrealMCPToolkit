// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/AIExportRuntimeScheduling.h"

#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreGlobals.h"
#include "Dom/JsonValue.h"
#include "Engine/LatentActionManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "TimerManager.h"

namespace CommonAIExport::RuntimeDiagnostics
{
namespace
{
TSharedPtr<FJsonObject> BuildRuntimeWorldTimeJson(UWorld* World)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), World != nullptr);
	if (!World)
	{
		return Data;
	}

	Data->SetNumberField(TEXT("time_seconds"), World->TimeSeconds);
	Data->SetNumberField(TEXT("unpaused_time_seconds"), World->UnpausedTimeSeconds);
	Data->SetNumberField(TEXT("real_time_seconds"), World->RealTimeSeconds);
	Data->SetNumberField(TEXT("audio_time_seconds"), World->AudioTimeSeconds);
	Data->SetNumberField(TEXT("delta_time_seconds"), World->DeltaTimeSeconds);
	Data->SetNumberField(TEXT("delta_real_time_seconds"), World->DeltaRealTimeSeconds);
	Data->SetNumberField(TEXT("delta_seconds"), World->GetDeltaSeconds());
	Data->SetNumberField(TEXT("pause_delay"), World->PauseDelay);
	Data->SetBoolField(TEXT("paused"), World->IsPaused());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeWorldSettingsTimeJson(AWorldSettings* WorldSettings)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(WorldSettings);
	if (!WorldSettings)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("allow_time_dilation"), WorldSettings->bAllowTimeDilation != 0);
	Data->SetNumberField(TEXT("time_dilation"), WorldSettings->TimeDilation);
	Data->SetNumberField(TEXT("cinematic_time_dilation"), WorldSettings->CinematicTimeDilation);
	Data->SetNumberField(TEXT("demo_play_time_dilation"), WorldSettings->DemoPlayTimeDilation);
	Data->SetNumberField(TEXT("effective_time_dilation"), WorldSettings->GetEffectiveTimeDilation());
	Data->SetNumberField(TEXT("min_global_time_dilation"), WorldSettings->MinGlobalTimeDilation);
	Data->SetNumberField(TEXT("max_global_time_dilation"), WorldSettings->MaxGlobalTimeDilation);
	Data->SetNumberField(TEXT("min_cinematic_time_dilation"), WorldSettings->MinCinematicTimeDilation);
	Data->SetNumberField(TEXT("max_cinematic_time_dilation"), WorldSettings->MaxCinematicTimeDilation);
	Data->SetObjectField(TEXT("pauser_player_state"), BuildObjectReferenceJson(WorldSettings->GetPauserPlayerState()));
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeTimerManagerJson(UWorld* World)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), World != nullptr);
	Data->SetBoolField(TEXT("active_timer_enumeration_available"), false);

	TArray<TSharedPtr<FJsonValue>> Limitations;
	Limitations.Add(MakeShared<FJsonValueString>(TEXT("FTimerManager does not expose active/paused/pending timer enumeration through public runtime API")));
	Data->SetArrayField(TEXT("limitations"), Limitations);

	if (!World)
	{
		return Data;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	Data->SetBoolField(TEXT("has_been_ticked_this_frame"), TimerManager.HasBeenTickedThisFrame());
	return Data;
}

FString TickGroupToString(TEnumAsByte<ETickingGroup> TickGroup)
{
	if (const UEnum* TickGroupEnum = StaticEnum<ETickingGroup>())
	{
		return TickGroupEnum->GetNameStringByValue(static_cast<int64>(TickGroup.GetValue()));
	}
	return FString::Printf(TEXT("TickGroup_%d"), static_cast<int32>(TickGroup.GetValue()));
}

TSharedPtr<FJsonObject> BuildRuntimeTickFunctionJson(const FTickFunction& TickFunction)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("can_ever_tick"), TickFunction.bCanEverTick != 0);
	Data->SetBoolField(TEXT("start_with_tick_enabled"), TickFunction.bStartWithTickEnabled != 0);
	Data->SetBoolField(TEXT("enabled"), TickFunction.IsTickFunctionEnabled());
	Data->SetBoolField(TEXT("registered"), TickFunction.IsTickFunctionRegistered());
	Data->SetBoolField(TEXT("tick_even_when_paused"), TickFunction.bTickEvenWhenPaused != 0);
	Data->SetBoolField(TEXT("allow_tick_on_dedicated_server"), TickFunction.bAllowTickOnDedicatedServer != 0);
	Data->SetBoolField(TEXT("allow_tick_batching"), TickFunction.bAllowTickBatching != 0);
	Data->SetBoolField(TEXT("high_priority"), TickFunction.bHighPriority != 0);
	Data->SetBoolField(TEXT("run_on_any_thread"), TickFunction.bRunOnAnyThread != 0);
	Data->SetBoolField(TEXT("run_transactionally"), TickFunction.bRunTransactionally != 0);
	Data->SetBoolField(TEXT("dispatch_manually"), TickFunction.bDispatchManually != 0);
	Data->SetBoolField(TEXT("can_dispatch_manually_now"), TickFunction.CanDispatchManually());
	Data->SetNumberField(TEXT("tick_interval"), TickFunction.TickInterval);
	Data->SetStringField(TEXT("tick_group"), TickGroupToString(TickFunction.TickGroup));
	Data->SetStringField(TEXT("end_tick_group"), TickGroupToString(TickFunction.EndTickGroup));
	Data->SetStringField(TEXT("actual_tick_group"), TickGroupToString(TickFunction.GetActualTickGroup()));
	Data->SetStringField(TEXT("actual_end_tick_group"), TickGroupToString(TickFunction.GetActualEndTickGroup()));
	Data->SetNumberField(TEXT("prerequisite_count"), TickFunction.GetPrerequisites().Num());
	Data->SetNumberField(TEXT("last_tick_game_time"), TickFunction.GetLastTickGameTime());
	return Data;
}

void IncrementTickGroupCount(TArray<int32>& Counts, TEnumAsByte<ETickingGroup> TickGroup)
{
	const int32 Index = static_cast<int32>(TickGroup.GetValue());
	if (Counts.IsValidIndex(Index))
	{
		++Counts[Index];
	}
}

TArray<TSharedPtr<FJsonValue>> BuildTickGroupCountsJson(const TArray<int32>& Counts)
{
	TArray<TSharedPtr<FJsonValue>> GroupsJson;
	for (int32 Index = 0; Index < Counts.Num(); ++Index)
	{
		const int32 Count = Counts[Index];
		if (Count <= 0)
		{
			continue;
		}

		TSharedPtr<FJsonObject> GroupJson = MakeShared<FJsonObject>();
		GroupJson->SetStringField(TEXT("tick_group"), TickGroupToString(static_cast<ETickingGroup>(Index)));
		GroupJson->SetNumberField(TEXT("count"), Count);
		GroupsJson.Add(MakeShared<FJsonValueObject>(GroupJson));
	}
	return GroupsJson;
}

TSharedPtr<FJsonObject> BuildRuntimeAppFrameJson(double HitchThresholdMs)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const double AppDeltaMs = FApp::GetDeltaTime() * 1000.0;
	const double IdleMs = FApp::GetIdleTime() * 1000.0;
	const double IdleOvershootMs = FApp::GetIdleTimeOvershoot() * 1000.0;
	Data->SetNumberField(TEXT("current_time_seconds"), FApp::GetCurrentTime());
	Data->SetNumberField(TEXT("last_time_seconds"), FApp::GetLastTime());
	Data->SetNumberField(TEXT("app_delta_time_seconds"), FApp::GetDeltaTime());
	Data->SetNumberField(TEXT("app_delta_time_ms"), AppDeltaMs);
	Data->SetNumberField(TEXT("idle_time_seconds"), FApp::GetIdleTime());
	Data->SetNumberField(TEXT("idle_time_ms"), IdleMs);
	Data->SetNumberField(TEXT("idle_time_overshoot_seconds"), FApp::GetIdleTimeOvershoot());
	Data->SetNumberField(TEXT("idle_time_overshoot_ms"), IdleOvershootMs);
	Data->SetNumberField(TEXT("game_time_seconds"), FApp::GetGameTime());
	Data->SetNumberField(TEXT("fixed_delta_time_seconds"), FApp::GetFixedDeltaTime());
	Data->SetBoolField(TEXT("use_fixed_time_step"), FApp::UseFixedTimeStep());
	Data->SetBoolField(TEXT("benchmarking"), FApp::IsBenchmarking());
	Data->SetBoolField(TEXT("has_focus"), FApp::HasFocus());
	Data->SetBoolField(TEXT("app_delta_exceeds_hitch_threshold"), AppDeltaMs >= HitchThresholdMs);
	Data->SetNumberField(TEXT("hitch_threshold_ms"), HitchThresholdMs);
	Data->SetNumberField(TEXT("frame_counter"), static_cast<double>(GFrameCounter));
	Data->SetNumberField(TEXT("frame_counter_render_thread"), static_cast<double>(GFrameCounterRenderThread));
	Data->SetNumberField(TEXT("frame_number"), static_cast<double>(GFrameNumber));
	Data->SetNumberField(TEXT("frame_number_render_thread"), static_cast<double>(GFrameNumberRenderThread));
	Data->SetNumberField(TEXT("platform_seconds"), FPlatformTime::Seconds());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeTaskGraphJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const bool bTaskGraphRunning = FTaskGraphInterface::IsRunning();
	Data->SetBoolField(TEXT("running"), bTaskGraphRunning);
	Data->SetBoolField(TEXT("multithread_estimate"), FPlatformProcess::SupportsMultithreading() && FApp::ShouldUseThreadingForPerformance());
	Data->SetBoolField(TEXT("is_in_game_thread"), IsInGameThread());
	if (!bTaskGraphRunning)
	{
		return Data;
	}

	FTaskGraphInterface& TaskGraph = FTaskGraphInterface::Get();
	Data->SetBoolField(TEXT("current_thread_known"), TaskGraph.IsCurrentThreadKnown());
	Data->SetNumberField(TEXT("current_thread"), static_cast<int32>(TaskGraph.GetCurrentThreadIfKnown()));
	Data->SetNumberField(TEXT("worker_threads_per_priority_set"), TaskGraph.GetNumWorkerThreads());
	Data->SetNumberField(TEXT("foreground_worker_threads"), TaskGraph.GetNumForegroundThreads());
	Data->SetNumberField(TEXT("background_worker_threads"), TaskGraph.GetNumBackgroundThreads());
	Data->SetBoolField(TEXT("game_thread_processing_tasks"), TaskGraph.IsThreadProcessingTasks(ENamedThreads::GameThread));
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeThreadingJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("platform_supports_multithreading"), FPlatformProcess::SupportsMultithreading());
	Data->SetBoolField(TEXT("should_use_threading_for_performance"), FApp::ShouldUseThreadingForPerformance());
	Data->SetBoolField(TEXT("is_multithread_server"), FApp::IsMultithreadServer());
	Data->SetNumberField(TEXT("physical_core_count"), FPlatformMisc::NumberOfCores());
	Data->SetNumberField(TEXT("logical_core_count"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	return Data;
}


UObject* FindRuntimeObjectForLatentDiagnostics(
	UWorld* World,
	const FString& ObjectPath,
	const FString& ActorPath,
	const FString& ActorLabel,
	const FString& ActorName)
{
	if (!World)
	{
		return nullptr;
	}

	const bool bActorTargetRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
	if (bActorTargetRequested)
	{
		if (AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName))
		{
			return Actor;
		}
	}

	if (!ObjectPath.IsEmpty())
	{
		return FindObject<UObject>(nullptr, *ObjectPath);
	}

	return nullptr;
}

}

TSharedPtr<FJsonObject> BuildTickTimerLatentDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ObjectPath;
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	bool bIncludeLatentActions = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("object_path"), ObjectPath);
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetBoolField(TEXT("include_latent_actions"), bIncludeLatentActions);
	}
	ObjectPath.TrimStartAndEndInline();
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	const int32 LatentActionLimit = ReadClampedIntField(Params, TEXT("latent_action_limit"), 50, 0, 500);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			return Data;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; tick/timer/latent diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetObjectField(TEXT("time"), BuildRuntimeWorldTimeJson(World));
		Data->SetObjectField(TEXT("world_settings"), BuildRuntimeWorldSettingsTimeJson(World->GetWorldSettings(false, false)));
		Data->SetObjectField(TEXT("timer_manager"), BuildRuntimeTimerManagerJson(World));

		TArray<TSharedPtr<FJsonValue>> Limitations;
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("FTimerManager public API only exposes whether it has ticked this frame; active timer lists are private")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("FLatentActionManager public API can query a specific target object but does not expose global latent action enumeration")));
		Data->SetArrayField(TEXT("limitations"), Limitations);

		const bool bTargetRequested = !ObjectPath.IsEmpty() || !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		UObject* TargetObject = FindRuntimeObjectForLatentDiagnostics(World, ObjectPath, ActorPath, ActorLabel, ActorName);

		TSharedPtr<FJsonObject> LatentJson = MakeShared<FJsonObject>();
		LatentJson->SetBoolField(TEXT("include_latent_actions"), bIncludeLatentActions);
		LatentJson->SetNumberField(TEXT("latent_action_limit"), LatentActionLimit);
		LatentJson->SetStringField(TEXT("object_path"), ObjectPath);
		LatentJson->SetStringField(TEXT("actor_path"), ActorPath);
		LatentJson->SetStringField(TEXT("actor_label"), ActorLabel);
		LatentJson->SetStringField(TEXT("actor_name"), ActorName);
		LatentJson->SetBoolField(TEXT("target_requested"), bTargetRequested);
		LatentJson->SetBoolField(TEXT("target_found"), TargetObject != nullptr);
		LatentJson->SetBoolField(TEXT("global_enumeration_available"), false);
#if WITH_EDITOR
		LatentJson->SetBoolField(TEXT("editor_descriptions_available"), true);
#else
		LatentJson->SetBoolField(TEXT("editor_descriptions_available"), false);
#endif

		if (!bTargetRequested)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("No latent action target specified; Unreal does not expose global latent action enumeration through public API")));
		}
		else if (!TargetObject)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("No latent action target matched the requested object or actor fields")));
		}

		TArray<TSharedPtr<FJsonValue>> LatentActionsJson;
		if (TargetObject)
		{
			LatentJson->SetObjectField(TEXT("target"), BuildObjectReferenceJson(TargetObject));
			const bool bTargetWorldMatches = TargetObject == World || TargetObject->GetWorld() == World || TargetObject->IsIn(World);
			LatentJson->SetBoolField(TEXT("target_world_matches"), bTargetWorldMatches);
			if (!bTargetWorldMatches)
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Latent action target was found but is not owned by the selected world")));
			}

			FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
			const int32 ActionCount = LatentActionManager.GetNumActionsForObject(TWeakObjectPtr<UObject>(TargetObject));
			LatentJson->SetNumberField(TEXT("action_count"), ActionCount);

			if (bIncludeLatentActions)
			{
#if WITH_EDITOR
				TSet<int32> UUIDSet;
				LatentActionManager.GetActiveUUIDs(TargetObject, UUIDSet);
				TArray<int32> UUIDs = UUIDSet.Array();
				UUIDs.Sort();
				for (const int32 UUID : UUIDs)
				{
					if (LatentActionsJson.Num() >= LatentActionLimit)
					{
						break;
					}
					TSharedPtr<FJsonObject> ActionJson = MakeShared<FJsonObject>();
					ActionJson->SetNumberField(TEXT("uuid"), UUID);
					ActionJson->SetStringField(TEXT("description"), LatentActionManager.GetDescription(TargetObject, UUID));
					LatentActionsJson.Add(MakeShared<FJsonValueObject>(ActionJson));
				}
				LatentJson->SetNumberField(TEXT("active_uuid_count"), UUIDs.Num());
				LatentJson->SetBoolField(TEXT("actions_truncated"), UUIDs.Num() > LatentActionsJson.Num());
#else
				LatentJson->SetNumberField(TEXT("active_uuid_count"), ActionCount);
				LatentJson->SetBoolField(TEXT("actions_truncated"), ActionCount > 0);
				if (ActionCount > 0)
				{
					Warnings.Add(MakeShared<FJsonValueString>(TEXT("Latent action UUID descriptions are editor-only and unavailable in this build")));
				}
#endif
			}
			else
			{
				LatentJson->SetNumberField(TEXT("active_uuid_count"), ActionCount);
				LatentJson->SetBoolField(TEXT("actions_truncated"), false);
			}
		}
		else
		{
			LatentJson->SetObjectField(TEXT("target"), BuildObjectReferenceJson(nullptr));
			LatentJson->SetBoolField(TEXT("target_world_matches"), false);
			LatentJson->SetNumberField(TEXT("action_count"), 0);
			LatentJson->SetNumberField(TEXT("active_uuid_count"), 0);
			LatentJson->SetBoolField(TEXT("actions_truncated"), false);
		}
		LatentJson->SetNumberField(TEXT("returned_action_count"), LatentActionsJson.Num());
		LatentJson->SetArrayField(TEXT("actions"), LatentActionsJson);
		Data->SetObjectField(TEXT("latent_actions"), LatentJson);

		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


TSharedPtr<FJsonObject> BuildSchedulerPerformanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString NameFilter;
	FString ClassFilter;
	FString ComponentClassFilter;
	bool bIncludeActorTicks = true;
	bool bIncludeComponentTicks = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Params->TryGetBoolField(TEXT("include_actor_ticks"), bIncludeActorTicks);
		Params->TryGetBoolField(TEXT("include_component_ticks"), bIncludeComponentTicks);
	}
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	ComponentClassFilter.TrimStartAndEndInline();
	const int32 ActorLimit = ReadClampedIntField(Params, TEXT("actor_limit"), 100, 0, 1000);
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 200, 0, 2000);
	const double HitchThresholdMs = ReadClampedDoubleField(Params, TEXT("hitch_threshold_ms"), 33.333, 1.0, 10000.0);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetStringField(TEXT("name_filter"), NameFilter);
		Data->SetStringField(TEXT("class_filter"), ClassFilter);
		Data->SetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
		Data->SetObjectField(TEXT("app_frame"), BuildRuntimeAppFrameJson(HitchThresholdMs));
		Data->SetObjectField(TEXT("task_graph"), BuildRuntimeTaskGraphJson());
		Data->SetObjectField(TEXT("threading"), BuildRuntimeThreadingJson());

		TArray<TSharedPtr<FJsonValue>> Limitations;
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("This is a point-in-time scheduling snapshot, not a sampled profiler history")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("TaskGraph public API does not expose queue depth, backlog, or per-task wait time")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("FTickFunction queued/running state is internal; this endpoint reports public tick configuration and registration state")));
		Data->SetArrayField(TEXT("limitations"), Limitations);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			return Data;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; scheduler/performance diagnostics reflect the editor world")));
		}

		const double AppDeltaMs = FApp::GetDeltaTime() * 1000.0;
		const double WorldDeltaMs = World->GetDeltaSeconds() * 1000.0;
		if (AppDeltaMs >= HitchThresholdMs)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Last app frame delta %.3f ms exceeded hitch threshold %.3f ms"), AppDeltaMs, HitchThresholdMs)));
		}
		if (WorldDeltaMs >= HitchThresholdMs)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Selected world delta %.3f ms exceeded hitch threshold %.3f ms"), WorldDeltaMs, HitchThresholdMs)));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetObjectField(TEXT("world_time"), BuildRuntimeWorldTimeJson(World));

		TSharedPtr<FJsonObject> PerformanceJson = MakeShared<FJsonObject>();
		PerformanceJson->SetNumberField(TEXT("hitch_threshold_ms"), HitchThresholdMs);
		PerformanceJson->SetNumberField(TEXT("app_delta_ms"), AppDeltaMs);
		PerformanceJson->SetNumberField(TEXT("world_delta_ms"), WorldDeltaMs);
		PerformanceJson->SetBoolField(TEXT("app_delta_exceeds_hitch_threshold"), AppDeltaMs >= HitchThresholdMs);
		PerformanceJson->SetBoolField(TEXT("world_delta_exceeds_hitch_threshold"), WorldDeltaMs >= HitchThresholdMs);
		PerformanceJson->SetBoolField(TEXT("world_paused"), World->IsPaused());
		Data->SetObjectField(TEXT("performance_flags"), PerformanceJson);

		auto ActorMatchesFilters = [&NameFilter, &ClassFilter](AActor* Actor)
		{
			if (!Actor)
			{
				return false;
			}
			const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
			if (!ClassFilter.IsEmpty() && !ActorClassPath.Contains(ClassFilter))
			{
				return false;
			}
			if (!NameFilter.IsEmpty() && !Actor->GetName().Contains(NameFilter) && !Actor->GetActorLabel().Contains(NameFilter))
			{
				return false;
			}
			return true;
		};

		auto ComponentMatchesFilters = [&NameFilter, &ClassFilter, &ComponentClassFilter](UActorComponent* Component)
		{
			if (!Component)
			{
				return false;
			}
			AActor* Owner = Component->GetOwner();
			const FString OwnerClassPath = (Owner && Owner->GetClass()) ? Owner->GetClass()->GetPathName() : TEXT("");
			const FString ComponentClassPath = Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT("");
			if (!ClassFilter.IsEmpty() && !OwnerClassPath.Contains(ClassFilter))
			{
				return false;
			}
			if (!ComponentClassFilter.IsEmpty() && !ComponentClassPath.Contains(ComponentClassFilter))
			{
				return false;
			}
			if (!NameFilter.IsEmpty())
			{
				const bool bNameMatches = Component->GetName().Contains(NameFilter)
					|| (Owner && (Owner->GetName().Contains(NameFilter) || Owner->GetActorLabel().Contains(NameFilter)));
				if (!bNameMatches)
				{
					return false;
				}
			}
			return true;
		};

		TSharedPtr<FJsonObject> TickSummaryJson = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> ActorTicksJson;
		TArray<TSharedPtr<FJsonValue>> ComponentTicksJson;
		TArray<int32> ActorTickGroupCounts;
		TArray<int32> ComponentTickGroupCounts;
		ActorTickGroupCounts.SetNumZeroed(static_cast<int32>(TG_MAX));
		ComponentTickGroupCounts.SetNumZeroed(static_cast<int32>(TG_MAX));

		int32 TotalActorCount = 0;
		int32 MatchedActorCount = 0;
		int32 ActorCanEverTickCount = 0;
		int32 ActorRegisteredTickCount = 0;
		int32 ActorEnabledTickCount = 0;
		int32 ActorIntervalTickCount = 0;
		int32 ActorAsyncTickCount = 0;
		int32 ActorHighPriorityTickCount = 0;
		int32 ActorTickEvenPausedCount = 0;
		int32 ActorPrerequisiteCount = 0;

		int32 TotalComponentCount = 0;
		int32 MatchedComponentCount = 0;
		int32 ComponentCanEverTickCount = 0;
		int32 ComponentRegisteredTickCount = 0;
		int32 ComponentEnabledTickCount = 0;
		int32 ComponentIntervalTickCount = 0;
		int32 ComponentAsyncTickCount = 0;
		int32 ComponentHighPriorityTickCount = 0;
		int32 ComponentTickEvenPausedCount = 0;
		int32 ComponentPrerequisiteCount = 0;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			++TotalActorCount;

			const bool bActorMatches = ActorMatchesFilters(Actor);
			if (bActorMatches)
			{
				++MatchedActorCount;
				const FTickFunction& ActorTick = Actor->PrimaryActorTick;
				if (ActorTick.bCanEverTick)
				{
					++ActorCanEverTickCount;
					IncrementTickGroupCount(ActorTickGroupCounts, ActorTick.TickGroup);
				}
				if (ActorTick.IsTickFunctionRegistered()) ++ActorRegisteredTickCount;
				if (ActorTick.IsTickFunctionEnabled()) ++ActorEnabledTickCount;
				if (ActorTick.TickInterval > 0.0f) ++ActorIntervalTickCount;
				if (ActorTick.bRunOnAnyThread) ++ActorAsyncTickCount;
				if (ActorTick.bHighPriority) ++ActorHighPriorityTickCount;
				if (ActorTick.bTickEvenWhenPaused) ++ActorTickEvenPausedCount;
				ActorPrerequisiteCount += ActorTick.GetPrerequisites().Num();

				const bool bHasTickInfo = ActorTick.bCanEverTick || ActorTick.IsTickFunctionRegistered() || ActorTick.IsTickFunctionEnabled() || ActorTick.TickInterval > 0.0f;
				if (bIncludeActorTicks && bHasTickInfo && ActorTicksJson.Num() < ActorLimit)
				{
					TSharedPtr<FJsonObject> ActorJson = BuildActorJson(Actor);
					ActorJson->SetObjectField(TEXT("tick"), BuildRuntimeTickFunctionJson(ActorTick));
					ActorTicksJson.Add(MakeShared<FJsonValueObject>(ActorJson));
				}
			}

			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}
				++TotalComponentCount;
				if (!ComponentMatchesFilters(Component))
				{
					continue;
				}
				++MatchedComponentCount;

				const FTickFunction& ComponentTick = Component->PrimaryComponentTick;
				if (ComponentTick.bCanEverTick)
				{
					++ComponentCanEverTickCount;
					IncrementTickGroupCount(ComponentTickGroupCounts, ComponentTick.TickGroup);
				}
				if (ComponentTick.IsTickFunctionRegistered()) ++ComponentRegisteredTickCount;
				if (ComponentTick.IsTickFunctionEnabled()) ++ComponentEnabledTickCount;
				if (ComponentTick.TickInterval > 0.0f) ++ComponentIntervalTickCount;
				if (ComponentTick.bRunOnAnyThread) ++ComponentAsyncTickCount;
				if (ComponentTick.bHighPriority) ++ComponentHighPriorityTickCount;
				if (ComponentTick.bTickEvenWhenPaused) ++ComponentTickEvenPausedCount;
				ComponentPrerequisiteCount += ComponentTick.GetPrerequisites().Num();

				const bool bHasComponentTickInfo = ComponentTick.bCanEverTick || ComponentTick.IsTickFunctionRegistered() || ComponentTick.IsTickFunctionEnabled() || ComponentTick.TickInterval > 0.0f;
				if (bIncludeComponentTicks && bHasComponentTickInfo && ComponentTicksJson.Num() < ComponentLimit)
				{
					TSharedPtr<FJsonObject> ComponentJson = BuildComponentJson(Component);
					ComponentJson->SetObjectField(TEXT("tick"), BuildRuntimeTickFunctionJson(ComponentTick));
					ComponentTicksJson.Add(MakeShared<FJsonValueObject>(ComponentJson));
				}
			}
		}

		TickSummaryJson->SetNumberField(TEXT("total_actor_count"), TotalActorCount);
		TickSummaryJson->SetNumberField(TEXT("matched_actor_count"), MatchedActorCount);
		TickSummaryJson->SetNumberField(TEXT("actor_can_ever_tick_count"), ActorCanEverTickCount);
		TickSummaryJson->SetNumberField(TEXT("actor_registered_tick_count"), ActorRegisteredTickCount);
		TickSummaryJson->SetNumberField(TEXT("actor_enabled_tick_count"), ActorEnabledTickCount);
		TickSummaryJson->SetNumberField(TEXT("actor_interval_tick_count"), ActorIntervalTickCount);
		TickSummaryJson->SetNumberField(TEXT("actor_async_tick_count"), ActorAsyncTickCount);
		TickSummaryJson->SetNumberField(TEXT("actor_high_priority_tick_count"), ActorHighPriorityTickCount);
		TickSummaryJson->SetNumberField(TEXT("actor_tick_even_paused_count"), ActorTickEvenPausedCount);
		TickSummaryJson->SetNumberField(TEXT("actor_prerequisite_count"), ActorPrerequisiteCount);
		TickSummaryJson->SetArrayField(TEXT("actor_tick_groups"), BuildTickGroupCountsJson(ActorTickGroupCounts));
		TickSummaryJson->SetNumberField(TEXT("total_component_count"), TotalComponentCount);
		TickSummaryJson->SetNumberField(TEXT("matched_component_count"), MatchedComponentCount);
		TickSummaryJson->SetNumberField(TEXT("component_can_ever_tick_count"), ComponentCanEverTickCount);
		TickSummaryJson->SetNumberField(TEXT("component_registered_tick_count"), ComponentRegisteredTickCount);
		TickSummaryJson->SetNumberField(TEXT("component_enabled_tick_count"), ComponentEnabledTickCount);
		TickSummaryJson->SetNumberField(TEXT("component_interval_tick_count"), ComponentIntervalTickCount);
		TickSummaryJson->SetNumberField(TEXT("component_async_tick_count"), ComponentAsyncTickCount);
		TickSummaryJson->SetNumberField(TEXT("component_high_priority_tick_count"), ComponentHighPriorityTickCount);
		TickSummaryJson->SetNumberField(TEXT("component_tick_even_paused_count"), ComponentTickEvenPausedCount);
		TickSummaryJson->SetNumberField(TEXT("component_prerequisite_count"), ComponentPrerequisiteCount);
		TickSummaryJson->SetArrayField(TEXT("component_tick_groups"), BuildTickGroupCountsJson(ComponentTickGroupCounts));
		TickSummaryJson->SetBoolField(TEXT("include_actor_ticks"), bIncludeActorTicks);
		TickSummaryJson->SetBoolField(TEXT("include_component_ticks"), bIncludeComponentTicks);
		TickSummaryJson->SetNumberField(TEXT("actor_limit"), ActorLimit);
		TickSummaryJson->SetNumberField(TEXT("component_limit"), ComponentLimit);
		TickSummaryJson->SetNumberField(TEXT("returned_actor_tick_count"), ActorTicksJson.Num());
		TickSummaryJson->SetNumberField(TEXT("returned_component_tick_count"), ComponentTicksJson.Num());
		TickSummaryJson->SetBoolField(TEXT("actor_ticks_truncated"), bIncludeActorTicks && ActorCanEverTickCount > ActorTicksJson.Num());
		TickSummaryJson->SetBoolField(TEXT("component_ticks_truncated"), bIncludeComponentTicks && ComponentCanEverTickCount > ComponentTicksJson.Num());
		Data->SetObjectField(TEXT("tick_summary"), TickSummaryJson);
		if (bIncludeActorTicks)
		{
			Data->SetArrayField(TEXT("actor_ticks"), ActorTicksJson);
		}
		if (bIncludeComponentTicks)
		{
			Data->SetArrayField(TEXT("component_ticks"), ComponentTicksJson);
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


}
