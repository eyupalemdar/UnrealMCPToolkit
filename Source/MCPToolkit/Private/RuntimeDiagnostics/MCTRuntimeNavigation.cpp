// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/MCTRuntimeNavigation.h"

#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "AI/Navigation/NavigationBounds.h"
#include "AI/Navigation/NavigationDataResolution.h"
#include "AI/Navigation/NavigationInvokerPriority.h"
#include "AI/Navigation/NavAgentSelector.h"
#include "Dom/JsonValue.h"
#include "Engine/Level.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "UObject/Package.h"

namespace MCPToolkit::RuntimeDiagnostics
{
namespace
{
FString RuntimeGenerationTypeToString(ERuntimeGenerationType Value)
{
	switch (Value)
	{
	case ERuntimeGenerationType::Static:
		return TEXT("Static");
	case ERuntimeGenerationType::DynamicModifiersOnly:
		return TEXT("DynamicModifiersOnly");
	case ERuntimeGenerationType::Dynamic:
		return TEXT("Dynamic");
	case ERuntimeGenerationType::LegacyGeneration:
		return TEXT("LegacyGeneration");
	default:
		return FString::Printf(TEXT("RuntimeGeneration_%d"), static_cast<int32>(Value));
	}
}

FString NavigationDataResolutionToString(ENavigationDataResolution Value)
{
	switch (Value)
	{
	case ENavigationDataResolution::Low:
		return TEXT("Low");
	case ENavigationDataResolution::Default:
		return TEXT("Default");
	case ENavigationDataResolution::High:
		return TEXT("High");
	case ENavigationDataResolution::Invalid:
		return TEXT("Invalid");
	default:
		return FString::Printf(TEXT("NavigationDataResolution_%d"), static_cast<int32>(Value));
	}
}

FString NavigationInvokerPriorityToString(ENavigationInvokerPriority Value)
{
	switch (Value)
	{
	case ENavigationInvokerPriority::VeryLow:
		return TEXT("VeryLow");
	case ENavigationInvokerPriority::Low:
		return TEXT("Low");
	case ENavigationInvokerPriority::Default:
		return TEXT("Default");
	case ENavigationInvokerPriority::High:
		return TEXT("High");
	case ENavigationInvokerPriority::VeryHigh:
		return TEXT("VeryHigh");
	default:
		return FString::Printf(TEXT("NavigationInvokerPriority_%d"), static_cast<int32>(Value));
	}
}

TSharedPtr<FJsonObject> BuildNavAgentPropertiesJson(const FNavAgentProperties& Agent)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), Agent.IsValid());
	Data->SetNumberField(TEXT("agent_radius"), Agent.AgentRadius);
	Data->SetNumberField(TEXT("agent_height"), Agent.AgentHeight);
	Data->SetNumberField(TEXT("agent_step_height"), Agent.AgentStepHeight);
	Data->SetBoolField(TEXT("has_step_height_override"), Agent.HasStepHeightOverride());
	Data->SetNumberField(TEXT("nav_walking_search_height_scale"), Agent.NavWalkingSearchHeightScale);
	Data->SetStringField(TEXT("preferred_nav_data"), Agent.PreferredNavData.ToString());
	Data->SetObjectField(TEXT("extent"), BuildVectorJson(Agent.GetExtent()));
	return Data;
}

TSharedPtr<FJsonObject> BuildNavDataConfigJson(const FNavDataConfig& Config)
{
	TSharedPtr<FJsonObject> Data = BuildNavAgentPropertiesJson(Config);
	Data->SetStringField(TEXT("name"), Config.Name.ToString());
	Data->SetBoolField(TEXT("config_valid"), Config.IsValid());
	Data->SetObjectField(TEXT("default_query_extent"), BuildVectorJson(Config.DefaultQueryExtent));

	TSharedPtr<FJsonObject> ColorJson = MakeShared<FJsonObject>();
	ColorJson->SetNumberField(TEXT("r"), Config.Color.R);
	ColorJson->SetNumberField(TEXT("g"), Config.Color.G);
	ColorJson->SetNumberField(TEXT("b"), Config.Color.B);
	ColorJson->SetNumberField(TEXT("a"), Config.Color.A);
	Data->SetObjectField(TEXT("color"), ColorJson);

	TSubclassOf<AActor> NavDataClass = Config.GetNavDataClass<AActor>();
	Data->SetStringField(TEXT("nav_data_class"), NavDataClass ? NavDataClass->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("description"), Config.GetDescription());
	return Data;
}

