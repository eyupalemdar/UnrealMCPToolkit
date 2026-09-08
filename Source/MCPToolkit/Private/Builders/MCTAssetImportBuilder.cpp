// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/MCTAssetImportBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Fonts/CompositeFont.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
bool IsSRGBGammaSpace(const EGammaSpace GammaSpace)
{
	return GammaSpace == EGammaSpace::sRGB || GammaSpace == EGammaSpace::Pow22;
}

FString GammaSpaceToString(const EGammaSpace GammaSpace)
{
	switch (GammaSpace)
	{
	case EGammaSpace::Linear:
		return TEXT("Linear");
	case EGammaSpace::sRGB:
		return TEXT("sRGB");
	case EGammaSpace::Pow22:
		return TEXT("Pow22");
	case EGammaSpace::Invalid:
	default:
		return TEXT("Invalid");
	}
}

EFontHinting ParseFontHinting(const FString& Hinting)
{
	if (Hinting == TEXT("None"))
	{
		return EFontHinting::None;
	}
	if (Hinting == TEXT("AutoLight"))
	{
		return EFontHinting::AutoLight;
	}
	return EFontHinting::Auto;
}

bool SavePackageForAsset(UPackage* Package, UObject* Asset, const FString& LongPackagePath)
{
	if (!Package || !Asset)
	{
		return false;
	}

	const FString PackageFilename = FPackageName::LongPackageNameToFilename(LongPackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
}
}

bool UMCTAssetImportBuilder::ParseTextureSettings(const FString& Compression, const FString& MipGen,
	const FString& LODGroup, TextureCompressionSettings& OutCompression,
	TextureMipGenSettings& OutMipGen, TextureGroup& OutGroup, FString& OutError)
{
	auto Parse = [&OutError](UEnum* Enum, FString Value, const TCHAR* Prefix, const TCHAR* Field, int64& Out)
	{
		if (!Value.StartsWith(Prefix)) { Value = FString(Prefix) + Value; }
		Out = Enum->GetValueByNameString(Value);
		if (Out == INDEX_NONE || Value.EndsWith(TEXT("_MAX")))
		{
			OutError = FString::Printf(TEXT("Unsupported %s: %s"), Field, *Value);
			return false;
		}
		return true;
	};
	FString CompressionName = Compression;
	if (CompressionName == TEXT("UserInterface2D")) { CompressionName = TEXT("EditorIcon"); }
	if (CompressionName == TEXT("NormalMap")) { CompressionName = TEXT("Normalmap"); }
	FString MipName = MipGen;
	if (MipName == TEXT("Sharpen")) { MipName = TEXT("Sharpen0"); }
	if (MipName == TEXT("Blur")) { MipName = TEXT("Blur1"); }
	int64 C, M, G;
	if (!Parse(StaticEnum<TextureCompressionSettings>(), CompressionName, TEXT("TC_"), TEXT("compression"), C)
		|| !Parse(StaticEnum<TextureMipGenSettings>(), MipName, TEXT("TMGS_"), TEXT("mip_gen"), M)
		|| !Parse(StaticEnum<TextureGroup>(), LODGroup, TEXT("TEXTUREGROUP_"), TEXT("lod_group"), G))
	{
		return false;
	}
	OutCompression = static_cast<TextureCompressionSettings>(C);
	OutMipGen = static_cast<TextureMipGenSettings>(M);
	OutGroup = static_cast<TextureGroup>(G);
	return true;
}

