// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportSequencerCommands.h"
#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleSequencerAssetInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Sequencer::HandleSequencerAssetInfo(Params);
}
