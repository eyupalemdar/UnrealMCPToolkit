// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonAIExport::CommandHandlers::BlueprintGraph
{
FString HandleAddEventNode(TSharedPtr<FJsonObject> Params);
FString HandleAddCustomEvent(TSharedPtr<FJsonObject> Params);
FString HandleAddFunctionCallNode(TSharedPtr<FJsonObject> Params);
FString HandleAddVariableGetNode(TSharedPtr<FJsonObject> Params);
FString HandleAddVariableSetNode(TSharedPtr<FJsonObject> Params);
FString HandleAddMakeStructNode(TSharedPtr<FJsonObject> Params);
FString HandleAddBranchNode(TSharedPtr<FJsonObject> Params);
FString HandleAddCallParentFunction(TSharedPtr<FJsonObject> Params);
FString HandleEnsureFunctionGraph(TSharedPtr<FJsonObject> Params);
FString HandleConnectPins(TSharedPtr<FJsonObject> Params);
FString HandleSetPinDefault(TSharedPtr<FJsonObject> Params);
FString HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params);
FString HandleGetGraph(TSharedPtr<FJsonObject> Params);
FString HandleListGraphs(TSharedPtr<FJsonObject> Params);
FString HandleAddVariable(TSharedPtr<FJsonObject> Params);
FString HandleSetVariableDefault(TSharedPtr<FJsonObject> Params);
FString HandleRemoveVariable(TSharedPtr<FJsonObject> Params);
FString HandleGetVariables(TSharedPtr<FJsonObject> Params);
}
