// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTAnimBlueprintCommands.h"
#include "CommandHandlers/MCTCommandResponse.h"
#include "MCTExportFunctionLibrary.h"
#include "Builders/MCTWidgetBlueprintBuilder.h"
#include "Builders/MCTMaterialBuilder.h"
#include "Builders/MCTBlueprintGraphBuilder.h"
#include "Builders/MCTDataAssetBuilder.h"
#include "Builders/MCTAssetFactory.h"
#include "Builders/MCTAnimBlueprintBuilder.h"
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


namespace MCPToolkit::CommandHandlers::AnimBlueprint
{
FString HandleCreateAnimBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, SkeletonPath;
	FString ParentClass = TEXT("AnimInstance");

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		return CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));

	Params->TryGetStringField(TEXT("parent_class"), ParentClass);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, SkeletonPath, ParentClass, Promise]()
	{
		UAnimBlueprint* AnimBP = UMCTAnimBlueprintBuilder::CreateAnimBlueprint(PackagePath, AssetName, SkeletonPath, ParentClass);
		if (!AnimBP)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create AnimBlueprint")));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AnimBP->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("skeleton"), SkeletonPath);
		Data->SetStringField(TEXT("parent_class"), AnimBP->ParentClass ? AnimBP->ParentClass->GetName() : TEXT("None"));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create AnimBlueprint timed out"));
	return Future.Get();
}



FString HandleGetAnimBlueprintInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UAnimBlueprint* AnimBP = UMCTAnimBlueprintBuilder::LoadAnimBlueprint(AssetPath);
		if (!AnimBP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UMCTAnimBlueprintBuilder::GetAnimBlueprintInfoAsJson(AnimBP);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get AnimBlueprint info timed out"));
	return Future.Get();
}


}
