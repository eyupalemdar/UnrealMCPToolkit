// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportFunctionLibrary.h"
#include "AIExporterRegistry.h"
#include "Exporters/AIExporterBase.h"
#include "Exporters/AITextureExporter.h"
#include "AIExportSettings.h"
#include "CommonAIExportModule.h"

#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/FileManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "AIExportFunctionLibrary"

//==========================================================================
// Asset Support Checks
//==========================================================================

bool UAIExportFunctionLibrary::IsAssetTypeSupported(const FAssetData& AssetData)
{
	// Load the asset to check via registry
	UObject* Asset = AssetData.GetAsset();
	return IsAssetTypeSupported(Asset);
}

bool UAIExportFunctionLibrary::IsAssetTypeSupported(UObject* Asset)
{
	if (!Asset)
	{
		return false;
	}

	// Use registry to check support
	UAIExporterRegistry* Registry = UAIExporterRegistry::Get();
	return Registry ? Registry->IsAssetSupported(Asset) : false;
}

//==========================================================================
// Export Functions
//==========================================================================

FString UAIExportFunctionLibrary::ExportAssetContent(UObject* Asset, bool bFilterDefaults)
{
	if (!Asset)
	{
		return TEXT("");
	}

	UAIExporterRegistry* Registry = UAIExporterRegistry::Get();
	if (!Registry)
	{
		UE_LOG(LogTemp, Error, TEXT("AIExportFunctionLibrary: Failed to get exporter registry"));
		return TEXT("");
	}

	UAIExporterBase* Exporter = Registry->FindExporterForAsset(Asset);
	if (!Exporter)
	{
		UE_LOG(LogTemp, Warning, TEXT("AIExportFunctionLibrary: No exporter found for asset type %s"),
			*Asset->GetClass()->GetName());
		return TEXT("");
	}

	return Exporter->Export(Asset, bFilterDefaults);
}

