// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::CDO
{
FString HandleSetCDOProperty(TSharedPtr<FJsonObject> Params);
FString HandleGetCDOProperties(TSharedPtr<FJsonObject> Params);
FString HandleAddCDOArrayElement(TSharedPtr<FJsonObject> Params);
FString HandleSetCDOArrayElementProperty(TSharedPtr<FJsonObject> Params);
FString HandleRemoveCDOArrayElement(TSharedPtr<FJsonObject> Params);
FString HandleGetCDOArrayLength(TSharedPtr<FJsonObject> Params);
}
