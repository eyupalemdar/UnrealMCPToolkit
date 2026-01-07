// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportCommandlet.h"
#include "CommonAIExportModule.h"
#include "AIExportSettings.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/DataAsset.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "K2Node.h"
#include "UObject/PropertyIterator.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"

// Audio Foundation includes
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundAttenuation.h"

// Audio Modulation includes
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulationPatch.h"

UAIExportCommandlet::UAIExportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UAIExportCommandlet::Main(const FString& Params)
{
	UE_LOG(LogAIExport, Display, TEXT("========================================"));
	UE_LOG(LogAIExport, Display, TEXT("AI Export Commandlet"));
	UE_LOG(LogAIExport, Display, TEXT("========================================"));

	// Parse parameters
	if (!ParseParameters(Params))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to parse parameters"));
		UE_LOG(LogAIExport, Display, TEXT("Usage: -run=AIExport -asset=\"/Game/Path/To/Asset\""));
		return 1;
	}

	// Export the asset
	if (!ExportAsset(AssetPath))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to export asset: %s"), *AssetPath);
		return 1;
	}

	UE_LOG(LogAIExport, Display, TEXT("Export completed successfully!"));
	return 0;
}

bool UAIExportCommandlet::ParseParameters(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;

	// Parse command line
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Get asset path (required)
	if (const FString* AssetParam = ParamVals.Find(TEXT("asset")))
	{
		AssetPath = *AssetParam;
	}
	else if (Tokens.Num() > 0)
	{
		AssetPath = Tokens[0];
	}

	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogAIExport, Error, TEXT("No asset path specified. Use -asset=\"/Game/Path/To/Asset\""));
		return false;
	}

	// Get output directory (optional)
	if (const FString* OutputParam = ParamVals.Find(TEXT("output")))
	{
		OutputDirectory = *OutputParam;
	}
	else
	{
		OutputDirectory = GetOutputDirectory();
	}

	// Check for output mode flags (command line overrides settings)
	if (Switches.Contains(TEXT("raw")))
	{
		OutputMode = EAIExportOutputMode::RawOnly;
	}
	else if (Switches.Contains(TEXT("simplify")))
	{
		OutputMode = EAIExportOutputMode::SimplifiedOnly;
	}
	else if (Switches.Contains(TEXT("both")))
	{
		OutputMode = EAIExportOutputMode::Both;
	}
	else
	{
		// Use settings
		OutputMode = UAIExportSettings::Get()->OutputMode;
	}

	// Get output format
	if (const FString* FormatParam = ParamVals.Find(TEXT("format")))
	{
		OutputFormat = *FormatParam;
	}
	else
	{
		OutputFormat = TEXT("text");
	}

	UE_LOG(LogAIExport, Display, TEXT("Asset Path: %s"), *AssetPath);
	UE_LOG(LogAIExport, Display, TEXT("Output Dir: %s"), *OutputDirectory);

	const TCHAR* OutputModeStr = TEXT("Unknown");
	switch (OutputMode)
	{
		case EAIExportOutputMode::RawOnly: OutputModeStr = TEXT("Raw Only"); break;
		case EAIExportOutputMode::SimplifiedOnly: OutputModeStr = TEXT("Simplified Only"); break;
		case EAIExportOutputMode::Both: OutputModeStr = TEXT("Both"); break;
	}
	UE_LOG(LogAIExport, Display, TEXT("Output Mode: %s"), OutputModeStr);

	return true;
}

bool UAIExportCommandlet::ExportAsset(const FString& InAssetPath)
{
	// Load the asset
	UObject* Asset = LoadObject<UObject>(nullptr, *InAssetPath);

	if (!Asset)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to load asset: %s"), *InAssetPath);
		return false;
	}

	UE_LOG(LogAIExport, Display, TEXT("Loaded asset: %s (Class: %s)"),
		*Asset->GetName(), *Asset->GetClass()->GetName());

	return ExportByType(Asset);
}

