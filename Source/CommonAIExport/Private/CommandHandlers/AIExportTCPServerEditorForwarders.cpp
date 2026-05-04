// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportEditorCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleEditorWorldInfo()
{
	return CommonAIExport::CommandHandlers::Editor::HandleEditorWorldInfo();
}

FString FAIExportTCPServer::HandleActorList(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorList(Params);
}

FString FAIExportTCPServer::HandleActorSpawn(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorSpawn(Params);
}

FString FAIExportTCPServer::HandleActorSetTransform(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorSetTransform(Params);
}

FString FAIExportTCPServer::HandleActorDelete(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleActorDelete(Params);
}

FString FAIExportTCPServer::HandleLevelOpen(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleLevelOpen(Params);
}

FString FAIExportTCPServer::HandleLevelSaveCurrent()
{
	return CommonAIExport::CommandHandlers::Editor::HandleLevelSaveCurrent();
}

FString FAIExportTCPServer::HandlePIEStatus()
{
	return CommonAIExport::CommandHandlers::Editor::HandlePIEStatus();
}

FString FAIExportTCPServer::HandlePIEStart()
{
	return CommonAIExport::CommandHandlers::Editor::HandlePIEStart();
}

FString FAIExportTCPServer::HandlePIEStop()
{
	return CommonAIExport::CommandHandlers::Editor::HandlePIEStop();
}

FString FAIExportTCPServer::HandleEditorConsoleCommand(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleEditorConsoleCommand(Params);
}

FString FAIExportTCPServer::HandleViewportCapture(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Editor::HandleViewportCapture(Params);
}
