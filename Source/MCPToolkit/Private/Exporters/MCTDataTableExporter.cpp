// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTDataTableExporter.h"

#include "Engine/DataTable.h"
#include "UObject/UnrealType.h"

bool UMCTDataTableExporter::CanExport(UObject* Asset) const
{
	return Asset && Asset->IsA<UDataTable>();
}

TArray<UClass*> UMCTDataTableExporter::GetSupportedClasses() const
{
	return { UDataTable::StaticClass() };
}

FString UMCTDataTableExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	UDataTable* DataTable = Cast<UDataTable>(Asset);
	if (!DataTable)
	{
		return TEXT("Error: Not a DataTable asset\n");
	}

	return ExportDataTable(DataTable, bFilterDefaults);
}

FString UMCTDataTableExporter::ExportDataTable(UDataTable* DataTable, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("DATA TABLE: %s"), *DataTable->GetName()));
	Output += FString::Printf(TEXT("Path: %s\n"), *DataTable->GetPathName());
	Output += FString::Printf(TEXT("RowStruct: %s\n"), DataTable->RowStruct ? *DataTable->RowStruct->GetPathName() : TEXT("None"));
	Output += FString::Printf(TEXT("RowCount: %d\n"), DataTable->GetRowMap().Num());

	TArray<FName> RowNames;
	DataTable->GetRowMap().GenerateKeyArray(RowNames);
	RowNames.Sort([](const FName& A, const FName& B)
	{
		return A.ToString() < B.ToString();
	});

	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("ROWS"));
	for (const FName& RowName : RowNames)
	{
		uint8* const* RowDataPtr = DataTable->GetRowMap().Find(RowName);
		Output += ExportRow(DataTable->RowStruct, RowName, RowDataPtr ? *RowDataPtr : nullptr);
	}

	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(DataTable, 0, bFilterDefaults);

	return Output;
}

FString UMCTDataTableExporter::ExportRow(UScriptStruct* RowStruct, const FName& RowName, const uint8* RowData) const
{
	FString Output;
	Output += MakeSubsectionHeader(FString::Printf(TEXT("Row: %s"), *RowName.ToString()));
	if (!RowStruct || !RowData)
	{
		Output += TEXT("  <missing row data>\n");
		return Output;
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
		Output += FString::Printf(TEXT("  %s=%s\n"), *Property->GetName(), *Value);
	}
	return Output;
}
