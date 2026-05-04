// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTExporterBase.h"
#include "MCTSettings.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Knot.h"
#include "K2Node_ExecutionSequence.h"
#include "EdGraphNode_Comment.h"
#include "UObject/PropertyIterator.h"

//==========================================================================
// Property Export Helpers
//==========================================================================

FString UMCTExporterBase::ExportObjectProperties(UObject* Object, int32 IndentLevel, bool bFilterDefaults)
{
	if (!Object)
	{
		return TEXT("(null)\n");
	}

	FString Output;
	FString Indent = GetIndent(IndentLevel);

	// Get archetype for default value comparison (only when filtering)
	UObject* Archetype = bFilterDefaults ? Object->GetArchetype() : nullptr;

	// Ensure archetype is of compatible class for property comparison
	if (Archetype && !Archetype->GetClass()->IsChildOf(Object->GetClass()) && !Object->GetClass()->IsChildOf(Archetype->GetClass()))
	{
		Archetype = nullptr;
	}

	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (ShouldSkipProperty(Property, Object, bFilterDefaults))
		{
			continue;
		}

		// Skip properties identical to archetype (only when filtering defaults)
		if (bFilterDefaults && Archetype)
		{
			// Safety check: ensure property exists in archetype's class
			if (Archetype->GetClass()->FindPropertyByName(Property->GetFName()))
			{
				if (Property->Identical_InContainer(Object, Archetype))
				{
					continue;
				}
			}
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
		FString ValueStr = FormatPropertyValue(Property, ValuePtr, Object);

		if (ValueStr.IsEmpty() || ValueStr == TEXT("None"))
		{
			continue;
		}

		Output += FString::Printf(TEXT("%s%s=%s\n"), *Indent, *Property->GetName(), *ValueStr);
	}

	return Output;
}

FString UMCTExporterBase::ExportCDOProperties(UClass* Class, bool bFilterDefaults)
{
	if (!Class)
	{
		return TEXT("(no class)\n");
	}

	UObject* CDO = Class->GetDefaultObject();
	if (!CDO)
	{
		return TEXT("(no CDO)\n");
	}

	return ExportObjectProperties(CDO, 0, bFilterDefaults);
}

bool UMCTExporterBase::ShouldSkipProperty(FProperty* Property, UObject* Object, bool bFilterDefaults) const
{
	if (!Property)
	{
		return true;
	}

	// Skip transient properties
	if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
	{
		return true;
	}

	// Skip deprecated properties
	if (Property->HasAnyPropertyFlags(CPF_Deprecated))
	{
		return true;
	}

	return false;
}

FString UMCTExporterBase::FormatPropertyValue(FProperty* Property, const void* ValuePtr, UObject* Object) const
{
	FString ValueStr;
	Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Object, PPF_None);

	// Truncate very long values (limit configurable via Project Settings)
	const int32 MaxLen = UMCTSettings::Get()->MaxPropertyValueLength;
	if (ValueStr.Len() > MaxLen)
	{
		ValueStr = ValueStr.Left(MaxLen) + TEXT("...(truncated)");
	}

	return ValueStr;
}

bool UMCTExporterBase::IsInstancedObjectProperty(FProperty* Property) const
{
	return Property && Property->ContainsInstancedObjectProperty();
}

