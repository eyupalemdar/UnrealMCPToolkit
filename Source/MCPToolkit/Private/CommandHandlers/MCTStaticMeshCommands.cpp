// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTStaticMeshCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "BodySetupEnums.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace MCPToolkit::CommandHandlers::StaticMesh
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

FString StripObjectPathDecorators(FString Value)
{
	Value.TrimStartAndEndInline();
	Value.TrimQuotesInline();

	int32 FirstQuote = INDEX_NONE;
	int32 LastQuote = INDEX_NONE;
	if (Value.FindChar(TEXT('\''), FirstQuote) && Value.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
	{
		Value = Value.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		Value.TrimStartAndEndInline();
		Value.TrimQuotesInline();
	}
	return Value;
}

FString ToObjectPath(const FString& AssetPath)
{
	FString CleanPath = StripObjectPathDecorators(AssetPath);
	if (CleanPath.Contains(TEXT(".")))
	{
		return CleanPath;
	}
	if (FPackageName::IsValidLongPackageName(CleanPath))
	{
		return CleanPath + TEXT(".") + FPackageName::GetLongPackageAssetName(CleanPath);
	}
	return CleanPath;
}

FName ToPackageName(const FString& AssetPath)
{
	FString PackageName = StripObjectPathDecorators(AssetPath);
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName = PackageName.Left(DotIndex);
	}
	return FName(*PackageName);
}

TSharedPtr<FJsonObject> BuildAssetDataJson(const FAssetData& AssetData)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), AssetData.IsValid());
	if (!AssetData.IsValid())
	{
		return Data;
	}

	Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
	Data->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
	Data->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
	Data->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
	return Data;
}

FAssetData FindAssetData(const FString& AssetPath)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	const FString ObjectPath = ToObjectPath(AssetPath);
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (AssetData.IsValid())
	{
		return AssetData;
	}

	TArray<FAssetData> PackageAssets;
	AssetRegistry.GetAssetsByPackageName(ToPackageName(AssetPath), PackageAssets);
	for (const FAssetData& PackageAsset : PackageAssets)
	{
		if (PackageAsset.AssetClassPath == UStaticMesh::StaticClass()->GetClassPathName())
		{
			return PackageAsset;
		}
	}
	return FAssetData();
}

UStaticMesh* LoadStaticMesh(const FString& AssetPath, FAssetData& OutAssetData, FString& OutResolvedObjectPath)
{
	OutAssetData = FindAssetData(AssetPath);
	if (OutAssetData.IsValid())
	{
		OutResolvedObjectPath = OutAssetData.GetSoftObjectPath().ToString();
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(OutAssetData.GetAsset()))
		{
			return Mesh;
		}
	}

	OutResolvedObjectPath = ToObjectPath(AssetPath);
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *OutResolvedObjectPath))
	{
		return Mesh;
	}

	const FString CleanPath = StripObjectPathDecorators(AssetPath);
	if (CleanPath != OutResolvedObjectPath)
	{
		OutResolvedObjectPath = CleanPath;
		return LoadObject<UStaticMesh>(nullptr, *CleanPath);
	}
	return nullptr;
}

TSharedPtr<FJsonObject> BuildBoundsJson(const UStaticMesh* Mesh)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Mesh)
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	Data->SetBoolField(TEXT("present"), true);
	Data->SetObjectField(TEXT("origin"), BuildVectorJson(Bounds.Origin));
	Data->SetObjectField(TEXT("box_extent"), BuildVectorJson(Bounds.BoxExtent));
	Data->SetNumberField(TEXT("sphere_radius"), Bounds.SphereRadius);
	Data->SetObjectField(TEXT("box"), BuildBoxJson(Mesh->GetBoundingBox()));
	Data->SetObjectField(TEXT("positive_bounds_extension"), BuildVectorJson(Mesh->GetPositiveBoundsExtension()));
	Data->SetObjectField(TEXT("negative_bounds_extension"), BuildVectorJson(Mesh->GetNegativeBoundsExtension()));
	return Data;
}

TSharedPtr<FJsonObject> BuildResourceSizeJson(UStaticMesh* Mesh)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Mesh)
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	FResourceSizeEx ExclusiveSize(EResourceSizeMode::Exclusive);
	Mesh->GetResourceSizeEx(ExclusiveSize);
	FResourceSizeEx EstimatedTotalSize(EResourceSizeMode::EstimatedTotal);
	Mesh->GetResourceSizeEx(EstimatedTotalSize);

	Data->SetBoolField(TEXT("present"), true);
	Data->SetNumberField(TEXT("exclusive_bytes"), static_cast<double>(ExclusiveSize.GetTotalMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_total_bytes"), static_cast<double>(EstimatedTotalSize.GetTotalMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_system_bytes"), static_cast<double>(EstimatedTotalSize.GetDedicatedSystemMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_video_bytes"), static_cast<double>(EstimatedTotalSize.GetDedicatedVideoMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_unknown_bytes"), static_cast<double>(EstimatedTotalSize.GetUnknownMemoryBytes()));
	return Data;
}

