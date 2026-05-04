// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTLevelStructureCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Async/Async.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/HLODProxy.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/LevelStreaming.h"
#include "Engine/LODActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceComponent.h"
#include "LevelInstance/LevelInstanceTypes.h"
#include "UObject/Package.h"
#include "WorldPartition/ActorDescContainerInstanceCollection.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerType.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODSourceActors.h"
#include "WorldPartition/WorldPartition.h"

namespace MCPToolkit::CommandHandlers::LevelStructure
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FString& DefaultValue = TEXT(""))
{
	FString Value = DefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
		Value.TrimStartAndEndInline();
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const bool bDefaultValue)
{
	bool bValue = bDefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

int32 ReadIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const int32 DefaultValue, const int32 MinValue, const int32 MaxValue)
{
	double NumberValue = static_cast<double>(DefaultValue);
	if (Params.IsValid())
	{
		Params->TryGetNumberField(FieldName, NumberValue);
	}
	return FMath::Clamp(FMath::RoundToInt(NumberValue), MinValue, MaxValue);
}

FString EnumValueToString(const UEnum* Enum, const int64 Value)
{
	return Enum ? Enum->GetNameStringByValue(Value) : FString::FromInt(static_cast<int32>(Value));
}

FString GuidToString(const FGuid& Guid)
{
	return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
}

FString UInt64ToString(const uint64 Value)
{
	return FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Value));
}

TSharedPtr<FJsonObject> BuildTransformJson(const FTransform& Transform)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("location"), BuildVectorJson(Transform.GetLocation()));
	Data->SetObjectField(TEXT("rotation"), BuildRotatorJson(Transform.Rotator()));
	Data->SetObjectField(TEXT("scale"), BuildVectorJson(Transform.GetScale3D()));
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

int32 CountValidActors(const ULevel* Level)
{
	int32 Count = 0;
	if (!Level)
	{
		return Count;
	}

	for (const TObjectPtr<AActor>& Actor : Level->Actors)
	{
		if (Actor)
		{
			++Count;
		}
	}
	return Count;
}

TSharedPtr<FJsonObject> BuildLevelJson(ULevel* Level, const UWorld* World, const bool bIsPersistent)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Level);
	Data->SetBoolField(TEXT("is_persistent"), bIsPersistent);
	if (!Level)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Level->GetName());
	Data->SetStringField(TEXT("package_name"), Level->GetOutermost() ? Level->GetOutermost()->GetName() : FString());
	Data->SetBoolField(TEXT("visible"), Level->bIsVisible != 0);
	Data->SetBoolField(TEXT("using_external_actors"), Level->IsUsingExternalActors());
	Data->SetNumberField(TEXT("actor_count"), CountValidActors(Level));
	Data->SetBoolField(TEXT("is_world_persistent_level"), World && Level == World->PersistentLevel);
	Data->SetObjectField(TEXT("level_script_actor"), BuildActorJson(Level->GetLevelScriptActor()));
	return Data;
}

TSharedPtr<FJsonObject> BuildStreamingLevelJson(const ULevelStreaming* StreamingLevel, const int32 Index, const UWorld* World)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(StreamingLevel);
	Data->SetNumberField(TEXT("index"), Index);
	if (!StreamingLevel)
	{
		return Data;
	}

	ULevel* LoadedLevel = StreamingLevel->GetLoadedLevel();
	Data->SetStringField(TEXT("class_name"), StreamingLevel->GetClass() ? StreamingLevel->GetClass()->GetName() : FString());
	Data->SetStringField(TEXT("world_asset_package_name"), StreamingLevel->GetWorldAssetPackageName());
	Data->SetStringField(TEXT("loaded_level_package_name"), LoadedLevel && LoadedLevel->GetOutermost() ? LoadedLevel->GetOutermost()->GetName() : FString());
	Data->SetStringField(TEXT("state"), FString(EnumToString(StreamingLevel->GetLevelStreamingState())));
	Data->SetBoolField(TEXT("is_level_loaded"), StreamingLevel->IsLevelLoaded());
	Data->SetBoolField(TEXT("is_level_visible"), StreamingLevel->IsLevelVisible());
	Data->SetBoolField(TEXT("should_be_loaded"), StreamingLevel->ShouldBeLoaded());
	Data->SetBoolField(TEXT("should_be_visible"), StreamingLevel->ShouldBeVisible());
	Data->SetBoolField(TEXT("should_be_always_loaded"), StreamingLevel->ShouldBeAlwaysLoaded());
	Data->SetBoolField(TEXT("should_be_visible_flag"), StreamingLevel->GetShouldBeVisibleFlag());
	Data->SetBoolField(TEXT("should_be_visible_in_editor"), StreamingLevel->GetShouldBeVisibleInEditor());
	Data->SetBoolField(TEXT("should_block_on_load"), StreamingLevel->bShouldBlockOnLoad != 0);
	Data->SetBoolField(TEXT("should_block_on_unload"), StreamingLevel->bShouldBlockOnUnload != 0);
	Data->SetBoolField(TEXT("disable_distance_streaming"), StreamingLevel->bDisableDistanceStreaming != 0);
	Data->SetObjectField(TEXT("level_transform"), BuildTransformJson(StreamingLevel->LevelTransform));
	Data->SetObjectField(TEXT("loaded_level"), BuildLevelJson(LoadedLevel, World, World && LoadedLevel == World->PersistentLevel));
	Data->SetNumberField(TEXT("loaded_actor_count"), CountValidActors(LoadedLevel));
	return Data;
}