FString UMCTExporterBase::ExportObjectPropertiesDeep(UObject* Object, const FString& PathPrefix, int32 Depth, bool bFilterDefaults)
{
	if (!Object || Depth > 10) // Prevent infinite recursion
	{
		return TEXT("");
	}

	FString Output;

	// Get archetype for default value comparison
	UObject* Archetype = bFilterDefaults ? Object->GetArchetype() : nullptr;
	if (Archetype && !Archetype->GetClass()->IsChildOf(Object->GetClass()) && !Object->GetClass()->IsChildOf(Archetype->GetClass()))
	{
		Archetype = nullptr;
	}

	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (ShouldSkipProperty(Property, Object, bFilterDefaults))
		{
			continue;
		}

		// Skip properties identical to archetype when filtering defaults
		if (bFilterDefaults && Archetype && Archetype->GetClass()->FindPropertyByName(Property->GetFName()))
		{
			if (Property->Identical_InContainer(Object, Archetype))
			{
				continue;
			}
		}

		FString CurrentPath = PathPrefix.IsEmpty()
			? Property->GetName()
			: FString::Printf(TEXT("%s.%s"), *PathPrefix, *Property->GetName());

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

		ExportPropertyWithPath(Property, ValuePtr, Object, CurrentPath, Depth, bFilterDefaults, Output);
	}

	return Output;
}

void UMCTExporterBase::ExportPropertyWithPath(FProperty* Property, const void* ValuePtr, UObject* Object, const FString& CurrentPath, int32 Depth, bool bFilterDefaults, FString& OutResult)
{
	if (!Property || !ValuePtr)
	{
		return;
	}

	// 1. Array Property - iterate elements
	if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
		FProperty* InnerProp = ArrayProp->Inner;

		for (int32 i = 0; i < ArrayHelper.Num(); ++i)
		{
			FString IndexedPath = FString::Printf(TEXT("%s[%d]"), *CurrentPath, i);
			const void* ElementPtr = ArrayHelper.GetRawPtr(i);

			ExportPropertyWithPath(InnerProp, ElementPtr, Object, IndexedPath, Depth, bFilterDefaults, OutResult);
		}
		return;
	}

	// 2. Object Property - check for instanced reference
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
	{
		UObject* SubObj = ObjProp->GetObjectPropertyValue(ValuePtr);

		if (SubObj)
		{
			// Check if this is an instanced/embedded subobject
			bool bIsInstanced = Property->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ExportObject);

			// Also check if the object's outer is our container (embedded subobject)
			if (!bIsInstanced && SubObj->GetOuter() == Object)
			{
				bIsInstanced = true;
			}

			if (bIsInstanced)
			{
				// Export object name/class
				OutResult += FString::Printf(TEXT("%s=%s (%s)\n"), *CurrentPath, *SubObj->GetName(), *SubObj->GetClass()->GetName());

				// Recurse into sub-object properties
				OutResult += ExportObjectPropertiesDeep(SubObj, CurrentPath, Depth + 1, bFilterDefaults);
				return;
			}
			else
			{
				// External reference - just show the path
				FString ValueStr;
				Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Object, PPF_None);
				if (!ValueStr.IsEmpty() && ValueStr != TEXT("None"))
				{
					OutResult += FString::Printf(TEXT("%s=%s\n"), *CurrentPath, *ValueStr);
				}
				return;
			}
		}
		return;
	}

	// 3. Struct Property - export members individually
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		UScriptStruct* Struct = StructProp->Struct;

		// For simple structs (like FVector, FRotator), just export as single line
		static const TSet<FName> SimpleStructs = {
			TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"), TEXT("LinearColor"),
			TEXT("Color"), TEXT("Vector2D"), TEXT("IntPoint"), TEXT("Guid")
		};

		if (SimpleStructs.Contains(Struct->GetFName()))
		{
			FString ValueStr;
			Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Object, PPF_None);
			if (!ValueStr.IsEmpty())
			{
				OutResult += FString::Printf(TEXT("%s=%s\n"), *CurrentPath, *ValueStr);
			}
			return;
		}

		// For complex structs, iterate members
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* MemberProp = *It;

			if (ShouldSkipProperty(MemberProp, nullptr, bFilterDefaults))
			{
				continue;
			}

			FString MemberPath = FString::Printf(TEXT("%s.%s"), *CurrentPath, *MemberProp->GetName());
			const void* MemberPtr = MemberProp->ContainerPtrToValuePtr<void>(ValuePtr);

			ExportPropertyWithPath(MemberProp, MemberPtr, Object, MemberPath, Depth, bFilterDefaults, OutResult);
		}
		return;
	}

	// 4. Map Property - iterate key-value pairs
	if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper MapHelper(MapProp, ValuePtr);

		int32 Index = 0;
		for (int32 i = 0; i < MapHelper.GetMaxIndex(); ++i)
		{
			if (MapHelper.IsValidIndex(i))
			{
				FString KeyPath = FString::Printf(TEXT("%s[%d].Key"), *CurrentPath, Index);
				FString ValuePath = FString::Printf(TEXT("%s[%d].Value"), *CurrentPath, Index);

				ExportPropertyWithPath(MapProp->KeyProp, MapHelper.GetKeyPtr(i), Object, KeyPath, Depth, bFilterDefaults, OutResult);
				ExportPropertyWithPath(MapProp->ValueProp, MapHelper.GetValuePtr(i), Object, ValuePath, Depth, bFilterDefaults, OutResult);

				Index++;
			}
		}
		return;
	}

	// 5. Set Property - iterate elements
	if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		FScriptSetHelper SetHelper(SetProp, ValuePtr);

		int32 Index = 0;
		for (int32 i = 0; i < SetHelper.GetMaxIndex(); ++i)
		{
			if (SetHelper.IsValidIndex(i))
			{
				FString ElementPath = FString::Printf(TEXT("%s[%d]"), *CurrentPath, Index);
				ExportPropertyWithPath(SetProp->ElementProp, SetHelper.GetElementPtr(i), Object, ElementPath, Depth, bFilterDefaults, OutResult);
				Index++;
			}
		}
		return;
	}

	// 6. Scalar property - normal export
	FString ValueStr;
	Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Object, PPF_None);

	if (!ValueStr.IsEmpty() && ValueStr != TEXT("None"))
	{
		OutResult += FString::Printf(TEXT("%s=%s\n"), *CurrentPath, *ValueStr);
	}
}

