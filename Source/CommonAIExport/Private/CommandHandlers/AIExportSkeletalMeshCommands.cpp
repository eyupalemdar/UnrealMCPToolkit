// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportSkeletalMeshCommands.h"

#include "CommandHandlers/AIExportCommandResponse.h"
#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "BodySetupEnums.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshTypes.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace CommonAIExport::CommandHandlers::SkeletalMesh
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

FString SkinVertexColorChannelToString(const ESkinVertexColorChannel Channel)
{
	switch (Channel)
	{
	case ESkinVertexColorChannel::Red:
		return TEXT("Red");
	case ESkinVertexColorChannel::Green:
		return TEXT("Green");
	case ESkinVertexColorChannel::Blue:
		return TEXT("Blue");
	case ESkinVertexColorChannel::Alpha:
		return TEXT("Alpha");
	default:
		return TEXT("Unknown");
	}
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

TSharedPtr<FJsonObject> BuildTransformJson(const FTransform& Transform)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("location"), BuildVectorJson(Transform.GetLocation()));
	Data->SetObjectField(TEXT("rotation"), BuildRotatorJson(Transform.Rotator()));
	Data->SetObjectField(TEXT("scale"), BuildVectorJson(Transform.GetScale3D()));
	return Data;
}

TSharedPtr<FJsonObject> BuildBoxSphereBoundsJson(const FBoxSphereBounds& Bounds)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("origin"), BuildVectorJson(Bounds.Origin));
	Data->SetObjectField(TEXT("box_extent"), BuildVectorJson(Bounds.BoxExtent));
	Data->SetNumberField(TEXT("sphere_radius"), Bounds.SphereRadius);
	return Data;
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
		if (PackageAsset.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
		{
			return PackageAsset;
		}
	}
	return FAssetData();
}

USkeletalMesh* LoadSkeletalMesh(const FString& AssetPath, FAssetData& OutAssetData, FString& OutResolvedObjectPath)
{
	OutAssetData = FindAssetData(AssetPath);
	if (OutAssetData.IsValid())
	{
		OutResolvedObjectPath = OutAssetData.GetSoftObjectPath().ToString();
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(OutAssetData.GetAsset()))
		{
			return Mesh;
		}
	}

	OutResolvedObjectPath = ToObjectPath(AssetPath);
	if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *OutResolvedObjectPath))
	{
		return Mesh;
	}

	const FString CleanPath = StripObjectPathDecorators(AssetPath);
	if (CleanPath != OutResolvedObjectPath)
	{
		OutResolvedObjectPath = CleanPath;
		return LoadObject<USkeletalMesh>(nullptr, *CleanPath);
	}
	return nullptr;
}

TSharedPtr<FJsonObject> BuildBoundsJson(const USkeletalMesh* Mesh)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Mesh)
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	Data->SetBoolField(TEXT("present"), true);
	Data->SetObjectField(TEXT("bounds"), BuildBoxSphereBoundsJson(Mesh->GetBounds()));
	Data->SetObjectField(TEXT("imported_bounds"), BuildBoxSphereBoundsJson(Mesh->GetImportedBounds()));
	Data->SetObjectField(TEXT("positive_bounds_extension"), BuildVectorJson(Mesh->GetPositiveBoundsExtension()));
	Data->SetObjectField(TEXT("negative_bounds_extension"), BuildVectorJson(Mesh->GetNegativeBoundsExtension()));
	return Data;
}

TSharedPtr<FJsonObject> BuildResourceSizeJson(USkeletalMesh* Mesh)
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

TSharedPtr<FJsonObject> BuildNaniteJson(const USkeletalMesh* Mesh)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Mesh)
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	Data->SetBoolField(TEXT("present"), true);
	Data->SetBoolField(TEXT("render_data_present"), RenderData != nullptr);
	Data->SetBoolField(TEXT("valid_data"), RenderData ? RenderData->HasValidNaniteData() : false);
	Data->SetNumberField(TEXT("nanite_vertex_count"), Mesh->GetNumNaniteVertices());
	Data->SetNumberField(TEXT("nanite_triangle_count"), Mesh->GetNumNaniteTriangles());

