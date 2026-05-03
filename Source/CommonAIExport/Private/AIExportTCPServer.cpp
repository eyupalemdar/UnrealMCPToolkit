// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "AIExportFunctionLibrary.h"
#include "Builders/AIWidgetBlueprintBuilder.h"
#include "Builders/AIMaterialBuilder.h"
#include "Builders/AIBlueprintGraphBuilder.h"
#include "Builders/AIDataAssetBuilder.h"
#include "Builders/AIAssetFactory.h"
#include "Builders/AIAnimBlueprintBuilder.h"
#include "CommandHandlers/AIExportWidgetCommands.h"
#include "CommandHandlers/AIExportMaterialCommands.h"
#include "CommandHandlers/AIExportDataAssetCommands.h"
#include "CommandHandlers/AIExportImportCommands.h"
#include "CommandHandlers/AIExportCDOCommands.h"
#include "CommandHandlers/AIExportBlueprintGraphCommands.h"
#include "CommandHandlers/AIExportAssetCommands.h"
#include "CommandHandlers/AIExportInputCommands.h"
#include "CommandHandlers/AIExportAnimBlueprintCommands.h"
#include "CommandHandlers/AIExportWidgetPreviewCommands.h"
#include "CommandHandlers/AIExportAssetLifecycleCommands.h"
#include "CommandHandlers/AIExportWorkflowCommands.h"
#include "CommandHandlers/AIExportExportCommands.h"
#include "CommandHandlers/AIExportEditorCommands.h"
#include "CommandHandlers/AIExportUtilityCommands.h"
#include "CommandHandlers/AIExportAsyncCommands.h"
#include "RuntimeDiagnostics/AIExportRuntimeAI.h"
#include "RuntimeDiagnostics/AIExportRuntimeAudio.h"
#include "RuntimeDiagnostics/AIExportRuntimeCore.h"
#include "RuntimeDiagnostics/AIExportRuntimeEQS.h"
#include "RuntimeDiagnostics/AIExportRuntimeFramework.h"
#include "RuntimeDiagnostics/AIExportRuntimeGameplay.h"
#include "RuntimeDiagnostics/AIExportRuntimeNavigation.h"
#include "RuntimeDiagnostics/AIExportRuntimePhysics.h"
#include "RuntimeDiagnostics/AIExportRuntimeScheduling.h"
#include "RuntimeDiagnostics/AIExportRuntimeStreaming.h"
#include "RuntimeDiagnostics/AIExportRuntimeUI.h"
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
#include "AudioDeviceHandle.h"
#include "AudioDeviceManager.h"
#include "Components/InputComponent.h"
#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundEffectSource.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/OnlineSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "GenericTeamAgentInterface.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/RecastNavMesh.h"
#include "AI/Navigation/NavigationBounds.h"
#include "AI/Navigation/NavigationDataResolution.h"
#include "AI/Navigation/NavigationInvokerPriority.h"
#include "AI/Navigation/NavAgentSelector.h"
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
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
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
		AI_COMMAND_OPTIONAL_PARAMS("runtime_ai_controller_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAIControllerDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_eqs_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeEQSDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_gameplay_tags_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeGameplayTagsDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_commonui_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeCommonUIDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_audio_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAudioDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_navigation_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeNavigationDiagnostics),
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

CommonAIExport::CommandHandlers::Utility::FAIExportUtilityContext FAIExportTCPServer::BuildUtilityContext() const
{
	CommonAIExport::CommandHandlers::Utility::FAIExportUtilityContext Context;
	Context.ServerPort = ServerPort;
	Context.ActiveClientConnections = TcpTransport.GetActiveClientConnections();
	Context.EditorInstanceId = EditorInstanceId;
	Context.EditorRegistryFilePath = EditorRegistryFilePath;
	Context.ServerStartedAtUtc = ServerStartedAtUtc;
	Context.PortFilePath = FPaths::ConvertRelativePathToFull(GetPortFilePath());
	Context.ManifestSource = TEXT("FAIExportTCPServer::GetCommandDescriptors");
	Context.HttpStatus = HttpMcpServer.GetStatus();
	Context.TaskCounts = AsyncJobStore.GetCounts();

	for (const FCommandDescriptor& Descriptor : GetCommandDescriptors())
	{
		CommonAIExport::CommandHandlers::Utility::FAIExportUtilityCommandDescriptor UtilityDescriptor;
		UtilityDescriptor.Name = Descriptor.Name;
		UtilityDescriptor.Category = Descriptor.Category;
		UtilityDescriptor.bRequiresParams = Descriptor.bRequiresParams;
		UtilityDescriptor.bMutating = Descriptor.bMutating;
		UtilityDescriptor.TimeoutSeconds = Descriptor.TimeoutSeconds;
		UtilityDescriptor.RequiredScope = Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read");
		UtilityDescriptor.bSupportsDryRun = Descriptor.bSupportsDryRun;
		UtilityDescriptor.bAsyncCandidate = Descriptor.bAsyncCandidate;
		Context.Commands.Add(MoveTemp(UtilityDescriptor));
	}

	return Context;
}

void FAIExportTCPServer::StartHttpServer()
{
	CommonAIExport::HttpMcp::FAIExportHttpMcpCallbacks Callbacks;
	Callbacks.HandlePing = [this]() { return HandlePing(); };
	Callbacks.HandleListCommands = [this]() { return HandleListCommands(); };
	Callbacks.HandleProjectStatus = [this]() { return HandleProjectStatus(); };
	Callbacks.HandleEditorIdentity = [this]() { return HandleEditorIdentity(); };
	Callbacks.HandleEditorLogRead = [this](TSharedPtr<FJsonObject> Params) { return HandleEditorLogRead(Params); };
	Callbacks.HandleTaskEvents = [this](TSharedPtr<FJsonObject> Params) { return HandleTaskEvents(Params); };
	Callbacks.HandleTaskEventsWait = [this](TSharedPtr<FJsonObject> Params) { return HandleTaskEventsWait(Params); };
	Callbacks.BuildTaskEventsSse = [this](TSharedPtr<FJsonObject> Params) { return AsyncJobStore.BuildTaskEventsSse(Params); };
	Callbacks.BuildTaskEventParamsFromHttpRequest = [this](const FHttpServerRequest& Request)
	{
		return AsyncJobStore.BuildTaskEventParamsFromHttpRequest(Request);
	};
	Callbacks.ProcessCommand = [this](const FString& JsonCommand) { return ProcessCommand(JsonCommand); };
	Callbacks.GetToolDescriptors = []()
	{
		TArray<CommonAIExport::HttpMcp::FAIExportHttpMcpToolDescriptor> Tools;
		for (const FCommandDescriptor& Descriptor : GetCommandDescriptors())
		{
			CommonAIExport::HttpMcp::FAIExportHttpMcpToolDescriptor Tool;
			Tool.Name = Descriptor.Name;
			Tool.Category = Descriptor.Category;
			Tool.RequiredScope = Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read");
			Tool.bMutating = Descriptor.bMutating;
			Tool.bSupportsDryRun = Descriptor.bSupportsDryRun;
			Tools.Add(MoveTemp(Tool));
		}
		return Tools;
	};
	Callbacks.IsStopRequested = [this]() { return bStopRequested.Load(); };
	Callbacks.FindAvailablePort = [](int32 StartPort, int32 EndPort)
	{
		return FAIExportTCPServer::FindAvailablePort(StartPort, EndPort);
	};

	HttpMcpServer.Start(Callbacks);
}

void FAIExportTCPServer::StopHttpServer()
{
	HttpMcpServer.Stop();
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
	FJsonSerializer::Serialize(CommonAIExport::CommandHandlers::Utility::BuildEditorIdentityJson(BuildUtilityContext()).ToSharedRef(), Writer);

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

bool FAIExportTCPServer::Init()
{
	return true;
}

uint32 FAIExportTCPServer::Run()
{
	CommonAIExport::Transport::FAIExportTcpTransportCallbacks Callbacks;
	Callbacks.ProcessCommand = [this](const FString& JsonCommand)
	{
		return ProcessCommand(JsonCommand);
	};
	Callbacks.IsStopRequested = [this]()
	{
		return bStopRequested.Load();
	};
	Callbacks.OnListening = [this]()
	{
		bIsRunning = true;
		WriteEditorRegistryFile();
	};
	Callbacks.OnStopped = [this]()
	{
		bIsRunning = false;
	};
	return TcpTransport.Run(ServerPort, Callbacks);
}

void FAIExportTCPServer::Stop()
{
	bStopRequested = true;
	TcpTransport.Stop();
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
	TcpTransport.Stop();

	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}

	RemoveEditorRegistryFile();
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
	return CommonAIExport::CommandHandlers::Utility::HandlePing(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleListCommands()
{
	return CommonAIExport::CommandHandlers::Utility::HandleListCommands(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleServerStatus()
{
	return CommonAIExport::CommandHandlers::Utility::HandleServerStatus(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleEditorIdentity()
{
	return CommonAIExport::CommandHandlers::Utility::HandleEditorIdentity(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleCommandManifestExport(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Utility::HandleCommandManifestExport(Params, BuildUtilityContext());
}

FString FAIExportTCPServer::HandleProjectStatus()
{
	return CommonAIExport::CommandHandlers::Utility::HandleProjectStatus(BuildUtilityContext());
}

FString FAIExportTCPServer::HandleSourceControlStatus(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlStatus(Params);
}
FString FAIExportTCPServer::HandleSourceControlLog(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlLog(Params);
}
FString FAIExportTCPServer::HandleSourceControlShow(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlShow(Params);
}
FString FAIExportTCPServer::HandleSourceControlDiff(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleSourceControlDiff(Params);
}
FString FAIExportTCPServer::HandleTaskSubmit(TSharedPtr<FJsonObject> Params)
{
	CommonAIExport::CommandHandlers::AsyncCommands::FAIExportAsyncSubmitCallbacks Callbacks;
	Callbacks.ResolveCommand = [](const FString& CommandName, CommonAIExport::CommandHandlers::AsyncCommands::FAIExportAsyncCommandDescriptor& OutDescriptor)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		if (!Descriptor)
		{
			return false;
		}
		OutDescriptor.Name = Descriptor->Name;
		OutDescriptor.bRequiresParams = Descriptor->bRequiresParams;
		OutDescriptor.bMutating = Descriptor->bMutating;
		OutDescriptor.bAsyncCandidate = Descriptor->bAsyncCandidate;
		OutDescriptor.TimeoutSeconds = Descriptor->TimeoutSeconds;
		return true;
	};
	Callbacks.ValidateCommand = [this](const FString& CommandName, TSharedPtr<FJsonObject> Meta)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		if (!Descriptor)
		{
			return FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName);
		}
		TSharedPtr<FJsonObject> SyntheticRoot = MakeShared<FJsonObject>();
		SyntheticRoot->SetStringField(TEXT("type"), CommandName);
		SyntheticRoot->SetObjectField(TEXT("meta"), Meta.IsValid() ? Meta : MakeShared<FJsonObject>());
		const FAICommandContext TargetContext = BuildCommandContext(SyntheticRoot, *Descriptor);
		FString ScopeError;
		if (!ValidateCommandScope(*Descriptor, TargetContext, ScopeError))
		{
			return ScopeError;
		}
		return FString();
	};
	Callbacks.CreateDryRunResponse = [this](const FString& CommandName, TSharedPtr<FJsonObject> Meta)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		if (!Descriptor)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName));
		}
		TSharedPtr<FJsonObject> SyntheticRoot = MakeShared<FJsonObject>();
		SyntheticRoot->SetStringField(TEXT("type"), CommandName);
		SyntheticRoot->SetObjectField(TEXT("meta"), Meta.IsValid() ? Meta : MakeShared<FJsonObject>());
		return CreateDryRunResponse(*Descriptor, BuildCommandContext(SyntheticRoot, *Descriptor));
	};
	Callbacks.DispatchCommand = [this](const FString& CommandName, TSharedPtr<FJsonObject> CommandParams)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		return Descriptor ? DispatchCommand(*Descriptor, CommandParams) : CreateErrorResponse(FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName));
	};
	Callbacks.IsStopRequested = [this]() { return bStopRequested.Load(); };
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskSubmit(Params, AsyncJobStore, Callbacks);
}

