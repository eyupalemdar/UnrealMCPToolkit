// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTLandscapeCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplinesComponent.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace MCPToolkit::CommandHandlers::Landscape
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

bool MatchesLandscapeFilter(const ALandscapeProxy* Landscape, const FString& NameFilter)
{
	if (!Landscape)
	{
		return false;
	}
	if (NameFilter.IsEmpty())
	{
		return true;
	}

	const FString Needle = NameFilter.ToLower();
	return Landscape->GetName().ToLower().Contains(Needle)
		|| Landscape->GetActorLabel().ToLower().Contains(Needle)
		|| Landscape->GetPathName().ToLower().Contains(Needle);
}

TSharedPtr<FJsonObject> BuildIntPointJson(const FIntPoint& Point)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("x"), Point.X);
	Data->SetNumberField(TEXT("y"), Point.Y);
	return Data;
}

TSharedPtr<FJsonObject> BuildColorJson(const FLinearColor& Color)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("r"), Color.R);
	Data->SetNumberField(TEXT("g"), Color.G);
	Data->SetNumberField(TEXT("b"), Color.B);
	Data->SetNumberField(TEXT("a"), Color.A);
	return Data;
}

TSharedPtr<FJsonObject> BuildLayerInfoJson(const FName& LayerName, const ULandscapeLayerInfoObject* LayerInfo, const int32 LayerIndex)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), LayerIndex);
	Data->SetStringField(TEXT("name"), LayerName.ToString());
	Data->SetObjectField(TEXT("layer_info"), BuildObjectReferenceJson(LayerInfo));
	if (!LayerInfo)
	{
		return Data;
	}

	Data->SetStringField(TEXT("layer_info_name"), LayerInfo->GetLayerName().ToString());
	Data->SetStringField(TEXT("blend_group"), LayerInfo->GetBlendGroup().ToString());
	Data->SetNumberField(TEXT("hardness"), LayerInfo->GetHardness());
	Data->SetObjectField(TEXT("debug_color"), BuildColorJson(LayerInfo->GetLayerUsageDebugColor()));
	Data->SetObjectField(TEXT("physical_material"), BuildObjectReferenceJson(LayerInfo->GetPhysicalMaterial()));
	if (const UEnum* BlendMethodEnum = StaticEnum<ELandscapeTargetLayerBlendMethod>())
	{
		Data->SetStringField(TEXT("blend_method"), BlendMethodEnum->GetNameStringByValue(static_cast<int64>(LayerInfo->GetBlendMethod())));
	}
#if WITH_EDITORONLY_DATA
	Data->SetNumberField(TEXT("minimum_collision_relevance_weight"), LayerInfo->GetMinimumCollisionRelevanceWeight());
#endif
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildTargetLayerArray(ALandscapeProxy* Landscape, const int32 LayerLimit, bool& bOutTruncated, int32& OutLayerCount)
{
	TArray<TSharedPtr<FJsonValue>> LayersJson;
	bOutTruncated = false;
	OutLayerCount = 0;

#if WITH_EDITOR
	if (!Landscape)
	{
		return LayersJson;
	}

	const TMap<FName, FLandscapeTargetLayerSettings>& TargetLayers = Landscape->GetTargetLayers();
	OutLayerCount = TargetLayers.Num();

	TArray<FName> LayerNames;
	TargetLayers.GetKeys(LayerNames);
	LayerNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.LexicalLess(Right);
	});

	for (const FName& LayerName : LayerNames)
	{
		if (LayersJson.Num() >= LayerLimit)
		{
			bOutTruncated = true;
			break;
		}

		const FLandscapeTargetLayerSettings* Settings = TargetLayers.Find(LayerName);
		LayersJson.Add(MakeShared<FJsonValueObject>(BuildLayerInfoJson(LayerName, Settings ? Settings->LayerInfoObj.Get() : nullptr, LayersJson.Num())));
	}
#endif

	return LayersJson;
}

