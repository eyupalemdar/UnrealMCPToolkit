// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportCommandlet.h"
#include "AIExportFunctionLibrary.h"
#include "AIExportSettings.h"
#include "CommonAIExportModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

UAIExportCommandlet::UAIExportCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
	ShowErrorCount = true;
	ShowProgress = true;

	HelpDescription = TEXT("Exports UE assets to AI-readable text format using the modular exporter registry.");
	HelpUsage = TEXT("UnrealEditor-Cmd.exe Project.uproject -run=AIExport -asset=\"/Game/Path/To/Asset\" [-raw|-simplify|-both] [-output=\"Dir\"]");
	HelpParamNames.Add(TEXT("asset"));
	HelpParamNames.Add(TEXT("output"));
	HelpParamNames.Add(TEXT("raw"));
	HelpParamNames.Add(TEXT("simplify"));
	HelpParamNames.Add(TEXT("both"));
	HelpParamNames.Add(TEXT("format"));
	HelpParamDescriptions.Add(TEXT("Asset path to export (required)"));
	HelpParamDescriptions.Add(TEXT("Output directory (optional, defaults to project settings)"));
	HelpParamDescriptions.Add(TEXT("Export raw file only (no simplification)"));
	HelpParamDescriptions.Add(TEXT("Export simplified file only (deletes raw after)"));
	HelpParamDescriptions.Add(TEXT("Export both raw and simplified files"));
	HelpParamDescriptions.Add(TEXT("Output format: text or json (default: text)"));
}

int32 UAIExportCommandlet::Main(const FString& Params)
{
	UE_LOG(LogAIExport, Display, TEXT("========================================"));
	UE_LOG(LogAIExport, Display, TEXT("AI Export Commandlet"));
	UE_LOG(LogAIExport, Display, TEXT("========================================"));

	// Parse parameters
	if (!ParseParameters(Params))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to parse parameters"));
		UE_LOG(LogAIExport, Display, TEXT("Usage: %s"), *HelpUsage);
		return 1;
	}

	// Export the asset
	if (!ExportAsset(AssetPath))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to export asset: %s"), *AssetPath);
		return 1;
	}

	UE_LOG(LogAIExport, Display, TEXT("Export completed successfully!"));
	return 0;
}

bool UAIExportCommandlet::ParseParameters(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;

	// Parse command line
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	// Get asset path (required)
	if (const FString* AssetParam = ParamVals.Find(TEXT("asset")))
	{
		AssetPath = *AssetParam;
	}
	else if (Tokens.Num() > 0)
	{
		AssetPath = Tokens[0];
	}

	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogAIExport, Error, TEXT("No asset path specified. Use -asset=\"/Game/Path/To/Asset\""));
		return false;
	}

	// Get output directory (optional)
	if (const FString* OutputParam = ParamVals.Find(TEXT("output")))
	{
		OutputDirectory = *OutputParam;
	}
	else
	{
		OutputDirectory = GetOutputDirectory();
	}

	// Check for output mode flags (command line overrides settings)
	if (Switches.Contains(TEXT("raw")))
	{
		OutputMode = EAIExportOutputMode::RawOnly;
	}
	else if (Switches.Contains(TEXT("simplify")))
	{
		OutputMode = EAIExportOutputMode::SimplifiedOnly;
	}
	else if (Switches.Contains(TEXT("both")))
	{
		OutputMode = EAIExportOutputMode::Both;
	}
	else
	{
		// Use settings
		OutputMode = UAIExportSettings::Get()->OutputMode;
	}

	// Get output format
	if (const FString* FormatParam = ParamVals.Find(TEXT("format")))
	{
		OutputFormat = *FormatParam;
	}
	else
	{
		OutputFormat = TEXT("text");
	}

	UE_LOG(LogAIExport, Display, TEXT("Asset Path: %s"), *AssetPath);
	UE_LOG(LogAIExport, Display, TEXT("Output Dir: %s"), *OutputDirectory);

	const TCHAR* OutputModeStr = TEXT("Unknown");
	switch (OutputMode)
	{
		case EAIExportOutputMode::RawOnly: OutputModeStr = TEXT("Raw Only"); break;
		case EAIExportOutputMode::SimplifiedOnly: OutputModeStr = TEXT("Simplified Only"); break;
		case EAIExportOutputMode::Both: OutputModeStr = TEXT("Both"); break;
	}
	UE_LOG(LogAIExport, Display, TEXT("Output Mode: %s"), OutputModeStr);

	return true;
}

