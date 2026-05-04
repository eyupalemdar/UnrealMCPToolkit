// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportAnimationAssetCommands.h"

#include "CommandHandlers/AIExportCommandResponse.h"
#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimEnums.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace CommonAIExport::CommandHandlers::AnimationAsset
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
		const FTopLevelAssetPath ClassPath = PackageAsset.AssetClassPath;
		if (ClassPath == UAnimSequence::StaticClass()->GetClassPathName()
			|| ClassPath == UAnimMontage::StaticClass()->GetClassPathName())
		{
			return PackageAsset;
		}
	}
	return FAssetData();
}

UAnimationAsset* LoadAnimationAsset(const FString& AssetPath, FAssetData& OutAssetData, FString& OutResolvedObjectPath)
{
	OutAssetData = FindAssetData(AssetPath);
	if (OutAssetData.IsValid())
	{
		OutResolvedObjectPath = OutAssetData.GetSoftObjectPath().ToString();
		if (UAnimationAsset* Asset = Cast<UAnimationAsset>(OutAssetData.GetAsset()))
		{
			return Asset;
		}
	}

	OutResolvedObjectPath = ToObjectPath(AssetPath);
	if (UAnimationAsset* Asset = LoadObject<UAnimationAsset>(nullptr, *OutResolvedObjectPath))
	{
		return Asset;
	}

	const FString CleanPath = StripObjectPathDecorators(AssetPath);
	if (CleanPath != OutResolvedObjectPath)
	{
		OutResolvedObjectPath = CleanPath;
		return LoadObject<UAnimationAsset>(nullptr, *CleanPath);
	}
	return nullptr;
}

TSharedPtr<FJsonObject> BuildResourceSizeJson(UAnimationAsset* Asset)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Asset)
	{
		Data->SetBoolField(TEXT("present"), false);
		return Data;
	}

	FResourceSizeEx ExclusiveSize(EResourceSizeMode::Exclusive);
	Asset->GetResourceSizeEx(ExclusiveSize);
	FResourceSizeEx EstimatedTotalSize(EResourceSizeMode::EstimatedTotal);
	Asset->GetResourceSizeEx(EstimatedTotalSize);

	Data->SetBoolField(TEXT("present"), true);
	Data->SetNumberField(TEXT("exclusive_bytes"), static_cast<double>(ExclusiveSize.GetTotalMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_total_bytes"), static_cast<double>(EstimatedTotalSize.GetTotalMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_system_bytes"), static_cast<double>(EstimatedTotalSize.GetDedicatedSystemMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_video_bytes"), static_cast<double>(EstimatedTotalSize.GetDedicatedVideoMemoryBytes()));
	Data->SetNumberField(TEXT("estimated_unknown_bytes"), static_cast<double>(EstimatedTotalSize.GetUnknownMemoryBytes()));
	return Data;
}

TSharedPtr<FJsonObject> BuildSkeletonJson(const UAnimationAsset* Asset)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	const USkeleton* Skeleton = Asset ? Asset->GetSkeleton() : nullptr;
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Skeleton);
	if (Skeleton)
	{
		Data->SetNumberField(TEXT("bone_count"), Skeleton->GetReferenceSkeleton().GetNum());
		Data->SetNumberField(TEXT("raw_bone_count"), Skeleton->GetReferenceSkeleton().GetRawBoneNum());
	}
	return Data;
}

FString ResolveNotifyName(const FAnimNotifyEvent& NotifyEvent)
{
	if (!NotifyEvent.NotifyName.IsNone())
	{
		return NotifyEvent.NotifyName.ToString();
	}
	if (NotifyEvent.Notify)
	{
		return NotifyEvent.Notify->GetName();
	}
	if (NotifyEvent.NotifyStateClass)
	{
		return NotifyEvent.NotifyStateClass->GetName();
	}
	return FString();
}

