// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AIExportSettings.generated.h"

/**
 * Output mode for AI Export
 */
UENUM(BlueprintType)
enum class EAIExportOutputMode : uint8
{
	/** Export only the raw file (no simplification) */
	RawOnly        UMETA(DisplayName = "Raw Only"),

	/** Export only the simplified file (raw is deleted after simplification) */
	SimplifiedOnly UMETA(DisplayName = "Simplified Only"),

	/** Export both raw and simplified files */
	Both           UMETA(DisplayName = "Both")
};

/**
 * Settings for the AI Export plugin.
 * Accessible via Project Settings > Plugins > AI Export
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "AI Export Settings"))
class COMMONAIEXPORT_API UAIExportSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAIExportSettings();

	/**
	 * Directory where exported files will be saved.
	 * Relative to project root (e.g., "Dev/AIExports")
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export", meta = (RelativePath))
	FDirectoryPath OutputDirectory;

	/**
	 * Output mode determines what files are generated.
	 * - Raw Only: Only raw export file (no simplification)
	 * - Simplified Only: Only simplified file (raw is deleted after simplification)
	 * - Both: Both raw and simplified files are kept
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	EAIExportOutputMode OutputMode = EAIExportOutputMode::SimplifiedOnly;

	/**
	 * Path to Python executable.
	 * Leave as "python" to use system default.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Python")
	FString PythonPath = TEXT("python");

	/**
	 * Maximum length for exported property values.
	 * Values exceeding this limit will be truncated with "...(truncated)" suffix.
	 * Increase for complex DataAssets or GameplayTag containers.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export", meta = (ClampMin = "100", ClampMax = "50000"))
	int32 MaxPropertyValueLength = 2000;

	/**
	 * Also copy the exported content to clipboard (in addition to saving file).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bAlsoCopyToClipboard = false;

	/**
	 * Open the exported file in default text editor after export.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bOpenFileAfterExport = false;

	/**
	 * Show notification toast after export completes.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Export")
	bool bShowNotification = true;

	/**
	 * Get the absolute output directory path.
	 */
	FString GetOutputDirectoryAbsolute() const;

	/** Gets the settings object */
	static const UAIExportSettings* Get()
	{
		return GetDefault<UAIExportSettings>();
	}

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	virtual FName GetSectionName() const override { return FName(TEXT("AI Export")); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("AIExportSettings", "SectionText", "AI Export");
	}

	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("AIExportSettings", "SectionDescription", "Configure AI Export settings for exporting assets to text format.");
	}
#endif
};