TSharedPtr<FJsonObject> BuildNavAgentSelectorJson(const FNavAgentSelector& Selector, const TArray<FNavDataConfig>& SupportedAgents)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("initialized"), Selector.IsInitialized());
	Data->SetBoolField(TEXT("contains_any_agent"), Selector.ContainsAnyAgent());
	Data->SetNumberField(TEXT("agent_bits"), static_cast<double>(Selector.GetAgentBits()));

	TArray<TSharedPtr<FJsonValue>> IndicesJson;
	TArray<TSharedPtr<FJsonValue>> NamesJson;
	for (int32 AgentIndex = 0; AgentIndex < 16; ++AgentIndex)
	{
		if (!Selector.Contains(AgentIndex))
		{
			continue;
		}
		IndicesJson.Add(MakeShared<FJsonValueNumber>(AgentIndex));
		if (SupportedAgents.IsValidIndex(AgentIndex))
		{
			NamesJson.Add(MakeShared<FJsonValueString>(SupportedAgents[AgentIndex].Name.ToString()));
		}
	}
	Data->SetArrayField(TEXT("agent_indices"), IndicesJson);
	Data->SetArrayField(TEXT("agent_names"), NamesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildNavigationBoundsJson(const FNavigationBounds& Bounds, const TArray<FNavDataConfig>& SupportedAgents)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("unique_id"), static_cast<double>(Bounds.UniqueID));
	Data->SetObjectField(TEXT("area_box"), BuildBoxJson(Bounds.AreaBox));
	Data->SetObjectField(TEXT("supported_agents"), BuildNavAgentSelectorJson(Bounds.SupportedAgents, SupportedAgents));
	if (ULevel* Level = Bounds.Level.Get())
	{
		Data->SetStringField(TEXT("level_name"), Level->GetName());
		Data->SetStringField(TEXT("level_path"), Level->GetPathName());
		Data->SetStringField(TEXT("level_package_name"), Level->GetOutermost() ? Level->GetOutermost()->GetName() : TEXT(""));
	}
	else
	{
		Data->SetStringField(TEXT("level_name"), TEXT(""));
		Data->SetStringField(TEXT("level_path"), TEXT(""));
		Data->SetStringField(TEXT("level_package_name"), TEXT(""));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRecastNavMeshJson(ARecastNavMesh* RecastNavMesh, UNavigationSystemV1* NavSys, int32 TileSampleLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), RecastNavMesh != nullptr);
	if (!RecastNavMesh)
	{
		return Data;
	}

	Data->SetNumberField(TEXT("tile_size_uu"), RecastNavMesh->GetTileSizeUU());
	Data->SetNumberField(TEXT("nav_mesh_tiles_count"), RecastNavMesh->GetNavMeshTilesCount());
	Data->SetNumberField(TEXT("active_tile_count"), RecastNavMesh->GetActiveTileSet().Num());
	Data->SetNumberField(TEXT("tile_number_hard_limit"), RecastNavMesh->GetTileNumberHardLimit());
	Data->SetBoolField(TEXT("fixed_tile_pool_size"), RecastNavMesh->bFixedTilePoolSize != 0);
	Data->SetNumberField(TEXT("tile_pool_size"), RecastNavMesh->TilePoolSize);
	Data->SetBoolField(TEXT("resizable"), RecastNavMesh->IsResizable());
	Data->SetBoolField(TEXT("voxel_cache_enabled"), ARecastNavMesh::IsVoxelCacheEnabled());
	Data->SetBoolField(TEXT("world_partitioned_dynamic_navmesh"), RecastNavMesh->IsWorldPartitionedDynamicNavmesh());
	Data->SetBoolField(TEXT("using_active_tiles_generation"), NavSys ? RecastNavMesh->IsUsingActiveTilesGeneration(*NavSys) : false);
	Data->SetObjectField(TEXT("nav_mesh_bounds"), BuildBoxJson(RecastNavMesh->GetNavMeshBounds()));
	Data->SetNumberField(TEXT("agent_radius"), RecastNavMesh->AgentRadius);
	Data->SetNumberField(TEXT("agent_height"), RecastNavMesh->AgentHeight);
	Data->SetNumberField(TEXT("agent_max_slope"), RecastNavMesh->AgentMaxSlope);
	Data->SetNumberField(TEXT("min_region_area"), RecastNavMesh->MinRegionArea);
	Data->SetNumberField(TEXT("merge_region_size"), RecastNavMesh->MergeRegionSize);
	Data->SetNumberField(TEXT("max_simplification_error"), RecastNavMesh->MaxSimplificationError);

	TArray<TSharedPtr<FJsonValue>> ResolutionsJson;
	const ENavigationDataResolution Resolutions[] = {
		ENavigationDataResolution::Low,
		ENavigationDataResolution::Default,
		ENavigationDataResolution::High,
	};
	for (const ENavigationDataResolution Resolution : Resolutions)
	{
		TSharedPtr<FJsonObject> ResolutionJson = MakeShared<FJsonObject>();
		ResolutionJson->SetStringField(TEXT("resolution"), NavigationDataResolutionToString(Resolution));
		ResolutionJson->SetNumberField(TEXT("cell_size"), RecastNavMesh->GetCellSize(Resolution));
		ResolutionJson->SetNumberField(TEXT("cell_height"), RecastNavMesh->GetCellHeight(Resolution));
		ResolutionJson->SetNumberField(TEXT("agent_max_step_height"), RecastNavMesh->GetAgentMaxStepHeight(Resolution));
		ResolutionsJson.Add(MakeShared<FJsonValueObject>(ResolutionJson));
	}
	Data->SetArrayField(TEXT("resolutions"), ResolutionsJson);

	Data->SetNumberField(TEXT("tile_sample_limit"), TileSampleLimit);
	if (TileSampleLimit > 0)
	{
		TArray<FNavTileRef> TileRefs;
		RecastNavMesh->GetAllNavMeshTiles(TileRefs);
		Data->SetNumberField(TEXT("valid_tile_ref_count"), TileRefs.Num());

		TArray<TSharedPtr<FJsonValue>> TilesJson;
		for (FNavTileRef TileRef : TileRefs)
		{
			if (TilesJson.Num() >= TileSampleLimit)
			{
				break;
			}

			TSharedPtr<FJsonObject> TileJson = MakeShared<FJsonObject>();
			TileJson->SetStringField(TEXT("tile_ref"), FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(TileRef)));
			int32 TileX = 0;
			int32 TileY = 0;
			int32 TileLayer = 0;
			const bool bHasTileXY = RecastNavMesh->GetNavMeshTileXY(TileRef, TileX, TileY, TileLayer);
			TileJson->SetBoolField(TEXT("has_tile_xy"), bHasTileXY);
			if (bHasTileXY)
			{
				TileJson->SetNumberField(TEXT("tile_x"), TileX);
				TileJson->SetNumberField(TEXT("tile_y"), TileY);
				TileJson->SetNumberField(TEXT("tile_layer"), TileLayer);
			}
			ENavigationDataResolution TileResolution = ENavigationDataResolution::Invalid;
			if (RecastNavMesh->GetNavmeshTileResolution(TileRef, TileResolution))
			{
				TileJson->SetStringField(TEXT("resolution"), NavigationDataResolutionToString(TileResolution));
			}
			TileJson->SetObjectField(TEXT("bounds"), BuildBoxJson(RecastNavMesh->GetNavMeshTileBounds(TileRef)));
			TilesJson.Add(MakeShared<FJsonValueObject>(TileJson));
		}
		Data->SetNumberField(TEXT("returned_tile_count"), TilesJson.Num());
		Data->SetBoolField(TEXT("tiles_truncated"), TileRefs.Num() > TilesJson.Num());
		Data->SetArrayField(TEXT("tile_samples"), TilesJson);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildNavigationDataJson(ANavigationData* NavData, UNavigationSystemV1* NavSys, int32 TileSampleLimit)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(NavData);
	if (!NavData)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("registered"), NavData->IsRegistered());
	Data->SetNumberField(TEXT("nav_data_unique_id"), NavData->GetNavDataUniqueID());
	Data->SetBoolField(TEXT("supporting_default_agent"), NavData->IsSupportingDefaultAgent());
	Data->SetBoolField(TEXT("can_be_main_nav_data"), NavData->CanBeMainNavData());
	Data->SetBoolField(TEXT("can_spawn_on_rebuild"), NavData->CanSpawnOnRebuild());
	Data->SetBoolField(TEXT("needs_rebuild_on_load"), NavData->NeedsRebuildOnLoad());
	Data->SetBoolField(TEXT("needs_rebuild"), NavData->NeedsRebuild());
	Data->SetBoolField(TEXT("supports_runtime_generation"), NavData->SupportsRuntimeGeneration());
	Data->SetBoolField(TEXT("supports_streaming"), NavData->SupportsStreaming());
	Data->SetStringField(TEXT("runtime_generation_mode"), RuntimeGenerationTypeToString(NavData->GetRuntimeGenerationMode()));
	Data->SetObjectField(TEXT("bounds"), BuildBoxJson(NavData->GetBounds()));
	Data->SetObjectField(TEXT("default_query_extent"), BuildVectorJson(NavData->GetDefaultQueryExtent()));
	Data->SetObjectField(TEXT("config"), BuildNavDataConfigJson(NavData->GetConfig()));
	Data->SetObjectField(TEXT("agent_properties"), BuildNavAgentPropertiesJson(NavData->GetNavAgentProperties()));

	TArray<FBox> NavigableBounds = NavData->GetNavigableBounds();
	Data->SetNumberField(TEXT("navigable_bounds_count"), NavigableBounds.Num());
	TArray<TSharedPtr<FJsonValue>> NavigableBoundsJson;
	for (const FBox& Bounds : NavigableBounds)
	{
		if (NavigableBoundsJson.Num() >= 8)
		{
			break;
		}
		NavigableBoundsJson.Add(MakeShared<FJsonValueObject>(BuildBoxJson(Bounds)));
	}
	Data->SetNumberField(TEXT("returned_navigable_bounds_count"), NavigableBoundsJson.Num());
	Data->SetBoolField(TEXT("navigable_bounds_truncated"), NavigableBounds.Num() > NavigableBoundsJson.Num());
	Data->SetArrayField(TEXT("navigable_bounds"), NavigableBoundsJson);

	if (ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData))
	{
		Data->SetObjectField(TEXT("recast"), BuildRecastNavMeshJson(RecastNavMesh, NavSys, TileSampleLimit));
	}
	else
	{
		Data->SetObjectField(TEXT("recast"), BuildRecastNavMeshJson(nullptr, NavSys, TileSampleLimit));
	}

	return Data;
}
}

