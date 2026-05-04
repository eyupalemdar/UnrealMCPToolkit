// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTFoliageCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Async/Async.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FoliageType.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Materials/MaterialInterface.h"
#include "UObject/Package.h"

namespace MCPToolkit::CommandHandlers::Foliage
{
namespace
{
struct FFoliageEntry
{
	UFoliageType* FoliageType = nullptr;
	const FFoliageInfo* FoliageInfo = nullptr;
};

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

double ReadDoubleField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const double DefaultValue, const double MinValue, const double MaxValue)
{
	double NumberValue = DefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetNumberField(FieldName, NumberValue);
	}
	return FMath::Clamp(NumberValue, MinValue, MaxValue);
}

bool TryReadRequiredDouble(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, double& OutValue)
{
	return Params.IsValid() && Params->TryGetNumberField(FieldName, OutValue);
}

FString EnumValueToString(const UEnum* Enum, const int64 Value)
{
	return Enum ? Enum->GetNameStringByValue(Value) : FString::FromInt(static_cast<int32>(Value));
}

FString MobilityToString(const EComponentMobility::Type Mobility)
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
		return TEXT("Unknown");
	}
}

FString ImplementationTypeToString(const EFoliageImplType Type)
{
	switch (Type)
	{
	case EFoliageImplType::StaticMesh:
		return TEXT("StaticMesh");
	case EFoliageImplType::Actor:
		return TEXT("Actor");
	case EFoliageImplType::ISMActor:
		return TEXT("ISMActor");
	case EFoliageImplType::Unknown:
	default:
		return TEXT("Unknown");
	}
}

template <typename TInterval>
TSharedPtr<FJsonObject> BuildIntervalJson(const TInterval& Interval)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("min"), Interval.Min);
	Data->SetNumberField(TEXT("max"), Interval.Max);
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildNameArrayJson(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName& Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> BuildObjectReferenceArrayJson(const TArray<TObjectPtr<UMaterialInterface>>& Objects, const int32 Limit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TArray<TSharedPtr<FJsonValue>> Values;
	for (int32 Index = 0; Index < Objects.Num() && Values.Num() < Limit; ++Index)
	{
		Values.Add(MakeShared<FJsonValueObject>(BuildObjectReferenceJson(Objects[Index].Get())));
	}
	return Values;
}

TSharedPtr<FJsonObject> BuildLightingChannelsJson(const FLightingChannels& LightingChannels)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("channel_0"), LightingChannels.bChannel0);
	Data->SetBoolField(TEXT("channel_1"), LightingChannels.bChannel1);
	Data->SetBoolField(TEXT("channel_2"), LightingChannels.bChannel2);
	return Data;
}

int32 GetFoliageInstanceCount(const FFoliageInfo& FoliageInfo)
{
	if (const UHierarchicalInstancedStaticMeshComponent* Component = FoliageInfo.GetComponent())
	{
		return Component->GetInstanceCount();
	}

#if WITH_EDITOR
	if (FoliageInfo.Implementation)
	{
		return FoliageInfo.Implementation->GetInstanceCount();
	}
#endif

	return 0;
}

bool TryGetInstanceWorldTransform(const FFoliageInfo& FoliageInfo, const int32 InstanceIndex, FTransform& OutTransform)
{
	if (const UHierarchicalInstancedStaticMeshComponent* Component = FoliageInfo.GetComponent())
	{
		return Component->GetInstanceTransform(InstanceIndex, OutTransform, true);
	}

#if WITH_EDITOR
	if (FoliageInfo.Implementation && InstanceIndex >= 0 && InstanceIndex < FoliageInfo.Implementation->GetInstanceCount())
	{
		OutTransform = FoliageInfo.Implementation->GetInstanceWorldTransform(InstanceIndex);
		return true;
	}
#endif

	return false;
}

FString GetFoliageTypeSortKey(const UFoliageType* FoliageType)
{
	if (!FoliageType)
	{
		return FString();
	}
	return FoliageType->GetPathName().ToLower();
}

