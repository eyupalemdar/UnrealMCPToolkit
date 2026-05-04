// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTWidgetCommands.h"
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


namespace MCPToolkit::CommandHandlers::Widget
{
FString HandleCreateWidgetBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString PackagePath;
	FString AssetName;
	FString ParentClassPath;

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
	{
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	}
	Params->TryGetStringField(TEXT("parent_class"), ParentClassPath);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, ParentClassPath, Promise]()
	{
		UClass* ParentClass = nullptr;
		if (!ParentClassPath.IsEmpty())
		{
			ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
			if (!ParentClass)
			{
				ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
			}
			if (!ParentClass)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not find parent class: %s"), *ParentClassPath)));
				return;
			}
		}

		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::CreateWidgetBlueprint(PackagePath, AssetName, ParentClass);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create Widget Blueprint")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), WBP->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Create widget blueprint timed out"));
	}
	return Future.Get();
}



FString HandleAddWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetClass, WidgetName, ParentName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_class"), WidgetClass))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_class' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	Params->TryGetStringField(TEXT("parent_name"), ParentName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetClass, WidgetName, ParentName, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		UWidget* Widget = UMCTWidgetBlueprintBuilder::AddWidget(WBP, WidgetClass, WidgetName, ParentName);
		if (!Widget)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add widget '%s' of class '%s'"), *WidgetName, *WidgetClass)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("widget_name"), Widget->GetName());
		Data->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Add widget timed out"));
	}
	return Future.Get();
}



FString HandleRemoveWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UMCTWidgetBlueprintBuilder::RemoveWidget(WBP, WidgetName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove widget: %s"), *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("removed"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Remove widget timed out"));
	}
	return Future.Get();
}



FString HandleMoveWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, NewParentName;
	double NewIndexDouble = -1.0;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("new_parent_name"), NewParentName))
	{
		return CreateErrorResponse(TEXT("Missing 'new_parent_name' parameter"));
	}
	Params->TryGetNumberField(TEXT("index"), NewIndexDouble);
	int32 NewIndex = (int32)NewIndexDouble;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, NewParentName, NewIndex, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UMCTWidgetBlueprintBuilder::MoveWidget(WBP, WidgetName, NewParentName, NewIndex);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to move widget '%s' to '%s'"), *WidgetName, *NewParentName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("new_parent"), NewParentName);
		Data->SetNumberField(TEXT("index"), NewIndex);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Move widget timed out"));
	}
	return Future.Get();
}



FString HandleSetWidgetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, PropertyName, Value, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UMCTWidgetBlueprintBuilder::SetWidgetProperty(WBP, WidgetName, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s' on widget '%s'"), *PropertyName, *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set widget property timed out"));
	}
	return Future.Get();
}



FString HandleSetSlotProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, PropertyName, Value, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UMCTWidgetBlueprintBuilder::SetSlotProperty(WBP, WidgetName, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set slot property '%s' on widget '%s'"), *PropertyName, *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set slot property timed out"));
	}
	return Future.Get();
}



FString HandleSetCanvasSlotLayout(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	// All layout params default to 0
	double PosX = 0, PosY = 0, SizeX = 0, SizeY = 0;
	double AnchorMinX = 0, AnchorMinY = 0, AnchorMaxX = 0, AnchorMaxY = 0;
	double AlignmentX = 0, AlignmentY = 0;

	Params->TryGetNumberField(TEXT("position_x"), PosX);
	Params->TryGetNumberField(TEXT("position_y"), PosY);
	Params->TryGetNumberField(TEXT("size_x"), SizeX);
	Params->TryGetNumberField(TEXT("size_y"), SizeY);
	Params->TryGetNumberField(TEXT("anchor_min_x"), AnchorMinX);
	Params->TryGetNumberField(TEXT("anchor_min_y"), AnchorMinY);
	Params->TryGetNumberField(TEXT("anchor_max_x"), AnchorMaxX);
	Params->TryGetNumberField(TEXT("anchor_max_y"), AnchorMaxY);
	Params->TryGetNumberField(TEXT("alignment_x"), AlignmentX);
	Params->TryGetNumberField(TEXT("alignment_y"), AlignmentY);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [=]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UMCTWidgetBlueprintBuilder::SetCanvasSlotLayout(
			WBP, WidgetName,
			(float)PosX, (float)PosY, (float)SizeX, (float)SizeY,
			(float)AnchorMinX, (float)AnchorMinY, (float)AnchorMaxX, (float)AnchorMaxY,
			(float)AlignmentX, (float)AlignmentY);

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set canvas slot layout on widget '%s'"), *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("summary"), FString::Printf(TEXT("Pos(%.0f,%.0f) Size(%.0f,%.0f)"), PosX, PosY, SizeX, SizeY));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set canvas slot layout timed out"));
	}
	return Future.Get();
}



