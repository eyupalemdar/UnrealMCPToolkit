// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "EdGraph/EdGraphPin.h"
#include "AIBlueprintGraphBuilder.generated.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node;
class FJsonObject;

struct FAIBlueprintGraphPinSpec
{
	FString Name;
	FString Type;
	FString DefaultValue;
};

/**
 * Static utility class for Blueprint graph manipulation.
 *
 * Provides programmatic creation of Blueprint event graphs:
 * - AddEventNode: Add event override nodes (BP_OnSelected, ReceiveBeginPlay, etc.)
 * - AddCustomEvent: Add custom event nodes
 * - AddFunctionCallNode: Add function call nodes (SetText, SetVisibility, etc.)
 * - AddVariableGetNode/SetNode: Add variable access nodes
 * - ConnectPins: Wire nodes together
 * - SetPinDefaultValue: Set unconnected input pin values
 *
 * Also handles Blueprint variable management:
 * - AddVariable/RemoveVariable: Manage member variables
 * - SetVariableDefault: Set default values
 *
 * Node naming convention: Each created node stores "AI:NodeName" in its
 * NodeComment field for later reference. FindNodeByName() uses this to locate nodes.
 *
 * All functions are static. Call from Game Thread only.
 */
UCLASS()
class COMMONAIEXPORT_API UAIBlueprintGraphBuilder : public UObject
{
	GENERATED_BODY()

public:
	// =========================================================================
	// NODE CREATION
	// =========================================================================