bool MatchesFoliageTypeFilter(const UFoliageType* FoliageType, const FString& TypeFilter)
{
	if (!FoliageType)
	{
		return false;
	}
	if (TypeFilter.IsEmpty())
	{
		return true;
	}

	const FString Needle = TypeFilter.ToLower();
	if (FoliageType->GetName().ToLower().Contains(Needle)
		|| FoliageType->GetPathName().ToLower().Contains(Needle)
		|| (FoliageType->GetClass() && FoliageType->GetClass()->GetPathName().ToLower().Contains(Needle)))
	{
		return true;
	}

	if (const UObject* Source = FoliageType->GetSource())
	{
		if (Source->GetName().ToLower().Contains(Needle) || Source->GetPathName().ToLower().Contains(Needle))
		{
			return true;
		}
	}

	const UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
	const UStaticMesh* StaticMesh = StaticMeshType ? StaticMeshType->GetStaticMesh() : nullptr;
	return StaticMesh && (StaticMesh->GetName().ToLower().Contains(Needle) || StaticMesh->GetPathName().ToLower().Contains(Needle));
}

TArray<FFoliageEntry> BuildSortedFoliageEntries(AInstancedFoliageActor* FoliageActor, const FString& TypeFilter)
{
	TArray<FFoliageEntry> Entries;
	if (!FoliageActor)
	{
		return Entries;
	}

	const TMap<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliageInfos = FoliageActor->GetFoliageInfos();
	for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : FoliageInfos)
	{
		if (!MatchesFoliageTypeFilter(Pair.Key, TypeFilter))
		{
			continue;
		}

		FFoliageEntry Entry;
		Entry.FoliageType = Pair.Key;
		Entry.FoliageInfo = &Pair.Value.Get();
		Entries.Add(Entry);
	}

	Entries.Sort([](const FFoliageEntry& Left, const FFoliageEntry& Right)
	{
		return GetFoliageTypeSortKey(Left.FoliageType) < GetFoliageTypeSortKey(Right.FoliageType);
	});
	return Entries;
}

