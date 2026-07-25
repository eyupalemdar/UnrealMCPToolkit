// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTModule.h"
#include "CommandDispatch/MCTGameThreadDispatcher.h"
#include "MCTExportContextMenu.h"
#include "MCTTcpServer.h"
#include "Interfaces/IPluginManager.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY(LogMCT);

#define LOCTEXT_NAMESPACE "FMCTModule"

void FMCTModule::StartupModule()
{
	UE_LOG(LogMCT, Log, TEXT("MCPToolkit module started"));

	// Register context menu after ToolMenus is ready
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
		this, &FMCTModule::RegisterContextMenu));

	// Register the safe Game Thread consumer before any transport can accept
	// commands. This prevents remote work from falling back to TaskGraph
	// reentrancy while Slate or render-asset streaming is suspended.
	FMCTGameThreadDispatcher::Get().Startup();

	// Start TCP server for external automation commands.
	FMCTTcpServerManager::Start();
}

void FMCTModule::ShutdownModule()
{
	// Reject new work and complete queued promises before joining transport
	// threads. Commands that have already started are never interrupted.
	FMCTGameThreadDispatcher::Get().BeginShutdown();

	// Stop TCP/HTTP servers before removing the Game Thread ticker.
	FMCTTcpServerManager::Stop();
	FMCTGameThreadDispatcher::Get().Shutdown();

	// Unregister context menu (guard against UObject system already torn down)
	if (UObjectInitialized())
	{
		UToolMenus::UnRegisterStartupCallback(this);
		FMCTExportContextMenu::Unregister();
	}

	UE_LOG(LogMCT, Log, TEXT("MCPToolkit module shutdown"));
}

void FMCTModule::RegisterContextMenu()
{
	FMCTExportContextMenu::Register();
}

FString FMCTModule::GetPluginDir()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MCPToolkit"));
	if (Plugin.IsValid())
	{
		return Plugin->GetBaseDir();
	}
	return FString();
}

FString FMCTModule::GetScriptsDir()
{
	FString PluginDir = GetPluginDir();
	if (!PluginDir.IsEmpty())
	{
		return FPaths::Combine(PluginDir, TEXT("Resources"), TEXT("Scripts"));
	}
	return FString();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMCTModule, MCPToolkit)