TSharedPtr<FJsonObject> BuildWeightmapAllocationJson(const FWeightmapLayerAllocationInfo& Allocation, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetBoolField(TEXT("allocated"), Allocation.IsAllocated());
	Data->SetStringField(TEXT("layer_name"), Allocation.LayerInfo ? Allocation.LayerInfo->GetLayerName().ToString() : FString());
	Data->SetNumberField(TEXT("weightmap_texture_index"), Allocation.WeightmapTextureIndex);
	Data->SetNumberField(TEXT("weightmap_texture_channel"), Allocation.WeightmapTextureChannel);
	Data->SetObjectField(TEXT("layer_info"), BuildObjectReferenceJson(Allocation.LayerInfo));
	return Data;
}

TSharedPtr<FJsonObject> BuildLandscapeComponentJson(ULandscapeComponent* Component, const int32 ComponentIndex, const int32 AllocationLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildComponentJson(Component);
	if (!Component)
	{
		return Data;
	}

	Data->SetNumberField(TEXT("component_index"), ComponentIndex);
	Data->SetObjectField(TEXT("location"), BuildVectorJson(Component->GetComponentLocation()));
	Data->SetObjectField(TEXT("bounds"), BuildBoxJson(Component->Bounds.GetBox()));
	Data->SetObjectField(TEXT("section_base"), BuildIntPointJson(Component->GetSectionBase()));
	Data->SetObjectField(TEXT("component_key"), BuildIntPointJson(Component->GetComponentKey()));
	Data->SetNumberField(TEXT("component_size_quads"), Component->ComponentSizeQuads);
	Data->SetNumberField(TEXT("subsection_size_quads"), Component->SubsectionSizeQuads);
	Data->SetNumberField(TEXT("num_subsections"), Component->NumSubsections);
	Data->SetBoolField(TEXT("visible"), Component->IsVisible());
	Data->SetObjectField(TEXT("heightmap"), BuildObjectReferenceJson(Component->GetHeightmap()));
	Data->SetObjectField(TEXT("landscape_material"), BuildObjectReferenceJson(Component->GetLandscapeMaterial()));
	Data->SetObjectField(TEXT("override_material"), BuildObjectReferenceJson(Component->OverrideMaterial));
	Data->SetNumberField(TEXT("material_instance_count"), Component->MaterialInstances.Num());
	Data->SetNumberField(TEXT("dynamic_material_instance_count"), Component->MaterialInstancesDynamic.Num());

	const TArray<FWeightmapLayerAllocationInfo>& Allocations = Component->GetWeightmapLayerAllocations();
	TArray<TSharedPtr<FJsonValue>> AllocationJson;
	for (int32 AllocationIndex = 0; AllocationIndex < Allocations.Num() && AllocationJson.Num() < AllocationLimit; ++AllocationIndex)
	{
		AllocationJson.Add(MakeShared<FJsonValueObject>(BuildWeightmapAllocationJson(Allocations[AllocationIndex], AllocationIndex)));
	}
	Data->SetNumberField(TEXT("weightmap_allocation_count"), Allocations.Num());
	Data->SetBoolField(TEXT("weightmap_allocations_truncated"), AllocationJson.Num() < Allocations.Num());
	Data->SetArrayField(TEXT("weightmap_allocations"), AllocationJson);
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildComponentArray(ALandscapeProxy* Landscape, const int32 ComponentLimit, const int32 AllocationLimit, bool& bOutTruncated, int32& OutComponentCount)
{
	TArray<TSharedPtr<FJsonValue>> ComponentsJson;
	bOutTruncated = false;
	OutComponentCount = 0;
	if (!Landscape)
	{
		return ComponentsJson;
	}

	TArray<ULandscapeComponent*> Components;
	Landscape->GetComponents<ULandscapeComponent>(Components);
	Components.Sort([](const ULandscapeComponent& Left, const ULandscapeComponent& Right)
	{
		if (Left.SectionBaseX != Right.SectionBaseX)
		{
			return Left.SectionBaseX < Right.SectionBaseX;
		}
		if (Left.SectionBaseY != Right.SectionBaseY)
		{
			return Left.SectionBaseY < Right.SectionBaseY;
		}
		return Left.GetName() < Right.GetName();
	});

	OutComponentCount = Components.Num();
	for (ULandscapeComponent* Component : Components)
	{
		if (ComponentsJson.Num() >= ComponentLimit)
		{
			bOutTruncated = true;
			break;
		}
		ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildLandscapeComponentJson(Component, ComponentsJson.Num(), AllocationLimit)));
	}
	return ComponentsJson;
}