//==========================================================================
// Graph Export Helpers
//==========================================================================

FString UMCTExporterBase::ExportGraphToText(UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("(null graph)\n");
	}

	FString Output;

	// Use UE's built-in graph export
	TSet<UObject*> NodesToExport;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node)
		{
			NodesToExport.Add(Node);
		}
	}

	if (NodesToExport.Num() > 0)
	{
		FEdGraphUtilities::ExportNodesToText(NodesToExport, Output);
	}
	else
	{
		Output = TEXT("(no nodes)\n");
	}

	return Output;
}

FString UMCTExporterBase::ExportGraphToTextSimplified(UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("(null graph)\n");
	}

	FString Output;

	// First, export any comment nodes at the top
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
		{
			FString CommentText = CommentNode->NodeComment;
			CommentText.ReplaceInline(TEXT("\r\n"), TEXT(" "));
			CommentText.ReplaceInline(TEXT("\n"), TEXT(" "));
			if (!CommentText.IsEmpty())
			{
				Output += FString::Printf(TEXT("// %s\n"), *CommentText);
			}
		}
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && !Node->IsA<UEdGraphNode_Comment>())
		{
			Output += ExportNodeSimplified(Node);
		}
	}

	if (Output.IsEmpty())
	{
		Output = TEXT("(no nodes)\n");
	}

	return Output;
}

