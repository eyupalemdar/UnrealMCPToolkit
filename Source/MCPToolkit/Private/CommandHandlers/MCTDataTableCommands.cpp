// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTDataTableCommands.h"

#include "Builders/MCTDataTableBuilder.h"
#include "CommandHandlers/MCTCommandResponse.h"

#include "Async/Async.h"

namespace MCPToolkit::CommandHandlers::DataTable
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName)
{
	FString Value;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const bool bDefault)
{
	bool bValue = bDefault;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

FString CreateBuilderErrorResponse(const FString& Error, const TCHAR* Fallback)
{
	return CreateErrorResponse(Error.IsEmpty() ? FString(Fallback) : Error);
}

FString RunOnGameThread(TFunction<FString()>&& Work, const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, Work = MoveTemp(Work)]()
	{
		Promise->SetValue(Work());
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleCreateDataTable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString PackagePath = ReadStringField(Params, TEXT("package_path"));
	const FString AssetName = ReadStringField(Params, TEXT("asset_name"));
	const FString RowStructPath = ReadStringField(Params, TEXT("row_struct_path"));
	if (PackagePath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	}
	if (AssetName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	}
	if (RowStructPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'row_struct_path' parameter"));
	}

	return RunOnGameThread([PackagePath, AssetName, RowStructPath]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTDataTableBuilder::CreateDataTable(PackagePath, AssetName, RowStructPath, Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to create DataTable"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Create DataTable timed out"));
}

FString HandleGetDataTableInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	return RunOnGameThread([AssetPath]()
	{
		FString Error;
		UDataTable* DataTable = UMCTDataTableBuilder::LoadDataTable(AssetPath, Error);
		if (!DataTable)
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to load DataTable"));
		}
		return CreateSuccessResponse(UMCTDataTableBuilder::BuildDataTableInfoJson(DataTable));
	}, TEXT("Get DataTable info timed out"));
}

FString HandleReadDataTableRows(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	const FString RowNameFilter = ReadStringField(Params, TEXT("row_name"));
	int32 Limit = 1000;
	double LimitValue = 0.0;
	if (Params->TryGetNumberField(TEXT("limit"), LimitValue) && LimitValue > 0.0)
	{
		Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, 10000);
	}

	return RunOnGameThread([AssetPath, RowNameFilter, Limit]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTDataTableBuilder::ReadRows(AssetPath, RowNameFilter, Limit, Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to read DataTable rows"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Read DataTable rows timed out"));
}

FString HandleAddDataTableRow(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString RowName = ReadStringField(Params, TEXT("row_name"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (RowName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'row_name' parameter"));
	}

	const TSharedPtr<FJsonObject>* ValuesObject = nullptr;
	if (!Params->TryGetObjectField(TEXT("values"), ValuesObject) || !ValuesObject || !ValuesObject->IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'values' object parameter"));
	}

	const bool bSave = ReadBoolField(Params, TEXT("save"), true);

	return RunOnGameThread([AssetPath, RowName, Values = *ValuesObject, bSave]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTDataTableBuilder::AddRow(AssetPath, RowName, Values, bSave, Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to add DataTable row"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Add DataTable row timed out"));
}

FString HandleRemoveDataTableRow(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString RowName = ReadStringField(Params, TEXT("row_name"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (RowName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'row_name' parameter"));
	}

	const bool bSave = ReadBoolField(Params, TEXT("save"), true);

	return RunOnGameThread([AssetPath, RowName, bSave]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTDataTableBuilder::RemoveRow(AssetPath, RowName, bSave, Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to remove DataTable row"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Remove DataTable row timed out"));
}

FString HandleImportDataTableCsv(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	const FString CsvText = ReadStringField(Params, TEXT("csv_text"));
	const FString CsvFilePath = ReadStringField(Params, TEXT("csv_file_path"));
	if (CsvText.IsEmpty() && CsvFilePath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Expected 'csv_text' or 'csv_file_path'"));
	}

	const bool bSave = ReadBoolField(Params, TEXT("save"), true);

	return RunOnGameThread([AssetPath, CsvText, CsvFilePath, bSave]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTDataTableBuilder::ImportCsv(AssetPath, CsvText, CsvFilePath, bSave, Error);
		if (!Error.IsEmpty())
		{
			return CreateErrorResponse(Error);
		}
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to import DataTable CSV"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Import DataTable CSV timed out"));
}
}
