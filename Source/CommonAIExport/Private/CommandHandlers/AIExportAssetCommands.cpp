// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportAssetCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"
#include "AIExportFunctionLibrary.h"
#include "Builders/AIWidgetBlueprintBuilder.h"
#include "Builders/AIMaterialBuilder.h"
#include "Builders/AIBlueprintGraphBuilder.h"
#include "Builders/AIDataAssetBuilder.h"
#include "Builders/AIAssetFactory.h"
#include "Builders/AIAnimBlueprintBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"

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


namespace CommonAIExport::CommandHandlers::Asset
{
TSharedPtr<FJsonObject> BuildAssetDataJson(const FAssetData& AssetData)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!AssetData.IsValid())
	{
		return Data;
	}

	Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
	Data->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
	Data->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
	Data->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
	Data->SetBoolField(TEXT("is_redirector"), AssetData.AssetClassPath.ToString().Contains(TEXT("ObjectRedirector")));
	return Data;
}

FName NormalizePackageName(const FString& InAssetPath)
{
	FString PackageName = InAssetPath;
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName = PackageName.Left(DotIndex);
	}
	return FName(*PackageName);
}

FString HandleCreateAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, AssetType;
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_type"), AssetType))
		return CreateErrorResponse(TEXT("Missing 'asset_type' parameter"));

	// Parse optional initial properties
	TMap<FString, FString> InitialProperties;
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FString Val;
			if (Pair.Value->TryGetString(Val))
			{
				InitialProperties.Add(Pair.Key, Val);
			}
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, AssetType, InitialProperties, Promise]()
	{
		UObject* Asset = UAIAssetFactory::CreateAsset(PackagePath, AssetName, AssetType, InitialProperties);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create %s"), *AssetType)));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("asset_type"), AssetType);
		Data->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create asset timed out"));
	return Future.Get();
}



FString HandleSetAssetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyPath, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath))
		return CreateErrorResponse(TEXT("Missing 'property_path'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyPath, Value, Promise]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIDataAssetBuilder::SetProperty(Asset, PropertyPath, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Data->SetStringField(TEXT("property_path"), PropertyPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set asset property timed out"));
	return Future.Get();
}



FString HandleGetAssetProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Props = UAIDataAssetBuilder::GetPropertiesAsJson(Asset);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Data->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Data->SetObjectField(TEXT("properties"), Props);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get asset properties timed out"));
	return Future.Get();
}



FString HandleAssetExists(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		FAssetData AssetData = AR.GetAssetByObjectPath(FSoftObjectPath(PackageName.ToString() + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName.ToString())));

		if (!AssetData.IsValid())
		{
			TArray<FAssetData> PackageAssets;
			AR.GetAssetsByPackageName(PackageName, PackageAssets);
			if (PackageAssets.Num() > 0)
			{
				AssetData = PackageAssets[0];
			}
		}

		FString PackageFilename;
		const bool bPackageExistsOnDisk = FPackageName::DoesPackageExist(PackageName.ToString(), &PackageFilename);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetBoolField(TEXT("exists"), AssetData.IsValid() || bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("asset_registry_valid"), AssetData.IsValid());
		Data->SetBoolField(TEXT("package_exists_on_disk"), bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetStringField(TEXT("package_filename"), PackageFilename);
		if (AssetData.IsValid())
		{
			Data->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
			Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
			Data->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
		}
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Asset exists timed out"));
	return Future.Get();
}