	/**
	 * Add an event override node to the event graph.
	 * @param Blueprint Target Blueprint
	 * @param EventName Function name to override (e.g. "BP_OnSelected", "ReceiveBeginPlay")
	 * @param NodeName Logical name for reference (stored as "AI:NodeName" in NodeComment)
	 * @param PosX X position in graph
	 * @param PosY Y position in graph
	 * @return The created/found node, or nullptr on failure
	 */
	static UK2Node* AddEventNode(
		UBlueprint* Blueprint,
		const FString& EventName,
		const FString& NodeName,
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	/**
	 * Add a Custom Event node.
	 * @param EventName Custom event display name
	 */
	static UK2Node* AddCustomEvent(
		UBlueprint* Blueprint,
		const FString& EventName,
		const FString& NodeName,
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	/**
	 * Add a function call node.
	 * @param FunctionName Function to call (e.g. "SetText", "SetVisibility", "SetColorAndOpacity")
	 * @param TargetClass Optional class path to search for the function.
	 *                    If empty, searches Blueprint parent class hierarchy + common UMG classes.
	 */
	static UK2Node* AddFunctionCallNode(
		UBlueprint* Blueprint,
		const FString& FunctionName,
		const FString& NodeName,
		const FString& TargetClass = TEXT(""),
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	/**
	 * Add a variable Get node.
	 * @param VariableName Blueprint variable name (e.g. "ButtonTextBlock", "Img_ActiveIndicator")
	 */
	static UK2Node* AddVariableGetNode(
		UBlueprint* Blueprint,
		const FString& VariableName,
		const FString& NodeName,
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	/**
	 * Add a variable Set node.
	 */
	static UK2Node* AddVariableSetNode(
		UBlueprint* Blueprint,
		const FString& VariableName,
		const FString& NodeName,
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	/**
	 * Add a "Make Struct" node (e.g. MakeLinearColor, MakeSlateFontInfo).
	 * @param StructName Struct type name (e.g. "LinearColor", "SlateFontInfo", "Vector")
	 */
	static UK2Node* AddMakeStructNode(
		UBlueprint* Blueprint,
		const FString& StructName,
		const FString& NodeName,
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	/**
	 * Add a Branch (if/else) node.
	 */
	static UK2Node* AddBranchNode(
		UBlueprint* Blueprint,
		const FString& NodeName,
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	/**
	 * Add a Call Parent Function node (Super:: call).
	 * Used to call the parent class implementation of an overridable function.
	 * @param FunctionName The function to call on parent (e.g. "HandleTabCreation")
	 */
	static UK2Node* AddCallParentFunctionNode(
		UBlueprint* Blueprint,
		const FString& FunctionName,
		const FString& NodeName,
		int32 PosX = 0, int32 PosY = 0,
		const FString& GraphName = TEXT(""));

	// =========================================================================
	// WIRING
	// =========================================================================

	/**
	 * Connect an output pin on one node to an input pin on another.
	 * @param FromNodeName Source node logical name
	 * @param FromPinName Source pin name (e.g. "then", "ReturnValue", exec pin aliases: "execute"/"exec")
	 * @param ToNodeName Target node logical name
	 * @param ToPinName Target pin name (e.g. "execute", "self", "InText", "NewParam")
	 * @return true if connection succeeded
	 */
	static bool ConnectPins(
		UBlueprint* Blueprint,
		const FString& FromNodeName,
		const FString& FromPinName,
		const FString& ToNodeName,
		const FString& ToPinName,
		const FString& GraphName = TEXT(""));

	/**
	 * Set a pin's default value (for unconnected input pins).
	 * @param PinName Pin name on the node
	 * @param DefaultValue Value as string (ImportText format)
	 */
	static bool SetPinDefaultValue(
		UBlueprint* Blueprint,
		const FString& NodeName,
		const FString& PinName,
		const FString& DefaultValue,
		const FString& GraphName = TEXT(""));

	// =========================================================================
	// NODE MANAGEMENT
	// =========================================================================

	/** Remove a node by its logical name. */
	static bool RemoveNode(
		UBlueprint* Blueprint,
		const FString& NodeName,
		const FString& GraphName = TEXT(""));

	/** Ensure a Blueprint function graph exists and optionally define entry/result pins. */
	static UEdGraph* EnsureFunctionGraph(
		UBlueprint* Blueprint,
		const FString& FunctionName,
		const TArray<FAIBlueprintGraphPinSpec>& Inputs,
		const TArray<FAIBlueprintGraphPinSpec>& Outputs,
		const FString& EntryNodeName = TEXT(""),
		const FString& ResultNodeName = TEXT(""));

	/** Get a graph as JSON (nodes + connections). */
	static TSharedPtr<FJsonObject> GetGraphAsJson(
		UBlueprint* Blueprint,
		const FString& GraphName = TEXT("EventGraph"));

	/** List all graph names in a Blueprint. */
	static TArray<FString> ListGraphs(UBlueprint* Blueprint);

	// =========================================================================
	// BLUEPRINT VARIABLE MANAGEMENT
	// =========================================================================

	/**
	 * Add a member variable to a Blueprint.
	 * @param VarName Variable name
	 * @param VarType Type string: "bool", "int", "float", "double", "String", "Name", "Text",
	 *               "byte", "Vector", "Rotator", "Transform", "LinearColor",
	 *               or object class path "/Script/UMG.TextBlock"
	 * @param bInstanceEditable Expose to Details panel in editor
	 * @param bBlueprintReadOnly Read-only in child Blueprints
	 * @param CategoryName Optional variable category
	 */
	static bool AddVariable(
		UBlueprint* Blueprint,
		const FString& VarName,
		const FString& VarType,
		bool bInstanceEditable = false,
		bool bBlueprintReadOnly = false,
		const FString& CategoryName = TEXT(""));

	/** Set a variable's default value. */
	static bool SetVariableDefault(
		UBlueprint* Blueprint,
		const FString& VarName,
		const FString& DefaultValue);

	/** Remove a variable from a Blueprint. */
	static bool RemoveVariable(
		UBlueprint* Blueprint,
		const FString& VarName);

	/** Get all Blueprint variables as JSON. */
	static TSharedPtr<FJsonObject> GetVariablesAsJson(UBlueprint* Blueprint);

	// =========================================================================
	// UTILITY
	// =========================================================================

	/** Load any Blueprint by asset path. */
	static UBlueprint* LoadBlueprint(const FString& AssetPath);

	/**
	 * Find a node by its logical name (AI:NodeName in NodeComment).
	 * Searches all graphs if GraphName is empty.
	 */
	static UK2Node* FindNodeByName(
		UBlueprint* Blueprint,
		const FString& NodeName,
		const FString& GraphName = TEXT(""));

	/** Get the event graph (or first UbergraphPage). */
	static UEdGraph* GetEventGraph(UBlueprint* Blueprint);

	/** Find an existing ubergraph or function graph by name. Empty/EventGraph resolves to the event graph. */
	static UEdGraph* FindGraph(
		UBlueprint* Blueprint,
		const FString& GraphName = TEXT(""));

	/** Find an existing graph, or create the event graph when requested by an empty/EventGraph name. */
	static UEdGraph* ResolveGraph(
		UBlueprint* Blueprint,
		const FString& GraphName = TEXT(""));

private:
	/** Find a pin by name on a node, with fallback matching. */
	static UEdGraphPin* FindPin(
		UK2Node* Node,
		const FString& PinName,
		EEdGraphPinDirection Direction);

	/** Resolve a type string to FEdGraphPinType for variable creation. */
	static FEdGraphPinType ResolveVarType(const FString& TypeString);

	/** Find a UFunction by name, searching multiple class hierarchies. */
	static UFunction* FindFunctionByName(
		UBlueprint* Blueprint,
		const FString& FunctionName,
		const FString& TargetClassPath);
};
