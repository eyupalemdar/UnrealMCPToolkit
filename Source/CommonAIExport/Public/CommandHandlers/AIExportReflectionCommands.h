// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::Reflection
{
FString HandleObjectQuery(TSharedPtr<FJsonObject> Params);
FString HandleObjectGetProperty(TSharedPtr<FJsonObject> Params);
FString HandleObjectSetProperty(TSharedPtr<FJsonObject> Params);
FString HandleObjectCallFunction(TSharedPtr<FJsonObject> Params);
FString HandleReflectClass(TSharedPtr<FJsonObject> Params);
FString HandleReflectStruct(TSharedPtr<FJsonObject> Params);
FString HandleReflectEnum(TSharedPtr<FJsonObject> Params);
FString HandleListClasses(TSharedPtr<FJsonObject> Params);
FString HandleListGameplayTags(TSharedPtr<FJsonObject> Params);
}