FString HandleScanAssetPaths(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	TArray<FString> Paths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("paths"), PathsArray) && PathsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *PathsArray)
		{
			FString PathValue;
			if (Value.IsValid() && Value->TryGetString(PathValue) && !PathValue.IsEmpty())
			{
				Paths.Add(PathValue);
			}
		}
	}
	else
	{
		FString SinglePath;
		if (Params->TryGetStringField(TEXT("path"), SinglePath) && !SinglePath.IsEmpty())
		{
			Paths.Add(SinglePath);
		}
	}

	if (Paths.Num() == 0)
	{
		return CreateErrorResponse(TEXT("Missing 'paths' array or 'path' parameter"));
	}

	bool bForceRescan = false;
	Params->TryGetBoolField(TEXT("force_rescan"), bForceRescan);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Paths, bForceRescan, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		AR.ScanPathsSynchronous(Paths, bForceRescan);

		TArray<TSharedPtr<FJsonValue>> PathValues;
		PathValues.Reserve(Paths.Num());
		for (const FString& Path : Paths)
		{
			PathValues.Add(MakeShared<FJsonValueString>(Path));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("paths"), PathValues);
		Data->SetNumberField(TEXT("count"), Paths.Num());
		Data->SetBoolField(TEXT("force_rescan"), bForceRescan);
		Data->SetBoolField(TEXT("scanned"), true);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Scan asset paths timed out"));
	return Future.Get();
}



FString HandleAssetSearch(TSharedPtr<FJsonObject> Params)
{
	FString Path = TEXT("/Game");
	FString NameFilter;
	FString ClassFilter;
	bool bRecursive = true;
	int32 Offset = 0;
	int32 Limit = 100;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("recursive"), bRecursive);

		double NumberValue = 0.0;
		if (Params->TryGetNumberField(TEXT("offset"), NumberValue) && NumberValue > 0.0)
		{
			Offset = static_cast<int32>(NumberValue);
		}
		if (Params->TryGetNumberField(TEXT("limit"), NumberValue) && NumberValue > 0.0)
		{
			Limit = FMath::Clamp(static_cast<int32>(NumberValue), 1, 1000);
		}
	}
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Path, NameFilter, ClassFilter, bRecursive, Offset, Limit, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Path));
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<TSharedPtr<FJsonValue>> Results;
		int32 MatchedCount = 0;
		for (const FAssetData& AssetData : Assets)
		{
			const FString AssetName = AssetData.AssetName.ToString();
			const FString PackageName = AssetData.PackageName.ToString();
			const FString ClassPath = AssetData.AssetClassPath.ToString();
			if (!NameFilter.IsEmpty() && !AssetName.Contains(NameFilter, ESearchCase::IgnoreCase) && !PackageName.Contains(NameFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}

			if (MatchedCount >= Offset && Results.Num() < Limit)
			{
				Results.Add(MakeShared<FJsonValueObject>(BuildAssetDataJson(AssetData)));
			}
			++MatchedCount;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("path"), Path);
		Data->SetStringField(TEXT("name_filter"), NameFilter);
		Data->SetStringField(TEXT("class_filter"), ClassFilter);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("offset"), Offset);
		Data->SetNumberField(TEXT("limit"), Limit);
		Data->SetNumberField(TEXT("returned_count"), Results.Num());
		Data->SetNumberField(TEXT("matched_count"), MatchedCount);
		Data->SetBoolField(TEXT("truncated"), MatchedCount > Offset + Results.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("assets"), Results);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Asset search timed out"));
	return Future.Get();
}