FString UMCTExporterBase::ExportNodeSimplified(UEdGraphNode* Node)
{
	if (!Node)
	{
		return TEXT("");
	}

	FString Output;
	FString NodeName = Node->GetName();
	FString NodeInfo;

	// Comment nodes - special handling
	if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
	{
		FString Comment = CommentNode->NodeComment;
		Comment.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Comment.ReplaceInline(TEXT("\n"), TEXT(" "));
		return FString::Printf(TEXT("// %s\n"), *Comment);
	}

	// Knot nodes - skip (they're just routing)
	if (Cast<UK2Node_Knot>(Node))
	{
		return TEXT("");
	}

	// Determine node type and extract relevant info
	if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
	{
		FName FuncName = EntryNode->FunctionReference.GetMemberName();
		NodeInfo = FString::Printf(TEXT("FunctionEntry: %s"), *FuncName.ToString());
	}
	else if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
	{
		FName FuncName = ResultNode->FunctionReference.GetMemberName();
		NodeInfo = FString::Printf(TEXT("FunctionResult: %s"), *FuncName.ToString());
	}
	else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		FName EventName = EventNode->GetFunctionName();
		NodeInfo = FString::Printf(TEXT("Event: %s"), *EventName.ToString());
	}
	else if (UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
	{
		FName EventName = CustomEventNode->CustomFunctionName;
		NodeInfo = FString::Printf(TEXT("CustomEvent: %s"), *EventName.ToString());
	}
	else if (UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(Node))
	{
		FName FuncName = CallFuncNode->GetFunctionName();
		FString Context = CallFuncNode->FunctionReference.IsSelfContext() ? TEXT("self") : TEXT("");
		if (!CallFuncNode->FunctionReference.IsSelfContext())
		{
			if (UClass* ParentClass = CallFuncNode->FunctionReference.GetMemberParentClass())
			{
				Context = ParentClass->GetName();
			}
		}
		NodeInfo = FString::Printf(TEXT("Call: %s (%s)"), *FuncName.ToString(), *Context);
	}
	else if (UK2Node_VariableGet* VarGetNode = Cast<UK2Node_VariableGet>(Node))
	{
		FName VarName = VarGetNode->GetVarName();
		NodeInfo = FString::Printf(TEXT("Get: %s"), *VarName.ToString());
	}
	else if (UK2Node_VariableSet* VarSetNode = Cast<UK2Node_VariableSet>(Node))
	{
		FName VarName = VarSetNode->GetVarName();
		NodeInfo = FString::Printf(TEXT("Set: %s"), *VarName.ToString());
	}
	else if (UK2Node_IfThenElse* BranchNode = Cast<UK2Node_IfThenElse>(Node))
	{
		NodeInfo = TEXT("Branch");
	}
	else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
	{
		UClass* TargetClass = CastNode->TargetType;
		FString TargetName = TargetClass ? TargetClass->GetName() : TEXT("Unknown");
		NodeInfo = FString::Printf(TEXT("Cast: %s"), *TargetName);
	}
	else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
	{
		UEdGraph* MacroGraph = MacroNode->GetMacroGraph();
		FString MacroName = MacroGraph ? MacroGraph->GetName() : TEXT("Unknown");
		NodeInfo = FString::Printf(TEXT("Macro: %s"), *MacroName);
	}
	else if (UK2Node_ExecutionSequence* SeqNode = Cast<UK2Node_ExecutionSequence>(Node))
	{
		NodeInfo = TEXT("Sequence");
	}
	else
	{
		// Generic node - just use class name
		NodeInfo = Node->GetClass()->GetName();
		NodeInfo.RemoveFromStart(TEXT("K2Node_"));
	}

	// Output node header
	Output = FString::Printf(TEXT("\n[%s] %s\n"), *NodeName, *NodeInfo);

	// Export pins (skip hidden pins, skip self pins for most cases)
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden)
		{
			continue;
		}

		// Skip "self" target pins - they're implicit
		if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
		{
			continue;
		}

		Output += ExportPinSimplified(Pin);
	}

	return Output;
}

