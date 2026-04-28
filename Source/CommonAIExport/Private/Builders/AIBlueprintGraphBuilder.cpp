// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/AIBlueprintGraphBuilder.h"

#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"

#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "AIBlueprintGraphBuilder"

DEFINE_LOG_CATEGORY_STATIC(LogAIGraphBuilder, Log, All);

// =============================================================================
// UTILITY
// =============================================================================

UBlueprint* UAIBlueprintGraphBuilder::LoadBlueprint(const FString& AssetPath)
{
	UObject* Asset = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
	return Cast<UBlueprint>(Asset);
}

UEdGraph* UAIBlueprintGraphBuilder::GetEventGraph(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Try UbergraphPages first (where event graph lives)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetFName() == UEdGraphSchema_K2::GN_EventGraph)
		{
			return Graph;
		}
	}

	// Return first ubergraph page if any exist
	if (Blueprint->UbergraphPages.Num() > 0)
	{
		return Blueprint->UbergraphPages[0];
	}

	// Create one if needed
	UEdGraph* EventGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint, UEdGraphSchema_K2::GN_EventGraph,
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	if (EventGraph)
	{
		Blueprint->UbergraphPages.Add(EventGraph);
	}
	return EventGraph;
}

UEdGraph* UAIBlueprintGraphBuilder::FindGraph(
	UBlueprint* Blueprint,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	if (GraphName.IsEmpty() || GraphName == UEdGraphSchema_K2::GN_EventGraph.ToString())
	{
		return GetEventGraph(Blueprint);
	}

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}

	return nullptr;
}

UEdGraph* UAIBlueprintGraphBuilder::ResolveGraph(
	UBlueprint* Blueprint,
	const FString& GraphName)
{
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("ResolveGraph: Graph '%s' not found"),
			GraphName.IsEmpty() ? TEXT("EventGraph") : *GraphName);
	}
	return Graph;
}

UK2Node* UAIBlueprintGraphBuilder::FindNodeByName(
	UBlueprint* Blueprint,
	const FString& NodeName,
	const FString& GraphName)
{
	if (!Blueprint || NodeName.IsEmpty())
	{
		return nullptr;
	}

	FString SearchComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	auto SearchGraph = [&SearchComment](UEdGraph* Graph) -> UK2Node*
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node* K2Node = Cast<UK2Node>(Node);
			if (K2Node && K2Node->NodeComment == SearchComment)
			{
				return K2Node;
			}
		}
		return nullptr;
	};

	if (!GraphName.IsEmpty())
	{
		// Search specific graph
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return SearchGraph(Graph);
			}
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				return SearchGraph(Graph);
			}
		}
	}
	else
	{
		// Search all graphs
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			UK2Node* Found = SearchGraph(Graph);
			if (Found) return Found;
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			UK2Node* Found = SearchGraph(Graph);
			if (Found) return Found;
		}
	}

	return nullptr;
}

UEdGraphPin* UAIBlueprintGraphBuilder::FindPin(
	UK2Node* Node,
	const FString& PinName,
	EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	// 1. Exact name match
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction == Direction &&
			Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}

	// 2. Display name match
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction == Direction)
		{
			FText DisplayName = Pin->GetDisplayName();
			if (DisplayName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
	}

	// 3. Common aliases
	if (PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase))
	{
		// Find the exec pin
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
	}

	// 4. For "self" / "target", find the self pin
	if (PinName.Equals(TEXT("self"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("target"), ESearchCase::IgnoreCase))
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Direction &&
				Pin->PinName == UEdGraphSchema_K2::PN_Self)
			{
				return Pin;
			}
		}
	}

	// 5. Partial match (starts with)
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin->Direction == Direction &&
			Pin->PinName.ToString().StartsWith(PinName, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}

	UE_LOG(LogAIGraphBuilder, Warning, TEXT("FindPin: Pin '%s' (direction=%d) not found on node '%s'. Available pins:"),
		*PinName, (int32)Direction, *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	for (UEdGraphPin* Pin : Node->Pins)
	{
		UE_LOG(LogAIGraphBuilder, Warning, TEXT("  - '%s' (direction=%d, category=%s)"),
			*Pin->PinName.ToString(), (int32)Pin->Direction, *Pin->PinType.PinCategory.ToString());
	}

	return nullptr;
}