bool UAIExportCommandlet::ExportByType(UObject* Asset)
{
	FString AssetTypeName;
	FString SanitizedName = SanitizeFileName(Asset->GetName());

	// Lambda to generate export content for the given asset
	auto GenerateExport = [this, Asset, &AssetTypeName]() -> FString
	{
		if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset))
		{
			AssetTypeName = TEXT("WidgetBlueprint");
			return ExportWidgetBlueprint(WidgetBP);
		}
		else if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Asset))
		{
			AssetTypeName = TEXT("AnimBlueprint");
			return ExportAnimBlueprint(AnimBP);
		}
		else if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			AssetTypeName = TEXT("Blueprint");
			return ExportBlueprint(Blueprint);
		}
		else if (UInputAction* InputAction = Cast<UInputAction>(Asset))
		{
			AssetTypeName = TEXT("InputAction");
			return ExportInputAction(InputAction);
		}
		else if (UInputMappingContext* MappingContext = Cast<UInputMappingContext>(Asset))
		{
			AssetTypeName = TEXT("InputMappingContext");
			return ExportInputMappingContext(MappingContext);
		}
		else if (UDataAsset* DataAsset = Cast<UDataAsset>(Asset))
		{
			AssetTypeName = TEXT("DataAsset");
			return ExportDataAsset(DataAsset);
		}
		// Audio Foundation
		else if (USoundClass* SoundClass = Cast<USoundClass>(Asset))
		{
			AssetTypeName = TEXT("SoundClass");
			return ExportSoundClass(SoundClass);
		}
		else if (USoundSubmix* Submix = Cast<USoundSubmix>(Asset))
		{
			AssetTypeName = TEXT("SoundSubmix");
			return ExportSoundSubmix(Submix);
		}
		else if (USoundConcurrency* Concurrency = Cast<USoundConcurrency>(Asset))
		{
			AssetTypeName = TEXT("SoundConcurrency");
			return ExportSoundConcurrency(Concurrency);
		}
		else if (USoundAttenuation* Attenuation = Cast<USoundAttenuation>(Asset))
		{
			AssetTypeName = TEXT("SoundAttenuation");
			return ExportSoundAttenuation(Attenuation);
		}
		// Audio Modulation
		else if (USoundControlBus* Bus = Cast<USoundControlBus>(Asset))
		{
			AssetTypeName = TEXT("SoundControlBus");
			return ExportSoundControlBus(Bus);
		}
		else if (USoundControlBusMix* Mix = Cast<USoundControlBusMix>(Asset))
		{
			AssetTypeName = TEXT("SoundControlBusMix");
			return ExportSoundControlBusMix(Mix);
		}
		else if (USoundModulationPatch* Patch = Cast<USoundModulationPatch>(Asset))
		{
			AssetTypeName = TEXT("SoundModulationPatch");
			return ExportSoundModulationPatch(Patch);
		}
		else
		{
			AssetTypeName = TEXT("Generic");
			return ExportGenericObject(Asset);
		}
	};

	// Handle output based on mode
	switch (OutputMode)
	{
		case EAIExportOutputMode::RawOnly:
		{
			// Raw: all properties (no filtering)
			bFilterDefaultValues = false;
			FString RawContent = GenerateExport();

			if (RawContent.IsEmpty())
			{
				UE_LOG(LogAIExport, Warning, TEXT("Export produced no content"));
				return false;
			}

			FString RawPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_raw.txt"), *SanitizedName));
			if (!WriteToFile(RawContent, RawPath))
			{
				return false;
			}
			UE_LOG(LogAIExport, Display, TEXT("Exported %s (raw) to: %s"), *AssetTypeName, *RawPath);
			break;
		}

		case EAIExportOutputMode::SimplifiedOnly:
		{
			// Simplified: filtered properties (skip defaults)
			bFilterDefaultValues = true;
			FString SimplifiedContent = GenerateExport();

			if (SimplifiedContent.IsEmpty())
			{
				UE_LOG(LogAIExport, Warning, TEXT("Export produced no content"));
				return false;
			}

			FString SimplifiedPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
			if (!WriteToFile(SimplifiedContent, SimplifiedPath))
			{
				return false;
			}
			UE_LOG(LogAIExport, Display, TEXT("Exported %s (simplified) to: %s"), *AssetTypeName, *SimplifiedPath);
			break;
		}

		case EAIExportOutputMode::Both:
		{
			// Generate raw first (all properties)
			bFilterDefaultValues = false;
			FString RawContent = GenerateExport();

			if (RawContent.IsEmpty())
			{
				UE_LOG(LogAIExport, Warning, TEXT("Export produced no content"));
				return false;
			}

			FString RawPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_raw.txt"), *SanitizedName));
			if (!WriteToFile(RawContent, RawPath))
			{
				return false;
			}
			UE_LOG(LogAIExport, Display, TEXT("Exported %s (raw) to: %s"), *AssetTypeName, *RawPath);

			// Generate simplified (filtered properties)
			bFilterDefaultValues = true;
			FString SimplifiedContent = GenerateExport();

			FString SimplifiedPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
			if (!WriteToFile(SimplifiedContent, SimplifiedPath))
			{
				return false;
			}
			UE_LOG(LogAIExport, Display, TEXT("Exported %s (simplified) to: %s"), *AssetTypeName, *SimplifiedPath);
			break;
		}
	}

	return true;
}