TSharedPtr<FJsonObject> BuildControlPointJson(ALandscapeProxy* Landscape, const ULandscapeSplineControlPoint* ControlPoint, const int32 ControlPointIndex)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(ControlPoint);
	Data->SetNumberField(TEXT("index"), ControlPointIndex);
	if (!Landscape || !ControlPoint)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("landscape_space_location"), BuildVectorJson(ControlPoint->Location));
	Data->SetObjectField(TEXT("world_location"), BuildVectorJson(Landscape->LandscapeActorToWorld().TransformPosition(ControlPoint->Location)));
	Data->SetObjectField(TEXT("rotation"), BuildRotatorJson(ControlPoint->Rotation));
	Data->SetNumberField(TEXT("width"), ControlPoint->Width);
	Data->SetNumberField(TEXT("layer_width_ratio"), ControlPoint->LayerWidthRatio);
	Data->SetNumberField(TEXT("side_falloff"), ControlPoint->SideFalloff);
	Data->SetNumberField(TEXT("end_falloff"), ControlPoint->EndFalloff);
	Data->SetNumberField(TEXT("connected_segment_count"), ControlPoint->ConnectedSegments.Num());
	Data->SetObjectField(TEXT("bounds"), BuildBoxJson(ControlPoint->GetBounds()));
#if WITH_EDITORONLY_DATA
	Data->SetStringField(TEXT("layer_name"), ControlPoint->LayerName.ToString());
	Data->SetBoolField(TEXT("raise_terrain"), ControlPoint->bRaiseTerrain != 0);
	Data->SetBoolField(TEXT("lower_terrain"), ControlPoint->bLowerTerrain != 0);
	Data->SetObjectField(TEXT("mesh"), BuildObjectReferenceJson(ControlPoint->Mesh));
	Data->SetObjectField(TEXT("mesh_scale"), BuildVectorJson(ControlPoint->MeshScale));
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildSegmentConnectionJson(
	const FLandscapeSplineSegmentConnection& Connection,
	const TMap<const ULandscapeSplineControlPoint*, int32>& ControlPointIndexByPtr,
	const int32 EndIndex)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("end_index"), EndIndex);
	Data->SetNumberField(TEXT("tangent_length"), Connection.TangentLen);
	Data->SetStringField(TEXT("socket_name"), Connection.SocketName.ToString());
	Data->SetObjectField(TEXT("control_point"), BuildObjectReferenceJson(Connection.ControlPoint));
	const int32* ControlPointIndex = ControlPointIndexByPtr.Find(Connection.ControlPoint.Get());
	Data->SetNumberField(TEXT("control_point_index"), ControlPointIndex ? *ControlPointIndex : INDEX_NONE);
	return Data;
}

TSharedPtr<FJsonObject> BuildSegmentJson(
	const ULandscapeSplineSegment* Segment,
	const TMap<const ULandscapeSplineControlPoint*, int32>& ControlPointIndexByPtr,
	const int32 SegmentIndex)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Segment);
	Data->SetNumberField(TEXT("index"), SegmentIndex);
	if (!Segment)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("bounds"), BuildBoxJson(Segment->GetBounds()));
	Data->SetNumberField(TEXT("interp_point_count"), Segment->GetPoints().Num());

	TArray<TSharedPtr<FJsonValue>> ConnectionsJson;
	for (int32 EndIndex = 0; EndIndex < 2; ++EndIndex)
	{
		ConnectionsJson.Add(MakeShared<FJsonValueObject>(BuildSegmentConnectionJson(Segment->Connections[EndIndex], ControlPointIndexByPtr, EndIndex)));
	}
	Data->SetArrayField(TEXT("connections"), ConnectionsJson);
