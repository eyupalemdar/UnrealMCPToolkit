// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTTcpServer.h"
#include "CommandHandlers/MCTRuntimeCommands.h"

#include "Dom/JsonObject.h"

FString FMCTTcpServer::HandleRuntimeWorldInfo(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeWorldInfo(Params);
}

FString FMCTTcpServer::HandleRuntimePlayerList(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimePlayerList(Params);
}

FString FMCTTcpServer::HandleRuntimeComponentList(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeComponentList(Params);
}

FString FMCTTcpServer::HandleRuntimeDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeInputRouting(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeInputRouting(Params);
}

FString FMCTTcpServer::HandleRuntimeGameplayTagsDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeGameplayTagsDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeCommonUIDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeCommonUIDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeAudioDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeAudioDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeNavigationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeNavigationDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeAssetStreamingDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeAssetStreamingDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeAsyncLoadDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeAsyncLoadDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeGameInstanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeGameInstanceDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeLevelTravelDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeLevelTravelDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeMultiplayerConnectionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeMultiplayerConnectionDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeTickTimerLatentDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeTickTimerLatentDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeSchedulerPerformanceDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeSchedulerPerformanceDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimePhysicsCollisionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimePhysicsCollisionDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeReplicationDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeReplicationDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeAbilitySystemDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeAbilitySystemDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeAIPerceptionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeAIPerceptionDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeAIControllerDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeAIControllerDiagnostics(Params);
}

FString FMCTTcpServer::HandleRuntimeEQSDiagnostics(TSharedPtr<FJsonObject> Params)
{
	return MCPToolkit::CommandHandlers::Runtime::HandleRuntimeEQSDiagnostics(Params);
}
