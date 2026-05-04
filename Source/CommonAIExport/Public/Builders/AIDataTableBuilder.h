// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AIDataTableBuilder.generated.h"

class FJsonObject;
class FJsonValue;
class UDataTable;
class UScriptStruct;

/**
 * Static utility class for DataTable asset authoring and inspection.
 *
 * Command handlers own transport concerns; this builder owns DataTable asset
 * creation, row serialization, row mutation, CSV import, and save bookkeeping.
 * Call from the Game Thread only.
 */
UCLASS()
class COMMONAIEXPORT_API UAIDataTableBuilder : public UObject
{
	GENERATED_BODY()

public:
	/** Load a UDataTable asset by path. */
	static UDataTable* LoadDataTable(const FString& AssetPath, FString& OutError);

	/** Build a summary for a DataTable asset. */
	static TSharedPtr<FJsonObject> BuildDataTableInfoJson(UDataTable* DataTable);

	/** Create a new DataTable asset with the supplied row struct. */
	static TSharedPtr<FJsonObject> CreateDataTable(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& RowStructPath,
		FString& OutError);

	/** Read DataTable row values as import-text strings. */
	static TSharedPtr<FJsonObject> ReadRows(
		const FString& AssetPath,
		const FString& RowNameFilter,
		int32 Limit,
		FString& OutError);

	/** Add or update a row. */
	static TSharedPtr<FJsonObject> AddRow(
		const FString& AssetPath,
		const FString& RowName,
		TSharedPtr<FJsonObject> Values,
		bool bSave,
		FString& OutError);

	/** Remove a row. */
	static TSharedPtr<FJsonObject> RemoveRow(
		const FString& AssetPath,
		const FString& RowName,
		bool bSave,
		FString& OutError);

	/** Import CSV text or a CSV file into the table. */
	static TSharedPtr<FJsonObject> ImportCsv(
		const FString& AssetPath,
		const FString& CsvText,
		const FString& CsvFilePath,
		bool bSave,
		FString& OutError);

private:
	static FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value);
	static TSharedPtr<FJsonObject> BuildStructRowJson(UScriptStruct* RowStruct, const uint8* RowData);
	static bool SaveDataTable(UDataTable* DataTable);
};
