// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::Workflow
{
FString HandleSourceControlStatus(TSharedPtr<FJsonObject> Params);
FString HandleSourceControlLog(TSharedPtr<FJsonObject> Params);
FString HandleSourceControlShow(TSharedPtr<FJsonObject> Params);
FString HandleSourceControlDiff(TSharedPtr<FJsonObject> Params);
FString HandleEditorLogRead(TSharedPtr<FJsonObject> Params);
FString HandleBuildProject(TSharedPtr<FJsonObject> Params);
FString HandleGenerateProjectFiles(TSharedPtr<FJsonObject> Params);
FString HandleCookProject(TSharedPtr<FJsonObject> Params);
FString HandleListTests(TSharedPtr<FJsonObject> Params);
FString HandleRunTests(TSharedPtr<FJsonObject> Params);
FString HandleGetTestLog(TSharedPtr<FJsonObject> Params);
}
