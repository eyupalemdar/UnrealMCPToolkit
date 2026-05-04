// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTAnimBlueprintExporter.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_BlendSpaceGraph.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "EdGraph/EdGraph.h"

bool UMCTAnimBlueprintExporter::CanExport(UObject* Asset) const
{
	return Asset && Asset->IsA<UAnimBlueprint>();
}

TArray<UClass*> UMCTAnimBlueprintExporter::GetSupportedClasses() const
{
	return { UAnimBlueprint::StaticClass() };
}

FString UMCTAnimBlueprintExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Asset);
	if (!AnimBP)
	{
		return TEXT("Error: Not an AnimBlueprint\n");
	}

	return ExportAnimBlueprint(AnimBP, bFilterDefaults);
}

FString UMCTAnimBlueprintExporter::ExportAnimBlueprint(UAnimBlueprint* AnimBP, bool bFilterDefaults)
{
	FString Output;

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("ANIM BLUEPRINT: %s"), *AnimBP->GetName()));

	// Parent class (AnimInstance class)
	Output += FString::Printf(TEXT("ParentClass: %s\n"),
		AnimBP->ParentClass ? *AnimBP->ParentClass->GetName() : TEXT("None"));

	// Target skeleton
	USkeleton* TargetSkeleton = AnimBP->TargetSkeleton.Get();
	if (TargetSkeleton)
	{
		Output += FString::Printf(TEXT("TargetSkeleton: %s\n"), *TargetSkeleton->GetName());
	}
	else
	{
		// Try preview skeleton
		USkeletalMesh* PreviewMesh = AnimBP->GetPreviewMesh();
		if (PreviewMesh)
		{
			Output += FString::Printf(TEXT("PreviewMesh: %s\n"), *PreviewMesh->GetName());
			if (PreviewMesh->GetSkeleton())
			{
				Output += FString::Printf(TEXT("TargetSkeleton: %s (from PreviewMesh)\n"),
					*PreviewMesh->GetSkeleton()->GetName());
			}
		}
	}

	Output += TEXT("\n");

	// Class Default Object properties
	if (AnimBP->GeneratedClass)
	{
		Output += MakeSectionHeader(TEXT("CLASS DEFAULTS"));
		Output += ExportCDOProperties(AnimBP->GeneratedClass, bFilterDefaults);
		Output += TEXT("\n");
	}

	// Blueprint variables
	FString VariablesOutput = ExportVariables(AnimBP, bFilterDefaults);
	if (!VariablesOutput.IsEmpty())
	{
		Output += VariablesOutput;
	}

	// Implemented interfaces
	FString InterfacesOutput = ExportInterfaces(AnimBP);
	if (!InterfacesOutput.IsEmpty())
	{
		Output += InterfacesOutput;
	}

	// AnimGraph summary (state machines, blend spaces)
	FString AnimGraphOutput = ExportAnimGraphSummary(AnimBP);
	if (!AnimGraphOutput.IsEmpty())
	{
		Output += AnimGraphOutput;
	}

	// Anim notifies
	FString NotifiesOutput = ExportAnimNotifies(AnimBP);
	if (!NotifiesOutput.IsEmpty())
	{
		Output += NotifiesOutput;
	}

	// Linked anim layers
	FString LayersOutput = ExportLinkedAnimLayers(AnimBP);
	if (!LayersOutput.IsEmpty())
	{
		Output += LayersOutput;
	}

	// Slot names
	FString SlotsOutput = ExportSlotNames(AnimBP);
	if (!SlotsOutput.IsEmpty())
	{
		Output += SlotsOutput;
	}

	// Export all graphs (full detail from base class)
	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		Output += MakeSectionHeader(FString::Printf(TEXT("GRAPH: %s"), *Graph->GetName()));

		if (bFilterDefaults)
		{
			Output += ExportGraphToTextSimplified(Graph);
		}
		else
		{
			Output += ExportGraphToText(Graph);
		}
		Output += TEXT("\n");
	}

	return Output;
}