bool UAIExportFunctionLibrary::ExportAsset(UObject* Asset, FAIExportResult& OutResult)
{
	if (!Asset)
	{
		OutResult.bSuccess = false;
		OutResult.ErrorMessage = TEXT("Asset is null");
		return false;
	}

	OutResult.AssetName = Asset->GetName();
	OutResult.AssetType = GetAssetTypeName(Asset);

	UE_LOG(LogTemp, Log, TEXT("AIExport: Starting export for asset '%s' (type: %s)"), *OutResult.AssetName, *OutResult.AssetType);

	// Lambda to generate export content with optional filtering
	auto GenerateExport = [Asset](bool bFilterDefaults) -> FString
	{
		return ExportAssetContent(Asset, bFilterDefaults);
	};

	// Get settings - use asset path-based output directory for folder structure mirroring
	FString OutputDir = GetOutputPathForAsset(Asset);
	FString SanitizedName = SanitizeFileName(Asset->GetName());
	const UAIExportSettings* Settings = UAIExportSettings::Get();
	EAIExportOutputMode OutputMode = Settings ? Settings->OutputMode : EAIExportOutputMode::SimplifiedOnly;

	UE_LOG(LogTemp, Log, TEXT("AIExport: Output directory: '%s'"), *OutputDir);

	// Handle output based on mode
	switch (OutputMode)
	{
		case EAIExportOutputMode::RawOnly:
		{
			FString RawContent = GenerateExport(false);
			if (RawContent.IsEmpty())
			{
				OutResult.bSuccess = false;
				OutResult.ErrorMessage = TEXT("Export produced no content");
				return false;
			}

			FString RawPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_raw.txt"), *SanitizedName));
			if (!WriteToFile(RawContent, RawPath))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to write file: %s"), *RawPath);
				return false;
			}
			OutResult.RawFilePath = RawPath;
			break;
		}

		case EAIExportOutputMode::SimplifiedOnly:
		{
			// Generate unfiltered content - Python simplifier does its own intelligent filtering
			FString RawContent = GenerateExport(false);
			if (RawContent.IsEmpty())
			{
				OutResult.bSuccess = false;
				OutResult.ErrorMessage = TEXT("Export produced no content");
				return false;
			}

			// Write to temp raw file for simplifier (expects _raw.txt suffix)
			FString TempRawPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_temp_raw.txt"), *SanitizedName));
			if (!WriteToFile(RawContent, TempRawPath))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to write temp file: %s"), *TempRawPath);
				return false;
			}

			// Run Python simplifier
			FString SimplifiedPath;
			if (RunSimplifier(TempRawPath, SimplifiedPath))
			{
				// Rename _temp_simplified.txt → _simplified.txt
				FString FinalSimplifiedPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
				if (SimplifiedPath != FinalSimplifiedPath)
				{
					IFileManager::Get().Move(*FinalSimplifiedPath, *SimplifiedPath);
					SimplifiedPath = FinalSimplifiedPath;
				}
				OutResult.SimplifiedFilePath = SimplifiedPath;
			}
			else
			{
				// Fallback: if simplifier fails, generate filtered content
				SimplifiedPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
				FString FilteredContent = GenerateExport(true);
				WriteToFile(FilteredContent, SimplifiedPath);
				OutResult.SimplifiedFilePath = SimplifiedPath;
				UE_LOG(LogTemp, Warning, TEXT("Simplifier failed, using filtered content as simplified"));
			}

			// Delete temp raw file
			IFileManager::Get().Delete(*TempRawPath);
			// Also clean up temp stripped file if it was created
			FString TempStrippedPath = TempRawPath.Replace(TEXT("_raw.txt"), TEXT("_stripped.txt"));
			IFileManager::Get().Delete(*TempStrippedPath);
			break;
		}

		case EAIExportOutputMode::Both:
		{
			// Generate raw (unfiltered) for the raw file
			FString RawContent = GenerateExport(false);
			if (RawContent.IsEmpty())
			{
				OutResult.bSuccess = false;
				OutResult.ErrorMessage = TEXT("Export produced no content");
				return false;
			}

			FString RawPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_raw.txt"), *SanitizedName));
			if (!WriteToFile(RawContent, RawPath))
			{
				OutResult.bSuccess = false;
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to write file: %s"), *RawPath);
				return false;
			}
			OutResult.RawFilePath = RawPath;

			// Run Python simplifier directly on the raw file
			// Python simplifier does its own intelligent filtering/simplification
			FString SimplifiedPath;
			if (RunSimplifier(RawPath, SimplifiedPath))
			{
				OutResult.SimplifiedFilePath = SimplifiedPath;
				// Check if stripped file was also produced
				FString StrippedPath = RawPath.Replace(TEXT("_raw.txt"), TEXT("_stripped.txt"));
				if (FPaths::FileExists(StrippedPath))
				{
					OutResult.StrippedFilePath = StrippedPath;
				}
			}
			else
			{
				// Fallback: if simplifier fails, generate filtered content
				SimplifiedPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
				FString FilteredContent = GenerateExport(true);
				WriteToFile(FilteredContent, SimplifiedPath);
				OutResult.SimplifiedFilePath = SimplifiedPath;
				UE_LOG(LogTemp, Warning, TEXT("Simplifier failed, using filtered content as simplified"));
			}

			break;
		}
	}

	// Special handling for texture assets - also export PNG file
	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		UAIExporterRegistry* Registry = UAIExporterRegistry::Get();
		if (Registry)
		{
			UAITextureExporter* TextureExporter = Cast<UAITextureExporter>(Registry->FindExporterForAsset(Asset));
			if (TextureExporter)
			{
				FString PNGPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s.png"), *SanitizedName));
				if (TextureExporter->ExportTextureToPNG(Texture, PNGPath))
				{
					UE_LOG(LogTemp, Log, TEXT("AIExport: Exported texture PNG to %s"), *PNGPath);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("AIExport: Failed to export texture PNG for %s"), *Asset->GetName());
				}
			}
		}
	}

	// Copy to clipboard if enabled
	if (Settings && Settings->bAlsoCopyToClipboard)
	{
		FString ContentToCopy = OutResult.SimplifiedFilePath.IsEmpty() ?
			GenerateExport(false) : GenerateExport(true);
		CopyToClipboard(ContentToCopy);
	}

	OutResult.bSuccess = true;
	return true;
}