TSharedPtr<FJsonObject> BuildFoliageSettingsJson(const UFoliageType* FoliageType)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!FoliageType)
	{
		return Data;
	}

	Data->SetStringField(TEXT("update_guid"), FoliageType->UpdateGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Data->SetNumberField(TEXT("density"), FoliageType->Density);
	Data->SetNumberField(TEXT("density_adjustment_factor"), FoliageType->DensityAdjustmentFactor);
	Data->SetNumberField(TEXT("radius"), FoliageType->Radius);
	Data->SetBoolField(TEXT("single_instance_mode_override_radius"), FoliageType->bSingleInstanceModeOverrideRadius);
	Data->SetNumberField(TEXT("single_instance_mode_radius"), FoliageType->SingleInstanceModeRadius);
	Data->SetStringField(TEXT("scaling"), EnumValueToString(StaticEnum<EFoliageScaling>(), static_cast<int64>(FoliageType->Scaling)));
	Data->SetObjectField(TEXT("scale_x"), BuildIntervalJson(FoliageType->ScaleX));
	Data->SetObjectField(TEXT("scale_y"), BuildIntervalJson(FoliageType->ScaleY));
	Data->SetObjectField(TEXT("scale_z"), BuildIntervalJson(FoliageType->ScaleZ));
	Data->SetObjectField(TEXT("z_offset"), BuildIntervalJson(FoliageType->ZOffset));

	Data->SetBoolField(TEXT("align_to_normal"), FoliageType->AlignToNormal != 0);
	Data->SetBoolField(TEXT("average_normal"), FoliageType->AverageNormal != 0);
	Data->SetBoolField(TEXT("average_normal_single_component"), FoliageType->AverageNormalSingleComponent != 0);
	Data->SetNumberField(TEXT("align_max_angle"), FoliageType->AlignMaxAngle);
	Data->SetBoolField(TEXT("random_yaw"), FoliageType->RandomYaw != 0);
	Data->SetNumberField(TEXT("random_pitch_angle"), FoliageType->RandomPitchAngle);
	Data->SetObjectField(TEXT("ground_slope_angle"), BuildIntervalJson(FoliageType->GroundSlopeAngle));
	Data->SetObjectField(TEXT("height"), BuildIntervalJson(FoliageType->Height));
	Data->SetArrayField(TEXT("landscape_layers"), BuildNameArrayJson(FoliageType->LandscapeLayers));
	Data->SetNumberField(TEXT("minimum_layer_weight"), FoliageType->MinimumLayerWeight);
	Data->SetArrayField(TEXT("exclusion_landscape_layers"), BuildNameArrayJson(FoliageType->ExclusionLandscapeLayers));
	Data->SetNumberField(TEXT("minimum_exclusion_layer_weight"), FoliageType->MinimumExclusionLayerWeight);
	Data->SetBoolField(TEXT("collision_with_world"), FoliageType->CollisionWithWorld != 0);
	Data->SetObjectField(TEXT("collision_scale"), BuildVectorJson(FoliageType->CollisionScale));
	Data->SetNumberField(TEXT("average_normal_sample_count"), FoliageType->AverageNormalSampleCount);

	Data->SetStringField(TEXT("mobility"), MobilityToString(FoliageType->Mobility.GetValue()));
	Data->SetObjectField(TEXT("cull_distance"), BuildIntervalJson(FoliageType->CullDistance));
	Data->SetBoolField(TEXT("cast_shadow"), FoliageType->CastShadow != 0);
	Data->SetBoolField(TEXT("affect_dynamic_indirect_lighting"), FoliageType->bAffectDynamicIndirectLighting != 0);
	Data->SetBoolField(TEXT("affect_distance_field_lighting"), FoliageType->bAffectDistanceFieldLighting != 0);
	Data->SetBoolField(TEXT("cast_dynamic_shadow"), FoliageType->bCastDynamicShadow != 0);
	Data->SetBoolField(TEXT("cast_static_shadow"), FoliageType->bCastStaticShadow != 0);
	Data->SetBoolField(TEXT("cast_contact_shadow"), FoliageType->bCastContactShadow != 0);
	Data->SetBoolField(TEXT("cast_shadow_as_two_sided"), FoliageType->bCastShadowAsTwoSided != 0);
	Data->SetBoolField(TEXT("receives_decals"), FoliageType->bReceivesDecals != 0);
	Data->SetBoolField(TEXT("override_lightmap_resolution"), FoliageType->bOverrideLightMapRes != 0);
	Data->SetNumberField(TEXT("overridden_lightmap_resolution"), FoliageType->OverriddenLightMapRes);
	Data->SetBoolField(TEXT("use_as_occluder"), FoliageType->bUseAsOccluder != 0);
	Data->SetBoolField(TEXT("visible_in_ray_tracing"), FoliageType->bVisibleInRayTracing != 0);
	Data->SetBoolField(TEXT("evaluate_world_position_offset"), FoliageType->bEvaluateWorldPositionOffset != 0);
	Data->SetNumberField(TEXT("world_position_offset_disable_distance"), FoliageType->WorldPositionOffsetDisableDistance);
	Data->SetObjectField(TEXT("lighting_channels"), BuildLightingChannelsJson(FoliageType->LightingChannels));
	Data->SetBoolField(TEXT("render_custom_depth"), FoliageType->bRenderCustomDepth != 0);
	Data->SetNumberField(TEXT("custom_depth_stencil_value"), FoliageType->CustomDepthStencilValue);
	Data->SetNumberField(TEXT("translucency_sort_priority"), FoliageType->TranslucencySortPriority);

	Data->SetNumberField(TEXT("collision_radius"), FoliageType->CollisionRadius);
	Data->SetNumberField(TEXT("shade_radius"), FoliageType->ShadeRadius);
	Data->SetNumberField(TEXT("procedural_num_steps"), FoliageType->NumSteps);
	Data->SetNumberField(TEXT("initial_seed_density"), FoliageType->InitialSeedDensity);
	Data->SetNumberField(TEXT("average_spread_distance"), FoliageType->AverageSpreadDistance);
	Data->SetNumberField(TEXT("spread_variance"), FoliageType->SpreadVariance);
	Data->SetNumberField(TEXT("seeds_per_step"), FoliageType->SeedsPerStep);
	Data->SetNumberField(TEXT("distribution_seed"), FoliageType->DistributionSeed);
	Data->SetNumberField(TEXT("max_initial_seed_offset"), FoliageType->MaxInitialSeedOffset);
	Data->SetBoolField(TEXT("can_grow_in_shade"), FoliageType->bCanGrowInShade);
	Data->SetBoolField(TEXT("spawns_in_shade"), FoliageType->bSpawnsInShade);
	Data->SetNumberField(TEXT("max_initial_age"), FoliageType->MaxInitialAge);
	Data->SetNumberField(TEXT("max_age"), FoliageType->MaxAge);
	Data->SetNumberField(TEXT("overlap_priority"), FoliageType->OverlapPriority);
	Data->SetObjectField(TEXT("procedural_scale"), BuildIntervalJson(FoliageType->ProceduralScale));

	Data->SetBoolField(TEXT("enable_density_scaling"), FoliageType->bEnableDensityScaling != 0);
	Data->SetBoolField(TEXT("enable_discard_on_load"), FoliageType->bEnableDiscardOnLoad != 0);
	Data->SetBoolField(TEXT("enable_cull_distance_scaling"), FoliageType->bEnableCullDistanceScaling != 0);
	Data->SetNumberField(TEXT("runtime_virtual_texture_count"), FoliageType->RuntimeVirtualTextures.Num());
	Data->SetNumberField(TEXT("virtual_texture_cull_mips"), FoliageType->VirtualTextureCullMips);
	return Data;
}