FString FAIExportTCPServer::HandleTaskStatus(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskStatus(Params, AsyncJobStore);
}

FString FAIExportTCPServer::HandleTaskResult(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskResult(Params, AsyncJobStore);
}

FString FAIExportTCPServer::HandleTaskEvents(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskEvents(Params, AsyncJobStore);
}

FString FAIExportTCPServer::HandleTaskEventsWait(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskEventsWait(Params, AsyncJobStore, [this]() { return bStopRequested.Load(); });
}

FString FAIExportTCPServer::HandleTaskCancel(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AsyncCommands::HandleTaskCancel(Params, AsyncJobStore);
}

FString FAIExportTCPServer::HandleExportWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Export::HandleExportWidget(Params);
}
FString FAIExportTCPServer::HandleExportBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Export::HandleExportBlueprint(Params);
}
FString FAIExportTCPServer::HandleListSupportedTypes()
{
	return CommonAIExport::CommandHandlers::Export::HandleListSupportedTypes();
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

FString FAIExportTCPServer::HandleEditorWorldInfo()
{
	return CommonAIExport::CommandHandlers::Editor::HandleEditorWorldInfo();
}

FString FAIExportTCPServer::HandleRuntimeWorldInfo(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = CommonAIExport::RuntimeDiagnostics::BuildWorldInfo(Params);
		Promise->SetValue(Data.IsValid() ? CreateSuccessResponse(Data) : CreateErrorResponse(TEXT("Runtime world is not available")));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime world info timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimePlayerList(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = CommonAIExport::RuntimeDiagnostics::BuildPlayerList(Params);
		Promise->SetValue(Data.IsValid() ? CreateSuccessResponse(Data) : CreateErrorResponse(TEXT("Runtime world is not available")));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime player list timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeComponentList(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = CommonAIExport::RuntimeDiagnostics::BuildComponentList(Params);
		Promise->SetValue(Data.IsValid() ? CreateSuccessResponse(Data) : CreateErrorResponse(TEXT("Runtime component list target is not available")));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime component list timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		TSharedPtr<FJsonObject> Data = CommonAIExport::RuntimeDiagnostics::BuildRuntimeDiagnostics(Params);
		Promise->SetValue(Data.IsValid() ? CreateSuccessResponse(Data) : CreateErrorResponse(TEXT("Runtime diagnostics world is not available")));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeInputRouting(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildInputRoutingDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime input routing timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeGameplayTagsDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildGameplayTagsDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime gameplay tags diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeCommonUIDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildCommonUIDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime CommonUI diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeAudioDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildAudioDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime audio diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeNavigationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildNavigationDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime navigation diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeAssetStreamingDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildAssetStreamingDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime asset streaming diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeAsyncLoadDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildAsyncLoadDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime async load diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeGameInstanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildGameInstanceDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime game instance diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeLevelTravelDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildLevelTravelDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime level travel diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeMultiplayerConnectionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildMultiplayerConnectionDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime multiplayer connection diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeTickTimerLatentDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildTickTimerLatentDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime tick/timer/latent diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeSchedulerPerformanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildSchedulerPerformanceDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime scheduler/performance diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimePhysicsCollisionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildPhysicsCollisionDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime physics/collision diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeReplicationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildReplicationDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime replication diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeAbilitySystemDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildAbilitySystemDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime ability system diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRuntimeAIPerceptionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildAIPerceptionDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime AI perception diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeAIControllerDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildAIControllerDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime AI controller diagnostics timed out"));
	return Future.Get();
}
FString FAIExportTCPServer::HandleRuntimeEQSDiagnostics(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise, this]()
	{
		Promise->SetValue(CreateSuccessResponse(CommonAIExport::RuntimeDiagnostics::BuildEQSDiagnostics(Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Runtime EQS diagnostics timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleActorList(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorList(Params);
}

FString FAIExportTCPServer::HandleActorSpawn(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorSpawn(Params);
}

FString FAIExportTCPServer::HandleActorSetTransform(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorSetTransform(Params);
}

FString FAIExportTCPServer::HandleActorDelete(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorDelete(Params);
}

FString FAIExportTCPServer::HandleLevelOpen(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleLevelOpen(Params);
}

FString FAIExportTCPServer::HandleLevelSaveCurrent()
{
	return CommonAIExport::CommandHandlers::Editor::HandleLevelSaveCurrent();
}

FString FAIExportTCPServer::HandlePIEStatus()
{
	return CommonAIExport::CommandHandlers::Editor::HandlePIEStatus();
}

FString FAIExportTCPServer::HandlePIEStart()
{
	return CommonAIExport::CommandHandlers::Editor::HandlePIEStart();
}

FString FAIExportTCPServer::HandlePIEStop()
{
	return CommonAIExport::CommandHandlers::Editor::HandlePIEStop();
}

FString FAIExportTCPServer::HandleEditorConsoleCommand(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleEditorConsoleCommand(Params);
}

FString FAIExportTCPServer::HandleEditorLogRead(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Workflow::HandleEditorLogRead(Params);
}
FString FAIExportTCPServer::HandleViewportCapture(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleViewportCapture(Params);
}

//////////////////////////////////////////////////////////////////////////
// Widget Builder Command Handlers

FString FAIExportTCPServer::HandleCreateWidgetBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleCreateWidgetBlueprint(Params);
}
FString FAIExportTCPServer::HandleAddWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleAddWidget(Params);
}
FString FAIExportTCPServer::HandleRemoveWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleRemoveWidget(Params);
}
FString FAIExportTCPServer::HandleMoveWidget(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleMoveWidget(Params);
}
FString FAIExportTCPServer::HandleSetWidgetProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetWidgetProperty(Params);
}
FString FAIExportTCPServer::HandleSetSlotProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetSlotProperty(Params);
}
FString FAIExportTCPServer::HandleSetCanvasSlotLayout(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetCanvasSlotLayout(Params);
}
FString FAIExportTCPServer::HandleSetWidgetProperties(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleSetWidgetProperties(Params);
}
//////////////////////////////////////////////////////////////////////////
// Blueprint Utility Command Handlers

FString FAIExportTCPServer::HandleReparentBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleReparentBlueprint(Params);
}
FString FAIExportTCPServer::HandleCompileAndSave(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleCompileAndSave(Params);
}
FString FAIExportTCPServer::HandleGetWidgetTree(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Widget::HandleGetWidgetTree(Params);
}
FString FAIExportTCPServer::HandleListWidgetClasses()
{
	return CommonAIExport::CommandHandlers::Widget::HandleListWidgetClasses();
}
//////////////////////////////////////////////////////////////////////////
// Material Builder Handlers

FString FAIExportTCPServer::HandleCreateMaterial(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleCreateMaterial(Params);
}
FString FAIExportTCPServer::HandleSetMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSetMaterialProperty(Params);
}
FString FAIExportTCPServer::HandleAddExpression(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleAddExpression(Params);
}
FString FAIExportTCPServer::HandleSetExpressionProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSetExpressionProperty(Params);
}
FString FAIExportTCPServer::HandleConnectExpressions(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleConnectExpressions(Params);
}
FString FAIExportTCPServer::HandleConnectToMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleConnectToMaterialProperty(Params);
}
FString FAIExportTCPServer::HandleDisconnectInput(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleDisconnectInput(Params);
}
FString FAIExportTCPServer::HandleRemoveExpression(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleRemoveExpression(Params);
}
FString FAIExportTCPServer::HandleCompileMaterial(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleCompileMaterial(Params);
}
FString FAIExportTCPServer::HandleGetMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleGetMaterialGraph(Params);
}
FString FAIExportTCPServer::HandleListExpressionClasses()
{
	return CommonAIExport::CommandHandlers::Material::HandleListExpressionClasses();
}
FString FAIExportTCPServer::HandleCreateMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleCreateMaterialInstance(Params);
}
FString FAIExportTCPServer::HandleSetInstanceParameter(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSetInstanceParameter(Params);
}
FString FAIExportTCPServer::HandleSaveMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleSaveMaterialInstance(Params);
}
FString FAIExportTCPServer::HandleGetMaterialInstanceInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Material::HandleGetMaterialInstanceInfo(Params);
}
// =============================================================================
// DATA ASSET COMMANDS
// =============================================================================