#if WITH_EDITORONLY_DATA
	Data->SetStringField(TEXT("layer_name"), Segment->LayerName.ToString());
	Data->SetBoolField(TEXT("raise_terrain"), Segment->bRaiseTerrain != 0);
	Data->SetBoolField(TEXT("lower_terrain"), Segment->bLowerTerrain != 0);
	Data->SetNumberField(TEXT("spline_mesh_entry_count"), Segment->SplineMeshes.Num());
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildSplineSummaryJson(ALandscapeProxy* Landscape, const bool bIncludeSplines, const int32 ControlPointLimit, const int32 SegmentLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	ULandscapeSplinesComponent* SplinesComponent = Landscape ? Landscape->GetSplinesComponent() : nullptr;
	Data->SetObjectField(TEXT("component"), BuildObjectReferenceJson(SplinesComponent));
	Data->SetBoolField(TEXT("present"), SplinesComponent != nullptr);
	if (!SplinesComponent)
	{
		Data->SetNumberField(TEXT("control_point_count"), 0);
		Data->SetNumberField(TEXT("segment_count"), 0);
		return Data;
	}

	const TArray<TObjectPtr<ULandscapeSplineControlPoint>>& ControlPoints = SplinesComponent->GetControlPoints();
	const TArray<TObjectPtr<ULandscapeSplineSegment>>& Segments = SplinesComponent->GetSegments();
	Data->SetNumberField(TEXT("control_point_count"), ControlPoints.Num());
	Data->SetNumberField(TEXT("segment_count"), Segments.Num());
	Data->SetObjectField(TEXT("bounds"), BuildBoxJson(SplinesComponent->Bounds.GetBox()));

	if (!bIncludeSplines)
	{
		return Data;
	}

	TMap<const ULandscapeSplineControlPoint*, int32> ControlPointIndexByPtr;
	for (int32 Index = 0; Index < ControlPoints.Num(); ++Index)
	{
		ControlPointIndexByPtr.Add(ControlPoints[Index].Get(), Index);
	}

	TArray<TSharedPtr<FJsonValue>> ControlPointsJson;
	for (int32 Index = 0; Index < ControlPoints.Num() && ControlPointsJson.Num() < ControlPointLimit; ++Index)
	{
		ControlPointsJson.Add(MakeShared<FJsonValueObject>(BuildControlPointJson(Landscape, ControlPoints[Index].Get(), Index)));
	}
	Data->SetArrayField(TEXT("control_points"), ControlPointsJson);
	Data->SetBoolField(TEXT("control_points_truncated"), ControlPointsJson.Num() < ControlPoints.Num());

	TArray<TSharedPtr<FJsonValue>> SegmentsJson;
	for (int32 Index = 0; Index < Segments.Num() && SegmentsJson.Num() < SegmentLimit; ++Index)
	{
		SegmentsJson.Add(MakeShared<FJsonValueObject>(BuildSegmentJson(Segments[Index].Get(), ControlPointIndexByPtr, Index)));
	}
	Data->SetArrayField(TEXT("segments"), SegmentsJson);
	Data->SetBoolField(TEXT("segments_truncated"), SegmentsJson.Num() < Segments.Num());
	return Data;
}