TSharedPtr<FJsonObject> BuildFoliageTypeJson(const UFoliageType* FoliageType, const bool bIncludeSettings)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(FoliageType);
	if (!FoliageType)
	{
		return Data;
	}

	Data->SetStringField(TEXT("class_name"), FoliageType->GetClass() ? FoliageType->GetClass()->GetName() : FString());
	Data->SetStringField(TEXT("package_name"), FoliageType->GetOutermost() ? FoliageType->GetOutermost()->GetName() : FString());
	Data->SetBoolField(TEXT("stored_in_map_package"), FoliageType->GetOutermost() && FoliageType->GetOutermost()->ContainsMap());
	Data->SetObjectField(TEXT("source"), BuildObjectReferenceJson(FoliageType->GetSource()));

	const UFoliageType_InstancedStaticMesh* StaticMeshType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
	if (StaticMeshType)
	{
		const UStaticMesh* StaticMesh = StaticMeshType->GetStaticMesh();
		Data->SetObjectField(TEXT("mesh"), BuildObjectReferenceJson(StaticMesh));
		Data->SetObjectField(TEXT("component_class"), BuildObjectReferenceJson(StaticMeshType->GetComponentClass()));
		Data->SetNumberField(TEXT("override_material_count"), StaticMeshType->OverrideMaterials.Num());
		Data->SetArrayField(TEXT("override_materials"), BuildObjectReferenceArrayJson(StaticMeshType->OverrideMaterials, 16));
		Data->SetNumberField(TEXT("nanite_override_material_count"), StaticMeshType->NaniteOverrideMaterials.Num());
		Data->SetArrayField(TEXT("nanite_override_materials"), BuildObjectReferenceArrayJson(StaticMeshType->NaniteOverrideMaterials, 16));
	}
	else
	{
		Data->SetObjectField(TEXT("mesh"), BuildObjectReferenceJson(nullptr));
	}

	if (bIncludeSettings)
	{
		Data->SetObjectField(TEXT("settings"), BuildFoliageSettingsJson(FoliageType));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildFoliageUsageJson(const FFoliageEntry& Entry, const int32 Index, const bool bIncludeSettings)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetObjectField(TEXT("foliage_type"), BuildFoliageTypeJson(Entry.FoliageType, bIncludeSettings));
	if (!Entry.FoliageInfo)
	{
		Data->SetNumberField(TEXT("instance_count"), 0);
		return Data;
	}

	const FFoliageInfo& FoliageInfo = *Entry.FoliageInfo;
	const UHierarchicalInstancedStaticMeshComponent* Component = FoliageInfo.GetComponent();
	Data->SetStringField(TEXT("implementation_type"), ImplementationTypeToString(FoliageInfo.Type));
	Data->SetNumberField(TEXT("instance_count"), GetFoliageInstanceCount(FoliageInfo));
	Data->SetObjectField(TEXT("component"), BuildComponentJson(const_cast<UHierarchicalInstancedStaticMeshComponent*>(Component)));
	if (Component)
	{
		Data->SetObjectField(TEXT("component_bounds"), BuildBoxJson(Component->Bounds.GetBox()));
		Data->SetObjectField(TEXT("component_location"), BuildVectorJson(Component->GetComponentLocation()));
		Data->SetObjectField(TEXT("component_static_mesh"), BuildObjectReferenceJson(Component->GetStaticMesh()));
		Data->SetBoolField(TEXT("component_visible"), Component->IsVisible());
		Data->SetNumberField(TEXT("component_instance_count"), Component->GetInstanceCount());
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildFoliageActorJson(AInstancedFoliageActor* FoliageActor, const TArray<FFoliageEntry>& Entries, const bool bIncludeSettings, const int32 FoliageTypeLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildActorJson(FoliageActor);
	if (!FoliageActor)
	{
		return Data;
	}

	int32 InstanceCount = 0;
	for (const FFoliageEntry& Entry : Entries)
	{
		if (Entry.FoliageInfo)
		{
			InstanceCount += GetFoliageInstanceCount(*Entry.FoliageInfo);
		}
	}

	TArray<TSharedPtr<FJsonValue>> TypesJson;
	for (const FFoliageEntry& Entry : Entries)
	{
		if (TypesJson.Num() >= FoliageTypeLimit)
		{
			break;
		}
		TypesJson.Add(MakeShared<FJsonValueObject>(BuildFoliageUsageJson(Entry, TypesJson.Num(), bIncludeSettings)));
	}

	Data->SetNumberField(TEXT("foliage_type_count"), Entries.Num());
	Data->SetNumberField(TEXT("instance_count"), InstanceCount);
	Data->SetBoolField(TEXT("types_truncated"), TypesJson.Num() < Entries.Num());
	Data->SetArrayField(TEXT("types"), TypesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSampleJson(
	AInstancedFoliageActor* FoliageActor,
	const FFoliageEntry& Entry,
	const int32 InstanceIndex,
	const FTransform& Transform,
	const FVector& Center)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("actor"), BuildActorJson(FoliageActor));
	Data->SetObjectField(TEXT("foliage_type"), BuildFoliageTypeJson(Entry.FoliageType, false));
	Data->SetNumberField(TEXT("instance_index"), InstanceIndex);
	Data->SetObjectField(TEXT("location"), BuildVectorJson(Transform.GetLocation()));
	Data->SetObjectField(TEXT("rotation"), BuildRotatorJson(Transform.Rotator()));
	Data->SetObjectField(TEXT("scale"), BuildVectorJson(Transform.GetScale3D()));
	Data->SetNumberField(TEXT("distance"), FVector::Dist(Transform.GetLocation(), Center));
	return Data;
}

TSharedPtr<FJsonObject> BuildTypeCountJson(const FString& TypeKey, const int32 Count)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("foliage_type_path"), TypeKey);
	Data->SetNumberField(TEXT("count"), Count);
	return Data;
}