TSharedPtr<FJsonObject> BuildWorldPartitionJson(UWorld* World)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;
	Data->SetBoolField(TEXT("present"), WorldPartition != nullptr);
	Data->SetBoolField(TEXT("is_partitioned_world"), World ? World->IsPartitionedWorld() : false);
	if (!WorldPartition)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("world_partition"), BuildObjectReferenceJson(WorldPartition));
	Data->SetBoolField(TEXT("initialized"), WorldPartition->IsInitialized());
	Data->SetBoolField(TEXT("streaming_supported"), WorldPartition->SupportsStreaming());
	Data->SetBoolField(TEXT("streaming_enabled"), WorldPartition->IsStreamingEnabled());
#if WITH_EDITOR
	Data->SetBoolField(TEXT("streaming_enabled_in_editor"), WorldPartition->IsStreamingEnabledInEditor());
	Data->SetStringField(TEXT("editor_name"), WorldPartition->GetWorldPartitionEditorName().ToString());
#else
	Data->SetBoolField(TEXT("streaming_enabled_in_editor"), false);
	Data->SetStringField(TEXT("editor_name"), FString());
#endif
	Data->SetNumberField(TEXT("actor_desc_container_count"), WorldPartition->GetActorDescContainerCount());
	Data->SetObjectField(TEXT("editor_world_bounds"), BuildBoxJson(WorldPartition->GetEditorWorldBounds()));
	Data->SetObjectField(TEXT("runtime_world_bounds"), BuildBoxJson(WorldPartition->GetRuntimeWorldBounds()));
	Data->SetObjectField(TEXT("default_hlod_layer"), BuildObjectReferenceJson(WorldPartition->GetDefaultHLODLayer()));
	Data->SetBoolField(TEXT("standalone_hlod_world"), WorldPartition->IsStandaloneHLODWorld());
	Data->SetBoolField(TEXT("has_standalone_hlod"), WorldPartition->HasStandaloneHLOD());
	return Data;
}