UFunction* UAIBlueprintGraphBuilder::FindFunctionByName(
	UBlueprint* Blueprint,
	const FString& FunctionName,
	const FString& TargetClassPath)
{
	FName FuncName(*FunctionName);

	// If explicit target class given, search there first
	if (!TargetClassPath.IsEmpty())
	{
		UClass* TargetClass = FindObject<UClass>(nullptr, *TargetClassPath);
		if (!TargetClass)
		{
			TargetClass = LoadObject<UClass>(nullptr, *TargetClassPath);
		}
		if (TargetClass)
		{
			UFunction* Func = TargetClass->FindFunctionByName(FuncName);
			if (Func)
			{
				return Func;
			}
		}
	}

	// Search Blueprint-generated classes for user-defined functions that may
	// already be compiled into the BP skeleton/generated class.
	if (Blueprint)
	{
		if (Blueprint->SkeletonGeneratedClass)
		{
			if (UFunction* Func = Blueprint->SkeletonGeneratedClass->FindFunctionByName(FuncName))
			{
				return Func;
			}
		}
		if (Blueprint->GeneratedClass)
		{
			if (UFunction* Func = Blueprint->GeneratedClass->FindFunctionByName(FuncName))
			{
				return Func;
			}
		}
	}

	// Search Blueprint parent class hierarchy
	if (Blueprint && Blueprint->ParentClass)
	{
		for (UClass* Class = Blueprint->ParentClass; Class; Class = Class->GetSuperClass())
		{
			UFunction* Func = Class->FindFunctionByName(FuncName);
			if (Func)
			{
				return Func;
			}
		}
	}

	// Search common UMG / CommonUI classes
	static const TCHAR* CommonClassPaths[] = {
		TEXT("/Script/UMG.Widget"),
		TEXT("/Script/UMG.UserWidget"),
		TEXT("/Script/UMG.TextBlock"),
		TEXT("/Script/UMG.Image"),
		TEXT("/Script/UMG.PanelWidget"),
		TEXT("/Script/UMG.ContentWidget"),
		TEXT("/Script/UMG.HorizontalBox"),
		TEXT("/Script/UMG.VerticalBox"),
		TEXT("/Script/UMG.CanvasPanel"),
		TEXT("/Script/UMG.Border"),
		TEXT("/Script/UMG.Overlay"),
		TEXT("/Script/UMG.WidgetSwitcher"),
		TEXT("/Script/CommonUI.CommonTextBlock"),
		TEXT("/Script/CommonUI.CommonButtonBase"),
		TEXT("/Script/CommonUI.CommonActionWidget"),
		TEXT("/Script/CommonUI.CommonActivatableWidget"),
		TEXT("/Script/CommonUI.CommonTabListWidgetBase"),
		TEXT("/Script/CommonGame.CommonUIExtensions"),
		TEXT("/Script/SlateCore.SlateBlueprintLibrary"),
		TEXT("/Script/Engine.KismetMathLibrary"),
		TEXT("/Script/Engine.KismetSystemLibrary"),
		TEXT("/Script/Engine.KismetTextLibrary"),
	};

	for (const TCHAR* ClassPath : CommonClassPaths)
	{
		UClass* Class = LoadObject<UClass>(nullptr, ClassPath);
		if (Class)
		{
			UFunction* Func = Class->FindFunctionByName(FuncName);
			if (Func)
			{
				return Func;
			}
		}
	}

	UE_LOG(LogAIGraphBuilder, Warning, TEXT("FindFunctionByName: Could not find function '%s'"), *FunctionName);
	return nullptr;
}

namespace
{
FString CleanGraphTypeString(const FString& InType)
{
	FString Type = InType;
	Type.TrimStartAndEndInline();

	if (Type.Len() >= 2
		&& ((Type.StartsWith(TEXT("\"")) && Type.EndsWith(TEXT("\"")))
			|| (Type.StartsWith(TEXT("'")) && Type.EndsWith(TEXT("'")))))
	{
		Type = Type.Mid(1, Type.Len() - 2);
		Type.TrimStartAndEndInline();
	}

	return Type;
}

bool ConsumeTypePrefix(FString& Type, const FString& Prefix)
{
	if (Type.StartsWith(Prefix, ESearchCase::IgnoreCase))
	{
		Type = Type.Mid(Prefix.Len());
		Type.TrimStartAndEndInline();
		return true;
	}
	return false;
}

bool ConsumeWrappedType(FString& Type, const FString& Prefix)
{
	if (Type.StartsWith(Prefix, ESearchCase::IgnoreCase) && Type.EndsWith(TEXT(">")))
	{
		Type = Type.Mid(Prefix.Len(), Type.Len() - Prefix.Len() - 1);
		Type.TrimStartAndEndInline();
		return true;
	}
	return false;
}

UClass* ResolveClassForGraphType(const FString& InType)
{
	FString Type = CleanGraphTypeString(InType);
	if (Type.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* Class = FindObject<UClass>(nullptr, *Type))
	{
		return Class;
	}
	if (UClass* Class = LoadObject<UClass>(nullptr, *Type))
	{
		return Class;
	}

	if (Type.StartsWith(TEXT("/Script/")))
	{
		const FString ObjectPath = Type + TEXT(".") + Type.RightChop(Type.Find(TEXT(".")) + 1);
		if (UClass* Class = LoadObject<UClass>(nullptr, *ObjectPath))
		{
			return Class;
		}
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class)
		{
			continue;
		}
		const FString ClassName = Class->GetName();
		if (ClassName.Equals(Type, ESearchCase::IgnoreCase)
			|| Class->GetPathName().Equals(Type, ESearchCase::IgnoreCase)
			|| (ClassName.StartsWith(TEXT("U")) && ClassName.Mid(1).Equals(Type, ESearchCase::IgnoreCase)))
		{
			return Class;
		}
	}