int32 UAIExportFunctionLibrary::ExportAssets(const TArray<FAssetData>& Assets, FOnAIExportProgress OnProgress, TArray<FAIExportResult>& OutResults)
{
	int32 SuccessCount = 0;
	int32 Total = Assets.Num();

	OutResults.Empty();
	OutResults.Reserve(Total);

	for (int32 i = 0; i < Total; ++i)
	{
		const FAssetData& AssetData = Assets[i];

		if (OnProgress.IsBound())
		{
			OnProgress.Execute(i + 1, Total, AssetData.AssetName.ToString());
		}

		UObject* Asset = AssetData.GetAsset();
		if (!Asset)
		{
			FAIExportResult FailResult;
			FailResult.bSuccess = false;
			FailResult.AssetName = AssetData.AssetName.ToString();
			FailResult.ErrorMessage = TEXT("Failed to load asset");
			OutResults.Add(FailResult);
			continue;
		}

		FAIExportResult Result;
		if (ExportAsset(Asset, Result))
		{
			SuccessCount++;
		}
		OutResults.Add(Result);
	}

	return SuccessCount;
}

FAIExportResult UAIExportFunctionLibrary::ExportAssetByPath(const FString& AssetPath, const FString& OutputDirectory, bool bBothFormats)
{
	FAIExportResult Result;

	// Load the asset
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath);
		return Result;
	}

	Result.AssetName = Asset->GetName();
	Result.AssetType = GetAssetTypeName(Asset);

	// Check if supported
	if (!IsAssetTypeSupported(Asset))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(TEXT("Unsupported asset type: %s"), *Asset->GetClass()->GetName());
		return Result;
	}

	// Determine output directory
	FString OutputDir = OutputDirectory.IsEmpty() ? GetOutputPathForAsset(Asset) : OutputDirectory;
	IFileManager::Get().MakeDirectory(*OutputDir, true);

	FString SanitizedName = SanitizeFileName(Asset->GetName());

	if (bBothFormats)
	{
		// Raw export (unfiltered)
		FString RawContent = ExportAssetContent(Asset, false);
		if (RawContent.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Export produced no content");
			return Result;
		}

		FString RawPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_raw.txt"), *SanitizedName));
		if (!WriteToFile(RawContent, RawPath))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("Failed to write raw file: %s"), *RawPath);
			return Result;
		}
		Result.RawFilePath = RawPath;

		// Run strip on the real raw file to produce _stripped.txt
		// (The simplifier will run on a temp file where strip is skipped via _temp_raw guard)
		{
			FString StripScriptPath = FPaths::Combine(FCommonAIExportModule::GetScriptsDir(), TEXT("strip_utils.py"));
			if (FPaths::FileExists(StripScriptPath))
			{
				FString StripArgs = FString::Printf(TEXT("\"%s\" \"%s\""), *StripScriptPath, *RawPath);
				int32 StripReturnCode = 0;
				FString StripStdOut, StripStdErr;
				FPlatformProcess::ExecProcess(TEXT("python"), *StripArgs, &StripReturnCode, &StripStdOut, &StripStdErr);
			}
		}

		// Generate filtered content for simplifier (separate from raw)
		FString FilteredContent = ExportAssetContent(Asset, true);
		FString TempRawPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_temp_raw.txt"), *SanitizedName));
		WriteToFile(FilteredContent, TempRawPath);

		// Run Python simplifier on filtered temp file
		FString SimplifiedPath;
		if (RunSimplifier(TempRawPath, SimplifiedPath))
		{
			// Rename _temp_simplified.txt → _simplified.txt
			FString FinalSimplifiedPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
			if (SimplifiedPath != FinalSimplifiedPath)
			{
				IFileManager::Get().Move(*FinalSimplifiedPath, *SimplifiedPath);
				SimplifiedPath = FinalSimplifiedPath;
			}
			Result.SimplifiedFilePath = SimplifiedPath;
			// Check if stripped file was also produced (from the real raw file)
			FString StrippedPath = RawPath.Replace(TEXT("_raw.txt"), TEXT("_stripped.txt"));
			if (FPaths::FileExists(StrippedPath))
			{
				Result.StrippedFilePath = StrippedPath;
			}
		}
		else
		{
			// Fallback: if simplifier fails, copy filtered as simplified
			SimplifiedPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
			WriteToFile(FilteredContent, SimplifiedPath);
			Result.SimplifiedFilePath = SimplifiedPath;
			UE_LOG(LogTemp, Warning, TEXT("Simplifier failed, using filtered content as simplified"));
		}

		// Delete temp file
		IFileManager::Get().Delete(*TempRawPath);
		// Also clean up temp stripped file if it was created
		FString TempStrippedPath = TempRawPath.Replace(TEXT("_raw.txt"), TEXT("_stripped.txt"));
		IFileManager::Get().Delete(*TempStrippedPath);
	}
	else
	{
		// Simplified only - generate filtered content (bFilterDefaults=true)
		FString FilteredContent = ExportAssetContent(Asset, true);
		if (FilteredContent.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("Export produced no content");
			return Result;
		}

		// Write to temp raw file for simplifier (expects _raw.txt suffix)
		FString TempRawPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_temp_raw.txt"), *SanitizedName));
		if (!WriteToFile(FilteredContent, TempRawPath))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("Failed to write temp file: %s"), *TempRawPath);
			return Result;
		}

		// Run Python simplifier
		FString SimplifiedPath;
		if (RunSimplifier(TempRawPath, SimplifiedPath))
		{
			// Rename _temp_simplified.txt → _simplified.txt
			FString FinalSimplifiedPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
			if (SimplifiedPath != FinalSimplifiedPath)
			{
				IFileManager::Get().Move(*FinalSimplifiedPath, *SimplifiedPath);
				SimplifiedPath = FinalSimplifiedPath;
			}
			Result.SimplifiedFilePath = SimplifiedPath;
		}
		else
		{
			// Fallback: if simplifier fails, use filtered content
			SimplifiedPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s_simplified.txt"), *SanitizedName));
			WriteToFile(FilteredContent, SimplifiedPath);
			Result.SimplifiedFilePath = SimplifiedPath;
			UE_LOG(LogTemp, Warning, TEXT("Simplifier failed, using filtered content as simplified"));
		}

		// Delete temp raw file
		IFileManager::Get().Delete(*TempRawPath);
		// Also clean up temp stripped file if it was created
		FString TempStrippedPath = TempRawPath.Replace(TEXT("_raw.txt"), TEXT("_stripped.txt"));
		IFileManager::Get().Delete(*TempStrippedPath);
	}

	// Special handling for texture assets - also export PNG file
	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		UAIExporterRegistry* Registry = UAIExporterRegistry::Get();
		if (Registry)
		{
			UAITextureExporter* TextureExporter = Cast<UAITextureExporter>(Registry->FindExporterForAsset(Asset));
			if (TextureExporter)
			{
				FString PNGPath = FPaths::Combine(OutputDir, FString::Printf(TEXT("%s.png"), *SanitizedName));
				if (TextureExporter->ExportTextureToPNG(Texture, PNGPath))
				{
					UE_LOG(LogTemp, Log, TEXT("AIExport: Exported texture PNG to %s"), *PNGPath);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("AIExport: Failed to export texture PNG for %s"), *Asset->GetName());
				}
			}
		}
	}

	Result.bSuccess = true;
	return Result;
}