TSharedPtr<FJsonObject> BuildNavigationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString NavDataFilter;
	bool bIncludeNavData = true;
	bool bIncludeBounds = true;
	bool bIncludeSupportedAgents = true;
	bool bIncludeInvokers = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("nav_data_filter"), NavDataFilter);
		Params->TryGetBoolField(TEXT("include_nav_data"), bIncludeNavData);
		Params->TryGetBoolField(TEXT("include_bounds"), bIncludeBounds);
		Params->TryGetBoolField(TEXT("include_supported_agents"), bIncludeSupportedAgents);
		Params->TryGetBoolField(TEXT("include_invokers"), bIncludeInvokers);
	}
	NavDataFilter.TrimStartAndEndInline();
	const int32 NavDataLimit = ReadClampedIntField(Params, TEXT("nav_data_limit"), 50, 0, 500);
	const int32 BoundsLimit = ReadClampedIntField(Params, TEXT("bounds_limit"), 100, 0, 1000);
	const int32 InvokerLimit = ReadClampedIntField(Params, TEXT("invoker_limit"), 100, 0, 1000);
	const int32 TileSampleLimit = ReadClampedIntField(Params, TEXT("tile_sample_limit"), 0, 0, 100);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("requested_world"), WorldSelector);
	Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
	Data->SetStringField(TEXT("nav_data_filter"), NavDataFilter);
	Data->SetBoolField(TEXT("include_nav_data"), bIncludeNavData);
	Data->SetBoolField(TEXT("include_bounds"), bIncludeBounds);
	Data->SetBoolField(TEXT("include_supported_agents"), bIncludeSupportedAgents);
	Data->SetBoolField(TEXT("include_invokers"), bIncludeInvokers);
	Data->SetNumberField(TEXT("nav_data_limit"), NavDataLimit);
	Data->SetNumberField(TEXT("bounds_limit"), BoundsLimit);
	Data->SetNumberField(TEXT("invoker_limit"), InvokerLimit);
	Data->SetNumberField(TEXT("tile_sample_limit"), TileSampleLimit);

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
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; navigation diagnostics reflect the editor world")));
	}

	Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	Data->SetBoolField(TEXT("navigation_system_available"), NavSys != nullptr);
	if (!NavSys)
	{
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected world has no UNavigationSystemV1 instance")));
		Data->SetObjectField(TEXT("navigation_system"), BuildObjectReferenceJson(nullptr));
		Data->SetNumberField(TEXT("nav_data_count"), 0);
		Data->SetNumberField(TEXT("navigation_bounds_count"), 0);
		Data->SetNumberField(TEXT("supported_agent_count"), 0);
		Data->SetNumberField(TEXT("invoker_count"), 0);
		Data->SetArrayField(TEXT("nav_data"), TArray<TSharedPtr<FJsonValue>>());
		Data->SetArrayField(TEXT("navigation_bounds"), TArray<TSharedPtr<FJsonValue>>());
		Data->SetArrayField(TEXT("supported_agents"), TArray<TSharedPtr<FJsonValue>>());
		Data->SetArrayField(TEXT("invokers"), TArray<TSharedPtr<FJsonValue>>());
		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
	}

	const TArray<FNavDataConfig>& SupportedAgents = NavSys->GetSupportedAgents();
	const TSet<FNavigationBounds>& NavigationBounds = NavSys->GetNavigationBounds();
	const TArray<FNavigationInvokerRaw>& InvokerLocations = NavSys->GetInvokerLocations();

	TSharedPtr<FJsonObject> NavSysJson = BuildObjectReferenceJson(NavSys);
	NavSysJson->SetBoolField(TEXT("navigation_being_built"), UNavigationSystemV1::IsNavigationBeingBuilt(World));
	NavSysJson->SetBoolField(TEXT("navigation_being_built_or_locked"), UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(World));
	NavSysJson->SetBoolField(TEXT("navigation_dirty"), NavSys->IsNavigationDirty());
	NavSysJson->SetBoolField(TEXT("can_rebuild_dirty_navigation"), NavSys->CanRebuildDirtyNavigation());
	NavSysJson->SetBoolField(TEXT("supports_navigation_generation"), NavSys->SupportsNavigationGeneration());
	NavSysJson->SetBoolField(TEXT("supports_dynamic_changes"), FNavigationSystem::SupportsDynamicChanges(World));
	NavSysJson->SetBoolField(TEXT("there_is_anywhere_to_build_navigation"), NavSys->IsThereAnywhereToBuildNavigation());
	NavSysJson->SetBoolField(TEXT("generate_navigation_everywhere"), NavSys->ShouldGenerateNavigationEverywhere());
	NavSysJson->SetBoolField(TEXT("allow_client_side_navigation"), NavSys->ShouldAllowClientSideNavigation());
	NavSysJson->SetBoolField(TEXT("discard_sub_level_nav_data"), NavSys->ShouldDiscardSubLevelNavData());
	NavSysJson->SetBoolField(TEXT("active_tiles_generation_enabled"), NavSys->IsActiveTilesGenerationEnabled());
	NavSysJson->SetObjectField(TEXT("main_nav_data"), BuildObjectReferenceJson(NavSys->GetDefaultNavDataInstance()));
	NavSysJson->SetObjectField(TEXT("abstract_nav_data"), BuildObjectReferenceJson(NavSys->GetAbstractNavData()));
	NavSysJson->SetObjectField(TEXT("build_bounds"), BuildBoxJson(NavSys->BuildBounds));
	NavSysJson->SetObjectField(TEXT("world_bounds"), BuildBoxJson(NavSys->GetWorldBounds()));
	NavSysJson->SetObjectField(TEXT("navigable_world_bounds"), BuildBoxJson(NavSys->GetNavigableWorldBounds()));
	NavSysJson->SetObjectField(TEXT("computed_nav_data_bounds"), BuildBoxJson(NavSys->ComputeNavDataBounds()));
	NavSysJson->SetNumberField(TEXT("nav_data_count"), NavSys->NavDataSet.Num());
	NavSysJson->SetNumberField(TEXT("nav_data_registration_queue_count"), NavSys->NavDataRegistrationQueue.Num());
	NavSysJson->SetNumberField(TEXT("pending_nav_bounds_update_count"), NavSys->PendingNavBoundsUpdates.Num());
	NavSysJson->SetNumberField(TEXT("navigation_bounds_count"), NavigationBounds.Num());
	NavSysJson->SetNumberField(TEXT("supported_agent_count"), SupportedAgents.Num());
	NavSysJson->SetNumberField(TEXT("invoker_count"), InvokerLocations.Num());
	NavSysJson->SetObjectField(TEXT("supported_agents_mask"), BuildNavAgentSelectorJson(NavSys->GetSupportedAgentsMask(), SupportedAgents));
	Data->SetObjectField(TEXT("navigation_system"), NavSysJson);

	if (bIncludeSupportedAgents)
	{
		TArray<TSharedPtr<FJsonValue>> SupportedAgentsJson;
		for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); ++AgentIndex)
		{
			TSharedPtr<FJsonObject> AgentJson = BuildNavDataConfigJson(SupportedAgents[AgentIndex]);
			AgentJson->SetNumberField(TEXT("index"), AgentIndex);
			AgentJson->SetBoolField(TEXT("enabled_in_mask"), NavSys->GetSupportedAgentsMask().Contains(AgentIndex));
			SupportedAgentsJson.Add(MakeShared<FJsonValueObject>(AgentJson));
		}
		Data->SetArrayField(TEXT("supported_agents"), SupportedAgentsJson);
	}
	Data->SetNumberField(TEXT("supported_agent_count"), SupportedAgents.Num());

	if (bIncludeBounds)
	{
		TArray<TSharedPtr<FJsonValue>> BoundsJson;
		for (const FNavigationBounds& Bounds : NavigationBounds)
		{
			if (BoundsJson.Num() >= BoundsLimit)
			{
				break;
			}
			BoundsJson.Add(MakeShared<FJsonValueObject>(BuildNavigationBoundsJson(Bounds, SupportedAgents)));
		}
		Data->SetNumberField(TEXT("returned_navigation_bounds_count"), BoundsJson.Num());
		Data->SetBoolField(TEXT("navigation_bounds_truncated"), NavigationBounds.Num() > BoundsJson.Num());
		Data->SetArrayField(TEXT("navigation_bounds"), BoundsJson);
	}
	Data->SetNumberField(TEXT("navigation_bounds_count"), NavigationBounds.Num());

	if (bIncludeInvokers)
	{
		TArray<TSharedPtr<FJsonValue>> InvokersJson;
		for (const FNavigationInvokerRaw& Invoker : InvokerLocations)
		{
			if (InvokersJson.Num() >= InvokerLimit)
			{
				break;
			}

			TSharedPtr<FJsonObject> InvokerJson = MakeShared<FJsonObject>();
			InvokerJson->SetObjectField(TEXT("location"), BuildVectorJson(Invoker.Location));
			InvokerJson->SetNumberField(TEXT("radius_min"), Invoker.RadiusMin);
			InvokerJson->SetNumberField(TEXT("radius_max"), Invoker.RadiusMax);
			InvokerJson->SetStringField(TEXT("priority"), NavigationInvokerPriorityToString(Invoker.Priority));
			InvokerJson->SetObjectField(TEXT("supported_agents"), BuildNavAgentSelectorJson(Invoker.SupportedAgents, SupportedAgents));
			InvokersJson.Add(MakeShared<FJsonValueObject>(InvokerJson));
		}
		Data->SetNumberField(TEXT("returned_invoker_count"), InvokersJson.Num());
		Data->SetBoolField(TEXT("invokers_truncated"), InvokerLocations.Num() > InvokersJson.Num());
		Data->SetArrayField(TEXT("invokers"), InvokersJson);

		const TArray<FBox>& InvokerSeedBounds = NavSys->GetInvokersSeedBounds();
		Data->SetNumberField(TEXT("invoker_seed_bounds_count"), InvokerSeedBounds.Num());
	}
	Data->SetNumberField(TEXT("invoker_count"), InvokerLocations.Num());

	int32 ValidNavDataCount = 0;
	int32 MatchedNavDataCount = 0;
	TArray<TSharedPtr<FJsonValue>> NavDataJson;
	for (ANavigationData* NavData : NavSys->NavDataSet)
	{
		if (!NavData)
		{
			continue;
		}
		++ValidNavDataCount;

		const FString ClassPath = NavData->GetClass() ? NavData->GetClass()->GetPathName() : TEXT("");
		const FString ConfigName = NavData->GetConfig().Name.ToString();
		if (!NavDataFilter.IsEmpty()
			&& !NavData->GetName().Contains(NavDataFilter)
			&& !NavData->GetPathName().Contains(NavDataFilter)
			&& !ClassPath.Contains(NavDataFilter)
			&& !ConfigName.Contains(NavDataFilter))
		{
			continue;
		}

		++MatchedNavDataCount;
		if (bIncludeNavData && NavDataJson.Num() < NavDataLimit)
		{
			NavDataJson.Add(MakeShared<FJsonValueObject>(BuildNavigationDataJson(NavData, NavSys, TileSampleLimit)));
		}
	}

	Data->SetNumberField(TEXT("nav_data_count"), ValidNavDataCount);
	Data->SetNumberField(TEXT("matched_nav_data_count"), MatchedNavDataCount);
	if (bIncludeNavData)
	{
		Data->SetNumberField(TEXT("returned_nav_data_count"), NavDataJson.Num());
		Data->SetBoolField(TEXT("nav_data_truncated"), MatchedNavDataCount > NavDataJson.Num());
		Data->SetArrayField(TEXT("nav_data"), NavDataJson);
	}
	Data->SetArrayField(TEXT("warnings"), Warnings);
	return Data;
}
}
