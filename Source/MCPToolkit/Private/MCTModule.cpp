// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTModule.h"
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

	// Start TCP server for external automation commands.
	FMCTTcpServerManager::Start();
}

void FMCTModule::ShutdownModule()
{
	// Stop TCP server
	FMCTTcpServerManager::Stop();

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
