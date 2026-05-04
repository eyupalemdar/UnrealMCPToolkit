// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportBlueprintComponentCommands.h"

#include "Builders/AIDataAssetBuilder.h"
#include "Builders/AIBlueprintGraphBuilder.h"
#include "CommandHandlers/AIExportCommandResponse.h"

#include "Async/Async.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"

namespace CommonAIExport::CommandHandlers::BlueprintComponent
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName)
{
	FString Value;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
		Value.TrimStartAndEndInline();
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, bool bDefault)
{
	bool bValue = bDefault;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

UClass* ResolveComponentClass(const FString& ComponentClassName, FString& OutError)
{
	FString ClassName = ComponentClassName;
	ClassName.TrimStartAndEndInline();
	if (ClassName.IsEmpty())
	{
		OutError = TEXT("Missing 'component_class' parameter");
		return nullptr;
	}

	auto ValidateClass = [&OutError, &ComponentClassName](UClass* Class) -> UClass*
	{
		if (!Class)
		{
			return nullptr;
		}
		if (!Class->IsChildOf(UActorComponent::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Class is not an ActorComponent subclass: %s"), *ComponentClassName);
			return nullptr;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			OutError = FString::Printf(TEXT("Component class is not instantiable: %s"), *Class->GetPathName());
			return nullptr;
		}
		return Class;
	};

	if (ClassName.StartsWith(TEXT("Class'")) && ClassName.EndsWith(TEXT("'")))
	{
		ClassName = ClassName.Mid(6, ClassName.Len() - 7);
	}

	if (UClass* LoadedClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, *ClassName))
	{
		return ValidateClass(LoadedClass);
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (!Candidate || !Candidate->IsChildOf(UActorComponent::StaticClass()))
		{
			continue;
		}

		const FString CandidateName = Candidate->GetName();
		if (CandidateName.Equals(ClassName, ESearchCase::IgnoreCase)
			|| CandidateName.Equals(FString::Printf(TEXT("U%s"), *ClassName), ESearchCase::IgnoreCase)
			|| Candidate->GetPathName().Equals(ClassName, ESearchCase::IgnoreCase))
		{
			return ValidateClass(Candidate);
		}
	}

	OutError = FString::Printf(TEXT("Could not resolve component class: %s"), *ComponentClassName);
	return nullptr;
}

UBlueprint* LoadActorBlueprint(const FString& AssetPath, FString& OutError)
{
	UBlueprint* Blueprint = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath);
		return nullptr;
	}
	if (!Blueprint->SimpleConstructionScript)
	{
		OutError = FString::Printf(TEXT("Blueprint has no SimpleConstructionScript: %s"), *AssetPath);
		return nullptr;
	}
	UClass* ParentClass = Blueprint->ParentClass.Get();
	if (!ParentClass || !ParentClass->IsChildOf(AActor::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Blueprint parent is not an Actor subclass: %s"), *AssetPath);
		return nullptr;
	}
	return Blueprint;
}

TSharedPtr<FJsonObject> BuildComponentNodeJson(USCS_Node* Node, USimpleConstructionScript* SCS)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Node != nullptr);
	if (!Node)
	{
		return Data;
	}

	UActorComponent* Template = Node->ComponentTemplate;
	UClass* ComponentClass = Node->ComponentClass;
	const FName VariableName = Node->GetVariableName();

	Data->SetStringField(TEXT("name"), VariableName.ToString());
	Data->SetStringField(TEXT("guid"), Node->VariableGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Data->SetStringField(TEXT("class"), ComponentClass ? ComponentClass->GetName() : TEXT(""));
	Data->SetStringField(TEXT("class_path"), ComponentClass ? ComponentClass->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("scene_component"), ComponentClass ? ComponentClass->IsChildOf(USceneComponent::StaticClass()) : false);
	Data->SetStringField(TEXT("template_name"), Template ? Template->GetName() : TEXT(""));
	Data->SetStringField(TEXT("template_path"), Template ? Template->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("root"), Node->IsRootNode());
	Data->SetStringField(TEXT("attach_socket"), Node->AttachToName.ToString());

	if (SCS)
	{
		if (USCS_Node* ParentNode = SCS->FindParentNode(Node))
		{
			Data->SetStringField(TEXT("parent"), ParentNode->GetVariableName().ToString());
		}
		else
		{
			Data->SetStringField(TEXT("parent"), TEXT(""));
		}
	}

	TArray<TSharedPtr<FJsonValue>> Children;
	for (USCS_Node* ChildNode : Node->GetChildNodes())
	{
		if (ChildNode)
		{
			Children.Add(MakeShared<FJsonValueString>(ChildNode->GetVariableName().ToString()));
		}
	}
	Data->SetArrayField(TEXT("children"), Children);
	Data->SetNumberField(TEXT("child_count"), Children.Num());

	return Data;
}