FAIExportResult UAIExportFunctionLibrary::ExportWidgetBlueprintByPath(const FString& AssetPath, const FString& OutputDirectory, bool bBothFormats)
{
	return ExportAssetByPath(AssetPath, OutputDirectory, bBothFormats);
}

FAIExportResult UAIExportFunctionLibrary::ExportBlueprintByPath(const FString& AssetPath, const FString& OutputDirectory, bool bBothFormats)
{
	return ExportAssetByPath(AssetPath, OutputDirectory, bBothFormats);
}

//==========================================================================
// Utility Functions
//==========================================================================

bool UAIExportFunctionLibrary::RunSimplifier(const FString& RawFilePath, FString& OutSimplifiedPath)
{
	FString ScriptPath = GetSimplifierScriptPath();
	if (ScriptPath.IsEmpty() || !FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Simplifier script not found at: %s"), *ScriptPath);
		return false;
	}

	OutSimplifiedPath = RawFilePath.Replace(TEXT("_raw.txt"), TEXT("_simplified.txt"));

	FString Args = FString::Printf(TEXT("\"%s\" \"%s\" \"%s\""), *ScriptPath, *RawFilePath, *OutSimplifiedPath);
	int32 ReturnCode = 0;
	FString StdOut;
	FString StdErr;

	bool bSuccess = FPlatformProcess::ExecProcess(TEXT("python"), *Args, &ReturnCode, &StdOut, &StdErr);

	if (!bSuccess || ReturnCode != 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Simplifier failed: %s"), *StdErr);
		return false;
	}

	return true;
}

