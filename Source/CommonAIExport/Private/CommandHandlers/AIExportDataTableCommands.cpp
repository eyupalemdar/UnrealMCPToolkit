// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportDataTableCommands.h"

#include "Builders/AIDataAssetBuilder.h"
#include "CommandHandlers/AIExportCommandResponse.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace CommonAIExport::CommandHandlers::DataTable
{
namespace
{
FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value)
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

UDataTable* LoadDataTable(const FString& AssetPath, FString& OutError)
{
	UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("DataTable not found: %s"), *AssetPath);
	}
	return DataTable;
}

TSharedPtr<FJsonObject> BuildStructRowJson(UScriptStruct* RowStruct, const uint8* RowData)
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

TSharedPtr<FJsonObject> BuildDataTableInfoJson(UDataTable* DataTable)
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

bool SaveDataTable(UDataTable* DataTable)
{
	return DataTable && UAIDataAssetBuilder::SaveAsset(DataTable);
}
}

FString HandleCreateDataTable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath;
	FString AssetName;
	FString RowStructPath;
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath) || PackagePath.IsEmpty())
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	if (!Params->TryGetStringField(TEXT("row_struct_path"), RowStructPath) || RowStructPath.IsEmpty())
		return CreateErrorResponse(TEXT("Missing 'row_struct_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, RowStructPath, Promise]()
	{
		UScriptStruct* RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
		if (!RowStruct)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Row struct not found: %s"), *RowStructPath)));
			return;
		}

		const FString PackageName = PackagePath / AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not create package: %s"), *PackageName)));
			return;
		}

		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "CreateDataTable", "AI Create DataTable"));
		UDataTable* DataTable = NewObject<UDataTable>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		DataTable->RowStruct = RowStruct;
		DataTable->Modify();
		FAssetRegistryModule::AssetCreated(DataTable);
		Package->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = BuildDataTableInfoJson(DataTable);
		Data->SetBoolField(TEXT("created"), true);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create DataTable timed out"));
	return Future.Get();
}

FString HandleGetDataTableInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise]()
	{
		FString Error;
		UDataTable* DataTable = LoadDataTable(AssetPath, Error);
		Promise->SetValue(DataTable ? CreateSuccessResponse(BuildDataTableInfoJson(DataTable)) : CreateErrorResponse(Error));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get DataTable info timed out"));
	return Future.Get();
}

FString HandleReadDataTableRows(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	FString RowNameFilter;
	int32 Limit = 1000;
	Params->TryGetStringField(TEXT("row_name"), RowNameFilter);
	double LimitValue = 0.0;
	if (Params->TryGetNumberField(TEXT("limit"), LimitValue) && LimitValue > 0.0)
	{
		Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, 10000);
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [AssetPath, RowNameFilter, Limit, Promise]()
	{
		FString Error;
		UDataTable* DataTable = LoadDataTable(AssetPath, Error);
		if (!DataTable)
		{
			Promise->SetValue(CreateErrorResponse(Error));
			return;
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
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Read DataTable rows timed out"));
	return Future.Get();
}

FString HandleAddDataTableRow(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	FString RowName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("row_name"), RowName) || RowName.IsEmpty())
		return CreateErrorResponse(TEXT("Missing 'row_name' parameter"));

	const TSharedPtr<FJsonObject>* ValuesObject = nullptr;
	if (!Params->TryGetObjectField(TEXT("values"), ValuesObject) || !ValuesObject || !ValuesObject->IsValid())
		return CreateErrorResponse(TEXT("Missing 'values' object parameter"));

	bool bSave = true;
	Params->TryGetBoolField(TEXT("save"), bSave);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [AssetPath, RowName, Values = *ValuesObject, bSave, Promise]()
	{
		FString Error;
		UDataTable* DataTable = LoadDataTable(AssetPath, Error);
		if (!DataTable || !DataTable->RowStruct)
		{
			Promise->SetValue(CreateErrorResponse(DataTable ? TEXT("DataTable has no RowStruct") : Error));
			return;
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
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Row property not found: %s"), *Pair.Key)));
				return;
			}

			const FString ImportText = JsonValueToImportText(Pair.Value);
			if (!Property->ImportText_Direct(*ImportText, Property->ContainerPtrToValuePtr<void>(RowData), DataTable, PPF_None))
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to import row property %s"), *Pair.Key)));
				return;
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
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add DataTable row timed out"));
	return Future.Get();
}

FString HandleRemoveDataTableRow(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	FString RowName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("row_name"), RowName) || RowName.IsEmpty())
		return CreateErrorResponse(TEXT("Missing 'row_name' parameter"));

	bool bSave = true;
	Params->TryGetBoolField(TEXT("save"), bSave);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [AssetPath, RowName, bSave, Promise]()
	{
		FString Error;
		UDataTable* DataTable = LoadDataTable(AssetPath, Error);
		if (!DataTable)
		{
			Promise->SetValue(CreateErrorResponse(Error));
			return;
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
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove DataTable row timed out"));
	return Future.Get();
}

FString HandleImportDataTableCsv(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	FString CsvText;
	FString CsvFilePath;
	Params->TryGetStringField(TEXT("csv_text"), CsvText);
	Params->TryGetStringField(TEXT("csv_file_path"), CsvFilePath);
	if (CsvText.IsEmpty() && CsvFilePath.IsEmpty())
		return CreateErrorResponse(TEXT("Expected 'csv_text' or 'csv_file_path'"));

	bool bSave = true;
	Params->TryGetBoolField(TEXT("save"), bSave);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [AssetPath, CsvText, CsvFilePath, bSave, Promise]()
	{
		FString Error;
		UDataTable* DataTable = LoadDataTable(AssetPath, Error);
		if (!DataTable)
		{
			Promise->SetValue(CreateErrorResponse(Error));
			return;
		}

		FString EffectiveCsvText = CsvText;
		if (EffectiveCsvText.IsEmpty())
		{
			if (!FFileHelper::LoadFileToString(EffectiveCsvText, *CsvFilePath))
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not read CSV file: %s"), *CsvFilePath)));
				return;
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
		Promise->SetValue(Problems.Num() == 0 ? CreateSuccessResponse(Data) : CreateErrorResponse(FString::Printf(TEXT("CSV import completed with %d problems"), Problems.Num())));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Import DataTable CSV timed out"));
	return Future.Get();
}
}