FString HandleAssetValidateLight(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FAssetData> PackageAssets;
		AR.GetAssetsByPackageName(PackageName, PackageAssets);

		FString PackageFilename;
		const bool bPackageExistsOnDisk = FPackageName::DoesPackageExist(PackageName.ToString(), &PackageFilename);

		TArray<FName> Dependencies;
		TArray<FName> Referencers;
		AR.GetDependencies(PackageName, Dependencies);
		AR.GetReferencers(PackageName, Referencers);

		TArray<TSharedPtr<FJsonValue>> Warnings;
		TArray<TSharedPtr<FJsonValue>> ExternalDependencies;
		if (PackageAssets.Num() == 0 && !bPackageExistsOnDisk)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("asset_missing")));
		}
		if (bScanIncomplete)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("asset_registry_scan_was_incomplete")));
		}

		for (const FAssetData& AssetData : PackageAssets)
		{
			if (AssetData.AssetClassPath.ToString().Contains(TEXT("ObjectRedirector")))
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("asset_is_redirector")));
				break;
			}
		}

		for (const FName& Dependency : Dependencies)
		{
			const FString DependencyPath = Dependency.ToString();
			if (!DependencyPath.StartsWith(TEXT("/Game/")) && !DependencyPath.StartsWith(TEXT("/Engine/")) && !DependencyPath.StartsWith(TEXT("/Script/")))
			{
				ExternalDependencies.Add(MakeShared<FJsonValueString>(DependencyPath));
			}
		}
		if (ExternalDependencies.Num() > 0)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("has_non_project_engine_script_dependencies")));
		}

		TArray<TSharedPtr<FJsonValue>> AssetArray;
		for (const FAssetData& AssetData : PackageAssets)
		{
			AssetArray.Add(MakeShared<FJsonValueObject>(BuildAssetDataJson(AssetData)));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetStringField(TEXT("package_filename"), PackageFilename);
		Data->SetBoolField(TEXT("valid"), Warnings.Num() == 0);
		Data->SetBoolField(TEXT("exists"), PackageAssets.Num() > 0 || bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("asset_registry_valid"), PackageAssets.Num() > 0);
		Data->SetBoolField(TEXT("package_exists_on_disk"), bPackageExistsOnDisk);
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetNumberField(TEXT("assets_in_package_count"), PackageAssets.Num());
		Data->SetNumberField(TEXT("dependency_count"), Dependencies.Num());
		Data->SetNumberField(TEXT("referencer_count"), Referencers.Num());
		Data->SetNumberField(TEXT("external_dependency_count"), ExternalDependencies.Num());
		Data->SetArrayField(TEXT("assets"), AssetArray);
		Data->SetArrayField(TEXT("external_dependencies"), ExternalDependencies);
		Data->SetArrayField(TEXT("warnings"), Warnings);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Asset validation timed out"));
	return Future.Get();
}



FString HandleSaveAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		bool bSaved = UAIDataAssetBuilder::SaveAsset(Asset);
		if (!bSaved) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to save: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Save asset timed out"));
	return Future.Get();
}



FString HandleRenameAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	// Either or both may be provided. Empty = keep current value.
	FString NewPackagePath, NewAssetName;
	Params->TryGetStringField(TEXT("new_package_path"), NewPackagePath);
	Params->TryGetStringField(TEXT("new_asset_name"), NewAssetName);

	if (NewPackagePath.IsEmpty() && NewAssetName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("At least one of 'new_package_path' or 'new_asset_name' must be provided"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NewPackagePath, NewAssetName, Promise]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		// For Blueprint assets, the FAssetRenameData target should be the BP itself, not its generated class.
		// LoadAssetObject typically returns the BP UObject, which is what we want.

		const UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Asset has no outer package")));
			return;
		}

		const FString CurrentPackageName = Package->GetName();
		const FString CurrentPackagePath = FPackageName::GetLongPackagePath(CurrentPackageName);
		const FString CurrentAssetName = Asset->GetName();

		const FString FinalPackagePath = NewPackagePath.IsEmpty() ? CurrentPackagePath : NewPackagePath;
		const FString FinalAssetName = NewAssetName.IsEmpty() ? CurrentAssetName : NewAssetName;

		if (FinalPackagePath == CurrentPackagePath && FinalAssetName == CurrentAssetName)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("New path equals current path; nothing to rename")));
			return;
		}

		TArray<FAssetRenameData> AssetsToRename;
		AssetsToRename.Add(FAssetRenameData(Asset, FinalPackagePath, FinalAssetName));

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		IAssetTools& AssetTools = AssetToolsModule.Get();
		bool RenameResult = AssetTools.RenameAssets(AssetsToRename);

		if (!RenameResult)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(
				TEXT("RenameAssets failed (result=%d) for %s -> %s/%s"),
				RenameResult, *AssetPath, *FinalPackagePath, *FinalAssetName)));
			return;
		}

		const FString NewAssetPath = FinalPackagePath / FinalAssetName;

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("renamed"), true);
		Data->SetStringField(TEXT("old_path"), AssetPath);
		Data->SetStringField(TEXT("new_path"), NewAssetPath);
		Data->SetStringField(TEXT("new_package_path"), FinalPackagePath);
		Data->SetStringField(TEXT("new_asset_name"), FinalAssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Rename asset timed out"));
	return Future.Get();
}



