// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/AIExporterBase.h"
#include "AIInputExporter.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Exporter for Enhanced Input assets.
 *
 * Handles:
 * - UInputAction
 * - UInputMappingContext
 *
 * Priority: 50 (standard)
 */
UCLASS()
class COMMONAIEXPORT_API UAIInputExporter : public UAIExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UAIExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 50; }
	virtual FString GetExporterDisplayName() const override { return TEXT("InputExporter"); }
	//~ End UAIExporterBase Interface

protected:
	/**
	 * Export an Input Action.
	 */
	FString ExportInputAction(UInputAction* InputAction, bool bFilterDefaults);

	/**
	 * Export an Input Mapping Context.
	 */
	FString ExportInputMappingContext(UInputMappingContext* MappingContext, bool bFilterDefaults);
};
