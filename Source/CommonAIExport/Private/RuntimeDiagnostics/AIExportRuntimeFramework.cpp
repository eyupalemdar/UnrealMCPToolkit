// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/AIExportRuntimeFramework.h"

#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/OnlineSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "UObject/Package.h"

namespace CommonAIExport::RuntimeDiagnostics
{
namespace
{
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


TSharedPtr<FJsonObject> BuildRuntimeSubsystemJson(const USubsystem* Subsystem)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Subsystem);
	if (!Subsystem)
	{
		return Data;
	}

	if (const UObject* Outer = Subsystem->GetOuter())
	{
		Data->SetObjectField(TEXT("outer"), BuildObjectReferenceJson(Outer));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeLocalPlayerJson(ULocalPlayer* LocalPlayer, UWorld* World, bool bIncludeSubsystems, int32 SubsystemLimit)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(LocalPlayer);
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
		Data->SetObjectField(TEXT("player_controller"), BuildObjectReferenceJson(PlayerController));
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			Data->SetObjectField(TEXT("pawn"), BuildActorJson(Pawn));
		}
	}

	if (LocalPlayer->ViewportClient)
	{
		Data->SetObjectField(TEXT("viewport_client"), BuildObjectReferenceJson(LocalPlayer->ViewportClient));
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
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Connection);
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

	Data->SetObjectField(TEXT("driver"), BuildObjectReferenceJson(Connection->GetDriver()));
	Data->SetObjectField(TEXT("player_controller"), BuildObjectReferenceJson(Connection->PlayerController));
	Data->SetObjectField(TEXT("owning_actor"), BuildObjectReferenceJson(Connection->OwningActor));
	Data->SetObjectField(TEXT("view_target"), BuildObjectReferenceJson(Connection->ViewTarget));
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimeNetDriverJson(UNetDriver* NetDriver, bool bIncludeConnections = false, int32 ConnectionLimit = 0, bool bIncludeURLOptions = false, int32 URLOptionLimit = 0)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(NetDriver);
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
			: BuildObjectReferenceJson(NetDriver->ServerConnection));
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

}

TSharedPtr<FJsonObject> BuildGameInstanceDiagnostics(TSharedPtr<FJsonObject> Params)
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
		}
		else
		{
			if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; game instance diagnostics reflect the editor world and may not have a runtime GameInstance")));
			}

			Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
			UGameInstance* GameInstance = World->GetGameInstance();
			Data->SetBoolField(TEXT("game_instance_available"), GameInstance != nullptr);
			if (GameInstance)
			{
				TSharedPtr<FJsonObject> GameInstanceJson = BuildObjectReferenceJson(GameInstance);
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
				Data->SetObjectField(TEXT("game_instance"), BuildObjectReferenceJson(nullptr));
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
		return Data;
}


TSharedPtr<FJsonObject> BuildLevelTravelDiagnostics(TSharedPtr<FJsonObject> Params)
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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; level travel diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));

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
		return Data;
}


TSharedPtr<FJsonObject> BuildMultiplayerConnectionDiagnostics(TSharedPtr<FJsonObject> Params)
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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; multiplayer diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));

		UGameInstance* GameInstance = World->GetGameInstance();
		TSharedPtr<FJsonObject> OnlineSessionJson = MakeShared<FJsonObject>();
		OnlineSessionJson->SetBoolField(TEXT("game_instance_available"), GameInstance != nullptr);
		if (GameInstance)
		{
			OnlineSessionJson->SetObjectField(TEXT("game_instance"), BuildObjectReferenceJson(GameInstance));
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

				TSharedPtr<FJsonObject> ControllerJson = BuildObjectReferenceJson(Controller);
				ControllerJson->SetBoolField(TEXT("is_local_controller"), Controller->IsLocalController());
				ControllerJson->SetStringField(TEXT("net_mode"), NetModeToString(Controller->GetNetMode()));
				if (APawn* Pawn = Controller->GetPawn())
				{
					ControllerJson->SetObjectField(TEXT("pawn"), BuildActorJson(Pawn));
				}
				if (UNetConnection* NetConnection = Controller->GetNetConnection())
				{
					ControllerJson->SetBoolField(TEXT("net_connection_present"), true);
					ControllerJson->SetObjectField(TEXT("net_connection"), bIncludeConnections
						? BuildRuntimeNetConnectionJson(NetConnection, bIncludeURLOptions, URLOptionLimit)
						: BuildObjectReferenceJson(NetConnection));
				}
				else
				{
					ControllerJson->SetBoolField(TEXT("net_connection_present"), false);
				}
				if (Controller->PlayerState)
				{
					ControllerJson->SetStringField(TEXT("player_state_name"), Controller->PlayerState->GetPlayerName());
					ControllerJson->SetObjectField(TEXT("player_state"), BuildObjectReferenceJson(Controller->PlayerState));
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
				ContextJson->SetObjectField(TEXT("game_viewport"), BuildObjectReferenceJson(WorldContext->GameViewport));
				ContextJson->SetObjectField(TEXT("owning_game_instance"), BuildObjectReferenceJson(WorldContext->OwningGameInstance));

				if (UPendingNetGame* PendingNetGame = WorldContext->PendingNetGame)
				{
					TSharedPtr<FJsonObject> PendingJson = BuildObjectReferenceJson(PendingNetGame);
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
		return Data;
}


TSharedPtr<FJsonObject> BuildReplicationDiagnostics(TSharedPtr<FJsonObject> Params)
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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; replication diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
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
			AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName);
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

			TSharedPtr<FJsonObject> ActorJson = BuildActorJson(Actor);
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
		return Data;
}


}