	return nullptr;
}

UEnum* ResolveEnumForGraphType(const FString& InType)
{
	FString Type = CleanGraphTypeString(InType);
	if (Type.IsEmpty())
	{
		return nullptr;
	}

	if (UEnum* Enum = FindObject<UEnum>(nullptr, *Type))
	{
		return Enum;
	}
	if (UEnum* Enum = LoadObject<UEnum>(nullptr, *Type))
	{
		return Enum;
	}

	static const TCHAR* EnumPrefixes[] = {
		TEXT("/Script/CoreUObject."),
		TEXT("/Script/Core."),
		TEXT("/Script/Slate."),
		TEXT("/Script/SlateCore."),
		TEXT("/Script/UMG."),
		TEXT("/Script/CommonInput."),
		TEXT("/Script/CommonUI."),
	};

	for (const TCHAR* Prefix : EnumPrefixes)
	{
		const FString FullPath = FString(Prefix) + Type;
		if (UEnum* Enum = FindObject<UEnum>(nullptr, *FullPath))
		{
			return Enum;
		}
		if (UEnum* Enum = LoadObject<UEnum>(nullptr, *FullPath))
		{
			return Enum;
		}
	}

	for (TObjectIterator<UEnum> It; It; ++It)
	{
		UEnum* Enum = *It;
		if (!Enum)
		{
			continue;
		}
		const FString EnumName = Enum->GetName();
		if (EnumName.Equals(Type, ESearchCase::IgnoreCase)
			|| Enum->GetPathName().Equals(Type, ESearchCase::IgnoreCase))
		{
			return Enum;
		}
	}

	return nullptr;
}
}