FString UAIExportCommandlet::ExportBlueprint(UBlueprint* Blueprint)
{
	FString Output;

	Output += FString::Printf(TEXT("=== BLUEPRINT: %s ===\n"), *Blueprint->GetName());
	Output += FString::Printf(TEXT("ParentClass: %s\n"),
		Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("None"));
	Output += TEXT("\n");

	// Export Class Default Object properties
	if (Blueprint->GeneratedClass)
	{
		Output += TEXT("=== CLASS DEFAULTS ===\n");
		Output += ExportCDOProperties(Blueprint->GeneratedClass);
		Output += TEXT("\n");
	}

	// Export all graphs
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph)
		{
			Output += FString::Printf(TEXT("=== GRAPH: %s ===\n"), *Graph->GetName());
			Output += ExportGraphToText(Graph);
			Output += TEXT("\n");
		}
	}

	return Output;
}

FString UAIExportCommandlet::ExportWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint)
{
	FString Output;

	Output += FString::Printf(TEXT("=== WIDGET BLUEPRINT: %s ===\n"), *WidgetBlueprint->GetName());
	Output += TEXT("\n");

	// Export widget tree
	if (WidgetBlueprint->WidgetTree)
	{
		Output += TEXT("=== WIDGET TREE ===\n");
		if (UWidget* RootWidget = WidgetBlueprint->WidgetTree->RootWidget)
		{
			Output += ExportWidgetTree(RootWidget, 0);
		}
		Output += TEXT("\n");
	}

	// Export Blueprint portion (graphs)
	Output += ExportBlueprint(WidgetBlueprint);

	return Output;
}

FString UAIExportCommandlet::ExportAnimBlueprint(UAnimBlueprint* AnimBlueprint)
{
	FString Output;

	Output += FString::Printf(TEXT("=== ANIM BLUEPRINT: %s ===\n"), *AnimBlueprint->GetName());
	Output += FString::Printf(TEXT("TargetSkeleton: %s\n"),
		AnimBlueprint->TargetSkeleton ? *AnimBlueprint->TargetSkeleton->GetName() : TEXT("None"));
	Output += TEXT("\n");

	// Export Blueprint portion
	Output += ExportBlueprint(AnimBlueprint);

	return Output;
}

