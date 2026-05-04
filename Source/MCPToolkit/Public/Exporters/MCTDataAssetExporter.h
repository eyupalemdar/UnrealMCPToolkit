// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/MCTExporterBase.h"
#include "MCTDataAssetExporter.generated.h"

class UDataAsset;

/**
 * Exporter for UDataAsset and derived assets.
 *
 * Handles generic Data Assets (UDataAsset and any subclass).
 * Exports all UPROPERTY values in a readable format.
 *
 * Priority: 40 (lower than Blueprint to avoid catching DataAsset BPs)
 */
UCLASS()
class MCPTOOLKIT_API UMCTDataAssetExporter : public UMCTExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UMCTExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 40; }
	virtual FString GetExporterDisplayName() const override { return TEXT("DataAssetExporter"); }
	//~ End UMCTExporterBase Interface

protected:
	/**
	 * Export a DataAsset to text.
	 */
	FString ExportDataAsset(UDataAsset* DataAsset, bool bFilterDefaults);
};