TSharedPtr<FJsonObject> BuildNotifyJson(const FAnimNotifyEvent& NotifyEvent, const int32 Index)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("name"), ResolveNotifyName(NotifyEvent));
	Data->SetStringField(TEXT("notify_name"), NotifyEvent.NotifyName.ToString());
	Data->SetNumberField(TEXT("trigger_time"), NotifyEvent.GetTriggerTime());
	Data->SetNumberField(TEXT("duration"), NotifyEvent.GetDuration());
	Data->SetNumberField(TEXT("track_index"), NotifyEvent.TrackIndex);
	Data->SetNumberField(TEXT("trigger_weight_threshold"), NotifyEvent.TriggerWeightThreshold);
	Data->SetObjectField(TEXT("notify"), BuildObjectReferenceJson(NotifyEvent.Notify));
	Data->SetObjectField(TEXT("notify_state"), BuildObjectReferenceJson(NotifyEvent.NotifyStateClass));
	Data->SetBoolField(TEXT("is_state"), NotifyEvent.NotifyStateClass != nullptr);
	Data->SetBoolField(TEXT("is_blueprint_notify"), NotifyEvent.IsBlueprintNotify());
	return Data;
}

TSharedPtr<FJsonObject> BuildNotifiesJson(const UAnimSequenceBase* SequenceBase, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> NotifiesJson;
	const int32 NotifyCount = SequenceBase ? SequenceBase->Notifies.Num() : 0;

	for (int32 Index = 0; SequenceBase && Index < SequenceBase->Notifies.Num() && NotifiesJson.Num() < Limit; ++Index)
	{
		NotifiesJson.Add(MakeShared<FJsonValueObject>(BuildNotifyJson(SequenceBase->Notifies[Index], Index)));
	}

	Data->SetNumberField(TEXT("count"), NotifyCount);
	Data->SetBoolField(TEXT("truncated"), NotifiesJson.Num() < NotifyCount);
	Data->SetArrayField(TEXT("events"), NotifiesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildCurveJson(const FFloatCurve& Curve, const int32 Index)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("name"), Curve.GetName().ToString());
	Data->SetNumberField(TEXT("flags"), Curve.GetCurveTypeFlags());
	Data->SetNumberField(TEXT("key_count"), Curve.FloatCurve.GetNumKeys());
	return Data;
}