FString UAIExportCommandlet::ExportDataAsset(UDataAsset* DataAsset)
{
	FString Output;

	Output += FString::Printf(TEXT("=== DATA ASSET: %s ===\n"), *DataAsset->GetName());
	Output += FString::Printf(TEXT("Class: %s\n"), *DataAsset->GetClass()->GetName());
	Output += TEXT("\n");

	Output += TEXT("=== PROPERTIES ===\n");
	Output += ExportObjectProperties(DataAsset);

	return Output;
}

FString UAIExportCommandlet::ExportInputAction(UInputAction* InputAction)
{
	FString Output;

	Output += FString::Printf(TEXT("=== INPUT ACTION: %s ===\n"), *InputAction->GetName());
	Output += TEXT("\n");

	Output += TEXT("=== PROPERTIES ===\n");
	Output += ExportObjectProperties(InputAction);

	return Output;
}

FString UAIExportCommandlet::ExportInputMappingContext(UInputMappingContext* MappingContext)
{
	FString Output;

	Output += FString::Printf(TEXT("=== INPUT MAPPING CONTEXT: %s ===\n"), *MappingContext->GetName());
	Output += TEXT("\n");

	Output += TEXT("=== PROPERTIES ===\n");
	Output += ExportObjectProperties(MappingContext);

	return Output;
}

FString UAIExportCommandlet::ExportGenericObject(UObject* Object)
{
	FString Output;

	Output += FString::Printf(TEXT("=== OBJECT: %s ===\n"), *Object->GetName());
	Output += FString::Printf(TEXT("Class: %s\n"), *Object->GetClass()->GetName());
	Output += TEXT("\n");

	Output += TEXT("=== PROPERTIES ===\n");
	Output += ExportObjectProperties(Object);

	return Output;
}

//////////////////////////////////////////////////////////////////////////
// Audio Foundation Export Functions

FString UAIExportCommandlet::ExportSoundClass(USoundClass* SoundClass)
{
	FString Output;

	Output += FString::Printf(TEXT("=== SOUND_CLASS: %s ===\n"), *SoundClass->GetName());

	// Hierarchy - Parent
	Output += FString::Printf(TEXT("ParentClass: %s\n"),
		SoundClass->ParentClass ? *SoundClass->ParentClass->GetPathName() : TEXT("None"));

	// Child Classes
	Output += FString::Printf(TEXT("ChildClassCount: %d\n"), SoundClass->ChildClasses.Num());
	for (const TObjectPtr<USoundClass>& Child : SoundClass->ChildClasses)
	{
		if (Child)
		{
			Output += FString::Printf(TEXT("  Child: %s\n"), *Child->GetName());
		}
	}

	Output += TEXT("\n=== PROPERTIES ===\n");
	Output += ExportObjectProperties(SoundClass);

	return Output;
}