FEdGraphPinType UAIBlueprintGraphBuilder::ResolveVarType(const FString& TypeString)
{
	FEdGraphPinType PinType;
	FString Type = CleanGraphTypeString(TypeString);

	if (Type.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Type.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (Type.Equals(TEXT("int"), ESearchCase::IgnoreCase) ||
			 Type.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Type.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (Type.Equals(TEXT("float"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = TEXT("float");
	}
	else if (Type.Equals(TEXT("double"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = TEXT("double");
	}
	else if (Type.Equals(TEXT("String"), ESearchCase::IgnoreCase) ||
			 Type.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Type.Equals(TEXT("Name"), ESearchCase::IgnoreCase) ||
			 Type.Equals(TEXT("FName"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Type.Equals(TEXT("Text"), ESearchCase::IgnoreCase) ||
			 Type.Equals(TEXT("FText"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (ConsumeTypePrefix(Type, TEXT("class:"))
		|| ConsumeTypePrefix(Type, TEXT("class "))
		|| ConsumeWrappedType(Type, TEXT("Class<"))
		|| ConsumeWrappedType(Type, TEXT("TSubclassOf<")))
	{
		if (UClass* Class = ResolveClassForGraphType(Type))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			PinType.PinSubCategoryObject = Class;
		}
		else
		{
			UE_LOG(LogAIGraphBuilder, Warning, TEXT("ResolveVarType: Could not load class reference '%s'"), *TypeString);
			PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			PinType.PinSubCategoryObject = UObject::StaticClass();
		}
	}
	else if (ConsumeTypePrefix(Type, TEXT("enum:"))
		|| ConsumeTypePrefix(Type, TEXT("enum "))
		|| ConsumeWrappedType(Type, TEXT("Enum<")))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		if (UEnum* Enum = ResolveEnumForGraphType(Type))
		{
			PinType.PinSubCategoryObject = Enum;
		}
		else
		{
			UE_LOG(LogAIGraphBuilder, Warning, TEXT("ResolveVarType: Could not load enum '%s'"), *TypeString);
		}
	}
	else if (ConsumeTypePrefix(Type, TEXT("object:"))
		|| ConsumeTypePrefix(Type, TEXT("object ")))
	{
		if (UClass* Class = ResolveClassForGraphType(Type))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = Class;
		}
		else
		{
			UE_LOG(LogAIGraphBuilder, Warning, TEXT("ResolveVarType: Could not load object class '%s'"), *TypeString);
		}
	}
	else if (Type.StartsWith(TEXT("/")))
	{
		// Backward-compatible object reference path: "/Script/UMG.TextBlock"
		UClass* Class = ResolveClassForGraphType(Type);
		if (Class)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = Class;
		}
		else
		{
			UE_LOG(LogAIGraphBuilder, Warning, TEXT("ResolveVarType: Could not load class '%s'"), *Type);
		}
	}
	else
	{
		// Try as a struct name: "Vector", "Rotator", "LinearColor", "Transform", "SlateFontInfo"
		UScriptStruct* Struct = nullptr;

		// Common struct lookup paths
		static const TCHAR* StructPrefixes[] = {
			TEXT("/Script/CoreUObject."),
			TEXT("/Script/Core."),
			TEXT("/Script/SlateCore."),
			TEXT("/Script/UMG."),
		};

		for (const TCHAR* Prefix : StructPrefixes)
		{
			FString FullPath = FString(Prefix) + Type;
			Struct = FindObject<UScriptStruct>(nullptr, *FullPath);
			if (Struct) break;
			Struct = LoadObject<UScriptStruct>(nullptr, *FullPath);
			if (Struct) break;
		}

		if (Struct)
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = Struct;
		}
		else
		{
			if (UEnum* Enum = ResolveEnumForGraphType(Type))
			{
				PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				PinType.PinSubCategoryObject = Enum;
			}
			else
			{
				UE_LOG(LogAIGraphBuilder, Warning, TEXT("ResolveVarType: Unknown type '%s', defaulting to String"), *TypeString);
				PinType.PinCategory = UEdGraphSchema_K2::PC_String;
			}
		}
	}

	return PinType;
}

// =============================================================================
// NODE CREATION
// =============================================================================

UK2Node* UAIBlueprintGraphBuilder::AddEventNode(
	UBlueprint* Blueprint,
	const FString& EventName,
	const FString& NodeName,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("AddEventNode: Blueprint is null"));
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("AddEventNode: Could not get event graph"));
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddEventNode", "AI: Add Event Node"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	// Check if this event override already exists
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_Event* ExistingEvent = Cast<UK2Node_Event>(Node);
		if (ExistingEvent && ExistingEvent->EventReference.GetMemberName() == FName(*EventName))
		{
			ExistingEvent->NodeComment = AIComment;
			ExistingEvent->bCommentBubbleVisible = false;
			UE_LOG(LogAIGraphBuilder, Log, TEXT("AddEventNode: Found existing event '%s', tagged as '%s'"),
				*EventName, *NodeName);
			return ExistingEvent;
		}
	}

	// Find the function in parent class
	UFunction* Function = nullptr;
	if (Blueprint->ParentClass)
	{
		Function = Blueprint->ParentClass->FindFunctionByName(FName(*EventName));
	}
	if (!Function)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("AddEventNode: Function '%s' not found in parent class '%s'"),
			*EventName, Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("null"));
		return nullptr;
	}

	// Create event node
	UK2Node_Event* EventNode = NewObject<UK2Node_Event>(EventGraph);
	EventNode->EventReference.SetFromField<UFunction>(Function, false);
	EventNode->bOverrideFunction = true;
	EventNode->NodePosX = PosX;
	EventNode->NodePosY = PosY;
	EventNode->NodeComment = AIComment;
	EventNode->bCommentBubbleVisible = false;

	EventGraph->AddNode(EventNode, false, false);
	EventNode->CreateNewGuid();
	EventNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddEventNode: Created event override '%s' as '%s'"),
		*EventName, *NodeName);
	return EventNode;
}

UK2Node* UAIBlueprintGraphBuilder::AddCustomEvent(
	UBlueprint* Blueprint,
	const FString& EventName,
	const FString& NodeName,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddCustomEvent", "AI: Add Custom Event"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	UK2Node_CustomEvent* CustomEvent = NewObject<UK2Node_CustomEvent>(EventGraph);
	CustomEvent->CustomFunctionName = FName(*EventName);
	CustomEvent->NodePosX = PosX;
	CustomEvent->NodePosY = PosY;
	CustomEvent->NodeComment = AIComment;
	CustomEvent->bCommentBubbleVisible = false;

	EventGraph->AddNode(CustomEvent, false, false);
	CustomEvent->CreateNewGuid();
	CustomEvent->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddCustomEvent: Created custom event '%s' as '%s'"),
		*EventName, *NodeName);
	return CustomEvent;
}

UK2Node* UAIBlueprintGraphBuilder::AddFunctionCallNode(
	UBlueprint* Blueprint,
	const FString& FunctionName,
	const FString& NodeName,
	const FString& TargetClass,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		return nullptr;
	}

	UFunction* Function = FindFunctionByName(Blueprint, FunctionName, TargetClass);
	const bool bSelfFunctionGraph = !Function && FindGraph(Blueprint, FunctionName) != nullptr;
	if (!Function && !bSelfFunctionGraph)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("AddFunctionCallNode: Function '%s' not found"), *FunctionName);
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddFunctionCallNode", "AI: Add Function Call Node"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(EventGraph);
	if (Function)
	{
		CallNode->SetFromFunction(Function);
	}
	else
	{
		CallNode->FunctionReference.SetSelfMember(FName(*FunctionName));
	}
	CallNode->NodePosX = PosX;
	CallNode->NodePosY = PosY;
	CallNode->NodeComment = AIComment;
	CallNode->bCommentBubbleVisible = false;

	EventGraph->AddNode(CallNode, false, false);
	CallNode->CreateNewGuid();
	CallNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddFunctionCallNode: Created call to '%s' as '%s'"),
		*FunctionName, *NodeName);
	return CallNode;
}

