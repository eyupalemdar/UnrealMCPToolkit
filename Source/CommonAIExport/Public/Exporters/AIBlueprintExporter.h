// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/AIExporterBase.h"
#include "AIBlueprintExporter.generated.h"

class UBlueprint;

/**
 * Exporter for UBlueprint assets.
 *
 * Handles standard Blueprints (Actors, Objects, etc.)
 * Does NOT handle specialized blueprints like WidgetBlueprint or AnimBlueprint
 * - those have their own dedicated exporters with higher priority.
 *
 * Export includes:
 * - Parent class information
 * - Class Default Object (CDO) properties
 * - All Blueprint graphs (EventGraph, Functions, Macros)
 *
 * Priority: 50 (standard)
 */
UCLASS()
class COMMONAIEXPORT_API UAIBlueprintExporter : public UAIExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UAIExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 50; }
	virtual FString GetExporterDisplayName() const override { return TEXT("BlueprintExporter"); }
	//~ End UAIExporterBase Interface

protected:
	/**
	 * Export a Blueprint to text.
	 */
	virtual FString ExportBlueprint(UBlueprint* Blueprint, bool bFilterDefaults);

	/**
	 * Export implemented interfaces.
	 */
	FString ExportInterfaces(UBlueprint* Blueprint);

	/**
	 * Export Blueprint variables (member variables).
	 */
	FString ExportVariables(UBlueprint* Blueprint, bool bFilterDefaults);
};