TSharedPtr<FJsonObject> BuildNaniteJson(const UStaticMesh* Mesh)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Mesh)
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	const FMeshNaniteSettings& Settings = Mesh->GetNaniteSettings();
	Data->SetBoolField(TEXT("present"), true);
	Data->SetBoolField(TEXT("enabled"), Mesh->IsNaniteEnabled());
	Data->SetBoolField(TEXT("force_enabled"), Mesh->IsNaniteForceEnabled());
	Data->SetBoolField(TEXT("assembly"), Mesh->IsNaniteAssembly());
	Data->SetBoolField(TEXT("landscape_mesh"), Mesh->IsNaniteLandscape());
	Data->SetBoolField(TEXT("valid_data"), Mesh->HasValidNaniteData());
	Data->SetBoolField(TEXT("setting_enabled"), Settings.bEnabled != 0);
	Data->SetBoolField(TEXT("explicit_tangents"), Settings.bExplicitTangents != 0);
	Data->SetBoolField(TEXT("lerp_uvs"), Settings.bLerpUVs != 0);
	Data->SetBoolField(TEXT("separable"), Settings.bSeparable != 0);
	Data->SetBoolField(TEXT("voxel_ndf"), Settings.bVoxelNDF != 0);
	Data->SetBoolField(TEXT("voxel_opacity"), Settings.bVoxelOpacity != 0);
	Data->SetStringField(TEXT("shape_preservation"), EnumValueToString(StaticEnum<ENaniteShapePreservation>(), static_cast<int64>(Settings.ShapePreservation)));
	Data->SetStringField(TEXT("generate_fallback"), EnumValueToString(StaticEnum<ENaniteGenerateFallback>(), static_cast<int64>(Settings.GenerateFallback)));
	Data->SetStringField(TEXT("fallback_target"), EnumValueToString(StaticEnum<ENaniteFallbackTarget>(), static_cast<int64>(Settings.FallbackTarget)));
	Data->SetNumberField(TEXT("position_precision"), Settings.PositionPrecision);
	Data->SetNumberField(TEXT("normal_precision"), Settings.NormalPrecision);
	Data->SetNumberField(TEXT("tangent_precision"), Settings.TangentPrecision);
	Data->SetNumberField(TEXT("target_minimum_residency_kb"), Settings.TargetMinimumResidencyInKB);
	Data->SetNumberField(TEXT("keep_percent_triangles"), Settings.KeepPercentTriangles);
	Data->SetNumberField(TEXT("trim_relative_error"), Settings.TrimRelativeError);
	Data->SetNumberField(TEXT("fallback_percent_triangles"), Settings.FallbackPercentTriangles);
	Data->SetNumberField(TEXT("fallback_relative_error"), Settings.FallbackRelativeError);
	Data->SetNumberField(TEXT("max_edge_length_factor"), Settings.MaxEdgeLengthFactor);
	Data->SetNumberField(TEXT("nanite_vertex_count"), Mesh->GetNumNaniteVertices());
	Data->SetNumberField(TEXT("nanite_triangle_count"), Mesh->GetNumNaniteTriangles());
	Data->SetNumberField(TEXT("cached_assembly_reference_count"), Mesh->GetCachedNaniteAssemblyReferences().Num());
	Data->SetNumberField(TEXT("displacement_map_count"), Settings.DisplacementMaps.Num());
	return Data;
}

TSharedPtr<FJsonObject> BuildMaterialJson(const FStaticMaterial& Material, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("slot_name"), Material.MaterialSlotName.ToString());
	Data->SetStringField(TEXT("imported_slot_name"), Material.ImportedMaterialSlotName.ToString());
	Data->SetObjectField(TEXT("material"), BuildObjectReferenceJson(Material.MaterialInterface));
	Data->SetObjectField(TEXT("overlay_material"), BuildObjectReferenceJson(Material.OverlayMaterialInterface));
	Data->SetBoolField(TEXT("uv_channel_data_initialized"), Material.UVChannelData.bInitialized != 0);
	return Data;
}