TSharedPtr<FJsonObject> BuildLandscapeJson(
	ALandscapeProxy* Landscape,
	const bool bIncludeComponents,
	const bool bIncludeLayers,
	const bool bIncludeSplines,
	const int32 ComponentLimit,
	const int32 LayerLimit,
	const int32 SplineControlPointLimit,
	const int32 SplineSegmentLimit,
	const int32 WeightmapAllocationLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildActorJson(Landscape);
	if (!Landscape)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("is_landscape_actor"), Landscape->IsA(ALandscape::StaticClass()));
	Data->SetStringField(TEXT("landscape_guid"), Landscape->GetLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphens));
	Data->SetStringField(TEXT("original_landscape_guid"), Landscape->GetOriginalLandscapeGuid().ToString(EGuidFormats::DigitsWithHyphens));
	Data->SetNumberField(TEXT("component_size_quads"), Landscape->ComponentSizeQuads);
	Data->SetNumberField(TEXT("subsection_size_quads"), Landscape->SubsectionSizeQuads);
	Data->SetNumberField(TEXT("num_subsections"), Landscape->NumSubsections);
	Data->SetBoolField(TEXT("used_for_navigation"), Landscape->bUsedForNavigation != 0);
	Data->SetObjectField(TEXT("landscape_material"), BuildObjectReferenceJson(Landscape->GetLandscapeMaterial()));
	Data->SetObjectField(TEXT("proxy_bounds"), BuildBoxJson(Landscape->GetProxyBounds()));
	Data->SetObjectField(TEXT("actor_components_bounds"), BuildBoxJson(Landscape->GetComponentsBoundingBox(true)));

	bool bComponentsTruncated = false;
	int32 ComponentCount = 0;
	TArray<TSharedPtr<FJsonValue>> ComponentsJson = BuildComponentArray(Landscape, ComponentLimit, WeightmapAllocationLimit, bComponentsTruncated, ComponentCount);
	Data->SetNumberField(TEXT("component_count"), ComponentCount);
	Data->SetBoolField(TEXT("components_truncated"), bComponentsTruncated);
	if (bIncludeComponents)
	{
		Data->SetArrayField(TEXT("components"), ComponentsJson);
	}

	bool bLayersTruncated = false;
	int32 LayerCount = 0;
	TArray<TSharedPtr<FJsonValue>> LayersJson = BuildTargetLayerArray(Landscape, LayerLimit, bLayersTruncated, LayerCount);
	Data->SetNumberField(TEXT("target_layer_count"), LayerCount);
	Data->SetBoolField(TEXT("target_layers_truncated"), bLayersTruncated);
	if (bIncludeLayers)
	{
		Data->SetArrayField(TEXT("target_layers"), LayersJson);
	}

	Data->SetObjectField(TEXT("splines"), BuildSplineSummaryJson(Landscape, bIncludeSplines, SplineControlPointLimit, SplineSegmentLimit));
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

FString HandleLandscapeInfo(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadStringField(Params, TEXT("world"), TEXT("editor"));
	const FString NameFilter = ReadStringField(Params, TEXT("name_filter"));
	const bool bIncludeComponents = ReadBoolField(Params, TEXT("include_components"), true);
	const bool bIncludeLayers = ReadBoolField(Params, TEXT("include_layers"), true);
	const bool bIncludeSplines = ReadBoolField(Params, TEXT("include_splines"), true);
	const int32 LandscapeLimit = ReadIntField(Params, TEXT("landscape_limit"), 100, 1, 5000);
	const int32 ComponentLimit = ReadIntField(Params, TEXT("component_limit"), 100, 0, 50000);
	const int32 LayerLimit = ReadIntField(Params, TEXT("layer_limit"), 100, 0, 10000);
	const int32 SplineControlPointLimit = ReadIntField(Params, TEXT("spline_control_point_limit"), 200, 0, 50000);
	const int32 SplineSegmentLimit = ReadIntField(Params, TEXT("spline_segment_limit"), 200, 0, 50000);
	const int32 WeightmapAllocationLimit = ReadIntField(Params, TEXT("weightmap_allocation_limit"), 32, 0, 1024);

	return RunOnGameThread([WorldSelector, NameFilter, bIncludeComponents, bIncludeLayers, bIncludeSplines, LandscapeLimit, ComponentLimit, LayerLimit, SplineControlPointLimit, SplineSegmentLimit, WeightmapAllocationLimit]()
	{
		using namespace MCPToolkit::RuntimeDiagnostics;

		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return CreateErrorResponse(TEXT("Landscape info world is not available"));
		}

		TArray<TSharedPtr<FJsonValue>> LandscapesJson;
		int32 MatchedCount = 0;
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Landscape = *It;
			if (!MatchesLandscapeFilter(Landscape, NameFilter))
			{
				continue;
			}

			++MatchedCount;
			if (LandscapesJson.Num() >= LandscapeLimit)
			{
				continue;
			}
			LandscapesJson.Add(MakeShared<FJsonValueObject>(BuildLandscapeJson(
				Landscape,
				bIncludeComponents,
				bIncludeLayers,
				bIncludeSplines,
				ComponentLimit,
				LayerLimit,
				SplineControlPointLimit,
				SplineSegmentLimit,
				WeightmapAllocationLimit)));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetStringField(TEXT("name_filter"), NameFilter);
		Data->SetNumberField(TEXT("count"), MatchedCount);
		Data->SetBoolField(TEXT("landscapes_truncated"), LandscapesJson.Num() < MatchedCount);
		Data->SetArrayField(TEXT("landscapes"), LandscapesJson);
		return CreateSuccessResponse(Data);
	}, TEXT("Landscape info timed out"));
}