#if WITH_EDITORONLY_DATA
	const FMeshNaniteSettings& Settings = Mesh->GetNaniteSettings();
	Data->SetBoolField(TEXT("settings_available"), true);
	Data->SetBoolField(TEXT("enabled"), Mesh->IsNaniteEnabled());
	Data->SetBoolField(TEXT("assembly"), Mesh->IsNaniteAssembly());
	Data->SetBoolField(TEXT("cached_assembly_references_ready"), Mesh->HasCachedNaniteAssemblyReferences());
	Data->SetNumberField(TEXT("cached_assembly_reference_count"), Mesh->GetCachedNaniteAssemblyReferences().Num());
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
	Data->SetNumberField(TEXT("bone_weight_precision"), Settings.BoneWeightPrecision);
	Data->SetNumberField(TEXT("target_minimum_residency_kb"), Settings.TargetMinimumResidencyInKB);
	Data->SetNumberField(TEXT("keep_percent_triangles"), Settings.KeepPercentTriangles);
	Data->SetNumberField(TEXT("trim_relative_error"), Settings.TrimRelativeError);
	Data->SetNumberField(TEXT("fallback_percent_triangles"), Settings.FallbackPercentTriangles);
	Data->SetNumberField(TEXT("fallback_relative_error"), Settings.FallbackRelativeError);
	Data->SetNumberField(TEXT("max_edge_length_factor"), Settings.MaxEdgeLengthFactor);
	Data->SetNumberField(TEXT("num_rays"), Settings.NumRays);
	Data->SetNumberField(TEXT("voxel_level"), Settings.VoxelLevel);
	Data->SetNumberField(TEXT("ray_back_up"), Settings.RayBackUp);
	Data->SetNumberField(TEXT("displacement_uv_channel"), Settings.DisplacementUVChannel);
	Data->SetNumberField(TEXT("displacement_map_count"), Settings.DisplacementMaps.Num());
#else
	Data->SetBoolField(TEXT("settings_available"), false);
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildMaterialJson(const FSkeletalMaterial& Material, const int32 Index)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("slot_name"), Material.MaterialSlotName.ToString());
#if WITH_EDITORONLY_DATA
	Data->SetStringField(TEXT("imported_slot_name"), Material.ImportedMaterialSlotName.ToString());
#endif
	Data->SetObjectField(TEXT("material"), BuildObjectReferenceJson(Material.MaterialInterface));
	Data->SetObjectField(TEXT("overlay_material"), BuildObjectReferenceJson(Material.OverlayMaterialInterface));
	Data->SetBoolField(TEXT("uv_channel_data_initialized"), Material.UVChannelData.bInitialized != 0);
	return Data;
}

TSharedPtr<FJsonObject> BuildMaterialsJson(const USkeletalMesh* Mesh, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> MaterialsJson;
	const int32 MaterialCount = Mesh ? Mesh->GetMaterials().Num() : 0;

	for (int32 Index = 0; Index < MaterialCount && MaterialsJson.Num() < Limit; ++Index)
	{
		MaterialsJson.Add(MakeShared<FJsonValueObject>(BuildMaterialJson(Mesh->GetMaterials()[Index], Index)));
	}

	Data->SetNumberField(TEXT("count"), MaterialCount);
	Data->SetBoolField(TEXT("truncated"), MaterialsJson.Num() < MaterialCount);
	Data->SetArrayField(TEXT("slots"), MaterialsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSocketJson(const USkeletalMeshSocket* Socket, const int32 Index)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Socket);
	Data->SetNumberField(TEXT("index"), Index);
	if (!Socket)
	{
		return Data;
	}

	Data->SetStringField(TEXT("socket_name"), Socket->SocketName.ToString());
	Data->SetStringField(TEXT("bone_name"), Socket->BoneName.ToString());
	Data->SetObjectField(TEXT("relative_location"), BuildVectorJson(Socket->RelativeLocation));
	Data->SetObjectField(TEXT("relative_rotation"), BuildRotatorJson(Socket->RelativeRotation));
	Data->SetObjectField(TEXT("relative_scale"), BuildVectorJson(Socket->RelativeScale));
	Data->SetBoolField(TEXT("force_always_animated"), Socket->bForceAlwaysAnimated);
	return Data;
}

