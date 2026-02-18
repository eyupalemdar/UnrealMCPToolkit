// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/AIBlueprintExporter.h"
#include "AIAnimBlueprintExporter.generated.h"

class UAnimBlueprint;

/**
 * Exporter for UAnimBlueprint assets.
 *
 * Extends Blueprint exporter with AnimBlueprint-specific functionality:
 * - Target skeleton reference
 * - AnimGraph summary (state machines, blend spaces)
 * - Anim notifies
 * - Linked anim layers
 * - Slot names
 *
 * Priority: 90 (between WidgetBlueprint:100 and Blueprint:50)
 */
UCLASS()
class COMMONAIEXPORT_API UAIAnimBlueprintExporter : public UAIBlueprintExporter
{
	GENERATED_BODY()

public:
	//~ Begin UAIExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 90; }
	virtual FString GetExporterDisplayName() const override { return TEXT("AnimBlueprintExporter"); }
	//~ End UAIExporterBase Interface

protected:
	/**
	 * Export an AnimBlueprint to text.
	 */
	FString ExportAnimBlueprint(UAnimBlueprint* AnimBP, bool bFilterDefaults);

	/**
	 * Export AnimGraph summary: state machines, blend spaces, etc.
	 */
	FString ExportAnimGraphSummary(UAnimBlueprint* AnimBP);

	/**
	 * Export anim notifies defined in the AnimBlueprint.
	 */
	FString ExportAnimNotifies(UAnimBlueprint* AnimBP);

	/**
	 * Export linked anim layer interfaces.
	 */
	FString ExportLinkedAnimLayers(UAnimBlueprint* AnimBP);

	/**
	 * Export anim slot names used by the AnimBlueprint.
	 */
	FString ExportSlotNames(UAnimBlueprint* AnimBP);
};
