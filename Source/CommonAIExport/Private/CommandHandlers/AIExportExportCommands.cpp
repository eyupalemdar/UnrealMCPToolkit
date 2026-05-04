// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportExportCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"
#include "AIExportFunctionLibrary.h"
#include "AIExporterRegistry.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace CommonAIExport::CommandHandlers::Export
{
FString HandleExportWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		FAIExportResult Result = UAIExportFunctionLibrary::ExportWidgetBlueprintByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}



FString HandleExportBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		// Use ExportAssetByPath to support all asset types (Blueprint, Audio, Input, etc.)
		FAIExportResult Result = UAIExportFunctionLibrary::ExportAssetByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}



FString HandleListSupportedTypes()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> Types;
	TArray<TSharedPtr<FJsonValue>> ClassPaths;

	TArray<UClass*> SupportedClasses = UAIExporterRegistry::Get()->GetAllSupportedClasses();
	SupportedClasses.Sort([](const UClass& A, const UClass& B)
	{
		return A.GetName() < B.GetName();
	});

	for (UClass* SupportedClass : SupportedClasses)
	{
		if (!SupportedClass)
		{
			continue;
		}
		Types.Add(MakeShared<FJsonValueString>(SupportedClass->GetName()));
		ClassPaths.Add(MakeShared<FJsonValueString>(SupportedClass->GetPathName()));
	}

	Data->SetArrayField(TEXT("types"), Types);
	Data->SetArrayField(TEXT("class_paths"), ClassPaths);
	return CreateSuccessResponse(Data);
}


}
