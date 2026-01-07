// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/AIBlueprintExporter.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"

bool UAIBlueprintExporter::CanExport(UObject* Asset) const
{
	if (!Asset)
	{
		return false;
	}

	// We handle UBlueprint but NOT specialized blueprints
	// (WidgetBlueprint and AnimBlueprint have their own exporters)
	if (Asset->IsA<UWidgetBlueprint>() || Asset->IsA<UAnimBlueprint>())
	{
		return false;
	}

	return Asset->IsA<UBlueprint>();
}

TArray<UClass*> UAIBlueprintExporter::GetSupportedClasses() const
{
	return { UBlueprint::StaticClass() };
}

FString UAIBlueprintExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)
	{
		return TEXT("Error: Not a Blueprint\n");
	}

	return ExportBlueprint(Blueprint, bFilterDefaults);
}

FString UAIBlueprintExporter::ExportBlueprint(UBlueprint* Blueprint, bool bFilterDefaults)
{
	FString Output;

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("BLUEPRINT: %s"), *Blueprint->GetName()));

	// Parent class
	Output += FString::Printf(TEXT("ParentClass: %s\n"),
		Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("None"));

	// Blueprint type
	Output += FString::Printf(TEXT("BlueprintType: %s\n"),
		*UEnum::GetValueAsString(Blueprint->BlueprintType));

	Output += TEXT("\n");

	// Implemented interfaces
	FString InterfacesOutput = ExportInterfaces(Blueprint);
	if (!InterfacesOutput.IsEmpty())
	{
		Output += InterfacesOutput;
	}

	// Class Default Object properties
	if (Blueprint->GeneratedClass)
	{
		Output += MakeSectionHeader(TEXT("CLASS DEFAULTS"));
		Output += ExportCDOProperties(Blueprint->GeneratedClass, bFilterDefaults);
		Output += TEXT("\n");
	}

	// Blueprint variables (member variables defined in BP)
	FString VariablesOutput = ExportVariables(Blueprint, bFilterDefaults);
	if (!VariablesOutput.IsEmpty())
	{
		Output += VariablesOutput;
	}

	// Export all graphs
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		Output += MakeSectionHeader(FString::Printf(TEXT("GRAPH: %s"), *Graph->GetName()));

		// Use simplified export when filtering defaults (for AI-readable output)
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

FString UAIBlueprintExporter::ExportInterfaces(UBlueprint* Blueprint)
{
	if (!Blueprint || Blueprint->ImplementedInterfaces.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("INTERFACES"));

	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface)
		{
			FString InterfaceName = InterfaceDesc.Interface->GetName();
			InterfaceName.RemoveFromEnd(TEXT("_C"));
			Output += FString::Printf(TEXT("%s\n"), *InterfaceName);

			// List interface graphs (functions)
			for (UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				if (Graph)
				{
					Output += FString::Printf(TEXT("  - %s\n"), *Graph->GetName());
				}
			}
		}
	}

	Output += TEXT("\n");
	return Output;
}

FString UAIBlueprintExporter::ExportVariables(UBlueprint* Blueprint, bool bFilterDefaults)
{
	if (!Blueprint || Blueprint->NewVariables.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("VARIABLES"));

	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		FString VarType = Var.VarType.PinCategory.ToString();

		// Get more specific type info
		if (UObject* SubCatObj = Var.VarType.PinSubCategoryObject.Get())
		{
			VarType = SubCatObj->GetName();
		}
		else if (!Var.VarType.PinSubCategory.IsNone())
		{
			VarType = Var.VarType.PinSubCategory.ToString();
		}

		// Container type modifiers
		if (Var.VarType.IsArray())
		{
			VarType = FString::Printf(TEXT("Array<%s>"), *VarType);
		}
		else if (Var.VarType.IsSet())
		{
			VarType = FString::Printf(TEXT("Set<%s>"), *VarType);
		}
		else if (Var.VarType.IsMap())
		{
			VarType = FString::Printf(TEXT("Map<%s>"), *VarType);
		}

		Output += FString::Printf(TEXT("%s: %s"), *Var.VarName.ToString(), *VarType);

		// Default value (if present and not filtering)
		if (!bFilterDefaults && !Var.DefaultValue.IsEmpty())
		{
			FString DefaultVal = Var.DefaultValue;
			if (DefaultVal.Len() > 100)
			{
				DefaultVal = DefaultVal.Left(100) + TEXT("...");
			}
			Output += FString::Printf(TEXT(" = %s"), *DefaultVal);
		}

		// Flags
		TArray<FString> Flags;
		if (Var.PropertyFlags & CPF_BlueprintReadOnly)
		{
			Flags.Add(TEXT("ReadOnly"));
		}
		if (Var.PropertyFlags & CPF_ExposeOnSpawn)
		{
			Flags.Add(TEXT("ExposeOnSpawn"));
		}
		if (Var.PropertyFlags & CPF_SaveGame)
		{
			Flags.Add(TEXT("SaveGame"));
		}

		if (Flags.Num() > 0)
		{
			Output += FString::Printf(TEXT(" [%s]"), *FString::Join(Flags, TEXT(", ")));
		}

		Output += TEXT("\n");
	}

	Output += TEXT("\n");
	return Output;
}