FString FAIExportTCPServer::HandleSaveDataAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::DataAsset::HandleSaveDataAsset(Params);
}
// =============================================================================
// Asset Import Commands
// =============================================================================

FString FAIExportTCPServer::HandleImportTexture(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Import::HandleImportTexture(Params);
}
FString FAIExportTCPServer::HandleImportFont(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Import::HandleImportFont(Params);
}
// =============================================================================
// CDO PROPERTY HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleSetCDOProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleSetCDOProperty(Params);
}
FString FAIExportTCPServer::HandleGetCDOProperties(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleGetCDOProperties(Params);
}
// =============================================================================
// CDO ARRAY PROPERTY HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleAddCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleAddCDOArrayElement(Params);
}
FString FAIExportTCPServer::HandleSetCDOArrayElementProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleSetCDOArrayElementProperty(Params);
}
FString FAIExportTCPServer::HandleRemoveCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleRemoveCDOArrayElement(Params);
}
FString FAIExportTCPServer::HandleGetCDOArrayLength(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::CDO::HandleGetCDOArrayLength(Params);
}
// =============================================================================
// BLUEPRINT GRAPH HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleAddEventNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddEventNode(Params);
}
FString FAIExportTCPServer::HandleAddCustomEvent(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddCustomEvent(Params);
}
FString FAIExportTCPServer::HandleAddFunctionCallNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddFunctionCallNode(Params);
}
FString FAIExportTCPServer::HandleAddVariableGetNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddVariableGetNode(Params);
}
FString FAIExportTCPServer::HandleAddVariableSetNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddVariableSetNode(Params);
}
FString FAIExportTCPServer::HandleAddMakeStructNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddMakeStructNode(Params);
}
FString FAIExportTCPServer::HandleAddBranchNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddBranchNode(Params);
}
FString FAIExportTCPServer::HandleAddCallParentFunction(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddCallParentFunction(Params);
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
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleEnsureFunctionGraph(Params);
}
FString FAIExportTCPServer::HandleConnectPins(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleConnectPins(Params);
}
FString FAIExportTCPServer::HandleSetPinDefault(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleSetPinDefault(Params);
}
FString FAIExportTCPServer::HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleRemoveGraphNode(Params);
}
FString FAIExportTCPServer::HandleGetGraph(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleGetGraph(Params);
}
FString FAIExportTCPServer::HandleListGraphs(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleListGraphs(Params);
}
// =============================================================================
// BLUEPRINT VARIABLE HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleAddVariable(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleAddVariable(Params);
}
FString FAIExportTCPServer::HandleSetVariableDefault(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleSetVariableDefault(Params);
}
FString FAIExportTCPServer::HandleRemoveVariable(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleRemoveVariable(Params);
}
FString FAIExportTCPServer::HandleGetVariables(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::BlueprintGraph::HandleGetVariables(Params);
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
	return CommonAIExport::CommandHandlers::Asset::HandleCreateAsset(Params);
}
FString FAIExportTCPServer::HandleSetAssetProperty(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleSetAssetProperty(Params);
}
FString FAIExportTCPServer::HandleGetAssetProperties(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleGetAssetProperties(Params);
}
FString FAIExportTCPServer::HandleAssetExists(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleAssetExists(Params);
}
FString FAIExportTCPServer::HandleScanAssetPaths(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleScanAssetPaths(Params);
}
FString FAIExportTCPServer::HandleAssetSearch(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleAssetSearch(Params);
}
FString FAIExportTCPServer::HandleAssetValidateLight(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleAssetValidateLight(Params);
}
FString FAIExportTCPServer::HandleSaveAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleSaveAsset(Params);
}
FString FAIExportTCPServer::HandleRenameAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleRenameAsset(Params);
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
	return CommonAIExport::CommandHandlers::Asset::HandleGetReferencers(Params);
}
FString FAIExportTCPServer::HandleGetDependencies(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleGetDependencies(Params);
}
FString FAIExportTCPServer::HandleDeleteAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleDeleteAsset(Params);
}
FString FAIExportTCPServer::HandleListRedirectors(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleListRedirectors(Params);
}
FString FAIExportTCPServer::HandleFixupRedirectors(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Asset::HandleFixupRedirectors(Params);
}
// =============================================================================
// Input Mapping Context Command Handlers
// =============================================================================

FString FAIExportTCPServer::HandleAddInputMapping(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Input::HandleAddInputMapping(Params);
}
FString FAIExportTCPServer::HandleRemoveInputMapping(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Input::HandleRemoveInputMapping(Params);
}
FString FAIExportTCPServer::HandleGetInputMappings(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Input::HandleGetInputMappings(Params);
}
// =============================================================================
// AnimBlueprint Builder Command Handlers
// =============================================================================

FString FAIExportTCPServer::HandleCreateAnimBlueprint(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AnimBlueprint::HandleCreateAnimBlueprint(Params);
}
FString FAIExportTCPServer::HandleGetAnimBlueprintInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AnimBlueprint::HandleGetAnimBlueprintInfo(Params);
}
//////////////////////////////////////////////////////////////////////////
// Widget Preview Capture — IFTP verify loop

FString FAIExportTCPServer::HandleCaptureWidgetPreview(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::WidgetPreview::HandleCaptureWidgetPreview(Params);
}
//////////////////////////////////////////////////////////////////////////
// Asset Lifecycle — Reload asset (fixes cached editor tab after compile_and_save)

FString FAIExportTCPServer::HandleReloadAsset(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::AssetLifecycle::HandleReloadAsset(Params);
}
