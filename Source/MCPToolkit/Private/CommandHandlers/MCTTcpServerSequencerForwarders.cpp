// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTSequencerCommands.h"
#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleSequencerAssetInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Sequencer::HandleSequencerAssetInfo(Params);
}
