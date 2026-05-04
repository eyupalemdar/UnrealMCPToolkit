// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTBlueprintGraphCommands.h"
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


namespace MCPToolkit::CommandHandlers::BlueprintGraph
{
TArray<FMCTBlueprintGraphPinSpec> ParseGraphPinSpecs(
	const TSharedPtr<FJsonObject>& Params,
	const FString& FieldName)
{
	TArray<FMCTBlueprintGraphPinSpec> Specs;

	const TArray<TSharedPtr<FJsonValue>>* PinArray = nullptr;
	if (!Params.IsValid() || !Params->TryGetArrayField(FieldName, PinArray) || !PinArray)
	{
		return Specs;
	}

	for (const TSharedPtr<FJsonValue>& Value : *PinArray)
	{
		const TSharedPtr<FJsonObject>* PinObj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(PinObj) || !PinObj || !PinObj->IsValid())
		{
			continue;
		}

		FMCTBlueprintGraphPinSpec Spec;
		(*PinObj)->TryGetStringField(TEXT("name"), Spec.Name);
		(*PinObj)->TryGetStringField(TEXT("type"), Spec.Type);
		(*PinObj)->TryGetStringField(TEXT("default_value"), Spec.DefaultValue);
		if (Spec.DefaultValue.IsEmpty())
		{
			(*PinObj)->TryGetStringField(TEXT("default"), Spec.DefaultValue);
		}

		if (!Spec.Name.IsEmpty())
		{
			Specs.Add(MoveTemp(Spec));
		}
	}

	return Specs;
}

// Helper macro for graph node creation handlers (they all follow the same pattern)
#define GRAPH_NODE_HANDLER_BODY(HandlerName, BuilderCall)                                      \
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));        \
	FString AssetPath, NodeName;                                                               \
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))                             \
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));                    \
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))                               \
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));                     \
	FString GraphName;                                                                         \
	Params->TryGetStringField(TEXT("graph_name"), GraphName);                                  \
	double PosX = 0, PosY = 0;                                                                \
	Params->TryGetNumberField(TEXT("pos_x"), PosX);                                            \
	Params->TryGetNumberField(TEXT("pos_y"), PosY);

FString HandleAddEventNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddEventNode, AddEventNode)

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddEventNode(BP, EventName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Data->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add event node timed out"));
	return Future.Get();
}



FString HandleAddCustomEvent(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddCustomEvent, AddCustomEvent)

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddCustomEvent(BP, EventName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add custom event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add custom event timed out"));
	return Future.Get();
}



FString HandleAddFunctionCallNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddFunctionCallNode, AddFunctionCallNode)

	FString FunctionName, TargetClass;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	Params->TryGetStringField(TEXT("target_class"), TargetClass);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, TargetClass, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddFunctionCallNode(BP, FunctionName, NodeName, TargetClass, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add function call '%s'"), *FunctionName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add function call timed out"));
	return Future.Get();
}



FString HandleAddVariableGetNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddVariableGetNode, AddVariableGetNode)

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
		return CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddVariableGetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Get '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable get node timed out"));
	return Future.Get();
}



FString HandleAddVariableSetNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddVariableSetNode, AddVariableSetNode)

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
		return CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddVariableSetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Set '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable set node timed out"));
	return Future.Get();
}



FString HandleAddMakeStructNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddMakeStructNode, AddMakeStructNode)

	FString StructName;
	if (!Params->TryGetStringField(TEXT("struct_name"), StructName))
		return CreateErrorResponse(TEXT("Missing 'struct_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, StructName, NodeName, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddMakeStructNode(BP, StructName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add MakeStruct '%s'"), *StructName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("struct_name"), StructName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add make struct node timed out"));
	return Future.Get();
}



FString HandleAddBranchNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddBranchNode, AddBranchNode)

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddBranchNode(BP, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add branch node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add branch node timed out"));
	return Future.Get();
}



FString HandleAddCallParentFunction(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddCallParentFunction, AddCallParentFunctionNode)

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, GraphName, PosX, PosY, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UMCTBlueprintGraphBuilder::AddCallParentFunctionNode(BP, FunctionName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add call parent function node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Data->SetStringField(TEXT("node_class"), TEXT("K2Node_CallParentFunction"));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add call parent function timed out"));
	return Future.Get();
}



FString HandleEnsureFunctionGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FunctionName, EntryNodeName, ResultNodeName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	Params->TryGetStringField(TEXT("entry_node_name"), EntryNodeName);
	Params->TryGetStringField(TEXT("result_node_name"), ResultNodeName);

	TArray<FMCTBlueprintGraphPinSpec> Inputs = ParseGraphPinSpecs(Params, TEXT("inputs"));
	TArray<FMCTBlueprintGraphPinSpec> Outputs = ParseGraphPinSpecs(Params, TEXT("outputs"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, Inputs, Outputs, EntryNodeName, ResultNodeName, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath)));
			return;
		}

		UEdGraph* Graph = UMCTBlueprintGraphBuilder::EnsureFunctionGraph(
			BP,
			FunctionName,
			Inputs,
			Outputs,
			EntryNodeName,
			ResultNodeName);
		if (!Graph)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to ensure function graph '%s'"), *FunctionName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("graph_name"), Graph->GetName());
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetNumberField(TEXT("input_count"), Inputs.Num());
		Data->SetNumberField(TEXT("output_count"), Outputs.Num());
		Data->SetStringField(TEXT("entry_node_name"), EntryNodeName.IsEmpty() ? FString::Printf(TEXT("%s_Entry"), *FunctionName) : EntryNodeName);
		if (Outputs.Num() > 0)
		{
			Data->SetStringField(TEXT("result_node_name"), ResultNodeName.IsEmpty() ? FString::Printf(TEXT("%s_Result"), *FunctionName) : ResultNodeName);
		}
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Ensure function graph timed out"));
	return Future.Get();
}



