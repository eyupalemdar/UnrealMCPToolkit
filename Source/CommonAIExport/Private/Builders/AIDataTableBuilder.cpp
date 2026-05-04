// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/AIDataTableBuilder.h"

#include "Builders/AIDataAssetBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

FString UAIDataTableBuilder::JsonValueToImportText(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return TEXT("");
	}

	FString StringValue;
	if (Value->TryGetString(StringValue))
	{
		return StringValue;
	}

	double NumberValue = 0.0;
	if (Value->TryGetNumber(NumberValue))
	{
		return FString::SanitizeFloat(NumberValue);
	}

	bool BoolValue = false;
	if (Value->TryGetBool(BoolValue))
	{
		return BoolValue ? TEXT("true") : TEXT("false");
	}

	const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
	if (Value->TryGetObject(ObjectValue) && ObjectValue && ObjectValue->IsValid())
	{
		FString ExplicitImportText;
		if ((*ObjectValue)->TryGetStringField(TEXT("_import_text"), ExplicitImportText))
		{
			return ExplicitImportText;
		}
	}

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT(""), Writer);
	return Serialized;
}

UDataTable* UAIDataTableBuilder::LoadDataTable(const FString& AssetPath, FString& OutError)
{
	UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("DataTable not found: %s"), *AssetPath);
	}
	return DataTable;
}

TSharedPtr<FJsonObject> UAIDataTableBuilder::BuildStructRowJson(UScriptStruct* RowStruct, const uint8* RowData)
{
	TSharedPtr<FJsonObject> RowJson = MakeShared<FJsonObject>();
	if (!RowStruct || !RowData)
	{
		return RowJson;
	}

	for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || Property->HasAnyPropertyFlags(CPF_Transient))
		{
			continue;
		}

		FString Value;
		Property->ExportText_Direct(Value, Property->ContainerPtrToValuePtr<void>(RowData), nullptr, nullptr, PPF_None);
		RowJson->SetStringField(Property->GetName(), Value);
	}
	return RowJson;
}

TSharedPtr<FJsonObject> UAIDataTableBuilder::BuildDataTableInfoJson(UDataTable* DataTable)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!DataTable)
	{
		return Data;
	}

	Data->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	Data->SetStringField(TEXT("row_struct"), DataTable->RowStruct ? DataTable->RowStruct->GetPathName() : TEXT(""));
	Data->SetNumberField(TEXT("row_count"), DataTable->GetRowMap().Num());

	TArray<TSharedPtr<FJsonValue>> RowNames;
	for (const TPair<FName, uint8*>& Pair : DataTable->GetRowMap())
	{
		RowNames.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
	}
	Data->SetArrayField(TEXT("row_names"), RowNames);
	return Data;
}

TSharedPtr<FJsonObject> UAIDataTableBuilder::CreateDataTable(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& RowStructPath,
	FString& OutError)
{
	UScriptStruct* RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
	if (!RowStruct)
	{
		OutError = FString::Printf(TEXT("Row struct not found: %s"), *RowStructPath);
		return nullptr;
	}

	const FString PackageName = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Could not create package: %s"), *PackageName);
		return nullptr;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "CreateDataTable", "AI Create DataTable"));
	UDataTable* DataTable = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("Could not create DataTable object: %s"), *PackageName);
		return nullptr;
	}

	DataTable->RowStruct = RowStruct;
	DataTable->Modify();
	FAssetRegistryModule::AssetCreated(DataTable);
	Package->MarkPackageDirty();

	TSharedPtr<FJsonObject> Data = BuildDataTableInfoJson(DataTable);
	Data->SetBoolField(TEXT("created"), true);
	return Data;
}

