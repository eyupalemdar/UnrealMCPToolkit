// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTExportCDOCommands.h"
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


namespace MCPToolkit::CommandHandlers::CDO
{
FString HandleSetCDOProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyName, Value, Promise]()
	{
		UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		if (WBP)
		{
			bool bSuccess = UMCTWidgetBlueprintBuilder::SetCDOProperty(WBP, PropertyName, Value);
			if (!bSuccess)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set CDO property '%s'"), *PropertyName)));
				return;
			}
		}
		else
		{
			// Non-WBP path: if Asset is a Blueprint (e.g. CommonButtonStyle subclass),
			// redirect writes to its generated class CDO so we can set parent-class
			// properties like NormalBase / NormalHovered (not present on the BP itself).
			UObject* TargetForSet = Asset;
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				UClass* GenClass = BP->GeneratedClass;
				if (!GenClass)
				{
					FKismetEditorUtilities::CompileBlueprint(BP);
					GenClass = BP->GeneratedClass;
				}
				if (!GenClass)
				{
					Promise->SetValue(CreateErrorResponse(TEXT("Blueprint has no GeneratedClass")));
					return;
				}
				TargetForSet = GenClass->GetDefaultObject();
				if (!TargetForSet)
				{
					Promise->SetValue(CreateErrorResponse(TEXT("Blueprint generated class has no CDO")));
					return;
				}
			}

			bool bSuccess = UMCTDataAssetBuilder::SetProperty(TargetForSet, PropertyName, Value);
			if (!bSuccess)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s'"), *PropertyName)));
				return;
			}
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Data->SetStringField(TEXT("value"), Value);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set CDO property timed out"));
	return Future.Get();
}



FString HandleGetCDOProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		if (WBP)
		{
			TSharedPtr<FJsonObject> PropsJson = UMCTWidgetBlueprintBuilder::GetCDOPropertiesAsJson(WBP);
			Promise->SetValue(CreateSuccessResponse(PropsJson));
		}
		else
		{
			// Non-WBP path: if Asset is a Blueprint, read properties from its CDO so
			// inherited fields (e.g. CommonButtonStyle::NormalBase) are visible.
			UObject* TargetForRead = Asset;
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				UClass* GenClass = BP->GeneratedClass;
				if (!GenClass)
				{
					FKismetEditorUtilities::CompileBlueprint(BP);
					GenClass = BP->GeneratedClass;
				}
				if (GenClass && GenClass->GetDefaultObject())
				{
					TargetForRead = GenClass->GetDefaultObject();
				}
			}
			TSharedPtr<FJsonObject> PropsJson = UMCTDataAssetBuilder::GetPropertiesAsJson(TargetForRead);
			Promise->SetValue(CreateSuccessResponse(PropsJson));
		}
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get CDO properties timed out"));
	return Future.Get();
}



FString HandleAddCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName, ElementValuesJson, ClassName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	Params->TryGetStringField(TEXT("element_values"), ElementValuesJson);
	Params->TryGetStringField(TEXT("class_name"), ClassName);

	// Parse element_values JSON string into a map
	TMap<FString, FString> ElementValues;
	if (!ElementValuesJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ElementValuesJson);
		TSharedPtr<FJsonObject> ValuesObj;
		if (FJsonSerializer::Deserialize(Reader, ValuesObj) && ValuesObj.IsValid())
		{
			for (const auto& Pair : ValuesObj->Values)
			{
				FString Val;
				if (Pair.Value->TryGetString(Val))
				{
					ElementValues.Add(Pair.Key, Val);
				}
			}
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementValues, ClassName, Promise]()
	{
		UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		int32 NewIndex = -1;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass)
			{
				FKismetEditorUtilities::CompileBlueprint(WBP);
				GenClass = WBP->GeneratedClass;
			}
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }

			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			NewIndex = UMCTWidgetBlueprintBuilder::AddArrayElement(CDO, ArrayName, ElementValues, ClassName);
		}
		else if (BP)
		{
			// Non-WBP Blueprint (BPTYPE_Const data assets, etc.)
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass)
			{
				FKismetEditorUtilities::CompileBlueprint(BP);
				GenClass = BP->GeneratedClass;
			}
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }

			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			NewIndex = UMCTWidgetBlueprintBuilder::AddArrayElement(CDO, ArrayName, ElementValues, ClassName);
		}
		else
		{
			NewIndex = UMCTDataAssetBuilder::AddArrayElement(Asset, ArrayName, ElementValues, ClassName);
		}

		if (NewIndex < 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add element to '%s'"), *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("index"), NewIndex);
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add CDO array element timed out"));
	return Future.Get();
}



FString HandleSetCDOArrayElementProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName, PropertyName, Value;
	double Index = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return CreateErrorResponse(TEXT("Missing 'index' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));

	int32 ElementIndex = (int32)Index;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementIndex, PropertyName, Value, Promise]()
	{
		UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = false;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			bSuccess = UMCTWidgetBlueprintBuilder::SetArrayElementProperty(CDO, ArrayName, ElementIndex, PropertyName, Value);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			bSuccess = UMCTWidgetBlueprintBuilder::SetArrayElementProperty(CDO, ArrayName, ElementIndex, PropertyName, Value);
		}
		else
		{
			bSuccess = UMCTDataAssetBuilder::SetArrayElementProperty(Asset, ArrayName, ElementIndex, PropertyName, Value);
		}

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set '%s' on element %d of '%s'"),
				*PropertyName, ElementIndex, *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("index"), ElementIndex);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set CDO array element property timed out"));
	return Future.Get();
}



FString HandleRemoveCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName;
	double Index = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return CreateErrorResponse(TEXT("Missing 'index' parameter"));

	int32 ElementIndex = (int32)Index;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementIndex, Promise]()
	{
		UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = false;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			bSuccess = UMCTWidgetBlueprintBuilder::RemoveArrayElement(CDO, ArrayName, ElementIndex);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			bSuccess = UMCTWidgetBlueprintBuilder::RemoveArrayElement(CDO, ArrayName, ElementIndex);
		}
		else
		{
			bSuccess = UMCTDataAssetBuilder::RemoveArrayElement(Asset, ArrayName, ElementIndex);
		}

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove element %d from '%s'"),
				ElementIndex, *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("removed_index"), ElementIndex);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove CDO array element timed out"));
	return Future.Get();
}



FString HandleGetCDOArrayLength(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, Promise]()
	{
		UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		int32 Length = -1;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			Length = UMCTWidgetBlueprintBuilder::GetArrayLength(CDO, ArrayName);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			Length = UMCTWidgetBlueprintBuilder::GetArrayLength(CDO, ArrayName);
		}
		else
		{
			Length = UMCTDataAssetBuilder::GetArrayLength(Asset, ArrayName);
		}

		if (Length < 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Array '%s' not found"), *ArrayName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("length"), Length);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get CDO array length timed out"));
	return Future.Get();
}


}