FString HandleConnectPins(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromPin, ToNode, ToPin, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("from_node"), FromNode))
		return CreateErrorResponse(TEXT("Missing 'from_node' parameter"));
	if (!Params->TryGetStringField(TEXT("from_pin"), FromPin))
		return CreateErrorResponse(TEXT("Missing 'from_pin' parameter"));
	if (!Params->TryGetStringField(TEXT("to_node"), ToNode))
		return CreateErrorResponse(TEXT("Missing 'to_node' parameter"));
	if (!Params->TryGetStringField(TEXT("to_pin"), ToPin))
		return CreateErrorResponse(TEXT("Missing 'to_pin' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromPin, ToNode, ToPin, GraphName, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UMCTBlueprintGraphBuilder::ConnectPins(BP, FromNode, FromPin, ToNode, ToPin, GraphName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to connect %s.%s -> %s.%s"),
				*FromNode, *FromPin, *ToNode, *ToPin)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("from_node"), FromNode);
		Data->SetStringField(TEXT("from_pin"), FromPin);
		Data->SetStringField(TEXT("to_node"), ToNode);
		Data->SetStringField(TEXT("to_pin"), ToPin);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect pins timed out"));
	return Future.Get();
}



FString HandleSetPinDefault(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, PinName, DefaultValue, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));
	if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
		return CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
		return CreateErrorResponse(TEXT("Missing 'default_value' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, PinName, DefaultValue, GraphName, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UMCTBlueprintGraphBuilder::SetPinDefaultValue(BP, NodeName, PinName, DefaultValue, GraphName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set pin default %s.%s"),
				*NodeName, *PinName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("pin_name"), PinName);
		Data->SetStringField(TEXT("default_value"), DefaultValue);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set pin default timed out"));
	return Future.Get();
}



FString HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, GraphName, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UMCTBlueprintGraphBuilder::RemoveNode(BP, NodeName, GraphName);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Node '%s' not found"), *NodeName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("removed"), NodeName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove graph node timed out"));
	return Future.Get();
}



FString HandleGetGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	if (GraphName.IsEmpty()) GraphName = TEXT("EventGraph");

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, GraphName, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> GraphJson = UMCTBlueprintGraphBuilder::GetGraphAsJson(BP, GraphName);
		Promise->SetValue(CreateSuccessResponse(GraphJson));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get graph timed out"));
	return Future.Get();
}



FString HandleListGraphs(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TArray<FString> Graphs = UMCTBlueprintGraphBuilder::ListGraphs(BP);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> GraphArray;
		for (const FString& Name : Graphs)
		{
			GraphArray.Add(MakeShared<FJsonValueString>(Name));
		}
		Data->SetArrayField(TEXT("graphs"), GraphArray);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List graphs timed out"));
	return Future.Get();
}



FString HandleAddVariable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName, VarType, Category;
	bool bInstanceEditable = false;
	bool bBlueprintReadOnly = false;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));
	if (!Params->TryGetStringField(TEXT("var_type"), VarType))
		return CreateErrorResponse(TEXT("Missing 'var_type' parameter"));
	Params->TryGetBoolField(TEXT("instance_editable"), bInstanceEditable);
	Params->TryGetBoolField(TEXT("blueprint_read_only"), bBlueprintReadOnly);
	Params->TryGetStringField(TEXT("category"), Category);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, VarType, bInstanceEditable, bBlueprintReadOnly, Category, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UMCTBlueprintGraphBuilder::AddVariable(BP, VarName, VarType, bInstanceEditable, bBlueprintReadOnly, Category);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add variable '%s'"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("var_name"), VarName);
		Data->SetStringField(TEXT("var_type"), VarType);
		Data->SetBoolField(TEXT("blueprint_read_only"), bBlueprintReadOnly);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable timed out"));
	return Future.Get();
}



FString HandleSetVariableDefault(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName, DefaultValue;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
		return CreateErrorResponse(TEXT("Missing 'default_value' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, DefaultValue, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UMCTBlueprintGraphBuilder::SetVariableDefault(BP, VarName, DefaultValue);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set default for '%s'"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("var_name"), VarName);
		Data->SetStringField(TEXT("default_value"), DefaultValue);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set variable default timed out"));
	return Future.Get();
}



FString HandleRemoveVariable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UMCTBlueprintGraphBuilder::RemoveVariable(BP, VarName);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("removed"), VarName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove variable timed out"));
	return Future.Get();
}



FString HandleGetVariables(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		UBlueprint* BP = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> VarsJson = UMCTBlueprintGraphBuilder::GetVariablesAsJson(BP);
		Promise->SetValue(CreateSuccessResponse(VarsJson));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get variables timed out"));
	return Future.Get();
}


#undef GRAPH_NODE_HANDLER_BODY

}