TSharedPtr<FJsonObject> UAIDataTableBuilder::ReadRows(
	const FString& AssetPath,
	const FString& RowNameFilter,
	const int32 Limit,
	FString& OutError)
{
	UDataTable* DataTable = LoadDataTable(AssetPath, OutError);
	if (!DataTable)
	{
		return nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 MatchedCount = 0;
	for (const TPair<FName, uint8*>& Pair : DataTable->GetRowMap())
	{
		const FString RowName = Pair.Key.ToString();
		if (!RowNameFilter.IsEmpty() && RowName != RowNameFilter)
		{
			continue;
		}

		++MatchedCount;
		if (Rows.Num() >= Limit)
		{
			continue;
		}

		TSharedPtr<FJsonObject> RowJson = MakeShared<FJsonObject>();
		RowJson->SetStringField(TEXT("row_name"), RowName);
		RowJson->SetObjectField(TEXT("values"), BuildStructRowJson(DataTable->RowStruct, Pair.Value));
		Rows.Add(MakeShared<FJsonValueObject>(RowJson));
	}

	TSharedPtr<FJsonObject> Data = BuildDataTableInfoJson(DataTable);
	Data->SetArrayField(TEXT("rows"), Rows);
	Data->SetNumberField(TEXT("matched_count"), MatchedCount);
	Data->SetNumberField(TEXT("returned_count"), Rows.Num());
	Data->SetBoolField(TEXT("truncated"), MatchedCount > Rows.Num());
	return Data;
}

TSharedPtr<FJsonObject> UAIDataTableBuilder::AddRow(
	const FString& AssetPath,
	const FString& RowName,
	TSharedPtr<FJsonObject> Values,
	const bool bSave,
	FString& OutError)
{
	UDataTable* DataTable = LoadDataTable(AssetPath, OutError);
	if (!DataTable || !DataTable->RowStruct)
	{
		if (DataTable && OutError.IsEmpty())
		{
			OutError = TEXT("DataTable has no RowStruct");
		}
		return nullptr;
	}
	if (!Values.IsValid())
	{
		OutError = TEXT("Missing 'values' object parameter");
		return nullptr;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "AddDataTableRow", "AI Add DataTable Row"));
	TArray<uint8> RowBuffer;
	RowBuffer.SetNumZeroed(DataTable->RowStruct->GetStructureSize());
	uint8* RowData = RowBuffer.GetData();
	DataTable->RowStruct->InitializeStruct(RowData);
	ON_SCOPE_EXIT
	{
		DataTable->RowStruct->DestroyStruct(RowData);
	};

	if (uint8* const* ExistingRowPtr = DataTable->GetRowMap().Find(FName(*RowName)))
	{
		if (*ExistingRowPtr)
		{
			DataTable->RowStruct->CopyScriptStruct(RowData, *ExistingRowPtr);
		}
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values->Values)
	{
		FProperty* Property = DataTable->RowStruct->FindPropertyByName(FName(*Pair.Key));
		if (!Property)
		{
			OutError = FString::Printf(TEXT("Row property not found: %s"), *Pair.Key);
			return nullptr;
		}

		const FString ImportText = JsonValueToImportText(Pair.Value);
		if (!Property->ImportText_Direct(*ImportText, Property->ContainerPtrToValuePtr<void>(RowData), DataTable, PPF_None))
		{
			OutError = FString::Printf(TEXT("Failed to import row property %s"), *Pair.Key);
			return nullptr;
		}
	}

	DataTable->Modify();
	DataTable->AddRow(FName(*RowName), *reinterpret_cast<FTableRowBase*>(RowData));
	DataTable->MarkPackageDirty();
	const bool bSaved = bSave ? SaveDataTable(DataTable) : false;

	TSharedPtr<FJsonObject> Data = BuildDataTableInfoJson(DataTable);
	Data->SetBoolField(TEXT("row_added_or_updated"), true);
	Data->SetStringField(TEXT("row_name"), RowName);
	Data->SetBoolField(TEXT("saved"), bSaved);
	return Data;
}

TSharedPtr<FJsonObject> UAIDataTableBuilder::RemoveRow(
	const FString& AssetPath,
	const FString& RowName,
	const bool bSave,
	FString& OutError)
{
	UDataTable* DataTable = LoadDataTable(AssetPath, OutError);
	if (!DataTable)
	{
		return nullptr;
	}

	const bool bExisted = DataTable->GetRowMap().Contains(FName(*RowName));
	const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "RemoveDataTableRow", "AI Remove DataTable Row"));
	DataTable->Modify();
	DataTable->RemoveRow(FName(*RowName));
	DataTable->MarkPackageDirty();
	const bool bSaved = bSave ? SaveDataTable(DataTable) : false;

	TSharedPtr<FJsonObject> Data = BuildDataTableInfoJson(DataTable);
	Data->SetBoolField(TEXT("removed"), bExisted);
	Data->SetStringField(TEXT("row_name"), RowName);
	Data->SetBoolField(TEXT("saved"), bSaved);
	return Data;
}

TSharedPtr<FJsonObject> UAIDataTableBuilder::ImportCsv(
	const FString& AssetPath,
	const FString& CsvText,
	const FString& CsvFilePath,
	const bool bSave,
	FString& OutError)
{
	UDataTable* DataTable = LoadDataTable(AssetPath, OutError);
	if (!DataTable)
	{
		return nullptr;
	}

	FString EffectiveCsvText = CsvText;
	if (EffectiveCsvText.IsEmpty())
	{
		if (!FFileHelper::LoadFileToString(EffectiveCsvText, *CsvFilePath))
		{
			OutError = FString::Printf(TEXT("Could not read CSV file: %s"), *CsvFilePath);
			return nullptr;
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ImportDataTableCsv", "AI Import DataTable CSV"));
	DataTable->Modify();
	const TArray<FString> Problems = DataTable->CreateTableFromCSVString(EffectiveCsvText);
	DataTable->MarkPackageDirty();
	const bool bSaved = bSave ? SaveDataTable(DataTable) : false;

	TArray<TSharedPtr<FJsonValue>> ProblemArray;
	for (const FString& Problem : Problems)
	{
		ProblemArray.Add(MakeShared<FJsonValueString>(Problem));
	}

	TSharedPtr<FJsonObject> Data = BuildDataTableInfoJson(DataTable);
	Data->SetBoolField(TEXT("imported"), Problems.Num() == 0);
	Data->SetBoolField(TEXT("saved"), bSaved);
	Data->SetStringField(TEXT("csv_file_path"), CsvFilePath);
	Data->SetNumberField(TEXT("problem_count"), Problems.Num());
	Data->SetArrayField(TEXT("problems"), ProblemArray);

	if (!Problems.IsEmpty())
	{
		OutError = FString::Printf(TEXT("CSV import completed with %d problems"), Problems.Num());
	}
	return Data;
}

bool UAIDataTableBuilder::SaveDataTable(UDataTable* DataTable)
{
	return DataTable && UAIDataAssetBuilder::SaveAsset(DataTable);
}