FString UAIExportCommandlet::ExportSoundSubmix(USoundSubmix* Submix)
{
	FString Output;

	Output += FString::Printf(TEXT("=== SOUND_SUBMIX: %s ===\n"), *Submix->GetName());

	// Hierarchy - Parent
	Output += FString::Printf(TEXT("ParentSubmix: %s\n"),
		Submix->ParentSubmix ? *Submix->ParentSubmix->GetPathName() : TEXT("None"));

	// Child Submixes
	Output += FString::Printf(TEXT("ChildSubmixCount: %d\n"), Submix->ChildSubmixes.Num());
	for (const TObjectPtr<USoundSubmixBase>& Child : Submix->ChildSubmixes)
	{
		if (Child)
		{
			Output += FString::Printf(TEXT("  Child: %s\n"), *Child->GetName());
		}
	}

	// Important operational properties (always include in header)
	Output += TEXT("\n=== OPERATIONAL SETTINGS ===\n");
	Output += FString::Printf(TEXT("bMuteWhenBackgrounded: %s\n"), Submix->bMuteWhenBackgrounded ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bAutoDisable: %s\n"), Submix->bAutoDisable ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("AutoDisableTime: %.3f\n"), Submix->AutoDisableTime);
	Output += FString::Printf(TEXT("bSendToAudioLink: %s\n"), Submix->bSendToAudioLink ? TEXT("True") : TEXT("False"));

	// Volume modulation settings
	Output += FString::Printf(TEXT("OutputVolumeModulation.Value: %.2f dB\n"), Submix->OutputVolumeModulation.Value);
	Output += FString::Printf(TEXT("WetLevelModulation.Value: %.2f dB\n"), Submix->WetLevelModulation.Value);
	Output += FString::Printf(TEXT("DryLevelModulation.Value: %.2f dB\n"), Submix->DryLevelModulation.Value);

	// Envelope follower settings
	Output += FString::Printf(TEXT("EnvelopeFollowerAttackTime: %d ms\n"), Submix->EnvelopeFollowerAttackTime);
	Output += FString::Printf(TEXT("EnvelopeFollowerReleaseTime: %d ms\n"), Submix->EnvelopeFollowerReleaseTime);

	Output += TEXT("\n=== PROPERTIES ===\n");
	Output += ExportObjectProperties(Submix);

	return Output;
}

FString UAIExportCommandlet::ExportSoundConcurrency(USoundConcurrency* Concurrency)
{
	FString Output;

	Output += FString::Printf(TEXT("=== SOUND_CONCURRENCY: %s ===\n"), *Concurrency->GetName());

	// Direct access to Concurrency settings
	const FSoundConcurrencySettings& Settings = Concurrency->Concurrency;
	Output += FString::Printf(TEXT("MaxCount: %d\n"), Settings.MaxCount);
	Output += FString::Printf(TEXT("bLimitToOwner: %s\n"), Settings.bLimitToOwner ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("ResolutionRule: %s\n"),
		*UEnum::GetValueAsString(Settings.ResolutionRule));

	Output += TEXT("\n=== PROPERTIES ===\n");
	Output += ExportObjectProperties(Concurrency);

	return Output;
}

FString UAIExportCommandlet::ExportSoundAttenuation(USoundAttenuation* Attenuation)
{
	FString Output;

	Output += FString::Printf(TEXT("=== SOUND_ATTENUATION: %s ===\n"), *Attenuation->GetName());

	// Key attenuation settings
	const FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Output += FString::Printf(TEXT("bAttenuate: %s\n"), Settings.bAttenuate ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("DistanceAlgorithm: %s\n"),
		*UEnum::GetValueAsString(Settings.DistanceAlgorithm));
	Output += FString::Printf(TEXT("FalloffDistance: %.2f\n"), Settings.FalloffDistance);

	Output += TEXT("\n=== PROPERTIES ===\n");
	Output += ExportObjectProperties(Attenuation);

	return Output;
}

//////////////////////////////////////////////////////////////////////////
// Audio Modulation Export Functions

FString UAIExportCommandlet::ExportSoundControlBus(USoundControlBus* Bus)
{
	FString Output;

	Output += FString::Printf(TEXT("=== SOUND_CONTROL_BUS: %s ===\n"), *Bus->GetName());

	// Key bus properties
	Output += FString::Printf(TEXT("Address: %s\n"), *Bus->Address);
	Output += FString::Printf(TEXT("bBypass: %s\n"), Bus->bBypass ? TEXT("True") : TEXT("False"));

	Output += TEXT("\n=== PROPERTIES ===\n");
	Output += ExportObjectProperties(Bus);

	return Output;
}

FString UAIExportCommandlet::ExportSoundControlBusMix(USoundControlBusMix* Mix)
{
	FString Output;

	Output += FString::Printf(TEXT("=== SOUND_CONTROL_BUS_MIX: %s ===\n"), *Mix->GetName());

	// Mix stages
	Output += FString::Printf(TEXT("StageCount: %d\n"), Mix->MixStages.Num());
	for (int32 i = 0; i < Mix->MixStages.Num(); ++i)
	{
		const FSoundControlBusMixStage& Stage = Mix->MixStages[i];
		Output += FString::Printf(TEXT("  Stage[%d].Bus: %s\n"), i,
			Stage.Bus ? *Stage.Bus->GetPathName() : TEXT("None"));
		Output += FString::Printf(TEXT("  Stage[%d].Value.TargetValue: %.2f\n"), i, Stage.Value.TargetValue);
	}

	Output += TEXT("\n=== PROPERTIES ===\n");
	Output += ExportObjectProperties(Mix);

	return Output;
}

FString UAIExportCommandlet::ExportSoundModulationPatch(USoundModulationPatch* Patch)
{
	FString Output;

	Output += FString::Printf(TEXT("=== SOUND_MODULATION_PATCH: %s ===\n"), *Patch->GetName());

	// Patch settings
	const FSoundControlModulationPatch& Settings = Patch->PatchSettings;
	Output += FString::Printf(TEXT("bBypass: %s\n"), Settings.bBypass ? TEXT("True") : TEXT("False"));

	// Output parameter
	if (USoundModulationParameter* OutParam = Settings.OutputParameter)
	{
		Output += FString::Printf(TEXT("OutputParameter: %s\n"), *OutParam->GetPathName());
	}
	else
	{
		Output += TEXT("OutputParameter: None\n");
	}

	// Input buses
	Output += FString::Printf(TEXT("InputCount: %d\n"), Settings.Inputs.Num());
	for (int32 i = 0; i < Settings.Inputs.Num(); ++i)
	{
		const FSoundControlModulationInput& Input = Settings.Inputs[i];
		Output += FString::Printf(TEXT("  Input[%d].Bus: %s\n"), i,
			Input.Bus ? *Input.Bus->GetPathName() : TEXT("None"));
		Output += FString::Printf(TEXT("  Input[%d].bSampleAndHold: %s\n"), i,
			Input.bSampleAndHold ? TEXT("True") : TEXT("False"));
		Output += FString::Printf(TEXT("  Input[%d].Transform.Scalar: %.2f\n"), i, Input.Transform.Scalar);
	}

	Output += TEXT("\n=== PROPERTIES ===\n");
	Output += ExportObjectProperties(Patch);

	return Output;
}

//////////////////////////////////////////////////////////////////////////

FString UAIExportCommandlet::ExportGraphToText(UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("(null graph)\n");
	}

	FString Output;

	// Use UE's built-in graph export - same as Copy operation
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

FString UAIExportCommandlet::ExportCDOProperties(UClass* Class)
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

	return ExportObjectProperties(CDO);
}

