// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "AIExportSettings.h"
#include "AIExportCommandlet.generated.h"

/**
 * Commandlet for exporting UE assets to text format for AI analysis.
 *
 * Uses the modular AIExporterRegistry system — adding support for new asset types
 * only requires creating a new UAIExporterBase subclass; no commandlet changes needed.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=AIExport -asset="/Game/Path/To/Asset" [-simplify|-raw|-both] [-output="Dir"]
 *
 * Parameters:
 *   -asset       : Asset path to export (required)
 *   -output      : Output directory (optional, defaults to Dev/AIExports)
 *   -raw         : Export raw file only (no simplification)
 *   -simplify    : Export simplified file only (deletes raw after simplification)
 *   -both        : Export both raw and simplified files
 *   -format      : Output format - text or json (optional, defaults to text)
 */
UCLASS()
class COMMONAIEXPORT_API UAIExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAIExportCommandlet();

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet Interface

private:
	/** Parse command line parameters */
	bool ParseParameters(const FString& Params);

	/** Export asset by path (delegates to UAIExportFunctionLibrary) */
	bool ExportAsset(const FString& InAssetPath);

	/** Write content to file */
	bool WriteToFile(const FString& Content, const FString& FilePath);

	/** Get output directory (from settings or default) */
	FString GetOutputDirectory() const;

	/** Sanitize asset name for filename */
	FString SanitizeFileName(const FString& InName) const;

private:
	/** Asset path to export */
	FString AssetPath;

	/** Output directory */
	FString OutputDirectory;

	/** Output mode (RawOnly/SimplifiedOnly/Both) */
	EAIExportOutputMode OutputMode = EAIExportOutputMode::SimplifiedOnly;

	/** Output format (text/json) */
	FString OutputFormat = TEXT("text");
};