FString UMCTExporterBase::ExportPinSimplified(UEdGraphPin* Pin)
{
	if (!Pin || Pin->bHidden)
	{
		return TEXT("");
	}

	// Direction label
	FString DirLabel;
	bool bIsExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
	if (bIsExec)
	{
		DirLabel = (Pin->Direction == EGPD_Output) ? TEXT("ExecOut") : TEXT("ExecIn");
	}
	else
	{
		DirLabel = (Pin->Direction == EGPD_Output) ? TEXT("Out") : TEXT("In");
	}

	// Type string (skip for exec pins)
	FString TypeStr = GetPinTypeString(Pin->PinType);

	// Default value (only if different from autogenerated)
	FString DefaultStr;
	if (!Pin->DefaultValue.IsEmpty() && Pin->DefaultValue != Pin->AutogeneratedDefaultValue)
	{
		DefaultStr = FString::Printf(TEXT(" = %s"), *Pin->DefaultValue);
	}
	// For object pins, check DefaultObject
	else if (Pin->DefaultObject)
	{
		// Show asset path for assets, just name for others
		if (Pin->DefaultObject->IsAsset())
		{
			DefaultStr = FString::Printf(TEXT(" = %s"), *Pin->DefaultObject->GetPathName());
		}
		else
		{
			DefaultStr = FString::Printf(TEXT(" = %s"), *Pin->DefaultObject->GetName());
		}
	}

	// Connections
	FString ConnStr;
	for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode())
		{
			FString Arrow = (Pin->Direction == EGPD_Output) ? TEXT(" -> ") : TEXT(" <- ");
			ConnStr += Arrow + LinkedNode->GetName();
		}
	}

	// Build output
	FString Output = FString::Printf(TEXT("  Pin[%s] %s"), *DirLabel, *Pin->PinName.ToString());
	if (!TypeStr.IsEmpty())
	{
		Output += FString::Printf(TEXT(" (%s)"), *TypeStr);
	}
	Output += DefaultStr;
	Output += ConnStr;
	Output += TEXT("\n");

	return Output;
}

FString UMCTExporterBase::GetPinTypeString(const FEdGraphPinType& PinType)
{
	FString TypeStr;

	// Skip exec pins - they don't need type display
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return TEXT("");
	}

	// Main category
	TypeStr = PinType.PinCategory.ToString();

	// Sub category object (e.g., class name, struct name, enum name)
	if (UObject* SubCatObj = PinType.PinSubCategoryObject.Get())
	{
		FString ObjName = SubCatObj->GetName();
		// Clean up common prefixes
		ObjName.RemoveFromStart(TEXT("E")); // Enum prefix
		TypeStr = ObjName;
	}
	else if (!PinType.PinSubCategory.IsNone())
	{
		TypeStr = PinType.PinSubCategory.ToString();
	}

	// Container type
	if (PinType.IsArray())
	{
		TypeStr = FString::Printf(TEXT("Array<%s>"), *TypeStr);
	}
	else if (PinType.IsSet())
	{
		TypeStr = FString::Printf(TEXT("Set<%s>"), *TypeStr);
	}
	else if (PinType.IsMap())
	{
		TypeStr = FString::Printf(TEXT("Map<%s>"), *TypeStr);
	}

	// Reference
	if (PinType.bIsReference)
	{
		TypeStr += TEXT("&");
	}

	return TypeStr;
}

//==========================================================================
// Formatting Helpers
//==========================================================================

FString UMCTExporterBase::GetIndent(int32 Level) const
{
	return FString::ChrN(Level * 2, TEXT(' '));
}

FString UMCTExporterBase::MakeSectionHeader(const FString& Title) const
{
	return FString::Printf(TEXT("=== %s ===\n"), *Title);
}

FString UMCTExporterBase::MakeSubsectionHeader(const FString& Title) const
{
	return FString::Printf(TEXT("--- %s ---\n"), *Title);
}

FString UMCTExporterBase::SanitizeString(const FString& Input) const
{
	FString Result = Input;
	Result.ReplaceInline(TEXT("\r\n"), TEXT(" "));
	Result.ReplaceInline(TEXT("\n"), TEXT(" "));
	Result.ReplaceInline(TEXT("\t"), TEXT(" "));
	return Result;
}
