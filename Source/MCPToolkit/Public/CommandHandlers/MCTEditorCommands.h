// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPToolkit::CommandHandlers::Editor
{
FString HandleEditorWorldInfo();
FString HandleActorList(TSharedPtr<FJsonObject> Params);
FString HandleActorSpawn(TSharedPtr<FJsonObject> Params);
FString HandleActorSetTransform(TSharedPtr<FJsonObject> Params);
FString HandleActorDelete(TSharedPtr<FJsonObject> Params);
FString HandleLevelOpen(TSharedPtr<FJsonObject> Params);
FString HandleLevelSaveCurrent();
FString HandlePIEStatus();
FString HandlePIEStart();
FString HandlePIEStop();
FString HandleEditorConsoleCommand(TSharedPtr<FJsonObject> Params);
FString HandleViewportCapture(TSharedPtr<FJsonObject> Params);
}