void FinishBlueprintMutation(
	UBlueprint* Blueprint,
	const bool bStructural,
	const bool bCompileBlueprint,
	const bool bSaveAsset,
	TSharedPtr<FJsonObject> Data)
{
	if (bStructural)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	bool bCompiled = false;
	if (bCompileBlueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		bCompiled = true;
	}

	bool bSaved = false;
	if (bSaveAsset)
	{
		bSaved = UAIDataAssetBuilder::SaveAsset(Blueprint);
	}

	if (Data.IsValid())
	{
		Data->SetBoolField(TEXT("compiled"), bCompiled);
		Data->SetBoolField(TEXT("saved"), bSaved);
	}
}

FString RunOnGameThread(TFunction<FString()>&& Work, const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, Work = MoveTemp(Work)]()
	{
		Promise->SetValue(Work());
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleBlueprintComponentList(TSharedPtr<FJsonObject> Params)
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

	return RunOnGameThread([AssetPath]()
	{
		FString Error;
		UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, Error);
		if (!Blueprint)
		{
			return CreateErrorResponse(Error);
		}

		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		TArray<TSharedPtr<FJsonValue>> Components;
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node)
			{
				Components.Add(MakeShared<FJsonValueObject>(BuildComponentNodeJson(Node, SCS)));
			}
		}

		TArray<TSharedPtr<FJsonValue>> Roots;
		for (USCS_Node* RootNode : SCS->GetRootNodes())
		{
			if (RootNode)
			{
				Roots.Add(MakeShared<FJsonValueString>(RootNode->GetVariableName().ToString()));
			}
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetNumberField(TEXT("count"), Components.Num());
		Data->SetArrayField(TEXT("components"), Components);
		Data->SetArrayField(TEXT("roots"), Roots);
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component list timed out"));
}

FString HandleBlueprintComponentAdd(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const FString ComponentClassName = ReadStringField(Params, TEXT("component_class"));
	const FString ParentComponentName = ReadStringField(Params, TEXT("parent_component_name"));
	const bool bCompileBlueprint = ReadBoolField(Params, TEXT("compile_blueprint"), true);
	const bool bSaveAsset = ReadBoolField(Params, TEXT("save_asset"), false);

	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (ComponentName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	return RunOnGameThread([AssetPath, ComponentName, ComponentClassName, ParentComponentName, bCompileBlueprint, bSaveAsset]()
	{
		FString Error;
		UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, Error);
		if (!Blueprint)
		{
			return CreateErrorResponse(Error);
		}

		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		const FName ComponentFName(*ComponentName);
		if (SCS->FindSCSNode(ComponentFName))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Blueprint component already exists: %s"), *ComponentName));
		}

		UClass* ComponentClass = ResolveComponentClass(ComponentClassName, Error);
		if (!ComponentClass)
		{
			return CreateErrorResponse(Error);
		}

		USCS_Node* ParentNode = nullptr;
		if (!ParentComponentName.IsEmpty())
		{
			ParentNode = SCS->FindSCSNode(FName(*ParentComponentName));
			if (!ParentNode)
			{
				return CreateErrorResponse(FString::Printf(TEXT("Parent component not found: %s"), *ParentComponentName));
			}
			if (!ComponentClass->IsChildOf(USceneComponent::StaticClass()))
			{
				return CreateErrorResponse(TEXT("Only SceneComponent subclasses can be parented under another component"));
			}
			if (!ParentNode->ComponentClass || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
			{
				return CreateErrorResponse(FString::Printf(TEXT("Parent is not a SceneComponent: %s"), *ParentComponentName));
			}
		}

		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "BlueprintComponentAdd", "AI Add Blueprint Component"));
		Blueprint->Modify();
		SCS->Modify();

		USCS_Node* NewNode = SCS->CreateNode(ComponentClass, ComponentFName);
		if (!NewNode)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to create component node: %s"), *ComponentName));
		}
		NewNode->Modify();

		if (ParentNode)
		{
			ParentNode->Modify();
			ParentNode->AddChildNode(NewNode);
		}
		else
		{
			SCS->AddNode(NewNode);
		}

		TSharedPtr<FJsonObject> Data = BuildComponentNodeJson(NewNode, SCS);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("component_class"), ComponentClass->GetPathName());
		FinishBlueprintMutation(Blueprint, true, bCompileBlueprint, bSaveAsset, Data);
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component add timed out"));
}

