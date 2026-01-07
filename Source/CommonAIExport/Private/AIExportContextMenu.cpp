// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportContextMenu.h"
#include "AIExportFunctionLibrary.h"
#include "AIExportSettings.h"
#include "CommonAIExportModule.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "AIExportContextMenu"

void FAIExportContextMenu::Register()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// Extend the Content Browser asset context menu
	UToolMenu* Menu = ToolMenus->ExtendMenu("ContentBrowser.AssetContextMenu");
	if (!Menu)
	{
		return;
	}

	// Add a new section for AI Export
	FToolMenuSection& Section = Menu->FindOrAddSection("AIExport");
	Section.Label = LOCTEXT("AIExportSection", "AI Export");

	// Add dynamic entry that checks asset type
	Section.AddDynamicEntry("AIExportEntry", FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
	{
		// Get selected assets
		TArray<FAssetData> SelectedAssets = GetSelectedAssets();

		// Check if any selected asset is supported
		bool bHasSupportedAsset = false;
		for (const FAssetData& Asset : SelectedAssets)
		{
			if (UAIExportFunctionLibrary::IsAssetTypeSupported(Asset))
			{
				bHasSupportedAsset = true;
				break;
			}
		}

		// Only show menu if there are supported assets
		if (bHasSupportedAsset)
		{
			InSection.AddMenuEntry(
				"ExportForAI",
				LOCTEXT("ExportForAI", "Export for AI"),
				LOCTEXT("ExportForAITooltip", "Export selected assets to simplified text format for AI analysis"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Documentation"),
				FUIAction(
					FExecuteAction::CreateStatic(&FAIExportContextMenu::ExecuteExportForAI),
					FCanExecuteAction::CreateStatic(&FAIExportContextMenu::CanExecuteExportForAI)
				)
			);
		}
	}));

	UE_LOG(LogAIExport, Log, TEXT("AI Export context menu registered"));
}

void FAIExportContextMenu::Unregister()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus)
	{
		ToolMenus->RemoveSection("ContentBrowser.AssetContextMenu", "AIExport");
	}

	UE_LOG(LogAIExport, Log, TEXT("AI Export context menu unregistered"));
}

void FAIExportContextMenu::ExecuteExportForAI()
{
	TArray<FAssetData> SelectedAssets = GetSelectedAssets();

	// Filter to only supported assets
	TArray<FAssetData> SupportedAssets;
	for (const FAssetData& Asset : SelectedAssets)
	{
		if (UAIExportFunctionLibrary::IsAssetTypeSupported(Asset))
		{
			SupportedAssets.Add(Asset);
		}
	}

	if (SupportedAssets.Num() == 0)
	{
		return;
	}

	// Get settings
	const UAIExportSettings* Settings = UAIExportSettings::Get();
	const bool bShowNotification = Settings ? Settings->bShowNotification : true;
	const bool bOpenFile = Settings ? Settings->bOpenFileAfterExport : false;

	TArray<FAIExportResult> Results;
	int32 SuccessCount = 0;

	// Use progress dialog for multiple assets
	if (SupportedAssets.Num() > 1)
	{
		FScopedSlowTask SlowTask(SupportedAssets.Num(), LOCTEXT("ExportingAssets", "Exporting assets for AI..."));
		SlowTask.MakeDialog(true); // Cancelable

		for (int32 i = 0; i < SupportedAssets.Num(); ++i)
		{
			if (SlowTask.ShouldCancel())
			{
				break;
			}

			const FAssetData& AssetData = SupportedAssets[i];
			SlowTask.EnterProgressFrame(1, FText::FromString(AssetData.AssetName.ToString()));

			UObject* Asset = AssetData.GetAsset();
			if (!Asset)
			{
				FAIExportResult Result;
				Result.bSuccess = false;
				Result.AssetName = AssetData.AssetName.ToString();
				Result.ErrorMessage = TEXT("Failed to load asset");
				Results.Add(Result);
				continue;
			}

			FAIExportResult Result;
			if (UAIExportFunctionLibrary::ExportAsset(Asset, Result))
			{
				SuccessCount++;
			}
			Results.Add(Result);
		}
	}
	else
	{
		// Single asset - no dialog needed
		UObject* Asset = SupportedAssets[0].GetAsset();
		if (Asset)
		{
			FAIExportResult Result;
			if (UAIExportFunctionLibrary::ExportAsset(Asset, Result))
			{
				SuccessCount++;
			}
			Results.Add(Result);
		}
	}

	// Show notification
	if (bShowNotification)
	{
		int32 FailCount = Results.Num() - SuccessCount;
		UAIExportFunctionLibrary::ShowExportNotification(SuccessCount, FailCount, UAIExportFunctionLibrary::GetOutputDirectory());
	}

	// Open file if single asset and setting enabled
	if (bOpenFile && Results.Num() == 1 && Results[0].bSuccess)
	{
		FString FileToOpen = Results[0].SimplifiedFilePath.IsEmpty() ? Results[0].RawFilePath : Results[0].SimplifiedFilePath;
		UAIExportFunctionLibrary::OpenFileInEditor(FileToOpen);
	}
}

bool FAIExportContextMenu::CanExecuteExportForAI()
{
	TArray<FAssetData> SelectedAssets = GetSelectedAssets();

	for (const FAssetData& Asset : SelectedAssets)
	{
		if (UAIExportFunctionLibrary::IsAssetTypeSupported(Asset))
		{
			return true;
		}
	}

	return false;
}

TArray<FAssetData> FAIExportContextMenu::GetSelectedAssets()
{
	TArray<FAssetData> SelectedAssets;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	IContentBrowserSingleton& ContentBrowser = ContentBrowserModule.Get();

	ContentBrowser.GetSelectedAssets(SelectedAssets);

	return SelectedAssets;
}

#undef LOCTEXT_NAMESPACE
