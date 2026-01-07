// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Handles registration and management of AI Export context menu items
 * in the Content Browser.
 */
class FAIExportContextMenu
{
public:
	/** Register the context menu extension */
	static void Register();

	/** Unregister the context menu extension */
	static void Unregister();

private:
	/** Execute export for selected assets */
	static void ExecuteExportForAI();

	/** Check if export action can be executed */
	static bool CanExecuteExportForAI();

	/** Get currently selected assets in Content Browser */
	static TArray<FAssetData> GetSelectedAssets();
};