FString UAIExportCommandlet::ExportObjectProperties(UObject* Object, int32 IndentLevel)
{
	if (!Object)
	{
		return TEXT("(null)\n");
	}

	FString Output;
	FString Indent = FString::ChrN(IndentLevel * 2, TEXT(' '));

	// Get archetype for default value comparison (only when filtering is enabled)
	UObject* Archetype = bFilterDefaultValues ? Object->GetArchetype() : nullptr;

	// Iterate over all properties
	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Skip transient properties
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
		{
			continue;
		}

		// Skip properties that are identical to archetype (only when filtering defaults)
		if (bFilterDefaultValues && Archetype && Property->Identical_InContainer(Object, Archetype))
		{
			continue;
		}

		// Get property value as string
		FString ValueStr;
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);

		Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Object, PPF_None);

		// Skip empty values, "None", empty containers, and null delegates
		if (ValueStr.IsEmpty() ||
			ValueStr == TEXT("None") ||
			ValueStr == TEXT("()") ||
			ValueStr == TEXT("(null).None") ||
			ValueStr.StartsWith(TEXT("(null).")))
		{
			continue;
		}

		// Truncate very long values
		if (ValueStr.Len() > 500)
		{
			ValueStr = ValueStr.Left(500) + TEXT("...(truncated)");
		}

		Output += FString::Printf(TEXT("%s%s=%s\n"), *Indent, *Property->GetName(), *ValueStr);
	}

	return Output;
}