FString HandleBlueprintComponentRemove(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const bool bPromoteChildren = ReadBoolField(Params, TEXT("promote_children"), true);
	const bool bCompileBlueprint = ReadBoolField(Params, TEXT("compile_blueprint"), true);
	const bool bSaveAsset = ReadBoolField(Params, TEXT("save_asset"), false);

	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (ComponentName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	return RunOnGameThread([AssetPath, ComponentName, bPromoteChildren, bCompileBlueprint, bSaveAsset]()
	{
		FString Error;
		UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, Error);
		if (!Blueprint)
		{
			return CreateErrorResponse(Error);
		}

		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		USCS_Node* Node = SCS->FindSCSNode(FName(*ComponentName));
		if (!Node)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Blueprint component not found: %s"), *ComponentName));
		}

		TSharedPtr<FJsonObject> Removed = BuildComponentNodeJson(Node, SCS);
		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "BlueprintComponentRemove", "AI Remove Blueprint Component"));
		Blueprint->Modify();
		SCS->Modify();
		Node->Modify();

		if (bPromoteChildren)
		{
			SCS->RemoveNodeAndPromoteChildren(Node);
		}
		else
		{
			SCS->RemoveNode(Node);
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("removed"), ComponentName);
		Data->SetBoolField(TEXT("promote_children"), bPromoteChildren);
		Data->SetObjectField(TEXT("removed_component"), Removed);
		FinishBlueprintMutation(Blueprint, true, bCompileBlueprint, bSaveAsset, Data);
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component remove timed out"));
}

FString HandleBlueprintComponentSetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const FString PropertyPath = ReadStringField(Params, TEXT("property_path"));
	const FString Value = ReadStringField(Params, TEXT("value"));
	const bool bCompileBlueprint = ReadBoolField(Params, TEXT("compile_blueprint"), false);
	const bool bSaveAsset = ReadBoolField(Params, TEXT("save_asset"), false);

	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (ComponentName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}
	if (PropertyPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'property_path' parameter"));
	}

	return RunOnGameThread([AssetPath, ComponentName, PropertyPath, Value, bCompileBlueprint, bSaveAsset]()
	{
		FString Error;
		UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, Error);
		if (!Blueprint)
		{
			return CreateErrorResponse(Error);
		}

		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		USCS_Node* Node = SCS->FindSCSNode(FName(*ComponentName));
		if (!Node)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Blueprint component not found: %s"), *ComponentName));
		}

		UActorComponent* Template = Node->ComponentTemplate;
		if (!Template)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Blueprint component has no template: %s"), *ComponentName));
		}

		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "BlueprintComponentSetProperty", "AI Set Blueprint Component Property"));
		Blueprint->Modify();
		SCS->Modify();
		Node->Modify();
		Template->Modify();

		if (!UAIDataAssetBuilder::SetProperty(Template, PropertyPath, Value))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Failed to set component property '%s' on '%s'"), *PropertyPath, *ComponentName));
		}

		TSharedPtr<FJsonObject> Data = BuildComponentNodeJson(Node, SCS);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("component_name"), ComponentName);
		Data->SetStringField(TEXT("property_path"), PropertyPath);
		Data->SetStringField(TEXT("value"), Value);
		FinishBlueprintMutation(Blueprint, false, bCompileBlueprint, bSaveAsset, Data);
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component set property timed out"));
}
}
