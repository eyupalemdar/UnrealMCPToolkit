// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/MCTExporterBase.h"
#include "MCTPhysicalMaterialExporter.generated.h"

class UPhysicalMaterial;

/**
 * Exporter for PhysicalMaterial assets.
 *
 * Handles:
 * - UPhysicalMaterial (Friction, Restitution, Density, SurfaceType, etc.)
 * - All subclasses (e.g. UPhysicalMaterialWithTags) via reflection
 *
 * Uses ExportObjectProperties() so all UPROPERTY fields on any subclass
 * are exported automatically without needing a compile-time dependency
 * on the subclass module.
 *
 * Priority: 46 (above Material:45, below Blueprint:50)
 */
UCLASS()
class MCPTOOLKIT_API UMCTPhysicalMaterialExporter : public UMCTExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UMCTExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 46; }
	virtual FString GetExporterDisplayName() const override { return TEXT("PhysicalMaterialExporter"); }
	//~ End UMCTExporterBase Interface

protected:
	FString ExportPhysicalMaterial(UPhysicalMaterial* PhysMat, bool bFilterDefaults);
};