FString RunOnGameThread(TFunction<FString()>&& Work, const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, Work = MoveTemp(Work)]()
	{
		Promise->SetValue(Work());
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleFoliageInfo(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadStringField(Params, TEXT("world"), TEXT("editor"));
	const FString TypeFilter = ReadStringField(Params, TEXT("type_filter"));
	const bool bIncludeSettings = ReadBoolField(Params, TEXT("include_settings"), true);
	const int32 FoliageActorLimit = ReadIntField(Params, TEXT("foliage_actor_limit"), 100, 1, 5000);
	const int32 FoliageTypeLimit = ReadIntField(Params, TEXT("foliage_type_limit"), 200, 0, 50000);

	return RunOnGameThread([WorldSelector, TypeFilter, bIncludeSettings, FoliageActorLimit, FoliageTypeLimit]()
	{
		using namespace MCPToolkit::RuntimeDiagnostics;

		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return CreateErrorResponse(TEXT("Foliage info world is not available"));
		}

		TArray<TSharedPtr<FJsonValue>> ActorsJson;
		int32 MatchedActorCount = 0;
		int32 MatchedTypeCount = 0;
		int32 MatchedInstanceCount = 0;
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			AInstancedFoliageActor* FoliageActor = *It;
			TArray<FFoliageEntry> Entries = BuildSortedFoliageEntries(FoliageActor, TypeFilter);
			if (!TypeFilter.IsEmpty() && Entries.Num() == 0)
			{
				continue;
			}

			++MatchedActorCount;
			MatchedTypeCount += Entries.Num();
			for (const FFoliageEntry& Entry : Entries)
			{
				if (Entry.FoliageInfo)
				{
					MatchedInstanceCount += GetFoliageInstanceCount(*Entry.FoliageInfo);
				}
			}

			if (ActorsJson.Num() >= FoliageActorLimit)
			{
				continue;
			}
			ActorsJson.Add(MakeShared<FJsonValueObject>(BuildFoliageActorJson(FoliageActor, Entries, bIncludeSettings, FoliageTypeLimit)));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetStringField(TEXT("type_filter"), TypeFilter);
		Data->SetNumberField(TEXT("foliage_actor_count"), MatchedActorCount);
		Data->SetNumberField(TEXT("foliage_type_count"), MatchedTypeCount);
		Data->SetNumberField(TEXT("instance_count"), MatchedInstanceCount);
		Data->SetBoolField(TEXT("foliage_actors_truncated"), ActorsJson.Num() < MatchedActorCount);
		Data->SetArrayField(TEXT("foliage_actors"), ActorsJson);
		return CreateSuccessResponse(Data);
	}, TEXT("Foliage info timed out"));
}