TSharedPtr<FJsonObject> UMCTAssetImportBuilder::ImportTexture(
	const FString& SourcePath,
	const FString& PackagePath,
	const FString& AssetName,
	const FString& Compression,
	const FString& MipGen,
	const FString& LODGroup,
	const bool bSRGB,
	FString& OutError)
{
	FString NormalizedSourcePath = SourcePath;
	NormalizedSourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (!FPaths::FileExists(NormalizedSourcePath))
	{
		OutError = FString::Printf(TEXT("Source file not found: %s"), *NormalizedSourcePath);
		return nullptr;
	}

	TextureCompressionSettings ParsedCompression;
	TextureMipGenSettings ParsedMipGen;
	TextureGroup ParsedLODGroup;
	if (!ParseTextureSettings(Compression, MipGen, LODGroup, ParsedCompression, ParsedMipGen, ParsedLODGroup, OutError))
	{
		return nullptr;
	}

	FString EffectiveAssetName = AssetName;
	if (EffectiveAssetName.IsEmpty())
	{
		EffectiveAssetName = FPaths::GetBaseFilename(NormalizedSourcePath);
	}

	const FString FullPackagePath = PackagePath / EffectiveAssetName;
	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Failed to create package: %s"), *FullPackagePath);
		return nullptr;
	}
	Package->FullyLoad();

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *NormalizedSourcePath))
	{
		OutError = FString::Printf(TEXT("Failed to read file: %s"), *NormalizedSourcePath);
		return nullptr;
	}


	const FString FullObjectPath = FString::Printf(TEXT("%s.%s"), *FullPackagePath, *EffectiveAssetName);
	UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr, *FullObjectPath);
	bool bEffectiveSRGB = bSRGB;
	bool bPreservedExistingSourceGamma = false;
	FString ExistingSourceGammaString;
	if (ExistingTexture && ExistingTexture->Source.IsValid() && ExistingTexture->Source.GetNumLayers() > 0)
	{
		const EGammaSpace ExistingSourceGamma = ExistingTexture->Source.GetGammaSpace(0);
		const bool bExistingSourceSRGB = IsSRGBGammaSpace(ExistingSourceGamma);
		ExistingSourceGammaString = GammaSpaceToString(ExistingSourceGamma);
		if (bExistingSourceSRGB != bSRGB)
		{
			bEffectiveSRGB = bExistingSourceSRGB;
			bPreservedExistingSourceGamma = true;
		}
	}

	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	if (!TextureFactory)
	{
		OutError = TEXT("Failed to create texture factory");
		return nullptr;
	}

	TextureFactory->CompressionSettings = ParsedCompression;
	TextureFactory->MipGenSettings = ParsedMipGen;
	TextureFactory->LODGroup = ParsedLODGroup;
	TextureFactory->ColorSpaceMode = bEffectiveSRGB ? ETextureSourceColorSpace::SRGB : ETextureSourceColorSpace::Linear;
	TextureFactory->bDeferCompression = true;

	TextureFactory->AddToRoot();
	ON_SCOPE_EXIT
	{
		TextureFactory->RemoveFromRoot();
	};
	UTextureFactory::SuppressImportOverwriteDialog();

	const uint8* DataPtr = FileData.GetData();
	UObject* ImportedObject = TextureFactory->FactoryCreateBinary(
		UTexture2D::StaticClass(),
		Package,
		*EffectiveAssetName,
		RF_Public | RF_Standalone,
		nullptr,
		*FPaths::GetExtension(NormalizedSourcePath),
		DataPtr,
		DataPtr + FileData.Num(),
		GWarn);

	UTexture2D* Texture = Cast<UTexture2D>(ImportedObject);
	if (!Texture)
	{
		OutError = TEXT("Failed to import texture");
		return nullptr;
	}

	Texture->CompressionSettings = ParsedCompression;
	Texture->SRGB = bEffectiveSRGB;
	Texture->MipGenSettings = ParsedMipGen;
	Texture->LODGroup = ParsedLODGroup;
	Texture->PostEditChange();
	Texture->UpdateResource();
	Package->MarkPackageDirty();

	const bool bSaved = SavePackageForAsset(Package, Texture, FullPackagePath);
	if (!bSaved)
	{
		OutError = FString::Printf(TEXT("Imported texture could not be saved: %s"), *FullPackagePath);
		return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Texture);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Texture->GetPathName());
	Data->SetStringField(TEXT("asset_name"), EffectiveAssetName);
	Data->SetNumberField(TEXT("width"), Texture->GetSizeX());
	Data->SetNumberField(TEXT("height"), Texture->GetSizeY());
	Data->SetStringField(TEXT("format"), UEnum::GetValueAsString(Texture->GetPixelFormat()));
	Data->SetBoolField(TEXT("requested_srgb"), bSRGB);
	Data->SetBoolField(TEXT("effective_srgb"), bEffectiveSRGB);
	Data->SetBoolField(TEXT("preserved_existing_source_gamma"), bPreservedExistingSourceGamma);
	if (!ExistingSourceGammaString.IsEmpty())
	{
		Data->SetStringField(TEXT("existing_source_gamma"), ExistingSourceGammaString);
	}
	Data->SetBoolField(TEXT("saved"), bSaved);
	Data->SetNumberField(TEXT("source_mip_count"), Texture->Source.GetNumMips());
	Data->SetNumberField(TEXT("runtime_mip_count"), Texture->GetNumMips());
	Data->SetStringField(TEXT("effective_mip_gen"), UEnum::GetValueAsString(Texture->MipGenSettings));
	Data->SetStringField(TEXT("effective_lod_group"), UEnum::GetValueAsString(Texture->LODGroup));
	return Data;
}

