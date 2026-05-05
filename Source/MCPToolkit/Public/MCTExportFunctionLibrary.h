// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "MCTExportFunctionLibrary.generated.h"

/**
 * Result of an export operation
 */
USTRUCT(BlueprintType)
struct FMCTExportResult
{
	GENERATED_BODY()

	UPROPERTY()
	bool bSuccess = false;

	UPROPERTY()
	FString RawFilePath;

	UPROPERTY()
	FString SimplifiedFilePath;

	UPROPERTY()
	FString StrippedFilePath;

	UPROPERTY()
	FString ErrorMessage;

	UPROPERTY()
	FString AssetName;

	UPROPERTY()
	FString AssetType;
};

/**
 * Delegate for export progress reporting
 */
DECLARE_DELEGATE_ThreeParams(FOnMCTExportProgress, int32 /*Current*/, int32 /*Total*/, const FString& /*AssetName*/);

/**
 * Function library for AI Export operations.
 *
 * This is a FACADE that provides a clean public API for AI export functionality.
 * The actual export logic is delegated to the MCTExporterRegistry and individual exporters.
 *
 * Supported Asset Types (via modular exporters):
 * - UBlueprint (Actor BP, Object BP, etc.)
 * - UWidgetBlueprint
 * - UDataAsset (and all derived types)
 * - UDataTable
 * - UInputAction, UInputMappingContext
 * - Audio: USoundClass, USoundSubmix, USoundConcurrency, USoundAttenuation
 * - Audio Modulation: USoundControlBus, USoundControlBusMix, USoundModulationPatch
 * - UWorld (Map/Level)
 *
 * Usage:
 *   FMCTExportResult Result = UMCTExportFunctionLibrary::ExportAssetByPath(
 *       "/Game/Maps/L_MyMap",
 *       "/path/to/exports",
 *       true  // both raw and simplified
 *   );
 */
UCLASS()
class MCPTOOLKIT_API UMCTExportFunctionLibrary : public UObject
{
	GENERATED_BODY()

public:
	//==========================================================================
	// Asset Support Checks
	//==========================================================================

	/**
	 * Check if an asset type is supported for AI export (via AssetData)
	 */
	static bool IsAssetTypeSupported(const FAssetData& AssetData);

	/**
	 * Check if an asset type is supported for AI export (via UObject)
	 */
	static bool IsAssetTypeSupported(UObject* Asset);

	//==========================================================================
	// Export Functions
	//==========================================================================

	/**
	 * Export a single asset to text format
	 * @param Asset The asset to export
	 * @param OutResult Export result with file paths and status
	 * @return true if export succeeded
	 */
	static bool ExportAsset(UObject* Asset, FMCTExportResult& OutResult);

	/**
	 * Export multiple assets with progress reporting
	 * @param Assets Array of assets to export
	 * @param OnProgress Progress callback
	 * @param OutResults Array of export results
	 * @return Number of successfully exported assets
	 */
	static int32 ExportAssets(const TArray<FAssetData>& Assets, FOnMCTExportProgress OnProgress, TArray<FMCTExportResult>& OutResults);

	/**
	 * Export any supported asset type by path.
	 * This is the primary function for TCP/external tool integration.
	 * @param AssetPath Full asset path (e.g., "/Game/Maps/L_ExampleMap")
	 * @param OutputDirectory Output directory for export files
	 * @param bBothFormats If true, exports both raw and simplified formats
	 * @return Export result with file paths and status
	 */
	UFUNCTION(BlueprintCallable, Category = "AI Export")
	static FMCTExportResult ExportAssetByPath(const FString& AssetPath, const FString& OutputDirectory, bool bBothFormats = true);

	/**
	 * Export a Widget Blueprint by path (convenience function)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI Export")
	static FMCTExportResult ExportWidgetBlueprintByPath(const FString& AssetPath, const FString& OutputDirectory, bool bBothFormats = true);

	/**
	 * Export a Blueprint by path (convenience function)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI Export")
	static FMCTExportResult ExportBlueprintByPath(const FString& AssetPath, const FString& OutputDirectory, bool bBothFormats = true);

	//==========================================================================
	// Utility Functions
	//==========================================================================

	/**
	 * Run Python simplifier on a raw export file
	 */
	static bool RunSimplifier(const FString& RawFilePath, FString& OutSimplifiedPath);

	/**
	 * Copy content to system clipboard
	 */
	static void CopyToClipboard(const FString& Content);

	/**
	 * Show export completion notification
	 */
	static void ShowExportNotification(int32 SuccessCount, int32 FailCount, const FString& OutputDirectory);

	/**
	 * Open file in default text editor
	 */
	static void OpenFileInEditor(const FString& FilePath);

	/**
	 * Get the output directory (from settings or default)
	 */
	static FString GetOutputDirectory();

	/**
	 * Get the output path for an asset, mirroring the Content folder structure
	 */
	static FString GetOutputPathForAsset(UObject* Asset);

	/**
	 * Get the simplifier script path
	 */
	static FString GetSimplifierScriptPath();

	/**
	 * Export asset content as string using the modular exporter registry.
	 * @param Asset The asset to export
	 * @param bFilterDefaults If true, skip properties that match the archetype defaults
	 * @return Exported text content
	 */
	static FString ExportAssetContent(UObject* Asset, bool bFilterDefaults);

private:
	/**
	 * Write content to file
	 */
	static bool WriteToFile(const FString& Content, const FString& FilePath);

	/**
	 * Sanitize a file name (remove invalid characters)
	 */
	static FString SanitizeFileName(const FString& InName);

	/**
	 * Get a human-readable asset type name
	 */
	static FString GetAssetTypeName(UObject* Asset);
};
