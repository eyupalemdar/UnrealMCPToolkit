// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/AIExportRuntimeStreaming.h"

#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentStreaming.h"
#include "Dom/JsonValue.h"
#include "Engine/AssetManager.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StreamableManager.h"
#include "Engine/StreamableRenderAsset.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "RenderAssetUpdate.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace CommonAIExport::RuntimeDiagnostics
{
namespace
{
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
		Data->SetObjectField(TEXT("loaded_object"), BuildObjectReferenceJson(LoadedObject));
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
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(StreamingLevel);
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
		TSharedPtr<FJsonObject> LoadedLevelJson = BuildObjectReferenceJson(LoadedLevel);
		LoadedLevelJson->SetStringField(TEXT("package_name"), LoadedLevel->GetOutermost() ? LoadedLevel->GetOutermost()->GetName() : TEXT(""));
		LoadedLevelJson->SetNumberField(TEXT("actor_slot_count"), LoadedLevel->Actors.Num());
		Data->SetObjectField(TEXT("loaded_level"), LoadedLevelJson);
	}
	else
	{
		Data->SetObjectField(TEXT("loaded_level"), BuildObjectReferenceJson(nullptr));
	}

	return Data;
}

}

TSharedPtr<FJsonObject> BuildAssetStreamingDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	bool bIncludeLevels = true;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_levels"), bIncludeLevels);
	}
	const int32 LevelLimit = ReadClampedIntField(Params, TEXT("level_limit"), 100, 0, 1000);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
		Data->SetObjectField(TEXT("streaming_manager"), BuildStreamingManagerJson());

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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; asset streaming diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
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
		return Data;
}


TSharedPtr<FJsonObject> BuildAsyncLoadDiagnostics(TSharedPtr<FJsonObject> Params)
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
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; async load diagnostics reflect the editor world")));
			}
			Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
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
		return Data;
}


}