TSharedPtr<FJsonObject> BuildCurvesJson(const UAnimSequenceBase* SequenceBase, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> CurvesJson;
	if (!SequenceBase)
	{
		Data->SetNumberField(TEXT("float_count"), 0);
		Data->SetNumberField(TEXT("vector_count"), 0);
		Data->SetNumberField(TEXT("transform_count"), 0);
		Data->SetArrayField(TEXT("float_curves"), CurvesJson);
		return Data;
	}

	const FRawCurveTracks& CurveData = SequenceBase->GetCurveData();
	for (int32 Index = 0; Index < CurveData.FloatCurves.Num() && CurvesJson.Num() < Limit; ++Index)
	{
		CurvesJson.Add(MakeShared<FJsonValueObject>(BuildCurveJson(CurveData.FloatCurves[Index], Index)));
	}

	Data->SetNumberField(TEXT("float_count"), CurveData.FloatCurves.Num());
#if WITH_EDITORONLY_DATA
	Data->SetNumberField(TEXT("vector_count"), CurveData.VectorCurves.Num());
	Data->SetNumberField(TEXT("transform_count"), CurveData.TransformCurves.Num());
#else
	Data->SetNumberField(TEXT("vector_count"), 0);
	Data->SetNumberField(TEXT("transform_count"), 0);
#endif
	Data->SetBoolField(TEXT("float_curves_truncated"), CurvesJson.Num() < CurveData.FloatCurves.Num());
	Data->SetArrayField(TEXT("float_curves"), CurvesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildDataModelJson(const UAnimSequence* Sequence, const int32 TrackNameLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const IAnimationDataModel* DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
	Data->SetBoolField(TEXT("present"), DataModel != nullptr);
	if (!DataModel)
	{
		return Data;
	}

	TArray<FName> TrackNames;
	DataModel->GetBoneTrackNames(TrackNames);
	TArray<TSharedPtr<FJsonValue>> TrackNamesJson;
	for (const FName& TrackName : TrackNames)
	{
		if (TrackNamesJson.Num() >= TrackNameLimit)
		{
			break;
		}
		TrackNamesJson.Add(MakeShared<FJsonValueString>(TrackName.ToString()));
	}

	Data->SetNumberField(TEXT("play_length"), DataModel->GetPlayLength());
	Data->SetNumberField(TEXT("source_frame_count"), DataModel->GetNumberOfFrames());
	Data->SetNumberField(TEXT("source_key_count"), DataModel->GetNumberOfKeys());
	Data->SetNumberField(TEXT("source_frame_rate"), DataModel->GetFrameRate().AsDecimal());
	Data->SetNumberField(TEXT("bone_track_count"), DataModel->GetNumBoneTracks());
	Data->SetNumberField(TEXT("float_curve_count"), DataModel->GetNumberOfFloatCurves());
	Data->SetNumberField(TEXT("transform_curve_count"), DataModel->GetNumberOfTransformCurves());
	Data->SetNumberField(TEXT("attribute_count"), DataModel->GetNumberOfAttributes());
	Data->SetBoolField(TEXT("track_names_truncated"), TrackNamesJson.Num() < TrackNames.Num());
	Data->SetArrayField(TEXT("track_names"), TrackNamesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildMarkersJson(UAnimSequence* Sequence, const int32 Limit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> MarkersJson;
	TArray<FName>* UniqueMarkers = Sequence ? Sequence->GetUniqueMarkerNames() : nullptr;
	const int32 MarkerCount = UniqueMarkers ? UniqueMarkers->Num() : 0;

	if (UniqueMarkers)
	{
		for (const FName& MarkerName : *UniqueMarkers)
		{
			if (MarkersJson.Num() >= Limit)
			{
				break;
			}
			MarkersJson.Add(MakeShared<FJsonValueString>(MarkerName.ToString()));
		}
	}

	Data->SetNumberField(TEXT("count"), MarkerCount);
	Data->SetBoolField(TEXT("truncated"), MarkersJson.Num() < MarkerCount);
	Data->SetArrayField(TEXT("names"), MarkersJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSequenceJson(UAnimSequence* Sequence, const bool bIncludeDataModel, const bool bIncludeMarkers, const int32 MarkerLimit, const int32 TrackNameLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Sequence != nullptr);
	if (!Sequence)
	{
		return Data;
	}

	Data->SetStringField(TEXT("additive_type"), EnumValueToString(StaticEnum<EAdditiveAnimationType>(), static_cast<int64>(Sequence->AdditiveAnimType)));
	Data->SetStringField(TEXT("interpolation"), EnumValueToString(StaticEnum<EAnimInterpolationType>(), static_cast<int64>(Sequence->Interpolation)));
	Data->SetStringField(TEXT("retarget_source"), Sequence->RetargetSource.ToString());
	Data->SetBoolField(TEXT("enable_root_motion"), Sequence->bEnableRootMotion);
	Data->SetBoolField(TEXT("force_root_lock"), Sequence->bForceRootLock);
	Data->SetBoolField(TEXT("use_normalized_root_motion_scale"), Sequence->bUseNormalizedRootMotionScale);
	Data->SetBoolField(TEXT("root_motion_settings_copied_from_montage"), Sequence->bRootMotionSettingsCopiedFromMontage);
	Data->SetStringField(TEXT("root_motion_root_lock"), EnumValueToString(StaticEnum<ERootMotionRootLock::Type>(), static_cast<int64>(Sequence->RootMotionRootLock.GetValue())));
	Data->SetStringField(TEXT("strip_data_on_dedicated_server"), EnumValueToString(StaticEnum<EStripAnimDataOnDedicatedServerSettings>(), static_cast<int64>(Sequence->StripAnimDataOnDedicatedServer)));

	if (bIncludeDataModel)
	{
		Data->SetObjectField(TEXT("data_model"), BuildDataModelJson(Sequence, TrackNameLimit));
	}
	if (bIncludeMarkers)
	{
		Data->SetObjectField(TEXT("markers"), BuildMarkersJson(Sequence, MarkerLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildSegmentJson(const FAnimSegment& Segment, const int32 Index)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetObjectField(TEXT("animation"), BuildObjectReferenceJson(Segment.GetAnimReference()));
	Data->SetNumberField(TEXT("start_pos"), Segment.StartPos);
	Data->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
	Data->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
	Data->SetNumberField(TEXT("anim_play_rate"), Segment.AnimPlayRate);
	Data->SetNumberField(TEXT("looping_count"), Segment.LoopingCount);
	Data->SetNumberField(TEXT("valid_play_rate"), Segment.GetValidPlayRate());
	Data->SetNumberField(TEXT("length"), Segment.GetLength());
	return Data;
}

TSharedPtr<FJsonObject> BuildSlotJson(const FSlotAnimationTrack& SlotTrack, const int32 Index, const int32 SegmentLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SegmentsJson;

	for (int32 SegmentIndex = 0; SegmentIndex < SlotTrack.AnimTrack.AnimSegments.Num() && SegmentsJson.Num() < SegmentLimit; ++SegmentIndex)
	{
		SegmentsJson.Add(MakeShared<FJsonValueObject>(BuildSegmentJson(SlotTrack.AnimTrack.AnimSegments[SegmentIndex], SegmentIndex)));
	}

	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());
	Data->SetNumberField(TEXT("segment_count"), SlotTrack.AnimTrack.AnimSegments.Num());
	Data->SetBoolField(TEXT("segments_truncated"), SegmentsJson.Num() < SlotTrack.AnimTrack.AnimSegments.Num());
	Data->SetArrayField(TEXT("segments"), SegmentsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildSectionJson(const UAnimMontage* Montage, const FCompositeSection& Section, const int32 Index)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("index"), Index);
	Data->SetStringField(TEXT("name"), Section.SectionName.ToString());
	Data->SetStringField(TEXT("next_section"), Section.NextSectionName.ToString());
	Data->SetNumberField(TEXT("start_time"), Section.GetTime());
	Data->SetNumberField(TEXT("length"), Montage ? Montage->GetSectionLength(Index) : 0.0f);
	Data->SetNumberField(TEXT("metadata_count"), Section.GetMetaData().Num());
	return Data;
}

TSharedPtr<FJsonObject> BuildMontageJson(UAnimMontage* Montage, const int32 SlotLimit, const int32 SectionLimit, const int32 SegmentLimit)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Montage != nullptr);
	if (!Montage)
	{
		return Data;
	}

	TArray<TSharedPtr<FJsonValue>> SectionsJson;
	for (int32 SectionIndex = 0; SectionIndex < Montage->CompositeSections.Num() && SectionsJson.Num() < SectionLimit; ++SectionIndex)
	{
		SectionsJson.Add(MakeShared<FJsonValueObject>(BuildSectionJson(Montage, Montage->CompositeSections[SectionIndex], SectionIndex)));
	}

	TArray<TSharedPtr<FJsonValue>> SlotsJson;
	for (int32 SlotIndex = 0; SlotIndex < Montage->SlotAnimTracks.Num() && SlotsJson.Num() < SlotLimit; ++SlotIndex)
	{
		SlotsJson.Add(MakeShared<FJsonValueObject>(BuildSlotJson(Montage->SlotAnimTracks[SlotIndex], SlotIndex, SegmentLimit)));
	}

	int32 BranchingPointCount = 0;
	for (const FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
	{
		if (NotifyEvent.IsBranchingPoint())
		{
			++BranchingPointCount;
		}
	}

	Data->SetNumberField(TEXT("blend_in_time"), Montage->GetDefaultBlendInTime());
	Data->SetNumberField(TEXT("blend_out_time"), Montage->GetDefaultBlendOutTime());
	Data->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
	Data->SetBoolField(TEXT("enable_auto_blend_out"), Montage->bEnableAutoBlendOut);
	Data->SetBoolField(TEXT("dynamic_montage"), Montage->IsDynamicMontage());
	Data->SetBoolField(TEXT("has_root_motion"), Montage->HasRootMotion());
	Data->SetStringField(TEXT("sync_group"), Montage->SyncGroup.ToString());
	Data->SetNumberField(TEXT("sync_slot_index"), Montage->SyncSlotIndex);
	Data->SetObjectField(TEXT("first_animation_reference"), BuildObjectReferenceJson(Montage->GetFirstAnimReference()));
	Data->SetNumberField(TEXT("section_count"), Montage->CompositeSections.Num());
	Data->SetBoolField(TEXT("sections_truncated"), SectionsJson.Num() < Montage->CompositeSections.Num());
	Data->SetArrayField(TEXT("sections"), SectionsJson);
	Data->SetNumberField(TEXT("slot_count"), Montage->SlotAnimTracks.Num());
	Data->SetBoolField(TEXT("slots_truncated"), SlotsJson.Num() < Montage->SlotAnimTracks.Num());
	Data->SetArrayField(TEXT("slots"), SlotsJson);
	Data->SetNumberField(TEXT("branching_point_notify_count"), BranchingPointCount);
	return Data;
}

TSharedPtr<FJsonObject> BuildReferencesJson(UAnimationAsset* Asset, const int32 Limit)
{
	using namespace CommonAIExport::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ReferencesJson;
	TArray<UAnimationAsset*> References;
	if (Asset)
	{
		Asset->GetAllAnimationSequencesReferred(References, true);
	}

	for (int32 Index = 0; Index < References.Num() && ReferencesJson.Num() < Limit; ++Index)
	{
		ReferencesJson.Add(MakeShared<FJsonValueObject>(BuildObjectReferenceJson(References[Index])));
	}

	Data->SetNumberField(TEXT("count"), References.Num());
	Data->SetBoolField(TEXT("truncated"), ReferencesJson.Num() < References.Num());
	Data->SetArrayField(TEXT("assets"), ReferencesJson);
	return Data;
}
}

FString HandleAnimationAssetInfo(TSharedPtr<FJsonObject> Params)
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

	const bool bIncludeNotifies = ReadBoolField(Params, TEXT("include_notifies"), true);
	const bool bIncludeCurves = ReadBoolField(Params, TEXT("include_curves"), true);
	const bool bIncludeMontage = ReadBoolField(Params, TEXT("include_montage"), true);
	const bool bIncludeSequence = ReadBoolField(Params, TEXT("include_sequence"), true);
	const bool bIncludeSkeleton = ReadBoolField(Params, TEXT("include_skeleton"), true);
	const bool bIncludeDataModel = ReadBoolField(Params, TEXT("include_data_model"), true);
	const bool bIncludeMarkers = ReadBoolField(Params, TEXT("include_markers"), true);
	const bool bIncludeReferences = ReadBoolField(Params, TEXT("include_references"), true);
	const int32 NotifyLimit = ReadIntField(Params, TEXT("notify_limit"), 128, 0, 4096);
	const int32 CurveLimit = ReadIntField(Params, TEXT("curve_limit"), 256, 0, 8192);
	const int32 MarkerLimit = ReadIntField(Params, TEXT("marker_limit"), 128, 0, 4096);
	const int32 TrackNameLimit = ReadIntField(Params, TEXT("track_name_limit"), 256, 0, 8192);
	const int32 SlotLimit = ReadIntField(Params, TEXT("slot_limit"), 64, 0, 2048);
	const int32 SectionLimit = ReadIntField(Params, TEXT("section_limit"), 128, 0, 4096);
	const int32 SegmentLimit = ReadIntField(Params, TEXT("segment_limit"), 128, 0, 4096);
	const int32 ReferenceLimit = ReadIntField(Params, TEXT("reference_limit"), 256, 0, 4096);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [
		AssetPath,
		bIncludeNotifies,
		bIncludeCurves,
		bIncludeMontage,
		bIncludeSequence,
		bIncludeSkeleton,
		bIncludeDataModel,
		bIncludeMarkers,
		bIncludeReferences,
		NotifyLimit,
		CurveLimit,
		MarkerLimit,
		TrackNameLimit,
		SlotLimit,
		SectionLimit,
		SegmentLimit,
		ReferenceLimit,
		Promise]()
	{
		using namespace CommonAIExport::RuntimeDiagnostics;

		FAssetData AssetData;
		FString ResolvedObjectPath;
		UAnimationAsset* Asset = LoadAnimationAsset(AssetPath, AssetData, ResolvedObjectPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath)));
			return;
		}

		UAnimSequenceBase* SequenceBase = Cast<UAnimSequenceBase>(Asset);
		UAnimSequence* Sequence = Cast<UAnimSequence>(Asset);
		UAnimMontage* Montage = Cast<UAnimMontage>(Asset);

		TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Asset);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("resolved_object_path"), ResolvedObjectPath);
		Data->SetStringField(TEXT("package_name"), Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : FString());
		Data->SetStringField(TEXT("asset_name"), Asset->GetName());
		Data->SetStringField(TEXT("asset_type"), Montage ? TEXT("AnimMontage") : (Sequence ? TEXT("AnimSequence") : (SequenceBase ? TEXT("AnimSequenceBase") : TEXT("AnimationAsset"))));
		Data->SetObjectField(TEXT("asset_registry"), BuildAssetDataJson(AssetData));
		Data->SetObjectField(TEXT("resource_size"), BuildResourceSizeJson(Asset));
		Data->SetNumberField(TEXT("play_length"), Asset->GetPlayLength());
		Data->SetNumberField(TEXT("rate_scale"), SequenceBase ? SequenceBase->RateScale : 1.0f);
		Data->SetBoolField(TEXT("loop"), SequenceBase ? SequenceBase->bLoop : false);
		Data->SetBoolField(TEXT("has_root_motion"), SequenceBase ? SequenceBase->HasRootMotion() : false);
		Data->SetNumberField(TEXT("sampled_key_count"), SequenceBase ? SequenceBase->GetNumberOfSampledKeys() : 0);
		Data->SetNumberField(TEXT("sampling_frame_rate"), SequenceBase ? SequenceBase->GetSamplingFrameRate().AsDecimal() : 0.0);
		Data->SetNumberField(TEXT("notify_count"), SequenceBase ? SequenceBase->Notifies.Num() : 0);
		Data->SetObjectField(TEXT("skeleton_asset"), BuildObjectReferenceJson(Asset->GetSkeleton()));

		if (bIncludeSkeleton)
		{
			Data->SetObjectField(TEXT("skeleton"), BuildSkeletonJson(Asset));
		}
		if (bIncludeNotifies)
		{
			Data->SetObjectField(TEXT("notifies"), BuildNotifiesJson(SequenceBase, NotifyLimit));
		}
		if (bIncludeCurves)
		{
			Data->SetObjectField(TEXT("curves"), BuildCurvesJson(SequenceBase, CurveLimit));
		}
		if (bIncludeSequence)
		{
			Data->SetObjectField(TEXT("sequence"), BuildSequenceJson(Sequence, bIncludeDataModel, bIncludeMarkers, MarkerLimit, TrackNameLimit));
		}
		if (bIncludeMontage)
		{
			Data->SetObjectField(TEXT("montage"), BuildMontageJson(Montage, SlotLimit, SectionLimit, SegmentLimit));
		}
		if (bIncludeReferences)
		{
			Data->SetObjectField(TEXT("references"), BuildReferencesJson(Asset, ReferenceLimit));
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Animation asset info timed out"));
	}
	return Future.Get();
}
}