FString HandleGetReferencers(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			// Block briefly so reference results are not falsely empty.
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FName> Referencers;
		AR.GetReferencers(PackageName, Referencers);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Referencers.Num());
		for (const FName& Ref : Referencers)
		{
			Array.Add(MakeShared<FJsonValueString>(Ref.ToString()));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("referencers"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get referencers timed out"));
	return Future.Get();
}



FString HandleGetDependencies(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FName> Dependencies;
		AR.GetDependencies(PackageName, Dependencies);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Dependencies.Num());
		for (const FName& Dep : Dependencies)
		{
			Array.Add(MakeShared<FJsonValueString>(Dep.ToString()));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("dependencies"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get dependencies timed out"));
	return Future.Get();
}



FString HandleDeleteAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	bool bForce = false;
	Params->TryGetBoolField(TEXT("force"), bForce);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, bForce, Promise]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		const FString PackageName = Asset->GetOutermost()->GetName();

		int32 NumDeleted = 0;
		if (bForce)
		{
			TArray<UObject*> Objects;
			Objects.Add(Asset);
			NumDeleted = ObjectTools::ForceDeleteObjects(Objects, /*bShowConfirmation=*/false);
		}
		else
		{
			TArray<FAssetData> AssetsToDelete;
			AssetsToDelete.Add(FAssetData(Asset));
			NumDeleted = ObjectTools::DeleteAssets(AssetsToDelete, /*bShowConfirmation=*/false);
		}

		if (NumDeleted == 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(
				TEXT("Delete returned 0 for %s (check referencers with get_referencers, or pass force=true to bypass reference check)"),
				*AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("deleted"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName);
		Data->SetNumberField(TEXT("num_deleted"), NumDeleted);
		Data->SetBoolField(TEXT("force"), bForce);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Delete asset timed out"));
	return Future.Get();
}



FString HandleListRedirectors(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath))
		return CreateErrorResponse(TEXT("Missing 'folder_path' parameter"));

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [FolderPath, bRecursive, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*FolderPath));
		Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Assets.Num());
		for (const FAssetData& AssetData : Assets)
		{
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset());
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
			Entry->SetStringField(TEXT("destination_path"),
				(Redirector && Redirector->DestinationObject)
					? Redirector->DestinationObject->GetPathName()
					: TEXT(""));
			Entry->SetBoolField(TEXT("stale"),
				!(Redirector && Redirector->DestinationObject));
			Array.Add(MakeShared<FJsonValueObject>(Entry));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("folder_path"), FolderPath);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("redirectors"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List redirectors timed out"));
	return Future.Get();
}



FString HandleFixupRedirectors(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath))
		return CreateErrorResponse(TEXT("Missing 'folder_path' parameter"));

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [FolderPath, bRecursive, Promise]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*FolderPath));
		Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<UObjectRedirector*> Redirectors;
		TArray<TSharedPtr<FJsonValue>> Skipped;
		Redirectors.Reserve(Assets.Num());
		for (const FAssetData& AssetData : Assets)
		{
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset());
			if (!Redirector)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
				Entry->SetStringField(TEXT("reason"), TEXT("load_failed"));
				Skipped.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			if (!Redirector->DestinationObject)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
				Entry->SetStringField(TEXT("reason"), TEXT("stale_no_destination"));
				Skipped.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Redirectors.Add(Redirector);
		}

		const int32 FoundCount = Assets.Num();
		const int32 FixedCount = Redirectors.Num();

		if (Redirectors.Num() > 0)
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().FixupReferencers(Redirectors, /*bCheckoutDialogPrompt=*/false);
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("folder_path"), FolderPath);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("redirectors_found"), FoundCount);
		Data->SetNumberField(TEXT("redirectors_fixed"), FixedCount);
		Data->SetNumberField(TEXT("skipped_count"), Skipped.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("skipped"), Skipped);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Fixup redirectors timed out"));
	return Future.Get();
}


}