TSharedPtr<FJsonObject> BuildMaterialsJson(const UStaticMesh* Mesh, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> MaterialsJson;
	const int32 MaterialCount = Mesh ? Mesh->GetStaticMaterials().Num() : 0;

	for (int32 Index = 0; Mesh && Index < Mesh->GetStaticMaterials().Num() && MaterialsJson.Num() < Limit; ++Index)
	{
		MaterialsJson.Add(MakeShared<FJsonValueObject>(BuildMaterialJson(Mesh->GetStaticMaterials()[Index], Index)));
	}

	Data->SetNumberField(TEXT("count"), MaterialCount);
	Data->SetBoolField(TEXT("truncated"), MaterialsJson.Num() < MaterialCount);
	Data->SetArrayField(TEXT("slots"), MaterialsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSocketJson(const UStaticMeshSocket* Socket, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Socket);
	Data->SetNumberField(TEXT("index"), Index);
	if (!Socket)
	{
		return Data;
	}

	Data->SetStringField(TEXT("socket_name"), Socket->SocketName.ToString());
	Data->SetStringField(TEXT("tag"), Socket->Tag);
	Data->SetObjectField(TEXT("relative_location"), BuildVectorJson(Socket->RelativeLocation));
	Data->SetObjectField(TEXT("relative_rotation"), BuildRotatorJson(Socket->RelativeRotation));
	Data->SetObjectField(TEXT("relative_scale"), BuildVectorJson(Socket->RelativeScale));
#if WITH_EDITORONLY_DATA
	Data->SetBoolField(TEXT("created_at_import"), Socket->bSocketCreatedAtImport);
	Data->SetObjectField(TEXT("preview_static_mesh"), BuildObjectReferenceJson(Socket->PreviewStaticMesh));
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildSocketsJson(const UStaticMesh* Mesh, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SocketsJson;
	const int32 SocketCount = Mesh ? Mesh->Sockets.Num() : 0;

	for (int32 Index = 0; Mesh && Index < Mesh->Sockets.Num() && SocketsJson.Num() < Limit; ++Index)
	{
		SocketsJson.Add(MakeShared<FJsonValueObject>(BuildSocketJson(Mesh->Sockets[Index], Index)));
	}

	Data->SetNumberField(TEXT("count"), SocketCount);
	Data->SetBoolField(TEXT("truncated"), SocketsJson.Num() < SocketCount);
	Data->SetArrayField(TEXT("sockets"), SocketsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildCollisionJson(const UStaticMesh* Mesh)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	UBodySetup* BodySetup = Mesh ? Mesh->GetBodySetup() : nullptr;
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(BodySetup);
	if (!BodySetup)
	{
		return Data;
	}

	const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
	Data->SetStringField(TEXT("collision_trace_flag"), LexToString(BodySetup->CollisionTraceFlag));
	Data->SetBoolField(TEXT("double_sided_geometry"), BodySetup->bDoubleSidedGeometry != 0);
	Data->SetBoolField(TEXT("generate_mirrored_collision"), BodySetup->bGenerateMirroredCollision != 0);
	Data->SetNumberField(TEXT("simple_element_count"), AggGeom.GetElementCount());
	Data->SetNumberField(TEXT("sphere_count"), AggGeom.SphereElems.Num());
	Data->SetNumberField(TEXT("box_count"), AggGeom.BoxElems.Num());
	Data->SetNumberField(TEXT("sphyl_count"), AggGeom.SphylElems.Num());
	Data->SetNumberField(TEXT("convex_count"), AggGeom.ConvexElems.Num());
	Data->SetNumberField(TEXT("tapered_capsule_count"), AggGeom.TaperedCapsuleElems.Num());
	Data->SetNumberField(TEXT("level_set_count"), AggGeom.LevelSetElems.Num());
	Data->SetNumberField(TEXT("skinned_level_set_count"), AggGeom.SkinnedLevelSetElems.Num());
	Data->SetNumberField(TEXT("ml_level_set_count"), AggGeom.MLLevelSetElems.Num());
	return Data;
}

TSharedPtr<FJsonObject> BuildSectionJson(const UStaticMesh* Mesh, const FStaticMeshSection& Section, const int32 LODIndex, const int32 SectionIndex)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const FMeshSectionInfo SectionInfo = Mesh ? Mesh->GetSectionInfoMap().Get(LODIndex, SectionIndex) : FMeshSectionInfo();

	Data->SetNumberField(TEXT("index"), SectionIndex);
	Data->SetNumberField(TEXT("material_index"), Section.MaterialIndex);
	Data->SetNumberField(TEXT("section_info_material_index"), SectionInfo.MaterialIndex);
	Data->SetNumberField(TEXT("first_index"), Section.FirstIndex);
	Data->SetNumberField(TEXT("triangle_count"), Section.NumTriangles);
	Data->SetNumberField(TEXT("min_vertex_index"), Section.MinVertexIndex);
	Data->SetNumberField(TEXT("max_vertex_index"), Section.MaxVertexIndex);
	Data->SetBoolField(TEXT("collision_enabled"), Section.bEnableCollision);
	Data->SetBoolField(TEXT("section_info_collision_enabled"), SectionInfo.bEnableCollision);
	Data->SetBoolField(TEXT("cast_shadow"), Section.bCastShadow);
	Data->SetBoolField(TEXT("section_info_cast_shadow"), SectionInfo.bCastShadow);
	Data->SetBoolField(TEXT("visible_in_ray_tracing"), Section.bVisibleInRayTracing);
	Data->SetBoolField(TEXT("section_info_visible_in_ray_tracing"), SectionInfo.bVisibleInRayTracing);
	Data->SetBoolField(TEXT("affect_distance_field_lighting"), Section.bAffectDistanceFieldLighting);
	Data->SetBoolField(TEXT("section_info_affect_distance_field_lighting"), SectionInfo.bAffectDistanceFieldLighting);
	Data->SetBoolField(TEXT("force_opaque"), Section.bForceOpaque);
	Data->SetBoolField(TEXT("section_info_force_opaque"), SectionInfo.bForceOpaque);
	return Data;
}

TSharedPtr<FJsonObject> BuildLODJson(const UStaticMesh* Mesh, const FStaticMeshRenderData* RenderData, const int32 LODIndex, const int32 SectionLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), LODIndex);
	if (!Mesh || !RenderData || !RenderData->LODResources.IsValidIndex(LODIndex))
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	const FStaticMeshLODResources& LODResource = RenderData->LODResources[LODIndex];
	TArray<TSharedPtr<FJsonValue>> SectionsJson;
	for (int32 SectionIndex = 0; SectionIndex < LODResource.Sections.Num() && SectionsJson.Num() < SectionLimit; ++SectionIndex)
	{
		SectionsJson.Add(MakeShared<FJsonValueObject>(BuildSectionJson(Mesh, LODResource.Sections[SectionIndex], LODIndex, SectionIndex)));
	}

	Data->SetBoolField(TEXT("present"), true);
	Data->SetNumberField(TEXT("vertex_count"), LODResource.GetNumVertices());
	Data->SetNumberField(TEXT("triangle_count"), LODResource.GetNumTriangles());
	Data->SetNumberField(TEXT("tex_coord_count"), LODResource.GetNumTexCoords());
	Data->SetNumberField(TEXT("section_count"), LODResource.Sections.Num());
	Data->SetBoolField(TEXT("sections_truncated"), SectionsJson.Num() < LODResource.Sections.Num());
	Data->SetArrayField(TEXT("sections"), SectionsJson);
	Data->SetNumberField(TEXT("position_vertex_count"), LODResource.VertexBuffers.PositionVertexBuffer.GetNumVertices());
	Data->SetNumberField(TEXT("static_mesh_vertex_count"), LODResource.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
	Data->SetNumberField(TEXT("color_vertex_count"), LODResource.VertexBuffers.ColorVertexBuffer.GetNumVertices());
	Data->SetBoolField(TEXT("has_color_vertex_data"), LODResource.VertexBuffers.ColorVertexBuffer.GetNumVertices() > 0);
	Data->SetBoolField(TEXT("position_cpu_access"), LODResource.VertexBuffers.PositionVertexBuffer.GetAllowCPUAccess());
	Data->SetBoolField(TEXT("static_mesh_cpu_access"), LODResource.VertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess());
	Data->SetBoolField(TEXT("color_cpu_access"), LODResource.VertexBuffers.ColorVertexBuffer.GetAllowCPUAccess());
	Data->SetBoolField(TEXT("high_precision_tangent_basis"), LODResource.VertexBuffers.StaticMeshVertexBuffer.GetUseHighPrecisionTangentBasis());
	if (LODIndex < MAX_STATIC_MESH_LODS)
	{
		Data->SetNumberField(TEXT("screen_size"), RenderData->ScreenSize[LODIndex].GetValue());
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildLODsJson(const UStaticMesh* Mesh, const int32 LODLimit, const int32 SectionLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const FStaticMeshRenderData* RenderData = Mesh ? Mesh->GetRenderData() : nullptr;
	const int32 LODCount = Mesh ? Mesh->GetNumLODs() : 0;
	TArray<TSharedPtr<FJsonValue>> LODsJson;

	for (int32 LODIndex = 0; LODIndex < LODCount && LODsJson.Num() < LODLimit; ++LODIndex)
	{
		LODsJson.Add(MakeShared<FJsonValueObject>(BuildLODJson(Mesh, RenderData, LODIndex, SectionLimit)));
	}

	Data->SetBoolField(TEXT("render_data_present"), RenderData != nullptr);
	Data->SetBoolField(TEXT("valid_render_data"), Mesh ? Mesh->HasValidRenderData(true) : false);
	Data->SetNumberField(TEXT("count"), LODCount);
	Data->SetBoolField(TEXT("truncated"), LODsJson.Num() < LODCount);
	Data->SetArrayField(TEXT("lods"), LODsJson);
	return Data;
}

int32 CountSectionsWithCollision(const UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return 0;
	}

	int32 CollisionSectionCount = 0;
	const int32 LODCount = Mesh->GetNumLODs();
	for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		const int32 SectionCount = Mesh->GetNumSections(LODIndex);
		for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
		{
			if (Mesh->GetSectionInfoMap().Get(LODIndex, SectionIndex).bEnableCollision)
			{
				++CollisionSectionCount;
			}
		}
	}
	return CollisionSectionCount;
}
}