TSharedPtr<FJsonObject> UMCTAssetImportBuilder::ImportFont(
	const FString& PackagePath,
	const FString& FontName,
	const TArray<FMCTImportFontFaceSpec>& Faces,
	const FString& Hinting,
	FString& OutError)
{
	const EFontHinting HintingEnum = ParseFontHinting(Hinting);
	TArray<TSharedPtr<FJsonObject>> FaceResults;
	TArray<UFontFace*> FontFaceAssets;

	for (const FMCTImportFontFaceSpec& Face : Faces)
	{
		FString NormalizedSourcePath = Face.SourcePath;
		NormalizedSourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (!FPaths::FileExists(NormalizedSourcePath))
		{
			OutError = FString::Printf(TEXT("Font file not found: %s"), *NormalizedSourcePath);
			return nullptr;
		}

		const FString FaceName = FontName + TEXT("-") + Face.Name;
		const FString FacePackagePath = PackagePath / FaceName;
		UPackage* FacePackage = CreatePackage(*FacePackagePath);
		if (!FacePackage)
		{
			OutError = FString::Printf(TEXT("Failed to create package for font face: %s"), *FaceName);
			return nullptr;
		}
		FacePackage->FullyLoad();

		TArray<uint8> FontData;
		if (!FFileHelper::LoadFileToArray(FontData, *NormalizedSourcePath))
		{
			OutError = FString::Printf(TEXT("Failed to read font file: %s"), *NormalizedSourcePath);
			return nullptr;
		}

		UFontFace* FontFace = NewObject<UFontFace>(FacePackage, *FaceName, RF_Public | RF_Standalone);
		if (!FontFace)
		{
			OutError = FString::Printf(TEXT("Failed to create font face asset: %s"), *FaceName);
			return nullptr;
		}

		FontFace->SourceFilename = NormalizedSourcePath;
		FontFace->Hinting = HintingEnum;
		FontFace->LoadingPolicy = EFontLoadingPolicy::Inline;
		FontFace->FontFaceData->SetData(MoveTemp(FontData));
#if WITH_EDITORONLY_DATA
		FontFace->CacheSubFaces();
#endif
		FontFace->PostEditChange();
		FacePackage->MarkPackageDirty();
		SavePackageForAsset(FacePackage, FontFace, FacePackagePath);
		FAssetRegistryModule::AssetCreated(FontFace);
		FontFaceAssets.Add(FontFace);

		TSharedPtr<FJsonObject> FaceResult = MakeShared<FJsonObject>();
		FaceResult->SetStringField(TEXT("name"), Face.Name);
		FaceResult->SetStringField(TEXT("asset_path"), FontFace->GetPathName());
		FaceResults.Add(FaceResult);
	}

	const FString CompositePath = PackagePath / FontName;
	UPackage* FontPackage = CreatePackage(*CompositePath);
	if (!FontPackage)
	{
		OutError = TEXT("Failed to create composite font package");
		return nullptr;
	}
	FontPackage->FullyLoad();

	UFont* CompositeFont = NewObject<UFont>(FontPackage, *FontName, RF_Public | RF_Standalone);
	if (!CompositeFont)
	{
		OutError = FString::Printf(TEXT("Failed to create composite font asset: %s"), *FontName);
		return nullptr;
	}
	CompositeFont->FontCacheType = EFontCacheType::Runtime;

	FTypeface& DefaultTypeface = CompositeFont->GetMutableInternalCompositeFont().DefaultTypeface;
	DefaultTypeface.Fonts.Empty();
	for (int32 FaceIndex = 0; FaceIndex < FontFaceAssets.Num(); ++FaceIndex)
	{
		FTypefaceEntry& Entry = DefaultTypeface.Fonts.AddDefaulted_GetRef();
		Entry.Name = *Faces[FaceIndex].Name;
		Entry.Font = FFontData(FontFaceAssets[FaceIndex]);
	}

	CompositeFont->PostEditChange();
	FontPackage->MarkPackageDirty();
	const bool bSaved = SavePackageForAsset(FontPackage, CompositeFont, CompositePath);
	FAssetRegistryModule::AssetCreated(CompositeFont);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("font_asset_path"), CompositeFont->GetPathName());
	Data->SetStringField(TEXT("font_name"), FontName);
	Data->SetNumberField(TEXT("face_count"), FontFaceAssets.Num());
	Data->SetBoolField(TEXT("saved"), bSaved);

	TArray<TSharedPtr<FJsonValue>> FaceResultValues;
	for (const TSharedPtr<FJsonObject>& FaceResult : FaceResults)
	{
		FaceResultValues.Add(MakeShared<FJsonValueObject>(FaceResult));
	}
	Data->SetArrayField(TEXT("faces"), FaceResultValues);
	return Data;
}