FString HandleSetWidgetProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropertiesObj) || !PropertiesObj || !(*PropertiesObj).IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'properties' object parameter"));
	}

	// Convert JSON object to TMap
	TMap<FString, FString> Properties;
	for (const auto& Pair : (*PropertiesObj)->Values)
	{
		FString StringValue;
		if (Pair.Value->TryGetString(StringValue))
		{
			Properties.Add(Pair.Key, StringValue);
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, Properties, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		TArray<FString> Failed;
		int32 SetCount = UMCTWidgetBlueprintBuilder::SetWidgetProperties(WBP, WidgetName, Properties, &Failed);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("set_count"), SetCount);

		if (Failed.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> FailedArray;
			for (const FString& F : Failed)
			{
				FailedArray.Add(MakeShared<FJsonValueString>(F));
			}
			Data->SetArrayField(TEXT("failed"), FailedArray);
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set widget properties timed out"));
	}
	return Future.Get();
}



FString HandleReparentBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString NewParentClassPath;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("new_parent_class"), NewParentClassPath))
	{
		return CreateErrorResponse(TEXT("Missing 'new_parent_class' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NewParentClassPath, Promise]()
	{
		// Load the Widget Blueprint
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		// Resolve new parent class
		UClass* NewParentClass = FindObject<UClass>(nullptr, *NewParentClassPath);
		if (!NewParentClass)
		{
			NewParentClass = LoadObject<UClass>(nullptr, *NewParentClassPath);
		}
		if (!NewParentClass)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not find parent class: %s"), *NewParentClassPath)));
			return;
		}

		FString OldParentName = WBP->ParentClass ? WBP->ParentClass->GetName() : TEXT("None");

		// Perform reparenting
		bool bSuccess = UMCTWidgetBlueprintBuilder::ReparentBlueprint(WBP, NewParentClass);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to reparent blueprint")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("old_parent"), OldParentName);
		Data->SetStringField(TEXT("new_parent"), NewParentClass->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Reparent blueprint timed out"));
	}
	return Future.Get();
}



FString HandleCompileAndSave(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (WBP)
		{
			TArray<FString> Warnings;
			bool bSuccess = UMCTWidgetBlueprintBuilder::CompileAndSave(WBP, &Warnings);

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("compiled"), bSuccess);
			Data->SetBoolField(TEXT("saved"), bSuccess);

			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> WarningArray;
				for (const FString& W : Warnings)
				{
					WarningArray.Add(MakeShared<FJsonValueString>(W));
				}
				Data->SetArrayField(TEXT("warnings"), WarningArray);
			}

			Promise->SetValue(CreateSuccessResponse(Data));
		}
		else
		{
			// Fallback: Data Asset — just save (no compile step)
			UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
			if (!Asset)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
				return;
			}

			bool bSaved = UMCTDataAssetBuilder::SaveAsset(Asset);
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("compiled"), false);
			Data->SetBoolField(TEXT("saved"), bSaved);
			Promise->SetValue(bSaved ? CreateSuccessResponse(Data) :
				CreateErrorResponse(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath)));
		}
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Compile and save timed out"));
	}
	return Future.Get();
}



FString HandleGetWidgetTree(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UWidgetBlueprint* WBP = UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> TreeJson = UMCTWidgetBlueprintBuilder::GetWidgetTreeAsJson(WBP);
		if (!TreeJson.IsValid())
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Widget tree is empty")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("root"), TreeJson);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Get widget tree timed out"));
	}
	return Future.Get();
}



FString HandleListWidgetClasses()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise]()
	{
		TArray<TPair<FString, bool>> Classes = UMCTWidgetBlueprintBuilder::GetAvailableWidgetClasses();

		TArray<TSharedPtr<FJsonValue>> ClassArray;
		for (const auto& Pair : Classes)
		{
			TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
			ClassObj->SetStringField(TEXT("name"), Pair.Key);
			ClassObj->SetBoolField(TEXT("is_panel"), Pair.Value);
			ClassArray.Add(MakeShared<FJsonValueObject>(ClassObj));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("classes"), ClassArray);
		Data->SetNumberField(TEXT("count"), Classes.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("List widget classes timed out"));
	}
	return Future.Get();
}


}
