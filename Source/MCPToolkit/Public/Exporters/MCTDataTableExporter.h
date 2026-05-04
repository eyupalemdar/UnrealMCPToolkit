// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/MCTExporterBase.h"
#include "MCTDataTableExporter.generated.h"

class UDataTable;
class UScriptStruct;

/**
 * Exporter for UDataTable assets.
 *
 * Produces a canonical text export with row struct metadata, sorted row names,
 * and row values in Unreal import-text format.
 */
UCLASS()
class MCPTOOLKIT_API UMCTDataTableExporter : public UMCTExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UMCTExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 50; }
	virtual FString GetExporterDisplayName() const override { return TEXT("DataTableExporter"); }
	//~ End UMCTExporterBase Interface

protected:
	FString ExportDataTable(UDataTable* DataTable, bool bFilterDefaults);
	FString ExportRow(UScriptStruct* RowStruct, const FName& RowName, const uint8* RowData) const;
};