bool UAIExportCommandlet::ExportAsset(const FString& InAssetPath)
{
	// Delegate to the function library which uses the modular exporter registry.
	// This avoids duplicating export logic that already exists in the registry system.
	const bool bBothFormats = (OutputMode == EAIExportOutputMode::Both);

	// For RawOnly mode, temporarily override the project setting
	// ExportAssetByPath respects the bBothFormats flag: true=Both, false=SimplifiedOnly
	// For RawOnly, we handle it by generating content directly via registry
	if (OutputMode == EAIExportOutputMode::RawOnly)
	{
		// Load the asset
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *InAssetPath);
		if (!Asset)
		{
			UE_LOG(LogAIExport, Error, TEXT("Failed to load asset: %s"), *InAssetPath);
			return false;
		}

		// Check if supported
		if (!UAIExportFunctionLibrary::IsAssetTypeSupported(Asset))
		{
			UE_LOG(LogAIExport, Error, TEXT("Unsupported asset type: %s"), *Asset->GetClass()->GetName());
			return false;
		}

		// Generate raw content using registry (no default filtering)
		FString RawContent = UAIExportFunctionLibrary::ExportAssetContent(Asset, false);
		if (RawContent.IsEmpty())
		{
			UE_LOG(LogAIExport, Warning, TEXT("Export produced no content"));
			return false;
		}

		FString SanitizedName = SanitizeFileName(Asset->GetName());
		FString RawPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("%s_raw.txt"), *SanitizedName));
		if (!WriteToFile(RawContent, RawPath))
		{
			return false;
		}

		UE_LOG(LogAIExport, Display, TEXT("Exported %s (raw) to: %s"), *Asset->GetClass()->GetName(), *RawPath);
		return true;
	}

	// SimplifiedOnly and Both modes — use ExportAssetByPath which handles
	// Python simplification, temp file cleanup, and stripped file generation
	FAIExportResult Result = UAIExportFunctionLibrary::ExportAssetByPath(InAssetPath, OutputDirectory, bBothFormats);

	if (!Result.bSuccess)
	{
		UE_LOG(LogAIExport, Error, TEXT("Export failed: %s"), *Result.ErrorMessage);
		return false;
	}

	if (!Result.RawFilePath.IsEmpty())
	{
		UE_LOG(LogAIExport, Display, TEXT("Exported %s (raw) to: %s"), *Result.AssetType, *Result.RawFilePath);
	}
	if (!Result.SimplifiedFilePath.IsEmpty())
	{
		UE_LOG(LogAIExport, Display, TEXT("Exported %s (simplified) to: %s"), *Result.AssetType, *Result.SimplifiedFilePath);
	}

	return true;
}

bool UAIExportCommandlet::WriteToFile(const FString& Content, const FString& FilePath)
{
	// Ensure directory exists
	FString Directory = FPaths::GetPath(FilePath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*Directory))
	{
		PlatformFile.CreateDirectoryTree(*Directory);
	}

	// Write file
	if (!FFileHelper::SaveStringToFile(Content, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to write file: %s"), *FilePath);
		return false;
	}

	return true;
}

FString UAIExportCommandlet::GetOutputDirectory() const
{
	// Get from settings
	const UAIExportSettings* Settings = UAIExportSettings::Get();
	if (Settings)
	{
		return Settings->GetOutputDirectoryAbsolute();
	}

	// Fallback to default
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Dev"), TEXT("AIExports"));
}

FString UAIExportCommandlet::SanitizeFileName(const FString& InName) const
{
	FString Result = InName;

	// Remove path separators
	Result.ReplaceInline(TEXT("/"), TEXT("_"));
	Result.ReplaceInline(TEXT("\\"), TEXT("_"));

	// Remove invalid characters
	Result.ReplaceInline(TEXT(":"), TEXT("_"));
	Result.ReplaceInline(TEXT("*"), TEXT("_"));
	Result.ReplaceInline(TEXT("?"), TEXT("_"));
	Result.ReplaceInline(TEXT("\""), TEXT("_"));
	Result.ReplaceInline(TEXT("<"), TEXT("_"));
	Result.ReplaceInline(TEXT(">"), TEXT("_"));
	Result.ReplaceInline(TEXT("|"), TEXT("_"));

	return Result;
}