UK2Node* UAIBlueprintGraphBuilder::AddVariableGetNode(
	UBlueprint* Blueprint,
	const FString& VariableName,
	const FString& NodeName,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddVariableGetNode", "AI: Add Variable Get Node"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(EventGraph);
	GetNode->VariableReference.SetSelfMember(FName(*VariableName));
	GetNode->NodePosX = PosX;
	GetNode->NodePosY = PosY;
	GetNode->NodeComment = AIComment;
	GetNode->bCommentBubbleVisible = false;

	EventGraph->AddNode(GetNode, false, false);
	GetNode->CreateNewGuid();
	GetNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddVariableGetNode: Created Get '%s' as '%s'"),
		*VariableName, *NodeName);
	return GetNode;
}

UK2Node* UAIBlueprintGraphBuilder::AddVariableSetNode(
	UBlueprint* Blueprint,
	const FString& VariableName,
	const FString& NodeName,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddVariableSetNode", "AI: Add Variable Set Node"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(EventGraph);
	SetNode->VariableReference.SetSelfMember(FName(*VariableName));
	SetNode->NodePosX = PosX;
	SetNode->NodePosY = PosY;
	SetNode->NodeComment = AIComment;
	SetNode->bCommentBubbleVisible = false;

	EventGraph->AddNode(SetNode, false, false);
	SetNode->CreateNewGuid();
	SetNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddVariableSetNode: Created Set '%s' as '%s'"),
		*VariableName, *NodeName);
	return SetNode;
}

UK2Node* UAIBlueprintGraphBuilder::AddMakeStructNode(
	UBlueprint* Blueprint,
	const FString& StructName,
	const FString& NodeName,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		return nullptr;
	}

	// Resolve struct type
	FEdGraphPinType PinType = ResolveVarType(StructName);
	UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get());
	if (!Struct)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("AddMakeStructNode: Could not resolve struct '%s'"), *StructName);
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddMakeStructNode", "AI: Add Make Struct Node"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	UK2Node_MakeStruct* MakeNode = NewObject<UK2Node_MakeStruct>(EventGraph);
	MakeNode->StructType = Struct;
	MakeNode->NodePosX = PosX;
	MakeNode->NodePosY = PosY;
	MakeNode->NodeComment = AIComment;
	MakeNode->bCommentBubbleVisible = false;

	EventGraph->AddNode(MakeNode, false, false);
	MakeNode->CreateNewGuid();
	MakeNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddMakeStructNode: Created MakeStruct '%s' as '%s'"),
		*StructName, *NodeName);
	return MakeNode;
}

UK2Node* UAIBlueprintGraphBuilder::AddBranchNode(
	UBlueprint* Blueprint,
	const FString& NodeName,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddBranchNode", "AI: Add Branch Node"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(EventGraph);
	BranchNode->NodePosX = PosX;
	BranchNode->NodePosY = PosY;
	BranchNode->NodeComment = AIComment;
	BranchNode->bCommentBubbleVisible = false;

	EventGraph->AddNode(BranchNode, false, false);
	BranchNode->CreateNewGuid();
	BranchNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddBranchNode: Created branch as '%s'"), *NodeName);
	return BranchNode;
}

UK2Node* UAIBlueprintGraphBuilder::AddCallParentFunctionNode(
	UBlueprint* Blueprint,
	const FString& FunctionName,
	const FString& NodeName,
	int32 PosX, int32 PosY,
	const FString& GraphName)
{
	if (!Blueprint || FunctionName.IsEmpty())
	{
		return nullptr;
	}

	UEdGraph* EventGraph = ResolveGraph(Blueprint, GraphName);
	if (!EventGraph)
	{
		return nullptr;
	}

	// Find the function in parent class
	UFunction* Function = nullptr;
	if (Blueprint->ParentClass)
	{
		Function = Blueprint->ParentClass->FindFunctionByName(FName(*FunctionName));
	}

	if (!Function)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("AddCallParentFunctionNode: Function '%s' not found in parent class"), *FunctionName);
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddCallParentFunctionNode", "AI: Add Call Parent Function Node"));

	FString AIComment = FString::Printf(TEXT("AI:%s"), *NodeName);

	UK2Node_CallParentFunction* ParentCallNode = NewObject<UK2Node_CallParentFunction>(EventGraph);
	ParentCallNode->SetFromFunction(Function);
	ParentCallNode->NodePosX = PosX;
	ParentCallNode->NodePosY = PosY;
	ParentCallNode->NodeComment = AIComment;
	ParentCallNode->bCommentBubbleVisible = false;

	EventGraph->AddNode(ParentCallNode, false, false);
	ParentCallNode->CreateNewGuid();
	ParentCallNode->AllocateDefaultPins();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddCallParentFunctionNode: Created parent call to '%s' as '%s'"),
		*FunctionName, *NodeName);
	return ParentCallNode;
}