TSharedPtr<FJsonObject> BuildDataLayerJson(UDataLayerInstance* DataLayer, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(DataLayer);
	Data->SetNumberField(TEXT("index"), Index);
	if (!DataLayer)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), DataLayer->GetDataLayerFName().ToString());
	Data->SetStringField(TEXT("short_name"), DataLayer->GetDataLayerShortName());
	Data->SetStringField(TEXT("full_name"), DataLayer->GetDataLayerFullName());
	Data->SetStringField(TEXT("type"), EnumValueToString(StaticEnum<EDataLayerType>(), static_cast<int64>(DataLayer->GetType())));
	Data->SetBoolField(TEXT("runtime"), DataLayer->IsRuntime());
	Data->SetBoolField(TEXT("client_only"), DataLayer->IsClientOnly());
	Data->SetBoolField(TEXT("server_only"), DataLayer->IsServerOnly());
	Data->SetStringField(TEXT("initial_runtime_state"), GetDataLayerRuntimeStateName(DataLayer->GetInitialRuntimeState()));
	Data->SetStringField(TEXT("runtime_state"), GetDataLayerRuntimeStateName(DataLayer->GetRuntimeState()));
	Data->SetStringField(TEXT("effective_runtime_state"), GetDataLayerRuntimeStateName(DataLayer->GetEffectiveRuntimeState()));
	Data->SetBoolField(TEXT("initially_visible"), DataLayer->IsInitiallyVisible());
	Data->SetBoolField(TEXT("visible"), DataLayer->IsVisible());
	Data->SetBoolField(TEXT("effective_visible"), DataLayer->IsEffectiveVisible());
	Data->SetObjectField(TEXT("debug_color"), BuildColorJson(DataLayer->GetDebugColor()));
	Data->SetObjectField(TEXT("asset"), BuildObjectReferenceJson(DataLayer->GetAsset()));
	Data->SetObjectField(TEXT("parent"), BuildObjectReferenceJson(DataLayer->GetParent()));
	Data->SetStringField(TEXT("parent_name"), DataLayer->GetParent() ? DataLayer->GetParent()->GetDataLayerFullName() : FString());
	Data->SetNumberField(TEXT("child_count"), DataLayer->GetChildren().Num());