TSharedPtr<FJsonObject> BuildSocketArrayJson(const TArray<USkeletalMeshSocket*>& Sockets, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SocketsJson;

	for (int32 Index = 0; Index < Sockets.Num() && SocketsJson.Num() < Limit; ++Index)
	{
		SocketsJson.Add(MakeShared<FJsonValueObject>(BuildSocketJson(Sockets[Index], Index)));
	}

	Data->SetNumberField(TEXT("count"), Sockets.Num());
	Data->SetBoolField(TEXT("truncated"), SocketsJson.Num() < Sockets.Num());
	Data->SetArrayField(TEXT("sockets"), SocketsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSocketsJson(const USkeletalMesh* Mesh, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Mesh)
	{
		Data->SetNumberField(TEXT("mesh_only_count"), 0);
		Data->SetNumberField(TEXT("active_count"), 0);
		return Data;
	}

	const TArray<USkeletalMeshSocket*>& MeshOnlySockets = Mesh->GetMeshOnlySocketList();
	const TArray<USkeletalMeshSocket*> ActiveSockets = Mesh->GetActiveSocketList();

	Data->SetNumberField(TEXT("mesh_only_count"), MeshOnlySockets.Num());
	Data->SetNumberField(TEXT("active_count"), ActiveSockets.Num());
	Data->SetObjectField(TEXT("mesh_only"), BuildSocketArrayJson(MeshOnlySockets, Limit));
	Data->SetObjectField(TEXT("active"), BuildSocketArrayJson(ActiveSockets, Limit));
	return Data;
}

TSharedPtr<FJsonObject> BuildBoneJson(const FReferenceSkeleton& RefSkeleton, const int32 BoneIndex)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), BoneIndex);
	if (!RefSkeleton.IsValidIndex(BoneIndex))
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
	Data->SetBoolField(TEXT("present"), true);
	Data->SetStringField(TEXT("name"), RefSkeleton.GetBoneName(BoneIndex).ToString());
	Data->SetNumberField(TEXT("parent_index"), ParentIndex);
	Data->SetStringField(TEXT("parent_name"), RefSkeleton.IsValidIndex(ParentIndex) ? RefSkeleton.GetBoneName(ParentIndex).ToString() : FString());
	if (RefSkeleton.GetRefBonePose().IsValidIndex(BoneIndex))
	{
		Data->SetObjectField(TEXT("ref_pose"), BuildTransformJson(RefSkeleton.GetRefBonePose()[BoneIndex]));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildReferenceSkeletonJson(const FReferenceSkeleton& RefSkeleton, const int32 BoneLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> BonesJson;
	const int32 BoneCount = RefSkeleton.GetNum();

	for (int32 BoneIndex = 0; BoneIndex < BoneCount && BonesJson.Num() < BoneLimit; ++BoneIndex)
	{
		BonesJson.Add(MakeShared<FJsonValueObject>(BuildBoneJson(RefSkeleton, BoneIndex)));
	}

	Data->SetNumberField(TEXT("bone_count"), BoneCount);
	Data->SetNumberField(TEXT("raw_bone_count"), RefSkeleton.GetRawBoneNum());
	Data->SetBoolField(TEXT("bones_truncated"), BonesJson.Num() < BoneCount);
	Data->SetArrayField(TEXT("bones"), BonesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSkeletonJson(const USkeletalMesh* Mesh, const int32 BoneLimit)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Mesh)
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	const USkeleton* Skeleton = Mesh->GetSkeleton();
	Data->SetBoolField(TEXT("present"), true);
	Data->SetObjectField(TEXT("skeleton_asset"), BuildObjectReferenceJson(Skeleton));
	Data->SetObjectField(TEXT("mesh_reference_skeleton"), BuildReferenceSkeletonJson(Mesh->GetRefSkeleton(), BoneLimit));
	if (Skeleton)
	{
		Data->SetObjectField(TEXT("skeleton_reference_skeleton"), BuildReferenceSkeletonJson(Skeleton->GetReferenceSkeleton(), BoneLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildPhysicsBodyJson(const USkeletalBodySetup* BodySetup, const int32 Index)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(BodySetup);
	Data->SetNumberField(TEXT("index"), Index);
	if (!BodySetup)
	{
		return Data;
	}

	const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
	Data->SetStringField(TEXT("body_name"), BodySetup->GetName());
	Data->SetStringField(TEXT("collision_trace_flag"), LexToString(BodySetup->CollisionTraceFlag));
	Data->SetBoolField(TEXT("double_sided_geometry"), BodySetup->bDoubleSidedGeometry != 0);
	Data->SetBoolField(TEXT("generate_mirrored_collision"), BodySetup->bGenerateMirroredCollision != 0);
	Data->SetBoolField(TEXT("skip_scale_from_animation"), BodySetup->bSkipScaleFromAnimation);
	Data->SetNumberField(TEXT("physical_animation_profile_count"), BodySetup->GetPhysicalAnimationProfiles().Num());
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

TSharedPtr<FJsonObject> BuildConstraintJson(const UPhysicsConstraintTemplate* Constraint, const int32 Index)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Constraint);
	Data->SetNumberField(TEXT("index"), Index);
	if (!Constraint)
	{
		return Data;
	}

	const FConstraintInstance& Instance = Constraint->DefaultInstance;
	Data->SetStringField(TEXT("child_bone"), Instance.GetChildBoneName().ToString());
	Data->SetStringField(TEXT("parent_bone"), Instance.GetParentBoneName().ToString());
	Data->SetNumberField(TEXT("profile_count"), Constraint->ProfileHandles.Num());
	Data->SetBoolField(TEXT("disable_collision"), Instance.ProfileInstance.bDisableCollision);
	Data->SetBoolField(TEXT("linear_breakable"), Instance.ProfileInstance.bLinearBreakable);
	Data->SetBoolField(TEXT("angular_breakable"), Instance.ProfileInstance.bAngularBreakable);
	Data->SetNumberField(TEXT("linear_limit"), Instance.GetLinearLimit());
	Data->SetNumberField(TEXT("swing1_limit_degrees"), Instance.GetAngularSwing1Limit());
	Data->SetNumberField(TEXT("swing2_limit_degrees"), Instance.GetAngularSwing2Limit());
	Data->SetNumberField(TEXT("twist_limit_degrees"), Instance.GetAngularTwistLimit());
	return Data;
}

TSharedPtr<FJsonObject> BuildPhysicsAssetJson(const USkeletalMesh* Mesh, const int32 BodyLimit, const int32 ConstraintLimit)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	UPhysicsAsset* PhysicsAsset = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(PhysicsAsset);
	Data->SetObjectField(TEXT("shadow_physics_asset"), BuildObjectReferenceJson(Mesh ? Mesh->GetShadowPhysicsAsset() : nullptr));
	if (!PhysicsAsset)
	{
		return Data;
	}

	TArray<TSharedPtr<FJsonValue>> BodiesJson;
	for (int32 Index = 0; Index < PhysicsAsset->SkeletalBodySetups.Num() && BodiesJson.Num() < BodyLimit; ++Index)
	{
		BodiesJson.Add(MakeShared<FJsonValueObject>(BuildPhysicsBodyJson(PhysicsAsset->SkeletalBodySetups[Index], Index)));
	}

	TArray<TSharedPtr<FJsonValue>> ConstraintsJson;
	for (int32 Index = 0; Index < PhysicsAsset->ConstraintSetup.Num() && ConstraintsJson.Num() < ConstraintLimit; ++Index)
	{
		ConstraintsJson.Add(MakeShared<FJsonValueObject>(BuildConstraintJson(PhysicsAsset->ConstraintSetup[Index], Index)));
	}

	Data->SetNumberField(TEXT("body_count"), PhysicsAsset->SkeletalBodySetups.Num());
	Data->SetNumberField(TEXT("constraint_count"), PhysicsAsset->ConstraintSetup.Num());
	Data->SetBoolField(TEXT("bodies_truncated"), BodiesJson.Num() < PhysicsAsset->SkeletalBodySetups.Num());
	Data->SetBoolField(TEXT("constraints_truncated"), ConstraintsJson.Num() < PhysicsAsset->ConstraintSetup.Num());
	Data->SetArrayField(TEXT("bodies"), BodiesJson);
	Data->SetArrayField(TEXT("constraints"), ConstraintsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSectionJson(const FSkelMeshRenderSection& Section, const int32 SectionIndex)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), SectionIndex);
	Data->SetNumberField(TEXT("material_index"), Section.MaterialIndex);
	Data->SetNumberField(TEXT("base_index"), Section.BaseIndex);
	Data->SetNumberField(TEXT("triangle_count"), Section.NumTriangles);
	Data->SetNumberField(TEXT("base_vertex_index"), Section.BaseVertexIndex);
	Data->SetNumberField(TEXT("vertex_count"), Section.NumVertices);
	Data->SetNumberField(TEXT("max_bone_influences"), Section.MaxBoneInfluences);
	Data->SetNumberField(TEXT("bone_map_count"), Section.BoneMap.Num());
	Data->SetNumberField(TEXT("cloth_lod_mapping_count"), Section.ClothMappingDataLODs.Num());
	Data->SetNumberField(TEXT("correspond_cloth_asset_index"), Section.CorrespondClothAssetIndex);
	Data->SetBoolField(TEXT("valid"), Section.IsValid());
	Data->SetBoolField(TEXT("disabled"), Section.bDisabled);
	Data->SetBoolField(TEXT("recompute_tangent"), Section.bRecomputeTangent);
	Data->SetBoolField(TEXT("cast_shadow"), Section.bCastShadow);
	Data->SetBoolField(TEXT("visible_in_ray_tracing"), Section.bVisibleInRayTracing);
	Data->SetBoolField(TEXT("has_clothing_data"), Section.HasClothingData());
	Data->SetStringField(TEXT("recompute_tangents_vertex_mask_channel"), SkinVertexColorChannelToString(Section.RecomputeTangentsVertexMaskChannel));
	return Data;
}

TSharedPtr<FJsonObject> BuildLODJson(const USkeletalMesh* Mesh, const FSkeletalMeshRenderData* RenderData, const int32 LODIndex, const bool bIncludeSections, const int32 SectionLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), LODIndex);
	if (!Mesh || !RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	const FSkeletalMeshLODRenderData& LODResource = RenderData->LODRenderData[LODIndex];
	Data->SetBoolField(TEXT("present"), true);
	Data->SetNumberField(TEXT("vertex_count"), LODResource.GetNumVertices());
	Data->SetNumberField(TEXT("triangle_count"), LODResource.GetTotalFaces());
	Data->SetNumberField(TEXT("tex_coord_count"), LODResource.GetNumTexCoords());
	Data->SetNumberField(TEXT("section_count"), LODResource.RenderSections.Num());
	Data->SetNumberField(TEXT("position_vertex_count"), LODResource.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
	Data->SetNumberField(TEXT("static_mesh_vertex_count"), LODResource.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
	Data->SetNumberField(TEXT("color_vertex_count"), LODResource.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices());
	Data->SetBoolField(TEXT("has_color_vertex_data"), LODResource.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() > 0);
	Data->SetBoolField(TEXT("position_cpu_access"), LODResource.StaticVertexBuffers.PositionVertexBuffer.GetAllowCPUAccess());
	Data->SetBoolField(TEXT("static_mesh_cpu_access"), LODResource.StaticVertexBuffers.StaticMeshVertexBuffer.GetAllowCPUAccess());
	Data->SetBoolField(TEXT("color_cpu_access"), LODResource.StaticVertexBuffers.ColorVertexBuffer.GetAllowCPUAccess());
	Data->SetBoolField(TEXT("high_precision_tangent_basis"), LODResource.StaticVertexBuffers.StaticMeshVertexBuffer.GetUseHighPrecisionTangentBasis());
	Data->SetNumberField(TEXT("skin_weight_max_bone_influences"), LODResource.GetVertexBufferMaxBoneInfluences());
	Data->SetBoolField(TEXT("skin_weight_uses_16bit_bone_index"), LODResource.DoesVertexBufferUse16BitBoneIndex());
	Data->SetNumberField(TEXT("active_bone_count"), LODResource.ActiveBoneIndices.Num());
	Data->SetNumberField(TEXT("required_bone_count"), LODResource.RequiredBones.Num());
	Data->SetNumberField(TEXT("buffer_size_bytes"), LODResource.BuffersSize);
	Data->SetBoolField(TEXT("has_cloth_data"), LODResource.HasClothData());
	Data->SetBoolField(TEXT("streamed_data_inlined"), LODResource.bStreamedDataInlined != 0);
	Data->SetBoolField(TEXT("lod_optional"), LODResource.bIsLODOptional != 0);

	if (const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(LODIndex))
	{
		Data->SetNumberField(TEXT("screen_size"), LODInfo->ScreenSize.GetValue());
		Data->SetNumberField(TEXT("lod_hysteresis"), LODInfo->LODHysteresis);
		Data->SetBoolField(TEXT("allow_cpu_access"), LODInfo->bAllowCPUAccess != 0);
		Data->SetBoolField(TEXT("has_been_simplified"), LODInfo->bHasBeenSimplified != 0);
		Data->SetBoolField(TEXT("has_per_lod_vertex_colors"), LODInfo->bHasPerLODVertexColors != 0);
		Data->SetBoolField(TEXT("build_half_edge_buffers"), LODInfo->bBuildHalfEdgeBuffers != 0);
		Data->SetBoolField(TEXT("allow_mesh_deformer"), LODInfo->bAllowMeshDeformer != 0);
		Data->SetBoolField(TEXT("support_uniform_sampling"), LODInfo->bSupportUniformlyDistributedSampling != 0);
		Data->SetNumberField(TEXT("lod_material_map_count"), LODInfo->LODMaterialMap.Num());
		Data->SetNumberField(TEXT("bones_to_remove_count"), LODInfo->BonesToRemove.Num());
		Data->SetNumberField(TEXT("bones_to_prioritize_count"), LODInfo->BonesToPrioritize.Num());
		Data->SetNumberField(TEXT("sections_to_prioritize_count"), LODInfo->SectionsToPrioritize.Num());
		Data->SetNumberField(TEXT("vertex_attribute_count"), LODInfo->VertexAttributes.Num());
	}

	if (bIncludeSections)
	{
		TArray<TSharedPtr<FJsonValue>> SectionsJson;
		for (int32 SectionIndex = 0; SectionIndex < LODResource.RenderSections.Num() && SectionsJson.Num() < SectionLimit; ++SectionIndex)
		{
			SectionsJson.Add(MakeShared<FJsonValueObject>(BuildSectionJson(LODResource.RenderSections[SectionIndex], SectionIndex)));
		}
		Data->SetBoolField(TEXT("sections_truncated"), SectionsJson.Num() < LODResource.RenderSections.Num());
		Data->SetArrayField(TEXT("sections"), SectionsJson);
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildLODsJson(const USkeletalMesh* Mesh, const int32 LODLimit, const bool bIncludeSections, const int32 SectionLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const FSkeletalMeshRenderData* RenderData = Mesh ? Mesh->GetResourceForRendering() : nullptr;
	const int32 LODCount = Mesh ? Mesh->GetLODNum() : 0;
	TArray<TSharedPtr<FJsonValue>> LODsJson;

	for (int32 LODIndex = 0; LODIndex < LODCount && LODsJson.Num() < LODLimit; ++LODIndex)
	{
		LODsJson.Add(MakeShared<FJsonValueObject>(BuildLODJson(Mesh, RenderData, LODIndex, bIncludeSections, SectionLimit)));
	}

	Data->SetBoolField(TEXT("render_data_present"), RenderData != nullptr);
	Data->SetBoolField(TEXT("valid_render_data"), RenderData != nullptr && RenderData->LODRenderData.Num() > 0);
	Data->SetBoolField(TEXT("nanite_render_data_valid"), RenderData ? RenderData->HasValidNaniteData() : false);
	Data->SetNumberField(TEXT("count"), LODCount);
	Data->SetNumberField(TEXT("render_lod_count"), RenderData ? RenderData->LODRenderData.Num() : 0);
	Data->SetNumberField(TEXT("non_streaming_lod_count"), RenderData ? RenderData->NumInlinedLODs : 0);
	Data->SetNumberField(TEXT("inlined_lod_count"), RenderData ? RenderData->NumInlinedLODs : 0);
	Data->SetNumberField(TEXT("non_optional_lod_count"), RenderData ? RenderData->NumNonOptionalLODs : 0);
	Data->SetNumberField(TEXT("current_first_lod_index"), RenderData ? RenderData->CurrentFirstLODIdx : INDEX_NONE);
	Data->SetNumberField(TEXT("pending_first_lod_index"), RenderData ? RenderData->PendingFirstLODIdx : INDEX_NONE);
	Data->SetBoolField(TEXT("truncated"), LODsJson.Num() < LODCount);
	Data->SetArrayField(TEXT("lods"), LODsJson);
	return Data;
}
}

FString HandleSkeletalMeshInfo(TSharedPtr<FJsonObject> Params)
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
	const bool bIncludeSkeleton = ReadBoolField(Params, TEXT("include_skeleton"), true);
	const bool bIncludePhysicsAsset = ReadBoolField(Params, TEXT("include_physics_asset"), true);
	const bool bIncludeBounds = ReadBoolField(Params, TEXT("include_bounds"), true);
	const bool bIncludeNanite = ReadBoolField(Params, TEXT("include_nanite"), true);
	const int32 LODLimit = ReadIntField(Params, TEXT("lod_limit"), 8, 0, 64);
	const int32 SectionLimit = bIncludeSections ? ReadIntField(Params, TEXT("section_limit"), 64, 0, 2048) : 0;
	const int32 MaterialLimit = ReadIntField(Params, TEXT("material_limit"), 128, 0, 4096);
	const int32 SocketLimit = ReadIntField(Params, TEXT("socket_limit"), 128, 0, 4096);
	const int32 BoneLimit = ReadIntField(Params, TEXT("bone_limit"), 256, 0, 8192);
	const int32 PhysicsBodyLimit = ReadIntField(Params, TEXT("physics_body_limit"), 128, 0, 4096);
	const int32 ConstraintLimit = ReadIntField(Params, TEXT("constraint_limit"), 128, 0, 4096);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [
		AssetPath,
		bIncludeLODs,
		bIncludeSections,
		bIncludeMaterials,
		bIncludeSockets,
		bIncludeSkeleton,
		bIncludePhysicsAsset,
		bIncludeBounds,
		bIncludeNanite,
		LODLimit,
		SectionLimit,
		MaterialLimit,
		SocketLimit,
		BoneLimit,
		PhysicsBodyLimit,
		ConstraintLimit,
		Promise]()
	{
		using namespace CommonAIExport::RuntimeDiagnostics;

		FAssetData AssetData;
		FString ResolvedObjectPath;
		USkeletalMesh* Mesh = LoadSkeletalMesh(AssetPath, AssetData, ResolvedObjectPath);
		if (!Mesh)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("SkeletalMesh not found: %s"), *AssetPath)));
			return;
		}

		const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
		const TArray<USkeletalMeshSocket*> ActiveSockets = Mesh->GetActiveSocketList();
		UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();

		TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Mesh);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("resolved_object_path"), ResolvedObjectPath);
		Data->SetStringField(TEXT("package_name"), Mesh->GetOutermost() ? Mesh->GetOutermost()->GetName() : FString());
		Data->SetStringField(TEXT("asset_name"), Mesh->GetName());
		Data->SetObjectField(TEXT("asset_registry"), BuildAssetDataJson(AssetData));
		Data->SetObjectField(TEXT("resource_size"), BuildResourceSizeJson(Mesh));
		Data->SetNumberField(TEXT("lod_count"), Mesh->GetLODNum());
		Data->SetNumberField(TEXT("material_slot_count"), Materials.Num());
		Data->SetNumberField(TEXT("mesh_socket_count"), Mesh->GetMeshOnlySocketList().Num());
		Data->SetNumberField(TEXT("active_socket_count"), ActiveSockets.Num());
		Data->SetNumberField(TEXT("mesh_bone_count"), Mesh->GetRefSkeleton().GetNum());
		Data->SetNumberField(TEXT("raw_mesh_bone_count"), Mesh->GetRefSkeleton().GetRawBoneNum());
		Data->SetNumberField(TEXT("morph_target_count"), Mesh->GetMorphTargets().Num());
		Data->SetNumberField(TEXT("physics_body_count"), PhysicsAsset ? PhysicsAsset->SkeletalBodySetups.Num() : 0);
		Data->SetNumberField(TEXT("physics_constraint_count"), PhysicsAsset ? PhysicsAsset->ConstraintSetup.Num() : 0);
		Data->SetObjectField(TEXT("skeleton_asset"), BuildObjectReferenceJson(Mesh->GetSkeleton()));
		Data->SetObjectField(TEXT("physics_asset"), BuildObjectReferenceJson(PhysicsAsset));

		if (bIncludeBounds)
		{
			Data->SetObjectField(TEXT("bounds"), BuildBoundsJson(Mesh));
		}
		if (bIncludeLODs)
		{
			Data->SetObjectField(TEXT("lods"), BuildLODsJson(Mesh, LODLimit, bIncludeSections, SectionLimit));
		}
		if (bIncludeMaterials)
		{
			Data->SetObjectField(TEXT("materials"), BuildMaterialsJson(Mesh, MaterialLimit));
		}
		if (bIncludeSockets)
		{
			Data->SetObjectField(TEXT("sockets"), BuildSocketsJson(Mesh, SocketLimit));
		}
		if (bIncludeSkeleton)
		{
			Data->SetObjectField(TEXT("skeleton"), BuildSkeletonJson(Mesh, BoneLimit));
		}
		if (bIncludePhysicsAsset)
		{
			Data->SetObjectField(TEXT("physics"), BuildPhysicsAssetJson(Mesh, PhysicsBodyLimit, ConstraintLimit));
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
		return CreateErrorResponse(TEXT("SkeletalMesh info timed out"));
	}
	return Future.Get();
}
}