FString UAIExportCommandlet::ExportWidgetTree(UWidget* Widget, int32 IndentLevel)
{
	if (!Widget)
	{
		return TEXT("");
	}

	FString Output;
	FString Indent = FString::ChrN(IndentLevel * 2, TEXT(' '));

	// Widget info
	Output += FString::Printf(TEXT("%s- %s (%s)\n"),
		*Indent,
		*Widget->GetName(),
		*Widget->GetClass()->GetName());

	// If it's a panel, recurse into children
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			if (UWidget* Child = Panel->GetChildAt(i))
			{
				Output += ExportWidgetTree(Child, IndentLevel + 1);
			}
		}
	}

	return Output;
}

bool UAIExportCommandlet::WriteToFile(const FString& Content, const FString& FilePath)
{
	// Ensure directory exists
	FString Directory = FPaths::GetPath(FilePath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*Directory))
	{
		PlatformFile.CreateDirectoryTree(*Directory);
	}

	// Write file
	if (!FFileHelper::SaveStringToFile(Content, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to write file: %s"), *FilePath);
		return false;
	}

	return true;
}

bool UAIExportCommandlet::RunSimplifier(const FString& FilePath)
{
	// Get simplifier script path from plugin
	FString SimplifierPath = GetSimplifierScriptPath();

	if (SimplifierPath.IsEmpty() || !FPaths::FileExists(SimplifierPath))
	{
		UE_LOG(LogAIExport, Warning, TEXT("Simplifier script not found: %s"), *SimplifierPath);
		return true; // Not a fatal error
	}

	// Get Python path from settings
	FString PythonPath = UAIExportSettings::Get()->PythonPath;

	// Run Python script
	FString Args = FString::Printf(TEXT("\"%s\" \"%s\""), *SimplifierPath, *FilePath);

	UE_LOG(LogAIExport, Display, TEXT("Running simplifier: %s %s"), *PythonPath, *Args);

	int32 ReturnCode = 0;
	FString StdOut, StdErr;

	FPlatformProcess::ExecProcess(*PythonPath, *Args, &ReturnCode, &StdOut, &StdErr);

	if (ReturnCode != 0)
	{
		UE_LOG(LogAIExport, Warning, TEXT("Simplifier returned error: %s"), *StdErr);
	}
	else
	{
		UE_LOG(LogAIExport, Display, TEXT("Simplifier output: %s"), *StdOut);
	}

	return true;
}

FString UAIExportCommandlet::GetOutputDirectory() const
{
	// Get from settings
	const UAIExportSettings* Settings = UAIExportSettings::Get();
	if (Settings)
	{
		return Settings->GetOutputDirectoryAbsolute();
	}

	// Fallback to default
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Dev"), TEXT("AIExports"));
}

FString UAIExportCommandlet::GetSimplifierScriptPath() const
{
	FString ScriptsDir = FCommonAIExportModule::GetScriptsDir();
	if (!ScriptsDir.IsEmpty())
	{
		return FPaths::Combine(ScriptsDir, TEXT("simplify_asset.py"));
	}

	// Fallback: try project Scripts folder
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts"), TEXT("simplify_asset.py"));
}

FString UAIExportCommandlet::SanitizeFileName(const FString& InName) const
{
	FString Result = InName;

	// Remove path separators
	Result.ReplaceInline(TEXT("/"), TEXT("_"));
	Result.ReplaceInline(TEXT("\\"), TEXT("_"));

	// Remove invalid characters
	Result.ReplaceInline(TEXT(":"), TEXT("_"));
	Result.ReplaceInline(TEXT("*"), TEXT("_"));
	Result.ReplaceInline(TEXT("?"), TEXT("_"));
	Result.ReplaceInline(TEXT("\""), TEXT("_"));
	Result.ReplaceInline(TEXT("<"), TEXT("_"));
	Result.ReplaceInline(TEXT(">"), TEXT("_"));
	Result.ReplaceInline(TEXT("|"), TEXT("_"));

	return Result;
}