#if WITH_EDITOR
	FText ReadOnlyReason;
	const bool bReadOnly = DataLayer->IsReadOnly(&ReadOnlyReason);
	Data->SetBoolField(TEXT("effective_loaded_in_editor"), DataLayer->IsEffectiveLoadedInEditor());
	Data->SetBoolField(TEXT("initially_loaded_in_editor"), DataLayer->IsInitiallyLoadedInEditor());
	Data->SetBoolField(TEXT("loaded_in_editor"), DataLayer->IsLoadedInEditor());
	Data->SetBoolField(TEXT("loaded_in_editor_changed_by_user"), DataLayer->IsLoadedInEditorChangedByUserOperation());
	Data->SetBoolField(TEXT("read_only"), bReadOnly);
	Data->SetStringField(TEXT("read_only_reason"), ReadOnlyReason.ToString());
	Data->SetBoolField(TEXT("supports_actor_filters"), DataLayer->SupportsActorFilters());
	Data->SetBoolField(TEXT("included_in_actor_filter_default"), DataLayer->IsIncludedInActorFilterDefault());
	Data->SetStringField(TEXT("override_block_on_slow_streaming"), EnumValueToString(StaticEnum<EOverrideBlockOnSlowStreaming>(), static_cast<int64>(DataLayer->GetOverrideBlockOnSlowStreaming())));
	Data->SetNumberField(TEXT("streaming_priority"), DataLayer->GetStreamingPriority());
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildDataLayersJson(UWorld* World, const int32 Limit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AWorldDataLayers* WorldDataLayers = World ? World->GetWorldDataLayers() : nullptr;
	Data->SetBoolField(TEXT("present"), WorldDataLayers != nullptr);
	Data->SetObjectField(TEXT("world_data_layers"), BuildObjectReferenceJson(WorldDataLayers));

	TArray<TSharedPtr<FJsonValue>> LayersJson;
	int32 TotalCount = 0;
	if (WorldDataLayers)
	{
		Data->SetBoolField(TEXT("supports_external_package_instances"), WorldDataLayers->SupportsExternalPackageDataLayerInstances());
		Data->SetBoolField(TEXT("using_external_package_instances"), WorldDataLayers->IsUsingExternalPackageDataLayerInstances());
		WorldDataLayers->ForEachDataLayerInstance([&LayersJson, &TotalCount, Limit](UDataLayerInstance* DataLayer)
		{
			const int32 Index = TotalCount++;
			if (LayersJson.Num() < Limit)
			{
				LayersJson.Add(MakeShared<FJsonValueObject>(BuildDataLayerJson(DataLayer, Index)));
			}
			return true;
		});
	}
	else
	{
		Data->SetBoolField(TEXT("supports_external_package_instances"), false);
		Data->SetBoolField(TEXT("using_external_package_instances"), false);
	}

	Data->SetNumberField(TEXT("count"), TotalCount);
	Data->SetBoolField(TEXT("truncated"), LayersJson.Num() < TotalCount);
	Data->SetArrayField(TEXT("layers"), LayersJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildLevelInstanceJson(ALevelInstance* LevelInstance, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildActorJson(LevelInstance);
	Data->SetNumberField(TEXT("index"), Index);
	if (!LevelInstance)
	{
		return Data;
	}

	const FLevelInstanceID& LevelInstanceID = LevelInstance->GetLevelInstanceID();
	Data->SetStringField(TEXT("world_asset_path"), LevelInstance->GetWorldAsset().ToSoftObjectPath().ToString());
	Data->SetStringField(TEXT("level_instance_guid"), GuidToString(LevelInstance->GetLevelInstanceGuid()));
	Data->SetBoolField(TEXT("level_instance_id_valid"), LevelInstanceID.IsValid());
	Data->SetStringField(TEXT("level_instance_id_hash"), UInt64ToString(LevelInstanceID.GetHash()));
	Data->SetBoolField(TEXT("loading_enabled"), LevelInstance->IsLoadingEnabled());

#if WITH_EDITOR
	Data->SetStringField(TEXT("desired_runtime_behavior"), EnumValueToString(StaticEnum<ELevelInstanceRuntimeBehavior>(), static_cast<int64>(LevelInstance->GetDesiredRuntimeBehavior())));
	Data->SetObjectField(TEXT("level_instance_component"), BuildComponentJson(LevelInstance->GetLevelInstanceComponent()));
	Data->SetBoolField(TEXT("supports_partial_editor_loading"), LevelInstance->SupportsPartialEditorLoading());
	Data->SetBoolField(TEXT("supports_property_overrides"), LevelInstance->SupportsPropertyOverrides());
	Data->SetObjectField(TEXT("property_override_asset"), BuildObjectReferenceJson(LevelInstance->GetPropertyOverrideAsset()));
	Data->SetBoolField(TEXT("hlod_relevant"), LevelInstance->IsHLODRelevant());
	Data->SetBoolField(TEXT("has_hlod_relevant_components"), LevelInstance->HasHLODRelevantComponents());
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildClassicHLODJson(ALODActor* HLODActor, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildActorJson(HLODActor);
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("kind"), TEXT("classic_hlod"));
	if (!HLODActor)
	{
		return Data;
	}

	UStaticMeshComponent* StaticMeshComponent = HLODActor->GetStaticMeshComponent();
	Data->SetNumberField(TEXT("lod_level"), HLODActor->LODLevel);
	Data->SetNumberField(TEXT("draw_distance"), HLODActor->GetLODDrawDistance());
	Data->SetNumberField(TEXT("draw_distance_with_override"), HLODActor->GetLODDrawDistanceWithOverride());
	Data->SetNumberField(TEXT("sub_actor_count"), HLODActor->SubActors.Num());
	Data->SetNumberField(TEXT("cached_num_hlod_levels"), HLODActor->CachedNumHLODLevels);
	Data->SetBoolField(TEXT("has_valid_lod_children"), HLODActor->HasValidLODChildren());
	Data->SetObjectField(TEXT("static_mesh_component"), BuildComponentJson(StaticMeshComponent));
	Data->SetObjectField(TEXT("static_mesh"), BuildObjectReferenceJson(StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr));
	Data->SetObjectField(TEXT("proxy"), BuildObjectReferenceJson(HLODActor->GetProxy()));
	Data->SetNumberField(TEXT("instanced_static_mesh_component_count"), HLODActor->GetInstancedStaticMeshComponents().Num());
#if WITH_EDITOR
	Data->SetBoolField(TEXT("has_sub_actors"), HLODActor->HasAnySubActors());
	Data->SetBoolField(TEXT("valid_sub_actors"), HLODActor->HasValidSubActors());
	Data->SetBoolField(TEXT("built_from_hlod_desc"), HLODActor->WasBuiltFromHLODDesc());
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildWorldPartitionHLODJson(AWorldPartitionHLOD* HLODActor, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildActorJson(HLODActor);
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("kind"), TEXT("world_partition_hlod"));
	if (!HLODActor)
	{
		return Data;
	}

	Data->SetNumberField(TEXT("lod_level"), HLODActor->GetLODLevel());
	Data->SetStringField(TEXT("name_or_label"), HLODActor->GetHLODNameOrLabel());
	Data->SetBoolField(TEXT("requires_warmup"), HLODActor->DoesRequireWarmup());
	Data->SetNumberField(TEXT("assets_to_warmup_count"), HLODActor->GetAssetsToWarmup().Num());
	Data->SetStringField(TEXT("source_cell_guid"), GuidToString(HLODActor->GetSourceCellGuid()));
	Data->SetBoolField(TEXT("standalone"), HLODActor->IsStandalone());
	Data->SetStringField(TEXT("standalone_hlod_guid"), GuidToString(HLODActor->GetStandaloneHLODGuid()));
	Data->SetBoolField(TEXT("custom_hlod"), HLODActor->IsCustomHLOD());
	Data->SetStringField(TEXT("custom_hlod_guid"), GuidToString(HLODActor->GetCustomHLODGuid()));
#if WITH_EDITOR
	Data->SetObjectField(TEXT("hlod_bounds"), BuildBoxJson(HLODActor->GetHLODBounds()));
	Data->SetNumberField(TEXT("min_visible_distance"), HLODActor->GetMinVisibleDistance());
	Data->SetNumberField(TEXT("hlod_hash"), HLODActor->GetHLODHash());
	Data->SetObjectField(TEXT("source_actors"), BuildObjectReferenceJson(HLODActor->GetSourceActors()));
#endif
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildLevelsArrayJson(UWorld* World, const int32 LevelLimit, int32& OutTotalCount)
{
	TArray<TSharedPtr<FJsonValue>> LevelsJson;
	OutTotalCount = 0;
	if (!World)
	{
		return LevelsJson;
	}

	for (ULevel* Level : World->GetLevels())
	{
		if (!Level)
		{
			continue;
		}

		const int32 Index = OutTotalCount++;
		if (LevelsJson.Num() < LevelLimit)
		{
			TSharedPtr<FJsonObject> LevelJson = BuildLevelJson(Level, World, Level == World->PersistentLevel);
			LevelJson->SetNumberField(TEXT("index"), Index);
			LevelsJson.Add(MakeShared<FJsonValueObject>(LevelJson));
		}
	}
	return LevelsJson;
}

TArray<TSharedPtr<FJsonValue>> BuildStreamingLevelsArrayJson(UWorld* World, const int32 LevelLimit, int32& OutTotalCount)
{
	TArray<TSharedPtr<FJsonValue>> StreamingLevelsJson;
	OutTotalCount = 0;
	if (!World)
	{
		return StreamingLevelsJson;
	}

	for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel)
		{
			continue;
		}

		const int32 Index = OutTotalCount++;
		if (StreamingLevelsJson.Num() < LevelLimit)
		{
			StreamingLevelsJson.Add(MakeShared<FJsonValueObject>(BuildStreamingLevelJson(StreamingLevel, Index, World)));
		}
	}
	return StreamingLevelsJson;
}

TSharedPtr<FJsonObject> BuildLevelInstancesJson(UWorld* World, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> InstancesJson;
	int32 TotalCount = 0;

	if (World)
	{
		for (TActorIterator<ALevelInstance> It(World); It; ++It)
		{
			ALevelInstance* LevelInstance = *It;
			if (!LevelInstance)
			{
				continue;
			}

			const int32 Index = TotalCount++;
			if (InstancesJson.Num() < Limit)
			{
				InstancesJson.Add(MakeShared<FJsonValueObject>(BuildLevelInstanceJson(LevelInstance, Index)));
			}
		}
	}

	Data->SetNumberField(TEXT("count"), TotalCount);
	Data->SetBoolField(TEXT("truncated"), InstancesJson.Num() < TotalCount);
	Data->SetArrayField(TEXT("instances"), InstancesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildHLODActorsJson(UWorld* World, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> HLODActorsJson;
	int32 TotalCount = 0;

	if (World)
	{
		for (TActorIterator<ALODActor> It(World); It; ++It)
		{
			ALODActor* HLODActor = *It;
			if (!HLODActor)
			{
				continue;
			}

			const int32 Index = TotalCount++;
			if (HLODActorsJson.Num() < Limit)
			{
				HLODActorsJson.Add(MakeShared<FJsonValueObject>(BuildClassicHLODJson(HLODActor, Index)));
			}
		}

		for (TActorIterator<AWorldPartitionHLOD> It(World); It; ++It)
		{
			AWorldPartitionHLOD* HLODActor = *It;
			if (!HLODActor)
			{
				continue;
			}

			const int32 Index = TotalCount++;
			if (HLODActorsJson.Num() < Limit)
			{
				HLODActorsJson.Add(MakeShared<FJsonValueObject>(BuildWorldPartitionHLODJson(HLODActor, Index)));
			}
		}
	}

	Data->SetNumberField(TEXT("count"), TotalCount);
	Data->SetBoolField(TEXT("truncated"), HLODActorsJson.Num() < TotalCount);
	Data->SetArrayField(TEXT("actors"), HLODActorsJson);
	return Data;
}
}

FString HandleLevelStructureInfo(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadStringField(Params, TEXT("world"), TEXT("editor"));
	const bool bIncludeStreamingLevels = ReadBoolField(Params, TEXT("include_streaming_levels"), true);
	const bool bIncludeWorldPartition = ReadBoolField(Params, TEXT("include_world_partition"), true);
	const bool bIncludeDataLayers = ReadBoolField(Params, TEXT("include_data_layers"), true);
	const bool bIncludeLevelInstances = ReadBoolField(Params, TEXT("include_level_instances"), true);
	const bool bIncludeHLOD = ReadBoolField(Params, TEXT("include_hlod"), true);
	const int32 LevelLimit = ReadIntField(Params, TEXT("level_limit"), 200, 0, 5000);
	const int32 DataLayerLimit = ReadIntField(Params, TEXT("data_layer_limit"), 200, 0, 5000);
	const int32 LevelInstanceLimit = ReadIntField(Params, TEXT("level_instance_limit"), 200, 0, 5000);
	const int32 HLODLimit = ReadIntField(Params, TEXT("hlod_limit"), 200, 0, 5000);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [
		WorldSelector,
		bIncludeStreamingLevels,
		bIncludeWorldPartition,
		bIncludeDataLayers,
		bIncludeLevelInstances,
		bIncludeHLOD,
		LevelLimit,
		DataLayerLimit,
		LevelInstanceLimit,
		HLODLimit,
		Promise]()
	{
		using namespace MCPToolkit::RuntimeDiagnostics;

		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("No world is available for selector '%s'"), *WorldSelector)));
			return;
		}

		int32 LevelCount = 0;
		TArray<TSharedPtr<FJsonValue>> LevelsJson = BuildLevelsArrayJson(World, LevelLimit, LevelCount);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetStringField(TEXT("map_filename"), FEditorFileUtils::GetFilename(World));
		Data->SetNumberField(TEXT("loaded_level_count"), LevelCount);
		Data->SetBoolField(TEXT("loaded_levels_truncated"), LevelsJson.Num() < LevelCount);
		Data->SetArrayField(TEXT("loaded_levels"), LevelsJson);
		Data->SetObjectField(TEXT("persistent_level"), BuildLevelJson(World->PersistentLevel, World, true));

		if (bIncludeStreamingLevels)
		{
			int32 StreamingLevelCount = 0;
			TArray<TSharedPtr<FJsonValue>> StreamingLevelsJson = BuildStreamingLevelsArrayJson(World, LevelLimit, StreamingLevelCount);
			TSharedPtr<FJsonObject> StreamingData = MakeShared<FJsonObject>();
			StreamingData->SetNumberField(TEXT("count"), StreamingLevelCount);
			StreamingData->SetBoolField(TEXT("truncated"), StreamingLevelsJson.Num() < StreamingLevelCount);
			StreamingData->SetArrayField(TEXT("levels"), StreamingLevelsJson);
			Data->SetObjectField(TEXT("streaming_levels"), StreamingData);
		}

		if (bIncludeWorldPartition)
		{
			Data->SetObjectField(TEXT("world_partition"), BuildWorldPartitionJson(World));
		}

		if (bIncludeDataLayers)
		{
			Data->SetObjectField(TEXT("data_layers"), BuildDataLayersJson(World, DataLayerLimit));
		}

		if (bIncludeLevelInstances)
		{
			Data->SetObjectField(TEXT("level_instances"), BuildLevelInstancesJson(World, LevelInstanceLimit));
		}

		if (bIncludeHLOD)
		{
			Data->SetObjectField(TEXT("hlod_actors"), BuildHLODActorsJson(World, HLODLimit));
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Level structure info timed out"));
	}
	return Future.Get();
}
}
