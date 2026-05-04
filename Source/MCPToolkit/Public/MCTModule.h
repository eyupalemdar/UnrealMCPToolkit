// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMCT, Log, All);

class FMCTModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Gets the plugin's base directory path.
	 * Useful for locating Resources/Scripts folder.
	 */
	static FString GetPluginDir();

	/**
	 * Gets the path to the Scripts folder within the plugin.
	 */
	static FString GetScriptsDir();

	/**
	 * Singleton-like access to this module's interface.
	 */
	static FMCTModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMCTModule>("MCPToolkit");
	}

	/**
	 * Checks to see if this module is loaded and ready.
	 */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MCPToolkit");
	}

private:
	/** Called when ToolMenus is ready to register context menu */
	void RegisterContextMenu();
};