FString HandleStaticMeshInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	const bool bIncludeLODs = ReadBoolField(Params, TEXT("include_lods"), true);
	const bool bIncludeSections = ReadBoolField(Params, TEXT("include_sections"), true);
	const bool bIncludeMaterials = ReadBoolField(Params, TEXT("include_materials"), true);
	const bool bIncludeSockets = ReadBoolField(Params, TEXT("include_sockets"), true);
	const bool bIncludeCollision = ReadBoolField(Params, TEXT("include_collision"), true);
	const bool bIncludeNanite = ReadBoolField(Params, TEXT("include_nanite"), true);
	const int32 LODLimit = ReadIntField(Params, TEXT("lod_limit"), 8, 0, MAX_STATIC_MESH_LODS);
	const int32 SectionLimit = bIncludeSections ? ReadIntField(Params, TEXT("section_limit"), 64, 0, 2048) : 0;
	const int32 MaterialLimit = ReadIntField(Params, TEXT("material_limit"), 128, 0, 4096);
	const int32 SocketLimit = ReadIntField(Params, TEXT("socket_limit"), 128, 0, 4096);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [
		AssetPath,
		bIncludeLODs,
		bIncludeMaterials,
		bIncludeSockets,
		bIncludeCollision,
		bIncludeNanite,
		LODLimit,
		SectionLimit,
		MaterialLimit,
		SocketLimit,
		Promise]()
	{
		using namespace MCPToolkit::RuntimeDiagnostics;

		FAssetData AssetData;
		FString ResolvedObjectPath;
		UStaticMesh* Mesh = LoadStaticMesh(AssetPath, AssetData, ResolvedObjectPath);
		if (!Mesh)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("StaticMesh not found: %s"), *AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Mesh);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("resolved_object_path"), ResolvedObjectPath);
		Data->SetStringField(TEXT("package_name"), Mesh->GetOutermost() ? Mesh->GetOutermost()->GetName() : FString());
		Data->SetStringField(TEXT("asset_name"), Mesh->GetName());
		Data->SetObjectField(TEXT("asset_registry"), BuildAssetDataJson(AssetData));
		Data->SetObjectField(TEXT("bounds"), BuildBoundsJson(Mesh));
		Data->SetObjectField(TEXT("resource_size"), BuildResourceSizeJson(Mesh));
		Data->SetNumberField(TEXT("lod_count"), Mesh->GetNumLODs());
		Data->SetNumberField(TEXT("material_slot_count"), Mesh->GetStaticMaterials().Num());
		Data->SetNumberField(TEXT("socket_count"), Mesh->Sockets.Num());
		Data->SetNumberField(TEXT("sections_with_collision"), CountSectionsWithCollision(Mesh));

		if (bIncludeLODs)
		{
			Data->SetObjectField(TEXT("lods"), BuildLODsJson(Mesh, LODLimit, SectionLimit));
		}
		if (bIncludeMaterials)
		{
			Data->SetObjectField(TEXT("materials"), BuildMaterialsJson(Mesh, MaterialLimit));
		}
		if (bIncludeSockets)
		{
			Data->SetObjectField(TEXT("sockets"), BuildSocketsJson(Mesh, SocketLimit));
		}
		if (bIncludeCollision)
		{
			Data->SetObjectField(TEXT("collision"), BuildCollisionJson(Mesh));
		}
		if (bIncludeNanite)
		{
			Data->SetObjectField(TEXT("nanite"), BuildNaniteJson(Mesh));
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("StaticMesh info timed out"));
	}
	return Future.Get();
}
}
