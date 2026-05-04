// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportNiagaraCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleNiagaraAssetInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Niagara::HandleNiagaraAssetInfo(Params);
}