FString HandleFoliageSampleInstances(TSharedPtr<FJsonObject> Params)
{
	double X = 0.0;
	double Y = 0.0;
	if (!TryReadRequiredDouble(Params, TEXT("x"), X) || !TryReadRequiredDouble(Params, TEXT("y"), Y))
	{
		return CreateErrorResponse(TEXT("Missing required 'x' and 'y' parameters"));
	}

	const double Z = ReadDoubleField(Params, TEXT("z"), 0.0, -HALF_WORLD_MAX, HALF_WORLD_MAX);
	const double Radius = ReadDoubleField(Params, TEXT("radius"), 1000.0, 0.0, HALF_WORLD_MAX);
	const int32 Limit = ReadIntField(Params, TEXT("limit"), 100, 0, 10000);
	const int32 ScanLimit = ReadIntField(Params, TEXT("scan_limit"), 50000, 1, 500000);
	const FString WorldSelector = ReadStringField(Params, TEXT("world"), TEXT("editor"));
	const FString TypeFilter = ReadStringField(Params, TEXT("type_filter"));

	return RunOnGameThread([X, Y, Z, Radius, Limit, ScanLimit, WorldSelector, TypeFilter]()
	{
		using namespace MCPToolkit::RuntimeDiagnostics;

		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return CreateErrorResponse(TEXT("Foliage sample world is not available"));
		}

		const FVector Center(X, Y, Z);
		const double RadiusSquared = Radius * Radius;
		TArray<TSharedPtr<FJsonValue>> SamplesJson;
		TMap<FString, int32> CountsByType;
		int32 InstancesScanned = 0;
		int32 InstancesMatched = 0;
		bool bScanTruncated = false;

		for (TActorIterator<AInstancedFoliageActor> It(World); It && !bScanTruncated; ++It)
		{
			AInstancedFoliageActor* FoliageActor = *It;
			TArray<FFoliageEntry> Entries = BuildSortedFoliageEntries(FoliageActor, TypeFilter);
			for (const FFoliageEntry& Entry : Entries)
			{
				if (bScanTruncated)
				{
					break;
				}

				if (!Entry.FoliageInfo)
				{
					continue;
				}

				const int32 InstanceCount = GetFoliageInstanceCount(*Entry.FoliageInfo);
				const FString TypeKey = Entry.FoliageType ? Entry.FoliageType->GetPathName() : TEXT("");
				for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
				{
					if (InstancesScanned >= ScanLimit)
					{
						bScanTruncated = true;
						break;
					}

					++InstancesScanned;
					FTransform Transform;
					if (!TryGetInstanceWorldTransform(*Entry.FoliageInfo, InstanceIndex, Transform))
					{
						continue;
					}

					if (Radius > 0.0 && FVector::DistSquared(Transform.GetLocation(), Center) > RadiusSquared)
					{
						continue;
					}

					++InstancesMatched;
					CountsByType.FindOrAdd(TypeKey) += 1;
					if (SamplesJson.Num() < Limit)
					{
						SamplesJson.Add(MakeShared<FJsonValueObject>(BuildSampleJson(FoliageActor, Entry, InstanceIndex, Transform, Center)));
					}
				}
			}
		}

		TArray<FString> TypeKeys;
		CountsByType.GetKeys(TypeKeys);
		TypeKeys.Sort();

		TArray<TSharedPtr<FJsonValue>> TypeCountsJson;
		for (const FString& TypeKey : TypeKeys)
		{
			TypeCountsJson.Add(MakeShared<FJsonValueObject>(BuildTypeCountJson(TypeKey, CountsByType.FindRef(TypeKey))));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetObjectField(TEXT("center"), BuildVectorJson(Center));
		Data->SetNumberField(TEXT("radius"), Radius);
		Data->SetNumberField(TEXT("limit"), Limit);
		Data->SetNumberField(TEXT("scan_limit"), ScanLimit);
		Data->SetStringField(TEXT("type_filter"), TypeFilter);
		Data->SetNumberField(TEXT("instances_scanned"), InstancesScanned);
		Data->SetNumberField(TEXT("instances_matched"), InstancesMatched);
		Data->SetNumberField(TEXT("instances_emitted"), SamplesJson.Num());
		Data->SetBoolField(TEXT("scan_truncated"), bScanTruncated);
		Data->SetBoolField(TEXT("samples_truncated"), SamplesJson.Num() < InstancesMatched);
		Data->SetArrayField(TEXT("counts_by_type"), TypeCountsJson);
		Data->SetArrayField(TEXT("instances"), SamplesJson);
		return CreateSuccessResponse(Data);
	}, TEXT("Foliage sample timed out"));
}

FString HandleFoliageTypeSettings(TSharedPtr<FJsonObject> Params)
{
	const FString FoliageTypePath = ReadStringField(Params, TEXT("foliage_type_path"));
	if (FoliageTypePath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing required 'foliage_type_path' parameter"));
	}

	return RunOnGameThread([FoliageTypePath]()
	{
		UFoliageType* FoliageType = LoadObject<UFoliageType>(nullptr, *FoliageTypePath);
		if (!FoliageType)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Foliage type not found: %s"), *FoliageTypePath));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("foliage_type_path"), FoliageTypePath);
		Data->SetObjectField(TEXT("foliage_type"), BuildFoliageTypeJson(FoliageType, true));
		return CreateSuccessResponse(Data);
	}, TEXT("Foliage type settings timed out"));
}
}
