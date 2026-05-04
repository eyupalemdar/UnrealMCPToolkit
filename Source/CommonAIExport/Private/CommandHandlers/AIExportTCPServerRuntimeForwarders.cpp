// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandHandlers/AIExportRuntimeCommands.h"

#include "Dom/JsonObject.h"

FString FAIExportTCPServer::HandleRuntimeWorldInfo(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeWorldInfo(Params);
}

FString FAIExportTCPServer::HandleRuntimePlayerList(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimePlayerList(Params);
}

FString FAIExportTCPServer::HandleRuntimeComponentList(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeComponentList(Params);
}

FString FAIExportTCPServer::HandleRuntimeDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeInputRouting(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeInputRouting(Params);
}

FString FAIExportTCPServer::HandleRuntimeGameplayTagsDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeGameplayTagsDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeCommonUIDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeCommonUIDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeAudioDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeAudioDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeNavigationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeNavigationDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeAssetStreamingDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeAssetStreamingDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeAsyncLoadDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeAsyncLoadDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeGameInstanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeGameInstanceDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeLevelTravelDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeLevelTravelDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeMultiplayerConnectionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeMultiplayerConnectionDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeTickTimerLatentDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeTickTimerLatentDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeSchedulerPerformanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeSchedulerPerformanceDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimePhysicsCollisionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimePhysicsCollisionDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeReplicationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeReplicationDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeAbilitySystemDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeAbilitySystemDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeAIPerceptionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeAIPerceptionDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeAIControllerDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeAIControllerDiagnostics(Params);
}

FString FAIExportTCPServer::HandleRuntimeEQSDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return CommonAIExport::CommandHandlers::Runtime::HandleRuntimeEQSDiagnostics(Params);
}