// =============================================================================
// WIRING
// =============================================================================

bool UAIBlueprintGraphBuilder::ConnectPins(
	UBlueprint* Blueprint,
	const FString& FromNodeName,
	const FString& FromPinName,
	const FString& ToNodeName,
	const FString& ToPinName,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return false;
	}

	UK2Node* FromNode = FindNodeByName(Blueprint, FromNodeName, GraphName);
	UK2Node* ToNode = FindNodeByName(Blueprint, ToNodeName, GraphName);

	if (!FromNode)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("ConnectPins: Source node '%s' not found"), *FromNodeName);
		return false;
	}
	if (!ToNode)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("ConnectPins: Target node '%s' not found"), *ToNodeName);
		return false;
	}

	UEdGraphPin* FromPin = FindPin(FromNode, FromPinName, EGPD_Output);
	UEdGraphPin* ToPin = FindPin(ToNode, ToPinName, EGPD_Input);

	if (!FromPin)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("ConnectPins: Output pin '%s' not found on node '%s'"),
			*FromPinName, *FromNodeName);
		return false;
	}
	if (!ToPin)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("ConnectPins: Input pin '%s' not found on node '%s'"),
			*ToPinName, *ToNodeName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIConnectPins", "AI: Connect Pins"));

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	bool bConnected = Schema->TryCreateConnection(FromPin, ToPin);

	if (bConnected)
	{
		UE_LOG(LogAIGraphBuilder, Log, TEXT("ConnectPins: Connected %s.%s -> %s.%s"),
			*FromNodeName, *FromPinName, *ToNodeName, *ToPinName);
	}
	else
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("ConnectPins: Failed to connect %s.%s -> %s.%s"),
			*FromNodeName, *FromPinName, *ToNodeName, *ToPinName);
	}

	return bConnected;
}

bool UAIBlueprintGraphBuilder::SetPinDefaultValue(
	UBlueprint* Blueprint,
	const FString& NodeName,
	const FString& PinName,
	const FString& DefaultValue,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return false;
	}

	UK2Node* Node = FindNodeByName(Blueprint, NodeName, GraphName);
	if (!Node)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("SetPinDefaultValue: Node '%s' not found"), *NodeName);
		return false;
	}

	UEdGraphPin* Pin = FindPin(Node, PinName, EGPD_Input);
	if (!Pin)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("SetPinDefaultValue: Pin '%s' not found on node '%s'"),
			*PinName, *NodeName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetPinDefaultValue", "AI: Set Pin Default Value"));

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->TrySetDefaultValue(*Pin, DefaultValue);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("SetPinDefaultValue: Set %s.%s = '%s'"),
		*NodeName, *PinName, *DefaultValue);
	return true;
}

// =============================================================================
// NODE MANAGEMENT
// =============================================================================

