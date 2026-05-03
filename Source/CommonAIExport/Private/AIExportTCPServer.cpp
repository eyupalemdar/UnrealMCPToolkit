// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "AIExportFunctionLibrary.h"
#include "Builders/AIWidgetBlueprintBuilder.h"
#include "Builders/AIMaterialBuilder.h"
#include "Builders/AIBlueprintGraphBuilder.h"
#include "Builders/AIDataAssetBuilder.h"
#include "Builders/AIAssetFactory.h"
#include "Builders/AIAnimBlueprintBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "CommonAIExportModule.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#include "WidgetBlueprint.h"
#include "Components/Widget.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringOutputDevice.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "CoreGlobals.h"
#include "HAL/RunnableThread.h"
#include "Misc/EngineVersion.h"
#include "Misc/App.h"

#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

// Asset rename (HandleRenameAsset)
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// Asset delete (HandleDeleteAsset) — ObjectTools lives in UnrealEd
#include "ObjectTools.h"
#include "UObject/ObjectRedirector.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "InputMappingContext.h"
#include "Animation/AnimBlueprint.h"

// Widget Preview Capture includes (for HandleCaptureWidgetPreview)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "ContentStreaming.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "CommonActivatableWidget.h"
#include "CommonUserWidget.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBinding.h"
#include "Input/UIActionBindingHandle.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "ICommonInputModule.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "PlayInEditorDataTypes.h"
#include "RenderingThread.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/StreamableRenderAsset.h"
#include "Engine/GameViewportClient.h"
#include "RenderAssetUpdate.h"
#include "Misc/Base64.h"
#include "ScopedTransaction.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"
#include "Engine/GameInstance.h"
#include "Engine/LatentActionManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "CommonInputSubsystem.h"
#include "Components/InputComponent.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/OnlineSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GenericTeamAgentInterface.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISenseConfig.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "TimerManager.h"

// Asset Lifecycle includes (for HandleReloadAsset)
#include "Subsystems/AssetEditorSubsystem.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Interfaces/IPluginManager.h"

namespace
{
FString CommonAIExportHttpVerbToString(EHttpServerRequestVerbs Verb)
{
	if (Verb == EHttpServerRequestVerbs::VERB_GET) return TEXT("GET");
	if (Verb == EHttpServerRequestVerbs::VERB_POST) return TEXT("POST");
	if (Verb == EHttpServerRequestVerbs::VERB_DELETE) return TEXT("DELETE");
	if (Verb == EHttpServerRequestVerbs::VERB_OPTIONS) return TEXT("OPTIONS");
	if (Verb == EHttpServerRequestVerbs::VERB_PUT) return TEXT("PUT");
	if (Verb == EHttpServerRequestVerbs::VERB_PATCH) return TEXT("PATCH");
	return TEXT("UNKNOWN");
}

FString QuoteProcessArgument(const FString& Argument)
{
	FString Escaped = Argument;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

bool ResolveProjectScopedDirectory(const FString& RequestedPath, FString& OutDirectory, FString& OutError)
{
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);

	FString Candidate = RequestedPath;
	Candidate.TrimStartAndEndInline();
	if (Candidate.IsEmpty())
	{
		Candidate = ProjectDir;
	}
	else if (FPaths::IsRelative(Candidate))
	{
		Candidate = FPaths::Combine(ProjectDir, Candidate);
	}

	Candidate = FPaths::ConvertRelativePathToFull(Candidate);
	FPaths::NormalizeDirectoryName(Candidate);

	if (!Candidate.Equals(ProjectDir, ESearchCase::IgnoreCase) && !FPaths::IsUnderDirectory(Candidate, ProjectDir))
	{
		OutError = TEXT("repo_path must resolve under the project directory");
		return false;
	}
	if (!IFileManager::Get().DirectoryExists(*Candidate))
	{
		OutError = FString::Printf(TEXT("repo_path does not exist or is not a directory: %s"), *Candidate);
		return false;
	}

	OutDirectory = Candidate;
	return true;
}

struct FSourceControlCommandContext
{
	FString Provider = TEXT("auto");
	FString Executable;
	FString RepoDir;
	bool bHasDiversion = false;
	bool bHasGit = false;
};

bool ResolveSourceControlCommandContext(TSharedPtr<FJsonObject> Params, FSourceControlCommandContext& OutContext, FString& OutError)
{
	FString Provider = TEXT("auto");
	FString RepoPath;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("provider"), Provider);
		Params->TryGetStringField(TEXT("repo_path"), RepoPath);
	}

	Provider.TrimStartAndEndInline();
	Provider = Provider.IsEmpty() ? TEXT("auto") : Provider.ToLower();

	if (!ResolveProjectScopedDirectory(RepoPath, OutContext.RepoDir, OutError))
	{
		return false;
	}

	OutContext.bHasDiversion = IFileManager::Get().DirectoryExists(*FPaths::Combine(OutContext.RepoDir, TEXT(".diversion")));
	OutContext.bHasGit = IFileManager::Get().DirectoryExists(*FPaths::Combine(OutContext.RepoDir, TEXT(".git")));

	if ((Provider == TEXT("auto") && OutContext.bHasDiversion) || Provider == TEXT("dv") || Provider == TEXT("diversion"))
	{
		OutContext.Provider = TEXT("diversion");
		OutContext.Executable = TEXT("dv");
	}
	else if ((Provider == TEXT("auto") && OutContext.bHasGit) || Provider == TEXT("git"))
	{
		OutContext.Provider = TEXT("git");
		OutContext.Executable = TEXT("git");
	}
	else if (Provider == TEXT("auto"))
	{
		OutContext.Provider = TEXT("auto");
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported source-control provider: %s"), *Provider);
		return false;
	}

	return true;
}

void AddSourceControlContextJson(TSharedPtr<FJsonObject> Data, const FSourceControlCommandContext& Context)
{
	Data->SetStringField(TEXT("provider"), Context.Provider);
	Data->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Data->SetStringField(TEXT("repo_dir"), Context.RepoDir);
	Data->SetBoolField(TEXT("diversion_repo"), Context.bHasDiversion);
	Data->SetBoolField(TEXT("git_repo"), Context.bHasGit);
}

void AddSourceControlProcessResult(TSharedPtr<FJsonObject> Data, const FSourceControlCommandContext& Context, const FString& Arguments)
{
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	const bool bLaunched = FPlatformProcess::ExecProcess(*Context.Executable, *Arguments, &ReturnCode, &StdOut, &StdErr, *Context.RepoDir);
	Data->SetBoolField(TEXT("available"), bLaunched);
	Data->SetStringField(TEXT("executable"), Context.Executable);
	Data->SetStringField(TEXT("arguments"), Arguments);
	Data->SetNumberField(TEXT("return_code"), ReturnCode);
	Data->SetStringField(TEXT("stdout"), StdOut);
	Data->SetStringField(TEXT("stderr"), StdErr);
	Data->SetStringField(TEXT("status"), (bLaunched && ReturnCode == 0) ? TEXT("ok") : TEXT("failed"));
}

int32 ReadClampedIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
{
	if (!Params.IsValid())
	{
		return DefaultValue;
	}

	double NumberValue = 0.0;
	if (!Params->TryGetNumberField(FieldName, NumberValue))
	{
		return DefaultValue;
	}
	return FMath::Clamp(static_cast<int32>(NumberValue), MinValue, MaxValue);
}

double ReadClampedDoubleField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, double DefaultValue, double MinValue, double MaxValue)
{
	if (!Params.IsValid())
	{
		return DefaultValue;
	}

	double NumberValue = 0.0;
	if (!Params->TryGetNumberField(FieldName, NumberValue))
	{
		return DefaultValue;
	}
	return FMath::Clamp(NumberValue, MinValue, MaxValue);
}

void AppendStringArrayField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, TArray<FString>& OutValues)
{
	if (!Params.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Params->TryGetArrayField(FieldName, Values) || !Values)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString StringValue;
		if (!Value.IsValid() || !Value->TryGetString(StringValue))
		{
			continue;
		}
		StringValue.TrimStartAndEndInline();
		if (!StringValue.IsEmpty())
		{
			OutValues.Add(StringValue);
		}
	}
}

FString ReadLowerStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FString& DefaultValue)
{
	FString Value = DefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
	}
	Value.TrimStartAndEndInline();
	return Value.ToLower();
}

FString NetModeToString(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone:
		return TEXT("Standalone");
	case NM_DedicatedServer:
		return TEXT("DedicatedServer");
	case NM_ListenServer:
		return TEXT("ListenServer");
	case NM_Client:
		return TEXT("Client");
	default:
		return TEXT("Unknown");
	}
}

FString NetRoleToString(ENetRole Role)
{
	switch (Role)
	{
	case ROLE_None:
		return TEXT("None");
	case ROLE_SimulatedProxy:
		return TEXT("SimulatedProxy");
	case ROLE_AutonomousProxy:
		return TEXT("AutonomousProxy");
	case ROLE_Authority:
		return TEXT("Authority");
	default:
		return TEXT("Unknown");
	}
}

FString NetDormancyToString(ENetDormancy Dormancy)
{
	switch (Dormancy)
	{
	case DORM_Never:
		return TEXT("Never");
	case DORM_Awake:
		return TEXT("Awake");
	case DORM_DormantAll:
		return TEXT("DormantAll");
	case DORM_DormantPartial:
		return TEXT("DormantPartial");
	case DORM_Initial:
		return TEXT("Initial");
	default:
		return TEXT("Unknown");
	}
}

UWorld* SelectAIWorld(const FString& RequestedWorld, FString& OutWorldSource)
{
	if (!GEditor)
	{
		OutWorldSource = TEXT("none");
		return nullptr;
	}

	const FString Selector = RequestedWorld.IsEmpty() ? TEXT("auto") : RequestedWorld.ToLower();
	if (Selector == TEXT("editor"))
	{
		OutWorldSource = TEXT("editor");
		return GEditor->GetEditorWorldContext().World();
	}
	if (Selector == TEXT("pie") || Selector == TEXT("runtime") || Selector == TEXT("play"))
	{
		OutWorldSource = TEXT("pie");
		return GEditor->PlayWorld;
	}

	if (GEditor->PlayWorld)
	{
		OutWorldSource = TEXT("pie");
		return GEditor->PlayWorld;
	}

	OutWorldSource = TEXT("editor");
	return GEditor->GetEditorWorldContext().World();
}

TSharedPtr<FJsonObject> BuildPIEStateJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	Data->SetBoolField(TEXT("simulating"), GEditor && GEditor->bIsSimulatingInEditor);
	Data->SetStringField(TEXT("play_world"), (GEditor && GEditor->PlayWorld) ? GEditor->PlayWorld->GetName() : TEXT(""));
	return Data;
}

TSharedPtr<FJsonObject> BuildVectorJson(const FVector& Vector)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("x"), Vector.X);
	Data->SetNumberField(TEXT("y"), Vector.Y);
	Data->SetNumberField(TEXT("z"), Vector.Z);
	return Data;
}

TSharedPtr<FJsonObject> BuildVector2DJson(const FVector2D& Vector)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("x"), Vector.X);
	Data->SetNumberField(TEXT("y"), Vector.Y);
	return Data;
}

FString SaveExistsResultToString(ISaveGameSystem::ESaveExistsResult Result)
{
	switch (Result)
	{
	case ISaveGameSystem::ESaveExistsResult::OK:
		return TEXT("OK");
	case ISaveGameSystem::ESaveExistsResult::DoesNotExist:
		return TEXT("DoesNotExist");
	case ISaveGameSystem::ESaveExistsResult::Corrupt:
		return TEXT("Corrupt");
	case ISaveGameSystem::ESaveExistsResult::UnspecifiedError:
		return TEXT("UnspecifiedError");
	default:
		return TEXT("Unknown");
	}
}

FString TravelTypeToString(ETravelType TravelType)
{
	switch (TravelType)
	{
	case TRAVEL_Absolute:
		return TEXT("Absolute");
	case TRAVEL_Partial:
		return TEXT("Partial");
	case TRAVEL_Relative:
		return TEXT("Relative");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildURLJson(const FURL& URL, bool bIncludeOptions, int32 OptionLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("text"), URL.ToString(false));
	Data->SetStringField(TEXT("fully_qualified_text"), URL.ToString(true));
	Data->SetStringField(TEXT("protocol"), URL.Protocol);
	Data->SetStringField(TEXT("host"), URL.Host);
	Data->SetNumberField(TEXT("port"), URL.Port);
	Data->SetBoolField(TEXT("valid"), URL.Valid != 0);
	Data->SetStringField(TEXT("map"), URL.Map);
	Data->SetStringField(TEXT("redirect_url"), URL.RedirectURL);
	Data->SetStringField(TEXT("portal"), URL.Portal);
	Data->SetBoolField(TEXT("internal"), URL.IsInternal());
	Data->SetBoolField(TEXT("local_internal"), URL.IsLocalInternal());
	Data->SetNumberField(TEXT("option_count"), URL.Op.Num());
	Data->SetBoolField(TEXT("include_options"), bIncludeOptions);
	Data->SetNumberField(TEXT("option_limit"), OptionLimit);
	if (bIncludeOptions)
	{
		TArray<TSharedPtr<FJsonValue>> OptionsJson;
		for (const FString& Option : URL.Op)
		{
			if (OptionsJson.Num() >= OptionLimit)
			{
				break;
			}
			OptionsJson.Add(MakeShared<FJsonValueString>(Option));
		}
		Data->SetNumberField(TEXT("returned_option_count"), OptionsJson.Num());
		Data->SetBoolField(TEXT("options_truncated"), URL.Op.Num() > OptionsJson.Num());
		Data->SetArrayField(TEXT("options"), OptionsJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeObjectReferenceJson(const UObject* Object);

TSharedPtr<FJsonObject> BuildRuntimeActorJson(AActor* Actor)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Actor)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Actor->GetName());
	Data->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Data->SetStringField(TEXT("path"), Actor->GetPathName());
	Data->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));

	Data->SetObjectField(TEXT("location"), BuildVectorJson(Actor->GetActorLocation()));

	TSharedPtr<FJsonObject> Rotation = MakeShared<FJsonObject>();
	Rotation->SetNumberField(TEXT("pitch"), Actor->GetActorRotation().Pitch);
	Rotation->SetNumberField(TEXT("yaw"), Actor->GetActorRotation().Yaw);
	Rotation->SetNumberField(TEXT("roll"), Actor->GetActorRotation().Roll);
	Data->SetObjectField(TEXT("rotation"), Rotation);

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeWorldJson(UWorld* World, const FString& WorldSource)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!World)
	{
		return Data;
	}

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		++ActorCount;
	}

	int32 PlayerControllerCount = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		++PlayerControllerCount;
	}

	int32 LocalPlayerCount = 0;
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		LocalPlayerCount = GameInstance->GetLocalPlayers().Num();
	}

	TArray<TSharedPtr<FJsonValue>> Levels;
	for (ULevel* Level : World->GetLevels())
	{
		if (!Level)
		{
			continue;
		}
		TSharedPtr<FJsonObject> LevelJson = MakeShared<FJsonObject>();
		LevelJson->SetStringField(TEXT("name"), Level->GetName());
		LevelJson->SetStringField(TEXT("package_name"), Level->GetOutermost() ? Level->GetOutermost()->GetName() : TEXT(""));
		LevelJson->SetBoolField(TEXT("is_persistent"), Level == World->PersistentLevel);
		Levels.Add(MakeShared<FJsonValueObject>(LevelJson));
	}

	Data->SetStringField(TEXT("world_source"), WorldSource);
	Data->SetStringField(TEXT("world_name"), World->GetName());
	Data->SetStringField(TEXT("package_name"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("world_type"), LexToString(World->WorldType));
	Data->SetStringField(TEXT("net_mode"), NetModeToString(World->GetNetMode()));
	Data->SetNumberField(TEXT("time_seconds"), World->GetTimeSeconds());
	Data->SetNumberField(TEXT("actor_count"), ActorCount);
	Data->SetNumberField(TEXT("level_count"), Levels.Num());
	Data->SetNumberField(TEXT("player_controller_count"), PlayerControllerCount);
	Data->SetNumberField(TEXT("local_player_count"), LocalPlayerCount);
	Data->SetArrayField(TEXT("levels"), Levels);
	Data->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	Data->SetBoolField(TEXT("simulating"), GEditor && GEditor->bIsSimulatingInEditor);

	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		Data->SetStringField(TEXT("game_instance_class"), GameInstance->GetClass() ? GameInstance->GetClass()->GetPathName() : TEXT(""));
	}
	if (AGameModeBase* GameMode = World->GetAuthGameMode())
	{
		Data->SetStringField(TEXT("auth_game_mode_class"), GameMode->GetClass() ? GameMode->GetClass()->GetPathName() : TEXT(""));
	}
	if (AGameStateBase* GameState = World->GetGameState())
	{
		Data->SetStringField(TEXT("game_state_class"), GameState->GetClass() ? GameState->GetClass()->GetPathName() : TEXT(""));
	}

	return Data;
}

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
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(WorldSettings);
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
	Data->SetObjectField(TEXT("pauser_player_state"), BuildRuntimeObjectReferenceJson(WorldSettings->GetPauserPlayerState()));
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

TSharedPtr<FJsonObject> BuildRuntimeAsyncLoadingJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("is_async_loading"), IsAsyncLoading ? IsAsyncLoading() : false);
	Data->SetBoolField(TEXT("is_async_loading_suspended"), IsAsyncLoadingSuspended ? IsAsyncLoadingSuspended() : false);
	Data->SetBoolField(TEXT("is_async_loading_multithreaded"), IsAsyncLoadingMultithreaded ? IsAsyncLoadingMultithreaded() : false);
	Data->SetBoolField(TEXT("is_loading"), IsLoading());
	Data->SetNumberField(TEXT("num_async_packages"), GetNumAsyncPackages());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeAssetRegistryLoadingJson(IAssetRegistry& AssetRegistry)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("loading_assets"), AssetRegistry.IsLoadingAssets());
	Data->SetBoolField(TEXT("gathering"), AssetRegistry.IsGathering());
	Data->SetBoolField(TEXT("search_async"), AssetRegistry.IsSearchAsync());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeStreamableManagerJson(FStreamableManager* StreamableManager)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("asset_manager_initialized"), UAssetManager::IsInitialized());
	Data->SetBoolField(TEXT("present"), StreamableManager != nullptr);
	if (!StreamableManager)
	{
		return Data;
	}

	Data->SetStringField(TEXT("manager_name"), StreamableManager->GetManagerName());
	Data->SetBoolField(TEXT("all_async_loads_complete"), StreamableManager->AreAllAsyncLoadsComplete());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeAssetDataSummaryJson(const FAssetData& AssetData)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), AssetData.IsValid());
	if (!AssetData.IsValid())
	{
		return Data;
	}

	Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
	Data->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
	Data->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
	Data->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeStreamableHandleJson(const TSharedRef<FStreamableHandle>& Handle, int32 RequestedAssetLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("debug_name"), Handle->GetDebugName());
	Data->SetBoolField(TEXT("active"), Handle->IsActive());
	Data->SetBoolField(TEXT("loading_in_progress"), Handle->IsLoadingInProgress());
	Data->SetBoolField(TEXT("load_completed"), Handle->HasLoadCompleted());
	Data->SetBoolField(TEXT("load_completed_or_stalled"), Handle->HasLoadCompletedOrStalled());
	Data->SetBoolField(TEXT("canceled"), Handle->WasCanceled());
	Data->SetBoolField(TEXT("stalled"), Handle->IsStalled());
	Data->SetBoolField(TEXT("combined_handle"), Handle->IsCombinedHandle());
	Data->SetBoolField(TEXT("has_error"), Handle->HasError());
	Data->SetNumberField(TEXT("load_progress"), Handle->GetLoadProgress());
	Data->SetNumberField(TEXT("relative_download_progress"), Handle->GetRelativeDownloadProgress());
	Data->SetNumberField(TEXT("absolute_download_progress"), Handle->GetAbsoluteDownloadProgress());

	int32 LoadedCount = 0;
	int32 RequestedCount = 0;
	Handle->GetLoadedCount(LoadedCount, RequestedCount);
	Data->SetNumberField(TEXT("loaded_count"), LoadedCount);
	Data->SetNumberField(TEXT("requested_count"), RequestedCount);

	TArray<FSoftObjectPath> RequestedAssets;
	Handle->GetRequestedAssets(RequestedAssets, true);
	TArray<TSharedPtr<FJsonValue>> RequestedAssetsJson;
	for (const FSoftObjectPath& RequestedAsset : RequestedAssets)
	{
		if (RequestedAssetsJson.Num() >= RequestedAssetLimit)
		{
			break;
		}
		RequestedAssetsJson.Add(MakeShared<FJsonValueString>(RequestedAsset.ToString()));
	}
	Data->SetNumberField(TEXT("requested_asset_count"), RequestedAssets.Num());
	Data->SetNumberField(TEXT("returned_requested_asset_count"), RequestedAssetsJson.Num());
	Data->SetBoolField(TEXT("requested_assets_truncated"), RequestedAssets.Num() > RequestedAssetsJson.Num());
	Data->SetArrayField(TEXT("requested_assets"), RequestedAssetsJson);
	return Data;
}

bool ResolveAsyncLoadProbeInput(const FString& Input, FString& OutPackageName, FString& OutObjectPath, FString& OutError)
{
	FString Candidate = Input;
	Candidate.TrimStartAndEndInline();
	if (Candidate.IsEmpty())
	{
		OutError = TEXT("Probe input is empty");
		return false;
	}

	Candidate = FPackageName::ExportTextPathToObjectPath(Candidate);

	const FSoftObjectPath SoftObjectPath(Candidate);
	if (SoftObjectPath.IsValid() && !SoftObjectPath.GetLongPackageName().IsEmpty())
	{
		OutPackageName = SoftObjectPath.GetLongPackageName();
		OutObjectPath = SoftObjectPath.ToString();
		return true;
	}

	FString PackageCandidate = Candidate;
	if (Candidate.Contains(TEXT(".")))
	{
		PackageCandidate = FPackageName::ObjectPathToPackageName(Candidate);
	}

	FText Reason;
	if (FPackageName::IsValidLongPackageName(PackageCandidate, true, &Reason))
	{
		OutPackageName = PackageCandidate;
		OutObjectPath = FString::Printf(TEXT("%s.%s"), *PackageCandidate, *FPackageName::GetLongPackageAssetName(PackageCandidate));
		return true;
	}

	FString ConvertedPackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(Candidate, ConvertedPackageName)
		&& FPackageName::IsValidLongPackageName(ConvertedPackageName, true, &Reason))
	{
		OutPackageName = ConvertedPackageName;
		OutObjectPath = FString::Printf(TEXT("%s.%s"), *ConvertedPackageName, *FPackageName::GetLongPackageAssetName(ConvertedPackageName));
		return true;
	}

	OutError = Reason.IsEmpty() ? TEXT("Input is not a valid mounted package, object path, or package filename") : Reason.ToString();
	return false;
}

TSharedPtr<FJsonObject> BuildRuntimeAsyncLoadProbeJson(
	const FString& Input,
	IAssetRegistry& AssetRegistry,
	FStreamableManager* StreamableManager,
	bool bIncludeStreamableHandles,
	int32 AssetDataLimit,
	int32 HandleLimit,
	int32 RequestedAssetLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("input"), Input);

	FString PackageName;
	FString ObjectPath;
	FString ResolveError;
	const bool bResolved = ResolveAsyncLoadProbeInput(Input, PackageName, ObjectPath, ResolveError);
	Data->SetBoolField(TEXT("resolved"), bResolved);
	if (!bResolved)
	{
		Data->SetStringField(TEXT("error"), ResolveError);
		return Data;
	}

	const FName PackageFName(*PackageName);
	Data->SetStringField(TEXT("package_name"), PackageName);
	Data->SetStringField(TEXT("object_path"), ObjectPath);
	Data->SetBoolField(TEXT("valid_long_package_name"), FPackageName::IsValidLongPackageName(PackageName, true));
	Data->SetBoolField(TEXT("valid_object_path"), FPackageName::IsValidObjectPath(ObjectPath));

	FString PackageFilename;
	const bool bPackageExistsOnDisk = FPackageName::DoesPackageExist(PackageName, &PackageFilename);
	Data->SetBoolField(TEXT("package_exists_on_disk"), bPackageExistsOnDisk);
	Data->SetStringField(TEXT("package_filename"), PackageFilename);
	Data->SetBoolField(TEXT("loaded_package_present"), FindPackage(nullptr, *PackageName) != nullptr);

	const FSoftObjectPath SoftObjectPath(ObjectPath);
	UObject* LoadedObject = SoftObjectPath.IsValid() ? SoftObjectPath.ResolveObject() : nullptr;
	Data->SetBoolField(TEXT("loaded_object_present"), LoadedObject != nullptr);
	if (LoadedObject)
	{
		Data->SetObjectField(TEXT("loaded_object"), BuildRuntimeObjectReferenceJson(LoadedObject));
	}

	const float AsyncLoadPercentage = GetAsyncLoadPercentage(PackageFName);
	Data->SetNumberField(TEXT("async_load_percentage"), AsyncLoadPercentage);
	Data->SetBoolField(TEXT("async_load_active"), AsyncLoadPercentage >= 0.0f);

	TArray<FAssetData> PackageAssets;
	AssetRegistry.GetAssetsByPackageName(PackageFName, PackageAssets);
	Data->SetNumberField(TEXT("asset_registry_asset_count"), PackageAssets.Num());
	TArray<TSharedPtr<FJsonValue>> AssetsJson;
	for (const FAssetData& AssetData : PackageAssets)
	{
		if (AssetsJson.Num() >= AssetDataLimit)
		{
			break;
		}
		AssetsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeAssetDataSummaryJson(AssetData)));
	}
	Data->SetNumberField(TEXT("returned_asset_data_count"), AssetsJson.Num());
	Data->SetBoolField(TEXT("asset_data_truncated"), PackageAssets.Num() > AssetsJson.Num());
	Data->SetArrayField(TEXT("asset_data"), AssetsJson);

	FSoftObjectPath StreamablePath = SoftObjectPath;
	if ((!StreamablePath.IsValid() || StreamablePath.IsNull()) && PackageAssets.Num() > 0)
	{
		StreamablePath = PackageAssets[0].GetSoftObjectPath();
		Data->SetStringField(TEXT("streamable_probe_path"), StreamablePath.ToString());
	}
	else
	{
		Data->SetStringField(TEXT("streamable_probe_path"), StreamablePath.ToString());
	}

	Data->SetBoolField(TEXT("streamable_manager_available"), StreamableManager != nullptr);
	if (StreamableManager && StreamablePath.IsValid())
	{
		Data->SetBoolField(TEXT("streamable_async_load_complete"), StreamableManager->IsAsyncLoadComplete(StreamablePath));

		TArray<TSharedRef<FStreamableHandle>> ActiveHandles;
		TArray<TSharedRef<FStreamableHandle>> ManagedHandles;
		StreamableManager->GetActiveHandles(StreamablePath, ActiveHandles, false);
		StreamableManager->GetActiveHandles(StreamablePath, ManagedHandles, true);
		Data->SetNumberField(TEXT("streamable_active_handle_count"), ActiveHandles.Num());
		Data->SetNumberField(TEXT("streamable_managed_active_handle_count"), ManagedHandles.Num());

		if (bIncludeStreamableHandles)
		{
			TArray<TSharedPtr<FJsonValue>> HandlesJson;
			for (const TSharedRef<FStreamableHandle>& Handle : ActiveHandles)
			{
				if (HandlesJson.Num() >= HandleLimit)
				{
					break;
				}
				HandlesJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeStreamableHandleJson(Handle, RequestedAssetLimit)));
			}
			Data->SetNumberField(TEXT("returned_streamable_handle_count"), HandlesJson.Num());
			Data->SetBoolField(TEXT("streamable_handles_truncated"), ActiveHandles.Num() > HandlesJson.Num());
			Data->SetArrayField(TEXT("streamable_handles"), HandlesJson);
		}
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildStreamingManagerJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("streaming_manager_shutdown"), IStreamingManager::HasShutdown());
	if (IStreamingManager::HasShutdown())
	{
		return Data;
	}

	FStreamingManagerCollection& StreamingManager = IStreamingManager::Get();
	Data->SetBoolField(TEXT("streaming_enabled"), StreamingManager.IsStreamingEnabled());
	Data->SetBoolField(TEXT("texture_streaming_enabled"), StreamingManager.IsTextureStreamingEnabled());
	Data->SetBoolField(TEXT("render_asset_texture_streaming_enabled"), StreamingManager.IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::Texture));
	Data->SetBoolField(TEXT("render_asset_static_mesh_streaming_enabled"), StreamingManager.IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::StaticMesh));
	Data->SetBoolField(TEXT("render_asset_skeletal_mesh_streaming_enabled"), StreamingManager.IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::SkeletalMesh));
	Data->SetNumberField(TEXT("wanting_resource_count"), StreamingManager.GetNumWantingResources());
	Data->SetNumberField(TEXT("wanting_resource_id"), StreamingManager.GetNumWantingResourcesID());
	Data->SetNumberField(TEXT("streaming_view_count"), StreamingManager.GetNumViews());

	IRenderAssetStreamingManager& RenderAssetStreaming = StreamingManager.GetRenderAssetStreamingManager();
	TSharedPtr<FJsonObject> RenderAssetsJson = MakeShared<FJsonObject>();
	RenderAssetsJson->SetNumberField(TEXT("wanting_resource_count"), RenderAssetStreaming.GetNumWantingResources());
	RenderAssetsJson->SetNumberField(TEXT("wanting_resource_id"), RenderAssetStreaming.GetNumWantingResourcesID());
	RenderAssetsJson->SetNumberField(TEXT("pool_size_bytes"), static_cast<double>(RenderAssetStreaming.GetPoolSize()));
	RenderAssetsJson->SetNumberField(TEXT("required_pool_size_bytes"), static_cast<double>(RenderAssetStreaming.GetRequiredPoolSize()));
	RenderAssetsJson->SetNumberField(TEXT("memory_over_budget_bytes"), static_cast<double>(RenderAssetStreaming.GetMemoryOverBudget()));
	RenderAssetsJson->SetNumberField(TEXT("max_ever_required_bytes"), static_cast<double>(RenderAssetStreaming.GetMaxEverRequired()));
	RenderAssetsJson->SetNumberField(TEXT("cached_mips"), RenderAssetStreaming.GetCachedMips());
	Data->SetObjectField(TEXT("render_asset_streaming"), RenderAssetsJson);

	return Data;
}

TSharedPtr<FJsonObject> BuildLevelStreamingJson(ULevelStreaming* StreamingLevel)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(StreamingLevel);
	if (!StreamingLevel)
	{
		return Data;
	}

	Data->SetStringField(TEXT("world_asset_package"), StreamingLevel->GetWorldAssetPackageName());
	Data->SetStringField(TEXT("world_asset_package_name"), StreamingLevel->GetWorldAssetPackageFName().ToString());
	Data->SetStringField(TEXT("package_name_to_load"), StreamingLevel->PackageNameToLoad.ToString());
	Data->SetStringField(TEXT("state"), EnumToString(StreamingLevel->GetLevelStreamingState()));
	Data->SetBoolField(TEXT("should_be_loaded"), StreamingLevel->ShouldBeLoaded());
	Data->SetBoolField(TEXT("should_be_visible"), StreamingLevel->ShouldBeVisible());
	Data->SetBoolField(TEXT("should_be_visible_flag"), StreamingLevel->GetShouldBeVisibleFlag());
	Data->SetBoolField(TEXT("should_block_on_load"), StreamingLevel->bShouldBlockOnLoad != 0);
	Data->SetBoolField(TEXT("should_block_on_unload"), StreamingLevel->ShouldBlockOnUnload());
	Data->SetBoolField(TEXT("should_be_always_loaded"), StreamingLevel->ShouldBeAlwaysLoaded());
	Data->SetBoolField(TEXT("has_loaded_level"), StreamingLevel->HasLoadedLevel());
	Data->SetBoolField(TEXT("load_request_pending"), StreamingLevel->HasLoadRequestPending());
	Data->SetBoolField(TEXT("requesting_unload_and_removal"), StreamingLevel->GetIsRequestingUnloadAndRemoval());
	Data->SetBoolField(TEXT("waiting_net_visibility_ack_visible"), StreamingLevel->IsWaitingForNetVisibilityTransactionAck(ENetLevelVisibilityRequest::MakingVisible));
	Data->SetBoolField(TEXT("waiting_net_visibility_ack_invisible"), StreamingLevel->IsWaitingForNetVisibilityTransactionAck(ENetLevelVisibilityRequest::MakingInvisible));
	Data->SetNumberField(TEXT("priority"), StreamingLevel->GetPriority());
	Data->SetNumberField(TEXT("level_lod_index"), StreamingLevel->GetLevelLODIndex());
	Data->SetNumberField(TEXT("async_request_count"), StreamingLevel->GetAsyncRequestIDs().Num());
	Data->SetNumberField(TEXT("lod_package_count"), StreamingLevel->LODPackageNames.Num());

	if (ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel())
	{
		TSharedPtr<FJsonObject> LoadedLevelJson = BuildRuntimeObjectReferenceJson(LoadedLevel);
		LoadedLevelJson->SetStringField(TEXT("package_name"), LoadedLevel->GetOutermost() ? LoadedLevel->GetOutermost()->GetName() : TEXT(""));
		LoadedLevelJson->SetNumberField(TEXT("actor_slot_count"), LoadedLevel->Actors.Num());
		Data->SetObjectField(TEXT("loaded_level"), LoadedLevelJson);
	}
	else
	{
		Data->SetObjectField(TEXT("loaded_level"), BuildRuntimeObjectReferenceJson(nullptr));
	}

	return Data;
}

AActor* FindRuntimeActorForAI(UWorld* World, const FString& ActorPath, const FString& ActorLabel, const FString& ActorName)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (!ActorPath.IsEmpty() && Actor->GetPathName() == ActorPath) return Actor;
		if (!ActorLabel.IsEmpty() && Actor->GetActorLabel() == ActorLabel) return Actor;
		if (!ActorName.IsEmpty() && Actor->GetName() == ActorName) return Actor;
	}
	return nullptr;
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
		if (AActor* Actor = FindRuntimeActorForAI(World, ActorPath, ActorLabel, ActorName))
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

TSharedPtr<FJsonObject> BuildRuntimeComponentJson(UActorComponent* Component)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Component)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Component->GetName());
	Data->SetStringField(TEXT("path"), Component->GetPathName());
	Data->SetStringField(TEXT("class"), Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("registered"), Component->IsRegistered());
	Data->SetBoolField(TEXT("active"), Component->IsActive());

	if (AActor* Owner = Component->GetOwner())
	{
		Data->SetStringField(TEXT("owner_name"), Owner->GetName());
		Data->SetStringField(TEXT("owner_label"), Owner->GetActorLabel());
		Data->SetStringField(TEXT("owner_path"), Owner->GetPathName());
	}

	TArray<TSharedPtr<FJsonValue>> Tags;
	for (const FName& Tag : Component->ComponentTags)
	{
		Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	Data->SetArrayField(TEXT("tags"), Tags);
	return Data;
}

FString CollisionEnabledToString(ECollisionEnabled::Type Value)
{
	switch (Value)
	{
	case ECollisionEnabled::NoCollision:
		return TEXT("NoCollision");
	case ECollisionEnabled::QueryOnly:
		return TEXT("QueryOnly");
	case ECollisionEnabled::PhysicsOnly:
		return TEXT("PhysicsOnly");
	case ECollisionEnabled::QueryAndPhysics:
		return TEXT("QueryAndPhysics");
	case ECollisionEnabled::ProbeOnly:
		return TEXT("ProbeOnly");
	case ECollisionEnabled::QueryAndProbe:
		return TEXT("QueryAndProbe");
	default:
		return FString::Printf(TEXT("CollisionEnabled_%d"), static_cast<int32>(Value));
	}
}

FString CollisionResponseToString(ECollisionResponse Value)
{
	switch (Value)
	{
	case ECR_Ignore:
		return TEXT("Ignore");
	case ECR_Overlap:
		return TEXT("Overlap");
	case ECR_Block:
		return TEXT("Block");
	default:
		return FString::Printf(TEXT("CollisionResponse_%d"), static_cast<int32>(Value));
	}
}

FString CollisionChannelToString(ECollisionChannel Channel)
{
	if (const UEnum* ChannelEnum = StaticEnum<ECollisionChannel>())
	{
		return ChannelEnum->GetNameStringByValue(static_cast<int64>(Channel));
	}
	return FString::Printf(TEXT("CollisionChannel_%d"), static_cast<int32>(Channel));
}

FString ComponentMobilityToString(EComponentMobility::Type Mobility)
{
	switch (Mobility)
	{
	case EComponentMobility::Static:
		return TEXT("Static");
	case EComponentMobility::Stationary:
		return TEXT("Stationary");
	case EComponentMobility::Movable:
		return TEXT("Movable");
	default:
		return FString::Printf(TEXT("Mobility_%d"), static_cast<int32>(Mobility));
	}
}

void IncrementStringCounter(TMap<FString, int32>& Counter, const FString& Key)
{
	++Counter.FindOrAdd(Key);
}

TArray<TSharedPtr<FJsonValue>> BuildStringCounterJson(const TMap<FString, int32>& Counter, const TCHAR* NameField)
{
	TArray<FString> Keys;
	Counter.GetKeys(Keys);
	Keys.Sort();

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& Key : Keys)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(NameField, Key);
		Entry->SetNumberField(TEXT("count"), Counter.FindRef(Key));
		Values.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return Values;
}

void AddReflectedSettingsPropertyJson(TSharedPtr<FJsonObject> Target, const UObject* Settings, const TCHAR* PropertyName, const TCHAR* JsonName)
{
	if (!Target.IsValid() || !Settings)
	{
		return;
	}

	const FProperty* Property = Settings->GetClass() ? Settings->GetClass()->FindPropertyByName(PropertyName) : nullptr;
	if (!Property)
	{
		return;
	}

	FString Value;
	Property->ExportText_InContainer(0, Value, Settings, Settings, const_cast<UObject*>(Settings), PPF_None);
	Target->SetStringField(JsonName, Value);
}

TSharedPtr<FJsonObject> BuildRuntimePhysicsSettingsJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const UPhysicsSettings* Settings = UPhysicsSettings::Get();
	Data->SetBoolField(TEXT("present"), Settings != nullptr);
	if (!Settings)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("object"), BuildRuntimeObjectReferenceJson(Settings));
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("DefaultGravityZ"), TEXT("default_gravity_z"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("DefaultTerminalVelocity"), TEXT("default_terminal_velocity"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("DefaultFluidFriction"), TEXT("default_fluid_friction"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("SimulateScratchMemorySize"), TEXT("simulate_scratch_memory_size"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("RagdollAggregateThreshold"), TEXT("ragdoll_aggregate_threshold"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("bEnableEnhancedDeterminism"), TEXT("enable_enhanced_determinism"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("MinDeltaVelocityForHitEvents"), TEXT("min_delta_velocity_for_hit_events"));
	Data->SetObjectField(TEXT("properties"), Properties);
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimePhysicsWorldJson(UWorld* World)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), World != nullptr);
	if (!World)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("physics_scene_present"), World->GetPhysicsScene() != nullptr);
	Data->SetNumberField(TEXT("gravity_z"), World->GetGravityZ());
	Data->SetNumberField(TEXT("default_gravity_z"), World->GetDefaultGravityZ());
	Data->SetBoolField(TEXT("default_physics_volume_created"), World->HasDefaultPhysicsVolume());
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildCollisionResponsesJson(UPrimitiveComponent* Component, int32& OutBlockCount, int32& OutOverlapCount, int32& OutIgnoreCount)
{
	OutBlockCount = 0;
	OutOverlapCount = 0;
	OutIgnoreCount = 0;

	TArray<TSharedPtr<FJsonValue>> Responses;
	if (!Component)
	{
		return Responses;
	}

	const int32 MaxSerializableChannel = static_cast<int32>(ECC_OverlapAll_Deprecated);
	for (int32 ChannelIndex = 0; ChannelIndex < MaxSerializableChannel; ++ChannelIndex)
	{
		const ECollisionChannel Channel = static_cast<ECollisionChannel>(ChannelIndex);
		const ECollisionResponse Response = Component->GetCollisionResponseToChannel(Channel);
		if (Response == ECR_Block)
		{
			++OutBlockCount;
		}
		else if (Response == ECR_Overlap)
		{
			++OutOverlapCount;
		}
		else if (Response == ECR_Ignore)
		{
			++OutIgnoreCount;
		}

		TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
		ResponseJson->SetStringField(TEXT("channel"), CollisionChannelToString(Channel));
		ResponseJson->SetNumberField(TEXT("channel_index"), ChannelIndex);
		ResponseJson->SetStringField(TEXT("response"), CollisionResponseToString(Response));
		Responses.Add(MakeShared<FJsonValueObject>(ResponseJson));
	}
	return Responses;
}

TSharedPtr<FJsonObject> BuildRuntimePrimitivePhysicsJson(UPrimitiveComponent* Component, bool bIncludeResponses)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeComponentJson(Component);
	if (!Component)
	{
		return Data;
	}

	const ECollisionEnabled::Type CollisionEnabled = Component->GetCollisionEnabled();
	const ECollisionChannel ObjectType = Component->GetCollisionObjectType();
	FBodyInstance* BodyInstance = Component->GetBodyInstance();
	const bool bBodyInstancePresent = BodyInstance != nullptr;
	const float Mass = bBodyInstancePresent ? Component->GetMass() : 0.0f;

	Data->SetStringField(TEXT("mobility"), ComponentMobilityToString(Component->Mobility.GetValue()));
	Data->SetObjectField(TEXT("component_location"), BuildVectorJson(Component->GetComponentLocation()));
	Data->SetObjectField(TEXT("component_velocity"), BuildVectorJson(Component->GetComponentVelocity()));
	Data->SetObjectField(TEXT("bounds_origin"), BuildVectorJson(Component->Bounds.Origin));
	Data->SetObjectField(TEXT("bounds_box_extent"), BuildVectorJson(Component->Bounds.BoxExtent));
	Data->SetNumberField(TEXT("bounds_sphere_radius"), Component->Bounds.SphereRadius);
	Data->SetStringField(TEXT("collision_profile_name"), Component->GetCollisionProfileName().ToString());
	Data->SetStringField(TEXT("collision_enabled"), CollisionEnabledToString(CollisionEnabled));
	Data->SetBoolField(TEXT("query_collision_enabled"), CollisionEnabledHasQuery(CollisionEnabled));
	Data->SetBoolField(TEXT("physics_collision_enabled"), CollisionEnabledHasPhysics(CollisionEnabled));
	Data->SetBoolField(TEXT("probe_collision_enabled"), CollisionEnabledHasProbe(CollisionEnabled));
	Data->SetStringField(TEXT("collision_object_type"), CollisionChannelToString(ObjectType));
	Data->SetNumberField(TEXT("collision_object_type_index"), static_cast<int32>(ObjectType));
	Data->SetBoolField(TEXT("generate_overlap_events"), Component->GetGenerateOverlapEvents());
	Data->SetBoolField(TEXT("simulating_physics"), Component->IsSimulatingPhysics());
	Data->SetBoolField(TEXT("gravity_enabled"), Component->IsGravityEnabled());
	Data->SetBoolField(TEXT("body_instance_present"), bBodyInstancePresent);
	Data->SetNumberField(TEXT("mass_kg"), Mass);

	if (BodyInstance)
	{
		Data->SetStringField(TEXT("body_collision_enabled"), CollisionEnabledToString(BodyInstance->GetCollisionEnabled()));
		Data->SetStringField(TEXT("body_collision_profile_name"), BodyInstance->GetCollisionProfileName().ToString());
	}

	int32 BlockResponseCount = 0;
	int32 OverlapResponseCount = 0;
	int32 IgnoreResponseCount = 0;
	TArray<TSharedPtr<FJsonValue>> Responses = BuildCollisionResponsesJson(Component, BlockResponseCount, OverlapResponseCount, IgnoreResponseCount);
	Data->SetNumberField(TEXT("block_response_count"), BlockResponseCount);
	Data->SetNumberField(TEXT("overlap_response_count"), OverlapResponseCount);
	Data->SetNumberField(TEXT("ignore_response_count"), IgnoreResponseCount);
	Data->SetBoolField(TEXT("include_responses"), bIncludeResponses);
	if (bIncludeResponses)
	{
		Data->SetArrayField(TEXT("responses"), Responses);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeReplicationComponentJson(UActorComponent* Component)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Component)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Component->GetName());
	Data->SetStringField(TEXT("path"), Component->GetPathName());
	Data->SetStringField(TEXT("class"), Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("registered"), Component->IsRegistered());
	Data->SetBoolField(TEXT("active"), Component->IsActive());
	Data->SetBoolField(TEXT("replicated"), Component->GetIsReplicated());
	Data->SetBoolField(TEXT("ready_for_replication"), Component->IsReadyForReplication());
	Data->SetStringField(TEXT("owner_role"), NetRoleToString(Component->GetOwnerRole()));
	Data->SetStringField(TEXT("net_mode"), NetModeToString(Component->GetNetMode()));
	if (AActor* Owner = Component->GetOwner())
	{
		Data->SetStringField(TEXT("owner_name"), Owner->GetName());
		Data->SetStringField(TEXT("owner_path"), Owner->GetPathName());
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeActorReplicationJson(AActor* Actor, bool bIncludeComponents, int32 ComponentLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Actor)
	{
		return Data;
	}

	Data->SetStringField(TEXT("net_mode"), NetModeToString(Actor->GetNetMode()));
	Data->SetStringField(TEXT("local_role"), NetRoleToString(Actor->GetLocalRole()));
	Data->SetStringField(TEXT("remote_role"), NetRoleToString(Actor->GetRemoteRole()));
	Data->SetBoolField(TEXT("has_authority"), Actor->HasAuthority());
	Data->SetBoolField(TEXT("replicated"), Actor->GetIsReplicated());
	Data->SetBoolField(TEXT("replicates_movement"), Actor->IsReplicatingMovement());
	Data->SetBoolField(TEXT("tear_off"), Actor->GetTearOff());
	Data->SetBoolField(TEXT("net_temporary"), Actor->bNetTemporary != 0);
	Data->SetBoolField(TEXT("net_startup"), Actor->IsNetStartupActor());
	Data->SetBoolField(TEXT("net_load_on_client"), Actor->bNetLoadOnClient != 0);
	Data->SetBoolField(TEXT("always_relevant"), Actor->bAlwaysRelevant != 0);
	Data->SetBoolField(TEXT("only_relevant_to_owner"), Actor->bOnlyRelevantToOwner != 0);
	Data->SetBoolField(TEXT("net_use_owner_relevancy"), Actor->bNetUseOwnerRelevancy != 0);
	Data->SetBoolField(TEXT("relevant_for_network_replays"), Actor->bRelevantForNetworkReplays != 0);
	Data->SetStringField(TEXT("net_dormancy"), NetDormancyToString(Actor->NetDormancy));
	Data->SetNumberField(TEXT("net_update_frequency"), Actor->GetNetUpdateFrequency());
	Data->SetNumberField(TEXT("min_net_update_frequency"), Actor->GetMinNetUpdateFrequency());
	Data->SetNumberField(TEXT("net_priority"), Actor->NetPriority);
	Data->SetNumberField(TEXT("net_cull_distance_squared"), Actor->GetNetCullDistanceSquared());
	Data->SetBoolField(TEXT("net_connection_present"), Actor->GetNetConnection() != nullptr);
	Data->SetBoolField(TEXT("net_owning_player_present"), Actor->GetNetOwningPlayerAnyRole() != nullptr);

	if (AActor* Owner = Actor->GetOwner())
	{
		Data->SetStringField(TEXT("owner_name"), Owner->GetName());
		Data->SetStringField(TEXT("owner_label"), Owner->GetActorLabel());
		Data->SetStringField(TEXT("owner_path"), Owner->GetPathName());
		Data->SetStringField(TEXT("owner_class"), Owner->GetClass() ? Owner->GetClass()->GetPathName() : TEXT(""));
	}

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	int32 ReplicatedComponentCount = 0;
	int32 ReadyComponentCount = 0;
	int32 MatchedReplicationComponentCount = 0;
	TArray<TSharedPtr<FJsonValue>> ComponentJson;
	for (UActorComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		const bool bReplicatedComponent = Component->GetIsReplicated();
		const bool bReadyComponent = Component->IsReadyForReplication();
		if (bReplicatedComponent)
		{
			++ReplicatedComponentCount;
		}
		if (bReadyComponent)
		{
			++ReadyComponentCount;
		}
		if (bReplicatedComponent || bReadyComponent)
		{
			++MatchedReplicationComponentCount;
		}
		if (bIncludeComponents && (bReplicatedComponent || bReadyComponent) && ComponentJson.Num() < ComponentLimit)
		{
			ComponentJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeReplicationComponentJson(Component)));
		}
	}

	Data->SetNumberField(TEXT("component_count"), Components.Num());
	Data->SetNumberField(TEXT("replicated_component_count"), ReplicatedComponentCount);
	Data->SetNumberField(TEXT("ready_component_count"), ReadyComponentCount);
	Data->SetNumberField(TEXT("matched_replication_component_count"), MatchedReplicationComponentCount);
	Data->SetNumberField(TEXT("returned_component_count"), ComponentJson.Num());
	Data->SetBoolField(TEXT("components_truncated"), bIncludeComponents && MatchedReplicationComponentCount > ComponentJson.Num());
	if (bIncludeComponents)
	{
		Data->SetArrayField(TEXT("components"), ComponentJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeObjectReferenceJson(const UObject* Object)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Object != nullptr);
	if (!Object)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Object->GetName());
	Data->SetStringField(TEXT("path"), Object->GetPathName());
	Data->SetStringField(TEXT("class"), Object->GetClass() ? Object->GetClass()->GetPathName() : TEXT(""));
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeSubsystemJson(const USubsystem* Subsystem)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(Subsystem);
	if (!Subsystem)
	{
		return Data;
	}

	if (const UObject* Outer = Subsystem->GetOuter())
	{
		Data->SetObjectField(TEXT("outer"), BuildRuntimeObjectReferenceJson(Outer));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeLocalPlayerJson(ULocalPlayer* LocalPlayer, UWorld* World, bool bIncludeSubsystems, int32 SubsystemLimit)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(LocalPlayer);
	if (!LocalPlayer)
	{
		return Data;
	}

	const FPlatformUserId PlatformUserId = LocalPlayer->GetPlatformUserId();
	Data->SetNumberField(TEXT("controller_id"), LocalPlayer->GetControllerId());
	Data->SetNumberField(TEXT("platform_user_id"), PlatformUserId.GetInternalId());
	Data->SetBoolField(TEXT("platform_user_valid"), PlatformUserId.IsValid());
	Data->SetStringField(TEXT("nickname"), LocalPlayer->GetNickname());
	Data->SetObjectField(TEXT("origin"), BuildVector2DJson(LocalPlayer->Origin));
	Data->SetObjectField(TEXT("size"), BuildVector2DJson(LocalPlayer->Size));
	Data->SetBoolField(TEXT("sent_split_join"), LocalPlayer->bSentSplitJoin != 0);
	Data->SetBoolField(TEXT("emulate_splitscreen"), LocalPlayer->bEmulateSplitscreen);

	if (APlayerController* PlayerController = LocalPlayer->GetPlayerController(World))
	{
		Data->SetObjectField(TEXT("player_controller"), BuildRuntimeObjectReferenceJson(PlayerController));
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			Data->SetObjectField(TEXT("pawn"), BuildRuntimeActorJson(Pawn));
		}
	}

	if (LocalPlayer->ViewportClient)
	{
		Data->SetObjectField(TEXT("viewport_client"), BuildRuntimeObjectReferenceJson(LocalPlayer->ViewportClient));
	}

	if (bIncludeSubsystems)
	{
		const TArray<ULocalPlayerSubsystem*> Subsystems = LocalPlayer->GetSubsystemArrayCopy<ULocalPlayerSubsystem>();
		TArray<TSharedPtr<FJsonValue>> SubsystemsJson;
		for (ULocalPlayerSubsystem* Subsystem : Subsystems)
		{
			if (!Subsystem)
			{
				continue;
			}
			if (SubsystemsJson.Num() >= SubsystemLimit)
			{
				break;
			}
			SubsystemsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeSubsystemJson(Subsystem)));
		}

		Data->SetNumberField(TEXT("subsystem_count"), Subsystems.Num());
		Data->SetNumberField(TEXT("returned_subsystem_count"), SubsystemsJson.Num());
		Data->SetBoolField(TEXT("subsystems_truncated"), Subsystems.Num() > SubsystemsJson.Num());
		Data->SetArrayField(TEXT("subsystems"), SubsystemsJson);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeNetConnectionJson(UNetConnection* Connection, bool bIncludeURLOptions, int32 URLOptionLimit)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(Connection);
	if (!Connection)
	{
		return Data;
	}

	Data->SetStringField(TEXT("state"), LexToString(Connection->GetConnectionState()));
	Data->SetBoolField(TEXT("closing_or_closed"), Connection->IsClosingOrClosed());
	Data->SetBoolField(TEXT("pending_destroy"), Connection->bPendingDestroy != 0);
	Data->SetBoolField(TEXT("internal_ack"), Connection->IsInternalAck());
	Data->SetBoolField(TEXT("replay"), Connection->IsReplay());
	Data->SetBoolField(TEXT("force_initial_dirty"), Connection->IsForceInitialDirty());
	Data->SetBoolField(TEXT("unlimited_bunch_size_allowed"), Connection->IsUnlimitedBunchSizeAllowed());
	Data->SetStringField(TEXT("remote_address"), Connection->RemoteAddressToString());
	Data->SetStringField(TEXT("low_level_remote_address"), Connection->LowLevelGetRemoteAddress(true));
	Data->SetStringField(TEXT("client_world_package_name"), Connection->GetClientWorldPackageName().ToString());
	Data->SetStringField(TEXT("client_login_state"), EClientLoginState::ToString(Connection->ClientLoginState));
	Data->SetStringField(TEXT("request_url"), Connection->RequestURL);
	Data->SetObjectField(TEXT("url"), BuildURLJson(Connection->URL, bIncludeURLOptions, URLOptionLimit));
	Data->SetNumberField(TEXT("current_net_speed"), Connection->CurrentNetSpeed);
	Data->SetNumberField(TEXT("configured_internet_speed"), Connection->ConfiguredInternetSpeed);
	Data->SetNumberField(TEXT("configured_lan_speed"), Connection->ConfiguredLanSpeed);
	Data->SetNumberField(TEXT("max_packet"), Connection->MaxPacket);
	Data->SetNumberField(TEXT("packet_overhead"), Connection->PacketOverhead);
	Data->SetNumberField(TEXT("queued_bits"), Connection->QueuedBits);
	Data->SetNumberField(TEXT("tick_count"), Connection->TickCount);
	Data->SetNumberField(TEXT("open_channel_count"), Connection->OpenChannels.Num());
	Data->SetNumberField(TEXT("child_connection_count"), Connection->Children.Num());
	Data->SetNumberField(TEXT("sent_temporary_actor_count"), Connection->SentTemporaries.Num());
	Data->SetNumberField(TEXT("last_receive_time"), Connection->LastReceiveTime);
	Data->SetNumberField(TEXT("last_receive_realtime"), Connection->LastReceiveRealtime);
	Data->SetNumberField(TEXT("last_good_packet_realtime"), Connection->LastGoodPacketRealtime);
	Data->SetNumberField(TEXT("last_send_time"), Connection->LastSendTime);
	Data->SetNumberField(TEXT("last_tick_time"), Connection->LastTickTime);
	Data->SetNumberField(TEXT("connect_time"), Connection->GetConnectTime());
	Data->SetNumberField(TEXT("last_recv_ack_time"), Connection->GetLastRecvAckTime());

	Data->SetObjectField(TEXT("driver"), BuildRuntimeObjectReferenceJson(Connection->GetDriver()));
	Data->SetObjectField(TEXT("player_controller"), BuildRuntimeObjectReferenceJson(Connection->PlayerController));
	Data->SetObjectField(TEXT("owning_actor"), BuildRuntimeObjectReferenceJson(Connection->OwningActor));
	Data->SetObjectField(TEXT("view_target"), BuildRuntimeObjectReferenceJson(Connection->ViewTarget));
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeNetDriverJson(UNetDriver* NetDriver, bool bIncludeConnections = false, int32 ConnectionLimit = 0, bool bIncludeURLOptions = false, int32 URLOptionLimit = 0)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(NetDriver);
	if (!NetDriver)
	{
		return Data;
	}

	Data->SetStringField(TEXT("net_driver_name"), NetDriver->NetDriverName.ToString());
	Data->SetStringField(TEXT("net_driver_definition"), NetDriver->GetNetDriverDefinition().ToString());
	Data->SetStringField(TEXT("net_mode"), NetModeToString(NetDriver->GetNetMode()));
	Data->SetBoolField(TEXT("is_server"), NetDriver->IsServer());
	Data->SetBoolField(TEXT("server_connection_present"), NetDriver->ServerConnection != nullptr);
	Data->SetNumberField(TEXT("client_connection_count"), NetDriver->ClientConnections.Num());
	Data->SetNumberField(TEXT("recently_disconnected_client_count"), NetDriver->RecentlyDisconnectedClients.Num());
	Data->SetNumberField(TEXT("channel_definition_count"), NetDriver->ChannelDefinitions.Num());
	Data->SetBoolField(TEXT("replication_driver_present"), NetDriver->GetReplicationDriver() != nullptr);
	Data->SetBoolField(TEXT("include_connections"), bIncludeConnections);
	Data->SetNumberField(TEXT("connection_limit"), ConnectionLimit);

	if (NetDriver->ServerConnection)
	{
		Data->SetObjectField(TEXT("server_connection"), bIncludeConnections
			? BuildRuntimeNetConnectionJson(NetDriver->ServerConnection, bIncludeURLOptions, URLOptionLimit)
			: BuildRuntimeObjectReferenceJson(NetDriver->ServerConnection));
	}
	if (NetDriver->NetConnectionClass)
	{
		Data->SetStringField(TEXT("net_connection_class"), NetDriver->NetConnectionClass->GetPathName());
	}
	if (NetDriver->ChildNetConnectionClass)
	{
		Data->SetStringField(TEXT("child_net_connection_class"), NetDriver->ChildNetConnectionClass->GetPathName());
	}
	if (NetDriver->ReplicationDriverClass)
	{
		Data->SetStringField(TEXT("replication_driver_class"), NetDriver->ReplicationDriverClass->GetPathName());
	}

	if (bIncludeConnections)
	{
		TArray<TSharedPtr<FJsonValue>> ClientConnectionsJson;
		for (UNetConnection* ClientConnection : NetDriver->ClientConnections)
		{
			if (!ClientConnection)
			{
				continue;
			}
			if (ClientConnectionsJson.Num() >= ConnectionLimit)
			{
				break;
			}
			ClientConnectionsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeNetConnectionJson(ClientConnection, bIncludeURLOptions, URLOptionLimit)));
		}
		Data->SetNumberField(TEXT("returned_client_connection_count"), ClientConnectionsJson.Num());
		Data->SetBoolField(TEXT("client_connections_truncated"), NetDriver->ClientConnections.Num() > ClientConnectionsJson.Num());
		Data->SetArrayField(TEXT("client_connections"), ClientConnectionsJson);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildColorJson(const FColor& Color)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("r"), Color.R);
	Data->SetNumberField(TEXT("g"), Color.G);
	Data->SetNumberField(TEXT("b"), Color.B);
	Data->SetNumberField(TEXT("a"), Color.A);
	return Data;
}

TSharedPtr<FJsonObject> BuildAISenseIDJson(const FAISenseID& SenseID, const UAIPerceptionComponent* Perception)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), SenseID.IsValid());
	Data->SetNumberField(TEXT("index"), SenseID.IsValid() ? static_cast<int32>(SenseID.Index) : -1);
	Data->SetStringField(TEXT("name"), SenseID.Name.ToString());
	if (Perception && SenseID.IsValid())
	{
		if (const UAISenseConfig* SenseConfig = Perception->GetSenseConfig(SenseID))
		{
			Data->SetStringField(TEXT("config_name"), SenseConfig->GetSenseName());
			Data->SetObjectField(TEXT("config"), BuildRuntimeObjectReferenceJson(SenseConfig));
			if (UClass* SenseClass = SenseConfig->GetSenseImplementation().Get())
			{
				Data->SetObjectField(TEXT("sense_class"), BuildRuntimeObjectReferenceJson(SenseClass));
			}
		}
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildAISenseConfigJson(const UAISenseConfig* SenseConfig, const UAIPerceptionComponent* Perception)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(SenseConfig);
	if (!SenseConfig)
	{
		return Data;
	}

	const FAISenseID SenseID = SenseConfig->GetSenseID();
	Data->SetStringField(TEXT("sense_name"), SenseConfig->GetSenseName());
	Data->SetObjectField(TEXT("sense_id"), BuildAISenseIDJson(SenseID, Perception));
	Data->SetNumberField(TEXT("max_age"), SenseConfig->GetMaxAge());
	Data->SetBoolField(TEXT("starts_enabled"), SenseConfig->GetStartsEnabled());
	Data->SetObjectField(TEXT("debug_color"), BuildColorJson(SenseConfig->GetDebugColor()));
	if (UClass* SenseClass = SenseConfig->GetSenseImplementation().Get())
	{
		Data->SetObjectField(TEXT("sense_class"), BuildRuntimeObjectReferenceJson(SenseClass));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildAIStimulusJson(const FAIStimulus& Stimulus, const UAIPerceptionComponent* Perception)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), Stimulus.IsValid());
	Data->SetBoolField(TEXT("active"), Stimulus.IsActive());
	Data->SetBoolField(TEXT("expired"), Stimulus.IsExpired());
	Data->SetBoolField(TEXT("successfully_sensed"), Stimulus.WasSuccessfullySensed());
	Data->SetNumberField(TEXT("age"), Stimulus.GetAge());
	Data->SetNumberField(TEXT("strength"), Stimulus.Strength);
	Data->SetStringField(TEXT("tag"), Stimulus.Tag.ToString());
	Data->SetObjectField(TEXT("sense_id"), BuildAISenseIDJson(Stimulus.Type, Perception));
	if (Perception)
	{
		if (UClass* SenseClass = UAIPerceptionSystem::GetSenseClassForStimulus(const_cast<UAIPerceptionComponent*>(Perception), Stimulus).Get())
		{
			Data->SetObjectField(TEXT("sense_class"), BuildRuntimeObjectReferenceJson(SenseClass));
		}
	}
	Data->SetObjectField(TEXT("stimulus_location"), BuildVectorJson(Stimulus.StimulusLocation));
	Data->SetObjectField(TEXT("receiver_location"), BuildVectorJson(Stimulus.ReceiverLocation));
	return Data;
}

TSharedPtr<FJsonObject> BuildActorPerceptionInfoJson(
	const FActorPerceptionInfo& Info,
	const UAIPerceptionComponent* Perception,
	bool bIncludeStimuli,
	int32 StimulusLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AActor* Target = Info.Target.Get();
	Data->SetBoolField(TEXT("target_valid"), Target != nullptr);
	if (Target)
	{
		Data->SetObjectField(TEXT("target"), BuildRuntimeActorJson(Target));
	}
	else
	{
		Data->SetObjectField(TEXT("target"), BuildRuntimeObjectReferenceJson(nullptr));
	}

	Data->SetBoolField(TEXT("hostile"), Info.bIsHostile != 0);
	Data->SetBoolField(TEXT("friendly"), Info.bIsFriendly != 0);
	Data->SetBoolField(TEXT("has_any_known_stimulus"), Info.HasAnyKnownStimulus());
	Data->SetBoolField(TEXT("has_any_current_stimulus"), Info.HasAnyCurrentStimulus());
	Data->SetObjectField(TEXT("dominant_sense"), BuildAISenseIDJson(Info.DominantSense, Perception));

	float LastStimulusAge = 0.0f;
	const FVector LastStimulusLocation = Info.GetLastStimulusLocation(&LastStimulusAge);
	Data->SetNumberField(TEXT("last_stimulus_age"), LastStimulusAge);
	Data->SetObjectField(TEXT("last_stimulus_location"), BuildVectorJson(LastStimulusLocation));

	int32 ValidStimulusCount = 0;
	int32 ActiveStimulusCount = 0;
	int32 SuccessfulStimulusCount = 0;
	int32 ExpiredStimulusCount = 0;
	TArray<TSharedPtr<FJsonValue>> StimuliJson;
	for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
	{
		if (!Stimulus.IsValid())
		{
			continue;
		}

		++ValidStimulusCount;
		if (Stimulus.IsActive())
		{
			++ActiveStimulusCount;
		}
		if (Stimulus.WasSuccessfullySensed())
		{
			++SuccessfulStimulusCount;
		}
		if (Stimulus.IsExpired())
		{
			++ExpiredStimulusCount;
		}
		if (bIncludeStimuli && StimuliJson.Num() < StimulusLimit)
		{
			StimuliJson.Add(MakeShared<FJsonValueObject>(BuildAIStimulusJson(Stimulus, Perception)));
		}
	}

	Data->SetNumberField(TEXT("stored_stimulus_slot_count"), Info.LastSensedStimuli.Num());
	Data->SetNumberField(TEXT("valid_stimulus_count"), ValidStimulusCount);
	Data->SetNumberField(TEXT("active_stimulus_count"), ActiveStimulusCount);
	Data->SetNumberField(TEXT("successful_stimulus_count"), SuccessfulStimulusCount);
	Data->SetNumberField(TEXT("expired_stimulus_count"), ExpiredStimulusCount);
	Data->SetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
	if (bIncludeStimuli)
	{
		Data->SetNumberField(TEXT("returned_stimulus_count"), StimuliJson.Num());
		Data->SetBoolField(TEXT("stimuli_truncated"), ValidStimulusCount > StimuliJson.Num());
		Data->SetArrayField(TEXT("stimuli"), StimuliJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildAIPerceptionComponentJson(
	UAIPerceptionComponent* Perception,
	const FString& TargetNameFilter,
	bool bIncludeStimuli,
	int32 TargetLimit,
	int32 StimulusLimit)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeComponentJson(Perception);
	if (!Perception)
	{
		return Data;
	}

	const FPerceptionListenerID ListenerId = Perception->GetListenerId();
	Data->SetNumberField(TEXT("listener_id"), ListenerId.IsValid() ? static_cast<int32>(ListenerId.Index) : -1);
	Data->SetNumberField(TEXT("team_id"), Perception->GetTeamIdentifier().GetId());
	Data->SetObjectField(TEXT("dominant_sense"), BuildAISenseIDJson(Perception->GetDominantSenseID(), Perception));
	Data->SetObjectField(TEXT("body_actor"), BuildRuntimeObjectReferenceJson(Perception->GetBodyActor()));

	TArray<AActor*> CurrentlyPerceivedActors;
	Perception->GetCurrentlyPerceivedActors(nullptr, CurrentlyPerceivedActors);
	TArray<AActor*> KnownPerceivedActors;
	Perception->GetKnownPerceivedActors(nullptr, KnownPerceivedActors);
	TArray<AActor*> HostileActors;
	Perception->GetHostileActors(HostileActors);
	Data->SetNumberField(TEXT("currently_perceived_actor_count"), CurrentlyPerceivedActors.Num());
	Data->SetNumberField(TEXT("known_perceived_actor_count"), KnownPerceivedActors.Num());
	Data->SetNumberField(TEXT("hostile_actor_count"), HostileActors.Num());

	TArray<TSharedPtr<FJsonValue>> SenseConfigsJson;
	for (UAIPerceptionComponent::TAISenseConfigConstIterator It = Perception->GetSensesConfigIterator(); It; ++It)
	{
		const UAISenseConfig* SenseConfig = *It;
		if (!SenseConfig)
		{
			continue;
		}
		SenseConfigsJson.Add(MakeShared<FJsonValueObject>(BuildAISenseConfigJson(SenseConfig, Perception)));
	}
	Data->SetNumberField(TEXT("sense_config_count"), SenseConfigsJson.Num());
	Data->SetArrayField(TEXT("sense_configs"), SenseConfigsJson);

	auto TargetMatchesFilter = [&TargetNameFilter](AActor* Target)
	{
		if (TargetNameFilter.IsEmpty())
		{
			return true;
		}
		if (!Target)
		{
			return false;
		}
		return Target->GetName().Contains(TargetNameFilter) || Target->GetActorLabel().Contains(TargetNameFilter);
	};

	int32 MatchedTargetCount = 0;
	int32 ActiveTargetCount = 0;
	int32 KnownTargetCount = 0;
	int32 ValidStimulusCount = 0;
	int32 ActiveStimulusCount = 0;
	int32 ExpiredStimulusCount = 0;
	TArray<TSharedPtr<FJsonValue>> TargetsJson;
	for (UAIPerceptionComponent::FActorPerceptionContainer::TConstIterator It = Perception->GetPerceptualDataConstIterator(); It; ++It)
	{
		const FActorPerceptionInfo& Info = It->Value;
		AActor* Target = Info.Target.Get();
		if (!TargetMatchesFilter(Target))
		{
			continue;
		}

		++MatchedTargetCount;
		if (Info.HasAnyKnownStimulus())
		{
			++KnownTargetCount;
		}
		if (Info.HasAnyCurrentStimulus())
		{
			++ActiveTargetCount;
		}
		for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
		{
			if (!Stimulus.IsValid())
			{
				continue;
			}
			++ValidStimulusCount;
			if (Stimulus.IsActive())
			{
				++ActiveStimulusCount;
			}
			if (Stimulus.IsExpired())
			{
				++ExpiredStimulusCount;
			}
		}

		if (TargetsJson.Num() < TargetLimit)
		{
			TargetsJson.Add(MakeShared<FJsonValueObject>(BuildActorPerceptionInfoJson(Info, Perception, bIncludeStimuli, StimulusLimit)));
		}
	}

	Data->SetStringField(TEXT("target_name_filter"), TargetNameFilter);
	Data->SetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
	Data->SetNumberField(TEXT("target_limit"), TargetLimit);
	Data->SetNumberField(TEXT("stimulus_limit"), StimulusLimit);
	Data->SetNumberField(TEXT("matched_target_count"), MatchedTargetCount);
	Data->SetNumberField(TEXT("returned_target_count"), TargetsJson.Num());
	Data->SetBoolField(TEXT("targets_truncated"), MatchedTargetCount > TargetsJson.Num());
	Data->SetNumberField(TEXT("known_target_count"), KnownTargetCount);
	Data->SetNumberField(TEXT("active_target_count"), ActiveTargetCount);
	Data->SetNumberField(TEXT("valid_stimulus_count"), ValidStimulusCount);
	Data->SetNumberField(TEXT("active_stimulus_count"), ActiveStimulusCount);
	Data->SetNumberField(TEXT("expired_stimulus_count"), ExpiredStimulusCount);
	Data->SetArrayField(TEXT("targets"), TargetsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildGameplayTagContainerJson(const FGameplayTagContainer& Tags)
{
	TArray<FGameplayTag> TagArray;
	Tags.GetGameplayTagArray(TagArray);
	TagArray.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> TagValues;
	for (const FGameplayTag& Tag : TagArray)
	{
		TagValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("count"), TagArray.Num());
	Data->SetStringField(TEXT("text"), Tags.ToStringSimple());
	Data->SetArrayField(TEXT("items"), TagValues);
	return Data;
}

FString GameplayEffectReplicationModeToString(EGameplayEffectReplicationMode Mode)
{
	switch (Mode)
	{
	case EGameplayEffectReplicationMode::Minimal:
		return TEXT("Minimal");
	case EGameplayEffectReplicationMode::Mixed:
		return TEXT("Mixed");
	case EGameplayEffectReplicationMode::Full:
		return TEXT("Full");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityInstancingPolicyToString(EGameplayAbilityInstancingPolicy::Type Policy)
{
	switch (static_cast<int32>(Policy))
	{
	case 0:
		return TEXT("NonInstanced");
	case 1:
		return TEXT("InstancedPerActor");
	case 2:
		return TEXT("InstancedPerExecution");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityReplicationPolicyToString(EGameplayAbilityReplicationPolicy::Type Policy)
{
	switch (Policy)
	{
	case EGameplayAbilityReplicationPolicy::ReplicateNo:
		return TEXT("ReplicateNo");
	case EGameplayAbilityReplicationPolicy::ReplicateYes:
		return TEXT("ReplicateYes");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityNetExecutionPolicyToString(EGameplayAbilityNetExecutionPolicy::Type Policy)
{
	switch (Policy)
	{
	case EGameplayAbilityNetExecutionPolicy::LocalPredicted:
		return TEXT("LocalPredicted");
	case EGameplayAbilityNetExecutionPolicy::LocalOnly:
		return TEXT("LocalOnly");
	case EGameplayAbilityNetExecutionPolicy::ServerInitiated:
		return TEXT("ServerInitiated");
	case EGameplayAbilityNetExecutionPolicy::ServerOnly:
		return TEXT("ServerOnly");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityNetSecurityPolicyToString(EGameplayAbilityNetSecurityPolicy::Type Policy)
{
	switch (Policy)
	{
	case EGameplayAbilityNetSecurityPolicy::ClientOrServer:
		return TEXT("ClientOrServer");
	case EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution:
		return TEXT("ServerOnlyExecution");
	case EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination:
		return TEXT("ServerOnlyTermination");
	case EGameplayAbilityNetSecurityPolicy::ServerOnly:
		return TEXT("ServerOnly");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildGameplayAbilitySpecJson(const FGameplayAbilitySpec& Spec)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("handle"), Spec.Handle.ToString());
	Data->SetNumberField(TEXT("level"), Spec.Level);
	Data->SetNumberField(TEXT("input_id"), Spec.InputID);
	Data->SetNumberField(TEXT("active_count"), Spec.ActiveCount);
	Data->SetBoolField(TEXT("active"), Spec.IsActive());
	Data->SetBoolField(TEXT("input_pressed"), Spec.InputPressed != 0);
	Data->SetBoolField(TEXT("remove_after_activation"), Spec.RemoveAfterActivation != 0);
	Data->SetBoolField(TEXT("pending_remove"), Spec.PendingRemove != 0);
	Data->SetBoolField(TEXT("activate_once"), Spec.bActivateOnce != 0);
	Data->SetObjectField(TEXT("source_object"), BuildRuntimeObjectReferenceJson(Spec.SourceObject.Get()));
	Data->SetObjectField(TEXT("dynamic_source_tags"), BuildGameplayTagContainerJson(Spec.GetDynamicSpecSourceTags()));
	Data->SetNumberField(TEXT("replicated_instance_count"), Spec.ReplicatedInstances.Num());
	Data->SetNumberField(TEXT("non_replicated_instance_count"), Spec.NonReplicatedInstances.Num());
	Data->SetNumberField(TEXT("ability_instance_count"), Spec.GetAbilityInstances().Num());
	Data->SetStringField(TEXT("gameplay_effect_handle"), Spec.GameplayEffectHandle.ToString());

	UGameplayAbility* Ability = Spec.Ability.Get();
	Data->SetObjectField(TEXT("ability"), BuildRuntimeObjectReferenceJson(Ability));
	if (Ability)
	{
		Data->SetStringField(TEXT("ability_class"), Ability->GetClass() ? Ability->GetClass()->GetPathName() : TEXT(""));
		Data->SetStringField(TEXT("instancing_policy"), GameplayAbilityInstancingPolicyToString(Ability->GetInstancingPolicy()));
		Data->SetStringField(TEXT("replication_policy"), GameplayAbilityReplicationPolicyToString(Ability->GetReplicationPolicy()));
		Data->SetStringField(TEXT("net_execution_policy"), GameplayAbilityNetExecutionPolicyToString(Ability->GetNetExecutionPolicy()));
		Data->SetStringField(TEXT("net_security_policy"), GameplayAbilityNetSecurityPolicyToString(Ability->GetNetSecurityPolicy()));
		Data->SetBoolField(TEXT("replicate_input_directly"), Ability->bReplicateInputDirectly != 0);
		Data->SetObjectField(TEXT("asset_tags"), BuildGameplayTagContainerJson(Ability->GetAssetTags()));
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildActiveGameplayEffectJson(const FActiveGameplayEffect& ActiveEffect, UWorld* World)
{
	const FGameplayEffectSpec& Spec = ActiveEffect.Spec;
	const UGameplayEffect* EffectDef = Spec.Def.Get();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("handle"), ActiveEffect.Handle.ToString());
	Data->SetBoolField(TEXT("handle_valid"), ActiveEffect.Handle.IsValid());
	Data->SetObjectField(TEXT("effect"), BuildRuntimeObjectReferenceJson(EffectDef));
	Data->SetNumberField(TEXT("level"), Spec.GetLevel());
	Data->SetNumberField(TEXT("stack_count"), Spec.GetStackCount());
	Data->SetNumberField(TEXT("duration"), ActiveEffect.GetDuration());
	Data->SetNumberField(TEXT("period"), ActiveEffect.GetPeriod());
	Data->SetNumberField(TEXT("start_world_time"), ActiveEffect.StartWorldTime);
	Data->SetNumberField(TEXT("start_server_world_time"), ActiveEffect.StartServerWorldTime);
	Data->SetNumberField(TEXT("end_time"), ActiveEffect.GetEndTime());
	if (World)
	{
		Data->SetNumberField(TEXT("time_remaining"), ActiveEffect.GetTimeRemaining(World->GetTimeSeconds()));
	}
	Data->SetBoolField(TEXT("inhibited"), ActiveEffect.bIsInhibited);
	Data->SetBoolField(TEXT("pending_remove"), ActiveEffect.IsPendingRemove);
	Data->SetBoolField(TEXT("post_predict_object"), ActiveEffect.bPostPredictObject);
	Data->SetNumberField(TEXT("granted_ability_handle_count"), ActiveEffect.GrantedAbilityHandles.Num());

	FGameplayTagContainer AssetTags;
	Spec.GetAllAssetTags(AssetTags);
	FGameplayTagContainer GrantedTags;
	Spec.GetAllGrantedTags(GrantedTags);
	FGameplayTagContainer BlockedAbilityTags;
	Spec.GetAllBlockedAbilityTags(BlockedAbilityTags);
	Data->SetObjectField(TEXT("asset_tags"), BuildGameplayTagContainerJson(AssetTags));
	Data->SetObjectField(TEXT("granted_tags"), BuildGameplayTagContainerJson(GrantedTags));
	Data->SetObjectField(TEXT("blocked_ability_tags"), BuildGameplayTagContainerJson(BlockedAbilityTags));
	Data->SetObjectField(TEXT("dynamic_asset_tags"), BuildGameplayTagContainerJson(Spec.GetDynamicAssetTags()));
	Data->SetObjectField(TEXT("dynamic_granted_tags"), BuildGameplayTagContainerJson(Spec.DynamicGrantedTags));

	if (EffectDef)
	{
		Data->SetObjectField(TEXT("definition_asset_tags"), BuildGameplayTagContainerJson(EffectDef->GetAssetTags()));
		Data->SetObjectField(TEXT("definition_granted_tags"), BuildGameplayTagContainerJson(EffectDef->GetGrantedTags()));
		Data->SetObjectField(TEXT("definition_blocked_ability_tags"), BuildGameplayTagContainerJson(EffectDef->GetBlockedAbilityTags()));
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildAbilitySystemComponentJson(
	UAbilitySystemComponent* AbilitySystem,
	UWorld* World,
	bool bIncludeAbilities,
	bool bIncludeEffects,
	bool bIncludeAttributes,
	int32 AbilityLimit,
	int32 EffectLimit,
	int32 AttributeLimit)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeComponentJson(AbilitySystem);
	if (!AbilitySystem)
	{
		return Data;
	}

	Data->SetStringField(TEXT("replication_mode"), GameplayEffectReplicationModeToString(AbilitySystem->ReplicationMode));
	Data->SetNumberField(TEXT("generic_confirm_input_id"), AbilitySystem->GenericConfirmInputID);
	Data->SetNumberField(TEXT("generic_cancel_input_id"), AbilitySystem->GenericCancelInputID);
	Data->SetObjectField(TEXT("owner_actor"), BuildRuntimeActorJson(AbilitySystem->GetOwnerActor()));
	Data->SetObjectField(TEXT("avatar_actor"), BuildRuntimeActorJson(AbilitySystem->GetAvatarActor()));
	Data->SetObjectField(TEXT("owned_tags"), BuildGameplayTagContainerJson(AbilitySystem->GetOwnedGameplayTags()));

	FGameplayTagContainer BlockedAbilityTags;
	AbilitySystem->GetBlockedAbilityTags(BlockedAbilityTags);
	Data->SetObjectField(TEXT("blocked_ability_tags"), BuildGameplayTagContainerJson(BlockedAbilityTags));

	TArray<TSharedPtr<FJsonValue>> AttributeSetJson;
	for (const auto& AttributeSetObject : AbilitySystem->GetSpawnedAttributes())
	{
		const UAttributeSet* AttributeSet = AttributeSetObject;
		AttributeSetJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeObjectReferenceJson(AttributeSet)));
	}
	Data->SetNumberField(TEXT("attribute_set_count"), AttributeSetJson.Num());
	Data->SetArrayField(TEXT("attribute_sets"), AttributeSetJson);

	const TArray<FGameplayAbilitySpec>& Abilities = AbilitySystem->GetActivatableAbilities();
	int32 ActiveAbilityCount = 0;
	for (const FGameplayAbilitySpec& Spec : Abilities)
	{
		if (Spec.IsActive())
		{
			++ActiveAbilityCount;
		}
	}
	Data->SetNumberField(TEXT("ability_count"), Abilities.Num());
	Data->SetNumberField(TEXT("active_ability_count"), ActiveAbilityCount);
	Data->SetBoolField(TEXT("include_abilities"), bIncludeAbilities);
	if (bIncludeAbilities)
	{
		TArray<TSharedPtr<FJsonValue>> AbilityJson;
		for (const FGameplayAbilitySpec& Spec : Abilities)
		{
			if (AbilityJson.Num() >= AbilityLimit)
			{
				break;
			}
			AbilityJson.Add(MakeShared<FJsonValueObject>(BuildGameplayAbilitySpecJson(Spec)));
		}
		Data->SetNumberField(TEXT("returned_ability_count"), AbilityJson.Num());
		Data->SetBoolField(TEXT("abilities_truncated"), Abilities.Num() > AbilityJson.Num());
		Data->SetArrayField(TEXT("abilities"), AbilityJson);
	}

	const FActiveGameplayEffectsContainer& ActiveEffects = AbilitySystem->GetActiveGameplayEffects();
	const int32 ActiveEffectCount = ActiveEffects.GetNumGameplayEffects();
	Data->SetNumberField(TEXT("active_effect_count"), ActiveEffectCount);
	Data->SetBoolField(TEXT("include_effects"), bIncludeEffects);
	if (bIncludeEffects)
	{
		TArray<TSharedPtr<FJsonValue>> EffectJson;
		for (auto It = ActiveEffects.CreateConstIterator(); It && EffectJson.Num() < EffectLimit; ++It)
		{
			const FActiveGameplayEffect& ActiveEffect = *It;
			EffectJson.Add(MakeShared<FJsonValueObject>(BuildActiveGameplayEffectJson(ActiveEffect, World)));
		}
		Data->SetNumberField(TEXT("returned_effect_count"), EffectJson.Num());
		Data->SetBoolField(TEXT("effects_truncated"), ActiveEffectCount > EffectJson.Num());
		Data->SetArrayField(TEXT("active_effects"), EffectJson);
	}

	TArray<FGameplayAttribute> Attributes;
	if (bIncludeAttributes)
	{
		AbilitySystem->GetAllAttributes(Attributes);
	}
	Data->SetNumberField(TEXT("attribute_count"), Attributes.Num());
	Data->SetBoolField(TEXT("include_attributes"), bIncludeAttributes);
	if (bIncludeAttributes)
	{
		TArray<TSharedPtr<FJsonValue>> AttributeJson;
		for (const FGameplayAttribute& Attribute : Attributes)
		{
			if (AttributeJson.Num() >= AttributeLimit)
			{
				break;
			}

			TSharedPtr<FJsonObject> AttributeData = MakeShared<FJsonObject>();
			AttributeData->SetStringField(TEXT("name"), Attribute.GetName());
			if (FProperty* AttributeProperty = Attribute.GetUProperty())
			{
				AttributeData->SetStringField(TEXT("property_path"), AttributeProperty->GetPathName());
				UClass* AttributeSetClass = Attribute.GetAttributeSetClass();
				AttributeData->SetStringField(TEXT("attribute_set_class"), AttributeSetClass ? AttributeSetClass->GetPathName() : TEXT(""));
			}

			bool bFound = false;
			const float CurrentValue = AbilitySystem->GetGameplayAttributeValue(Attribute, bFound);
			AttributeData->SetBoolField(TEXT("found"), bFound);
			AttributeData->SetNumberField(TEXT("current_value"), CurrentValue);
			if (bFound)
			{
				AttributeData->SetNumberField(TEXT("base_value"), AbilitySystem->GetNumericAttributeBase(Attribute));
			}
			AttributeJson.Add(MakeShared<FJsonValueObject>(AttributeData));
		}
		Data->SetNumberField(TEXT("returned_attribute_count"), AttributeJson.Num());
		Data->SetBoolField(TEXT("attributes_truncated"), Attributes.Num() > AttributeJson.Num());
		Data->SetArrayField(TEXT("attributes"), AttributeJson);
	}

	return Data;
}

FString CommonInputTypeToString(ECommonInputType InputType)
{
	switch (InputType)
	{
	case ECommonInputType::MouseAndKeyboard:
		return TEXT("MouseAndKeyboard");
	case ECommonInputType::Gamepad:
		return TEXT("Gamepad");
	case ECommonInputType::Touch:
		return TEXT("Touch");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildRuntimeInputComponentJson(UInputComponent* InputComponent)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), InputComponent != nullptr);
	if (!InputComponent)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), InputComponent->GetName());
	Data->SetStringField(TEXT("path"), InputComponent->GetPathName());
	Data->SetStringField(TEXT("class"), InputComponent->GetClass() ? InputComponent->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("registered"), InputComponent->IsRegistered());
	Data->SetBoolField(TEXT("active"), InputComponent->IsActive());
	Data->SetNumberField(TEXT("priority"), InputComponent->Priority);
	Data->SetBoolField(TEXT("block_input"), InputComponent->bBlockInput != 0);
	Data->SetNumberField(TEXT("action_binding_count"), InputComponent->GetNumActionBindings());
	Data->SetNumberField(TEXT("key_binding_count"), InputComponent->KeyBindings.Num());
	Data->SetNumberField(TEXT("axis_binding_count"), InputComponent->AxisBindings.Num());
	Data->SetNumberField(TEXT("axis_key_binding_count"), InputComponent->AxisKeyBindings.Num());
	Data->SetNumberField(TEXT("vector_axis_binding_count"), InputComponent->VectorAxisBindings.Num());
	Data->SetNumberField(TEXT("touch_binding_count"), InputComponent->TouchBindings.Num());
	Data->SetNumberField(TEXT("gesture_binding_count"), InputComponent->GestureBindings.Num());

	if (AActor* Owner = InputComponent->GetOwner())
	{
		Data->SetStringField(TEXT("owner_name"), Owner->GetName());
		Data->SetStringField(TEXT("owner_label"), Owner->GetActorLabel());
		Data->SetStringField(TEXT("owner_path"), Owner->GetPathName());
		Data->SetStringField(TEXT("owner_class"), Owner->GetClass() ? Owner->GetClass()->GetPathName() : TEXT(""));
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
	{
		TSharedPtr<FJsonObject> EnhancedJson = MakeShared<FJsonObject>();
		EnhancedJson->SetNumberField(TEXT("action_event_binding_count"), EnhancedInputComponent->GetActionEventBindings().Num());
		EnhancedJson->SetNumberField(TEXT("action_value_binding_count"), EnhancedInputComponent->GetActionValueBindings().Num());
		EnhancedJson->SetNumberField(TEXT("debug_key_binding_count"), EnhancedInputComponent->GetDebugKeyBindings().Num());
		EnhancedJson->SetBoolField(TEXT("fire_delegates_in_editor"), EnhancedInputComponent->ShouldFireDelegatesInEditor());
		Data->SetObjectField(TEXT("enhanced_input"), EnhancedJson);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimePlayerInputJson(APlayerController* Controller)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	UPlayerInput* PlayerInput = Controller ? Controller->PlayerInput : nullptr;
	Data->SetBoolField(TEXT("present"), PlayerInput != nullptr);
	if (!PlayerInput)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), PlayerInput->GetName());
	Data->SetStringField(TEXT("path"), PlayerInput->GetPathName());
	Data->SetStringField(TEXT("class"), PlayerInput->GetClass() ? PlayerInput->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("is_enhanced_player_input"), PlayerInput->IsA<UEnhancedPlayerInput>());

	if (UEnhancedPlayerInput* EnhancedPlayerInput = Cast<UEnhancedPlayerInput>(PlayerInput))
	{
		Data->SetBoolField(TEXT("enhanced_player_input_available"), EnhancedPlayerInput != nullptr);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeEnhancedInputSubsystemJson(ULocalPlayer* LocalPlayer)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	UEnhancedInputLocalPlayerSubsystem* EnhancedSubsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	Data->SetBoolField(TEXT("subsystem_present"), EnhancedSubsystem != nullptr);
	if (!EnhancedSubsystem)
	{
		return Data;
	}

	Data->SetStringField(TEXT("input_mode_tags"), EnhancedSubsystem->GetInputMode().ToStringSimple());
	Data->SetNumberField(TEXT("player_mappable_action_key_mapping_count"), EnhancedSubsystem->GetAllPlayerMappableActionKeyMappings().Num());

	UEnhancedPlayerInput* PlayerInput = EnhancedSubsystem->GetPlayerInput();
	Data->SetBoolField(TEXT("player_input_present"), PlayerInput != nullptr);
	if (PlayerInput)
	{
		Data->SetStringField(TEXT("player_input_name"), PlayerInput->GetName());
		Data->SetStringField(TEXT("player_input_path"), PlayerInput->GetPathName());
		Data->SetStringField(TEXT("player_input_class"), PlayerInput->GetClass() ? PlayerInput->GetClass()->GetPathName() : TEXT(""));
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeCommonInputJson(ULocalPlayer* LocalPlayer)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	UCommonInputSubsystem* CommonInputSubsystem = UCommonInputSubsystem::Get(LocalPlayer);
	Data->SetBoolField(TEXT("subsystem_present"), CommonInputSubsystem != nullptr);
	if (!CommonInputSubsystem)
	{
		return Data;
	}

	Data->SetStringField(TEXT("current_input_type"), CommonInputTypeToString(CommonInputSubsystem->GetCurrentInputType()));
	Data->SetStringField(TEXT("default_input_type"), CommonInputTypeToString(CommonInputSubsystem->GetDefaultInputType()));
	Data->SetStringField(TEXT("current_gamepad_name"), CommonInputSubsystem->GetCurrentGamepadName().ToString());
	Data->SetBoolField(TEXT("using_pointer_input"), CommonInputSubsystem->IsUsingPointerInput());
	Data->SetBoolField(TEXT("should_show_input_keys"), CommonInputSubsystem->ShouldShowInputKeys());
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeControllerInputRouteJson(APlayerController* Controller)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Controller)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Controller->GetName());
	Data->SetStringField(TEXT("path"), Controller->GetPathName());
	Data->SetStringField(TEXT("class"), Controller->GetClass() ? Controller->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
	Data->SetObjectField(TEXT("input_component"), BuildRuntimeInputComponentJson(Controller->InputComponent));
	Data->SetObjectField(TEXT("player_input"), BuildRuntimePlayerInputJson(Controller));

	if (APawn* Pawn = Controller->GetPawn())
	{
		TSharedPtr<FJsonObject> PawnJson = BuildRuntimeActorJson(Pawn);
		PawnJson->SetObjectField(TEXT("input_component"), BuildRuntimeInputComponentJson(Pawn->InputComponent));
		Data->SetObjectField(TEXT("pawn"), PawnJson);
	}
	else
	{
		Data->SetBoolField(TEXT("pawn_present"), false);
	}

	return Data;
}

FString MouseCaptureModeToString(EMouseCaptureMode Mode)
{
	switch (Mode)
	{
	case EMouseCaptureMode::NoCapture:
		return TEXT("NoCapture");
	case EMouseCaptureMode::CapturePermanently:
		return TEXT("CapturePermanently");
	case EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown:
		return TEXT("CapturePermanently_IncludingInitialMouseDown");
	case EMouseCaptureMode::CaptureDuringMouseDown:
		return TEXT("CaptureDuringMouseDown");
	case EMouseCaptureMode::CaptureDuringRightMouseDown:
		return TEXT("CaptureDuringRightMouseDown");
	default:
		return TEXT("Unknown");
	}
}

FString MouseLockModeToString(EMouseLockMode Mode)
{
	switch (Mode)
	{
	case EMouseLockMode::DoNotLock:
		return TEXT("DoNotLock");
	case EMouseLockMode::LockOnCapture:
		return TEXT("LockOnCapture");
	case EMouseLockMode::LockAlways:
		return TEXT("LockAlways");
	case EMouseLockMode::LockInFullscreen:
		return TEXT("LockInFullscreen");
	default:
		return TEXT("Unknown");
	}
}

FString InputEventToString(EInputEvent InputEvent)
{
	switch (InputEvent)
	{
	case IE_Pressed:
		return TEXT("Pressed");
	case IE_Released:
		return TEXT("Released");
	case IE_Repeat:
		return TEXT("Repeat");
	case IE_DoubleClick:
		return TEXT("DoubleClick");
	case IE_Axis:
		return TEXT("Axis");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildUIInputConfigJson(const FUIInputConfig& Config)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("summary"), Config.ToString());
	Data->SetStringField(TEXT("input_mode"), LexToString(Config.GetInputMode()));
	Data->SetStringField(TEXT("mouse_capture_mode"), MouseCaptureModeToString(Config.GetMouseCaptureMode()));
	Data->SetStringField(TEXT("mouse_lock_mode"), MouseLockModeToString(Config.GetMouseLockMode()));
	Data->SetBoolField(TEXT("ignore_move_input"), Config.bIgnoreMoveInput);
	Data->SetBoolField(TEXT("ignore_look_input"), Config.bIgnoreLookInput);
	Data->SetBoolField(TEXT("hide_cursor_during_viewport_capture"), Config.HideCursorDuringViewportCapture());
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonUIActionBindingJson(const FUIActionBindingHandle& Handle)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("handle_valid"), Handle.IsValid());
	Data->SetStringField(TEXT("action_name"), Handle.GetActionName().ToString());
	Data->SetStringField(TEXT("display_name"), Handle.GetDisplayName().ToString());
	Data->SetBoolField(TEXT("display_in_action_bar"), Handle.GetDisplayInActionBar());
	Data->SetObjectField(TEXT("bound_widget"), BuildRuntimeObjectReferenceJson(Handle.GetBoundWidget()));

	if (TSharedPtr<FUIActionBinding> Binding = FUIActionBinding::FindBinding(Handle))
	{
		Data->SetBoolField(TEXT("registration_present"), true);
		Data->SetStringField(TEXT("debug_string"), Binding->ToDebugString());
		Data->SetStringField(TEXT("input_mode"), LexToString(Binding->InputMode));
		Data->SetStringField(TEXT("input_event"), InputEventToString(Binding->InputEvent));
		Data->SetBoolField(TEXT("consumes_input"), Binding->bConsumesInput);
		Data->SetBoolField(TEXT("persistent"), Binding->bIsPersistent);
		Data->SetBoolField(TEXT("display_in_action_bar_registration"), Binding->bDisplayInActionBar);
		Data->SetNumberField(TEXT("priority_within_collection"), Binding->PriorityWithinCollection);
		Data->SetNumberField(TEXT("user_index"), Binding->UserIndex);
		Data->SetNumberField(TEXT("normal_mapping_count"), Binding->NormalMappings.Num());
		Data->SetNumberField(TEXT("hold_mapping_count"), Binding->HoldMappings.Num());
		Data->SetObjectField(TEXT("input_action"), BuildRuntimeObjectReferenceJson(Binding->InputAction.Get()));
		if (Binding->LegacyActionTableRow.DataTable)
		{
			Data->SetObjectField(TEXT("legacy_action_table"), BuildRuntimeObjectReferenceJson(Binding->LegacyActionTableRow.DataTable));
			Data->SetStringField(TEXT("legacy_action_row"), Binding->LegacyActionTableRow.RowName.ToString());
		}
	}
	else
	{
		Data->SetBoolField(TEXT("registration_present"), false);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonUIActionRouterJson(UCommonUIActionRouterBase* Router, bool bIncludeBindings, int32 BindingLimit)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(Router);
	Data->SetBoolField(TEXT("present"), Router != nullptr);
	if (!Router)
	{
		return Data;
	}

	Data->SetNumberField(TEXT("local_player_index"), Router->GetLocalPlayerIndex());
	Data->SetStringField(TEXT("active_input_mode"), LexToString(Router->GetActiveInputMode(ECommonInputMode::All)));
	Data->SetStringField(TEXT("active_mouse_capture_mode"), MouseCaptureModeToString(Router->GetActiveMouseCaptureMode(EMouseCaptureMode::NoCapture)));
	Data->SetBoolField(TEXT("can_process_normal_game_input"), Router->CanProcessNormalGameInput());
	Data->SetBoolField(TEXT("pending_tree_change"), Router->IsPendingTreeChange());
	Data->SetBoolField(TEXT("always_show_cursor"), Router->ShouldAlwaysShowCursor());
	Data->SetBoolField(TEXT("common_analog_cursor_present"), Router->GetCommonAnalogCursor().IsValid());

	const TArray<FUIActionBindingHandle> ActiveBindings = Router->GatherActiveBindings();
	Data->SetNumberField(TEXT("active_binding_count"), ActiveBindings.Num());
	Data->SetBoolField(TEXT("include_bindings"), bIncludeBindings);
	if (bIncludeBindings)
	{
		TArray<TSharedPtr<FJsonValue>> BindingsJson;
		for (const FUIActionBindingHandle& Binding : ActiveBindings)
		{
			if (BindingsJson.Num() >= BindingLimit)
			{
				break;
			}
			BindingsJson.Add(MakeShared<FJsonValueObject>(BuildCommonUIActionBindingJson(Binding)));
		}
		Data->SetNumberField(TEXT("returned_binding_count"), BindingsJson.Num());
		Data->SetBoolField(TEXT("bindings_truncated"), ActiveBindings.Num() > BindingsJson.Num());
		Data->SetArrayField(TEXT("active_bindings"), BindingsJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonActivatableWidgetJson(UCommonActivatableWidget* Widget)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(Widget);
	if (!Widget)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("activated"), Widget->IsActivated());
	Data->SetBoolField(TEXT("modal"), Widget->IsModal());
	Data->SetBoolField(TEXT("supports_activation_focus"), Widget->SupportsActivationFocus());
	Data->SetBoolField(TEXT("auto_restores_focus"), Widget->AutoRestoresFocus());
	Data->SetBoolField(TEXT("sets_visibility_on_activated"), Widget->SetsVisibilityOnActivated());
	Data->SetBoolField(TEXT("sets_visibility_on_deactivated"), Widget->SetsVisibilityOnDeactivated());
	Data->SetStringField(TEXT("visibility"), StaticEnum<ESlateVisibility>()->GetNameStringByValue(static_cast<int64>(Widget->GetVisibility())));
	Data->SetBoolField(TEXT("visible"), Widget->IsVisible());
	Data->SetBoolField(TEXT("input_tree_node_present"), Widget->GetInputTreeNode().IsValid());
	Data->SetObjectField(TEXT("desired_focus_target"), BuildRuntimeObjectReferenceJson(Widget->GetDesiredFocusTarget()));

	if (ULocalPlayer* LocalPlayer = Widget->GetOwningLocalPlayer())
	{
		Data->SetBoolField(TEXT("owning_local_player_present"), true);
		Data->SetNumberField(TEXT("owning_local_player_controller_id"), LocalPlayer->GetControllerId());
		Data->SetStringField(TEXT("owning_local_player_name"), LocalPlayer->GetName());
		if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
		{
			Data->SetBoolField(TEXT("in_active_root"), Router->IsWidgetInActiveRoot(Widget));
		}
	}
	else
	{
		Data->SetBoolField(TEXT("owning_local_player_present"), false);
	}

	const TOptional<FUIInputConfig> DesiredInputConfig = Widget->GetDesiredInputConfig();
	Data->SetBoolField(TEXT("desired_input_config_present"), DesiredInputConfig.IsSet());
	if (DesiredInputConfig.IsSet())
	{
		Data->SetObjectField(TEXT("desired_input_config"), BuildUIInputConfigJson(DesiredInputConfig.GetValue()));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildCommonActivatableContainerJson(UCommonActivatableWidgetContainerBase* Container, int32 WidgetLimit)
{
	TSharedPtr<FJsonObject> Data = BuildRuntimeObjectReferenceJson(Container);
	if (!Container)
	{
		return Data;
	}

	UCommonActivatableWidget* ActiveWidget = Container->GetActiveWidget();
	Data->SetObjectField(TEXT("active_widget"), BuildRuntimeObjectReferenceJson(ActiveWidget));
	Data->SetNumberField(TEXT("widget_count"), Container->GetNumWidgets());
	Data->SetNumberField(TEXT("transition_duration"), Container->GetTransitionDuration());

	TArray<TSharedPtr<FJsonValue>> WidgetsJson;
	const TArray<UCommonActivatableWidget*>& Widgets = Container->GetWidgetList();
	for (UCommonActivatableWidget* Widget : Widgets)
	{
		if (!Widget)
		{
			continue;
		}
		if (WidgetsJson.Num() >= WidgetLimit)
		{
			break;
		}

		TSharedPtr<FJsonObject> WidgetJson = BuildRuntimeObjectReferenceJson(Widget);
		WidgetJson->SetBoolField(TEXT("active_widget"), Widget == ActiveWidget);
		WidgetJson->SetBoolField(TEXT("activated"), Widget->IsActivated());
		WidgetsJson.Add(MakeShared<FJsonValueObject>(WidgetJson));
	}
	Data->SetNumberField(TEXT("returned_widget_count"), WidgetsJson.Num());
	Data->SetBoolField(TEXT("widgets_truncated"), Widgets.Num() > WidgetsJson.Num());
	Data->SetArrayField(TEXT("widgets"), WidgetsJson);
	return Data;
}
}

// Static instance
TUniquePtr<FAIExportTCPServer> FAIExportTCPServerManager::Instance;

//////////////////////////////////////////////////////////////////////////
// FAIExportTCPServerManager

FAIExportTCPServer* FAIExportTCPServerManager::Get()
{
	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FAIExportTCPServer>();
	}
	return Instance.Get();
}

void FAIExportTCPServerManager::Start()
{
	FAIExportTCPServer* Server = Get();
	if (Server && !Server->IsRunning())
	{
		Server->StartServer();
	}
}

void FAIExportTCPServerManager::Stop()
{
	if (Instance.IsValid())
	{
		Instance->StopServer();
		Instance.Reset();
	}
}

//////////////////////////////////////////////////////////////////////////
// FAIExportTCPServer

FAIExportTCPServer::FAIExportTCPServer()
	: bIsRunning(false)
	, bStopRequested(false)
	, ActiveClientConnections(0)
{
}

FAIExportTCPServer::~FAIExportTCPServer()
{
	StopServer();
}

int32 FAIExportTCPServer::FindAvailablePort(int32 StartPort, int32 EndPort)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to get socket subsystem for port discovery"));
		return StartPort;
	}

	for (int32 Port = StartPort; Port <= EndPort; ++Port)
	{
		// Try to bind to this port
		FSocket* TestSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("PortTest"), false);
		if (!TestSocket)
		{
			continue;
		}

		// DO NOT use SetReuseAddr(true) here - we want the bind to FAIL if port is in use
		// This ensures proper port discovery when multiple UE projects are running

		FIPv4Address LocalAddress;
		FIPv4Address::Parse(TEXT("127.0.0.1"), LocalAddress);
		TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		Addr->SetIp(LocalAddress.Value);
		Addr->SetPort(Port);

		bool bBound = TestSocket->Bind(*Addr);
		SocketSubsystem->DestroySocket(TestSocket);

		if (bBound)
		{
			UE_LOG(LogAIExport, Log, TEXT("Found available port: %d"), Port);
			return Port;
		}
		else
		{
			UE_LOG(LogAIExport, Verbose, TEXT("Port %d is in use, trying next..."), Port);
		}
	}

	UE_LOG(LogAIExport, Warning, TEXT("No available port found in range %d-%d, using %d"), StartPort, EndPort, StartPort);
	return StartPort;
}

void FAIExportTCPServer::WritePortFile(int32 Port)
{
	FString PortFilePath = GetPortFilePath();
	FString PortString = FString::FromInt(Port);

	if (FFileHelper::SaveStringToFile(PortString, *PortFilePath))
	{
		UE_LOG(LogAIExport, Log, TEXT("Written port %d to: %s"), Port, *PortFilePath);
	}
	else
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to write port file: %s"), *PortFilePath);
	}
}

FString FAIExportTCPServer::GetPortFilePath()
{
	return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("AIExport_port.txt"));
}

FString FAIExportTCPServer::GetEditorRegistryDir()
{
	return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("CommonAIExport"), TEXT("Editors"));
}

FString FAIExportTCPServer::GetEditorRegistryFilePath(int32 Port)
{
	return FPaths::Combine(
		GetEditorRegistryDir(),
		FString::Printf(TEXT("%u-%d.json"), FPlatformProcess::GetCurrentProcessId(), Port));
}

const TArray<FAIExportTCPServer::FCommandDescriptor>& FAIExportTCPServer::GetCommandDescriptors()
{
#define AI_COMMAND_PARAMS(CommandName, CommandCategory, bCommandMutating, CommandTimeout, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), true, bCommandMutating, CommandTimeout, bCommandMutating ? TEXT("write") : TEXT("read"), bCommandMutating, CommandTimeout >= 120, &FAIExportTCPServer::HandlerName, nullptr }
#define AI_COMMAND_PARAMS_SCOPE(CommandName, CommandCategory, bCommandMutating, CommandTimeout, CommandScope, bCommandDryRun, bCommandAsyncCandidate, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), true, bCommandMutating, CommandTimeout, TEXT(CommandScope), bCommandDryRun, bCommandAsyncCandidate, &FAIExportTCPServer::HandlerName, nullptr }
#define AI_COMMAND_OPTIONAL_PARAMS(CommandName, CommandCategory, bCommandMutating, CommandTimeout, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), false, bCommandMutating, CommandTimeout, bCommandMutating ? TEXT("write") : TEXT("read"), bCommandMutating, CommandTimeout >= 120, &FAIExportTCPServer::HandlerName, nullptr }
#define AI_COMMAND_NO_PARAMS(CommandName, CommandCategory, CommandTimeout, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), false, false, CommandTimeout, TEXT("read"), false, false, nullptr, &FAIExportTCPServer::HandlerName }
#define AI_COMMAND_NO_PARAMS_SCOPE(CommandName, CommandCategory, bCommandMutating, CommandTimeout, CommandScope, bCommandDryRun, bCommandAsyncCandidate, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), false, bCommandMutating, CommandTimeout, TEXT(CommandScope), bCommandDryRun, bCommandAsyncCandidate, nullptr, &FAIExportTCPServer::HandlerName }

	static const TArray<FCommandDescriptor> Commands = {
		AI_COMMAND_NO_PARAMS("ping", "Utility", 0, HandlePing),
		AI_COMMAND_NO_PARAMS("list_commands", "Utility", 0, HandleListCommands),
		AI_COMMAND_NO_PARAMS("server_status", "Utility", 0, HandleServerStatus),
		AI_COMMAND_NO_PARAMS("editor_identity", "Utility", 0, HandleEditorIdentity),
		AI_COMMAND_OPTIONAL_PARAMS("command_manifest_export", "Utility", false, 30, HandleCommandManifestExport),
		AI_COMMAND_NO_PARAMS("project_status", "Workflow", 0, HandleProjectStatus),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_status", "Workflow", false, 30, HandleSourceControlStatus),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_log", "Workflow", false, 30, HandleSourceControlLog),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_show", "Workflow", false, 30, HandleSourceControlShow),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_diff", "Workflow", false, 30, HandleSourceControlDiff),
		AI_COMMAND_PARAMS("task_submit", "AsyncJob", false, 0, HandleTaskSubmit),
		AI_COMMAND_OPTIONAL_PARAMS("task_status", "AsyncJob", false, 0, HandleTaskStatus),
		AI_COMMAND_OPTIONAL_PARAMS("task_result", "AsyncJob", false, 0, HandleTaskResult),
		AI_COMMAND_OPTIONAL_PARAMS("task_cancel", "AsyncJob", false, 0, HandleTaskCancel),
		AI_COMMAND_OPTIONAL_PARAMS("task_events", "AsyncJob", false, 0, HandleTaskEvents),
		AI_COMMAND_OPTIONAL_PARAMS("task_events_wait", "AsyncJob", false, 30, HandleTaskEventsWait),
		AI_COMMAND_PARAMS("export_widget", "Export", false, 60, HandleExportWidget),
		AI_COMMAND_PARAMS("export_blueprint", "Export", false, 60, HandleExportBlueprint),
		AI_COMMAND_NO_PARAMS("list_supported_types", "Export", 0, HandleListSupportedTypes),

		AI_COMMAND_NO_PARAMS("editor_world_info", "Editor", 0, HandleEditorWorldInfo),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_world_info", "RuntimeInspector", false, 30, HandleRuntimeWorldInfo),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_player_list", "RuntimeInspector", false, 30, HandleRuntimePlayerList),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_component_list", "RuntimeInspector", false, 60, HandleRuntimeComponentList),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_input_routing", "RuntimeInspector", false, 60, HandleRuntimeInputRouting),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_replication_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeReplicationDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_ability_system_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAbilitySystemDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_ai_perception_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAIPerceptionDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_commonui_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeCommonUIDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_asset_streaming_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAssetStreamingDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_async_load_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAsyncLoadDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_game_instance_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeGameInstanceDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_level_travel_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeLevelTravelDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_multiplayer_connection_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeMultiplayerConnectionDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_tick_timer_latent_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeTickTimerLatentDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_scheduler_performance_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeSchedulerPerformanceDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_physics_collision_diagnostics", "RuntimeInspector", false, 60, HandleRuntimePhysicsCollisionDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("actor_list", "EditorActor", false, 60, HandleActorList),
		AI_COMMAND_PARAMS("actor_spawn", "EditorActor", true, 60, HandleActorSpawn),
		AI_COMMAND_PARAMS("actor_set_transform", "EditorActor", true, 60, HandleActorSetTransform),
		AI_COMMAND_PARAMS_SCOPE("actor_delete", "EditorActor", true, 60, "destructive", true, false, HandleActorDelete),
		AI_COMMAND_PARAMS("level_open", "EditorLevel", true, 60, HandleLevelOpen),
		AI_COMMAND_NO_PARAMS_SCOPE("level_save_current", "EditorLevel", true, 60, "write", true, false, HandleLevelSaveCurrent),
		AI_COMMAND_NO_PARAMS("pie_status", "PIE", 0, HandlePIEStatus),
		AI_COMMAND_NO_PARAMS_SCOPE("pie_start", "PIE", true, 30, "write", true, false, HandlePIEStart),
		AI_COMMAND_NO_PARAMS_SCOPE("pie_stop", "PIE", true, 30, "write", true, false, HandlePIEStop),
		AI_COMMAND_PARAMS_SCOPE("editor_console_command", "Editor", true, 60, "destructive", true, false, HandleEditorConsoleCommand),
		AI_COMMAND_OPTIONAL_PARAMS("editor_log_read", "Workflow", false, 30, HandleEditorLogRead),
		AI_COMMAND_OPTIONAL_PARAMS("viewport_capture", "EditorViewport", true, 30, HandleViewportCapture),

		AI_COMMAND_PARAMS("create_widget_blueprint", "Widget", true, 60, HandleCreateWidgetBlueprint),
		AI_COMMAND_PARAMS("add_widget", "Widget", true, 60, HandleAddWidget),
		AI_COMMAND_PARAMS("remove_widget", "Widget", true, 60, HandleRemoveWidget),
		AI_COMMAND_PARAMS("move_widget", "Widget", true, 60, HandleMoveWidget),
		AI_COMMAND_PARAMS("set_widget_property", "Widget", true, 60, HandleSetWidgetProperty),
		AI_COMMAND_PARAMS("set_slot_property", "Widget", true, 60, HandleSetSlotProperty),
		AI_COMMAND_PARAMS("set_canvas_slot_layout", "Widget", true, 60, HandleSetCanvasSlotLayout),
		AI_COMMAND_PARAMS("set_widget_properties", "Widget", true, 60, HandleSetWidgetProperties),
		AI_COMMAND_PARAMS("compile_and_save", "Widget", true, 60, HandleCompileAndSave),
		AI_COMMAND_PARAMS("get_widget_tree", "Widget", false, 60, HandleGetWidgetTree),
		AI_COMMAND_NO_PARAMS("list_widget_classes", "Widget", 60, HandleListWidgetClasses),

		AI_COMMAND_PARAMS("set_cdo_property", "CDO", true, 120, HandleSetCDOProperty),
		AI_COMMAND_PARAMS("get_cdo_properties", "CDO", false, 60, HandleGetCDOProperties),
		AI_COMMAND_PARAMS("add_cdo_array_element", "CDOArray", true, 60, HandleAddCDOArrayElement),
		AI_COMMAND_PARAMS("set_cdo_array_element_property", "CDOArray", true, 60, HandleSetCDOArrayElementProperty),
		AI_COMMAND_PARAMS("remove_cdo_array_element", "CDOArray", true, 60, HandleRemoveCDOArrayElement),
		AI_COMMAND_PARAMS("get_cdo_array_length", "CDOArray", false, 60, HandleGetCDOArrayLength),

		AI_COMMAND_PARAMS("add_event_node", "BlueprintGraph", true, 60, HandleAddEventNode),
		AI_COMMAND_PARAMS("add_custom_event", "BlueprintGraph", true, 60, HandleAddCustomEvent),
		AI_COMMAND_PARAMS("add_function_call", "BlueprintGraph", true, 60, HandleAddFunctionCallNode),
		AI_COMMAND_PARAMS("add_variable_get_node", "BlueprintGraph", true, 60, HandleAddVariableGetNode),
		AI_COMMAND_PARAMS("add_variable_set_node", "BlueprintGraph", true, 60, HandleAddVariableSetNode),
		AI_COMMAND_PARAMS("add_make_struct_node", "BlueprintGraph", true, 60, HandleAddMakeStructNode),
		AI_COMMAND_PARAMS("add_branch_node", "BlueprintGraph", true, 60, HandleAddBranchNode),
		AI_COMMAND_PARAMS("ensure_function_graph", "BlueprintGraph", true, 60, HandleEnsureFunctionGraph),
		AI_COMMAND_PARAMS("add_call_parent_function", "BlueprintGraph", true, 60, HandleAddCallParentFunction),
		AI_COMMAND_PARAMS("connect_pins", "BlueprintGraph", true, 60, HandleConnectPins),
		AI_COMMAND_PARAMS("set_pin_default", "BlueprintGraph", true, 60, HandleSetPinDefault),
		AI_COMMAND_PARAMS("remove_graph_node", "BlueprintGraph", true, 60, HandleRemoveGraphNode),
		AI_COMMAND_PARAMS("get_graph", "BlueprintGraph", false, 60, HandleGetGraph),
		AI_COMMAND_PARAMS("list_graphs", "BlueprintGraph", false, 60, HandleListGraphs),

		AI_COMMAND_PARAMS("add_variable", "BlueprintVariable", true, 60, HandleAddVariable),
		AI_COMMAND_PARAMS("set_variable_default", "BlueprintVariable", true, 60, HandleSetVariableDefault),
		AI_COMMAND_PARAMS("remove_variable", "BlueprintVariable", true, 60, HandleRemoveVariable),
		AI_COMMAND_PARAMS("get_variables", "BlueprintVariable", false, 60, HandleGetVariables),
		AI_COMMAND_PARAMS("reparent_blueprint", "BlueprintUtility", true, 60, HandleReparentBlueprint),

		AI_COMMAND_PARAMS("create_material", "Material", true, 60, HandleCreateMaterial),
		AI_COMMAND_PARAMS("set_material_property", "Material", true, 60, HandleSetMaterialProperty),
		AI_COMMAND_PARAMS("add_expression", "Material", true, 60, HandleAddExpression),
		AI_COMMAND_PARAMS("set_expression_property", "Material", true, 60, HandleSetExpressionProperty),
		AI_COMMAND_PARAMS("connect_expressions", "Material", true, 60, HandleConnectExpressions),
		AI_COMMAND_PARAMS("connect_to_material_property", "Material", true, 60, HandleConnectToMaterialProperty),
		AI_COMMAND_PARAMS("disconnect_input", "Material", true, 60, HandleDisconnectInput),
		AI_COMMAND_PARAMS("remove_expression", "Material", true, 60, HandleRemoveExpression),
		AI_COMMAND_PARAMS("compile_material", "Material", true, 120, HandleCompileMaterial),
		AI_COMMAND_PARAMS("get_material_graph", "Material", false, 60, HandleGetMaterialGraph),
		AI_COMMAND_NO_PARAMS("list_expression_classes", "Material", 60, HandleListExpressionClasses),
		AI_COMMAND_PARAMS("create_material_instance", "Material", true, 60, HandleCreateMaterialInstance),
		AI_COMMAND_PARAMS("set_instance_parameter", "Material", true, 60, HandleSetInstanceParameter),
		AI_COMMAND_PARAMS("save_material_instance", "Material", true, 60, HandleSaveMaterialInstance),
		AI_COMMAND_PARAMS("get_material_instance_info", "Material", false, 60, HandleGetMaterialInstanceInfo),

		AI_COMMAND_PARAMS("save_data_asset", "DataAsset", true, 60, HandleSaveDataAsset),

		AI_COMMAND_PARAMS("create_asset", "Asset", true, 60, HandleCreateAsset),
		AI_COMMAND_PARAMS("set_asset_property", "Asset", true, 60, HandleSetAssetProperty),
		AI_COMMAND_PARAMS("get_asset_properties", "Asset", false, 60, HandleGetAssetProperties),
		AI_COMMAND_PARAMS("asset_exists", "Asset", false, 30, HandleAssetExists),
		AI_COMMAND_PARAMS("scan_asset_paths", "Asset", false, 60, HandleScanAssetPaths),
		AI_COMMAND_OPTIONAL_PARAMS("asset_search", "Asset", false, 60, HandleAssetSearch),
		AI_COMMAND_PARAMS("asset_validate_light", "Asset", false, 60, HandleAssetValidateLight),
		AI_COMMAND_PARAMS("save_asset", "Asset", true, 60, HandleSaveAsset),
		AI_COMMAND_PARAMS("rename_asset", "Asset", true, 120, HandleRenameAsset),
		AI_COMMAND_PARAMS("get_referencers", "Asset", false, 60, HandleGetReferencers),
		AI_COMMAND_PARAMS("get_dependencies", "Asset", false, 60, HandleGetDependencies),
		AI_COMMAND_PARAMS_SCOPE("delete_asset", "Asset", true, 120, "destructive", true, true, HandleDeleteAsset),
		AI_COMMAND_PARAMS("list_redirectors", "Asset", false, 60, HandleListRedirectors),
		AI_COMMAND_PARAMS("fixup_redirectors", "Asset", true, 120, HandleFixupRedirectors),

		AI_COMMAND_PARAMS("add_input_mapping", "Input", true, 60, HandleAddInputMapping),
		AI_COMMAND_PARAMS("remove_input_mapping", "Input", true, 60, HandleRemoveInputMapping),
		AI_COMMAND_PARAMS("get_input_mappings", "Input", false, 60, HandleGetInputMappings),

		AI_COMMAND_PARAMS("create_anim_blueprint", "AnimBlueprint", true, 60, HandleCreateAnimBlueprint),
		AI_COMMAND_PARAMS("get_anim_blueprint_info", "AnimBlueprint", false, 60, HandleGetAnimBlueprintInfo),

		AI_COMMAND_PARAMS("import_texture", "Import", true, 60, HandleImportTexture),
		AI_COMMAND_PARAMS("import_font", "Import", true, 60, HandleImportFont),

		AI_COMMAND_PARAMS("capture_widget_preview", "WidgetPreview", false, 120, HandleCaptureWidgetPreview),
		AI_COMMAND_PARAMS("reload_asset", "AssetLifecycle", false, 30, HandleReloadAsset),
	};

#undef AI_COMMAND_PARAMS
#undef AI_COMMAND_PARAMS_SCOPE
#undef AI_COMMAND_OPTIONAL_PARAMS
#undef AI_COMMAND_NO_PARAMS
#undef AI_COMMAND_NO_PARAMS_SCOPE

	return Commands;
}

const FAIExportTCPServer::FCommandDescriptor* FAIExportTCPServer::FindCommandDescriptor(const FString& CommandType)
{
	for (const FCommandDescriptor& Descriptor : GetCommandDescriptors())
	{
		if (CommandType == Descriptor.Name)
		{
			return &Descriptor;
		}
	}

	return nullptr;
}

FAIExportTCPServer::FAICommandContext FAIExportTCPServer::BuildCommandContext(TSharedPtr<FJsonObject> RootObject, const FCommandDescriptor& Descriptor) const
{
	FAICommandContext Context;
	Context.RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Context.TimeoutSeconds = Descriptor.TimeoutSeconds;

	const TSharedPtr<FJsonObject>* MetaObject = nullptr;
	if (RootObject.IsValid() && RootObject->TryGetObjectField(TEXT("meta"), MetaObject) && MetaObject && MetaObject->IsValid())
	{
		(*MetaObject)->TryGetStringField(TEXT("request_id"), Context.RequestId);
		(*MetaObject)->TryGetStringField(TEXT("client_id"), Context.ClientId);
		(*MetaObject)->TryGetStringField(TEXT("session_id"), Context.SessionId);
		(*MetaObject)->TryGetStringField(TEXT("scope"), Context.Scope);
		(*MetaObject)->TryGetBoolField(TEXT("dry_run"), Context.bDryRun);
		(*MetaObject)->TryGetBoolField(TEXT("cancel_requested"), Context.bCancellationRequested);

		double RequestedTimeout = 0.0;
		if ((*MetaObject)->TryGetNumberField(TEXT("timeout_seconds"), RequestedTimeout) && RequestedTimeout > 0.0)
		{
			Context.TimeoutSeconds = static_cast<int32>(RequestedTimeout);
		}
	}

	Context.Scope = Context.Scope.ToLower();
	return Context;
}

bool FAIExportTCPServer::ValidateCommandScope(const FCommandDescriptor& Descriptor, const FAICommandContext& Context, FString& OutError) const
{
	const FString RequiredScope = FString(Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read")).ToLower();
	const FString EffectiveScope = Context.Scope.IsEmpty() ? TEXT("write") : Context.Scope.ToLower();

	auto ScopeRank = [](const FString& Scope) -> int32
	{
		if (Scope == TEXT("read")) return 0;
		if (Scope == TEXT("write")) return 1;
		if (Scope == TEXT("destructive")) return 2;
		return -1;
	};

	const int32 RequiredRank = ScopeRank(RequiredScope);
	const int32 EffectiveRank = ScopeRank(EffectiveScope);
	if (EffectiveRank < 0)
	{
		OutError = FString::Printf(TEXT("Invalid command scope '%s'. Expected one of: read, write, destructive"), *EffectiveScope);
		return false;
	}

	if (RequiredRank < 0)
	{
		OutError = FString::Printf(TEXT("Command '%s' has invalid required scope '%s'"), Descriptor.Name, *RequiredScope);
		return false;
	}

	if (EffectiveRank < RequiredRank)
	{
		OutError = FString::Printf(
			TEXT("Command '%s' requires '%s' scope; request provided '%s' scope. Pass top-level meta.scope='%s' only after explicit user approval."),
			Descriptor.Name,
			*RequiredScope,
			*EffectiveScope,
			*RequiredScope);
		return false;
	}

	return true;
}

FString FAIExportTCPServer::DispatchCommand(const FCommandDescriptor& Descriptor, TSharedPtr<FJsonObject> Params)
{
	if (Descriptor.ParamsHandler)
	{
		return (this->*Descriptor.ParamsHandler)(Params);
	}

	if (Descriptor.NoParamsHandler)
	{
		return (this->*Descriptor.NoParamsHandler)();
	}

	return CreateErrorResponse(FString::Printf(TEXT("Command '%s' has no registered handler"), Descriptor.Name));
}

FString FAIExportTCPServer::CreateDryRunResponse(const FCommandDescriptor& Descriptor, const FAICommandContext& Context)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("dry_run"), true);
	Data->SetBoolField(TEXT("would_execute"), true);
	Data->SetStringField(TEXT("command"), Descriptor.Name);
	Data->SetStringField(TEXT("category"), Descriptor.Category);
	Data->SetStringField(TEXT("request_id"), Context.RequestId);
	Data->SetStringField(TEXT("required_scope"), Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read"));
	Data->SetStringField(TEXT("effective_scope"), Context.Scope.IsEmpty() ? TEXT("write") : Context.Scope);
	Data->SetStringField(TEXT("message"), TEXT("Dry-run accepted. No Unreal Editor state was changed."));
	return CreateSuccessResponse(Data);
}

void FAIExportTCPServer::WriteCommandDescriptorJson(const FCommandDescriptor& Descriptor, TSharedPtr<FJsonObject> OutObject) const
{
	if (!OutObject.IsValid())
	{
		return;
	}

	OutObject->SetStringField(TEXT("name"), Descriptor.Name);
	OutObject->SetStringField(TEXT("category"), Descriptor.Category);
	OutObject->SetBoolField(TEXT("requires_params"), Descriptor.bRequiresParams);
	OutObject->SetBoolField(TEXT("mutating"), Descriptor.bMutating);
	OutObject->SetNumberField(TEXT("timeout_seconds"), Descriptor.TimeoutSeconds);
	OutObject->SetStringField(TEXT("required_scope"), Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read"));
	OutObject->SetBoolField(TEXT("supports_dry_run"), Descriptor.bSupportsDryRun);
	OutObject->SetBoolField(TEXT("async_candidate"), Descriptor.bAsyncCandidate);
}

TSharedPtr<FJsonObject> FAIExportTCPServer::BuildEditorIdentityJson() const
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString ProjectFile = FPaths::GetProjectFilePath();
	if (!ProjectFile.IsEmpty())
	{
		ProjectFile = FPaths::ConvertRelativePathToFull(ProjectFile);
	}

	FString PluginVersion = TEXT("unknown");
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CommonAIExport"));
	if (Plugin.IsValid())
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}

	TArray<TSharedPtr<FJsonValue>> Scopes;
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("read")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("write")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("destructive")));

	TArray<TSharedPtr<FJsonValue>> AllowedOrigins;
	for (const FString& Origin : HttpAllowedOrigins)
	{
		AllowedOrigins.Add(MakeShared<FJsonValueString>(Origin));
	}

	int32 ActiveSessionCount = 0;
	{
		FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
		ActiveSessionCount = ActiveMcpSessions.Num();
	}

	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetNumberField(TEXT("command_count"), GetCommandDescriptors().Num());
	Capabilities->SetBoolField(TEXT("supports_scope_gate"), true);
	Capabilities->SetBoolField(TEXT("supports_dry_run"), true);
	Capabilities->SetBoolField(TEXT("supports_async_jobs"), true);
	Capabilities->SetBoolField(TEXT("supports_cross_project_routing"), false);
	Capabilities->SetBoolField(TEXT("supports_native_http_mcp"), bHttpServerRunning);
	Capabilities->SetBoolField(TEXT("supports_http_mcp_sessions"), bHttpServerRunning);
	Capabilities->SetBoolField(TEXT("supports_mcp_pagination"), bHttpServerRunning);
	Capabilities->SetBoolField(TEXT("supports_http_audit"), bHttpServerRunning);
	Capabilities->SetBoolField(TEXT("http_auth_required"), !HttpAuthToken.IsEmpty());
	Capabilities->SetArrayField(TEXT("supported_scopes"), Scopes);

	TSharedPtr<FJsonObject> Transports = MakeShared<FJsonObject>();
	Transports->SetStringField(TEXT("editor_backend"), TEXT("tcp_json_bridge"));
	Transports->SetStringField(TEXT("mcp_wrapper"), TEXT("python_stdio_fastmcp"));
	Transports->SetStringField(TEXT("native_http_mcp"), bHttpServerRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), HttpPort) : TEXT(""));
	Transports->SetBoolField(TEXT("native_http_auth_required"), !HttpAuthToken.IsEmpty());
	Transports->SetStringField(TEXT("native_http_auth_env"), TEXT("COMMONAI_MCP_HTTP_TOKEN"));

	Data->SetNumberField(TEXT("schema_version"), 1);
	Data->SetStringField(TEXT("editor_id"), EditorInstanceId.IsEmpty() ? FString::Printf(TEXT("%s-%u-%d"), FApp::GetProjectName(), FPlatformProcess::GetCurrentProcessId(), ServerPort) : EditorInstanceId);
	Data->SetStringField(TEXT("server"), TEXT("CommonAIExport"));
	Data->SetStringField(TEXT("host"), TEXT("127.0.0.1"));
	Data->SetNumberField(TEXT("port"), ServerPort);
	Data->SetNumberField(TEXT("http_port"), HttpPort);
	Data->SetStringField(TEXT("http_health_url"), bHttpServerRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/commonai/health"), HttpPort) : TEXT(""));
	Data->SetStringField(TEXT("mcp_http_url"), bHttpServerRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), HttpPort) : TEXT(""));
	Data->SetStringField(TEXT("mcp_protocol_version"), TEXT("2025-06-18"));
	Data->SetNumberField(TEXT("mcp_session_ttl_seconds"), McpSessionTtlSeconds);
	Data->SetNumberField(TEXT("active_mcp_sessions"), ActiveSessionCount);
	Data->SetArrayField(TEXT("http_allowed_origins"), AllowedOrigins);
	Data->SetBoolField(TEXT("http_audit_enabled"), bHttpAuditEnabled);
	Data->SetStringField(TEXT("http_audit_log_path"), HttpAuditLogPath);
	Data->SetNumberField(TEXT("http_request_count"), HttpRequestCount.GetValue());
	Data->SetNumberField(TEXT("http_rejected_request_count"), HttpRejectedRequestCount.GetValue());
	Data->SetNumberField(TEXT("mcp_request_count"), McpRequestCount.GetValue());
	Data->SetNumberField(TEXT("mcp_rejected_request_count"), McpRejectedRequestCount.GetValue());
	Data->SetNumberField(TEXT("mcp_session_expired_count"), McpSessionExpiredCount.GetValue());
	Data->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
	Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Data->SetStringField(TEXT("project_dir"), ProjectDir);
	Data->SetStringField(TEXT("project_file"), ProjectFile);
	Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Data->SetStringField(TEXT("plugin_name"), TEXT("CommonAIExport"));
	Data->SetStringField(TEXT("plugin_version"), PluginVersion);
	Data->SetStringField(TEXT("started_at_utc"), ServerStartedAtUtc);
	Data->SetStringField(TEXT("last_seen_utc"), FDateTime::UtcNow().ToIso8601());
	Data->SetStringField(TEXT("port_file"), FPaths::ConvertRelativePathToFull(GetPortFilePath()));
	Data->SetStringField(TEXT("registry_file"), EditorRegistryFilePath);
	Data->SetObjectField(TEXT("capabilities"), Capabilities);
	Data->SetObjectField(TEXT("transports"), Transports);
	return Data;
}

TSharedPtr<FJsonObject> FAIExportTCPServer::BuildCommandManifestJson() const
{
	TArray<TSharedPtr<FJsonValue>> Commands;
	for (const FCommandDescriptor& Descriptor : GetCommandDescriptors())
	{
		TSharedPtr<FJsonObject> Command = MakeShared<FJsonObject>();
		WriteCommandDescriptorJson(Descriptor, Command);
		Commands.Add(MakeShared<FJsonValueObject>(Command));
	}

	TArray<TSharedPtr<FJsonValue>> Scopes;
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("read")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("write")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("destructive")));

	TArray<TSharedPtr<FJsonValue>> AllowedOrigins;
	for (const FString& Origin : HttpAllowedOrigins)
	{
		AllowedOrigins.Add(MakeShared<FJsonValueString>(Origin));
	}

	TSharedPtr<FJsonObject> Transports = MakeShared<FJsonObject>();
	Transports->SetStringField(TEXT("editor_backend"), TEXT("tcp_json_bridge"));
	Transports->SetStringField(TEXT("mcp_wrapper"), TEXT("python_stdio_fastmcp"));
	Transports->SetStringField(TEXT("native_http_mcp"), bHttpServerRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), HttpPort) : TEXT(""));
	Transports->SetBoolField(TEXT("native_http_auth_required"), !HttpAuthToken.IsEmpty());
	Transports->SetStringField(TEXT("native_http_auth_env"), TEXT("COMMONAI_MCP_HTTP_TOKEN"));

	TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetNumberField(TEXT("schema_version"), 2);
	Manifest->SetStringField(TEXT("server"), TEXT("CommonAIExport"));
	Manifest->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Manifest->SetStringField(TEXT("manifest_source"), TEXT("FAIExportTCPServer::GetCommandDescriptors"));
	Manifest->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Manifest->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Manifest->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Manifest->SetNumberField(TEXT("tcp_port"), ServerPort);
	Manifest->SetNumberField(TEXT("http_port"), HttpPort);
	Manifest->SetStringField(TEXT("mcp_http_url"), bHttpServerRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), HttpPort) : TEXT(""));
	Manifest->SetStringField(TEXT("scope_model"), TEXT("read < write < destructive; destructive commands require explicit meta.scope"));
	Manifest->SetBoolField(TEXT("http_auth_required"), !HttpAuthToken.IsEmpty());
	Manifest->SetBoolField(TEXT("supports_http_mcp_sessions"), bHttpServerRunning);
	Manifest->SetBoolField(TEXT("supports_mcp_pagination"), bHttpServerRunning);
	Manifest->SetBoolField(TEXT("supports_http_audit"), bHttpServerRunning);
	Manifest->SetBoolField(TEXT("http_audit_enabled"), bHttpAuditEnabled);
	Manifest->SetStringField(TEXT("http_audit_log_path"), HttpAuditLogPath);
	Manifest->SetStringField(TEXT("mcp_protocol_version"), TEXT("2025-06-18"));
	Manifest->SetNumberField(TEXT("mcp_session_ttl_seconds"), McpSessionTtlSeconds);
	Manifest->SetArrayField(TEXT("http_allowed_origins"), AllowedOrigins);
	Manifest->SetArrayField(TEXT("supported_scopes"), Scopes);
	Manifest->SetObjectField(TEXT("transports"), Transports);
	Manifest->SetNumberField(TEXT("command_count"), Commands.Num());
	Manifest->SetArrayField(TEXT("commands"), Commands);
	return Manifest;
}

const TArray<FString>* FAIExportTCPServer::FindHttpHeaderValues(const FHttpServerRequest& Request, const FString& HeaderName) const
{
	if (const TArray<FString>* DirectValues = Request.Headers.Find(HeaderName))
	{
		return DirectValues;
	}

	for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
	{
		if (Pair.Key.Equals(HeaderName, ESearchCase::IgnoreCase))
		{
			return &Pair.Value;
		}
	}

	return nullptr;
}

FString FAIExportTCPServer::GetHttpHeaderValue(const FHttpServerRequest& Request, const FString& HeaderName) const
{
	if (const TArray<FString>* Values = FindHttpHeaderValues(Request, HeaderName))
	{
		if (Values->Num() > 0)
		{
			FString Value = (*Values)[0];
			Value.TrimStartAndEndInline();
			return Value;
		}
	}

	return FString();
}

bool FAIExportTCPServer::IsHttpOriginAllowed(const FString& Origin) const
{
	if (Origin.IsEmpty())
	{
		return true;
	}

	for (const FString& AllowedOrigin : HttpAllowedOrigins)
	{
		if (AllowedOrigin == TEXT("*"))
		{
			return true;
		}
		if (Origin.Equals(AllowedOrigin, ESearchCase::IgnoreCase) || Origin.StartsWith(AllowedOrigin, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool FAIExportTCPServer::IsHttpRequestAllowed(const FHttpServerRequest& Request) const
{
	if (Request.PeerAddress.IsValid())
	{
		const FString PeerIp = Request.PeerAddress->ToString(false);
		if (PeerIp != TEXT("127.0.0.1") && PeerIp != TEXT("::1") && PeerIp != TEXT("0:0:0:0:0:0:0:1"))
		{
			return false;
		}
	}

	const TArray<FString>* OriginValues = FindHttpHeaderValues(Request, TEXT("Origin"));
	if (OriginValues)
	{
		for (const FString& Origin : *OriginValues)
		{
			if (!IsHttpOriginAllowed(Origin))
			{
				return false;
			}
		}
	}

	if (!HttpAuthToken.IsEmpty())
	{
		bool bAuthorized = false;
		const TArray<FString>* AuthorizationValues = FindHttpHeaderValues(Request, TEXT("Authorization"));
		if (AuthorizationValues)
		{
			const FString ExpectedBearer = FString::Printf(TEXT("Bearer %s"), *HttpAuthToken);
			for (FString Authorization : *AuthorizationValues)
			{
				Authorization.TrimStartAndEndInline();
				if (Authorization == ExpectedBearer || Authorization == HttpAuthToken)
				{
					bAuthorized = true;
					break;
				}
			}
		}

		if (!bAuthorized)
		{
			return false;
		}
	}

	return true;
}

bool FAIExportTCPServer::IsMcpProtocolVersionAllowed(const FHttpServerRequest& Request, FString& OutError) const
{
	const TArray<FString>* ProtocolVersions = FindHttpHeaderValues(Request, TEXT("MCP-Protocol-Version"));
	if (!ProtocolVersions)
	{
		return true;
	}

	for (FString ProtocolVersion : *ProtocolVersions)
	{
		ProtocolVersion.TrimStartAndEndInline();
		if (ProtocolVersion.IsEmpty() || ProtocolVersion == TEXT("2025-06-18"))
		{
			return true;
		}
	}

	OutError = TEXT("Unsupported MCP protocol version. Supported version: 2025-06-18");
	return false;
}

void FAIExportTCPServer::PruneExpiredMcpSessionsLocked(const FDateTime& NowUtc)
{
	TArray<FString> ExpiredSessionIds;
	for (const TPair<FString, FMcpHttpSession>& Pair : ActiveMcpSessions)
	{
		const FTimespan IdleTime = NowUtc - Pair.Value.LastSeenUtc;
		if (IdleTime.GetTotalSeconds() > McpSessionTtlSeconds)
		{
			ExpiredSessionIds.Add(Pair.Key);
		}
	}

	for (const FString& SessionId : ExpiredSessionIds)
	{
		ActiveMcpSessions.Remove(SessionId);
		McpSessionExpiredCount.Increment();
	}
}

void FAIExportTCPServer::AppendHttpAuditEvent(const FHttpServerRequest& Request, const FString& Route, int32 ResponseCode, bool bAllowed, const FString& Detail, const FString& McpSessionId) const
{
	if (!bHttpAuditEnabled || HttpAuditLogPath.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Event->SetStringField(TEXT("route"), Route);
	Event->SetStringField(TEXT("verb"), CommonAIExportHttpVerbToString(Request.Verb));
	Event->SetNumberField(TEXT("response_code"), ResponseCode);
	Event->SetBoolField(TEXT("allowed"), bAllowed);
	Event->SetStringField(TEXT("detail"), Detail);
	if (!McpSessionId.IsEmpty())
	{
		Event->SetStringField(TEXT("mcp_session_id"), McpSessionId);
	}
	if (Request.PeerAddress.IsValid())
	{
		Event->SetStringField(TEXT("peer"), Request.PeerAddress->ToString(false));
	}
	const FString Origin = GetHttpHeaderValue(Request, TEXT("Origin"));
	if (!Origin.IsEmpty())
	{
		Event->SetStringField(TEXT("origin"), Origin);
	}
	const FString ProtocolVersion = GetHttpHeaderValue(Request, TEXT("MCP-Protocol-Version"));
	if (!ProtocolVersion.IsEmpty())
	{
		Event->SetStringField(TEXT("mcp_protocol_version"), ProtocolVersion);
	}

	FString Line;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Event.ToSharedRef(), Writer);
	Line += LINE_TERMINATOR;

	FScopeLock Lock(&HttpAuditCriticalSection);
	FFileHelper::SaveStringToFile(Line, *HttpAuditLogPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

TUniquePtr<FHttpServerResponse> FAIExportTCPServer::MakeHttpJsonResponse(const FString& Json, int32 ResponseCode, const FString& McpSessionId) const
{
	return MakeHttpTextResponse(Json, TEXT("application/json"), ResponseCode, McpSessionId);
}

TUniquePtr<FHttpServerResponse> FAIExportTCPServer::MakeHttpTextResponse(const FString& Text, const FString& ContentType, int32 ResponseCode, const FString& McpSessionId) const
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Text, ContentType);
	Response->Code = static_cast<EHttpServerResponseCodes>(ResponseCode);
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("http://localhost") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Accept, MCP-Protocol-Version, Authorization, Mcp-Session-Id") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, DELETE, OPTIONS") });
	Response->Headers.Add(TEXT("MCP-Protocol-Version"), { TEXT("2025-06-18") });
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-store") });
	Response->Headers.Add(TEXT("X-Content-Type-Options"), { TEXT("nosniff") });
	if (ContentType.StartsWith(TEXT("text/event-stream")))
	{
		Response->Headers.Add(TEXT("X-Accel-Buffering"), { TEXT("no") });
	}
	if (!McpSessionId.IsEmpty())
	{
		Response->Headers.Add(TEXT("Mcp-Session-Id"), { McpSessionId });
	}
	return Response;
}

void FAIExportTCPServer::StartHttpServer()
{
	if (bHttpServerRunning)
	{
		return;
	}

	HttpAuthToken = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_HTTP_TOKEN"));
	HttpAuthToken.TrimStartAndEndInline();
	if (HttpAuthToken.IsEmpty())
	{
		HttpAuthToken = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAIEXPORT_HTTP_TOKEN"));
		HttpAuthToken.TrimStartAndEndInline();
	}

	HttpAllowedOrigins.Reset();
	HttpAllowedOriginsConfig = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_HTTP_ALLOWED_ORIGINS"));
	HttpAllowedOriginsConfig.TrimStartAndEndInline();
	if (HttpAllowedOriginsConfig.IsEmpty())
	{
		HttpAllowedOriginsConfig = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAIEXPORT_HTTP_ALLOWED_ORIGINS"));
		HttpAllowedOriginsConfig.TrimStartAndEndInline();
	}
	if (!HttpAllowedOriginsConfig.IsEmpty())
	{
		TArray<FString> ConfiguredOrigins;
		HttpAllowedOriginsConfig.ParseIntoArray(ConfiguredOrigins, TEXT(","), true);
		for (FString Origin : ConfiguredOrigins)
		{
			Origin.TrimStartAndEndInline();
			if (!Origin.IsEmpty())
			{
				HttpAllowedOrigins.Add(Origin);
			}
		}
	}
	if (HttpAllowedOrigins.Num() == 0)
	{
		HttpAllowedOrigins.Add(TEXT("http://localhost"));
		HttpAllowedOrigins.Add(TEXT("http://127.0.0.1"));
		HttpAllowedOrigins.Add(TEXT("http://[::1]"));
	}

	FString SessionTtl = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_SESSION_TTL_SECONDS"));
	SessionTtl.TrimStartAndEndInline();
	if (!SessionTtl.IsEmpty())
	{
		const int32 ParsedTtl = FCString::Atoi(*SessionTtl);
		if (ParsedTtl >= 60)
		{
			McpSessionTtlSeconds = ParsedTtl;
		}
	}

	FString AuditEnabled = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_HTTP_AUDIT"));
	AuditEnabled.TrimStartAndEndInline();
	bHttpAuditEnabled = !AuditEnabled.Equals(TEXT("0"), ESearchCase::IgnoreCase)
		&& !AuditEnabled.Equals(TEXT("false"), ESearchCase::IgnoreCase)
		&& !AuditEnabled.Equals(TEXT("off"), ESearchCase::IgnoreCase);
	HttpAuditLogPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("CommonAIExport_HTTP_Audit.jsonl")));

	HttpRequestCount.Reset();
	HttpRejectedRequestCount.Reset();
	McpRequestCount.Reset();
	McpRejectedRequestCount.Reset();
	McpSessionExpiredCount.Reset();

	HttpPort = FindAvailablePort(55610, 55650);
	HttpRouter = FHttpServerModule::Get().GetHttpRouter(static_cast<uint32>(HttpPort), true);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogAIExport, Warning, TEXT("Could not create HTTP router on port %d"), HttpPort);
		HttpPort = 0;
		return;
	}

	auto BindRoute = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, TFunction<FString(const FHttpServerRequest&)> Handler)
	{
		const FString Route(Path);
		FHttpRouteHandle Handle = HttpRouter->BindRoute(
			FHttpPath(Path),
			Verbs,
			FHttpRequestHandler::CreateLambda([this, Handler, Route](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				HttpRequestCount.Increment();
				if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
				{
					AppendHttpAuditEvent(Request, Route, 200, true, TEXT("preflight"));
					OnComplete(MakeHttpJsonResponse(TEXT("{}")));
					return true;
				}
				if (!IsHttpRequestAllowed(Request))
				{
					HttpRejectedRequestCount.Increment();
					UE_LOG(LogAIExport, Warning, TEXT("Rejected CommonAIExport HTTP request for non-MCP route"));
					AppendHttpAuditEvent(Request, Route, 403, false, TEXT("forbidden"));
					OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Forbidden origin, peer address, or authorization\"}"), 403));
					return true;
				}
				const FString ResponseJson = Handler(Request);
				AppendHttpAuditEvent(Request, Route, 200, true, TEXT("ok"));
				OnComplete(MakeHttpJsonResponse(ResponseJson));
				return true;
			}));
		if (Handle.IsValid())
		{
			HttpRouteHandles.Add(Handle);
		}
	};

	auto BindTextRoute = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, const FString& ContentType, TFunction<FString(const FHttpServerRequest&)> Handler)
	{
		const FString Route(Path);
		FHttpRouteHandle Handle = HttpRouter->BindRoute(
			FHttpPath(Path),
			Verbs,
			FHttpRequestHandler::CreateLambda([this, Handler, Route, ContentType](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				HttpRequestCount.Increment();
				if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
				{
					AppendHttpAuditEvent(Request, Route, 200, true, TEXT("preflight"));
					OnComplete(MakeHttpTextResponse(TEXT(""), ContentType));
					return true;
				}
				if (!IsHttpRequestAllowed(Request))
				{
					HttpRejectedRequestCount.Increment();
					UE_LOG(LogAIExport, Warning, TEXT("Rejected CommonAIExport HTTP text request for non-MCP route"));
					AppendHttpAuditEvent(Request, Route, 403, false, TEXT("forbidden"));
					OnComplete(MakeHttpTextResponse(TEXT("Forbidden origin, peer address, or authorization"), TEXT("text/plain"), 403));
					return true;
				}
				const FString ResponseText = Handler(Request);
				AppendHttpAuditEvent(Request, Route, 200, true, TEXT("ok"));
				OnComplete(MakeHttpTextResponse(ResponseText, ContentType));
				return true;
			}));
		if (Handle.IsValid())
		{
			HttpRouteHandles.Add(Handle);
		}
	};

	BindRoute(TEXT("/commonai/health"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest&)
	{
		return HandlePing();
	});
	BindRoute(TEXT("/commonai/commands"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest&)
	{
		return HandleListCommands();
	});
	BindRoute(TEXT("/commonai/command"), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest& Request)
	{
		FUTF8ToTCHAR BodyText(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		return ProcessCommand(FString(BodyText.Length(), BodyText.Get()));
	});
	BindRoute(TEXT("/commonai/tasks/events"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest& Request)
	{
		return HandleTaskEvents(BuildTaskEventParamsFromHttpRequest(Request));
	});
	BindRoute(TEXT("/commonai/tasks/events/wait"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest& Request)
	{
		return HandleTaskEventsWait(BuildTaskEventParamsFromHttpRequest(Request));
	});
	BindTextRoute(TEXT("/commonai/tasks/events/sse"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, TEXT("text/event-stream"), [this](const FHttpServerRequest& Request)
	{
		return BuildTaskEventsSse(BuildTaskEventParamsFromHttpRequest(Request));
	});

	FHttpRouteHandle McpRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_DELETE | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			HttpRequestCount.Increment();
			if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
			{
				AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, true, TEXT("preflight"));
				OnComplete(MakeHttpJsonResponse(TEXT("{}")));
				return true;
			}
			if (!IsHttpRequestAllowed(Request))
			{
				HttpRejectedRequestCount.Increment();
				McpRejectedRequestCount.Increment();
				UE_LOG(LogAIExport, Warning, TEXT("Rejected CommonAIExport MCP HTTP request"));
				AppendHttpAuditEvent(Request, TEXT("/mcp"), 403, false, TEXT("forbidden"));
				OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Forbidden origin, peer address, or authorization\"}"), 403));
				return true;
			}

			FString ProtocolError;
			if (!IsMcpProtocolVersionAllowed(Request, ProtocolError))
			{
				HttpRejectedRequestCount.Increment();
				McpRejectedRequestCount.Increment();
				const FString SafeProtocolError = ProtocolError.Replace(TEXT("\""), TEXT("\\\""));
				const FString ErrorJson = FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32002,\"message\":\"%s\"}}"), *SafeProtocolError);
				AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, false, TEXT("unsupported_protocol"));
				OnComplete(MakeHttpJsonResponse(ErrorJson));
				return true;
			}

			McpRequestCount.Increment();
			const FString RequestSessionId = GetHttpHeaderValue(Request, TEXT("Mcp-Session-Id"));

			if (Request.Verb == EHttpServerRequestVerbs::VERB_DELETE)
			{
				if (RequestSessionId.IsEmpty())
				{
					McpRejectedRequestCount.Increment();
					AppendHttpAuditEvent(Request, TEXT("/mcp"), 400, false, TEXT("missing_session_for_delete"));
					OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Mcp-Session-Id header is required to delete a session\"}"), 400));
					return true;
				}

				bool bRemoved = false;
				{
					FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
					PruneExpiredMcpSessionsLocked(FDateTime::UtcNow());
					bRemoved = ActiveMcpSessions.Remove(RequestSessionId) > 0;
				}

				if (!bRemoved)
				{
					McpRejectedRequestCount.Increment();
					AppendHttpAuditEvent(Request, TEXT("/mcp"), 404, false, TEXT("unknown_session_delete"), RequestSessionId);
					OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Unknown or expired MCP session id\"}"), 404));
					return true;
				}

				AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, true, TEXT("session_deleted"), RequestSessionId);
				OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":true,\"message\":\"MCP session deleted\"}")));
				return true;
			}

			FUTF8ToTCHAR BodyText(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
			FString ResponseSessionId;
			const FString ResponseJson = HandleMcpJsonRpc(FString(BodyText.Length(), BodyText.Get()), RequestSessionId, ResponseSessionId);
			AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, !ResponseJson.Contains(TEXT("\"error\"")), ResponseJson.Contains(TEXT("\"error\"")) ? TEXT("jsonrpc_error") : TEXT("ok"), ResponseSessionId.IsEmpty() ? RequestSessionId : ResponseSessionId);
			OnComplete(MakeHttpJsonResponse(ResponseJson, 200, ResponseSessionId));
			return true;
		}));
	if (McpRouteHandle.IsValid())
	{
		HttpRouteHandles.Add(McpRouteHandle);
	}

	FHttpServerModule::Get().StartAllListeners();
	bHttpServerRunning = HttpRouteHandles.Num() > 0;

	if (bHttpServerRunning)
	{
		const FString HttpPortPath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("AIExport_http_port.txt"));
		FFileHelper::SaveStringToFile(FString::FromInt(HttpPort), *HttpPortPath);
		UE_LOG(LogAIExport, Log, TEXT("CommonAIExport HTTP/MCP routes listening on http://127.0.0.1:%d/mcp"), HttpPort);
	}
}

void FAIExportTCPServer::StopHttpServer()
{
	if (HttpRouter.IsValid())
	{
		for (const FHttpRouteHandle& Handle : HttpRouteHandles)
		{
			if (Handle.IsValid())
			{
				HttpRouter->UnbindRoute(Handle);
			}
		}
	}

	HttpRouteHandles.Reset();
	HttpRouter.Reset();
	bHttpServerRunning = false;
	HttpAuthToken.Reset();
	HttpAllowedOrigins.Reset();
	HttpAllowedOriginsConfig.Reset();
	HttpAuditLogPath.Reset();
	bHttpAuditEnabled = true;
	{
		FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
		ActiveMcpSessions.Reset();
	}
}

FString FAIExportTCPServer::HandleMcpJsonRpc(const FString& RequestJson, const FString& RequestSessionId, FString& OutSessionId)
{
	auto SerializeObject = [](const TSharedPtr<FJsonObject>& Object) -> FString
	{
		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	};

	auto MakeRpcResponse = [&SerializeObject](const TSharedPtr<FJsonValue>& IdValue, const TSharedPtr<FJsonObject>& Result) -> FString
	{
		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Response->SetField(TEXT("id"), IdValue.IsValid() ? IdValue : MakeShared<FJsonValueNull>());
		Response->SetObjectField(TEXT("result"), Result);
		return SerializeObject(Response);
	};

	auto MakeRpcError = [&SerializeObject](const TSharedPtr<FJsonValue>& IdValue, int32 Code, const FString& Message) -> FString
	{
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);

		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Response->SetField(TEXT("id"), IdValue.IsValid() ? IdValue : MakeShared<FJsonValueNull>());
		Response->SetObjectField(TEXT("error"), Error);
		return SerializeObject(Response);
	};

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return MakeRpcError(nullptr, -32700, TEXT("Parse error"));
	}

	TSharedPtr<FJsonValue> IdValue = Root->TryGetField(TEXT("id"));
	FString Method;
	if (!Root->TryGetStringField(TEXT("method"), Method))
	{
		return MakeRpcError(IdValue, -32600, TEXT("Invalid Request: missing method"));
	}

	const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
	Root->TryGetObjectField(TEXT("params"), ParamsObj);

	if (!RequestSessionId.IsEmpty() && Method != TEXT("initialize"))
	{
		bool bKnownSession = false;
		{
			FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
			const FDateTime NowUtc = FDateTime::UtcNow();
			PruneExpiredMcpSessionsLocked(NowUtc);
			if (FMcpHttpSession* Session = ActiveMcpSessions.Find(RequestSessionId))
			{
				Session->LastSeenUtc = NowUtc;
				bKnownSession = true;
			}
		}

		if (!bKnownSession)
		{
			McpRejectedRequestCount.Increment();
			return MakeRpcError(IdValue, -32001, TEXT("Unknown or expired MCP session id"));
		}
		OutSessionId = RequestSessionId;
	}

	if (Method == TEXT("initialize"))
	{
		const FString NewSessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		FString ClientName = TEXT("unknown");
		if (ParamsObj && ParamsObj->IsValid())
		{
			const TSharedPtr<FJsonObject>* ClientInfo = nullptr;
			if ((*ParamsObj)->TryGetObjectField(TEXT("clientInfo"), ClientInfo) && ClientInfo && ClientInfo->IsValid())
			{
				(*ClientInfo)->TryGetStringField(TEXT("name"), ClientName);
			}
		}
		{
			FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
			const FDateTime NowUtc = FDateTime::UtcNow();
			PruneExpiredMcpSessionsLocked(NowUtc);

			FMcpHttpSession Session;
			Session.SessionId = NewSessionId;
			Session.ClientName = ClientName;
			Session.CreatedAtUtc = NowUtc.ToIso8601();
			Session.LastSeenUtc = NowUtc;
			ActiveMcpSessions.Add(NewSessionId, Session);
		}
		OutSessionId = NewSessionId;

		TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
		Capabilities->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());
		Capabilities->SetObjectField(TEXT("resources"), MakeShared<FJsonObject>());
		Capabilities->SetObjectField(TEXT("prompts"), MakeShared<FJsonObject>());
		TSharedPtr<FJsonObject> Experimental = MakeShared<FJsonObject>();
		Experimental->SetBoolField(TEXT("sessions"), true);
		Experimental->SetBoolField(TEXT("pagination"), true);
		Capabilities->SetObjectField(TEXT("experimental"), Experimental);

		TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
		ServerInfo->SetStringField(TEXT("name"), TEXT("CommonAIExport"));
		ServerInfo->SetStringField(TEXT("version"), TEXT("0.3.0"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));
		Result->SetObjectField(TEXT("capabilities"), Capabilities);
		Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
		Result->SetStringField(TEXT("sessionId"), NewSessionId);
		Result->SetNumberField(TEXT("sessionTtlSeconds"), McpSessionTtlSeconds);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("notifications/initialized"))
	{
		return MakeRpcResponse(IdValue, MakeShared<FJsonObject>());
	}

	if (Method == TEXT("tools/list"))
	{
		int32 StartIndex = 0;
		if (ParamsObj && ParamsObj->IsValid())
		{
			FString Cursor;
			if ((*ParamsObj)->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.IsEmpty())
			{
				if (Cursor.StartsWith(TEXT("offset:")))
				{
					Cursor = Cursor.Mid(7);
				}
				StartIndex = FMath::Max(0, FCString::Atoi(*Cursor));
			}
		}

		const int32 PageSize = 50;
		const TArray<FCommandDescriptor>& Descriptors = GetCommandDescriptors();
		const int32 EndIndex = FMath::Min(StartIndex + PageSize, Descriptors.Num());

		TArray<TSharedPtr<FJsonValue>> Tools;
		for (int32 Index = StartIndex; Index < EndIndex; ++Index)
		{
			const FCommandDescriptor& Descriptor = Descriptors[Index];
			TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetBoolField(TEXT("additionalProperties"), true);

			TSharedPtr<FJsonObject> Annotations = MakeShared<FJsonObject>();
			Annotations->SetBoolField(TEXT("readOnlyHint"), !Descriptor.bMutating);
			Annotations->SetBoolField(TEXT("destructiveHint"), FString(Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read")) == TEXT("destructive"));
			Annotations->SetBoolField(TEXT("idempotentHint"), !Descriptor.bMutating);

			TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
			Tool->SetStringField(TEXT("name"), Descriptor.Name);
			Tool->SetStringField(TEXT("description"), FString::Printf(TEXT("CommonAIExport %s command. category=%s scope=%s dry_run=%s"), Descriptor.Name, Descriptor.Category, Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read"), Descriptor.bSupportsDryRun ? TEXT("true") : TEXT("false")));
			Tool->SetObjectField(TEXT("inputSchema"), Schema);
			Tool->SetObjectField(TEXT("annotations"), Annotations);
			Tools.Add(MakeShared<FJsonValueObject>(Tool));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("tools"), Tools);
		if (EndIndex < Descriptors.Num())
		{
			Result->SetStringField(TEXT("nextCursor"), FString::Printf(TEXT("offset:%d"), EndIndex));
		}
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("tools/call"))
	{
		if (!ParamsObj || !ParamsObj->IsValid())
		{
			return MakeRpcError(IdValue, -32602, TEXT("tools/call requires params"));
		}

		FString Name;
		if (!(*ParamsObj)->TryGetStringField(TEXT("name"), Name))
		{
			return MakeRpcError(IdValue, -32602, TEXT("tools/call requires params.name"));
		}

		const TSharedPtr<FJsonObject>* ArgumentsObj = nullptr;
		(*ParamsObj)->TryGetObjectField(TEXT("arguments"), ArgumentsObj);

		TSharedPtr<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("type"), Name);
		if (ArgumentsObj && ArgumentsObj->IsValid())
		{
			Command->SetObjectField(TEXT("params"), *ArgumentsObj);

			TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
			FString Scope;
			bool bDryRun = false;
			if ((*ArgumentsObj)->TryGetStringField(TEXT("scope"), Scope) && !Scope.IsEmpty())
			{
				Meta->SetStringField(TEXT("scope"), Scope);
			}
			if ((*ArgumentsObj)->TryGetBoolField(TEXT("dry_run"), bDryRun) && bDryRun)
			{
				Meta->SetBoolField(TEXT("dry_run"), true);
			}
			if (Meta->Values.Num() > 0)
			{
				Command->SetObjectField(TEXT("meta"), Meta);
			}
		}

		const FString CommandJson = SerializeObject(Command);
		const FString RawResponse = ProcessCommand(CommandJson);

		bool bSuccess = false;
		TSharedPtr<FJsonObject> ParsedResponse;
		TSharedRef<TJsonReader<>> ResponseReader = TJsonReaderFactory<>::Create(RawResponse);
		if (FJsonSerializer::Deserialize(ResponseReader, ParsedResponse) && ParsedResponse.IsValid())
		{
			ParsedResponse->TryGetBoolField(TEXT("success"), bSuccess);
		}

		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), RawResponse);

		TArray<TSharedPtr<FJsonValue>> Content;
		Content.Add(MakeShared<FJsonValueObject>(TextContent));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("content"), Content);
		Result->SetBoolField(TEXT("isError"), !bSuccess);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("resources/list"))
	{
		TArray<TSharedPtr<FJsonValue>> Resources;
		auto AddResource = [&Resources](const TCHAR* Uri, const TCHAR* Name, const TCHAR* Description)
		{
			TSharedPtr<FJsonObject> Resource = MakeShared<FJsonObject>();
			Resource->SetStringField(TEXT("uri"), Uri);
			Resource->SetStringField(TEXT("name"), Name);
			Resource->SetStringField(TEXT("description"), Description);
			Resource->SetStringField(TEXT("mimeType"), TEXT("application/json"));
			Resources.Add(MakeShared<FJsonValueObject>(Resource));
		};
		AddResource(TEXT("commonai://project/status"), TEXT("Project Status"), TEXT("Current CommonAIExport project/editor status"));
		AddResource(TEXT("commonai://commands/manifest"), TEXT("Command Manifest"), TEXT("Command descriptor manifest"));
		AddResource(TEXT("commonai://editor/status"), TEXT("Editor Identity"), TEXT("Current editor identity and capabilities"));
		AddResource(TEXT("commonai://logs/latest"), TEXT("Latest Log"), TEXT("Recent project log lines"));
		AddResource(TEXT("commonai://audit/http"), TEXT("HTTP MCP Audit"), TEXT("Recent CommonAIExport native HTTP/MCP audit JSONL events"));
		AddResource(TEXT("commonai://tasks/events"), TEXT("Async Task Events"), TEXT("Recent CommonAIExport async task lifecycle events"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("resources"), Resources);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("resources/read"))
	{
		if (!ParamsObj || !ParamsObj->IsValid())
		{
			return MakeRpcError(IdValue, -32602, TEXT("resources/read requires params"));
		}

		FString Uri;
		if (!(*ParamsObj)->TryGetStringField(TEXT("uri"), Uri))
		{
			return MakeRpcError(IdValue, -32602, TEXT("resources/read requires params.uri"));
		}

		FString Text;
		if (Uri == TEXT("commonai://project/status")) Text = HandleProjectStatus();
		else if (Uri == TEXT("commonai://commands/manifest")) Text = HandleListCommands();
		else if (Uri == TEXT("commonai://editor/status")) Text = HandleEditorIdentity();
		else if (Uri == TEXT("commonai://logs/latest"))
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetNumberField(TEXT("max_lines"), 200);
			Text = HandleEditorLogRead(Params);
		}
		else if (Uri == TEXT("commonai://audit/http"))
		{
			TArray<FString> Lines;
			if (!HttpAuditLogPath.IsEmpty() && FPaths::FileExists(HttpAuditLogPath))
			{
				FFileHelper::LoadFileToStringArray(Lines, *HttpAuditLogPath);
			}

			TArray<TSharedPtr<FJsonValue>> Events;
			const int32 StartIndex = FMath::Max(0, Lines.Num() - 200);
			for (int32 Index = StartIndex; Index < Lines.Num(); ++Index)
			{
				const FString& Line = Lines[Index];
				if (Line.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}

				TSharedPtr<FJsonObject> ParsedEvent;
				TSharedRef<TJsonReader<>> EventReader = TJsonReaderFactory<>::Create(Line);
				if (FJsonSerializer::Deserialize(EventReader, ParsedEvent) && ParsedEvent.IsValid())
				{
					Events.Add(MakeShared<FJsonValueObject>(ParsedEvent));
				}
				else
				{
					TSharedPtr<FJsonObject> ErrorEvent = MakeShared<FJsonObject>();
					ErrorEvent->SetBoolField(TEXT("parse_error"), true);
					ErrorEvent->SetStringField(TEXT("raw"), Line);
					Events.Add(MakeShared<FJsonValueObject>(ErrorEvent));
				}
			}

			TSharedPtr<FJsonObject> Audit = MakeShared<FJsonObject>();
			Audit->SetBoolField(TEXT("success"), true);
			Audit->SetStringField(TEXT("log_path"), HttpAuditLogPath);
			Audit->SetNumberField(TEXT("line_count"), Lines.Num());
			Audit->SetNumberField(TEXT("returned_count"), Events.Num());
			Audit->SetArrayField(TEXT("events"), Events);
			TSharedRef<TJsonWriter<>> AuditWriter = TJsonWriterFactory<>::Create(&Text);
			FJsonSerializer::Serialize(Audit.ToSharedRef(), AuditWriter);
		}
		else if (Uri == TEXT("commonai://tasks/events"))
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetNumberField(TEXT("limit"), 200);
			Text = HandleTaskEvents(Params);
		}
		else return MakeRpcError(IdValue, -32602, FString::Printf(TEXT("Unknown resource URI: %s"), *Uri));

		TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
		Content->SetStringField(TEXT("uri"), Uri);
		Content->SetStringField(TEXT("mimeType"), TEXT("application/json"));
		Content->SetStringField(TEXT("text"), Text);
		TArray<TSharedPtr<FJsonValue>> Contents;
		Contents.Add(MakeShared<FJsonValueObject>(Content));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("contents"), Contents);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("prompts/list"))
	{
		TArray<TSharedPtr<FJsonValue>> Prompts;
		auto AddPrompt = [&Prompts](const TCHAR* Name, const TCHAR* Description)
		{
			TSharedPtr<FJsonObject> Prompt = MakeShared<FJsonObject>();
			Prompt->SetStringField(TEXT("name"), Name);
			Prompt->SetStringField(TEXT("description"), Description);
			Prompts.Add(MakeShared<FJsonValueObject>(Prompt));
		};
		AddPrompt(TEXT("build_fix_test"), TEXT("Guarded ProjectOkey build/fix/test workflow"));
		AddPrompt(TEXT("asset_safety_review"), TEXT("Review an Unreal asset before copying, deleting, or mutating it"));
		AddPrompt(TEXT("multi_editor_transfer"), TEXT("Plan guarded transfer between open Unreal projects"));
		AddPrompt(TEXT("ui_transfer_validation"), TEXT("Validate UI transfer tasks before production Widget Blueprint mutation"));
		AddPrompt(TEXT("blueprint_graph_inspection"), TEXT("Inspect Blueprint graph structure before graph mutation"));
		AddPrompt(TEXT("runtime_debug_triage"), TEXT("Triage runtime/editor issues with status, logs, PIE, and audit context"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("prompts"), Prompts);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("prompts/get"))
	{
		if (!ParamsObj || !ParamsObj->IsValid())
		{
			return MakeRpcError(IdValue, -32602, TEXT("prompts/get requires params"));
		}
		FString Name;
		(*ParamsObj)->TryGetStringField(TEXT("name"), Name);
		FString PromptText;
		if (Name == TEXT("build_fix_test"))
		{
			PromptText = TEXT("Use the guarded build workflow. Do not use Live Coding. Relaunch Unreal Editor through the existing VS2022 OkeyGame.sln Local Windows Debugger/F5 session. Check project_status, guarded build logs, and editor logs before and after fixes.");
		}
		else if (Name == TEXT("asset_safety_review"))
		{
			PromptText = TEXT("Before mutating an asset, run asset_validate_light, get_dependencies, get_referencers, and dry-run any destructive operation. Require explicit destructive scope for deletes/overwrites.");
		}
		else if (Name == TEXT("multi_editor_transfer"))
		{
			PromptText = TEXT("Use editors_list/editor registry, plan with asset_transfer_plan or code_transfer_plan, execute only after collision and scope review, then verify and run guarded build/status checks.");
		}
		else if (Name == TEXT("ui_transfer_validation"))
		{
			PromptText = TEXT("Before mutating production UI assets, read AI_UI_TRANSFER.md, Docs/AI_UI_Transfer/START_HERE.md, CommonUI architecture docs, and relevant component recipes. Ensure a TSpec exists and passes Scripts/ValidateUITSpecs.ps1. Probe uncertain components under /Game/UI/_AIProbe first.");
		}
		else if (Name == TEXT("blueprint_graph_inspection"))
		{
			PromptText = TEXT("Before editing Blueprint graphs, inspect with get_graph/list_graphs, identify existing events/functions/variables, and make narrowly scoped graph changes. Compile/save and inspect graph state after changes.");
		}
		else if (Name == TEXT("runtime_debug_triage"))
		{
			PromptText = TEXT("Start with project_status, server_status, pie_status, editor_log_read(filter='Error'), commonai://audit/http, and guarded_build_status. Reproduce in PIE only when needed; rerun smoke_mcp_runtime.py after fixes.");
		}
		else
		{
			return MakeRpcError(IdValue, -32602, FString::Printf(TEXT("Unknown prompt: %s"), *Name));
		}

		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), PromptText);
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), TEXT("user"));
		Message->SetObjectField(TEXT("content"), TextContent);
		TArray<TSharedPtr<FJsonValue>> Messages;
		Messages.Add(MakeShared<FJsonValueObject>(Message));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("description"), Name);
		Result->SetArrayField(TEXT("messages"), Messages);
		return MakeRpcResponse(IdValue, Result);
	}

	return MakeRpcError(IdValue, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
}

void FAIExportTCPServer::WriteEditorRegistryFile()
{
	if (EditorRegistryFilePath.IsEmpty())
	{
		EditorRegistryFilePath = GetEditorRegistryFilePath(ServerPort);
	}

	const FString RegistryDir = GetEditorRegistryDir();
	IFileManager::Get().MakeDirectory(*RegistryDir, true);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(BuildEditorIdentityJson().ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *EditorRegistryFilePath))
	{
		UE_LOG(LogAIExport, Log, TEXT("Written editor registry entry: %s"), *EditorRegistryFilePath);
	}
	else
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to write editor registry entry: %s"), *EditorRegistryFilePath);
	}
}

void FAIExportTCPServer::RemoveEditorRegistryFile()
{
	if (EditorRegistryFilePath.IsEmpty())
	{
		return;
	}

	if (IFileManager::Get().FileExists(*EditorRegistryFilePath))
	{
		IFileManager::Get().Delete(*EditorRegistryFilePath, false, true, true);
		UE_LOG(LogAIExport, Log, TEXT("Removed editor registry entry: %s"), *EditorRegistryFilePath);
	}
}

TSharedPtr<FJsonObject> FAIExportTCPServer::BuildTaskJson(const FAsyncCommandJob& Job, bool bIncludeResult) const
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), Job.TaskId);
	Data->SetStringField(TEXT("command"), Job.CommandName);
	Data->SetStringField(TEXT("status"), Job.Status);
	Data->SetStringField(TEXT("submitted_at"), Job.SubmittedAt);
	Data->SetBoolField(TEXT("cancel_requested"), Job.bCancelRequested);
	if (!Job.StartedAt.IsEmpty())
	{
		Data->SetStringField(TEXT("started_at"), Job.StartedAt);
	}
	if (!Job.FinishedAt.IsEmpty())
	{
		Data->SetStringField(TEXT("finished_at"), Job.FinishedAt);
	}
	if (!Job.ErrorMessage.IsEmpty())
	{
		Data->SetStringField(TEXT("error"), Job.ErrorMessage);
	}
	if (bIncludeResult && !Job.ResultJson.IsEmpty())
	{
		Data->SetStringField(TEXT("response_json"), Job.ResultJson);

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Job.ResultJson);
		TSharedPtr<FJsonObject> ParsedResult;
		if (FJsonSerializer::Deserialize(Reader, ParsedResult) && ParsedResult.IsValid())
		{
			Data->SetObjectField(TEXT("response"), ParsedResult);
		}
	}
	return Data;
}

TSharedPtr<FJsonObject> FAIExportTCPServer::BuildTaskEventJson(const FAsyncCommandEvent& Event) const
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("sequence"), static_cast<double>(Event.Sequence));
	Data->SetStringField(TEXT("task_id"), Event.TaskId);
	Data->SetStringField(TEXT("command"), Event.CommandName);
	Data->SetStringField(TEXT("status"), Event.Status);
	Data->SetStringField(TEXT("event"), Event.EventType);
	Data->SetStringField(TEXT("timestamp_utc"), Event.TimestampUtc);
	if (!Event.Message.IsEmpty())
	{
		Data->SetStringField(TEXT("message"), Event.Message);
	}
	return Data;
}

TSharedPtr<FJsonObject> FAIExportTCPServer::BuildTaskEventsJson(TSharedPtr<FJsonObject> Params) const
{
	FString TaskId;
	double AfterSequenceNumber = 0.0;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
		Params->TryGetNumberField(TEXT("after_sequence"), AfterSequenceNumber);
	}
	TaskId.TrimStartAndEndInline();
	const int64 AfterSequence = FMath::Max<int64>(0, static_cast<int64>(AfterSequenceNumber));
	const int32 Limit = ReadClampedIntField(Params, TEXT("limit"), 100, 1, 1000);

	TArray<TSharedPtr<FJsonValue>> Events;
	int32 MatchedCount = 0;
	int64 LatestSequence = 0;
	{
		FScopeLock Lock(&AsyncJobsCriticalSection);
		LatestSequence = AsyncJobEventSequence;
		for (const FAsyncCommandEvent& Event : AsyncJobEvents)
		{
			if (!TaskId.IsEmpty() && Event.TaskId != TaskId)
			{
				continue;
			}
			if (Event.Sequence <= AfterSequence)
			{
				continue;
			}

			++MatchedCount;
			if (Events.Num() < Limit)
			{
				Events.Add(MakeShared<FJsonValueObject>(BuildTaskEventJson(Event)));
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	Data->SetNumberField(TEXT("after_sequence"), static_cast<double>(AfterSequence));
	Data->SetNumberField(TEXT("latest_sequence"), static_cast<double>(LatestSequence));
	Data->SetNumberField(TEXT("matched_count"), MatchedCount);
	Data->SetNumberField(TEXT("returned_count"), Events.Num());
	Data->SetNumberField(TEXT("limit"), Limit);
	Data->SetBoolField(TEXT("has_more"), MatchedCount > Events.Num());
	Data->SetArrayField(TEXT("events"), Events);
	return Data;
}

TSharedPtr<FJsonObject> FAIExportTCPServer::BuildTaskEventsWaitJson(TSharedPtr<FJsonObject> Params) const
{
	const int32 TimeoutMs = ReadClampedIntField(Params, TEXT("timeout_ms"), 5000, 0, 30000);
	const int32 PollIntervalMs = ReadClampedIntField(Params, TEXT("poll_interval_ms"), 50, 10, 1000);
	const double StartSeconds = FPlatformTime::Seconds();
	const double DeadlineSeconds = StartSeconds + (static_cast<double>(TimeoutMs) / 1000.0);

	TSharedPtr<FJsonObject> Data;
	bool bTimedOut = false;
	bool bStopped = false;

	while (true)
	{
		Data = BuildTaskEventsJson(Params);
		const int32 ReturnedCount = Data.IsValid() ? static_cast<int32>(Data->GetNumberField(TEXT("returned_count"))) : 0;
		if (ReturnedCount > 0 || TimeoutMs <= 0)
		{
			break;
		}

		if (bStopRequested)
		{
			bStopped = true;
			break;
		}

		const double RemainingSeconds = DeadlineSeconds - FPlatformTime::Seconds();
		if (RemainingSeconds <= 0.0)
		{
			bTimedOut = true;
			break;
		}

		const double SleepSeconds = FMath::Min(RemainingSeconds, static_cast<double>(PollIntervalMs) / 1000.0);
		FPlatformProcess::Sleep(static_cast<float>(SleepSeconds));
	}

	if (!Data.IsValid())
	{
		Data = BuildTaskEventsJson(Params);
	}

	const int32 ReturnedCount = Data.IsValid() ? static_cast<int32>(Data->GetNumberField(TEXT("returned_count"))) : 0;
	if (ReturnedCount <= 0 && TimeoutMs > 0 && !bStopped && FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		bTimedOut = true;
	}

	const double WaitedMs = FMath::Max(0.0, (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	Data->SetBoolField(TEXT("waited"), true);
	Data->SetNumberField(TEXT("timeout_ms"), TimeoutMs);
	Data->SetNumberField(TEXT("poll_interval_ms"), PollIntervalMs);
	Data->SetNumberField(TEXT("waited_ms"), WaitedMs);
	Data->SetBoolField(TEXT("timed_out"), bTimedOut && ReturnedCount <= 0);
	Data->SetBoolField(TEXT("server_stopping"), bStopped);
	return Data;
}

FString FAIExportTCPServer::BuildTaskEventsSse(TSharedPtr<FJsonObject> Params) const
{
	TSharedPtr<FJsonObject> Data = BuildTaskEventsJson(Params);
	FString Output = TEXT(": CommonAIExport async task events\nretry: 1000\n\n");

	FString WatermarkJson;
	{
		TSharedPtr<FJsonObject> Watermark = MakeShared<FJsonObject>();
		Watermark->SetNumberField(TEXT("latest_sequence"), Data->GetNumberField(TEXT("latest_sequence")));
		Watermark->SetNumberField(TEXT("returned_count"), Data->GetNumberField(TEXT("returned_count")));
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&WatermarkJson);
		FJsonSerializer::Serialize(Watermark.ToSharedRef(), Writer);
	}
	Output += FString::Printf(TEXT("event: watermark\ndata: %s\n\n"), *WatermarkJson);

	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (!Data->TryGetArrayField(TEXT("events"), Events) || !Events)
	{
		return Output;
	}

	for (const TSharedPtr<FJsonValue>& EventValue : *Events)
	{
		const TSharedPtr<FJsonObject> EventObject = EventValue.IsValid() ? EventValue->AsObject() : nullptr;
		if (!EventObject.IsValid())
		{
			continue;
		}

		const int64 Sequence = static_cast<int64>(EventObject->GetNumberField(TEXT("sequence")));
		const FString EventName = EventObject->GetStringField(TEXT("event"));
		FString EventJson;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&EventJson);
		FJsonSerializer::Serialize(EventObject.ToSharedRef(), Writer);
		Output += FString::Printf(TEXT("id: %lld\nevent: %s\ndata: %s\n\n"), Sequence, *EventName, *EventJson);
	}

	return Output;
}

TSharedPtr<FJsonObject> FAIExportTCPServer::BuildTaskEventParamsFromHttpRequest(const FHttpServerRequest& Request) const
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (const FString* TaskId = Request.QueryParams.Find(TEXT("task_id")))
	{
		Params->SetStringField(TEXT("task_id"), *TaskId);
	}
	if (const FString* AfterSequence = Request.QueryParams.Find(TEXT("after_sequence")))
	{
		Params->SetNumberField(TEXT("after_sequence"), FCString::Atod(**AfterSequence));
	}
	if (const FString* Limit = Request.QueryParams.Find(TEXT("limit")))
	{
		Params->SetNumberField(TEXT("limit"), FCString::Atod(**Limit));
	}
	if (const FString* TimeoutMs = Request.QueryParams.Find(TEXT("timeout_ms")))
	{
		Params->SetNumberField(TEXT("timeout_ms"), FCString::Atod(**TimeoutMs));
	}
	if (const FString* PollIntervalMs = Request.QueryParams.Find(TEXT("poll_interval_ms")))
	{
		Params->SetNumberField(TEXT("poll_interval_ms"), FCString::Atod(**PollIntervalMs));
	}
	return Params;
}

void FAIExportTCPServer::AppendTaskEventLocked(const FAsyncCommandJob& Job, const FString& EventType, const FString& Message)
{
	FAsyncCommandEvent Event;
	Event.Sequence = ++AsyncJobEventSequence;
	Event.TaskId = Job.TaskId;
	Event.CommandName = Job.CommandName;
	Event.Status = Job.Status;
	Event.EventType = EventType;
	Event.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Event.Message = Message;

	AsyncJobEvents.Add(Event);
	while (AsyncJobEvents.Num() > MaxAsyncJobEvents)
	{
		AsyncJobEvents.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

bool FAIExportTCPServer::TryCopyTask(const FString& TaskId, FAsyncCommandJob& OutJob) const
{
	FScopeLock Lock(&AsyncJobsCriticalSection);
	const TSharedPtr<FAsyncCommandJob>* FoundJob = AsyncJobs.Find(TaskId);
	if (!FoundJob || !FoundJob->IsValid())
	{
		return false;
	}

	OutJob = *FoundJob->Get();
	return true;
}

bool FAIExportTCPServer::Init()
{
	return true;
}

uint32 FAIExportTCPServer::Run()
{
	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server thread started on port %d"), ServerPort);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to get socket subsystem"));
		return 1;
	}

	// Create listener socket
	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("AIExportTCPServer"), false);
	if (!ListenerSocket)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to create listener socket"));
		return 1;
	}

	// Set socket options
	ListenerSocket->SetReuseAddr(true);
	ListenerSocket->SetNoDelay(true);

	// Bind to localhost only (firewall-friendly)
	FIPv4Address LocalAddress;
	FIPv4Address::Parse(TEXT("127.0.0.1"), LocalAddress);
	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	Addr->SetIp(LocalAddress.Value);
	Addr->SetPort(ServerPort);

	if (!ListenerSocket->Bind(*Addr))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to bind to port %d"), ServerPort);
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return 1;
	}

	if (!ListenerSocket->Listen(8))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to listen on socket"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return 1;
	}

	bIsRunning = true;
	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server listening on 127.0.0.1:%d"), ServerPort);
	WriteEditorRegistryFile();

	// Main accept loop
	while (!bStopRequested)
	{
		bool bHasPendingConnection = false;
		if (ListenerSocket->WaitForPendingConnection(bHasPendingConnection, FTimespan::FromMilliseconds(100)))
		{
			if (bHasPendingConnection)
			{
				TSharedRef<FInternetAddr> RemoteAddress = SocketSubsystem->CreateInternetAddr();
				FSocket* ClientSocket = ListenerSocket->Accept(*RemoteAddress, TEXT("AIExportClient"));

				if (ClientSocket)
				{
					UE_LOG(LogAIExport, Verbose, TEXT("Client connected"));
					ActiveClientConnections.Increment();
					Async(EAsyncExecution::ThreadPool, [this, ClientSocket, SocketSubsystem]()
					{
						HandleClientConnection(ClientSocket);
						SocketSubsystem->DestroySocket(ClientSocket);
						ActiveClientConnections.Decrement();
					});
				}
			}
		}
	}

	// Cleanup
	if (ListenerSocket)
	{
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
	}

	while (ActiveClientConnections.GetValue() > 0)
	{
		FPlatformProcess::Sleep(0.01f);
	}

	bIsRunning = false;
	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server stopped"));

	return 0;
}

void FAIExportTCPServer::Stop()
{
	bStopRequested = true;
}

void FAIExportTCPServer::Exit()
{
	bIsRunning = false;
}

void FAIExportTCPServer::StartServer()
{
	if (bIsRunning)
	{
		UE_LOG(LogAIExport, Warning, TEXT("TCP Server already running"));
		return;
	}

	// Find an available port
	ServerPort = FindAvailablePort(55560, 55600);
	EditorInstanceId = FString::Printf(TEXT("%s-%u-%d"), FApp::GetProjectName(), FPlatformProcess::GetCurrentProcessId(), ServerPort);
	EditorRegistryFilePath = GetEditorRegistryFilePath(ServerPort);
	ServerStartedAtUtc = FDateTime::UtcNow().ToIso8601();

	// Write port to discovery file
	WritePortFile(ServerPort);
	StartHttpServer();

	bStopRequested = false;
	ServerThread = FRunnableThread::Create(this, TEXT("AIExportTCPServerThread"), 0, TPri_Normal);

	if (!ServerThread)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to create server thread"));
	}
}

void FAIExportTCPServer::StopServer()
{
	if (!bIsRunning && !ServerThread)
	{
		return;
	}

	UE_LOG(LogAIExport, Log, TEXT("Stopping AIExport TCP Server..."));

	StopHttpServer();
	bStopRequested = true;

	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}

	RemoveEditorRegistryFile();
}

void FAIExportTCPServer::HandleClientConnection(FSocket* ClientSocket)
{
	if (!ClientSocket)
	{
		return;
	}

	// Set receive buffer size
	int32 ActualSize = 0;
	ClientSocket->SetReceiveBufferSize(65536, ActualSize);

	// Read incoming data
	TArray<uint8> RecvBuffer;
	RecvBuffer.SetNumZeroed(65536);

	int32 BytesRead = 0;

	// Wait for data with timeout
	bool bHasData = false;
	ClientSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0));

	if (ClientSocket->Recv(RecvBuffer.GetData(), RecvBuffer.Num(), BytesRead))
	{
		if (BytesRead > 0)
		{
			// Convert to string
			FString JsonCommand = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(RecvBuffer.GetData())));
			JsonCommand = JsonCommand.Left(BytesRead);

			UE_LOG(LogAIExport, Verbose, TEXT("Received command: %s"), *JsonCommand.Left(200));

			// Process command
			FString Response = ProcessCommand(JsonCommand);

			// Send response
			FTCHARToUTF8 Converter(*Response);
			int32 BytesSent = 0;
			ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length(), BytesSent);

			UE_LOG(LogAIExport, Verbose, TEXT("Sent response: %d bytes"), BytesSent);
		}
	}
	else
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to receive data from client"));
	}
}

FString FAIExportTCPServer::ProcessCommand(const FString& JsonCommand)
{
	// Parse JSON
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	TSharedPtr<FJsonObject> RootObject;

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid JSON format"));
	}

	// Get command type
	FString CommandType;
	if (!RootObject->TryGetStringField(TEXT("type"), CommandType))
	{
		return CreateErrorResponse(TEXT("Missing 'type' field"));
	}

	// Get params (optional for no-parameter commands)
	TSharedPtr<FJsonObject> Params;
	if (RootObject->HasField(TEXT("params")))
	{
		const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
		if (!RootObject->TryGetObjectField(TEXT("params"), ParamsObject) || !ParamsObject || !ParamsObject->IsValid())
		{
			return CreateErrorResponse(TEXT("'params' must be a JSON object when provided"));
		}
		Params = *ParamsObject;
	}

	UE_LOG(LogAIExport, Log, TEXT("Processing command: %s"), *CommandType);

	const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandType);
	if (!Descriptor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown command: %s"), *CommandType));
	}

	if (Descriptor->bRequiresParams && !Params.IsValid())
	{
		return CreateErrorResponse(FString::Printf(TEXT("Command '%s' requires a 'params' object"), *CommandType));
	}

	const FAICommandContext Context = BuildCommandContext(RootObject, *Descriptor);
	FString ScopeError;
	if (!ValidateCommandScope(*Descriptor, Context, ScopeError))
	{
		return CreateErrorResponse(ScopeError);
	}

	if (Context.bDryRun && Descriptor->bMutating)
	{
		return CreateDryRunResponse(*Descriptor, Context);
	}

	return DispatchCommand(*Descriptor, Params);
}

FString FAIExportTCPServer::HandlePing()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("message"), TEXT("pong"));
	Data->SetStringField(TEXT("server"), TEXT("CommonAIExport"));
	Data->SetStringField(TEXT("editor_id"), EditorInstanceId);
	Data->SetNumberField(TEXT("port"), ServerPort);
	Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Data->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Data->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - GStartTime);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleListCommands()
{
	TSharedPtr<FJsonObject> Data = BuildCommandManifestJson();
	Data->SetNumberField(TEXT("count"), GetCommandDescriptors().Num());
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleServerStatus()
{
	int32 QueuedTasks = 0;
	int32 RunningTasks = 0;
	int32 CompletedTasks = 0;
	int32 FailedTasks = 0;
	int32 CancelledTasks = 0;
	int32 TaskEventCount = 0;
	int64 LatestTaskEventSequence = 0;

	{
		FScopeLock Lock(&AsyncJobsCriticalSection);
		for (const TPair<FString, TSharedPtr<FAsyncCommandJob>>& Pair : AsyncJobs)
		{
			if (!Pair.Value.IsValid())
			{
				continue;
			}

			const FString& Status = Pair.Value->Status;
			if (Status == TEXT("queued")) ++QueuedTasks;
			else if (Status == TEXT("running")) ++RunningTasks;
			else if (Status == TEXT("completed")) ++CompletedTasks;
			else if (Status == TEXT("failed")) ++FailedTasks;
			else if (Status == TEXT("cancelled")) ++CancelledTasks;
		}
		TaskEventCount = AsyncJobEvents.Num();
		LatestTaskEventSequence = AsyncJobEventSequence;
	}

	TSharedPtr<FJsonObject> Data = BuildEditorIdentityJson();
	TArray<TSharedPtr<FJsonValue>> Scopes;
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("read")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("write")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("destructive")));
	Data->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - GStartTime);
	Data->SetNumberField(TEXT("active_client_connections"), ActiveClientConnections.GetValue());
	Data->SetNumberField(TEXT("command_count"), GetCommandDescriptors().Num());
	Data->SetArrayField(TEXT("supported_scopes"), Scopes);
	Data->SetNumberField(TEXT("tasks_queued"), QueuedTasks);
	Data->SetNumberField(TEXT("tasks_running"), RunningTasks);
	Data->SetNumberField(TEXT("tasks_completed"), CompletedTasks);
	Data->SetNumberField(TEXT("tasks_failed"), FailedTasks);
	Data->SetNumberField(TEXT("tasks_cancelled"), CancelledTasks);
	Data->SetNumberField(TEXT("task_event_count"), TaskEventCount);
	Data->SetNumberField(TEXT("latest_task_event_sequence"), static_cast<double>(LatestTaskEventSequence));
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleEditorIdentity()
{
	return CreateSuccessResponse(BuildEditorIdentityJson());
}

FString FAIExportTCPServer::HandleCommandManifestExport(TSharedPtr<FJsonObject> Params)
{
	FString OutputPath;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("output_path"), OutputPath);
	}

	if (OutputPath.IsEmpty())
	{
		OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AIManifests"), TEXT("CommonAIExport_CommandManifest.json"));
	}
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!FPaths::IsUnderDirectory(OutputPath, ProjectDir))
	{
		return CreateErrorResponse(TEXT("output_path must resolve under the project directory"));
	}

	FString OutputJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
	FJsonSerializer::Serialize(BuildCommandManifestJson().ToSharedRef(), Writer);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	if (!FFileHelper::SaveStringToFile(OutputJson, *OutputPath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to write command manifest: %s"), *OutputPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("output_path"), OutputPath);
	Data->SetNumberField(TEXT("command_count"), GetCommandDescriptors().Num());
	Data->SetStringField(TEXT("manifest_source"), TEXT("FAIExportTCPServer::GetCommandDescriptors"));
	Data->SetBoolField(TEXT("written"), true);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleProjectStatus()
{
	TSharedPtr<FJsonObject> Data = BuildEditorIdentityJson();
	Data->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - GStartTime);
	Data->SetNumberField(TEXT("command_count"), GetCommandDescriptors().Num());
	Data->SetBoolField(TEXT("port_file_exists"), IFileManager::Get().FileExists(*GetPortFilePath()));
	Data->SetBoolField(TEXT("project_file_exists"), IFileManager::Get().FileExists(*FPaths::GetProjectFilePath()));
	Data->SetBoolField(TEXT("diversion_repo"), IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectDir(), TEXT(".diversion"))));
	Data->SetBoolField(TEXT("git_repo"), IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectDir(), TEXT(".git"))));
	Data->SetBoolField(TEXT("vs_solution_exists"), IFileManager::Get().FileExists(*FPaths::Combine(FPaths::ProjectDir(), FString::Printf(TEXT("%s.sln"), FApp::GetProjectName()))));
	Data->SetStringField(TEXT("last_build_log"), FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("Logs"), TEXT("LastBuild.log"))));
	Data->SetBoolField(TEXT("last_build_log_exists"), IFileManager::Get().FileExists(*FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("Logs"), TEXT("LastBuild.log"))));

	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *FPaths::Combine(FPaths::ProjectLogDir(), TEXT("*.log")), true, false);
	Data->SetNumberField(TEXT("log_file_count"), LogFiles.Num());

	TSharedPtr<FJsonObject> EditorState = MakeShared<FJsonObject>();
	EditorState->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	EditorState->SetBoolField(TEXT("simulating"), GEditor && GEditor->bIsSimulatingInEditor);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	EditorState->SetStringField(TEXT("world_name"), World ? World->GetName() : TEXT(""));
	EditorState->SetStringField(TEXT("world_package"), (World && World->GetOutermost()) ? World->GetOutermost()->GetName() : TEXT(""));
	Data->SetObjectField(TEXT("editor_state"), EditorState);

	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleSourceControlStatus(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Path;
	bool bNoLimit = false;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetBoolField(TEXT("no_limit"), bNoLimit);
	}
	Path.TrimStartAndEndInline();

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		Arguments = TEXT("status");
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Path);
		}
		if (bNoLimit)
		{
			Arguments += TEXT(" --no-limit");
		}
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = TEXT("status --short");
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" -- ") + QuoteProcessArgument(Path);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("path"), Path);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleSourceControlLog(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Path;
	FString Since;
	FString Until;
	FString Ref;
	bool bOneline = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetStringField(TEXT("since"), Since);
		Params->TryGetStringField(TEXT("until"), Until);
		Params->TryGetStringField(TEXT("ref"), Ref);
		Params->TryGetBoolField(TEXT("oneline"), bOneline);
	}
	Path.TrimStartAndEndInline();
	Since.TrimStartAndEndInline();
	Until.TrimStartAndEndInline();
	Ref.TrimStartAndEndInline();
	const int32 Limit = ReadClampedIntField(Params, TEXT("limit"), 20, 1, 200);

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		if (!Ref.IsEmpty())
		{
			return CreateErrorResponse(TEXT("source_control_log ref is only supported for git"));
		}
		Arguments = FString::Printf(TEXT("log %s -n %d --date iso"), *(Path.IsEmpty() ? QuoteProcessArgument(TEXT(".")) : QuoteProcessArgument(Path)), Limit);
		if (bOneline)
		{
			Arguments += TEXT(" --oneline");
		}
		if (!Since.IsEmpty())
		{
			Arguments += TEXT(" --since ") + QuoteProcessArgument(Since);
		}
		if (!Until.IsEmpty())
		{
			Arguments += TEXT(" --until ") + QuoteProcessArgument(Until);
		}
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = FString::Printf(TEXT("log --max-count=%d --date=iso --color=never"), Limit);
		if (bOneline)
		{
			Arguments += TEXT(" --oneline");
		}
		if (!Since.IsEmpty())
		{
			Arguments += TEXT(" --since=") + QuoteProcessArgument(Since);
		}
		if (!Until.IsEmpty())
		{
			Arguments += TEXT(" --until=") + QuoteProcessArgument(Until);
		}
		if (!Ref.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Ref);
		}
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" -- ") + QuoteProcessArgument(Path);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("path"), Path);
	Data->SetNumberField(TEXT("limit"), Limit);
	Data->SetBoolField(TEXT("oneline"), bOneline);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleSourceControlShow(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Ref;
	bool bNameStatus = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("ref"), Ref);
		Params->TryGetBoolField(TEXT("name_status"), bNameStatus);
	}
	Ref.TrimStartAndEndInline();

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		Arguments = TEXT("show");
		if (!Ref.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Ref);
		}
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status");
		}
		Arguments += TEXT(" --date iso --color never");
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = TEXT("show --date=iso --color=never");
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status --format=fuller");
		}
		if (!Ref.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Ref);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("ref"), Ref);
	Data->SetBoolField(TEXT("name_status"), bNameStatus);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleSourceControlDiff(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Path;
	FString Base;
	FString Compare;
	bool bNameStatus = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetStringField(TEXT("base"), Base);
		Params->TryGetStringField(TEXT("compare"), Compare);
		Params->TryGetBoolField(TEXT("name_status"), bNameStatus);
	}
	Path.TrimStartAndEndInline();
	Base.TrimStartAndEndInline();
	Compare.TrimStartAndEndInline();

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		Arguments = TEXT("diff");
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Path);
		}
		if (!Base.IsEmpty())
		{
			Arguments += TEXT(" --base ") + QuoteProcessArgument(Base);
		}
		if (!Compare.IsEmpty())
		{
			Arguments += TEXT(" --compare ") + QuoteProcessArgument(Compare);
		}
		Arguments += TEXT(" --color never");
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status");
		}
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = TEXT("diff --color=never");
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status");
		}
		if (!Base.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Base);
		}
		if (!Compare.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Compare);
		}
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" -- ") + QuoteProcessArgument(Path);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("path"), Path);
	Data->SetStringField(TEXT("base"), Base);
	Data->SetStringField(TEXT("compare"), Compare);
	Data->SetBoolField(TEXT("name_status"), bNameStatus);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleTaskSubmit(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString CommandName;
	if (!Params->TryGetStringField(TEXT("command"), CommandName) && !Params->TryGetStringField(TEXT("command_name"), CommandName))
	{
		return CreateErrorResponse(TEXT("Missing 'command' parameter"));
	}

	const FCommandDescriptor* TargetDescriptor = FindCommandDescriptor(CommandName);
	if (!TargetDescriptor)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName));
	}

	if (CommandName.StartsWith(TEXT("task_")))
	{
		return CreateErrorResponse(TEXT("Async task commands cannot submit other async task commands"));
	}

	TSharedPtr<FJsonObject> CommandParams;
	if (Params->HasField(TEXT("params")))
	{
		const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
		if (!Params->TryGetObjectField(TEXT("params"), ParamsObject) || !ParamsObject || !ParamsObject->IsValid())
		{
			return CreateErrorResponse(TEXT("'params' must be a JSON object when provided"));
		}
		CommandParams = *ParamsObject;
	}

	if (TargetDescriptor->bRequiresParams && !CommandParams.IsValid())
	{
		return CreateErrorResponse(FString::Printf(TEXT("Command '%s' requires a 'params' object"), *CommandName));
	}

	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* MetaObject = nullptr;
	if (Params->TryGetObjectField(TEXT("meta"), MetaObject) && MetaObject && MetaObject->IsValid())
	{
		Meta = *MetaObject;
	}

	FString ScopeOverride;
	if (Params->TryGetStringField(TEXT("scope"), ScopeOverride))
	{
		Meta->SetStringField(TEXT("scope"), ScopeOverride);
	}

	bool bDryRun = false;
	if (Params->TryGetBoolField(TEXT("dry_run"), bDryRun))
	{
		Meta->SetBoolField(TEXT("dry_run"), bDryRun);
	}

	TSharedPtr<FJsonObject> SyntheticRoot = MakeShared<FJsonObject>();
	SyntheticRoot->SetStringField(TEXT("type"), CommandName);
	SyntheticRoot->SetObjectField(TEXT("meta"), Meta);

	const FAICommandContext TargetContext = BuildCommandContext(SyntheticRoot, *TargetDescriptor);
	FString ScopeError;
	if (!ValidateCommandScope(*TargetDescriptor, TargetContext, ScopeError))
	{
		return CreateErrorResponse(ScopeError);
	}

	if (TargetContext.bDryRun && TargetDescriptor->bMutating)
	{
		return CreateDryRunResponse(*TargetDescriptor, TargetContext);
	}

	const FString TaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	TSharedPtr<FAsyncCommandJob> Job = MakeShared<FAsyncCommandJob>();
	Job->TaskId = TaskId;
	Job->CommandName = CommandName;
	Job->Status = TEXT("queued");
	Job->SubmittedAt = FDateTime::UtcNow().ToIso8601();

	{
		FScopeLock Lock(&AsyncJobsCriticalSection);
		AsyncJobs.Add(TaskId, Job);
		AppendTaskEventLocked(*Job, TEXT("queued"), TEXT("Task queued"));
	}

	Async(EAsyncExecution::ThreadPool, [this, TaskId, CommandName, TargetDescriptor, CommandParams]()
	{
		{
			FScopeLock Lock(&AsyncJobsCriticalSection);
			TSharedPtr<FAsyncCommandJob>* FoundJob = AsyncJobs.Find(TaskId);
			if (!FoundJob || !FoundJob->IsValid())
			{
				return;
			}
			if ((*FoundJob)->bCancelRequested)
			{
				(*FoundJob)->Status = TEXT("cancelled");
				(*FoundJob)->FinishedAt = FDateTime::UtcNow().ToIso8601();
				AppendTaskEventLocked(*FoundJob->Get(), TEXT("cancelled"), TEXT("Task cancelled before start"));
				return;
			}
			(*FoundJob)->Status = TEXT("running");
			(*FoundJob)->StartedAt = FDateTime::UtcNow().ToIso8601();
			AppendTaskEventLocked(*FoundJob->Get(), TEXT("running"), TEXT("Task started"));
		}

		const FString Response = DispatchCommand(*TargetDescriptor, CommandParams);
		bool bSuccess = false;
		FString ErrorMessage;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);
		TSharedPtr<FJsonObject> ParsedResponse;
		if (FJsonSerializer::Deserialize(Reader, ParsedResponse) && ParsedResponse.IsValid())
		{
			ParsedResponse->TryGetBoolField(TEXT("success"), bSuccess);
			ParsedResponse->TryGetStringField(TEXT("error"), ErrorMessage);
		}

		{
			FScopeLock Lock(&AsyncJobsCriticalSection);
			TSharedPtr<FAsyncCommandJob>* FoundJob = AsyncJobs.Find(TaskId);
			if (!FoundJob || !FoundJob->IsValid())
			{
				return;
			}
			(*FoundJob)->ResultJson = Response;
			(*FoundJob)->FinishedAt = FDateTime::UtcNow().ToIso8601();
			(*FoundJob)->Status = bSuccess ? TEXT("completed") : TEXT("failed");
			(*FoundJob)->ErrorMessage = ErrorMessage;
			if ((*FoundJob)->bCancelRequested && !bSuccess)
			{
				(*FoundJob)->Status = TEXT("cancelled");
			}
			AppendTaskEventLocked(
				*FoundJob->Get(),
				(*FoundJob)->Status,
				bSuccess ? TEXT("Task completed") : (ErrorMessage.IsEmpty() ? TEXT("Task failed") : ErrorMessage));
		}
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	Data->SetStringField(TEXT("command"), CommandName);
	Data->SetStringField(TEXT("status"), TEXT("queued"));
	Data->SetBoolField(TEXT("async_candidate"), TargetDescriptor->bAsyncCandidate);
	Data->SetNumberField(TEXT("timeout_seconds"), TargetDescriptor->TimeoutSeconds);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleTaskStatus(TSharedPtr<FJsonObject> Params)
{
	FString TaskId;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
	}

	if (!TaskId.IsEmpty())
	{
		FAsyncCommandJob Job;
		if (!TryCopyTask(TaskId, Job))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId));
		}
		return CreateSuccessResponse(BuildTaskJson(Job, false));
	}

	TArray<TSharedPtr<FJsonValue>> Tasks;
	{
		FScopeLock Lock(&AsyncJobsCriticalSection);
		for (const TPair<FString, TSharedPtr<FAsyncCommandJob>>& Pair : AsyncJobs)
		{
			if (Pair.Value.IsValid())
			{
				Tasks.Add(MakeShared<FJsonValueObject>(BuildTaskJson(*Pair.Value, false)));
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tasks"), Tasks);
	Data->SetNumberField(TEXT("count"), Tasks.Num());
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleTaskResult(TSharedPtr<FJsonObject> Params)
{
	FString TaskId;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
	}

	if (!TaskId.IsEmpty())
	{
		FAsyncCommandJob Job;
		if (!TryCopyTask(TaskId, Job))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId));
		}
		return CreateSuccessResponse(BuildTaskJson(Job, true));
	}

	TArray<TSharedPtr<FJsonValue>> Tasks;
	{
		FScopeLock Lock(&AsyncJobsCriticalSection);
		for (const TPair<FString, TSharedPtr<FAsyncCommandJob>>& Pair : AsyncJobs)
		{
			if (Pair.Value.IsValid() && (Pair.Value->Status == TEXT("completed") || Pair.Value->Status == TEXT("failed") || Pair.Value->Status == TEXT("cancelled")))
			{
				Tasks.Add(MakeShared<FJsonValueObject>(BuildTaskJson(*Pair.Value, false)));
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("completed_tasks"), Tasks);
	Data->SetNumberField(TEXT("count"), Tasks.Num());
	Data->SetStringField(TEXT("message"), TEXT("Pass task_id to include the stored command response."));
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleTaskEvents(TSharedPtr<FJsonObject> Params)
{
	return CreateSuccessResponse(BuildTaskEventsJson(Params));
}

FString FAIExportTCPServer::HandleTaskEventsWait(TSharedPtr<FJsonObject> Params)
{
	return CreateSuccessResponse(BuildTaskEventsWaitJson(Params));
}

FString FAIExportTCPServer::HandleTaskCancel(TSharedPtr<FJsonObject> Params)
{
	FString TaskId;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
	}

	if (TaskId.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> CancellableTasks;
		{
			FScopeLock Lock(&AsyncJobsCriticalSection);
			for (const TPair<FString, TSharedPtr<FAsyncCommandJob>>& Pair : AsyncJobs)
			{
				if (Pair.Value.IsValid() && (Pair.Value->Status == TEXT("queued") || Pair.Value->Status == TEXT("running")))
				{
					CancellableTasks.Add(MakeShared<FJsonValueObject>(BuildTaskJson(*Pair.Value, false)));
				}
			}
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("cancellable_tasks"), CancellableTasks);
		Data->SetNumberField(TEXT("count"), CancellableTasks.Num());
		Data->SetStringField(TEXT("message"), TEXT("Pass task_id to request cancellation."));
		return CreateSuccessResponse(Data);
	}

	FAsyncCommandJob JobCopy;
	{
		FScopeLock Lock(&AsyncJobsCriticalSection);
		TSharedPtr<FAsyncCommandJob>* FoundJob = AsyncJobs.Find(TaskId);
		if (!FoundJob || !FoundJob->IsValid())
		{
			return CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId));
		}

		(*FoundJob)->bCancelRequested = true;
		if ((*FoundJob)->Status == TEXT("queued"))
		{
			(*FoundJob)->Status = TEXT("cancelled");
			(*FoundJob)->FinishedAt = FDateTime::UtcNow().ToIso8601();
			AppendTaskEventLocked(*FoundJob->Get(), TEXT("cancelled"), TEXT("Task cancelled before start"));
		}
		else if ((*FoundJob)->Status == TEXT("running"))
		{
			AppendTaskEventLocked(*FoundJob->Get(), TEXT("cancel_requested"), TEXT("Cancellation requested"));
		}
		JobCopy = *FoundJob->Get();
	}

	TSharedPtr<FJsonObject> Data = BuildTaskJson(JobCopy, false);
	Data->SetBoolField(TEXT("cancel_requested"), true);
	Data->SetStringField(TEXT("message"), TEXT("Cancellation is cooperative; already-running UE work may finish before the task observes cancellation."));
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleExportWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		FAIExportResult Result = UAIExportFunctionLibrary::ExportWidgetBlueprintByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}

FString FAIExportTCPServer::HandleExportBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		// Use ExportAssetByPath to support all asset types (Blueprint, Audio, Input, etc.)
		FAIExportResult Result = UAIExportFunctionLibrary::ExportAssetByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}

FString FAIExportTCPServer::HandleListSupportedTypes()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> Types;
	// Blueprints
	Types.Add(MakeShared<FJsonValueString>(TEXT("WidgetBlueprint")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("Blueprint")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("AnimBlueprint")));
	// Data
	Types.Add(MakeShared<FJsonValueString>(TEXT("DataAsset")));
	// Input
	Types.Add(MakeShared<FJsonValueString>(TEXT("InputAction")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("InputMappingContext")));
	// Audio Foundation
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundClass")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundSubmix")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundConcurrency")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundAttenuation")));
	// Audio Modulation
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundControlBus")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundControlBusMix")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundModulationPatch")));
	// Physics
	Types.Add(MakeShared<FJsonValueString>(TEXT("PhysicalMaterial")));

	Data->SetArrayField(TEXT("types"), Types);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	return OutputString;
}

FString FAIExportTCPServer::CreateSuccessResponse(TSharedPtr<FJsonObject> Data)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), true);

	if (Data.IsValid())
	{
		Response->SetObjectField(TEXT("data"), Data);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	return OutputString;
}

namespace
{
	static UWorld* GetAIEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static void AddVectorJson(TSharedPtr<FJsonObject> Obj, const TCHAR* FieldName, const FVector& Value)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("x"), Value.X);
		Json->SetNumberField(TEXT("y"), Value.Y);
		Json->SetNumberField(TEXT("z"), Value.Z);
		Obj->SetObjectField(FieldName, Json);
	}

	static void AddRotatorJson(TSharedPtr<FJsonObject> Obj, const TCHAR* FieldName, const FRotator& Value)
	{
		TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("pitch"), Value.Pitch);
		Json->SetNumberField(TEXT("yaw"), Value.Yaw);
		Json->SetNumberField(TEXT("roll"), Value.Roll);
		Obj->SetObjectField(FieldName, Json);
	}

	static FVector ReadVectorField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FVector& DefaultValue)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Params.IsValid() || !Params->TryGetObjectField(FieldName, Obj) || !Obj || !Obj->IsValid())
		{
			return DefaultValue;
		}

		FVector Result = DefaultValue;
		double Value = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("x"), Value)) Result.X = Value;
		if ((*Obj)->TryGetNumberField(TEXT("y"), Value)) Result.Y = Value;
		if ((*Obj)->TryGetNumberField(TEXT("z"), Value)) Result.Z = Value;
		return Result;
	}

	static FRotator ReadRotatorField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FRotator& DefaultValue)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Params.IsValid() || !Params->TryGetObjectField(FieldName, Obj) || !Obj || !Obj->IsValid())
		{
			return DefaultValue;
		}

		FRotator Result = DefaultValue;
		double Value = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("pitch"), Value)) Result.Pitch = Value;
		if ((*Obj)->TryGetNumberField(TEXT("yaw"), Value)) Result.Yaw = Value;
		if ((*Obj)->TryGetNumberField(TEXT("roll"), Value)) Result.Roll = Value;
		return Result;
	}

	static TSharedPtr<FJsonObject> BuildActorJson(AActor* Actor)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		if (!Actor)
		{
			return Data;
		}

		Data->SetStringField(TEXT("name"), Actor->GetName());
		Data->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Data->SetStringField(TEXT("path"), Actor->GetPathName());
		Data->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
		Data->SetBoolField(TEXT("hidden"), Actor->IsHidden());
		AddVectorJson(Data, TEXT("location"), Actor->GetActorLocation());
		AddRotatorJson(Data, TEXT("rotation"), Actor->GetActorRotation());
		AddVectorJson(Data, TEXT("scale"), Actor->GetActorScale3D());
		return Data;
	}

	static TSharedPtr<FJsonObject> BuildAssetDataJson(const FAssetData& AssetData)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		if (!AssetData.IsValid())
		{
			return Data;
		}

		Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		Data->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
		Data->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		Data->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
		Data->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
		Data->SetBoolField(TEXT("is_redirector"), AssetData.AssetClassPath.ToString().Contains(TEXT("ObjectRedirector")));
		return Data;
	}

	static AActor* FindActorForAI(UWorld* World, const FString& ActorPath, const FString& ActorLabel, const FString& ActorName)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			if (!ActorPath.IsEmpty() && Actor->GetPathName() == ActorPath) return Actor;
			if (!ActorLabel.IsEmpty() && Actor->GetActorLabel() == ActorLabel) return Actor;
			if (!ActorName.IsEmpty() && Actor->GetName() == ActorName) return Actor;
		}
		return nullptr;
	}
}

FString FAIExportTCPServer::HandleEditorWorldInfo()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		int32 ActorCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			++ActorCount;
		}

		TArray<TSharedPtr<FJsonValue>> Levels;
		for (ULevel* Level : World->GetLevels())
		{
			if (!Level)
			{
				continue;
			}
			TSharedPtr<FJsonObject> LevelJson = MakeShared<FJsonObject>();
			LevelJson->SetStringField(TEXT("name"), Level->GetName());
			LevelJson->SetStringField(TEXT("package_name"), Level->GetOutermost() ? Level->GetOutermost()->GetName() : TEXT(""));
			LevelJson->SetBoolField(TEXT("is_persistent"), Level == World->PersistentLevel);
			Levels.Add(MakeShared<FJsonValueObject>(LevelJson));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("world_name"), World->GetName());
		Data->SetStringField(TEXT("package_name"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
		Data->SetStringField(TEXT("map_filename"), FEditorFileUtils::GetFilename(World));
		Data->SetStringField(TEXT("world_type"), LexToString(World->WorldType));
		Data->SetNumberField(TEXT("actor_count"), ActorCount);
		Data->SetNumberField(TEXT("level_count"), Levels.Num());
		Data->SetArrayField(TEXT("levels"), Levels);
		Data->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Editor world info timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeWorldInfo(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, Promise, this]()
	{
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			return;
		}

		Promise->SetValue(CreateSuccessResponse(BuildRuntimeWorldJson(World, WorldSource)));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime world info timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimePlayerList(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, Promise, this]()
	{
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Controllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ControllerJson = MakeShared<FJsonObject>();
			ControllerJson->SetStringField(TEXT("name"), Controller->GetName());
			ControllerJson->SetStringField(TEXT("path"), Controller->GetPathName());
			ControllerJson->SetStringField(TEXT("class"), Controller->GetClass() ? Controller->GetClass()->GetPathName() : TEXT(""));
			ControllerJson->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
			if (Controller->PlayerState)
			{
				ControllerJson->SetStringField(TEXT("player_state_name"), Controller->PlayerState->GetPlayerName());
				ControllerJson->SetStringField(TEXT("player_state_class"), Controller->PlayerState->GetClass() ? Controller->PlayerState->GetClass()->GetPathName() : TEXT(""));
			}
			if (APawn* Pawn = Controller->GetPawn())
			{
				ControllerJson->SetObjectField(TEXT("pawn"), BuildRuntimeActorJson(Pawn));
			}
			Controllers.Add(MakeShared<FJsonValueObject>(ControllerJson));
		}

		TArray<TSharedPtr<FJsonValue>> LocalPlayers;
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (!LocalPlayer)
				{
					continue;
				}

				TSharedPtr<FJsonObject> LocalPlayerJson = MakeShared<FJsonObject>();
				LocalPlayerJson->SetStringField(TEXT("name"), LocalPlayer->GetName());
				LocalPlayerJson->SetStringField(TEXT("class"), LocalPlayer->GetClass() ? LocalPlayer->GetClass()->GetPathName() : TEXT(""));
				if (APlayerController* Controller = LocalPlayer->GetPlayerController(World))
				{
					LocalPlayerJson->SetStringField(TEXT("player_controller_path"), Controller->GetPathName());
				}
				LocalPlayers.Add(MakeShared<FJsonValueObject>(LocalPlayerJson));
			}
		}

		TSharedPtr<FJsonObject> Data = BuildRuntimeWorldJson(World, WorldSource);
		Data->SetArrayField(TEXT("controllers"), Controllers);
		Data->SetArrayField(TEXT("local_players"), LocalPlayers);
		Data->SetNumberField(TEXT("controller_count"), Controllers.Num());
		Data->SetNumberField(TEXT("local_player_count"), LocalPlayers.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime player list timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeComponentList(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ActorClassFilter;
	FString ComponentClassFilter;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("actor_class_filter"), ActorClassFilter);
		Params->TryGetStringField(TEXT("component_class_filter"), ComponentClassFilter);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ActorClassFilter.TrimStartAndEndInline();
	ComponentClassFilter.TrimStartAndEndInline();
	const int32 Limit = ReadClampedIntField(Params, TEXT("limit"), 500, 1, 5000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, ActorPath, ActorLabel, ActorName, NameFilter, ActorClassFilter, ComponentClassFilter, Limit, Promise, this]()
	{
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			return;
		}

		TArray<AActor*> ActorsToInspect;
		const bool bHasSpecificActor = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		if (bHasSpecificActor)
		{
			AActor* Actor = FindRuntimeActorForAI(World, ActorPath, ActorLabel, ActorName);
			if (!Actor)
			{
				Promise->SetValue(CreateErrorResponse(TEXT("Actor not found")));
				return;
			}
			ActorsToInspect.Add(Actor);
		}
		else
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor)
				{
					continue;
				}

				const FString Label = Actor->GetActorLabel();
				const FString Name = Actor->GetName();
				const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
				if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
				{
					continue;
				}
				if (!ActorClassFilter.IsEmpty() && !ClassPath.Contains(ActorClassFilter))
				{
					continue;
				}
				ActorsToInspect.Add(Actor);
			}
		}

		TArray<TSharedPtr<FJsonValue>> ComponentsJson;
		int32 MatchedComponentCount = 0;
		int32 InspectedActorCount = 0;
		for (AActor* Actor : ActorsToInspect)
		{
			if (!Actor)
			{
				continue;
			}
			++InspectedActorCount;

			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}

				const FString ComponentClassPath = Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT("");
				if (!ComponentClassFilter.IsEmpty() && !ComponentClassPath.Contains(ComponentClassFilter))
				{
					continue;
				}

				++MatchedComponentCount;
				if (ComponentsJson.Num() < Limit)
				{
					ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeComponentJson(Component)));
				}
			}
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetStringField(TEXT("world_name"), World->GetName());
		Data->SetNumberField(TEXT("inspected_actor_count"), InspectedActorCount);
		Data->SetNumberField(TEXT("matched_component_count"), MatchedComponentCount);
		Data->SetNumberField(TEXT("count"), ComponentsJson.Num());
		Data->SetBoolField(TEXT("truncated"), MatchedComponentCount > ComponentsJson.Num());
		Data->SetArrayField(TEXT("components"), ComponentsJson);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime component list timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	bool bIncludeComponents = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, ActorPath, ActorLabel, ActorName, bIncludeComponents, ComponentLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; diagnostics reflect the editor world")));
		}

		TSharedPtr<FJsonObject> WorldJson = BuildRuntimeWorldJson(World, WorldSource);
		Data->SetObjectField(TEXT("world"), WorldJson);

		TArray<TSharedPtr<FJsonValue>> Controllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ControllerJson = MakeShared<FJsonObject>();
			ControllerJson->SetStringField(TEXT("name"), Controller->GetName());
			ControllerJson->SetStringField(TEXT("path"), Controller->GetPathName());
			ControllerJson->SetStringField(TEXT("class"), Controller->GetClass() ? Controller->GetClass()->GetPathName() : TEXT(""));
			ControllerJson->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
			if (APawn* Pawn = Controller->GetPawn())
			{
				ControllerJson->SetObjectField(TEXT("pawn"), BuildRuntimeActorJson(Pawn));
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("PlayerController '%s' has no possessed pawn"), *Controller->GetName())));
			}
			Controllers.Add(MakeShared<FJsonValueObject>(ControllerJson));
		}

		TSharedPtr<FJsonObject> PlayersJson = MakeShared<FJsonObject>();
		PlayersJson->SetArrayField(TEXT("controllers"), Controllers);
		PlayersJson->SetNumberField(TEXT("controller_count"), Controllers.Num());
		if (Controllers.Num() == 0 && WorldSource == TEXT("pie"))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE world has no player controllers")));
		}
		Data->SetObjectField(TEXT("players"), PlayersJson);

		const bool bActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		Data->SetBoolField(TEXT("selected_actor_requested"), bActorRequested);
		if (bActorRequested)
		{
			AActor* Actor = FindRuntimeActorForAI(World, ActorPath, ActorLabel, ActorName);
			Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
			if (Actor)
			{
				TSharedPtr<FJsonObject> ActorJson = BuildRuntimeActorJson(Actor);
				if (bIncludeComponents)
				{
					TArray<UActorComponent*> Components;
					Actor->GetComponents(Components);
					TArray<TSharedPtr<FJsonValue>> ComponentsJson;
					for (UActorComponent* Component : Components)
					{
						if (!Component)
						{
							continue;
						}
						if (ComponentsJson.Num() < ComponentLimit)
						{
							ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeComponentJson(Component)));
						}
					}
					ActorJson->SetNumberField(TEXT("component_count"), Components.Num());
					ActorJson->SetNumberField(TEXT("returned_component_count"), ComponentsJson.Num());
					ActorJson->SetBoolField(TEXT("components_truncated"), Components.Num() > ComponentsJson.Num());
					ActorJson->SetArrayField(TEXT("components"), ComponentsJson);
				}
				Data->SetObjectField(TEXT("selected_actor"), ActorJson);
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
			}
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeInputRouting(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; input routing reflects the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));

		TArray<TSharedPtr<FJsonValue>> Controllers;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (!Controller)
			{
				continue;
			}
			Controllers.Add(MakeShared<FJsonValueObject>(BuildRuntimeControllerInputRouteJson(Controller)));
		}
		Data->SetArrayField(TEXT("controllers"), Controllers);
		Data->SetNumberField(TEXT("controller_count"), Controllers.Num());
		if (Controllers.Num() == 0 && WorldSource == TEXT("pie"))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE world has no player controllers for input routing")));
		}

		TArray<TSharedPtr<FJsonValue>> LocalPlayers;
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (!LocalPlayer)
				{
					continue;
				}

				TSharedPtr<FJsonObject> LocalPlayerJson = MakeShared<FJsonObject>();
				LocalPlayerJson->SetStringField(TEXT("name"), LocalPlayer->GetName());
				LocalPlayerJson->SetStringField(TEXT("path"), LocalPlayer->GetPathName());
				LocalPlayerJson->SetStringField(TEXT("class"), LocalPlayer->GetClass() ? LocalPlayer->GetClass()->GetPathName() : TEXT(""));
				LocalPlayerJson->SetNumberField(TEXT("controller_id"), LocalPlayer->GetControllerId());
				LocalPlayerJson->SetObjectField(TEXT("common_input"), BuildRuntimeCommonInputJson(LocalPlayer));
				LocalPlayerJson->SetObjectField(TEXT("enhanced_input"), BuildRuntimeEnhancedInputSubsystemJson(LocalPlayer));

				if (APlayerController* Controller = LocalPlayer->GetPlayerController(World))
				{
					LocalPlayerJson->SetStringField(TEXT("player_controller_path"), Controller->GetPathName());
					LocalPlayerJson->SetObjectField(TEXT("player_input"), BuildRuntimePlayerInputJson(Controller));
				}
				else
				{
					LocalPlayerJson->SetBoolField(TEXT("player_controller_present"), false);
				}

				LocalPlayers.Add(MakeShared<FJsonValueObject>(LocalPlayerJson));
			}
		}
		Data->SetArrayField(TEXT("local_players"), LocalPlayers);
		Data->SetNumberField(TEXT("local_player_count"), LocalPlayers.Num());
		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime input routing timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeCommonUIDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString NameFilter;
	FString ClassFilter;
	bool bIncludeWidgets = true;
	bool bIncludeBindings = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("include_widgets"), bIncludeWidgets);
		Params->TryGetBoolField(TEXT("include_bindings"), bIncludeBindings);
	}
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	const int32 LocalPlayerLimit = ReadClampedIntField(Params, TEXT("local_player_limit"), 8, 1, 32);
	const int32 WidgetLimit = ReadClampedIntField(Params, TEXT("widget_limit"), 100, 0, 1000);
	const int32 ContainerLimit = ReadClampedIntField(Params, TEXT("container_limit"), 100, 0, 1000);
	const int32 BindingLimit = ReadClampedIntField(Params, TEXT("binding_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, NameFilter, ClassFilter, bIncludeWidgets, bIncludeBindings, LocalPlayerLimit, WidgetLimit, ContainerLimit, BindingLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; CommonUI diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
		Data->SetBoolField(TEXT("include_widgets"), bIncludeWidgets);
		Data->SetBoolField(TEXT("include_bindings"), bIncludeBindings);
		Data->SetNumberField(TEXT("local_player_limit"), LocalPlayerLimit);
		Data->SetNumberField(TEXT("widget_limit"), WidgetLimit);
		Data->SetNumberField(TEXT("container_limit"), ContainerLimit);
		Data->SetNumberField(TEXT("binding_limit"), BindingLimit);

		TArray<TSharedPtr<FJsonValue>> LocalPlayersJson;
		int32 MatchedLocalPlayerCount = 0;
		int32 ActionRouterCount = 0;
		int32 ActiveBindingCount = 0;
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
			{
				if (!LocalPlayer)
				{
					continue;
				}

				++MatchedLocalPlayerCount;
				if (LocalPlayersJson.Num() >= LocalPlayerLimit)
				{
					continue;
				}

				TSharedPtr<FJsonObject> LocalPlayerJson = MakeShared<FJsonObject>();
				LocalPlayerJson->SetStringField(TEXT("name"), LocalPlayer->GetName());
				LocalPlayerJson->SetStringField(TEXT("path"), LocalPlayer->GetPathName());
				LocalPlayerJson->SetStringField(TEXT("class"), LocalPlayer->GetClass() ? LocalPlayer->GetClass()->GetPathName() : TEXT(""));
				LocalPlayerJson->SetNumberField(TEXT("controller_id"), LocalPlayer->GetControllerId());
				LocalPlayerJson->SetObjectField(TEXT("common_input"), BuildRuntimeCommonInputJson(LocalPlayer));
				if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
				{
					++ActionRouterCount;
					const TArray<FUIActionBindingHandle> Bindings = Router->GatherActiveBindings();
					ActiveBindingCount += Bindings.Num();
					LocalPlayerJson->SetObjectField(TEXT("action_router"), BuildCommonUIActionRouterJson(Router, bIncludeBindings, BindingLimit));
				}
				else
				{
					TSharedPtr<FJsonObject> RouterJson = MakeShared<FJsonObject>();
					RouterJson->SetBoolField(TEXT("present"), false);
					LocalPlayerJson->SetObjectField(TEXT("action_router"), RouterJson);
				}
				LocalPlayersJson.Add(MakeShared<FJsonValueObject>(LocalPlayerJson));
			}
		}
		Data->SetNumberField(TEXT("matched_local_player_count"), MatchedLocalPlayerCount);
		Data->SetNumberField(TEXT("returned_local_player_count"), LocalPlayersJson.Num());
		Data->SetBoolField(TEXT("local_players_truncated"), MatchedLocalPlayerCount > LocalPlayersJson.Num());
		Data->SetNumberField(TEXT("action_router_count"), ActionRouterCount);
		Data->SetNumberField(TEXT("active_binding_count"), ActiveBindingCount);
		Data->SetArrayField(TEXT("local_players"), LocalPlayersJson);

		auto WidgetMatchesFilters = [&NameFilter, &ClassFilter](const UWidget* Widget)
		{
			if (!Widget)
			{
				return false;
			}
			const FString Name = Widget->GetName();
			const FString ClassPath = Widget->GetClass() ? Widget->GetClass()->GetPathName() : TEXT("");
			if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter))
			{
				return false;
			}
			if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter))
			{
				return false;
			}
			return true;
		};

		int32 MatchedActivatableWidgetCount = 0;
		int32 ActivatedWidgetCount = 0;
		int32 ModalWidgetCount = 0;
		int32 FocusSupportingWidgetCount = 0;
		TArray<TSharedPtr<FJsonValue>> WidgetsJson;
		for (TObjectIterator<UCommonActivatableWidget> It; It; ++It)
		{
			UCommonActivatableWidget* Widget = *It;
			if (!Widget || Widget->GetWorld() != World || !WidgetMatchesFilters(Widget))
			{
				continue;
			}

			++MatchedActivatableWidgetCount;
			if (Widget->IsActivated())
			{
				++ActivatedWidgetCount;
			}
			if (Widget->IsModal())
			{
				++ModalWidgetCount;
			}
			if (Widget->SupportsActivationFocus())
			{
				++FocusSupportingWidgetCount;
			}
			if (bIncludeWidgets && WidgetsJson.Num() < WidgetLimit)
			{
				WidgetsJson.Add(MakeShared<FJsonValueObject>(BuildCommonActivatableWidgetJson(Widget)));
			}
		}
		Data->SetNumberField(TEXT("matched_activatable_widget_count"), MatchedActivatableWidgetCount);
		Data->SetNumberField(TEXT("activated_widget_count"), ActivatedWidgetCount);
		Data->SetNumberField(TEXT("modal_widget_count"), ModalWidgetCount);
		Data->SetNumberField(TEXT("focus_supporting_widget_count"), FocusSupportingWidgetCount);
		if (bIncludeWidgets)
		{
			Data->SetNumberField(TEXT("returned_widget_count"), WidgetsJson.Num());
			Data->SetBoolField(TEXT("widgets_truncated"), MatchedActivatableWidgetCount > WidgetsJson.Num());
			Data->SetArrayField(TEXT("activatable_widgets"), WidgetsJson);
		}

		int32 MatchedContainerCount = 0;
		int32 NonEmptyContainerCount = 0;
		TArray<TSharedPtr<FJsonValue>> ContainersJson;
		for (TObjectIterator<UCommonActivatableWidgetContainerBase> It; It; ++It)
		{
			UCommonActivatableWidgetContainerBase* Container = *It;
			if (!Container || Container->GetWorld() != World || !WidgetMatchesFilters(Container))
			{
				continue;
			}

			++MatchedContainerCount;
			if (Container->GetNumWidgets() > 0)
			{
				++NonEmptyContainerCount;
			}
			if (bIncludeWidgets && ContainersJson.Num() < ContainerLimit)
			{
				ContainersJson.Add(MakeShared<FJsonValueObject>(BuildCommonActivatableContainerJson(Container, WidgetLimit)));
			}
		}
		Data->SetNumberField(TEXT("matched_container_count"), MatchedContainerCount);
		Data->SetNumberField(TEXT("non_empty_container_count"), NonEmptyContainerCount);
		if (bIncludeWidgets)
		{
			Data->SetNumberField(TEXT("returned_container_count"), ContainersJson.Num());
			Data->SetBoolField(TEXT("containers_truncated"), MatchedContainerCount > ContainersJson.Num());
			Data->SetArrayField(TEXT("activatable_containers"), ContainersJson);
		}

		if (MatchedLocalPlayerCount == 0 && WorldSource == TEXT("pie"))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE world has no local players for CommonUI routing")));
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime CommonUI diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeAssetStreamingDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	bool bIncludeLevels = true;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_levels"), bIncludeLevels);
	}
	const int32 LevelLimit = ReadClampedIntField(Params, TEXT("level_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, bIncludeLevels, LevelLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
		Data->SetObjectField(TEXT("streaming_manager"), BuildStreamingManagerJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; asset streaming diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
		Data->SetBoolField(TEXT("include_levels"), bIncludeLevels);
		Data->SetNumberField(TEXT("level_limit"), LevelLimit);

		const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();
		int32 LoadedStreamingLevelCount = 0;
		int32 VisibleStreamingLevelCount = 0;
		int32 PendingLoadRequestCount = 0;
		int32 RequestingUnloadAndRemovalCount = 0;
		for (ULevelStreaming* StreamingLevel : StreamingLevels)
		{
			if (!StreamingLevel)
			{
				continue;
			}
			if (StreamingLevel->HasLoadedLevel())
			{
				++LoadedStreamingLevelCount;
			}
			if (StreamingLevel->GetLevelStreamingState() == ELevelStreamingState::LoadedVisible || StreamingLevel->GetLevelStreamingState() == ELevelStreamingState::MakingVisible)
			{
				++VisibleStreamingLevelCount;
			}
			if (StreamingLevel->HasLoadRequestPending())
			{
				++PendingLoadRequestCount;
			}
			if (StreamingLevel->GetIsRequestingUnloadAndRemoval())
			{
				++RequestingUnloadAndRemovalCount;
			}
		}

		Data->SetNumberField(TEXT("streaming_level_count"), StreamingLevels.Num());
		Data->SetNumberField(TEXT("loaded_streaming_level_count"), LoadedStreamingLevelCount);
		Data->SetNumberField(TEXT("visible_streaming_level_count"), VisibleStreamingLevelCount);
		Data->SetNumberField(TEXT("pending_load_request_count"), PendingLoadRequestCount);
		Data->SetNumberField(TEXT("requesting_unload_and_removal_count"), RequestingUnloadAndRemovalCount);

		if (bIncludeLevels)
		{
			TArray<TSharedPtr<FJsonValue>> LevelsJson;
			for (ULevelStreaming* StreamingLevel : StreamingLevels)
			{
				if (!StreamingLevel)
				{
					continue;
				}
				if (LevelsJson.Num() >= LevelLimit)
				{
					break;
				}
				LevelsJson.Add(MakeShared<FJsonValueObject>(BuildLevelStreamingJson(StreamingLevel)));
			}
			Data->SetNumberField(TEXT("returned_streaming_level_count"), LevelsJson.Num());
			Data->SetBoolField(TEXT("streaming_levels_truncated"), StreamingLevels.Num() > LevelsJson.Num());
			Data->SetArrayField(TEXT("streaming_levels"), LevelsJson);
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime asset streaming diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeAsyncLoadDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	bool bIncludePackageProbes = true;
	bool bIncludeStreamableHandles = true;
	bool bIncludeStreamingManager = true;
	TArray<FString> ProbeInputs;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_package_probes"), bIncludePackageProbes);
		Params->TryGetBoolField(TEXT("include_streamable_handles"), bIncludeStreamableHandles);
		Params->TryGetBoolField(TEXT("include_streaming_manager"), bIncludeStreamingManager);
		AppendStringArrayField(Params, TEXT("asset_paths"), ProbeInputs);
		AppendStringArrayField(Params, TEXT("package_names"), ProbeInputs);

		FString SingleAssetPath;
		if (Params->TryGetStringField(TEXT("asset_path"), SingleAssetPath))
		{
			SingleAssetPath.TrimStartAndEndInline();
			if (!SingleAssetPath.IsEmpty())
			{
				ProbeInputs.Add(SingleAssetPath);
			}
		}
		FString SinglePackageName;
		if (Params->TryGetStringField(TEXT("package_name"), SinglePackageName))
		{
			SinglePackageName.TrimStartAndEndInline();
			if (!SinglePackageName.IsEmpty())
			{
				ProbeInputs.Add(SinglePackageName);
			}
		}
	}
	const int32 ProbeLimit = ReadClampedIntField(Params, TEXT("probe_limit"), 50, 0, 500);
	const int32 AssetDataLimit = ReadClampedIntField(Params, TEXT("asset_data_limit"), 10, 0, 100);
	const int32 HandleLimit = ReadClampedIntField(Params, TEXT("handle_limit"), 10, 0, 100);
	const int32 RequestedAssetLimit = ReadClampedIntField(Params, TEXT("requested_asset_limit"), 10, 0, 100);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, ProbeInputs, bIncludePackageProbes, bIncludeStreamableHandles, bIncludeStreamingManager, ProbeLimit, AssetDataLimit, HandleLimit, RequestedAssetLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
		Data->SetObjectField(TEXT("async_loading"), BuildRuntimeAsyncLoadingJson());

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		Data->SetObjectField(TEXT("asset_registry"), BuildRuntimeAssetRegistryLoadingJson(AssetRegistry));

		FStreamableManager* StreamableManager = UAssetManager::IsInitialized() ? &UAssetManager::GetStreamableManager() : nullptr;
		Data->SetObjectField(TEXT("streamable_manager"), BuildRuntimeStreamableManagerJson(StreamableManager));
		if (bIncludeStreamingManager)
		{
			Data->SetObjectField(TEXT("streaming_manager"), BuildStreamingManagerJson());
		}

		TArray<TSharedPtr<FJsonValue>> Limitations;
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("This endpoint is read-only and never starts, scans, flushes, or waits for async loads")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("Unreal public API exposes async package counts and per-package percentage probes, but not global async package names")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("GetAsyncLoadPercentage is only called for explicit or default probes and may return -1 when the package is not actively loading")));
		Data->SetArrayField(TEXT("limitations"), Limitations);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
		}
		else
		{
			if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; async load diagnostics reflect the editor world")));
			}
			Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
		}

		TArray<FString> EffectiveProbeInputs = ProbeInputs;
		bool bDefaultWorldPackageProbe = false;
		if (EffectiveProbeInputs.Num() == 0 && World)
		{
			if (UPackage* WorldPackage = World->GetOutermost())
			{
				EffectiveProbeInputs.Add(WorldPackage->GetName());
				bDefaultWorldPackageProbe = true;
			}
		}

		Data->SetBoolField(TEXT("include_package_probes"), bIncludePackageProbes);
		Data->SetBoolField(TEXT("include_streamable_handles"), bIncludeStreamableHandles);
		Data->SetBoolField(TEXT("include_streaming_manager"), bIncludeStreamingManager);
		Data->SetBoolField(TEXT("default_world_package_probe"), bDefaultWorldPackageProbe);
		Data->SetNumberField(TEXT("probe_limit"), ProbeLimit);
		Data->SetNumberField(TEXT("asset_data_limit"), AssetDataLimit);
		Data->SetNumberField(TEXT("handle_limit"), HandleLimit);
		Data->SetNumberField(TEXT("requested_asset_limit"), RequestedAssetLimit);
		Data->SetNumberField(TEXT("requested_probe_input_count"), EffectiveProbeInputs.Num());

		if (bIncludePackageProbes)
		{
			TArray<TSharedPtr<FJsonValue>> ProbeJson;
			TSet<FString> SeenInputs;
			int32 UniqueProbeCount = 0;
			for (FString ProbeInput : EffectiveProbeInputs)
			{
				ProbeInput.TrimStartAndEndInline();
				if (ProbeInput.IsEmpty() || SeenInputs.Contains(ProbeInput))
				{
					continue;
				}
				SeenInputs.Add(ProbeInput);
				++UniqueProbeCount;
				if (ProbeJson.Num() >= ProbeLimit)
				{
					continue;
				}
				ProbeJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeAsyncLoadProbeJson(
					ProbeInput,
					AssetRegistry,
					StreamableManager,
					bIncludeStreamableHandles,
					AssetDataLimit,
					HandleLimit,
					RequestedAssetLimit)));
			}
			Data->SetNumberField(TEXT("unique_probe_input_count"), UniqueProbeCount);
			Data->SetNumberField(TEXT("returned_probe_count"), ProbeJson.Num());
			Data->SetBoolField(TEXT("probes_truncated"), UniqueProbeCount > ProbeJson.Num());
			Data->SetArrayField(TEXT("package_probes"), ProbeJson);
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime async load diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeGameInstanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	bool bIncludeLocalPlayers = true;
	bool bIncludeSubsystems = true;
	bool bIncludeSaveNames = false;
	FString SaveSlotName;
	int32 SaveUserIndex = 0;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_local_players"), bIncludeLocalPlayers);
		Params->TryGetBoolField(TEXT("include_subsystems"), bIncludeSubsystems);
		Params->TryGetBoolField(TEXT("include_save_names"), bIncludeSaveNames);
		Params->TryGetStringField(TEXT("save_slot_name"), SaveSlotName);
		double NumberValue = 0.0;
		if (Params->TryGetNumberField(TEXT("save_user_index"), NumberValue))
		{
			SaveUserIndex = static_cast<int32>(NumberValue);
		}
	}
	SaveSlotName.TrimStartAndEndInline();
	const int32 LocalPlayerLimit = ReadClampedIntField(Params, TEXT("local_player_limit"), 16, 0, 128);
	const int32 SubsystemLimit = ReadClampedIntField(Params, TEXT("subsystem_limit"), 100, 0, 1000);
	const int32 SaveNameLimit = ReadClampedIntField(Params, TEXT("save_name_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, bIncludeLocalPlayers, bIncludeSubsystems, bIncludeSaveNames, SaveSlotName, SaveUserIndex, LocalPlayerLimit, SubsystemLimit, SaveNameLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
		}
		else
		{
			if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; game instance diagnostics reflect the editor world and may not have a runtime GameInstance")));
			}

			Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
			UGameInstance* GameInstance = World->GetGameInstance();
			Data->SetBoolField(TEXT("game_instance_available"), GameInstance != nullptr);
			if (GameInstance)
			{
				TSharedPtr<FJsonObject> GameInstanceJson = BuildRuntimeObjectReferenceJson(GameInstance);
				GameInstanceJson->SetBoolField(TEXT("dedicated_server_instance"), GameInstance->IsDedicatedServerInstance());
				GameInstanceJson->SetStringField(TEXT("online_platform_name"), GameInstance->GetOnlinePlatformName().ToString());
				TSharedPtr<FJsonObject> OnlineSessionJson = MakeShared<FJsonObject>();
				OnlineSessionJson->SetBoolField(TEXT("present"), GameInstance->GetOnlineSession() != nullptr);
				if (UClass* OnlineSessionClass = GameInstance->GetOnlineSessionClass())
				{
					OnlineSessionJson->SetStringField(TEXT("class"), OnlineSessionClass->GetPathName());
				}
				GameInstanceJson->SetObjectField(TEXT("online_session"), OnlineSessionJson);

				const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
				GameInstanceJson->SetNumberField(TEXT("local_player_count"), LocalPlayers.Num());
				GameInstanceJson->SetBoolField(TEXT("include_local_players"), bIncludeLocalPlayers);
				GameInstanceJson->SetNumberField(TEXT("local_player_limit"), LocalPlayerLimit);
				if (bIncludeLocalPlayers)
				{
					TArray<TSharedPtr<FJsonValue>> LocalPlayersJson;
					for (ULocalPlayer* LocalPlayer : LocalPlayers)
					{
						if (!LocalPlayer)
						{
							continue;
						}
						if (LocalPlayersJson.Num() >= LocalPlayerLimit)
						{
							break;
						}
						LocalPlayersJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeLocalPlayerJson(LocalPlayer, World, bIncludeSubsystems, SubsystemLimit)));
					}
					GameInstanceJson->SetNumberField(TEXT("returned_local_player_count"), LocalPlayersJson.Num());
					GameInstanceJson->SetBoolField(TEXT("local_players_truncated"), LocalPlayers.Num() > LocalPlayersJson.Num());
					GameInstanceJson->SetArrayField(TEXT("local_players"), LocalPlayersJson);
				}

				GameInstanceJson->SetBoolField(TEXT("include_subsystems"), bIncludeSubsystems);
				GameInstanceJson->SetNumberField(TEXT("subsystem_limit"), SubsystemLimit);
				if (bIncludeSubsystems)
				{
					const TArray<UGameInstanceSubsystem*> Subsystems = GameInstance->GetSubsystemArrayCopy<UGameInstanceSubsystem>();
					TArray<TSharedPtr<FJsonValue>> SubsystemsJson;
					for (UGameInstanceSubsystem* Subsystem : Subsystems)
					{
						if (!Subsystem)
						{
							continue;
						}
						if (SubsystemsJson.Num() >= SubsystemLimit)
						{
							break;
						}
						SubsystemsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimeSubsystemJson(Subsystem)));
					}
					GameInstanceJson->SetNumberField(TEXT("subsystem_count"), Subsystems.Num());
					GameInstanceJson->SetNumberField(TEXT("returned_subsystem_count"), SubsystemsJson.Num());
					GameInstanceJson->SetBoolField(TEXT("subsystems_truncated"), Subsystems.Num() > SubsystemsJson.Num());
					GameInstanceJson->SetArrayField(TEXT("subsystems"), SubsystemsJson);
				}

				Data->SetObjectField(TEXT("game_instance"), GameInstanceJson);
			}
			else
			{
				Data->SetObjectField(TEXT("game_instance"), BuildRuntimeObjectReferenceJson(nullptr));
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected world has no GameInstance")));
			}
		}

		TSharedPtr<FJsonObject> SaveSystemJson = MakeShared<FJsonObject>();
		SaveSystemJson->SetBoolField(TEXT("include_save_names"), bIncludeSaveNames);
		SaveSystemJson->SetStringField(TEXT("save_slot_name"), SaveSlotName);
		SaveSystemJson->SetNumberField(TEXT("save_user_index"), SaveUserIndex);
		SaveSystemJson->SetNumberField(TEXT("save_name_limit"), SaveNameLimit);
		ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
		SaveSystemJson->SetBoolField(TEXT("available"), SaveSystem != nullptr);
		if (SaveSystem)
		{
			SaveSystemJson->SetBoolField(TEXT("platform_has_native_ui"), SaveSystem->PlatformHasNativeUI());
			SaveSystemJson->SetBoolField(TEXT("supports_multiple_users"), SaveSystem->DoesSaveSystemSupportMultipleUsers());

			if (!SaveSlotName.IsEmpty())
			{
				const ISaveGameSystem::ESaveExistsResult ExistsResult = SaveSystem->DoesSaveGameExistWithResult(*SaveSlotName, SaveUserIndex);
				TSharedPtr<FJsonObject> SlotProbeJson = MakeShared<FJsonObject>();
				SlotProbeJson->SetStringField(TEXT("slot_name"), SaveSlotName);
				SlotProbeJson->SetNumberField(TEXT("user_index"), SaveUserIndex);
				SlotProbeJson->SetStringField(TEXT("exists_result"), SaveExistsResultToString(ExistsResult));
				SlotProbeJson->SetBoolField(TEXT("exists"), ExistsResult == ISaveGameSystem::ESaveExistsResult::OK);
				SaveSystemJson->SetObjectField(TEXT("slot_probe"), SlotProbeJson);
			}

			if (bIncludeSaveNames)
			{
				TArray<FString> FoundSaves;
				const bool bNamesSupported = SaveSystem->GetSaveGameNames(FoundSaves, SaveUserIndex);
				TArray<TSharedPtr<FJsonValue>> SaveNamesJson;
				for (const FString& SaveName : FoundSaves)
				{
					if (SaveNamesJson.Num() >= SaveNameLimit)
					{
						break;
					}
					SaveNamesJson.Add(MakeShared<FJsonValueString>(SaveName));
				}
				SaveSystemJson->SetBoolField(TEXT("save_names_supported"), bNamesSupported);
				SaveSystemJson->SetNumberField(TEXT("save_name_count"), FoundSaves.Num());
				SaveSystemJson->SetNumberField(TEXT("returned_save_name_count"), SaveNamesJson.Num());
				SaveSystemJson->SetBoolField(TEXT("save_names_truncated"), FoundSaves.Num() > SaveNamesJson.Num());
				SaveSystemJson->SetArrayField(TEXT("save_names"), SaveNamesJson);
			}
		}
		else
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("Save game system is unavailable")));
		}
		Data->SetObjectField(TEXT("save_game_system"), SaveSystemJson);

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime game instance diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeLevelTravelDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	bool bIncludeURLOptions = true;
	bool bIncludePreparingLevels = true;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_url_options"), bIncludeURLOptions);
		Params->TryGetBoolField(TEXT("include_preparing_levels"), bIncludePreparingLevels);
	}
	const int32 URLOptionLimit = ReadClampedIntField(Params, TEXT("url_option_limit"), 50, 0, 500);
	const int32 PreparingLevelLimit = ReadClampedIntField(Params, TEXT("preparing_level_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, bIncludeURLOptions, bIncludePreparingLevels, URLOptionLimit, PreparingLevelLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; level travel diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));

		TSharedPtr<FJsonObject> TravelJson = MakeShared<FJsonObject>();
		TravelJson->SetObjectField(TEXT("current_url"), BuildURLJson(World->URL, bIncludeURLOptions, URLOptionLimit));
		TravelJson->SetStringField(TEXT("local_url"), World->GetLocalURL());
		TravelJson->SetStringField(TEXT("address_url"), World->GetAddressURL());
		TravelJson->SetStringField(TEXT("next_url"), World->NextURL);
		TravelJson->SetBoolField(TEXT("server_travel_pending"), !World->NextURL.IsEmpty() || World->NextSwitchCountdown > 0.0f);
		TravelJson->SetNumberField(TEXT("next_switch_countdown"), World->NextSwitchCountdown);
		TravelJson->SetStringField(TEXT("next_travel_type"), TravelTypeToString(World->NextTravelType));
		TravelJson->SetBoolField(TEXT("seamless_travel_active"), World->IsInSeamlessTravel());
		TravelJson->SetNumberField(TEXT("preparing_level_count"), World->PreparingLevelNames.Num());
		TravelJson->SetStringField(TEXT("committed_persistent_level_name"), World->CommittedPersistentLevelName.ToString());
		TravelJson->SetBoolField(TEXT("include_preparing_levels"), bIncludePreparingLevels);
		TravelJson->SetNumberField(TEXT("preparing_level_limit"), PreparingLevelLimit);
		if (bIncludePreparingLevels)
		{
			TArray<TSharedPtr<FJsonValue>> PreparingLevelsJson;
			for (const FName& LevelName : World->PreparingLevelNames)
			{
				if (PreparingLevelsJson.Num() >= PreparingLevelLimit)
				{
					break;
				}
				PreparingLevelsJson.Add(MakeShared<FJsonValueString>(LevelName.ToString()));
			}
			TravelJson->SetNumberField(TEXT("returned_preparing_level_count"), PreparingLevelsJson.Num());
			TravelJson->SetBoolField(TEXT("preparing_levels_truncated"), World->PreparingLevelNames.Num() > PreparingLevelsJson.Num());
			TravelJson->SetArrayField(TEXT("preparing_levels"), PreparingLevelsJson);
		}
		Data->SetObjectField(TEXT("travel"), TravelJson);

		Data->SetObjectField(TEXT("net_driver"), BuildRuntimeNetDriverJson(World->GetNetDriver()));

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime level travel diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeMultiplayerConnectionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	bool bIncludeConnections = true;
	bool bIncludePlayerControllers = true;
	bool bIncludeWorldContext = true;
	bool bIncludeURLOptions = true;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_connections"), bIncludeConnections);
		Params->TryGetBoolField(TEXT("include_player_controllers"), bIncludePlayerControllers);
		Params->TryGetBoolField(TEXT("include_world_context"), bIncludeWorldContext);
		Params->TryGetBoolField(TEXT("include_url_options"), bIncludeURLOptions);
	}
	const int32 ConnectionLimit = ReadClampedIntField(Params, TEXT("connection_limit"), 32, 0, 512);
	const int32 PlayerControllerLimit = ReadClampedIntField(Params, TEXT("player_controller_limit"), 64, 0, 1024);
	const int32 URLOptionLimit = ReadClampedIntField(Params, TEXT("url_option_limit"), 50, 0, 500);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, bIncludeConnections, bIncludePlayerControllers, bIncludeWorldContext, bIncludeURLOptions, ConnectionLimit, PlayerControllerLimit, URLOptionLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; multiplayer diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));

		UGameInstance* GameInstance = World->GetGameInstance();
		TSharedPtr<FJsonObject> OnlineSessionJson = MakeShared<FJsonObject>();
		OnlineSessionJson->SetBoolField(TEXT("game_instance_available"), GameInstance != nullptr);
		if (GameInstance)
		{
			OnlineSessionJson->SetObjectField(TEXT("game_instance"), BuildRuntimeObjectReferenceJson(GameInstance));
			OnlineSessionJson->SetStringField(TEXT("online_platform_name"), GameInstance->GetOnlinePlatformName().ToString());
			OnlineSessionJson->SetBoolField(TEXT("online_session_present"), GameInstance->GetOnlineSession() != nullptr);
			if (UClass* OnlineSessionClass = GameInstance->GetOnlineSessionClass())
			{
				OnlineSessionJson->SetStringField(TEXT("online_session_class"), OnlineSessionClass->GetPathName());
			}
		}
		Data->SetObjectField(TEXT("online_session"), OnlineSessionJson);

		UNetDriver* WorldNetDriver = World->GetNetDriver();
		Data->SetObjectField(TEXT("net_driver"), BuildRuntimeNetDriverJson(WorldNetDriver, bIncludeConnections, ConnectionLimit, bIncludeURLOptions, URLOptionLimit));

		if (bIncludePlayerControllers)
		{
			TArray<TSharedPtr<FJsonValue>> ControllersJson;
			int32 TotalControllerCount = 0;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* Controller = It->Get();
				if (!Controller)
				{
					continue;
				}
				++TotalControllerCount;
				if (ControllersJson.Num() >= PlayerControllerLimit)
				{
					continue;
				}

				TSharedPtr<FJsonObject> ControllerJson = BuildRuntimeObjectReferenceJson(Controller);
				ControllerJson->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
				ControllerJson->SetStringField(TEXT("net_mode"), NetModeToString(Controller->GetNetMode()));
				if (APawn* Pawn = Controller->GetPawn())
				{
					ControllerJson->SetObjectField(TEXT("pawn"), BuildRuntimeActorJson(Pawn));
				}
				if (UNetConnection* NetConnection = Controller->GetNetConnection())
				{
					ControllerJson->SetBoolField(TEXT("net_connection_present"), true);
					ControllerJson->SetObjectField(TEXT("net_connection"), bIncludeConnections
						? BuildRuntimeNetConnectionJson(NetConnection, bIncludeURLOptions, URLOptionLimit)
						: BuildRuntimeObjectReferenceJson(NetConnection));
				}
				else
				{
					ControllerJson->SetBoolField(TEXT("net_connection_present"), false);
				}
				if (Controller->PlayerState)
				{
					ControllerJson->SetStringField(TEXT("player_state_name"), Controller->PlayerState->GetPlayerName());
					ControllerJson->SetObjectField(TEXT("player_state"), BuildRuntimeObjectReferenceJson(Controller->PlayerState));
				}
				ControllersJson.Add(MakeShared<FJsonValueObject>(ControllerJson));
			}
			Data->SetNumberField(TEXT("player_controller_count"), TotalControllerCount);
			Data->SetNumberField(TEXT("returned_player_controller_count"), ControllersJson.Num());
			Data->SetBoolField(TEXT("player_controllers_truncated"), TotalControllerCount > ControllersJson.Num());
			Data->SetArrayField(TEXT("player_controllers"), ControllersJson);
		}

		if (bIncludeWorldContext && GEngine)
		{
			if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
			{
				TSharedPtr<FJsonObject> ContextJson = MakeShared<FJsonObject>();
				ContextJson->SetBoolField(TEXT("present"), true);
				ContextJson->SetStringField(TEXT("context_handle"), WorldContext->ContextHandle.ToString());
				ContextJson->SetStringField(TEXT("world_type"), LexToString(WorldContext->WorldType));
				ContextJson->SetStringField(TEXT("travel_url"), WorldContext->TravelURL);
				ContextJson->SetStringField(TEXT("travel_type"), TravelTypeToString(static_cast<ETravelType>(WorldContext->TravelType)));
				ContextJson->SetObjectField(TEXT("last_url"), BuildURLJson(WorldContext->LastURL, bIncludeURLOptions, URLOptionLimit));
				ContextJson->SetObjectField(TEXT("last_remote_url"), BuildURLJson(WorldContext->LastRemoteURL, bIncludeURLOptions, URLOptionLimit));
				ContextJson->SetBoolField(TEXT("pending_net_game_present"), WorldContext->PendingNetGame != nullptr);
				ContextJson->SetBoolField(TEXT("waiting_on_online_subsystem"), WorldContext->bWaitingOnOnlineSubsystem);
				ContextJson->SetBoolField(TEXT("run_as_dedicated"), WorldContext->RunAsDedicated);
				ContextJson->SetBoolField(TEXT("primary_pie_instance"), WorldContext->bIsPrimaryPIEInstance);
				ContextJson->SetNumberField(TEXT("pie_instance"), WorldContext->PIEInstance);
				ContextJson->SetStringField(TEXT("pie_prefix"), WorldContext->PIEPrefix);
				ContextJson->SetNumberField(TEXT("active_net_driver_count"), WorldContext->ActiveNetDrivers.Num());
				ContextJson->SetNumberField(TEXT("levels_to_load_for_pending_map_change_count"), WorldContext->LevelsToLoadForPendingMapChange.Num());
				ContextJson->SetNumberField(TEXT("loaded_levels_for_pending_map_change_count"), WorldContext->LoadedLevelsForPendingMapChange.Num());
				ContextJson->SetBoolField(TEXT("should_commit_pending_map_change"), WorldContext->bShouldCommitPendingMapChange != 0);
				ContextJson->SetStringField(TEXT("pending_map_change_failure_description"), WorldContext->PendingMapChangeFailureDescription);
				ContextJson->SetObjectField(TEXT("game_viewport"), BuildRuntimeObjectReferenceJson(WorldContext->GameViewport));
				ContextJson->SetObjectField(TEXT("owning_game_instance"), BuildRuntimeObjectReferenceJson(WorldContext->OwningGameInstance));

				if (UPendingNetGame* PendingNetGame = WorldContext->PendingNetGame)
				{
					TSharedPtr<FJsonObject> PendingJson = BuildRuntimeObjectReferenceJson(PendingNetGame);
					PendingJson->SetObjectField(TEXT("url"), BuildURLJson(PendingNetGame->URL, bIncludeURLOptions, URLOptionLimit));
					PendingJson->SetBoolField(TEXT("successfully_connected"), PendingNetGame->bSuccessfullyConnected);
					PendingJson->SetBoolField(TEXT("sent_join_request"), PendingNetGame->bSentJoinRequest);
					PendingJson->SetBoolField(TEXT("loaded_map_successfully"), PendingNetGame->bLoadedMapSuccessfully);
					PendingJson->SetBoolField(TEXT("failed_travel"), PendingNetGame->HasFailedTravel());
					PendingJson->SetStringField(TEXT("connection_error"), PendingNetGame->ConnectionError);
					PendingJson->SetObjectField(TEXT("net_driver"), BuildRuntimeNetDriverJson(PendingNetGame->GetNetDriver(), bIncludeConnections, ConnectionLimit, bIncludeURLOptions, URLOptionLimit));
					PendingJson->SetBoolField(TEXT("demo_net_driver_present"), PendingNetGame->GetDemoNetDriver() != nullptr);
					ContextJson->SetObjectField(TEXT("pending_net_game"), PendingJson);
				}

				TArray<TSharedPtr<FJsonValue>> ActiveNetDriversJson;
				for (const FNamedNetDriver& NamedDriver : WorldContext->ActiveNetDrivers)
				{
					TSharedPtr<FJsonObject> NamedDriverJson = MakeShared<FJsonObject>();
					NamedDriverJson->SetBoolField(TEXT("present"), NamedDriver.NetDriver != nullptr);
					NamedDriverJson->SetObjectField(TEXT("net_driver"), BuildRuntimeNetDriverJson(NamedDriver.NetDriver, bIncludeConnections, ConnectionLimit, bIncludeURLOptions, URLOptionLimit));
					ActiveNetDriversJson.Add(MakeShared<FJsonValueObject>(NamedDriverJson));
				}
				ContextJson->SetArrayField(TEXT("active_net_drivers"), ActiveNetDriversJson);
				Data->SetObjectField(TEXT("world_context"), ContextJson);
			}
			else
			{
				TSharedPtr<FJsonObject> ContextJson = MakeShared<FJsonObject>();
				ContextJson->SetBoolField(TEXT("present"), false);
				Data->SetObjectField(TEXT("world_context"), ContextJson);
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("No engine world context was found for the selected world")));
			}
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime multiplayer connection diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeTickTimerLatentDiagnostics(TSharedPtr<FJsonObject> Params)
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

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, ObjectPath, ActorPath, ActorLabel, ActorName, bIncludeLatentActions, LatentActionLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; tick/timer/latent diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
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
			LatentJson->SetObjectField(TEXT("target"), BuildRuntimeObjectReferenceJson(TargetObject));
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
			LatentJson->SetObjectField(TEXT("target"), BuildRuntimeObjectReferenceJson(nullptr));
			LatentJson->SetBoolField(TEXT("target_world_matches"), false);
			LatentJson->SetNumberField(TEXT("action_count"), 0);
			LatentJson->SetNumberField(TEXT("active_uuid_count"), 0);
			LatentJson->SetBoolField(TEXT("actions_truncated"), false);
		}
		LatentJson->SetNumberField(TEXT("returned_action_count"), LatentActionsJson.Num());
		LatentJson->SetArrayField(TEXT("actions"), LatentActionsJson);
		Data->SetObjectField(TEXT("latent_actions"), LatentJson);

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime tick/timer/latent diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeSchedulerPerformanceDiagnostics(TSharedPtr<FJsonObject> Params)
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

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, NameFilter, ClassFilter, ComponentClassFilter, bIncludeActorTicks, bIncludeComponentTicks, ActorLimit, ComponentLimit, HitchThresholdMs, Promise, this]()
	{
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
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
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

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
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
					TSharedPtr<FJsonObject> ActorJson = BuildRuntimeActorJson(Actor);
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
					TSharedPtr<FJsonObject> ComponentJson = BuildRuntimeComponentJson(Component);
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
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime scheduler/performance diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimePhysicsCollisionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString NameFilter;
	FString ClassFilter;
	FString ComponentClassFilter;
	bool bIncludeComponents = true;
	bool bIncludeResponses = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
		Params->TryGetBoolField(TEXT("include_responses"), bIncludeResponses);
	}
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	ComponentClassFilter.TrimStartAndEndInline();
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 200, 0, 2000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, NameFilter, ClassFilter, ComponentClassFilter, bIncludeComponents, bIncludeResponses, ComponentLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetStringField(TEXT("name_filter"), NameFilter);
		Data->SetStringField(TEXT("class_filter"), ClassFilter);
		Data->SetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Data->SetBoolField(TEXT("include_components"), bIncludeComponents);
		Data->SetBoolField(TEXT("include_responses"), bIncludeResponses);
		Data->SetNumberField(TEXT("component_limit"), ComponentLimit);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
		Data->SetObjectField(TEXT("physics_settings"), BuildRuntimePhysicsSettingsJson());

		TArray<TSharedPtr<FJsonValue>> Limitations;
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("This endpoint is read-only and does not execute sweep, trace, overlap, or contact queries")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("Physics solver internals, broadphase details, and live contact pairs are not exposed through public runtime API")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("Mass, bounds, and body-instance fields are point-in-time component snapshots")));
		Data->SetArrayField(TEXT("limitations"), Limitations);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; physics/collision diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
		Data->SetObjectField(TEXT("physics_world"), BuildRuntimePhysicsWorldJson(World));

		auto ActorMatchesClassFilter = [&ClassFilter](AActor* Actor)
		{
			if (!Actor)
			{
				return false;
			}
			const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
			return ClassFilter.IsEmpty() || ActorClassPath.Contains(ClassFilter);
		};

		auto ComponentMatchesFilters = [&NameFilter, &ComponentClassFilter](UPrimitiveComponent* Component)
		{
			if (!Component)
			{
				return false;
			}

			const FString ComponentClassPath = Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT("");
			if (!ComponentClassFilter.IsEmpty() && !ComponentClassPath.Contains(ComponentClassFilter))
			{
				return false;
			}

			if (NameFilter.IsEmpty())
			{
				return true;
			}

			if (Component->GetName().Contains(NameFilter) || Component->GetPathName().Contains(NameFilter))
			{
				return true;
			}

			if (AActor* Owner = Component->GetOwner())
			{
				return Owner->GetName().Contains(NameFilter) || Owner->GetActorLabel().Contains(NameFilter) || Owner->GetPathName().Contains(NameFilter);
			}
			return false;
		};

		int32 TotalActorCount = 0;
		int32 ActorClassFilterMatchCount = 0;
		int32 TotalPrimitiveComponentCount = 0;
		int32 MatchedPrimitiveComponentCount = 0;
		int32 RegisteredPrimitiveComponentCount = 0;
		int32 CollisionEnabledCount = 0;
		int32 NoCollisionCount = 0;
		int32 QueryCollisionEnabledCount = 0;
		int32 PhysicsCollisionEnabledCount = 0;
		int32 ProbeCollisionEnabledCount = 0;
		int32 OverlapEventsEnabledCount = 0;
		int32 SimulatingPhysicsCount = 0;
		int32 GravityEnabledCount = 0;
		int32 BodyInstancePresentCount = 0;
		int32 BlockingResponseComponentCount = 0;
		int32 OverlapResponseComponentCount = 0;
		int32 IgnoreResponseComponentCount = 0;
		double TotalMassKg = 0.0;
		TMap<FString, int32> CollisionEnabledCounts;
		TMap<FString, int32> ObjectTypeCounts;
		TMap<FString, int32> ProfileCounts;
		TArray<TSharedPtr<FJsonValue>> ComponentsJson;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			++TotalActorCount;

			TArray<UPrimitiveComponent*> PrimitiveComponents;
			Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			TotalPrimitiveComponentCount += PrimitiveComponents.Num();

			if (!ActorMatchesClassFilter(Actor))
			{
				continue;
			}
			++ActorClassFilterMatchCount;

			for (UPrimitiveComponent* Component : PrimitiveComponents)
			{
				if (!Component || !ComponentMatchesFilters(Component))
				{
					continue;
				}

				++MatchedPrimitiveComponentCount;
				if (Component->IsRegistered()) ++RegisteredPrimitiveComponentCount;

				const ECollisionEnabled::Type CollisionEnabled = Component->GetCollisionEnabled();
				IncrementStringCounter(CollisionEnabledCounts, CollisionEnabledToString(CollisionEnabled));
				IncrementStringCounter(ObjectTypeCounts, CollisionChannelToString(Component->GetCollisionObjectType()));
				IncrementStringCounter(ProfileCounts, Component->GetCollisionProfileName().ToString());
				if (CollisionEnabled == ECollisionEnabled::NoCollision)
				{
					++NoCollisionCount;
				}
				else
				{
					++CollisionEnabledCount;
				}
				if (CollisionEnabledHasQuery(CollisionEnabled)) ++QueryCollisionEnabledCount;
				if (CollisionEnabledHasPhysics(CollisionEnabled)) ++PhysicsCollisionEnabledCount;
				if (CollisionEnabledHasProbe(CollisionEnabled)) ++ProbeCollisionEnabledCount;
				if (Component->GetGenerateOverlapEvents()) ++OverlapEventsEnabledCount;
				if (Component->IsSimulatingPhysics()) ++SimulatingPhysicsCount;
				if (Component->IsGravityEnabled()) ++GravityEnabledCount;

				FBodyInstance* BodyInstance = Component->GetBodyInstance();
				if (BodyInstance)
				{
					++BodyInstancePresentCount;
					TotalMassKg += Component->GetMass();
				}

				bool bHasBlockResponse = false;
				bool bHasOverlapResponse = false;
				bool bHasIgnoreResponse = false;
				const int32 MaxSerializableChannel = static_cast<int32>(ECC_OverlapAll_Deprecated);
				for (int32 ChannelIndex = 0; ChannelIndex < MaxSerializableChannel; ++ChannelIndex)
				{
					const ECollisionResponse Response = Component->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(ChannelIndex));
					bHasBlockResponse = bHasBlockResponse || Response == ECR_Block;
					bHasOverlapResponse = bHasOverlapResponse || Response == ECR_Overlap;
					bHasIgnoreResponse = bHasIgnoreResponse || Response == ECR_Ignore;
				}
				if (bHasBlockResponse) ++BlockingResponseComponentCount;
				if (bHasOverlapResponse) ++OverlapResponseComponentCount;
				if (bHasIgnoreResponse) ++IgnoreResponseComponentCount;

				if (bIncludeComponents && ComponentsJson.Num() < ComponentLimit)
				{
					ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimePrimitivePhysicsJson(Component, bIncludeResponses)));
				}
			}
		}

		TSharedPtr<FJsonObject> SummaryJson = MakeShared<FJsonObject>();
		SummaryJson->SetNumberField(TEXT("total_actor_count"), TotalActorCount);
		SummaryJson->SetNumberField(TEXT("actor_class_filter_match_count"), ActorClassFilterMatchCount);
		SummaryJson->SetNumberField(TEXT("total_primitive_component_count"), TotalPrimitiveComponentCount);
		SummaryJson->SetNumberField(TEXT("matched_primitive_component_count"), MatchedPrimitiveComponentCount);
		SummaryJson->SetNumberField(TEXT("registered_primitive_component_count"), RegisteredPrimitiveComponentCount);
		SummaryJson->SetNumberField(TEXT("collision_enabled_count"), CollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("no_collision_count"), NoCollisionCount);
		SummaryJson->SetNumberField(TEXT("query_collision_enabled_count"), QueryCollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("physics_collision_enabled_count"), PhysicsCollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("probe_collision_enabled_count"), ProbeCollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("overlap_events_enabled_count"), OverlapEventsEnabledCount);
		SummaryJson->SetNumberField(TEXT("simulating_physics_count"), SimulatingPhysicsCount);
		SummaryJson->SetNumberField(TEXT("gravity_enabled_count"), GravityEnabledCount);
		SummaryJson->SetNumberField(TEXT("body_instance_present_count"), BodyInstancePresentCount);
		SummaryJson->SetNumberField(TEXT("blocking_response_component_count"), BlockingResponseComponentCount);
		SummaryJson->SetNumberField(TEXT("overlap_response_component_count"), OverlapResponseComponentCount);
		SummaryJson->SetNumberField(TEXT("ignore_response_component_count"), IgnoreResponseComponentCount);
		SummaryJson->SetNumberField(TEXT("total_mass_kg"), TotalMassKg);
		SummaryJson->SetBoolField(TEXT("include_components"), bIncludeComponents);
		SummaryJson->SetBoolField(TEXT("include_responses"), bIncludeResponses);
		SummaryJson->SetNumberField(TEXT("component_limit"), ComponentLimit);
		SummaryJson->SetNumberField(TEXT("returned_component_count"), ComponentsJson.Num());
		SummaryJson->SetBoolField(TEXT("components_truncated"), bIncludeComponents && MatchedPrimitiveComponentCount > ComponentsJson.Num());
		SummaryJson->SetArrayField(TEXT("collision_enabled_counts"), BuildStringCounterJson(CollisionEnabledCounts, TEXT("collision_enabled")));
		SummaryJson->SetArrayField(TEXT("object_type_counts"), BuildStringCounterJson(ObjectTypeCounts, TEXT("object_type")));
		SummaryJson->SetArrayField(TEXT("profile_counts"), BuildStringCounterJson(ProfileCounts, TEXT("profile")));
		Data->SetObjectField(TEXT("summary"), SummaryJson);
		if (bIncludeComponents)
		{
			Data->SetArrayField(TEXT("components"), ComponentsJson);
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime physics/collision diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeReplicationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	bool bIncludeComponents = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	const int32 ActorLimit = ReadClampedIntField(Params, TEXT("actor_limit"), 100, 1, 1000);
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, ActorPath, ActorLabel, ActorName, NameFilter, ClassFilter, bIncludeComponents, ActorLimit, ComponentLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; replication diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
		Data->SetStringField(TEXT("world_net_mode"), NetModeToString(World->GetNetMode()));
		Data->SetBoolField(TEXT("include_components"), bIncludeComponents);
		Data->SetNumberField(TEXT("actor_limit"), ActorLimit);
		Data->SetNumberField(TEXT("component_limit"), ComponentLimit);

		const bool bSpecificActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		TArray<AActor*> ActorsToInspect;
		int32 MatchedActorCount = 0;
		int32 ReplicatedActorCount = 0;
		int32 ReplicatingMovementCount = 0;
		int32 DormantActorCount = 0;
		int32 AuthorityActorCount = 0;
		auto AddReplicationCounters = [&ReplicatedActorCount, &ReplicatingMovementCount, &DormantActorCount, &AuthorityActorCount](AActor* Actor)
		{
			if (!Actor)
			{
				return;
			}
			if (Actor->GetIsReplicated())
			{
				++ReplicatedActorCount;
			}
			if (Actor->IsReplicatingMovement())
			{
				++ReplicatingMovementCount;
			}
			if (Actor->NetDormancy == DORM_DormantAll || Actor->NetDormancy == DORM_DormantPartial || Actor->NetDormancy == DORM_Initial)
			{
				++DormantActorCount;
			}
			if (Actor->HasAuthority())
			{
				++AuthorityActorCount;
			}
		};

		if (bSpecificActorRequested)
		{
			AActor* Actor = FindRuntimeActorForAI(World, ActorPath, ActorLabel, ActorName);
			Data->SetBoolField(TEXT("selected_actor_requested"), true);
			Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
			if (Actor)
			{
				MatchedActorCount = 1;
				AddReplicationCounters(Actor);
				ActorsToInspect.Add(Actor);
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
			}
		}
		else
		{
			Data->SetBoolField(TEXT("selected_actor_requested"), false);
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor)
				{
					continue;
				}

				const FString Label = Actor->GetActorLabel();
				const FString Name = Actor->GetName();
				const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
				if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
				{
					continue;
				}
				if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter))
				{
					continue;
				}

				++MatchedActorCount;
				AddReplicationCounters(Actor);
				if (ActorsToInspect.Num() < ActorLimit)
				{
					ActorsToInspect.Add(Actor);
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> ActorsJson;
		for (AActor* Actor : ActorsToInspect)
		{
			if (!Actor)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ActorJson = BuildRuntimeActorJson(Actor);
			ActorJson->SetObjectField(TEXT("replication"), BuildRuntimeActorReplicationJson(Actor, bIncludeComponents, ComponentLimit));
			ActorsJson.Add(MakeShared<FJsonValueObject>(ActorJson));
		}

		Data->SetNumberField(TEXT("matched_actor_count"), MatchedActorCount);
		Data->SetNumberField(TEXT("returned_actor_count"), ActorsJson.Num());
		Data->SetBoolField(TEXT("actors_truncated"), MatchedActorCount > ActorsJson.Num());
		Data->SetNumberField(TEXT("replicated_actor_count"), ReplicatedActorCount);
		Data->SetNumberField(TEXT("replicating_movement_count"), ReplicatingMovementCount);
		Data->SetNumberField(TEXT("dormant_actor_count"), DormantActorCount);
		Data->SetNumberField(TEXT("authority_actor_count"), AuthorityActorCount);
		Data->SetArrayField(TEXT("actors"), ActorsJson);
		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime replication diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeAbilitySystemDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	bool bIncludeAbilities = true;
	bool bIncludeEffects = true;
	bool bIncludeAttributes = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("include_abilities"), bIncludeAbilities);
		Params->TryGetBoolField(TEXT("include_effects"), bIncludeEffects);
		Params->TryGetBoolField(TEXT("include_attributes"), bIncludeAttributes);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	const int32 ActorLimit = ReadClampedIntField(Params, TEXT("actor_limit"), 100, 1, 1000);
	const int32 AbilityLimit = ReadClampedIntField(Params, TEXT("ability_limit"), 100, 0, 1000);
	const int32 EffectLimit = ReadClampedIntField(Params, TEXT("effect_limit"), 100, 0, 1000);
	const int32 AttributeLimit = ReadClampedIntField(Params, TEXT("attribute_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, ActorPath, ActorLabel, ActorName, NameFilter, ClassFilter, bIncludeAbilities, bIncludeEffects, bIncludeAttributes, ActorLimit, AbilityLimit, EffectLimit, AttributeLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; ability system diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
		Data->SetStringField(TEXT("world_net_mode"), NetModeToString(World->GetNetMode()));
		Data->SetBoolField(TEXT("include_abilities"), bIncludeAbilities);
		Data->SetBoolField(TEXT("include_effects"), bIncludeEffects);
		Data->SetBoolField(TEXT("include_attributes"), bIncludeAttributes);
		Data->SetNumberField(TEXT("actor_limit"), ActorLimit);
		Data->SetNumberField(TEXT("ability_limit"), AbilityLimit);
		Data->SetNumberField(TEXT("effect_limit"), EffectLimit);
		Data->SetNumberField(TEXT("attribute_limit"), AttributeLimit);

		const bool bSpecificActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		TArray<AActor*> ActorsToInspect;
		int32 MatchedActorCount = 0;
		int32 AbilitySystemActorCount = 0;
		int32 AbilitySystemComponentCount = 0;
		int32 TotalAbilityCount = 0;
		int32 TotalActiveAbilityCount = 0;
		int32 TotalActiveEffectCount = 0;
		int32 TotalAttributeSetCount = 0;
		int32 TotalAttributeCount = 0;

		auto AccumulateAbilitySystemCounters = [&AbilitySystemActorCount, &AbilitySystemComponentCount, &TotalAbilityCount, &TotalActiveAbilityCount, &TotalActiveEffectCount, &TotalAttributeSetCount, &TotalAttributeCount](AActor* Actor)
		{
			if (!Actor)
			{
				return 0;
			}

			TArray<UAbilitySystemComponent*> AbilitySystems;
			Actor->GetComponents<UAbilitySystemComponent>(AbilitySystems);
			if (AbilitySystems.IsEmpty())
			{
				return 0;
			}

			++AbilitySystemActorCount;
			for (UAbilitySystemComponent* AbilitySystem : AbilitySystems)
			{
				if (!AbilitySystem)
				{
					continue;
				}

				++AbilitySystemComponentCount;
				const TArray<FGameplayAbilitySpec>& Abilities = AbilitySystem->GetActivatableAbilities();
				TotalAbilityCount += Abilities.Num();
				for (const FGameplayAbilitySpec& Spec : Abilities)
				{
					if (Spec.IsActive())
					{
						++TotalActiveAbilityCount;
					}
				}
				TotalActiveEffectCount += AbilitySystem->GetActiveGameplayEffects().GetNumGameplayEffects();
				TotalAttributeSetCount += AbilitySystem->GetSpawnedAttributes().Num();
				TArray<FGameplayAttribute> Attributes;
				AbilitySystem->GetAllAttributes(Attributes);
				TotalAttributeCount += Attributes.Num();
			}

			return AbilitySystems.Num();
		};

		if (bSpecificActorRequested)
		{
			AActor* Actor = FindRuntimeActorForAI(World, ActorPath, ActorLabel, ActorName);
			Data->SetBoolField(TEXT("selected_actor_requested"), true);
			Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
			if (Actor)
			{
				MatchedActorCount = 1;
				const int32 AbilitySystemCount = AccumulateAbilitySystemCounters(Actor);
				ActorsToInspect.Add(Actor);
				if (AbilitySystemCount == 0)
				{
					Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor has no AbilitySystemComponent")));
				}
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
			}
		}
		else
		{
			Data->SetBoolField(TEXT("selected_actor_requested"), false);
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor)
				{
					continue;
				}

				const FString Label = Actor->GetActorLabel();
				const FString Name = Actor->GetName();
				const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
				if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
				{
					continue;
				}
				if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter))
				{
					continue;
				}

				++MatchedActorCount;
				const int32 AbilitySystemCount = AccumulateAbilitySystemCounters(Actor);
				if (AbilitySystemCount > 0 && ActorsToInspect.Num() < ActorLimit)
				{
					ActorsToInspect.Add(Actor);
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> ActorsJson;
		for (AActor* Actor : ActorsToInspect)
		{
			if (!Actor)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ActorJson = BuildRuntimeActorJson(Actor);
			TArray<UAbilitySystemComponent*> AbilitySystems;
			Actor->GetComponents<UAbilitySystemComponent>(AbilitySystems);
			TArray<TSharedPtr<FJsonValue>> AbilitySystemJson;
			for (UAbilitySystemComponent* AbilitySystem : AbilitySystems)
			{
				if (!AbilitySystem)
				{
					continue;
				}
				AbilitySystemJson.Add(MakeShared<FJsonValueObject>(BuildAbilitySystemComponentJson(AbilitySystem, World, bIncludeAbilities, bIncludeEffects, bIncludeAttributes, AbilityLimit, EffectLimit, AttributeLimit)));
			}

			ActorJson->SetNumberField(TEXT("ability_system_component_count"), AbilitySystemJson.Num());
			ActorJson->SetArrayField(TEXT("ability_system_components"), AbilitySystemJson);
			ActorsJson.Add(MakeShared<FJsonValueObject>(ActorJson));
		}

		Data->SetNumberField(TEXT("matched_actor_count"), MatchedActorCount);
		Data->SetNumberField(TEXT("ability_system_actor_count"), AbilitySystemActorCount);
		Data->SetNumberField(TEXT("ability_system_component_count"), AbilitySystemComponentCount);
		Data->SetNumberField(TEXT("total_ability_count"), TotalAbilityCount);
		Data->SetNumberField(TEXT("total_active_ability_count"), TotalActiveAbilityCount);
		Data->SetNumberField(TEXT("total_active_effect_count"), TotalActiveEffectCount);
		Data->SetNumberField(TEXT("total_attribute_set_count"), TotalAttributeSetCount);
		Data->SetNumberField(TEXT("total_attribute_count"), TotalAttributeCount);
		Data->SetNumberField(TEXT("returned_actor_count"), ActorsJson.Num());
		Data->SetBoolField(TEXT("actors_truncated"), !bSpecificActorRequested && AbilitySystemActorCount > ActorsJson.Num());
		Data->SetArrayField(TEXT("actors"), ActorsJson);
		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime ability system diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeAIPerceptionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	FString TargetNameFilter;
	bool bIncludeStimuli = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("target_name_filter"), TargetNameFilter);
		Params->TryGetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	TargetNameFilter.TrimStartAndEndInline();
	const int32 ListenerLimit = ReadClampedIntField(Params, TEXT("listener_limit"), 100, 1, 1000);
	const int32 TargetLimit = ReadClampedIntField(Params, TEXT("target_limit"), 100, 0, 1000);
	const int32 StimulusLimit = ReadClampedIntField(Params, TEXT("stimulus_limit"), 100, 0, 1000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [WorldSelector, ActorPath, ActorLabel, ActorName, NameFilter, ClassFilter, TargetNameFilter, bIncludeStimuli, ListenerLimit, TargetLimit, StimulusLimit, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

		TArray<TSharedPtr<FJsonValue>> Warnings;
		FString WorldSource;
		UWorld* World = SelectAIWorld(WorldSelector, WorldSource);
		Data->SetStringField(TEXT("world_source"), WorldSource);
		Data->SetBoolField(TEXT("world_available"), World != nullptr);
		if (!World)
		{
			Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
			Data->SetArrayField(TEXT("warnings"), Warnings);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; AI perception diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildRuntimeWorldJson(World, WorldSource));
		Data->SetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
		Data->SetNumberField(TEXT("listener_limit"), ListenerLimit);
		Data->SetNumberField(TEXT("target_limit"), TargetLimit);
		Data->SetNumberField(TEXT("stimulus_limit"), StimulusLimit);
		Data->SetStringField(TEXT("target_name_filter"), TargetNameFilter);

		const bool bSpecificActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		TArray<UAIPerceptionComponent*> ComponentsToInspect;
		int32 MatchedListenerCount = 0;
		int32 PerceptionOwnerActorCount = 0;
		int32 TotalKnownTargetCount = 0;
		int32 TotalCurrentlyPerceivedTargetCount = 0;
		int32 TotalHostileTargetCount = 0;
		int32 TotalValidStimulusCount = 0;
		int32 TotalActiveStimulusCount = 0;
		int32 TotalExpiredStimulusCount = 0;

		auto AccumulatePerceptionCounters = [&PerceptionOwnerActorCount, &TotalKnownTargetCount, &TotalCurrentlyPerceivedTargetCount, &TotalHostileTargetCount, &TotalValidStimulusCount, &TotalActiveStimulusCount, &TotalExpiredStimulusCount](AActor* Actor)
		{
			if (!Actor)
			{
				return 0;
			}

			TArray<UAIPerceptionComponent*> PerceptionComponents;
			Actor->GetComponents<UAIPerceptionComponent>(PerceptionComponents);
			if (PerceptionComponents.IsEmpty())
			{
				return 0;
			}

			++PerceptionOwnerActorCount;
			for (UAIPerceptionComponent* Perception : PerceptionComponents)
			{
				if (!Perception)
				{
					continue;
				}

				TArray<AActor*> KnownActors;
				Perception->GetKnownPerceivedActors(nullptr, KnownActors);
				TotalKnownTargetCount += KnownActors.Num();

				TArray<AActor*> CurrentActors;
				Perception->GetCurrentlyPerceivedActors(nullptr, CurrentActors);
				TotalCurrentlyPerceivedTargetCount += CurrentActors.Num();

				TArray<AActor*> HostileActors;
				Perception->GetHostileActors(HostileActors);
				TotalHostileTargetCount += HostileActors.Num();

				for (UAIPerceptionComponent::FActorPerceptionContainer::TConstIterator It = Perception->GetPerceptualDataConstIterator(); It; ++It)
				{
					const FActorPerceptionInfo& Info = It->Value;
					for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
					{
						if (!Stimulus.IsValid())
						{
							continue;
						}
						++TotalValidStimulusCount;
						if (Stimulus.IsActive())
						{
							++TotalActiveStimulusCount;
						}
						if (Stimulus.IsExpired())
						{
							++TotalExpiredStimulusCount;
						}
					}
				}
			}

			return PerceptionComponents.Num();
		};

		if (bSpecificActorRequested)
		{
			AActor* Actor = FindRuntimeActorForAI(World, ActorPath, ActorLabel, ActorName);
			Data->SetBoolField(TEXT("selected_actor_requested"), true);
			Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
			if (Actor)
			{
				MatchedListenerCount = AccumulatePerceptionCounters(Actor);
				Actor->GetComponents<UAIPerceptionComponent>(ComponentsToInspect);
				if (MatchedListenerCount == 0)
				{
					Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor has no AIPerceptionComponent")));
				}
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
			}
		}
		else
		{
			Data->SetBoolField(TEXT("selected_actor_requested"), false);
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Actor = *It;
				if (!Actor)
				{
					continue;
				}

				const FString Label = Actor->GetActorLabel();
				const FString Name = Actor->GetName();
				const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
				if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
				{
					continue;
				}
				if (!ClassFilter.IsEmpty() && !ActorClassPath.Contains(ClassFilter))
				{
					continue;
				}

				TArray<UAIPerceptionComponent*> PerceptionComponents;
				Actor->GetComponents<UAIPerceptionComponent>(PerceptionComponents);
				if (PerceptionComponents.IsEmpty())
				{
					continue;
				}

				MatchedListenerCount += PerceptionComponents.Num();
				AccumulatePerceptionCounters(Actor);
				for (UAIPerceptionComponent* Perception : PerceptionComponents)
				{
					if (!Perception)
					{
						continue;
					}

					const FString ComponentClassPath = Perception->GetClass() ? Perception->GetClass()->GetPathName() : TEXT("");
					if (!ClassFilter.IsEmpty() && !ComponentClassPath.Contains(ClassFilter) && !ActorClassPath.Contains(ClassFilter))
					{
						continue;
					}
					if (ComponentsToInspect.Num() < ListenerLimit)
					{
						ComponentsToInspect.Add(Perception);
					}
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> ListenersJson;
		for (UAIPerceptionComponent* Perception : ComponentsToInspect)
		{
			if (!Perception)
			{
				continue;
			}
			ListenersJson.Add(MakeShared<FJsonValueObject>(BuildAIPerceptionComponentJson(Perception, TargetNameFilter, bIncludeStimuli, TargetLimit, StimulusLimit)));
		}

		Data->SetNumberField(TEXT("matched_listener_count"), MatchedListenerCount);
		Data->SetNumberField(TEXT("returned_listener_count"), ListenersJson.Num());
		Data->SetBoolField(TEXT("listeners_truncated"), MatchedListenerCount > ListenersJson.Num());
		Data->SetNumberField(TEXT("perception_owner_actor_count"), PerceptionOwnerActorCount);
		Data->SetNumberField(TEXT("total_known_target_count"), TotalKnownTargetCount);
		Data->SetNumberField(TEXT("total_currently_perceived_target_count"), TotalCurrentlyPerceivedTargetCount);
		Data->SetNumberField(TEXT("total_hostile_target_count"), TotalHostileTargetCount);
		Data->SetNumberField(TEXT("total_valid_stimulus_count"), TotalValidStimulusCount);
		Data->SetNumberField(TEXT("total_active_stimulus_count"), TotalActiveStimulusCount);
		Data->SetNumberField(TEXT("total_expired_stimulus_count"), TotalExpiredStimulusCount);
		Data->SetArrayField(TEXT("listeners"), ListenersJson);
		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime AI perception diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleActorList(TSharedPtr<FJsonObject> Params)
{
	FString NameFilter;
	FString ClassFilter;
	int32 Limit = 500;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		double LimitValue = 0.0;
		if (Params->TryGetNumberField(TEXT("limit"), LimitValue) && LimitValue > 0.0)
		{
			Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, 5000);
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [NameFilter, ClassFilter, Limit, Promise, this]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Actors;
		int32 MatchedCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			const FString Label = Actor->GetActorLabel();
			const FString Name = Actor->GetName();
			const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
			if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
			{
				continue;
			}
			if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter))
			{
				continue;
			}

			++MatchedCount;
			if (Actors.Num() < Limit)
			{
				Actors.Add(MakeShared<FJsonValueObject>(BuildActorJson(Actor)));
			}
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("actors"), Actors);
		Data->SetNumberField(TEXT("count"), Actors.Num());
		Data->SetNumberField(TEXT("matched_count"), MatchedCount);
		Data->SetBoolField(TEXT("truncated"), MatchedCount > Actors.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor list timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleActorSpawn(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString ClassPath;
	if (!Params->TryGetStringField(TEXT("class_path"), ClassPath))
		return CreateErrorResponse(TEXT("Missing 'class_path' parameter"));

	FString ActorLabel;
	Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
	const FVector Location = ReadVectorField(Params, TEXT("location"), FVector::ZeroVector);
	const FRotator Rotation = ReadRotatorField(Params, TEXT("rotation"), FRotator::ZeroRotator);
	const FVector Scale = ReadVectorField(Params, TEXT("scale"), FVector::OneVector);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [ClassPath, ActorLabel, Location, Rotation, Scale, Promise, this]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		UClass* ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
		if (!ActorClass)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load actor class: %s"), *ClassPath)));
			return;
		}

		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ActorSpawn", "AI Spawn Actor"));
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
		if (!Actor)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to spawn actor class: %s"), *ClassPath)));
			return;
		}

		Actor->Modify();
		Actor->SetActorScale3D(Scale);
		if (!ActorLabel.IsEmpty())
		{
			Actor->SetActorLabel(ActorLabel);
		}
		World->MarkPackageDirty();

		Promise->SetValue(CreateSuccessResponse(BuildActorJson(Actor)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor spawn timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleActorSetTransform(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString ActorPath, ActorLabel, ActorName;
	Params->TryGetStringField(TEXT("actor_path"), ActorPath);
	Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Params->TryGetStringField(TEXT("actor_name"), ActorName);
	if (ActorPath.IsEmpty() && ActorLabel.IsEmpty() && ActorName.IsEmpty())
		return CreateErrorResponse(TEXT("Expected one of: actor_path, actor_label, actor_name"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, ActorPath, ActorLabel, ActorName, Promise, this]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		AActor* Actor = FindActorForAI(World, ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Actor not found")));
			return;
		}

		const FVector Location = ReadVectorField(Params, TEXT("location"), Actor->GetActorLocation());
		const FRotator Rotation = ReadRotatorField(Params, TEXT("rotation"), Actor->GetActorRotation());
		const FVector Scale = ReadVectorField(Params, TEXT("scale"), Actor->GetActorScale3D());
		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ActorSetTransform", "AI Set Actor Transform"));
		Actor->Modify();
		Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		Actor->SetActorScale3D(Scale);
		Actor->MarkPackageDirty();
		Promise->SetValue(CreateSuccessResponse(BuildActorJson(Actor)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor set transform timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleActorDelete(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString ActorPath, ActorLabel, ActorName;
	Params->TryGetStringField(TEXT("actor_path"), ActorPath);
	Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Params->TryGetStringField(TEXT("actor_name"), ActorName);
	if (ActorPath.IsEmpty() && ActorLabel.IsEmpty() && ActorName.IsEmpty())
		return CreateErrorResponse(TEXT("Expected one of: actor_path, actor_label, actor_name"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [ActorPath, ActorLabel, ActorName, Promise, this]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		AActor* Actor = FindActorForAI(World, ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Actor not found")));
			return;
		}

		const FString DeletedPath = Actor->GetPathName();
		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ActorDelete", "AI Delete Actor"));
		const bool bDeleted = World->EditorDestroyActor(Actor, true);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("deleted"), bDeleted);
		Data->SetStringField(TEXT("actor_path"), DeletedPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor delete timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleLevelOpen(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString MapPath;
	if (!Params->TryGetStringField(TEXT("map_path"), MapPath))
		return CreateErrorResponse(TEXT("Missing 'map_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [MapPath, Promise, this]()
	{
		FString Filename = MapPath;
		if (MapPath.StartsWith(TEXT("/Game/")) || MapPath.StartsWith(TEXT("/Engine/")))
		{
			Filename = FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
		}

		const bool bLoaded = FEditorFileUtils::LoadMap(Filename, false, true);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("loaded"), bLoaded);
		Data->SetStringField(TEXT("map_path"), MapPath);
		Data->SetStringField(TEXT("filename"), Filename);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Level open timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleLevelSaveCurrent()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World || !World->PersistentLevel)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world/persistent level is available")));
			return;
		}
		FString SavedFilename;
		const bool bSaved = FEditorFileUtils::SaveLevel(World->PersistentLevel, TEXT(""), &SavedFilename);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), bSaved);
		Data->SetStringField(TEXT("package_name"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
		Data->SetStringField(TEXT("filename"), SavedFilename);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Level save current timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandlePIEStatus()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	Data->SetBoolField(TEXT("simulating"), GEditor && GEditor->bIsSimulatingInEditor);
	Data->SetStringField(TEXT("play_world"), (GEditor && GEditor->PlayWorld) ? GEditor->PlayWorld->GetName() : TEXT(""));
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandlePIEStart()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		if (!GEditor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("GEditor is not available")));
			return;
		}
		if (GEditor->PlayWorld)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("PIE is already active")));
			return;
		}

		FRequestPlaySessionParams Params;
		GEditor->RequestPlaySession(Params);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("requested"), true);
		Promise->SetValue(CreateSuccessResponse(Data));
	});
	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("PIE start timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandlePIEStop()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		if (!GEditor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("GEditor is not available")));
			return;
		}
		if (!GEditor->PlayWorld)
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("requested"), false);
			Data->SetBoolField(TEXT("pie_active"), false);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}
		GEditor->RequestEndPlayMap();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("requested"), true);
		Promise->SetValue(CreateSuccessResponse(Data));
	});
	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("PIE stop timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleEditorConsoleCommand(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString Command;
	if (!Params->TryGetStringField(TEXT("command"), Command) || Command.TrimStartAndEnd().IsEmpty())
		return CreateErrorResponse(TEXT("Missing 'command' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Command, Promise, this]()
	{
		if (!GEditor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("GEditor is not available")));
			return;
		}

		FStringOutputDevice Output;
		UWorld* World = GetAIEditorWorld();
		const bool bHandled = GEditor->Exec(World, *Command, Output);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("command"), Command);
		Data->SetBoolField(TEXT("handled"), bHandled);
		Data->SetStringField(TEXT("output"), static_cast<const FString&>(Output));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Editor console command timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleEditorLogRead(TSharedPtr<FJsonObject> Params)
{
	int32 MaxLines = 200;
	FString Filter;
	FString LogName;
	if (Params.IsValid())
	{
		double MaxLinesValue = 0.0;
		if (Params->TryGetNumberField(TEXT("max_lines"), MaxLinesValue) && MaxLinesValue > 0.0)
		{
			MaxLines = FMath::Clamp(static_cast<int32>(MaxLinesValue), 1, 5000);
		}
		Params->TryGetStringField(TEXT("filter"), Filter);
		Params->TryGetStringField(TEXT("log_name"), LogName);
	}

	if (LogName.IsEmpty())
	{
		LogName = FString::Printf(TEXT("%s.log"), FApp::GetProjectName());
	}

	const FString LogDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
	const FString LogFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(LogDir, FPaths::GetCleanFilename(LogName)));
	if (!FPaths::IsUnderDirectory(LogFilePath, LogDir))
	{
		return CreateErrorResponse(TEXT("log_name must resolve under the project log directory"));
	}

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *LogFilePath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Could not read log file: %s"), *LogFilePath));
	}

	TArray<FString> Lines;
	Contents.ParseIntoArrayLines(Lines, false);

	TArray<TSharedPtr<FJsonValue>> OutLines;
	const int32 StartIndex = FMath::Max(0, Lines.Num() - MaxLines);
	int32 MatchedCount = 0;
	for (int32 Index = StartIndex; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		if (!Filter.IsEmpty() && !Line.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		++MatchedCount;
		OutLines.Add(MakeShared<FJsonValueString>(Line));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("log_path"), LogFilePath);
	Data->SetStringField(TEXT("filter"), Filter);
	Data->SetNumberField(TEXT("max_lines"), MaxLines);
	Data->SetNumberField(TEXT("total_lines"), Lines.Num());
	Data->SetNumberField(TEXT("returned_count"), OutLines.Num());
	Data->SetNumberField(TEXT("matched_count"), MatchedCount);
	Data->SetArrayField(TEXT("lines"), OutLines);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleViewportCapture(TSharedPtr<FJsonObject> Params)
{
	FString OutputPath;
	bool bShowUI = true;
	bool bAddFilenameSuffix = false;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("output_path"), OutputPath);
		Params->TryGetBoolField(TEXT("show_ui"), bShowUI);
		Params->TryGetBoolField(TEXT("add_filename_suffix"), bAddFilenameSuffix);
	}

	if (OutputPath.IsEmpty())
	{
		const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
		OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("AIViewport"), FString::Printf(TEXT("Viewport_%s.png"), *Timestamp));
	}
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [OutputPath, bShowUI, bAddFilenameSuffix, Promise, this]()
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
		FScreenshotRequest::RequestScreenshot(OutputPath, bShowUI, bAddFilenameSuffix);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("screenshot_requested"), true);
		Data->SetStringField(TEXT("output_path"), OutputPath);
		Data->SetBoolField(TEXT("show_ui"), bShowUI);
		Data->SetBoolField(TEXT("add_filename_suffix"), bAddFilenameSuffix);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Viewport capture request timed out"));
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Widget Builder Command Handlers

FString FAIExportTCPServer::HandleCreateWidgetBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString PackagePath;
	FString AssetName;
	FString ParentClassPath;

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
	{
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	}
	Params->TryGetStringField(TEXT("parent_class"), ParentClassPath);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, ParentClassPath, Promise, this]()
	{
		UClass* ParentClass = nullptr;
		if (!ParentClassPath.IsEmpty())
		{
			ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
			if (!ParentClass)
			{
				ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
			}
			if (!ParentClass)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not find parent class: %s"), *ParentClassPath)));
				return;
			}
		}

		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::CreateWidgetBlueprint(PackagePath, AssetName, ParentClass);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create Widget Blueprint")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), WBP->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Create widget blueprint timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetClass, WidgetName, ParentName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_class"), WidgetClass))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_class' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	Params->TryGetStringField(TEXT("parent_name"), ParentName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetClass, WidgetName, ParentName, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		UWidget* Widget = UAIWidgetBlueprintBuilder::AddWidget(WBP, WidgetClass, WidgetName, ParentName);
		if (!Widget)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add widget '%s' of class '%s'"), *WidgetName, *WidgetClass)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("widget_name"), Widget->GetName());
		Data->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Add widget timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::RemoveWidget(WBP, WidgetName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove widget: %s"), *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("removed"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Remove widget timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleMoveWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, NewParentName;
	double NewIndexDouble = -1.0;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("new_parent_name"), NewParentName))
	{
		return CreateErrorResponse(TEXT("Missing 'new_parent_name' parameter"));
	}
	Params->TryGetNumberField(TEXT("index"), NewIndexDouble);
	int32 NewIndex = (int32)NewIndexDouble;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, NewParentName, NewIndex, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::MoveWidget(WBP, WidgetName, NewParentName, NewIndex);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to move widget '%s' to '%s'"), *WidgetName, *NewParentName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("new_parent"), NewParentName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Move widget timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetWidgetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, PropertyName, Value, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::SetWidgetProperty(WBP, WidgetName, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s' on widget '%s'"), *PropertyName, *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set widget property timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetSlotProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, PropertyName, Value, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::SetSlotProperty(WBP, WidgetName, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set slot property '%s' on widget '%s'"), *PropertyName, *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set slot property timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetCanvasSlotLayout(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	// All layout params default to 0
	double PosX = 0, PosY = 0, SizeX = 0, SizeY = 0;
	double AnchorMinX = 0, AnchorMinY = 0, AnchorMaxX = 0, AnchorMaxY = 0;
	double AlignmentX = 0, AlignmentY = 0;

	Params->TryGetNumberField(TEXT("position_x"), PosX);
	Params->TryGetNumberField(TEXT("position_y"), PosY);
	Params->TryGetNumberField(TEXT("size_x"), SizeX);
	Params->TryGetNumberField(TEXT("size_y"), SizeY);
	Params->TryGetNumberField(TEXT("anchor_min_x"), AnchorMinX);
	Params->TryGetNumberField(TEXT("anchor_min_y"), AnchorMinY);
	Params->TryGetNumberField(TEXT("anchor_max_x"), AnchorMaxX);
	Params->TryGetNumberField(TEXT("anchor_max_y"), AnchorMaxY);
	Params->TryGetNumberField(TEXT("alignment_x"), AlignmentX);
	Params->TryGetNumberField(TEXT("alignment_y"), AlignmentY);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [=, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::SetCanvasSlotLayout(
			WBP, WidgetName,
			(float)PosX, (float)PosY, (float)SizeX, (float)SizeY,
			(float)AnchorMinX, (float)AnchorMinY, (float)AnchorMaxX, (float)AnchorMaxY,
			(float)AlignmentX, (float)AlignmentY);

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set canvas slot layout on widget '%s'"), *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("summary"), FString::Printf(TEXT("Pos(%.0f,%.0f) Size(%.0f,%.0f)"), PosX, PosY, SizeX, SizeY));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set canvas slot layout timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetWidgetProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropertiesObj) || !PropertiesObj || !(*PropertiesObj).IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'properties' object parameter"));
	}

	// Convert JSON object to TMap
	TMap<FString, FString> Properties;
	for (const auto& Pair : (*PropertiesObj)->Values)
	{
		FString StringValue;
		if (Pair.Value->TryGetString(StringValue))
		{
			Properties.Add(Pair.Key, StringValue);
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, Properties, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		TArray<FString> Failed;
		int32 SetCount = UAIWidgetBlueprintBuilder::SetWidgetProperties(WBP, WidgetName, Properties, &Failed);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("set_count"), SetCount);

		if (Failed.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> FailedArray;
			for (const FString& F : Failed)
			{
				FailedArray.Add(MakeShared<FJsonValueString>(F));
			}
			Data->SetArrayField(TEXT("failed"), FailedArray);
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set widget properties timed out"));
	}
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Blueprint Utility Command Handlers

FString FAIExportTCPServer::HandleReparentBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString NewParentClassPath;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("new_parent_class"), NewParentClassPath))
	{
		return CreateErrorResponse(TEXT("Missing 'new_parent_class' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NewParentClassPath, Promise, this]()
	{
		// Load the Widget Blueprint
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		// Resolve new parent class
		UClass* NewParentClass = FindObject<UClass>(nullptr, *NewParentClassPath);
		if (!NewParentClass)
		{
			NewParentClass = LoadObject<UClass>(nullptr, *NewParentClassPath);
		}
		if (!NewParentClass)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not find parent class: %s"), *NewParentClassPath)));
			return;
		}

		FString OldParentName = WBP->ParentClass ? WBP->ParentClass->GetName() : TEXT("None");

		// Perform reparenting
		bool bSuccess = UAIWidgetBlueprintBuilder::ReparentBlueprint(WBP, NewParentClass);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to reparent blueprint")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("old_parent"), OldParentName);
		Data->SetStringField(TEXT("new_parent"), NewParentClass->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Reparent blueprint timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleCompileAndSave(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (WBP)
		{
			TArray<FString> Warnings;
			bool bSuccess = UAIWidgetBlueprintBuilder::CompileAndSave(WBP, &Warnings);

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("compiled"), bSuccess);
			Data->SetBoolField(TEXT("saved"), bSuccess);

			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> WarningArray;
				for (const FString& W : Warnings)
				{
					WarningArray.Add(MakeShared<FJsonValueString>(W));
				}
				Data->SetArrayField(TEXT("warnings"), WarningArray);
			}

			Promise->SetValue(CreateSuccessResponse(Data));
		}
		else
		{
			// Fallback: Data Asset — just save (no compile step)
			UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
			if (!Asset)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
				return;
			}

			bool bSaved = UAIDataAssetBuilder::SaveAsset(Asset);
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("compiled"), false);
			Data->SetBoolField(TEXT("saved"), bSaved);
			Promise->SetValue(bSaved ? CreateSuccessResponse(Data) :
				CreateErrorResponse(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath)));
		}
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Compile and save timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetWidgetTree(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> TreeJson = UAIWidgetBlueprintBuilder::GetWidgetTreeAsJson(WBP);
		if (!TreeJson.IsValid())
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Widget tree is empty")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("root"), TreeJson);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Get widget tree timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleListWidgetClasses()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		TArray<TPair<FString, bool>> Classes = UAIWidgetBlueprintBuilder::GetAvailableWidgetClasses();

		TArray<TSharedPtr<FJsonValue>> ClassArray;
		for (const auto& Pair : Classes)
		{
			TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
			ClassObj->SetStringField(TEXT("name"), Pair.Key);
			ClassObj->SetBoolField(TEXT("is_panel"), Pair.Value);
			ClassArray.Add(MakeShared<FJsonValueObject>(ClassObj));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("classes"), ClassArray);
		Data->SetNumberField(TEXT("count"), Classes.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("List widget classes timed out"));
	}
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Material Builder Handlers

FString FAIExportTCPServer::HandleCreateMaterial(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName;
	FString Domain = TEXT("Surface"), BlendMode = TEXT("Opaque"), ShadingModel = TEXT("DefaultLit");
	bool bTwoSided = false;

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));

	Params->TryGetStringField(TEXT("domain"), Domain);
	Params->TryGetStringField(TEXT("blend_mode"), BlendMode);
	Params->TryGetStringField(TEXT("shading_model"), ShadingModel);
	Params->TryGetBoolField(TEXT("two_sided"), bTwoSided);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, Domain, BlendMode, ShadingModel, bTwoSided, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::CreateMaterial(PackagePath, AssetName, Domain, BlendMode, ShadingModel, bTwoSided);
		if (!Material)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create material")));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create material timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyName, Value, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::SetMaterialProperty(Material, PropertyName, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set material property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddExpression(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ExprClass, NodeName;
	double PosX = 0, PosY = 0;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("expression_class"), ExprClass))
		return CreateErrorResponse(TEXT("Missing 'expression_class'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));

	Params->TryGetNumberField(TEXT("pos_x"), PosX);
	Params->TryGetNumberField(TEXT("pos_y"), PosY);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ExprClass, NodeName, PosX, PosY, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		UMaterialExpression* Expr = UAIMaterialBuilder::AddExpression(Material, ExprClass, NodeName, (int32)PosX, (int32)PosY);
		if (!Expr) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add expression '%s'"), *ExprClass))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
		Data->SetNumberField(TEXT("pos_x"), PosX);
		Data->SetNumberField(TEXT("pos_y"), PosY);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add expression timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetExpressionProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, PropertyName, Value, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::SetExpressionProperty(Material, NodeName, PropertyName, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set expression property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleConnectExpressions(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromOutput, ToNode, ToInput;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("from_node"), FromNode))
		return CreateErrorResponse(TEXT("Missing 'from_node'"));
	if (!Params->TryGetStringField(TEXT("from_output"), FromOutput))
		FromOutput = TEXT("");
	if (!Params->TryGetStringField(TEXT("to_node"), ToNode))
		return CreateErrorResponse(TEXT("Missing 'to_node'"));
	if (!Params->TryGetStringField(TEXT("to_input"), ToInput))
		ToInput = TEXT("");

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromOutput, ToNode, ToInput, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::ConnectExpressions(Material, FromNode, FromOutput, ToNode, ToInput);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect expressions timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleConnectToMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromOutput, MaterialProperty;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("from_node"), FromNode))
		return CreateErrorResponse(TEXT("Missing 'from_node'"));
	if (!Params->TryGetStringField(TEXT("from_output"), FromOutput))
		FromOutput = TEXT("");
	if (!Params->TryGetStringField(TEXT("material_property"), MaterialProperty))
		return CreateErrorResponse(TEXT("Missing 'material_property'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromOutput, MaterialProperty, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::ConnectToMaterialProperty(Material, FromNode, FromOutput, MaterialProperty);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect to material property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleDisconnectInput(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, InputName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));
	if (!Params->TryGetStringField(TEXT("input_name"), InputName))
		return CreateErrorResponse(TEXT("Missing 'input_name'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, InputName, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::DisconnectInput(Material, NodeName, InputName);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Disconnect input timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveExpression(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::RemoveExpression(Material, NodeName);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove expression timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleCompileMaterial(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		TArray<FString> Warnings;
		bool bCompiled = UAIMaterialBuilder::CompileMaterial(Material, &Warnings);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("compiled"), bCompiled);
		Data->SetBoolField(TEXT("saved"), true);

		if (Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarnArray;
			for (const FString& W : Warnings)
			{
				WarnArray.Add(MakeShared<FJsonValueString>(W));
			}
			Data->SetArrayField(TEXT("warnings"), WarnArray);
		}
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Compile material timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIMaterialBuilder::GetMaterialGraphAsJson(Material);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get material graph timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleListExpressionClasses()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		TArray<FString> Classes = UAIMaterialBuilder::GetAvailableExpressionClasses();

		TArray<TSharedPtr<FJsonValue>> ClassArray;
		for (const FString& ClassName : Classes)
		{
			ClassArray.Add(MakeShared<FJsonValueString>(ClassName));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("classes"), ClassArray);
		Data->SetNumberField(TEXT("count"), Classes.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List expression classes timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleCreateMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, ParentMaterialPath;
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path'"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name'"));
	if (!Params->TryGetStringField(TEXT("parent_material_path"), ParentMaterialPath))
		return CreateErrorResponse(TEXT("Missing 'parent_material_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, ParentMaterialPath, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::CreateMaterialInstance(PackagePath, AssetName, ParentMaterialPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to create material instance"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), MIC->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create material instance timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetInstanceParameter(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ParamName, ParamType, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
		return CreateErrorResponse(TEXT("Missing 'param_name'"));
	if (!Params->TryGetStringField(TEXT("param_type"), ParamType))
		return CreateErrorResponse(TEXT("Missing 'param_type'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ParamName, ParamType, Value, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::LoadMaterialInstance(AssetPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("MIC not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::SetInstanceParameter(MIC, ParamName, ParamType, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set instance parameter timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSaveMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::LoadMaterialInstance(AssetPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("MIC not found: %s"), *AssetPath))); return; }

		bool bSaved = UAIMaterialBuilder::SaveMaterialInstance(MIC);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), bSaved);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Save material instance timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetMaterialInstanceInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::LoadMaterialInstance(AssetPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("MIC not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIMaterialBuilder::GetMaterialInstanceInfoAsJson(MIC);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get material instance info timed out"));
	return Future.Get();
}

// =============================================================================
// DATA ASSET COMMANDS
// =============================================================================

FString FAIExportTCPServer::HandleSaveDataAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSaved = UAIDataAssetBuilder::SaveAsset(Asset);
		if (!bSaved)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Save data asset timed out"));
	return Future.Get();
}

// =============================================================================
// Asset Import Commands
// =============================================================================

FString FAIExportTCPServer::HandleImportTexture(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString SourcePath, PackagePath, AssetName;
	FString Compression = TEXT("UserInterface2D");
	FString MipGen = TEXT("NoMipmaps");
	FString LODGroup = TEXT("UI");
	bool bSRGB = true;

	if (!Params->TryGetStringField(TEXT("source_path"), SourcePath))
		return CreateErrorResponse(TEXT("Missing 'source_path' parameter"));
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));

	Params->TryGetStringField(TEXT("asset_name"), AssetName);
	Params->TryGetStringField(TEXT("compression"), Compression);
	Params->TryGetStringField(TEXT("mip_gen"), MipGen);
	Params->TryGetStringField(TEXT("lod_group"), LODGroup);
	Params->TryGetBoolField(TEXT("srgb"), bSRGB);

	// Normalize path separators
	SourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Verify source file exists
	if (!FPaths::FileExists(SourcePath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Source file not found: %s"), *SourcePath));
	}

	// Derive asset name from filename if not provided
	if (AssetName.IsEmpty())
	{
		AssetName = FPaths::GetBaseFilename(SourcePath);
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [SourcePath, PackagePath, AssetName, Compression, MipGen, LODGroup, bSRGB, Promise, this]()
	{
		// Build full package name
		FString FullPackagePath = PackagePath / AssetName;

		// Create package
		UPackage* Package = CreatePackage(*FullPackagePath);
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *FullPackagePath)));
			return;
		}
		Package->FullyLoad();

		// Read file data
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *SourcePath))
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to read file: %s"), *SourcePath)));
			return;
		}

		// Create texture factory
		UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
		TextureFactory->AddToRoot(); // Prevent GC during import
		TextureFactory->SuppressImportOverwriteDialog();

		// Import
		const uint8* DataPtr = FileData.GetData();
		UObject* ImportedObject = TextureFactory->FactoryCreateBinary(
			UTexture2D::StaticClass(),
			Package,
			*AssetName,
			RF_Public | RF_Standalone,
			nullptr,
			*FPaths::GetExtension(SourcePath),
			DataPtr,
			DataPtr + FileData.Num(),
			GWarn
		);

		TextureFactory->RemoveFromRoot();

		UTexture2D* Texture = Cast<UTexture2D>(ImportedObject);
		if (!Texture)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to import texture")));
			return;
		}

		// Apply compression settings
		if (Compression == TEXT("Default"))
			Texture->CompressionSettings = TC_Default;
		else if (Compression == TEXT("NormalMap") || Compression == TEXT("Normalmap"))
			Texture->CompressionSettings = TC_Normalmap;
		else if (Compression == TEXT("Masks"))
			Texture->CompressionSettings = TC_Masks;
		else if (Compression == TEXT("Grayscale") || Compression == TEXT("Displacementmap"))
			Texture->CompressionSettings = TC_Displacementmap;
		else if (Compression == TEXT("HDR"))
			Texture->CompressionSettings = TC_HDR;
		else if (Compression == TEXT("UserInterface2D") || Compression == TEXT("UI"))
			Texture->CompressionSettings = TC_EditorIcon;
		else if (Compression == TEXT("Alpha"))
			Texture->CompressionSettings = TC_Alpha;
		else
			Texture->CompressionSettings = TC_EditorIcon; // Default to UI

		// Apply sRGB
		Texture->SRGB = bSRGB;

		// Apply MipGen settings
		if (MipGen == TEXT("NoMipmaps"))
			Texture->MipGenSettings = TMGS_NoMipmaps;
		else if (MipGen == TEXT("FromTextureGroup"))
			Texture->MipGenSettings = TMGS_FromTextureGroup;
		else if (MipGen == TEXT("Sharpen0"))
			Texture->MipGenSettings = TMGS_Sharpen0;
		else if (MipGen == TEXT("Sharpen"))
			Texture->MipGenSettings = TMGS_Sharpen0;
		else if (MipGen == TEXT("Blur"))
			Texture->MipGenSettings = TMGS_Blur1;
		else
			Texture->MipGenSettings = TMGS_NoMipmaps;

		// Apply LOD Group
		if (LODGroup == TEXT("World"))
			Texture->LODGroup = TEXTUREGROUP_World;
		else if (LODGroup == TEXT("WorldNormalMap"))
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
		else if (LODGroup == TEXT("WorldSpecular"))
			Texture->LODGroup = TEXTUREGROUP_WorldSpecular;
		else if (LODGroup == TEXT("Character"))
			Texture->LODGroup = TEXTUREGROUP_Character;
		else if (LODGroup == TEXT("CharacterNormalMap"))
			Texture->LODGroup = TEXTUREGROUP_CharacterNormalMap;
		else if (LODGroup == TEXT("Effects"))
			Texture->LODGroup = TEXTUREGROUP_Effects;
		else if (LODGroup == TEXT("UI"))
			Texture->LODGroup = TEXTUREGROUP_UI;
		else if (LODGroup == TEXT("Lightmap"))
			Texture->LODGroup = TEXTUREGROUP_Lightmap;
		else if (LODGroup == TEXT("Shadowmap"))
			Texture->LODGroup = TEXTUREGROUP_Shadowmap;
		else
			Texture->LODGroup = TEXTUREGROUP_UI;

		// Apply changes and save
		Texture->PostEditChange();
		Texture->UpdateResource();
		Package->MarkPackageDirty();

		// Save the package
		FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs);

		// Notify asset registry
		FAssetRegistryModule::AssetCreated(Texture);

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Texture->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetNumberField(TEXT("width"), Texture->GetSizeX());
		Data->SetNumberField(TEXT("height"), Texture->GetSizeY());
		Data->SetStringField(TEXT("format"), UEnum::GetValueAsString(Texture->GetPixelFormat()));
		Data->SetBoolField(TEXT("saved"), bSaved);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Import texture timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleImportFont(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, FontName;
	FString Hinting = TEXT("Auto");

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("font_name"), FontName))
		return CreateErrorResponse(TEXT("Missing 'font_name' parameter"));

	Params->TryGetStringField(TEXT("hinting"), Hinting);

	// Parse faces array
	const TArray<TSharedPtr<FJsonValue>>* FacesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("faces"), FacesArray) || !FacesArray || FacesArray->Num() == 0)
	{
		return CreateErrorResponse(TEXT("Missing or empty 'faces' array. Each entry needs 'source_path' and 'name' (e.g. 'Regular', 'Bold')."));
	}

	// Validate all face entries before processing
	struct FFaceEntry
	{
		FString SourcePath;
		FString Name;
	};
	TArray<FFaceEntry> Faces;

	for (const auto& FaceValue : *FacesArray)
	{
		const TSharedPtr<FJsonObject>* FaceObj = nullptr;
		if (!FaceValue->TryGetObject(FaceObj) || !FaceObj || !(*FaceObj).IsValid())
		{
			return CreateErrorResponse(TEXT("Each face entry must be a JSON object with 'source_path' and 'name'"));
		}

		FFaceEntry Entry;
		if (!(*FaceObj)->TryGetStringField(TEXT("source_path"), Entry.SourcePath))
			return CreateErrorResponse(TEXT("Face entry missing 'source_path'"));
		if (!(*FaceObj)->TryGetStringField(TEXT("name"), Entry.Name))
			return CreateErrorResponse(TEXT("Face entry missing 'name' (e.g. 'Regular', 'Bold', 'Medium')"));

		Entry.SourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (!FPaths::FileExists(Entry.SourcePath))
			return CreateErrorResponse(FString::Printf(TEXT("Font file not found: %s"), *Entry.SourcePath));

		Faces.Add(MoveTemp(Entry));
	}

	// Resolve hinting enum
	EFontHinting HintingEnum = EFontHinting::Auto;
	if (Hinting == TEXT("None"))
		HintingEnum = EFontHinting::None;
	else if (Hinting == TEXT("Auto"))
		HintingEnum = EFontHinting::Auto;
	else if (Hinting == TEXT("AutoLight"))
		HintingEnum = EFontHinting::AutoLight;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, FontName, Faces, HintingEnum, Promise, this]()
	{
		TArray<TSharedPtr<FJsonObject>> FaceResults;

		// Step 1: Create UFontFace for each TTF/OTF
		TArray<UFontFace*> FontFaceAssets;
		for (const auto& Face : Faces)
		{
			FString FaceName = FontName + TEXT("-") + Face.Name;
			FString FacePackagePath = PackagePath / FaceName;

			UPackage* FacePackage = CreatePackage(*FacePackagePath);
			if (!FacePackage)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create package for font face: %s"), *FaceName)));
				return;
			}
			FacePackage->FullyLoad();

			// Read font file data
			TArray<uint8> FontData;
			if (!FFileHelper::LoadFileToArray(FontData, *Face.SourcePath))
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to read font file: %s"), *Face.SourcePath)));
				return;
			}

			// Create UFontFace
			UFontFace* FontFace = NewObject<UFontFace>(FacePackage, *FaceName, RF_Public | RF_Standalone);
			FontFace->SourceFilename = Face.SourcePath;
			FontFace->Hinting = HintingEnum;
			FontFace->LoadingPolicy = EFontLoadingPolicy::Inline;

			// Load font data into the asset
			FontFace->FontFaceData = MakeShared<FFontFaceData, ESPMode::ThreadSafe>(MoveTemp(FontData));

			FontFace->PostEditChange();
			FacePackage->MarkPackageDirty();

			// Save FontFace
			FString FaceFilename = FPackageName::LongPackageNameToFilename(FacePackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(FacePackage, FontFace, *FaceFilename, SaveArgs);

			FAssetRegistryModule::AssetCreated(FontFace);
			FontFaceAssets.Add(FontFace);

			// Track result
			TSharedPtr<FJsonObject> FaceResult = MakeShared<FJsonObject>();
			FaceResult->SetStringField(TEXT("name"), Face.Name);
			FaceResult->SetStringField(TEXT("asset_path"), FontFace->GetPathName());
			FaceResults.Add(FaceResult);
		}

		// Step 2: Create Composite UFont
		FString CompositePath = PackagePath / FontName;
		UPackage* FontPackage = CreatePackage(*CompositePath);
		if (!FontPackage)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create composite font package")));
			return;
		}
		FontPackage->FullyLoad();

		UFont* CompositeFont = NewObject<UFont>(FontPackage, *FontName, RF_Public | RF_Standalone);
		CompositeFont->FontCacheType = EFontCacheType::Runtime;

		// Build typeface entries
		FTypeface& DefaultTypeface = CompositeFont->GetMutableInternalCompositeFont().DefaultTypeface;
		DefaultTypeface.Fonts.Empty();

		for (int32 i = 0; i < FontFaceAssets.Num(); ++i)
		{
			FTypefaceEntry& Entry = DefaultTypeface.Fonts.AddDefaulted_GetRef();
			Entry.Name = *Faces[i].Name;
			Entry.Font = FFontData(FontFaceAssets[i]);
		}

		CompositeFont->PostEditChange();
		FontPackage->MarkPackageDirty();

		// Save composite font
		FString FontFilename = FPackageName::LongPackageNameToFilename(CompositePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(FontPackage, CompositeFont, *FontFilename, SaveArgs);

		FAssetRegistryModule::AssetCreated(CompositeFont);

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("font_asset_path"), CompositeFont->GetPathName());
		Data->SetStringField(TEXT("font_name"), FontName);
		Data->SetNumberField(TEXT("face_count"), FontFaceAssets.Num());
		Data->SetBoolField(TEXT("saved"), bSaved);

		TArray<TSharedPtr<FJsonValue>> FaceResultValues;
		for (const auto& FR : FaceResults)
		{
			FaceResultValues.Add(MakeShared<FJsonValueObject>(FR));
		}
		Data->SetArrayField(TEXT("faces"), FaceResultValues);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Import font timed out"));
	return Future.Get();
}

// =============================================================================
// CDO PROPERTY HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleSetCDOProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyName, Value, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		if (WBP)
		{
			bool bSuccess = UAIWidgetBlueprintBuilder::SetCDOProperty(WBP, PropertyName, Value);
			if (!bSuccess)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set CDO property '%s'"), *PropertyName)));
				return;
			}
		}
		else
		{
			// Non-WBP path: if Asset is a Blueprint (e.g. CommonButtonStyle subclass),
			// redirect writes to its generated class CDO so we can set parent-class
			// properties like NormalBase / NormalHovered (not present on the BP itself).
			UObject* TargetForSet = Asset;
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				UClass* GenClass = BP->GeneratedClass;
				if (!GenClass)
				{
					FKismetEditorUtilities::CompileBlueprint(BP);
					GenClass = BP->GeneratedClass;
				}
				if (!GenClass)
				{
					Promise->SetValue(CreateErrorResponse(TEXT("Blueprint has no GeneratedClass")));
					return;
				}
				TargetForSet = GenClass->GetDefaultObject();
				if (!TargetForSet)
				{
					Promise->SetValue(CreateErrorResponse(TEXT("Blueprint generated class has no CDO")));
					return;
				}
			}

			bool bSuccess = UAIDataAssetBuilder::SetProperty(TargetForSet, PropertyName, Value);
			if (!bSuccess)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s'"), *PropertyName)));
				return;
			}
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Data->SetStringField(TEXT("value"), Value);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set CDO property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetCDOProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		if (WBP)
		{
			TSharedPtr<FJsonObject> PropsJson = UAIWidgetBlueprintBuilder::GetCDOPropertiesAsJson(WBP);
			Promise->SetValue(CreateSuccessResponse(PropsJson));
		}
		else
		{
			// Non-WBP path: if Asset is a Blueprint, read properties from its CDO so
			// inherited fields (e.g. CommonButtonStyle::NormalBase) are visible.
			UObject* TargetForRead = Asset;
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				UClass* GenClass = BP->GeneratedClass;
				if (!GenClass)
				{
					FKismetEditorUtilities::CompileBlueprint(BP);
					GenClass = BP->GeneratedClass;
				}
				if (GenClass && GenClass->GetDefaultObject())
				{
					TargetForRead = GenClass->GetDefaultObject();
				}
			}
			TSharedPtr<FJsonObject> PropsJson = UAIDataAssetBuilder::GetPropertiesAsJson(TargetForRead);
			Promise->SetValue(CreateSuccessResponse(PropsJson));
		}
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get CDO properties timed out"));
	return Future.Get();
}

// =============================================================================
// CDO ARRAY PROPERTY HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleAddCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName, ElementValuesJson, ClassName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	Params->TryGetStringField(TEXT("element_values"), ElementValuesJson);
	Params->TryGetStringField(TEXT("class_name"), ClassName);

	// Parse element_values JSON string into a map
	TMap<FString, FString> ElementValues;
	if (!ElementValuesJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ElementValuesJson);
		TSharedPtr<FJsonObject> ValuesObj;
		if (FJsonSerializer::Deserialize(Reader, ValuesObj) && ValuesObj.IsValid())
		{
			for (const auto& Pair : ValuesObj->Values)
			{
				FString Val;
				if (Pair.Value->TryGetString(Val))
				{
					ElementValues.Add(Pair.Key, Val);
				}
			}
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementValues, ClassName, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		int32 NewIndex = -1;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass)
			{
				FKismetEditorUtilities::CompileBlueprint(WBP);
				GenClass = WBP->GeneratedClass;
			}
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }

			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			NewIndex = UAIWidgetBlueprintBuilder::AddArrayElement(CDO, ArrayName, ElementValues, ClassName);
		}
		else if (BP)
		{
			// Non-WBP Blueprint (BPTYPE_Const data assets, etc.)
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass)
			{
				FKismetEditorUtilities::CompileBlueprint(BP);
				GenClass = BP->GeneratedClass;
			}
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }

			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			NewIndex = UAIWidgetBlueprintBuilder::AddArrayElement(CDO, ArrayName, ElementValues, ClassName);
		}
		else
		{
			NewIndex = UAIDataAssetBuilder::AddArrayElement(Asset, ArrayName, ElementValues, ClassName);
		}

		if (NewIndex < 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add element to '%s'"), *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("index"), NewIndex);
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add CDO array element timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetCDOArrayElementProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName, PropertyName, Value;
	double Index = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return CreateErrorResponse(TEXT("Missing 'index' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));

	int32 ElementIndex = (int32)Index;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementIndex, PropertyName, Value, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = false;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::SetArrayElementProperty(CDO, ArrayName, ElementIndex, PropertyName, Value);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::SetArrayElementProperty(CDO, ArrayName, ElementIndex, PropertyName, Value);
		}
		else
		{
			bSuccess = UAIDataAssetBuilder::SetArrayElementProperty(Asset, ArrayName, ElementIndex, PropertyName, Value);
		}

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set '%s' on element %d of '%s'"),
				*PropertyName, ElementIndex, *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("index"), ElementIndex);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set CDO array element property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName;
	double Index = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return CreateErrorResponse(TEXT("Missing 'index' parameter"));

	int32 ElementIndex = (int32)Index;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementIndex, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = false;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::RemoveArrayElement(CDO, ArrayName, ElementIndex);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::RemoveArrayElement(CDO, ArrayName, ElementIndex);
		}
		else
		{
			bSuccess = UAIDataAssetBuilder::RemoveArrayElement(Asset, ArrayName, ElementIndex);
		}

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove element %d from '%s'"),
				ElementIndex, *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("removed_index"), ElementIndex);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove CDO array element timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetCDOArrayLength(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		int32 Length = -1;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			Length = UAIWidgetBlueprintBuilder::GetArrayLength(CDO, ArrayName);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			Length = UAIWidgetBlueprintBuilder::GetArrayLength(CDO, ArrayName);
		}
		else
		{
			Length = UAIDataAssetBuilder::GetArrayLength(Asset, ArrayName);
		}

		if (Length < 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Array '%s' not found"), *ArrayName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("length"), Length);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get CDO array length timed out"));
	return Future.Get();
}

// =============================================================================
// BLUEPRINT GRAPH HANDLERS
// =============================================================================

// Helper macro for graph node creation handlers (they all follow the same pattern)
#define GRAPH_NODE_HANDLER_BODY(HandlerName, BuilderCall)                                      \
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));        \
	FString AssetPath, NodeName;                                                               \
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))                             \
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));                    \
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))                               \
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));                     \
	FString GraphName;                                                                         \
	Params->TryGetStringField(TEXT("graph_name"), GraphName);                                  \
	double PosX = 0, PosY = 0;                                                                \
	Params->TryGetNumberField(TEXT("pos_x"), PosX);                                            \
	Params->TryGetNumberField(TEXT("pos_y"), PosY);

FString FAIExportTCPServer::HandleAddEventNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddEventNode, AddEventNode)

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddEventNode(BP, EventName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Data->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add event node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddCustomEvent(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddCustomEvent, AddCustomEvent)

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddCustomEvent(BP, EventName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add custom event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add custom event timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddFunctionCallNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddFunctionCallNode, AddFunctionCallNode)

	FString FunctionName, TargetClass;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	Params->TryGetStringField(TEXT("target_class"), TargetClass);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, TargetClass, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddFunctionCallNode(BP, FunctionName, NodeName, TargetClass, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add function call '%s'"), *FunctionName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add function call timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddVariableGetNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddVariableGetNode, AddVariableGetNode)

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
		return CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddVariableGetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Get '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable get node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddVariableSetNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddVariableSetNode, AddVariableSetNode)

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
		return CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddVariableSetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Set '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable set node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddMakeStructNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddMakeStructNode, AddMakeStructNode)

	FString StructName;
	if (!Params->TryGetStringField(TEXT("struct_name"), StructName))
		return CreateErrorResponse(TEXT("Missing 'struct_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, StructName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddMakeStructNode(BP, StructName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add MakeStruct '%s'"), *StructName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("struct_name"), StructName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add make struct node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddBranchNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddBranchNode, AddBranchNode)

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddBranchNode(BP, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add branch node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add branch node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddCallParentFunction(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddCallParentFunction, AddCallParentFunctionNode)

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddCallParentFunctionNode(BP, FunctionName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add call parent function node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Data->SetStringField(TEXT("node_class"), TEXT("K2Node_CallParentFunction"));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add call parent function timed out"));
	return Future.Get();
}

namespace
{
static TArray<FAIBlueprintGraphPinSpec> ParseGraphPinSpecs(
	const TSharedPtr<FJsonObject>& Params,
	const FString& FieldName)
{
	TArray<FAIBlueprintGraphPinSpec> Specs;

	const TArray<TSharedPtr<FJsonValue>>* PinArray = nullptr;
	if (!Params.IsValid() || !Params->TryGetArrayField(FieldName, PinArray) || !PinArray)
	{
		return Specs;
	}

	for (const TSharedPtr<FJsonValue>& Value : *PinArray)
	{
		const TSharedPtr<FJsonObject>* PinObj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(PinObj) || !PinObj || !PinObj->IsValid())
		{
			continue;
		}

		FAIBlueprintGraphPinSpec Spec;
		(*PinObj)->TryGetStringField(TEXT("name"), Spec.Name);
		(*PinObj)->TryGetStringField(TEXT("type"), Spec.Type);
		(*PinObj)->TryGetStringField(TEXT("default_value"), Spec.DefaultValue);
		if (Spec.DefaultValue.IsEmpty())
		{
			(*PinObj)->TryGetStringField(TEXT("default"), Spec.DefaultValue);
		}

		if (!Spec.Name.IsEmpty())
		{
			Specs.Add(MoveTemp(Spec));
		}
	}

	return Specs;
}
}

FString FAIExportTCPServer::HandleEnsureFunctionGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FunctionName, EntryNodeName, ResultNodeName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	Params->TryGetStringField(TEXT("entry_node_name"), EntryNodeName);
	Params->TryGetStringField(TEXT("result_node_name"), ResultNodeName);

	TArray<FAIBlueprintGraphPinSpec> Inputs = ParseGraphPinSpecs(Params, TEXT("inputs"));
	TArray<FAIBlueprintGraphPinSpec> Outputs = ParseGraphPinSpecs(Params, TEXT("outputs"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, Inputs, Outputs, EntryNodeName, ResultNodeName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath)));
			return;
		}

		UEdGraph* Graph = UAIBlueprintGraphBuilder::EnsureFunctionGraph(
			BP,
			FunctionName,
			Inputs,
			Outputs,
			EntryNodeName,
			ResultNodeName);
		if (!Graph)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to ensure function graph '%s'"), *FunctionName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("graph_name"), Graph->GetName());
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetNumberField(TEXT("input_count"), Inputs.Num());
		Data->SetNumberField(TEXT("output_count"), Outputs.Num());
		Data->SetStringField(TEXT("entry_node_name"), EntryNodeName.IsEmpty() ? FString::Printf(TEXT("%s_Entry"), *FunctionName) : EntryNodeName);
		if (Outputs.Num() > 0)
		{
			Data->SetStringField(TEXT("result_node_name"), ResultNodeName.IsEmpty() ? FString::Printf(TEXT("%s_Result"), *FunctionName) : ResultNodeName);
		}
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Ensure function graph timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleConnectPins(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromPin, ToNode, ToPin, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("from_node"), FromNode))
		return CreateErrorResponse(TEXT("Missing 'from_node' parameter"));
	if (!Params->TryGetStringField(TEXT("from_pin"), FromPin))
		return CreateErrorResponse(TEXT("Missing 'from_pin' parameter"));
	if (!Params->TryGetStringField(TEXT("to_node"), ToNode))
		return CreateErrorResponse(TEXT("Missing 'to_node' parameter"));
	if (!Params->TryGetStringField(TEXT("to_pin"), ToPin))
		return CreateErrorResponse(TEXT("Missing 'to_pin' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromPin, ToNode, ToPin, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::ConnectPins(BP, FromNode, FromPin, ToNode, ToPin, GraphName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to connect %s.%s -> %s.%s"),
				*FromNode, *FromPin, *ToNode, *ToPin)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("from_node"), FromNode);
		Data->SetStringField(TEXT("from_pin"), FromPin);
		Data->SetStringField(TEXT("to_node"), ToNode);
		Data->SetStringField(TEXT("to_pin"), ToPin);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect pins timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetPinDefault(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, PinName, DefaultValue, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));
	if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
		return CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
		return CreateErrorResponse(TEXT("Missing 'default_value' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, PinName, DefaultValue, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::SetPinDefaultValue(BP, NodeName, PinName, DefaultValue, GraphName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set pin default %s.%s"),
				*NodeName, *PinName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("pin_name"), PinName);
		Data->SetStringField(TEXT("default_value"), DefaultValue);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set pin default timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::RemoveNode(BP, NodeName, GraphName);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Node '%s' not found"), *NodeName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("removed"), NodeName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove graph node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	if (GraphName.IsEmpty()) GraphName = TEXT("EventGraph");

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> GraphJson = UAIBlueprintGraphBuilder::GetGraphAsJson(BP, GraphName);
		Promise->SetValue(CreateSuccessResponse(GraphJson));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get graph timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleListGraphs(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TArray<FString> Graphs = UAIBlueprintGraphBuilder::ListGraphs(BP);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> GraphArray;
		for (const FString& Name : Graphs)
		{
			GraphArray.Add(MakeShared<FJsonValueString>(Name));
		}
		Data->SetArrayField(TEXT("graphs"), GraphArray);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List graphs timed out"));
	return Future.Get();
}

// =============================================================================
// BLUEPRINT VARIABLE HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleAddVariable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName, VarType, Category;
	bool bInstanceEditable = false;
	bool bBlueprintReadOnly = false;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));
	if (!Params->TryGetStringField(TEXT("var_type"), VarType))
		return CreateErrorResponse(TEXT("Missing 'var_type' parameter"));
	Params->TryGetBoolField(TEXT("instance_editable"), bInstanceEditable);
	Params->TryGetBoolField(TEXT("blueprint_read_only"), bBlueprintReadOnly);
	Params->TryGetStringField(TEXT("category"), Category);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, VarType, bInstanceEditable, bBlueprintReadOnly, Category, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::AddVariable(BP, VarName, VarType, bInstanceEditable, bBlueprintReadOnly, Category);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add variable '%s'"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("var_name"), VarName);
		Data->SetStringField(TEXT("var_type"), VarType);
		Data->SetBoolField(TEXT("blueprint_read_only"), bBlueprintReadOnly);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetVariableDefault(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName, DefaultValue;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
		return CreateErrorResponse(TEXT("Missing 'default_value' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, DefaultValue, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::SetVariableDefault(BP, VarName, DefaultValue);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set default for '%s'"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("var_name"), VarName);
		Data->SetStringField(TEXT("default_value"), DefaultValue);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set variable default timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveVariable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::RemoveVariable(BP, VarName);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("removed"), VarName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove variable timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetVariables(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> VarsJson = UAIBlueprintGraphBuilder::GetVariablesAsJson(BP);
		Promise->SetValue(CreateSuccessResponse(VarsJson));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get variables timed out"));
	return Future.Get();
}

// =============================================================================
// Generic Asset Factory Command Handlers
// =============================================================================

namespace
{
	static FName NormalizePackageName(const FString& InAssetPath);
}

FString FAIExportTCPServer::HandleCreateAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, AssetType;
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_type"), AssetType))
		return CreateErrorResponse(TEXT("Missing 'asset_type' parameter"));

	// Parse optional initial properties
	TMap<FString, FString> InitialProperties;
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FString Val;
			if (Pair.Value->TryGetString(Val))
			{
				InitialProperties.Add(Pair.Key, Val);
			}
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, AssetType, InitialProperties, Promise, this]()
	{
		UObject* Asset = UAIAssetFactory::CreateAsset(PackagePath, AssetName, AssetType, InitialProperties);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create %s"), *AssetType)));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("asset_type"), AssetType);
		Data->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create asset timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetAssetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyPath, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath))
		return CreateErrorResponse(TEXT("Missing 'property_path'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyPath, Value, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIDataAssetBuilder::SetProperty(Asset, PropertyPath, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Data->SetStringField(TEXT("property_path"), PropertyPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set asset property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetAssetProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Props = UAIDataAssetBuilder::GetPropertiesAsJson(Asset);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Data->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Data->SetObjectField(TEXT("properties"), Props);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get asset properties timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAssetExists(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		FAssetData AssetData = AR.GetAssetByObjectPath(FSoftObjectPath(PackageName.ToString() + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName.ToString())));

		if (!AssetData.IsValid())
		{
			TArray<FAssetData> PackageAssets;
			AR.GetAssetsByPackageName(PackageName, PackageAssets);
			if (PackageAssets.Num() > 0)
			{
				AssetData = PackageAssets[0];
			}
		}

		FString PackageFilename;
		const bool bPackageExistsOnDisk = FPackageName::DoesPackageExist(PackageName.ToString(), &PackageFilename);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetBoolField(TEXT("exists"), AssetData.IsValid() || bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("asset_registry_valid"), AssetData.IsValid());
		Data->SetBoolField(TEXT("package_exists_on_disk"), bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetStringField(TEXT("package_filename"), PackageFilename);
		if (AssetData.IsValid())
		{
			Data->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
			Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
			Data->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
		}
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Asset exists timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleScanAssetPaths(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	TArray<FString> Paths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("paths"), PathsArray) && PathsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *PathsArray)
		{
			FString PathValue;
			if (Value.IsValid() && Value->TryGetString(PathValue) && !PathValue.IsEmpty())
			{
				Paths.Add(PathValue);
			}
		}
	}
	else
	{
		FString SinglePath;
		if (Params->TryGetStringField(TEXT("path"), SinglePath) && !SinglePath.IsEmpty())
		{
			Paths.Add(SinglePath);
		}
	}

	if (Paths.Num() == 0)
	{
		return CreateErrorResponse(TEXT("Missing 'paths' array or 'path' parameter"));
	}

	bool bForceRescan = false;
	Params->TryGetBoolField(TEXT("force_rescan"), bForceRescan);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Paths, bForceRescan, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		AR.ScanPathsSynchronous(Paths, bForceRescan);

		TArray<TSharedPtr<FJsonValue>> PathValues;
		PathValues.Reserve(Paths.Num());
		for (const FString& Path : Paths)
		{
			PathValues.Add(MakeShared<FJsonValueString>(Path));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("paths"), PathValues);
		Data->SetNumberField(TEXT("count"), Paths.Num());
		Data->SetBoolField(TEXT("force_rescan"), bForceRescan);
		Data->SetBoolField(TEXT("scanned"), true);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Scan asset paths timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAssetSearch(TSharedPtr<FJsonObject> Params)
{
	FString Path = TEXT("/Game");
	FString NameFilter;
	FString ClassFilter;
	bool bRecursive = true;
	int32 Offset = 0;
	int32 Limit = 100;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("recursive"), bRecursive);

		double NumberValue = 0.0;
		if (Params->TryGetNumberField(TEXT("offset"), NumberValue) && NumberValue > 0.0)
		{
			Offset = static_cast<int32>(NumberValue);
		}
		if (Params->TryGetNumberField(TEXT("limit"), NumberValue) && NumberValue > 0.0)
		{
			Limit = FMath::Clamp(static_cast<int32>(NumberValue), 1, 1000);
		}
	}
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Path, NameFilter, ClassFilter, bRecursive, Offset, Limit, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Path));
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<TSharedPtr<FJsonValue>> Results;
		int32 MatchedCount = 0;
		for (const FAssetData& AssetData : Assets)
		{
			const FString AssetName = AssetData.AssetName.ToString();
			const FString PackageName = AssetData.PackageName.ToString();
			const FString ClassPath = AssetData.AssetClassPath.ToString();
			if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter, ESearchCase::IgnoreCase) && !PackageName.Contains(NameFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}

			if (MatchedCount >= Offset && Results.Num() < Limit)
			{
				Results.Add(MakeShared<FJsonValueObject>(BuildAssetDataJson(AssetData)));
			}
			++MatchedCount;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("path"), Path);
		Data->SetStringField(TEXT("name_filter"), NameFilter);
		Data->SetStringField(TEXT("class_filter"), ClassFilter);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("offset"), Offset);
		Data->SetNumberField(TEXT("limit"), Limit);
		Data->SetNumberField(TEXT("returned_count"), Results.Num());
		Data->SetNumberField(TEXT("matched_count"), MatchedCount);
		Data->SetBoolField(TEXT("truncated"), MatchedCount > Offset + Results.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("assets"), Results);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Asset search timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAssetValidateLight(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FAssetData> PackageAssets;
		AR.GetAssetsByPackageName(PackageName, PackageAssets);

		FString PackageFilename;
		const bool bPackageExistsOnDisk = FPackageName::DoesPackageExist(PackageName.ToString(), &PackageFilename);

		TArray<FName> Dependencies;
		TArray<FName> Referencers;
		AR.GetDependencies(PackageName, Dependencies);
		AR.GetReferencers(PackageName, Referencers);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		TArray<TSharedPtr<FJsonValue>> ExternalDependencies;
		if (PackageAssets.Num() == 0 && !bPackageExistsOnDisk)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("asset_missing")));
		}
		if (bScanIncomplete)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("asset_registry_scan_was_incomplete")));
		}

		for (const FAssetData& AssetData : PackageAssets)
		{
			if (AssetData.AssetClassPath.ToString().Contains(TEXT("ObjectRedirector")))
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("asset_is_redirector")));
				break;
			}
		}

		for (const FName& Dependency : Dependencies)
		{
			const FString DependencyPath = Dependency.ToString();
			if (!DependencyPath.StartsWith(TEXT("/Game/")) && !DependencyPath.StartsWith(TEXT("/Engine/")) && !DependencyPath.StartsWith(TEXT("/Script/")))
			{
				ExternalDependencies.Add(MakeShared<FJsonValueString>(DependencyPath));
			}
		}
		if (ExternalDependencies.Num() > 0)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("has_non_project_engine_script_dependencies")));
		}

		TArray<TSharedPtr<FJsonValue>> AssetArray;
		for (const FAssetData& AssetData : PackageAssets)
		{
			AssetArray.Add(MakeShared<FJsonValueObject>(BuildAssetDataJson(AssetData)));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetStringField(TEXT("package_filename"), PackageFilename);
		Data->SetBoolField(TEXT("valid"), Warnings.Num() == 0);
		Data->SetBoolField(TEXT("exists"), PackageAssets.Num() > 0 || bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("asset_registry_valid"), PackageAssets.Num() > 0);
		Data->SetBoolField(TEXT("package_exists_on_disk"), bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetNumberField(TEXT("assets_in_package_count"), PackageAssets.Num());
		Data->SetNumberField(TEXT("dependency_count"), Dependencies.Num());
		Data->SetNumberField(TEXT("referencer_count"), Referencers.Num());
		Data->SetNumberField(TEXT("external_dependency_count"), ExternalDependencies.Num());
		Data->SetArrayField(TEXT("assets"), AssetArray);
		Data->SetArrayField(TEXT("external_dependencies"), ExternalDependencies);
		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Asset validation timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSaveAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		bool bSaved = UAIDataAssetBuilder::SaveAsset(Asset);
		if (!bSaved) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to save: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Save asset timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRenameAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	// Either or both may be provided. Empty = keep current value.
	FString NewPackagePath, NewAssetName;
	Params->TryGetStringField(TEXT("new_package_path"), NewPackagePath);
	Params->TryGetStringField(TEXT("new_asset_name"), NewAssetName);

	if (NewPackagePath.IsEmpty() && NewAssetName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("At least one of 'new_package_path' or 'new_asset_name' must be provided"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NewPackagePath, NewAssetName, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		// For Blueprint assets, the FAssetRenameData target should be the BP itself, not its generated class.
		// LoadAssetObject typically returns the BP UObject, which is what we want.

		const UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Asset has no outer package")));
			return;
		}

		const FString CurrentPackageName = Package->GetName();
		const FString CurrentPackagePath = FPackageName::GetLongPackagePath(CurrentPackageName);
		const FString CurrentAssetName = Asset->GetName();

		const FString FinalPackagePath = NewPackagePath.IsEmpty() ? CurrentPackagePath : NewPackagePath;
		const FString FinalAssetName = NewAssetName.IsEmpty() ? CurrentAssetName : NewAssetName;

		if (FinalPackagePath == CurrentPackagePath && FinalAssetName == CurrentAssetName)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("New path equals current path; nothing to rename")));
			return;
		}

		TArray<FAssetRenameData> AssetsToRename;
		AssetsToRename.Add(FAssetRenameData(Asset, FinalPackagePath, FinalAssetName));

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		IAssetTools& AssetTools = AssetToolsModule.Get();
		bool RenameResult = AssetTools.RenameAssets(AssetsToRename);

		if (!RenameResult)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(
				TEXT("RenameAssets failed (result=%d) for %s -> %s/%s"),
				RenameResult, *AssetPath, *FinalPackagePath, *FinalAssetName)));
			return;
		}

		const FString NewAssetPath = FinalPackagePath / FinalAssetName;

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("renamed"), true);
		Data->SetStringField(TEXT("old_path"), AssetPath);
		Data->SetStringField(TEXT("new_path"), NewAssetPath);
		Data->SetStringField(TEXT("new_package_path"), FinalPackagePath);
		Data->SetStringField(TEXT("new_asset_name"), FinalAssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Rename asset timed out"));
	return Future.Get();
}

namespace
{
	// Accepts "/Game/Path/Asset" OR "/Game/Path/Asset.Asset" — strips object suffix.
	static FName NormalizePackageName(const FString& InAssetPath)
	{
		FString PackageName = InAssetPath;
		int32 DotIndex = INDEX_NONE;
		if (PackageName.FindChar(TEXT('.'), DotIndex))
		{
			PackageName = PackageName.Left(DotIndex);
		}
		return FName(*PackageName);
	}
}

FString FAIExportTCPServer::HandleGetReferencers(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			// Block briefly so reference results are not falsely empty.
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FName> Referencers;
		AR.GetReferencers(PackageName, Referencers);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Referencers.Num());
		for (const FName& Ref : Referencers)
		{
			Array.Add(MakeShared<FJsonValueString>(Ref.ToString()));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("referencers"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get referencers timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetDependencies(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FName> Dependencies;
		AR.GetDependencies(PackageName, Dependencies);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Dependencies.Num());
		for (const FName& Dep : Dependencies)
		{
			Array.Add(MakeShared<FJsonValueString>(Dep.ToString()));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("dependencies"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get dependencies timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleDeleteAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	bool bForce = false;
	Params->TryGetBoolField(TEXT("force"), bForce);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, bForce, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		const FString PackageName = Asset->GetOutermost()->GetName();

		int32 NumDeleted = 0;
		if (bForce)
		{
			TArray<UObject*> Objects;
			Objects.Add(Asset);
			NumDeleted = ObjectTools::ForceDeleteObjects(Objects, /*bShowConfirmation=*/false);
		}
		else
		{
			TArray<FAssetData> AssetsToDelete;
			AssetsToDelete.Add(FAssetData(Asset));
			NumDeleted = ObjectTools::DeleteAssets(AssetsToDelete, /*bShowConfirmation=*/false);
		}

		if (NumDeleted == 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(
				TEXT("Delete returned 0 for %s (check referencers with get_referencers, or pass force=true to bypass reference check)"),
				*AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("deleted"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName);
		Data->SetNumberField(TEXT("num_deleted"), NumDeleted);
		Data->SetBoolField(TEXT("force"), bForce);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Delete asset timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleListRedirectors(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath))
		return CreateErrorResponse(TEXT("Missing 'folder_path' parameter"));

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [FolderPath, bRecursive, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*FolderPath));
		Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Assets.Num());
		for (const FAssetData& AssetData : Assets)
		{
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset());
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
			Entry->SetStringField(TEXT("destination_path"),
				(Redirector && Redirector->DestinationObject)
					? Redirector->DestinationObject->GetPathName()
					: TEXT(""));
			Entry->SetBoolField(TEXT("stale"),
				!(Redirector && Redirector->DestinationObject));
			Array.Add(MakeShared<FJsonValueObject>(Entry));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("folder_path"), FolderPath);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("redirectors"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List redirectors timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleFixupRedirectors(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath))
		return CreateErrorResponse(TEXT("Missing 'folder_path' parameter"));

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [FolderPath, bRecursive, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*FolderPath));
		Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<UObjectRedirector*> Redirectors;
		TArray<TSharedPtr<FJsonValue>> Skipped;
		Redirectors.Reserve(Assets.Num());
		for (const FAssetData& AssetData : Assets)
		{
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset());
			if (!Redirector)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
				Entry->SetStringField(TEXT("reason"), TEXT("load_failed"));
				Skipped.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			if (!Redirector->DestinationObject)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
				Entry->SetStringField(TEXT("reason"), TEXT("stale_no_destination"));
				Skipped.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Redirectors.Add(Redirector);
		}

		const int32 FoundCount = Assets.Num();
		const int32 FixedCount = Redirectors.Num();

		if (Redirectors.Num() > 0)
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().FixupReferencers(Redirectors, /*bCheckoutDialogPrompt=*/false);
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("folder_path"), FolderPath);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("redirectors_found"), FoundCount);
		Data->SetNumberField(TEXT("redirectors_fixed"), FixedCount);
		Data->SetNumberField(TEXT("skipped_count"), Skipped.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("skipped"), Skipped);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Fixup redirectors timed out"));
	return Future.Get();
}

// =============================================================================
// Input Mapping Context Command Handlers
// =============================================================================

FString FAIExportTCPServer::HandleAddInputMapping(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, InputActionPath, KeyName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("input_action_path"), InputActionPath))
		return CreateErrorResponse(TEXT("Missing 'input_action_path'"));
	if (!Params->TryGetStringField(TEXT("key"), KeyName))
		return CreateErrorResponse(TEXT("Missing 'key'"));

	// Parse optional trigger/modifier arrays
	TArray<FString> TriggerClasses, ModifierClasses;
	const TArray<TSharedPtr<FJsonValue>>* TriggersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("triggers"), TriggersArr) && TriggersArr)
	{
		for (const auto& Val : *TriggersArr)
		{
			FString Str;
			if (Val->TryGetString(Str)) TriggerClasses.Add(Str);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("modifiers"), ModifiersArr) && ModifiersArr)
	{
		for (const auto& Val : *ModifiersArr)
		{
			FString Str;
			if (Val->TryGetString(Str)) ModifierClasses.Add(Str);
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, InputActionPath, KeyName, TriggerClasses, ModifierClasses, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
		if (!IMC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Not an InputMappingContext: %s"), *AssetPath))); return; }

		bool bSuccess = UAIAssetFactory::AddInputMapping(IMC, InputActionPath, KeyName, TriggerClasses, ModifierClasses);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add input mapping"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("action"), InputActionPath);
		Data->SetStringField(TEXT("key"), KeyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add input mapping timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveInputMapping(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	double MappingIndex = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetNumberField(TEXT("mapping_index"), MappingIndex))
		return CreateErrorResponse(TEXT("Missing 'mapping_index'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	int32 Index = static_cast<int32>(MappingIndex);

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Index, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
		if (!IMC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Not an InputMappingContext: %s"), *AssetPath))); return; }

		bool bSuccess = UAIAssetFactory::RemoveInputMapping(IMC, Index);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove mapping at index %d"), Index))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("removed"), true);
		Data->SetNumberField(TEXT("mapping_index"), Index);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove input mapping timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetInputMappings(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
		if (!IMC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Not an InputMappingContext: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIAssetFactory::GetInputMappingsAsJson(IMC);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get input mappings timed out"));
	return Future.Get();
}

// =============================================================================
// AnimBlueprint Builder Command Handlers
// =============================================================================

FString FAIExportTCPServer::HandleCreateAnimBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, SkeletonPath;
	FString ParentClass = TEXT("AnimInstance");

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		return CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));

	Params->TryGetStringField(TEXT("parent_class"), ParentClass);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, SkeletonPath, ParentClass, Promise, this]()
	{
		UAnimBlueprint* AnimBP = UAIAnimBlueprintBuilder::CreateAnimBlueprint(PackagePath, AssetName, SkeletonPath, ParentClass);
		if (!AnimBP)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create AnimBlueprint")));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AnimBP->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("skeleton"), SkeletonPath);
		Data->SetStringField(TEXT("parent_class"), AnimBP->ParentClass ? AnimBP->ParentClass->GetName() : TEXT("None"));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create AnimBlueprint timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetAnimBlueprintInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UAnimBlueprint* AnimBP = UAIAnimBlueprintBuilder::LoadAnimBlueprint(AssetPath);
		if (!AnimBP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIAnimBlueprintBuilder::GetAnimBlueprintInfoAsJson(AnimBP);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get AnimBlueprint info timed out"));
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Widget Preview Capture — IFTP verify loop

FString FAIExportTCPServer::HandleCaptureWidgetPreview(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	// Parse primitive parameters (use double then clamp/cast)
	int32 Width = 1920;
	int32 Height = 1080;
	int32 WarmupFrames = 3;
	float DPIScale = 1.0f;
	bool bTransparentBG = false;
	bool bReturnBase64 = false;
	FString OutputPath;
	FString PreviewMode = TEXT("runtime");
	struct FPreviewFunctionCall
	{
		FString WidgetName;
		FString FunctionName;
		TMap<FString, FString> Args;
	};
	TArray<FPreviewFunctionCall> PreviewFunctionCalls;

	{
		double DVal = 0.0;
		if (Params->TryGetNumberField(TEXT("width"), DVal))         Width = FMath::Clamp((int32)DVal, 16, 8192);
		if (Params->TryGetNumberField(TEXT("height"), DVal))        Height = FMath::Clamp((int32)DVal, 16, 8192);
		if (Params->TryGetNumberField(TEXT("warmup_frames"), DVal)) WarmupFrames = FMath::Clamp((int32)DVal, 1, 10);
		if (Params->TryGetNumberField(TEXT("dpi_scale"), DVal))     DPIScale = FMath::Clamp((float)DVal, 0.1f, 8.0f);
	}
	Params->TryGetBoolField(TEXT("transparent_bg"), bTransparentBG);
	Params->TryGetBoolField(TEXT("return_base64"), bReturnBase64);
	Params->TryGetStringField(TEXT("output_path"), OutputPath);
	Params->TryGetStringField(TEXT("preview_mode"), PreviewMode);
	PreviewMode.TrimStartAndEndInline();
	PreviewMode.ToLowerInline();
	if (PreviewMode.IsEmpty())
	{
		PreviewMode = TEXT("runtime");
	}
	if (PreviewMode != TEXT("runtime") && PreviewMode != TEXT("designer"))
	{
		return CreateErrorResponse(TEXT("Invalid 'preview_mode'. Expected 'runtime' or 'designer'."));
	}

	const TArray<TSharedPtr<FJsonValue>>* FunctionCallsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("preview_function_calls"), FunctionCallsArray) && FunctionCallsArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *FunctionCallsArray)
		{
			const TSharedPtr<FJsonObject>* CallObj = nullptr;
			if (!Entry->TryGetObject(CallObj) || !CallObj->IsValid())
			{
				continue;
			}

			FPreviewFunctionCall Call;
			(*CallObj)->TryGetStringField(TEXT("widget_name"), Call.WidgetName);
			if (!(*CallObj)->TryGetStringField(TEXT("function_name"), Call.FunctionName) || Call.FunctionName.IsEmpty())
			{
				return CreateErrorResponse(TEXT("Invalid 'preview_function_calls' entry: missing 'function_name'."));
			}

			const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
			if ((*CallObj)->TryGetObjectField(TEXT("args"), ArgsObj) && ArgsObj->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& ArgPair : (*ArgsObj)->Values)
				{
					FString ArgValue;
					if (ArgPair.Value->TryGetString(ArgValue))
					{
						Call.Args.Add(ArgPair.Key, ArgValue);
					}
					else
					{
						Call.Args.Add(ArgPair.Key, ArgPair.Value->AsString());
					}
				}
			}

			PreviewFunctionCalls.Add(MoveTemp(Call));
		}
	}

	// Parse optional ratios array (multi-ratio mode)
	// Each entry: { "width": 2560, "height": 1080, "label": "21x9" }
	struct FRatioEntry
	{
		int32 Width;
		int32 Height;
		FString Label;
	};
	TArray<FRatioEntry> Ratios;
	const TArray<TSharedPtr<FJsonValue>>* RatiosArray = nullptr;
	if (Params->TryGetArrayField(TEXT("ratios"), RatiosArray) && RatiosArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *RatiosArray)
		{
			const TSharedPtr<FJsonObject>* RatioObj = nullptr;
			if (!Entry->TryGetObject(RatioObj) || !RatioObj->IsValid()) continue;
			FRatioEntry R;
			double RW = 1920.0, RH = 1080.0;
			(*RatioObj)->TryGetNumberField(TEXT("width"), RW);
			(*RatioObj)->TryGetNumberField(TEXT("height"), RH);
			R.Width = FMath::Clamp((int32)RW, 16, 8192);
			R.Height = FMath::Clamp((int32)RH, 16, 8192);
			(*RatioObj)->TryGetStringField(TEXT("label"), R.Label);
			Ratios.Add(R);
		}
	}

	if (Ratios.Num() == 0)
	{
		FRatioEntry R;
		R.Width = Width;
		R.Height = Height;
		Ratios.Add(R);
	}

	// Output directory
	FString DefaultOutputDir = FPaths::ProjectIntermediateDir() / TEXT("WidgetCaptures");
	FString OutputDir = OutputPath.IsEmpty() ? DefaultOutputDir : FPaths::GetPath(OutputPath);
	IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Ratios, WarmupFrames, DPIScale, bTransparentBG, bReturnBase64, OutputPath, OutputDir, PreviewMode, PreviewFunctionCalls, Promise, this]()
	{
		// 1) Load Widget Blueprint
		UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
		if (!WidgetBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath)));
			return;
		}

		UClass* WidgetClass = WidgetBP->GeneratedClass;
		if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Invalid WidgetBlueprint GeneratedClass")));
			return;
		}

		// 2) Get editor world for widget context
		UWorld* World = nullptr;
		if (GEditor)
		{
			World = GEditor->GetEditorWorldContext().World();
		}
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No valid editor world for widget instantiation")));
			return;
		}

		// 3) Create widget instance
		UUserWidget* UserWidget = CreateWidget<UUserWidget>(World, WidgetClass);
		if (!UserWidget)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to instantiate UserWidget")));
			return;
		}
		UserWidget->AddToRoot();  // Prevent GC during rendering

#if WITH_EDITOR
		if (PreviewMode == TEXT("designer"))
		{
			// Explicit designer preview path for widgets with IsDesignTime()-gated
			// sample data. Runtime acceptance captures must not set these flags.
			UserWidget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
			UserWidget->SynchronizeProperties();
		}
#endif

		// Runtime CommonUI construction can bind default input actions before any
		// LocalPlayer/CommonInputSubsystem exists in this offscreen editor context.
		ICommonInputModule::GetSettings().LoadData();

		// 4) Take Slate widget — triggers outer widget's Initialize + PreConstruct.
		TSharedRef<SWidget> SlateWidget = UserWidget->TakeWidget();

		// CommonUI screens often synchronize state during activation rather than
		// designer PreConstruct. Offscreen captures need that lifecycle too, or
		// button text, selected tabs, and settings rows can render with defaults.
		if (UCommonActivatableWidget* ActivatableWidget = Cast<UCommonActivatableWidget>(UserWidget))
		{
			ActivatableWidget->ActivateWidget();
		}

		int32 PreviewFunctionCallCount = 0;
		for (const FPreviewFunctionCall& Call : PreviewFunctionCalls)
		{
			UObject* Target = UserWidget;
			if (!Call.WidgetName.IsEmpty())
			{
				Target = UserWidget->WidgetTree ? UserWidget->WidgetTree->FindWidget(FName(*Call.WidgetName)) : nullptr;
			}
			if (!Target)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("preview_function_calls target widget not found: %s"), *Call.WidgetName)));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			UFunction* Function = Target->FindFunction(FName(*Call.FunctionName));
			if (!Function)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("preview_function_calls function not found: %s on %s"), *Call.FunctionName, *Target->GetName())));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			TArray<uint8> ParamBuffer;
			void* ParamData = nullptr;
			if (Function->ParmsSize > 0)
			{
				ParamBuffer.SetNumZeroed(Function->ParmsSize);
				ParamData = ParamBuffer.GetData();
				Function->InitializeStruct(ParamData);
			}

			bool bParamImportSucceeded = true;
			FString ParamError;
			for (TFieldIterator<FProperty> It(Function); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Parm) || Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					continue;
				}

				const FString* ArgValue = Call.Args.Find(Prop->GetName());
				if (!ArgValue)
				{
					continue;
				}

				if (!ParamData)
				{
					bParamImportSucceeded = false;
					ParamError = FString::Printf(TEXT("Function %s has no parameter storage for preview arg %s"), *Call.FunctionName, *Prop->GetName());
					break;
				}

				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(ParamData);
				if (!Prop->ImportText_Direct(**ArgValue, PropAddr, Target, PPF_None))
				{
					bParamImportSucceeded = false;
					ParamError = FString::Printf(TEXT("Failed to import preview function arg %s=%s for %s"), *Prop->GetName(), **ArgValue, *Call.FunctionName);
					break;
				}
			}

			if (!bParamImportSucceeded)
			{
				if (ParamData)
				{
					Function->DestroyStruct(ParamData);
				}
				Promise->SetValue(CreateErrorResponse(ParamError));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			Target->ProcessEvent(Function, ParamData);
			if (ParamData)
			{
				Function->DestroyStruct(ParamData);
			}
			++PreviewFunctionCallCount;
		}

		// 4a) Force-initialize all nested UUserWidget components, then preload textures.
		//     Nested UUserWidgets in the WidgetTree are NOT auto-initialized by the outer
		//     widget's TakeWidget() — they init lazily when first painted. That means their
		//     BP PreConstruct (which calls SetBrushFromTexture(NavIcon) etc.) has not yet run,
		//     so Brush.ResourceObject is null when we try to force-stream textures.
		//     Fix: explicitly Initialize() each nested UserWidget first, then walk again to
		//     collect the now-populated brush textures and force their mip residency.
		FlushAsyncLoading();
		int32 InitCount = 0;
		int32 TexCount = 0;
		int32 SyncCount = 0;
		int32 StreamingWaitSkippedCount = 0;
		{
			auto ForceTextureResidentForCapture = [&TexCount, &StreamingWaitSkippedCount](UTexture2D* Tex)
			{
				if (!Tex)
				{
					return;
				}

				Tex->SetForceMipLevelsToBeResident(30.0f, true);
				if (!IsAssetStreamingSuspended())
				{
					Tex->WaitForStreaming();
				}
				else
				{
					++StreamingWaitSkippedCount;
				}
				++TexCount;
			};

			// Pass 1: force Initialize() on all nested UUserWidget instances (recursive)
			TFunction<void(UWidgetTree*)> InitTree;
			InitTree = [&InitTree, &InitCount, &PreviewMode](UWidgetTree* Tree)
			{
				if (!Tree) return;
				Tree->ForEachWidget([&InitTree, &InitCount, &PreviewMode](UWidget* W)
				{
					if (UUserWidget* NestedUW = Cast<UUserWidget>(W))
					{
#if WITH_EDITOR
						if (PreviewMode == TEXT("designer"))
						{
							NestedUW->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
						}
#endif
						if (!NestedUW->IsConstructed())
						{
							NestedUW->Initialize();  // runs BP PreConstruct on this nested instance
							++InitCount;
						}
						NestedUW->TakeWidget();
						InitTree(NestedUW->WidgetTree);
					}
				});
			};
			InitTree(UserWidget->WidgetTree);

			// Pass 2: collect + stream all brush textures (recursive), then SynchronizeProperties
			//         to push the updated brush into the already-built SImage slate widget.
			//         PreConstruct's SetBrushFromTexture() only pushes to Slate if MyImage.IsValid()
			//         at call time. When PreConstruct runs during nested Initialize(), MyImage is
			//         usually null (slate widget not yet built), so the brush sits in the UImage
			//         struct but is never copied to the SImage. A manual SynchronizeProperties()
			//         after the slate tree is built (i.e. after TakeWidget) does exactly that copy.
			TFunction<void(UWidgetTree*)> PreloadTree;
			PreloadTree = [&PreloadTree, &ForceTextureResidentForCapture, &SyncCount](UWidgetTree* Tree)
			{
				if (!Tree) return;
				Tree->ForEachWidget([&PreloadTree, &ForceTextureResidentForCapture, &SyncCount](UWidget* W)
				{
					W->SynchronizeProperties();
					++SyncCount;

					if (UImage* Img = Cast<UImage>(W))
					{
						if (UTexture2D* Tex = Cast<UTexture2D>(Img->Brush.GetResourceObject()))
						{
							ForceTextureResidentForCapture(Tex);
						}
					}
					else if (UBorder* Brd = Cast<UBorder>(W))
					{
						// Same bridging issue as UImage: SBorder built before CDO Background
						// was pushed via reflection ImportText. Force a resync so
						// set_widget_property changes land on the Slate side.
						if (UTexture2D* Tex = Cast<UTexture2D>(Brd->Background.GetResourceObject()))
						{
							ForceTextureResidentForCapture(Tex);
						}
					}
					else if (UUserWidget* NestedUW = Cast<UUserWidget>(W))
					{
						NestedUW->SynchronizeProperties();
						PreloadTree(NestedUW->WidgetTree);
					}
				});
			};
			PreloadTree(UserWidget->WidgetTree);
		}
		// Final global streaming flush — catches anything ForceMipLevelsToBeResident missed.
		if (!IsAssetStreamingSuspended())
		{
			IStreamingManager::Get().StreamAllResources(0.0f);
		}
		else
		{
			++StreamingWaitSkippedCount;
		}
		UE_LOG(LogAIExport, Log, TEXT("CaptureWidgetPreview[%s]: applied %d preview function calls, initialized %d nested widgets, synchronized %d widgets, streamed %d textures, skipped %d streaming waits"), *PreviewMode, PreviewFunctionCallCount, InitCount, SyncCount, TexCount, StreamingWaitSkippedCount);

		// 5) Get ImageWrapper module
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

		// 6) Derive base filename
		FString AssetBaseName = FPaths::GetBaseFilename(AssetPath);

		// 7) Widget renderer (gamma correction on for sRGB output)
		TSharedPtr<FWidgetRenderer> Renderer = MakeShared<FWidgetRenderer>(/*bUseGammaCorrection=*/true);

		TArray<TSharedPtr<FJsonValue>> PngResults;
		bool bAllSucceeded = true;
		FString LastError;

		for (const FRatioEntry& Ratio : Ratios)
		{
			const int32 RW = Ratio.Width;
			const int32 RH = Ratio.Height;

			// Create render target — RTF_RGBA8 (raw UNORM) + TargetGamma=0 (use DisplayGamma default).
			// FWidgetRenderer(bUseGammaCorrection=true) already applies linear→sRGB in shader
			// using RT->GetDisplayGamma(). Setting TargetGamma=2.2 + sRGB format causes double
			// gamma (values look washed out). Setting TargetGamma=0 lets it fall through to
			// Engine->DisplayGamma (2.2) applied exactly once. Matches editor viewport.
			UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
			RT->ClearColor = bTransparentBG ? FLinearColor::Transparent : FLinearColor::Black;
			RT->TargetGamma = 0.0f;
			RT->RenderTargetFormat = RTF_RGBA8;
			RT->InitAutoFormat(RW, RH);
			RT->UpdateResourceImmediate(true);
			RT->AddToRoot();

			// Warmup + final render passes (absorb texture streaming delay)
			const FVector2D DrawSize(RW, RH);
			for (int32 i = 0; i < WarmupFrames; ++i)
			{
				Renderer->DrawWidget(RT, SlateWidget, DrawSize, 0.016f, false);
			}

			// Flush GPU work before reading pixels
			FlushRenderingCommands();

			// Read pixels
			TArray<FColor> Bitmap;
			FRenderTarget* RenderTargetResource = RT->GameThread_GetRenderTargetResource();
			if (!RenderTargetResource || !RenderTargetResource->ReadPixels(Bitmap))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to read pixels for %dx%d"), RW, RH);
				RT->RemoveFromRoot();
				continue;
			}

			// Encode PNG
			TSharedPtr<IImageWrapper> PNGWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (!PNGWrapper.IsValid() ||
				!PNGWrapper->SetRaw(Bitmap.GetData(),
									Bitmap.Num() * sizeof(FColor),
									RW, RH,
									ERGBFormat::BGRA, 8))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to encode PNG for %dx%d"), RW, RH);
				RT->RemoveFromRoot();
				continue;
			}

			const TArray64<uint8>& CompressedPng = PNGWrapper->GetCompressed(100);

			// Flatten into TArray<uint8> for FFileHelper + FBase64 compatibility
			TArray<uint8> FlatPng;
			FlatPng.SetNumUninitialized((int32)CompressedPng.Num());
			FMemory::Memcpy(FlatPng.GetData(), CompressedPng.GetData(), CompressedPng.Num());

			// Derive output file path
			FString OutPath;
			if (Ratios.Num() == 1 && !OutputPath.IsEmpty())
			{
				OutPath = OutputPath;
			}
			else
			{
				FString Suffix = Ratio.Label.IsEmpty()
					? FString::Printf(TEXT("_%dx%d"), RW, RH)
					: FString::Printf(TEXT("_%s"), *Ratio.Label);
				Suffix.ReplaceInline(TEXT(":"), TEXT(""));
				Suffix.ReplaceInline(TEXT("/"), TEXT("_"));
				Suffix.ReplaceInline(TEXT("\\"), TEXT("_"));
				OutPath = OutputDir / (AssetBaseName + Suffix + TEXT(".png"));
			}

			// Save to disk
			if (!FFileHelper::SaveArrayToFile(FlatPng, *OutPath))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to write PNG: %s"), *OutPath);
				RT->RemoveFromRoot();
				continue;
			}

			// Build JSON entry
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("png_path"), OutPath);
			Entry->SetNumberField(TEXT("width"), RW);
			Entry->SetNumberField(TEXT("height"), RH);
			Entry->SetNumberField(TEXT("size_bytes"), FlatPng.Num());
			Entry->SetStringField(TEXT("preview_mode"), PreviewMode);
			if (!Ratio.Label.IsEmpty())
			{
				Entry->SetStringField(TEXT("label"), Ratio.Label);
			}
			if (bReturnBase64)
			{
				FString B64 = FBase64::Encode(FlatPng);
				Entry->SetStringField(TEXT("png_base64"), B64);
			}
			PngResults.Add(MakeShared<FJsonValueObject>(Entry));

			// Cleanup RT
			RT->RemoveFromRoot();
		}

		// Cleanup widget
		if (UCommonActivatableWidget* ActivatableWidget = Cast<UCommonActivatableWidget>(UserWidget))
		{
			if (ActivatableWidget->IsActivated())
			{
				ActivatableWidget->DeactivateWidget();
			}
		}
		UserWidget->ReleaseSlateResources(true);
		UserWidget->RemoveFromRoot();

		if (PngResults.Num() == 0)
		{
			Promise->SetValue(CreateErrorResponse(LastError.IsEmpty() ? TEXT("No previews produced") : LastError));
			return;
		}

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("preview_mode"), PreviewMode);
		Data->SetNumberField(TEXT("preview_function_calls_applied"), PreviewFunctionCallCount);
		Data->SetArrayField(TEXT("pngs"), PngResults);
		Data->SetNumberField(TEXT("count"), PngResults.Num());
		if (!bAllSucceeded)
		{
			Data->SetStringField(TEXT("partial_error"), LastError);
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Widget preview capture timed out"));
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Asset Lifecycle — Reload asset (fixes cached editor tab after compile_and_save)

FString FAIExportTCPServer::HandleReloadAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	bool bReopenAfter = true;
	Params->TryGetBoolField(TEXT("reopen_after"), bReopenAfter);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, bReopenAfter, Promise, this]()
	{
		// 1) Silent existence check BEFORE LoadObject (avoids UE log spam on bad paths)
		//    Extract package path from asset path (strip .ObjectName suffix if present)
		FString PackagePath = AssetPath;
		int32 DotIdx;
		if (PackagePath.FindChar('.', DotIdx))
		{
			PackagePath = PackagePath.Left(DotIdx);
		}
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Package does not exist on disk: %s"), *PackagePath)));
			return;
		}

		// 2) Load the asset (safe now — package verified to exist)
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Asset has no outer package")));
			return;
		}

		// 2) Check if asset editor is currently open, remember state
		bool bWasOpen = false;
		UAssetEditorSubsystem* EditorSubsystem = nullptr;
		if (GEditor)
		{
			EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (EditorSubsystem)
			{
				// FindEditorsForAsset returns nullptr if not open
				bWasOpen = EditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false) != nullptr;

				// Close all editors for this asset (clears cached widget instance)
				if (bWasOpen)
				{
					EditorSubsystem->CloseAllEditorsForAsset(Asset);
				}
			}
		}

		// 3) Hard reload the package (reloads from disk, discards in-memory cache)
		TArray<UPackage*> PackagesToReload;
		PackagesToReload.Add(Package);

		FText ErrorMsg;
		bool bReloaded = UPackageTools::ReloadPackages(PackagesToReload, ErrorMsg, EReloadPackagesInteractionMode::AssumePositive);

		// 4) Reopen editor if it was previously open and reopen_after flag is true
		bool bReopened = false;
		if (bReopenAfter && bWasOpen && EditorSubsystem)
		{
			// Load fresh reference after reload
			UObject* FreshAsset = LoadObject<UObject>(nullptr, *AssetPath);
			if (FreshAsset)
			{
				bReopened = EditorSubsystem->OpenEditorForAsset(FreshAsset);
			}
		}

		// 5) Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetBoolField(TEXT("was_open"), bWasOpen);
		Data->SetBoolField(TEXT("reloaded"), bReloaded);
		Data->SetBoolField(TEXT("reopened"), bReopened);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Reload asset timed out"));
	return Future.Get();
}

#undef GRAPH_NODE_HANDLER_BODY