void UAIExportFunctionLibrary::CopyToClipboard(const FString& Content)
{
	FPlatformApplicationMisc::ClipboardCopy(*Content);
}

void UAIExportFunctionLibrary::ShowExportNotification(int32 SuccessCount, int32 FailCount, const FString& OutputDirectory)
{
	FText Message;
	SNotificationItem::ECompletionState State;

	if (FailCount == 0)
	{
		Message = FText::Format(LOCTEXT("ExportSuccess", "Exported {0} asset(s) to {1}"),
			FText::AsNumber(SuccessCount), FText::FromString(OutputDirectory));
		State = SNotificationItem::CS_Success;
	}
	else if (SuccessCount > 0)
	{
		Message = FText::Format(LOCTEXT("ExportPartial", "Exported {0} asset(s), {1} failed"),
			FText::AsNumber(SuccessCount), FText::AsNumber(FailCount));
		State = SNotificationItem::CS_Fail;
	}
	else
	{
		Message = FText::Format(LOCTEXT("ExportFailed", "Export failed for all {0} asset(s)"),
			FText::AsNumber(FailCount));
		State = SNotificationItem::CS_Fail;
	}

	FNotificationInfo Info(Message);
	Info.bFireAndForget = true;
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;

	// Add clickable hyperlink to open the export folder
	if (SuccessCount > 0)
	{
		// OutputDirectory may already be absolute path from GetOutputDirectory()
		FString FullOutputPath = FPaths::IsRelative(OutputDirectory)
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / OutputDirectory)
			: OutputDirectory;

		Info.Hyperlink = FSimpleDelegate::CreateLambda([FullOutputPath]()
		{
			FPlatformProcess::ExploreFolder(*FullOutputPath);
		});
		Info.HyperlinkText = LOCTEXT("OpenExportFolder", "Open Folder");
	}

	if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Notification->SetCompletionState(State);
	}
}

void UAIExportFunctionLibrary::OpenFileInEditor(const FString& FilePath)
{
	FPlatformProcess::LaunchFileInDefaultExternalApplication(*FilePath);
}

FString UAIExportFunctionLibrary::GetOutputDirectory()
{
	const UAIExportSettings* Settings = UAIExportSettings::Get();
	if (Settings && !Settings->OutputDirectory.Path.IsEmpty())
	{
		FString ConfiguredPath = Settings->OutputDirectory.Path;
		// If relative path, make it relative to project directory
		if (FPaths::IsRelative(ConfiguredPath))
		{
			ConfiguredPath = FPaths::Combine(FPaths::ProjectDir(), ConfiguredPath);
		}
		return FPaths::ConvertRelativePathToFull(ConfiguredPath);
	}

	// Default to project's Dev/AIExports folder
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Dev"), TEXT("AIExports")));
}

FString UAIExportFunctionLibrary::GetOutputPathForAsset(UObject* Asset)
{
	if (!Asset)
	{
		return GetOutputDirectory();
	}

	// Get asset's package path (e.g., "/Game/UI/MyWidget")
	FString PackagePath = Asset->GetOutermost()->GetName();

	// Keep Game/ prefix so output mirrors Content Browser structure
	FString RelativePath = PackagePath;
	RelativePath.RemoveFromStart(TEXT("/"));

	// Get directory part only
	FString DirectoryPart = FPaths::GetPath(RelativePath);

	// Combine with base output directory
	FString OutputPath = FPaths::Combine(GetOutputDirectory(), DirectoryPart);

	// Ensure directory exists
	IFileManager::Get().MakeDirectory(*OutputPath, true);

	return OutputPath;
}