bool UAIBlueprintGraphBuilder::RemoveNode(
	UBlueprint* Blueprint,
	const FString& NodeName,
	const FString& GraphName)
{
	if (!Blueprint)
	{
		return false;
	}

	UK2Node* Node = FindNodeByName(Blueprint, NodeName, GraphName);
	if (!Node)
	{
		UE_LOG(LogAIGraphBuilder, Warning, TEXT("RemoveNode: Node '%s' not found"), *NodeName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIRemoveNode", "AI: Remove Node"));

	FBlueprintEditorUtils::RemoveNode(Blueprint, Node);
	UE_LOG(LogAIGraphBuilder, Log, TEXT("RemoveNode: Removed '%s'"), *NodeName);
	return true;
}

UEdGraph* UAIBlueprintGraphBuilder::EnsureFunctionGraph(
	UBlueprint* Blueprint,
	const FString& FunctionName,
	const TArray<FAIBlueprintGraphPinSpec>& Inputs,
	const TArray<FAIBlueprintGraphPinSpec>& Outputs,
	const FString& EntryNodeName,
	const FString& ResultNodeName)
{
	if (!Blueprint || FunctionName.IsEmpty())
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIEnsureFunctionGraph", "AI: Ensure Function Graph"));

	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		Blueprint->Modify();
		FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			FName(*FunctionName),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());

		if (!FunctionGraph)
		{
			UE_LOG(LogAIGraphBuilder, Error, TEXT("EnsureFunctionGraph: Failed to create '%s'"), *FunctionName);
			return nullptr;
		}

		FBlueprintEditorUtils::AddFunctionGraph<UFunction>(
			Blueprint,
			FunctionGraph,
			/*bIsUserCreated=*/true,
			/*SignatureFromObject=*/nullptr);
	}

	UK2Node_FunctionEntry* EntryNode = nullptr;
	UK2Node_FunctionResult* ResultNode = nullptr;
	for (UEdGraphNode* GraphNode : FunctionGraph->Nodes)
	{
		if (!EntryNode)
		{
			EntryNode = Cast<UK2Node_FunctionEntry>(GraphNode);
		}
		if (!ResultNode)
		{
			ResultNode = Cast<UK2Node_FunctionResult>(GraphNode);
		}
	}

	if (EntryNode)
	{
		const FString EntryAIName = EntryNodeName.IsEmpty()
			? FString::Printf(TEXT("%s_Entry"), *FunctionName)
			: EntryNodeName;
		EntryNode->NodeComment = FString::Printf(TEXT("AI:%s"), *EntryAIName);
		EntryNode->bCommentBubbleVisible = false;
		EntryNode->CustomGeneratedFunctionName = FName(*FunctionName);

		for (const FAIBlueprintGraphPinSpec& PinSpec : Inputs)
		{
			if (PinSpec.Name.IsEmpty())
			{
				continue;
			}
			if (!FindPin(EntryNode, PinSpec.Name, EGPD_Output))
			{
				UEdGraphPin* Pin = EntryNode->CreateUserDefinedPin(
					FName(*PinSpec.Name),
					ResolveVarType(PinSpec.Type),
					EGPD_Output,
					/*bUseUniqueName=*/false);
				if (Pin && !PinSpec.DefaultValue.IsEmpty())
				{
					Pin->DefaultValue = PinSpec.DefaultValue;
				}
			}
		}
	}

	if (Outputs.Num() > 0)
	{
		if (!ResultNode)
		{
			ResultNode = NewObject<UK2Node_FunctionResult>(FunctionGraph);
			const FString ResultAIName = ResultNodeName.IsEmpty()
				? FString::Printf(TEXT("%s_Result"), *FunctionName)
				: ResultNodeName;
			ResultNode->NodePosX = 480;
			ResultNode->NodePosY = 0;
			ResultNode->NodeComment = FString::Printf(TEXT("AI:%s"), *ResultAIName);
			ResultNode->bCommentBubbleVisible = false;

			FunctionGraph->AddNode(ResultNode, false, false);
			ResultNode->CreateNewGuid();
			ResultNode->PostPlacedNewNode();
		}
		else
		{
			const FString ResultAIName = ResultNodeName.IsEmpty()
				? FString::Printf(TEXT("%s_Result"), *FunctionName)
				: ResultNodeName;
			ResultNode->NodeComment = FString::Printf(TEXT("AI:%s"), *ResultAIName);
			ResultNode->bCommentBubbleVisible = false;
		}

		for (const FAIBlueprintGraphPinSpec& PinSpec : Outputs)
		{
			if (!ResultNode || PinSpec.Name.IsEmpty())
			{
				continue;
			}
			if (!FindPin(ResultNode, PinSpec.Name, EGPD_Input))
			{
				UEdGraphPin* Pin = ResultNode->CreateUserDefinedPin(
					FName(*PinSpec.Name),
					ResolveVarType(PinSpec.Type),
					EGPD_Input,
					/*bUseUniqueName=*/false);
				if (Pin && !PinSpec.DefaultValue.IsEmpty())
				{
					Pin->DefaultValue = PinSpec.DefaultValue;
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("EnsureFunctionGraph: Ensured '%s' (%d inputs, %d outputs)"),
		*FunctionName, Inputs.Num(), Outputs.Num());
	return FunctionGraph;
}

TSharedPtr<FJsonObject> UAIBlueprintGraphBuilder::GetGraphAsJson(
	UBlueprint* Blueprint,
	const FString& GraphName)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Blueprint)
	{
		return Result;
	}

	// Find the graph
	UEdGraph* TargetGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && (GraphName.IsEmpty() || Graph->GetName() == GraphName))
		{
			TargetGraph = Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetName() == GraphName)
			{
				TargetGraph = Graph;
				break;
			}
		}
	}

	if (!TargetGraph)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
		return Result;
	}

	Result->SetStringField(TEXT("graph_name"), TargetGraph->GetName());

	// Nodes
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		// Extract AI name if present
		if (Node->NodeComment.StartsWith(TEXT("AI:")))
		{
			NodeObj->SetStringField(TEXT("ai_name"), Node->NodeComment.Mid(3));
		}

		// Pins
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			if (!Pin->DefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default"), Pin->DefaultValue);
			}

			// Connected to
			if (Pin->LinkedTo.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> LinksArray;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
					LinkObj->SetStringField(TEXT("node"), LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
					LinkObj->SetStringField(TEXT("pin"), LinkedPin->PinName.ToString());
					if (LinkedPin->GetOwningNode()->NodeComment.StartsWith(TEXT("AI:")))
					{
						LinkObj->SetStringField(TEXT("ai_name"), LinkedPin->GetOwningNode()->NodeComment.Mid(3));
					}
					LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
				}
				PinObj->SetArrayField(TEXT("connected_to"), LinksArray);
			}

			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);

		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}
	Result->SetArrayField(TEXT("nodes"), NodesArray);

	return Result;
}

