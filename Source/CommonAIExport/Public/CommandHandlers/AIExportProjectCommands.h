// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::Project
{
FString HandleProjectInfo(TSharedPtr<FJsonObject> Params);
FString HandleProjectPluginList(TSharedPtr<FJsonObject> Params);
FString HandleProjectPluginSetEnabled(TSharedPtr<FJsonObject> Params);
FString HandleProjectModuleList(TSharedPtr<FJsonObject> Params);
FString HandleProjectConfigGet(TSharedPtr<FJsonObject> Params);
FString HandleProjectConfigSet(TSharedPtr<FJsonObject> Params);
FString HandleProjectConfigDelete(TSharedPtr<FJsonObject> Params);
FString HandleProjectConfigListSections(TSharedPtr<FJsonObject> Params);
FString HandleProjectConfigListKeys(TSharedPtr<FJsonObject> Params);
}