FString UAIExportFunctionLibrary::GetSimplifierScriptPath()
{
	FString ScriptsDir = FCommonAIExportModule::GetScriptsDir();
	if (!ScriptsDir.IsEmpty())
	{
		return FPaths::Combine(ScriptsDir, TEXT("simplify_asset.py"));
	}
	return FString();
}

bool UAIExportFunctionLibrary::WriteToFile(const FString& Content, const FString& FilePath)
{
	// Ensure directory exists
	FString Directory = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*Directory, true);

	FString AbsolutePath = FPaths::ConvertRelativePathToFull(FilePath);
	UE_LOG(LogTemp, Log, TEXT("AIExport: Writing %d bytes to '%s'"), Content.Len(), *AbsolutePath);

	bool bSuccess = FFileHelper::SaveStringToFile(Content, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("AIExport: Successfully wrote file"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("AIExport: FAILED to write file to '%s'"), *AbsolutePath);
	}

	return bSuccess;
}

FString UAIExportFunctionLibrary::SanitizeFileName(const FString& InName)
{
	FString Result = InName;
	Result.ReplaceInline(TEXT("/"), TEXT("_"));
	Result.ReplaceInline(TEXT("\\"), TEXT("_"));
	Result.ReplaceInline(TEXT(":"), TEXT("_"));
	Result.ReplaceInline(TEXT("*"), TEXT("_"));
	Result.ReplaceInline(TEXT("?"), TEXT("_"));
	Result.ReplaceInline(TEXT("\""), TEXT("_"));
	Result.ReplaceInline(TEXT("<"), TEXT("_"));
	Result.ReplaceInline(TEXT(">"), TEXT("_"));
	Result.ReplaceInline(TEXT("|"), TEXT("_"));
	return Result;
}

FString UAIExportFunctionLibrary::GetAssetTypeName(UObject* Asset)
{
	if (!Asset)
	{
		return TEXT("Unknown");
	}

	FString ClassName = Asset->GetClass()->GetName();

	// Map class names to friendly names
	if (ClassName.Contains(TEXT("WidgetBlueprint")))
	{
		return TEXT("Widget Blueprint");
	}
	else if (ClassName.Contains(TEXT("AnimBlueprint")))
	{
		return TEXT("Anim Blueprint");
	}
	else if (ClassName.Contains(TEXT("Blueprint")))
	{
		return TEXT("Blueprint");
	}
	else if (ClassName.Contains(TEXT("World")))
	{
		return TEXT("Map/World");
	}
	else if (ClassName.Contains(TEXT("DataAsset")))
	{
		return TEXT("Data Asset");
	}
	else if (ClassName.Contains(TEXT("InputAction")))
	{
		return TEXT("Input Action");
	}
	else if (ClassName.Contains(TEXT("InputMappingContext")))
	{
		return TEXT("Input Mapping Context");
	}
	else if (ClassName.Contains(TEXT("SoundClass")))
	{
		return TEXT("Sound Class");
	}
	else if (ClassName.Contains(TEXT("SoundSubmix")))
	{
		return TEXT("Sound Submix");
	}
	else if (ClassName.Contains(TEXT("SoundConcurrency")))
	{
		return TEXT("Sound Concurrency");
	}
	else if (ClassName.Contains(TEXT("SoundAttenuation")))
	{
		return TEXT("Sound Attenuation");
	}
	else if (ClassName.Contains(TEXT("SoundControlBus")))
	{
		return TEXT("Sound Control Bus");
	}
	else if (ClassName.Contains(TEXT("SoundControlBusMix")))
	{
		return TEXT("Sound Control Bus Mix");
	}
	else if (ClassName.Contains(TEXT("SoundModulationPatch")))
	{
		return TEXT("Sound Modulation Patch");
	}

	return ClassName;
}

#undef LOCTEXT_NAMESPACE