TArray<FString> UAIBlueprintGraphBuilder::ListGraphs(UBlueprint* Blueprint)
{
	TArray<FString> Names;
	if (!Blueprint)
	{
		return Names;
	}

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph)
		{
			Names.Add(Graph->GetName());
		}
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph)
		{
			Names.Add(Graph->GetName());
		}
	}

	return Names;
}

// =============================================================================
// BLUEPRINT VARIABLE MANAGEMENT
// =============================================================================

bool UAIBlueprintGraphBuilder::AddVariable(
	UBlueprint* Blueprint,
	const FString& VarName,
	const FString& VarType,
	bool bInstanceEditable,
	bool bBlueprintReadOnly,
	const FString& CategoryName)
{
	if (!Blueprint || VarName.IsEmpty())
	{
		return false;
	}

	FEdGraphPinType PinType = ResolveVarType(VarType);

	FName VarFName(*VarName);

	// Check if already exists
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarFName) != INDEX_NONE)
	{
		UE_LOG(LogAIGraphBuilder, Warning, TEXT("AddVariable: Variable '%s' already exists"), *VarName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddVariable", "AI: Add Variable"));

	bool bResult = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarFName, PinType);
	if (!bResult)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("AddVariable: Failed to add '%s' of type '%s'"), *VarName, *VarType);
		return false;
	}

	// Set instance editable (expose to Details panel)
	if (bInstanceEditable)
	{
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VarFName, false);
	}
	if (bBlueprintReadOnly)
	{
		FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VarFName, true);
	}

	// Set category
	if (!CategoryName.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(
			Blueprint, VarFName, nullptr, FText::FromString(CategoryName));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("AddVariable: Added '%s' of type '%s'"), *VarName, *VarType);
	return true;
}

bool UAIBlueprintGraphBuilder::SetVariableDefault(
	UBlueprint* Blueprint,
	const FString& VarName,
	const FString& DefaultValue)
{
	if (!Blueprint || VarName.IsEmpty())
	{
		return false;
	}

	// Find the variable index
	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*VarName));
	if (VarIndex == INDEX_NONE)
	{
		UE_LOG(LogAIGraphBuilder, Error, TEXT("SetVariableDefault: Variable '%s' not found"), *VarName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetVariableDefault", "AI: Set Variable Default"));

	Blueprint->NewVariables[VarIndex].DefaultValue = DefaultValue;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	UE_LOG(LogAIGraphBuilder, Log, TEXT("SetVariableDefault: Set '%s' = '%s'"), *VarName, *DefaultValue);
	return true;
}

bool UAIBlueprintGraphBuilder::RemoveVariable(
	UBlueprint* Blueprint,
	const FString& VarName)
{
	if (!Blueprint || VarName.IsEmpty())
	{
		return false;
	}

	// Check if variable exists first
	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*VarName));
	if (VarIndex == INDEX_NONE)
	{
		UE_LOG(LogAIGraphBuilder, Warning, TEXT("RemoveVariable: Variable '%s' not found"), *VarName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIRemoveVariable", "AI: Remove Variable"));

	FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*VarName));
	UE_LOG(LogAIGraphBuilder, Log, TEXT("RemoveVariable: Removed '%s'"), *VarName);
	return true;
}

TSharedPtr<FJsonObject> UAIBlueprintGraphBuilder::GetVariablesAsJson(UBlueprint* Blueprint)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Blueprint)
	{
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type_category"), Var.VarType.PinCategory.ToString());
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			VarObj->SetStringField(TEXT("type_subcategory"), Var.VarType.PinSubCategoryObject->GetPathName());
		}
		if (!Var.VarType.PinSubCategory.IsNone())
		{
			VarObj->SetStringField(TEXT("type_sub"), Var.VarType.PinSubCategory.ToString());
		}
		if (!Var.DefaultValue.IsEmpty())
		{
			VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
		}
		VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VarObj->SetBoolField(TEXT("instance_editable"),
			!(Var.PropertyFlags & CPF_DisableEditOnInstance));

		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	Result->SetArrayField(TEXT("variables"), VarsArray);
	return Result;
}

#undef LOCTEXT_NAMESPACE
