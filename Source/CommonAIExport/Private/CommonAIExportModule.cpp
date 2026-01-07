// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommonAIExportModule.h"
#include "AIExportContextMenu.h"
#include "AIExportTCPServer.h"
#include "Interfaces/IPluginManager.h"
#include "ToolMenus.h"

DEFINE_LOG_CATEGORY(LogAIExport);

#define LOCTEXT_NAMESPACE "FCommonAIExportModule"

void FCommonAIExportModule::StartupModule()
{
	UE_LOG(LogAIExport, Log, TEXT("CommonAIExport module started"));

	// Register context menu after ToolMenus is ready
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
		this, &FCommonAIExportModule::RegisterContextMenu));

	// Start TCP server for external commands (Claude Code, Python scripts)
	FAIExportTCPServerManager::Start();
}

void FCommonAIExportModule::ShutdownModule()
{
	// Stop TCP server
	FAIExportTCPServerManager::Stop();

	// Unregister context menu
	UToolMenus::UnRegisterStartupCallback(this);
	FAIExportContextMenu::Unregister();

	UE_LOG(LogAIExport, Log, TEXT("CommonAIExport module shutdown"));
}

void FCommonAIExportModule::RegisterContextMenu()
{
	FAIExportContextMenu::Register();
}

FString FCommonAIExportModule::GetPluginDir()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CommonAIExport"));
	if (Plugin.IsValid())
	{
		return Plugin->GetBaseDir();
	}
	return FString();
}

FString FCommonAIExportModule::GetScriptsDir()
{
	FString PluginDir = GetPluginDir();
	if (!PluginDir.IsEmpty())
	{
		return FPaths::Combine(PluginDir, TEXT("Resources"), TEXT("Scripts"));
	}
	return FString();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCommonAIExportModule, CommonAIExport)
