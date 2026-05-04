// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTNiagaraCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleNiagaraAssetInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Niagara::HandleNiagaraAssetInfo(Params);
}
