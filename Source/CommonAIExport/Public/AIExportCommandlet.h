// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AIExportSettings.h"
#include "AIExportCommandlet.generated.h"

class UBlueprint;
class UWidgetBlueprint;
class UAnimBlueprint;
class UDataAsset;
class UInputAction;
class UInputMappingContext;
class UEdGraph;
class UWidget;

// Audio Foundation
class USoundClass;
class USoundSubmix;
class USoundConcurrency;
class USoundAttenuation;

// Audio Modulation
class USoundControlBus;
class USoundControlBusMix;
class USoundModulationPatch;

/**
 * Commandlet for exporting UE assets to text format for AI analysis.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=AIExport -asset="/Game/Path/To/Asset" [-simplify|-raw|-both] [-output="Dir"]
 *
 * Parameters:
 *   -asset       : Asset path to export (required)
 *   -output      : Output directory (optional, defaults to Dev/AIExports)
 *   -raw         : Export raw file only (no simplification)
 *   -simplify    : Export simplified file only (deletes raw after simplification)
 *   -both        : Export both raw and simplified files
 *   -format      : Output format - text or json (optional, defaults to text)
 *
 * Note: If no output mode switch is provided, the mode from Project Settings is used.
 */
UCLASS()
class COMMONAIEXPORT_API UAIExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAIExportCommandlet();

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	/** Parse command line parameters */
	bool ParseParameters(const FString& Params);

	/** Export asset by path */
	bool ExportAsset(const FString& InAssetPath);

	/** Export asset by type (dispatches to specific exporters) */
	bool ExportByType(UObject* Asset);

	// Asset Type Exporters
	FString ExportBlueprint(UBlueprint* Blueprint);
	FString ExportWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint);
	FString ExportAnimBlueprint(UAnimBlueprint* AnimBlueprint);
	FString ExportDataAsset(UDataAsset* DataAsset);
	FString ExportInputAction(UInputAction* InputAction);
	FString ExportInputMappingContext(UInputMappingContext* MappingContext);
	FString ExportGenericObject(UObject* Object);

	// Audio Foundation Exporters
	FString ExportSoundClass(USoundClass* SoundClass);
	FString ExportSoundSubmix(USoundSubmix* Submix);
	FString ExportSoundConcurrency(USoundConcurrency* Concurrency);
	FString ExportSoundAttenuation(USoundAttenuation* Attenuation);

	// Audio Modulation Exporters
	FString ExportSoundControlBus(USoundControlBus* Bus);
	FString ExportSoundControlBusMix(USoundControlBusMix* Mix);
	FString ExportSoundModulationPatch(USoundModulationPatch* Patch);

	// Helper Methods
	FString ExportGraphToText(UEdGraph* Graph);
	FString ExportCDOProperties(UClass* Class);
	FString ExportObjectProperties(UObject* Object, int32 IndentLevel = 0);
	FString ExportWidgetTree(UWidget* RootWidget, int32 IndentLevel = 0);

	/** Write content to file */
	bool WriteToFile(const FString& Content, const FString& FilePath);

	/** Run Python simplifier on exported file */
	bool RunSimplifier(const FString& FilePath);

	/** Get output directory (from settings or default) */
	FString GetOutputDirectory() const;

	/** Get path to simplifier script */
	FString GetSimplifierScriptPath() const;

	/** Sanitize asset name for filename */
	FString SanitizeFileName(const FString& InName) const;

private:
	/** Asset path to export */
	FString AssetPath;

	/** Output directory */
	FString OutputDirectory;

	/** Output mode (RawOnly/SimplifiedOnly/Both) */
	EAIExportOutputMode OutputMode = EAIExportOutputMode::SimplifiedOnly;

	/** Output format (text/json) */
	FString OutputFormat = TEXT("text");

	/** When true, ExportObjectProperties skips default values */
	bool bFilterDefaultValues = false;
};