FString UMCTAnimBlueprintExporter::ExportAnimGraphSummary(UAnimBlueprint* AnimBP)
{
	if (!AnimBP)
	{
		return TEXT("");
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	TArray<FString> StateMachines;
	TArray<FString> BlendSpaces;
	TArray<FString> AnimNodeSummaries;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			// State Machines
			if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
			{
				FString SMInfo = SMNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

				// Try to get state list from the state machine graph
				if (UEdGraph* SMGraph = SMNode->EditorStateMachineGraph)
				{
					TArray<FString> StateNames;
					for (UEdGraphNode* SMSubNode : SMGraph->Nodes)
					{
						if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMSubNode))
						{
							StateNames.Add(StateNode->GetStateName());
						}
					}

					if (StateNames.Num() > 0)
					{
						SMInfo += FString::Printf(TEXT(" (%d states: %s)"),
							StateNames.Num(), *FString::Join(StateNames, TEXT(", ")));
					}
				}

				StateMachines.Add(SMInfo);
			}

			// Blend Spaces
			if (UAnimGraphNode_BlendSpaceGraph* BSNode = Cast<UAnimGraphNode_BlendSpaceGraph>(Node))
			{
				FString BSInfo = BSNode->GetName();
				BlendSpaces.Add(BSInfo);
			}
		}
	}

	if (StateMachines.Num() == 0 && BlendSpaces.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("ANIMGRAPH SUMMARY"));

	if (StateMachines.Num() > 0)
	{
		Output += TEXT("State Machines:\n");
		for (const FString& SM : StateMachines)
		{
			Output += FString::Printf(TEXT("  - %s\n"), *SM);
		}
		Output += TEXT("\n");
	}

	if (BlendSpaces.Num() > 0)
	{
		Output += TEXT("Blend Spaces:\n");
		for (const FString& BS : BlendSpaces)
		{
			Output += FString::Printf(TEXT("  - %s\n"), *BS);
		}
		Output += TEXT("\n");
	}

	return Output;
}

FString UMCTAnimBlueprintExporter::ExportAnimNotifies(UAnimBlueprint* AnimBP)
{
	if (!AnimBP)
	{
		return TEXT("");
	}

	// AnimNotifies are defined on the skeleton, not the AnimBP directly.
	// However, AnimBP can reference them. Check generated class for notify names.
	UAnimBlueprintGeneratedClass* GenClass = Cast<UAnimBlueprintGeneratedClass>(AnimBP->GeneratedClass);
	if (!GenClass)
	{
		return TEXT("");
	}

	// Collect notify names from AnimNotifyEvent arrays on referenced montages
	// For now, list the anim notify events from the graph nodes
	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	TSet<FString> NotifyNames;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			// Check for notify handler function names
			FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (NodeTitle.Contains(TEXT("AnimNotify")) || NodeTitle.Contains(TEXT("Notify")))
			{
				NotifyNames.Add(NodeTitle);
			}
		}
	}

	if (NotifyNames.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("ANIM NOTIFIES"));

	for (const FString& NotifyName : NotifyNames)
	{
		Output += FString::Printf(TEXT("  - %s\n"), *NotifyName);
	}
	Output += TEXT("\n");

	return Output;
}

FString UMCTAnimBlueprintExporter::ExportLinkedAnimLayers(UAnimBlueprint* AnimBP)
{
	if (!AnimBP)
	{
		return TEXT("");
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	TArray<FString> LayerNames;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_LinkedAnimLayer* LayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(Node))
			{
				FString LayerInfo = LayerNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				LayerNames.Add(LayerInfo);
			}
		}
	}

	if (LayerNames.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("LINKED ANIM LAYERS"));

	for (const FString& Layer : LayerNames)
	{
		Output += FString::Printf(TEXT("  - %s\n"), *Layer);
	}
	Output += TEXT("\n");

	return Output;
}

FString UMCTAnimBlueprintExporter::ExportSlotNames(UAnimBlueprint* AnimBP)
{
	if (!AnimBP)
	{
		return TEXT("");
	}

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	TSet<FString> SlotNames;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_Slot* SlotNode = Cast<UAnimGraphNode_Slot>(Node))
			{
				FString SlotInfo = SlotNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				SlotNames.Add(SlotInfo);
			}
		}
	}

	if (SlotNames.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("ANIM SLOT NAMES"));

	for (const FString& Slot : SlotNames)
	{
		Output += FString::Printf(TEXT("  - %s\n"), *Slot);
	}
	Output += TEXT("\n");

	return Output;
}
