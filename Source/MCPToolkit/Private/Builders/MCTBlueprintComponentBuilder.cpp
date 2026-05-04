// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/MCTBlueprintComponentBuilder.h"

#include "Builders/MCTBlueprintGraphBuilder.h"
#include "Builders/MCTDataAssetBuilder.h"

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

UBlueprint* UMCTBlueprintComponentBuilder::LoadActorBlueprint(const FString& AssetPath, FString& OutError)
{
	UBlueprint* Blueprint = UMCTBlueprintGraphBuilder::LoadBlueprint(AssetPath);
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

UClass* UMCTBlueprintComponentBuilder::ResolveComponentClass(const FString& ComponentClassName, FString& OutError)
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

TSharedPtr<FJsonObject> UMCTBlueprintComponentBuilder::BuildComponentNodeJson(USCS_Node* Node, USimpleConstructionScript* SCS)
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

TSharedPtr<FJsonObject> UMCTBlueprintComponentBuilder::ListComponents(const FString& AssetPath, FString& OutError)
{
	UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, OutError);
	if (!Blueprint)
	{
		return nullptr;
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
	return Data;
}

TSharedPtr<FJsonObject> UMCTBlueprintComponentBuilder::AddComponent(
	const FString& AssetPath,
	const FString& ComponentName,
	const FString& ComponentClassName,
	const FString& ParentComponentName,
	const bool bCompileBlueprint,
	const bool bSaveAsset,
	FString& OutError)
{
	UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, OutError);
	if (!Blueprint)
	{
		return nullptr;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	const FName ComponentFName(*ComponentName);
	if (SCS->FindSCSNode(ComponentFName))
	{
		OutError = FString::Printf(TEXT("Blueprint component already exists: %s"), *ComponentName);
		return nullptr;
	}

	UClass* ComponentClass = ResolveComponentClass(ComponentClassName, OutError);
	if (!ComponentClass)
	{
		return nullptr;
	}

	USCS_Node* ParentNode = nullptr;
	if (!ParentComponentName.IsEmpty())
	{
		ParentNode = SCS->FindSCSNode(FName(*ParentComponentName));
		if (!ParentNode)
		{
			OutError = FString::Printf(TEXT("Parent component not found: %s"), *ParentComponentName);
			return nullptr;
		}
		if (!ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			OutError = TEXT("Only SceneComponent subclasses can be parented under another component");
			return nullptr;
		}
		if (!ParentNode->ComponentClass || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			OutError = FString::Printf(TEXT("Parent is not a SceneComponent: %s"), *ParentComponentName);
			return nullptr;
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MCPToolkit", "BlueprintComponentAdd", "AI Add Blueprint Component"));
	Blueprint->Modify();
	SCS->Modify();

	USCS_Node* NewNode = SCS->CreateNode(ComponentClass, ComponentFName);
	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("Failed to create component node: %s"), *ComponentName);
		return nullptr;
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
	return Data;
}

TSharedPtr<FJsonObject> UMCTBlueprintComponentBuilder::RemoveComponent(
	const FString& AssetPath,
	const FString& ComponentName,
	const bool bPromoteChildren,
	const bool bCompileBlueprint,
	const bool bSaveAsset,
	FString& OutError)
{
	UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, OutError);
	if (!Blueprint)
	{
		return nullptr;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	USCS_Node* Node = SCS->FindSCSNode(FName(*ComponentName));
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Blueprint component not found: %s"), *ComponentName);
		return nullptr;
	}

	TSharedPtr<FJsonObject> Removed = BuildComponentNodeJson(Node, SCS);
	const FScopedTransaction Transaction(NSLOCTEXT("MCPToolkit", "BlueprintComponentRemove", "AI Remove Blueprint Component"));
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
	return Data;
}

TSharedPtr<FJsonObject> UMCTBlueprintComponentBuilder::SetComponentProperty(
	const FString& AssetPath,
	const FString& ComponentName,
	const FString& PropertyPath,
	const FString& Value,
	const bool bCompileBlueprint,
	const bool bSaveAsset,
	FString& OutError)
{
	UBlueprint* Blueprint = LoadActorBlueprint(AssetPath, OutError);
	if (!Blueprint)
	{
		return nullptr;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	USCS_Node* Node = SCS->FindSCSNode(FName(*ComponentName));
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Blueprint component not found: %s"), *ComponentName);
		return nullptr;
	}

	UActorComponent* Template = Node->ComponentTemplate;
	if (!Template)
	{
		OutError = FString::Printf(TEXT("Blueprint component has no template: %s"), *ComponentName);
		return nullptr;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("MCPToolkit", "BlueprintComponentSetProperty", "AI Set Blueprint Component Property"));
	Blueprint->Modify();
	SCS->Modify();
	Node->Modify();
	Template->Modify();

	if (!UMCTDataAssetBuilder::SetProperty(Template, PropertyPath, Value))
	{
		OutError = FString::Printf(TEXT("Failed to set component property '%s' on '%s'"), *PropertyPath, *ComponentName);
		return nullptr;
	}

	TSharedPtr<FJsonObject> Data = BuildComponentNodeJson(Node, SCS);
	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("component_name"), ComponentName);
	Data->SetStringField(TEXT("property_path"), PropertyPath);
	Data->SetStringField(TEXT("value"), Value);
	FinishBlueprintMutation(Blueprint, false, bCompileBlueprint, bSaveAsset, Data);
	return Data;
}

void UMCTBlueprintComponentBuilder::FinishBlueprintMutation(
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
		bSaved = UMCTDataAssetBuilder::SaveAsset(Blueprint);
	}

	if (Data.IsValid())
	{
		Data->SetBoolField(TEXT("compiled"), bCompiled);
		Data->SetBoolField(TEXT("saved"), bSaved);
	}
}