FString HandleLandscapeSampleHeight(TSharedPtr<FJsonObject> Params)
{
	double X = 0.0;
	double Y = 0.0;
	if (!TryReadRequiredDouble(Params, TEXT("x"), X) || !TryReadRequiredDouble(Params, TEXT("y"), Y))
	{
		return CreateErrorResponse(TEXT("Missing required 'x' and 'y' parameters"));
	}

	const double Z = ReadDoubleField(Params, TEXT("z"), 0.0, -HALF_WORLD_MAX, HALF_WORLD_MAX);
	const double TraceExtent = ReadDoubleField(Params, TEXT("trace_extent"), 100000.0, 1.0, HALF_WORLD_MAX);
	const FString WorldSelector = ReadStringField(Params, TEXT("world"), TEXT("editor"));
	const FString NameFilter = ReadStringField(Params, TEXT("name_filter"));

	return RunOnGameThread([X, Y, Z, TraceExtent, WorldSelector, NameFilter]()
	{
		using namespace MCPToolkit::RuntimeDiagnostics;

		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return CreateErrorResponse(TEXT("Landscape sample world is not available"));
		}

		const FVector TraceStart(X, Y, Z + TraceExtent);
		const FVector TraceEnd(X, Y, Z - TraceExtent);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MCPToolkitLandscapeSample), true);
		QueryParams.bTraceComplex = true;

		TArray<FHitResult> Hits;
		World->LineTraceMultiByChannel(Hits, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams);
		for (const FHitResult& Hit : Hits)
		{
			ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(Hit.GetActor());
			if (!MatchesLandscapeFilter(Landscape, NameFilter))
			{
				continue;
			}

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
			Data->SetBoolField(TEXT("hit"), true);
			Data->SetStringField(TEXT("source"), TEXT("line_trace"));
			Data->SetNumberField(TEXT("height"), Hit.Location.Z);
			Data->SetObjectField(TEXT("input_location"), BuildVectorJson(FVector(X, Y, Z)));
			Data->SetObjectField(TEXT("trace_start"), BuildVectorJson(TraceStart));
			Data->SetObjectField(TEXT("trace_end"), BuildVectorJson(TraceEnd));
			Data->SetObjectField(TEXT("hit_location"), BuildVectorJson(Hit.Location));
			Data->SetObjectField(TEXT("normal"), BuildVectorJson(Hit.Normal));
			Data->SetObjectField(TEXT("landscape"), BuildActorJson(Landscape));
			return CreateSuccessResponse(Data);
		}

		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Landscape = *It;
			if (!MatchesLandscapeFilter(Landscape, NameFilter))
			{
				continue;
			}

			const TOptional<float> Height = Landscape->GetHeightAtLocation(FVector(X, Y, Z));
			if (!Height.IsSet())
			{
				continue;
			}

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
			Data->SetBoolField(TEXT("hit"), true);
			Data->SetStringField(TEXT("source"), TEXT("heightfield"));
			Data->SetNumberField(TEXT("height"), Height.GetValue());
			Data->SetObjectField(TEXT("input_location"), BuildVectorJson(FVector(X, Y, Z)));
			Data->SetObjectField(TEXT("hit_location"), BuildVectorJson(FVector(X, Y, Height.GetValue())));
			Data->SetObjectField(TEXT("landscape"), BuildActorJson(Landscape));
			return CreateSuccessResponse(Data);
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetBoolField(TEXT("hit"), false);
		Data->SetStringField(TEXT("source"), TEXT("none"));
		Data->SetObjectField(TEXT("input_location"), BuildVectorJson(FVector(X, Y, Z)));
		Data->SetObjectField(TEXT("trace_start"), BuildVectorJson(TraceStart));
		Data->SetObjectField(TEXT("trace_end"), BuildVectorJson(TraceEnd));
		return CreateSuccessResponse(Data);
	}, TEXT("Landscape sample timed out"));
}
}
