// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportImportCommands.h"

#include "Builders/AIAssetImportBuilder.h"
#include "CommandHandlers/AIExportCommandResponse.h"

#include "Async/Async.h"
#include "Dom/JsonValue.h"

namespace CommonAIExport::CommandHandlers::Import
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName)
{
	FString Value;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const bool bDefault)
{
	bool bValue = bDefault;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

FString ReadStringFieldOrDefault(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FString& DefaultValue)
{
	const FString Value = ReadStringField(Params, FieldName);
	return Value.IsEmpty() ? DefaultValue : Value;
}

FString CreateBuilderErrorResponse(const FString& Error, const TCHAR* Fallback)
{
	return CreateErrorResponse(Error.IsEmpty() ? FString(Fallback) : Error);
}

FString RunOnGameThread(TFunction<FString()>&& Work, const TCHAR* TimeoutError, const double TimeoutSeconds)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, Work = MoveTemp(Work)]()
	{
		Promise->SetValue(Work());
	});

	Future.WaitFor(FTimespan::FromSeconds(TimeoutSeconds));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleImportTexture(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString SourcePath = ReadStringField(Params, TEXT("source_path"));
	const FString PackagePath = ReadStringField(Params, TEXT("package_path"));
	const FString AssetName = ReadStringField(Params, TEXT("asset_name"));
	const FString Compression = ReadStringFieldOrDefault(Params, TEXT("compression"), TEXT("UserInterface2D"));
	const FString MipGen = ReadStringFieldOrDefault(Params, TEXT("mip_gen"), TEXT("NoMipmaps"));
	const FString LODGroup = ReadStringFieldOrDefault(Params, TEXT("lod_group"), TEXT("UI"));
	const bool bSRGB = ReadBoolField(Params, TEXT("srgb"), true);

	if (SourcePath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'source_path' parameter"));
	}
	if (PackagePath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	}

	return RunOnGameThread([SourcePath, PackagePath, AssetName, Compression, MipGen, LODGroup, bSRGB]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UAIAssetImportBuilder::ImportTexture(
			SourcePath,
			PackagePath,
			AssetName,
			Compression,
			MipGen,
			LODGroup,
			bSRGB,
			Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to import texture"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Import texture timed out"), 60.0);
}

FString HandleImportFont(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString PackagePath = ReadStringField(Params, TEXT("package_path"));
	const FString FontName = ReadStringField(Params, TEXT("font_name"));
	const FString Hinting = ReadStringFieldOrDefault(Params, TEXT("hinting"), TEXT("Auto"));
	if (PackagePath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	}
	if (FontName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'font_name' parameter"));
	}

	const TArray<TSharedPtr<FJsonValue>>* FacesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("faces"), FacesArray) || !FacesArray || FacesArray->IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing or empty 'faces' array. Each entry needs 'source_path' and 'name' (e.g. 'Regular', 'Bold')."));
	}

	TArray<FAIImportFontFaceSpec> Faces;
	for (const TSharedPtr<FJsonValue>& FaceValue : *FacesArray)
	{
		const TSharedPtr<FJsonObject>* FaceObject = nullptr;
		if (!FaceValue.IsValid() || !FaceValue->TryGetObject(FaceObject) || !FaceObject || !FaceObject->IsValid())
		{
			return CreateErrorResponse(TEXT("Each face entry must be a JSON object with 'source_path' and 'name'"));
		}

		FAIImportFontFaceSpec Face;
		if (!(*FaceObject)->TryGetStringField(TEXT("source_path"), Face.SourcePath))
		{
			return CreateErrorResponse(TEXT("Face entry missing 'source_path'"));
		}
		if (!(*FaceObject)->TryGetStringField(TEXT("name"), Face.Name))
		{
			return CreateErrorResponse(TEXT("Face entry missing 'name' (e.g. 'Regular', 'Bold', 'Medium')"));
		}
		Faces.Add(MoveTemp(Face));
	}

	return RunOnGameThread([PackagePath, FontName, Faces, Hinting]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UAIAssetImportBuilder::ImportFont(PackagePath, FontName, Faces, Hinting, Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to import font"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Import font timed out"), 120.0);
}
}
