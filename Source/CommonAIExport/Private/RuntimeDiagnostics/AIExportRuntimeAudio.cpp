// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/AIExportRuntimeAudio.h"

#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "AudioDeviceHandle.h"
#include "AudioDeviceManager.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundEffectSource.h"

namespace CommonAIExport::RuntimeDiagnostics
{
namespace
{
FString AudioComponentPlayStateToString(EAudioComponentPlayState PlayState)
{
	switch (PlayState)
	{
	case EAudioComponentPlayState::Playing:
		return TEXT("Playing");
	case EAudioComponentPlayState::Stopped:
		return TEXT("Stopped");
	case EAudioComponentPlayState::Paused:
		return TEXT("Paused");
	case EAudioComponentPlayState::FadingIn:
		return TEXT("FadingIn");
	case EAudioComponentPlayState::FadingOut:
		return TEXT("FadingOut");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildAudioComponentJson(UAudioComponent* AudioComponent)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(AudioComponent);
	if (!AudioComponent)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("sound"), BuildObjectReferenceJson(AudioComponent->Sound.Get()));
	Data->SetObjectField(TEXT("sound_class_override"), BuildObjectReferenceJson(AudioComponent->SoundClassOverride.Get()));
	Data->SetObjectField(TEXT("attenuation_settings"), BuildObjectReferenceJson(AudioComponent->AttenuationSettings.Get()));
	Data->SetObjectField(TEXT("source_effect_chain"), BuildObjectReferenceJson(AudioComponent->SourceEffectChain.Get()));
	Data->SetNumberField(TEXT("concurrency_set_count"), AudioComponent->ConcurrencySet.Num());
	Data->SetStringField(TEXT("audio_component_id"), FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(AudioComponent->GetAudioComponentID())));
	Data->SetStringField(TEXT("audio_component_user_id"), AudioComponent->GetAudioComponentUserID().ToString());
	Data->SetNumberField(TEXT("audio_device_id"), AudioComponent->AudioDeviceID);
	Data->SetBoolField(TEXT("audio_device_available"), AudioComponent->GetAudioDevice() != nullptr);
	Data->SetStringField(TEXT("play_state"), AudioComponentPlayStateToString(AudioComponent->GetPlayState()));
	Data->SetBoolField(TEXT("playing"), AudioComponent->IsPlaying());
	Data->SetBoolField(TEXT("paused"), AudioComponent->bIsPaused != 0);
	Data->SetBoolField(TEXT("virtualized"), AudioComponent->bIsVirtualized != 0);
	Data->SetBoolField(TEXT("fading_out"), AudioComponent->bIsFadingOut != 0);
	Data->SetBoolField(TEXT("ui_sound"), AudioComponent->bIsUISound != 0);
	Data->SetBoolField(TEXT("music"), AudioComponent->bIsMusic != 0);
	Data->SetBoolField(TEXT("preview_sound"), AudioComponent->bIsPreviewSound != 0);
	Data->SetBoolField(TEXT("allow_spatialization"), AudioComponent->bAllowSpatialization != 0);
	Data->SetBoolField(TEXT("override_attenuation"), AudioComponent->bOverrideAttenuation != 0);
	Data->SetBoolField(TEXT("can_play_multiple_instances"), AudioComponent->bCanPlayMultipleInstances != 0);
	Data->SetBoolField(TEXT("auto_destroy"), AudioComponent->bAutoDestroy != 0);
	Data->SetBoolField(TEXT("stop_when_owner_destroyed"), AudioComponent->bStopWhenOwnerDestroyed != 0);
	Data->SetNumberField(TEXT("volume_multiplier"), AudioComponent->VolumeMultiplier);
	Data->SetNumberField(TEXT("pitch_multiplier"), AudioComponent->PitchMultiplier);
	Data->SetNumberField(TEXT("priority"), AudioComponent->Priority);
	Data->SetNumberField(TEXT("active_count"), AudioComponent->ActiveCount);
	Data->SetNumberField(TEXT("last_play_order"), AudioComponent->GetLastPlayOrder());
	Data->SetObjectField(TEXT("location"), BuildVectorJson(AudioComponent->GetComponentLocation()));
	return Data;
}
}

