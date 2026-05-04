// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::DataTable
{
FString HandleCreateDataTable(TSharedPtr<FJsonObject> Params);
FString HandleGetDataTableInfo(TSharedPtr<FJsonObject> Params);
FString HandleReadDataTableRows(TSharedPtr<FJsonObject> Params);
FString HandleAddDataTableRow(TSharedPtr<FJsonObject> Params);
FString HandleRemoveDataTableRow(TSharedPtr<FJsonObject> Params);
FString HandleImportDataTableCsv(TSharedPtr<FJsonObject> Params);
}
