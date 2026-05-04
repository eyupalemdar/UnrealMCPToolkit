// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportExportCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"
#include "AIExportFunctionLibrary.h"
#include "AIExporterRegistry.h"
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


namespace CommonAIExport::CommandHandlers::Export
{
FString HandleExportWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		FAIExportResult Result = UAIExportFunctionLibrary::ExportWidgetBlueprintByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}



FString HandleExportBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		// Use ExportAssetByPath to support all asset types (Blueprint, Audio, Input, etc.)
		FAIExportResult Result = UAIExportFunctionLibrary::ExportAssetByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}



FString HandleListSupportedTypes()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> Types;
	TArray<TSharedPtr<FJsonValue>> ClassPaths;

	TArray<UClass*> SupportedClasses = UAIExporterRegistry::Get()->GetAllSupportedClasses();
	SupportedClasses.Sort([](const UClass& A, const UClass& B)
	{
		return A.GetName() < B.GetName();
	});

	for (UClass* SupportedClass : SupportedClasses)
	{
		if (!SupportedClass)
		{
			continue;
		}
		Types.Add(MakeShared<FJsonValueString>(SupportedClass->GetName()));
		ClassPaths.Add(MakeShared<FJsonValueString>(SupportedClass->GetPathName()));
	}

	Data->SetArrayField(TEXT("types"), Types);
	Data->SetArrayField(TEXT("class_paths"), ClassPaths);
	return CreateSuccessResponse(Data);
}


}