TSharedPtr<FJsonObject> BuildAudioDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	FString ComponentClassFilter;
	FString SoundFilter;
	bool bIncludeInactive = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Params->TryGetStringField(TEXT("sound_filter"), SoundFilter);
		Params->TryGetBoolField(TEXT("include_inactive"), bIncludeInactive);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	ComponentClassFilter.TrimStartAndEndInline();
	SoundFilter.TrimStartAndEndInline();
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 200, 0, 2000);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("requested_world"), WorldSelector);
	Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
	Data->SetStringField(TEXT("sound_filter"), SoundFilter);
	Data->SetBoolField(TEXT("include_inactive"), bIncludeInactive);
	Data->SetNumberField(TEXT("component_limit"), ComponentLimit);

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
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; audio diagnostics reflect the editor world")));
	}

	Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));

	TSharedPtr<FJsonObject> DeviceJson = MakeShared<FJsonObject>();
	FAudioDeviceHandle WorldAudioDevice = World->GetAudioDevice();
	DeviceJson->SetBoolField(TEXT("world_audio_device_valid"), WorldAudioDevice.IsValid());
	DeviceJson->SetNumberField(TEXT("world_audio_device_id"), WorldAudioDevice.IsValid() ? WorldAudioDevice.GetDeviceID() : 0);
	if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
	{
		DeviceJson->SetBoolField(TEXT("manager_available"), true);
		DeviceJson->SetNumberField(TEXT("main_audio_device_id"), DeviceManager->GetMainAudioDeviceID());
		DeviceJson->SetNumberField(TEXT("active_audio_device_count"), DeviceManager->GetNumActiveAudioDevices());
		DeviceJson->SetNumberField(TEXT("main_audio_device_world_count"), DeviceManager->GetNumMainAudioDeviceWorlds());
		DeviceJson->SetBoolField(TEXT("play_all_device_audio"), DeviceManager->IsPlayAllDeviceAudio());
		DeviceJson->SetBoolField(TEXT("always_play_non_realtime_device_audio"), DeviceManager->IsAlwaysPlayNonRealtimeDeviceAudio());
		DeviceJson->SetBoolField(TEXT("visualize_3d_debug"), DeviceManager->IsVisualizeDebug3dEnabled());
		DeviceJson->SetBoolField(TEXT("aggregate_device_support_enabled"), FAudioDeviceManager::IsAggregateDeviceSupportEnabled());
	}
	else
	{
		DeviceJson->SetBoolField(TEXT("manager_available"), false);
	}
	Data->SetObjectField(TEXT("audio_device"), DeviceJson);

	auto ActorMatchesFilters = [&NameFilter, &ClassFilter](AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}
		if (!NameFilter.IsEmpty()
			&& !Actor->GetName().Contains(NameFilter)
			&& !Actor->GetActorLabel().Contains(NameFilter)
			&& !Actor->GetPathName().Contains(NameFilter))
		{
			return false;
		}
		const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
		if (!ClassFilter.IsEmpty() && !ActorClassPath.Contains(ClassFilter))
		{
			return false;
		}
		return true;
	};

	auto AudioComponentMatchesFilters = [&NameFilter, &ComponentClassFilter, &SoundFilter](UAudioComponent* AudioComponent)
	{
		if (!AudioComponent)
		{
			return false;
		}
		if (!NameFilter.IsEmpty()
			&& !AudioComponent->GetName().Contains(NameFilter)
			&& !AudioComponent->GetPathName().Contains(NameFilter))
		{
			return false;
		}
		const FString ComponentClassPath = AudioComponent->GetClass() ? AudioComponent->GetClass()->GetPathName() : TEXT("");
		if (!ComponentClassFilter.IsEmpty() && !ComponentClassPath.Contains(ComponentClassFilter))
		{
			return false;
		}
		if (!SoundFilter.IsEmpty())
		{
			const USoundBase* Sound = AudioComponent->Sound;
			const FString SoundName = Sound ? Sound->GetName() : TEXT("");
			const FString SoundPath = Sound ? Sound->GetPathName() : TEXT("");
			if (!SoundName.Contains(SoundFilter) && !SoundPath.Contains(SoundFilter))
			{
				return false;
			}
		}
		return true;
	};

	TArray<AActor*> ActorsToInspect;
	const bool bActorTargetRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
	if (bActorTargetRequested)
	{
		if (AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName))
		{
			ActorsToInspect.Add(Actor);
		}
		else
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
		}
	}
	else
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			ActorsToInspect.Add(*It);
		}
	}

	int32 InspectedActorCount = 0;
	int32 AudioComponentCount = 0;
	int32 ActiveAudioComponentCount = 0;
	int32 PlayingAudioComponentCount = 0;
	int32 MatchedAudioComponentCount = 0;
	TArray<TSharedPtr<FJsonValue>> ComponentsJson;
	for (AActor* Actor : ActorsToInspect)
	{
		if (!Actor || !ActorMatchesFilters(Actor))
		{
			continue;
		}
		++InspectedActorCount;

		TArray<UAudioComponent*> AudioComponents;
		Actor->GetComponents<UAudioComponent>(AudioComponents);
		for (UAudioComponent* AudioComponent : AudioComponents)
		{
			if (!AudioComponent)
			{
				continue;
			}

			++AudioComponentCount;
			if (AudioComponent->IsActive())
			{
				++ActiveAudioComponentCount;
			}
			if (AudioComponent->IsPlaying())
			{
				++PlayingAudioComponentCount;
			}
			if (!bIncludeInactive && !AudioComponent->IsActive() && !AudioComponent->IsPlaying())
			{
				continue;
			}
			if (!AudioComponentMatchesFilters(AudioComponent))
			{
				continue;
			}

			++MatchedAudioComponentCount;
			if (ComponentsJson.Num() < ComponentLimit)
			{
				ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildAudioComponentJson(AudioComponent)));
			}
		}
	}

	Data->SetNumberField(TEXT("inspected_actor_count"), InspectedActorCount);
	Data->SetNumberField(TEXT("audio_component_count"), AudioComponentCount);
	Data->SetNumberField(TEXT("active_audio_component_count"), ActiveAudioComponentCount);
	Data->SetNumberField(TEXT("playing_audio_component_count"), PlayingAudioComponentCount);
	Data->SetNumberField(TEXT("matched_audio_component_count"), MatchedAudioComponentCount);
	Data->SetNumberField(TEXT("returned_audio_component_count"), ComponentsJson.Num());
	Data->SetBoolField(TEXT("audio_components_truncated"), MatchedAudioComponentCount > ComponentsJson.Num());
	Data->SetArrayField(TEXT("audio_components"), ComponentsJson);
	Data->SetArrayField(TEXT("warnings"), Warnings);
	return Data;
}
}
