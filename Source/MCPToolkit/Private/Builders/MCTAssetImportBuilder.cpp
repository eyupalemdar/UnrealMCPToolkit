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
TextureCompressionSettings ParseTextureCompression(const FString& Compression)
{
	if (Compression == TEXT("Default"))
	{
		return TC_Default;
	}
	if (Compression == TEXT("NormalMap") || Compression == TEXT("Normalmap"))
	{
		return TC_Normalmap;
	}
	if (Compression == TEXT("Masks"))
	{
		return TC_Masks;
	}
	if (Compression == TEXT("Grayscale") || Compression == TEXT("Displacementmap"))
	{
		return TC_Displacementmap;
	}
	if (Compression == TEXT("HDR"))
	{
		return TC_HDR;
	}
	if (Compression == TEXT("Alpha"))
	{
		return TC_Alpha;
	}
	return TC_EditorIcon;
}

TextureMipGenSettings ParseMipGenSettings(const FString& MipGen)
{
	if (MipGen == TEXT("FromTextureGroup"))
	{
		return TMGS_FromTextureGroup;
	}
	if (MipGen == TEXT("Sharpen0") || MipGen == TEXT("Sharpen"))
	{
		return TMGS_Sharpen0;
	}
	if (MipGen == TEXT("Blur"))
	{
		return TMGS_Blur1;
	}
	return TMGS_NoMipmaps;
}

TextureGroup ParseTextureGroup(const FString& LODGroup)
{
	if (LODGroup == TEXT("World"))
	{
		return TEXTUREGROUP_World;
	}
	if (LODGroup == TEXT("WorldNormalMap"))
	{
		return TEXTUREGROUP_WorldNormalMap;
	}
	if (LODGroup == TEXT("WorldSpecular"))
	{
		return TEXTUREGROUP_WorldSpecular;
	}
	if (LODGroup == TEXT("Character"))
	{
		return TEXTUREGROUP_Character;
	}
	if (LODGroup == TEXT("CharacterNormalMap"))
	{
		return TEXTUREGROUP_CharacterNormalMap;
	}
	if (LODGroup == TEXT("Effects"))
	{
		return TEXTUREGROUP_Effects;
	}
	if (LODGroup == TEXT("Lightmap"))
	{
		return TEXTUREGROUP_Lightmap;
	}
	if (LODGroup == TEXT("Shadowmap"))
	{
		return TEXTUREGROUP_Shadowmap;
	}
	return TEXTUREGROUP_UI;
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

	const TextureCompressionSettings ParsedCompression = ParseTextureCompression(Compression);
	const TextureMipGenSettings ParsedMipGen = ParseMipGenSettings(MipGen);
	const TextureGroup ParsedLODGroup = ParseTextureGroup(LODGroup);

	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	if (!TextureFactory)
	{
		OutError = TEXT("Failed to create texture factory");
		return nullptr;
	}

	TextureFactory->CompressionSettings = ParsedCompression;
	TextureFactory->MipGenSettings = ParsedMipGen;
	TextureFactory->LODGroup = ParsedLODGroup;
	TextureFactory->ColorSpaceMode = bSRGB ? ETextureSourceColorSpace::SRGB : ETextureSourceColorSpace::Linear;
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
	Texture->SRGB = bSRGB;
	Texture->MipGenSettings = ParsedMipGen;
	Texture->LODGroup = ParsedLODGroup;
	Texture->PostEditChange();
	Texture->UpdateResource();
	Package->MarkPackageDirty();

	const bool bSaved = SavePackageForAsset(Package, Texture, FullPackagePath);
	FAssetRegistryModule::AssetCreated(Texture);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("asset_path"), Texture->GetPathName());
	Data->SetStringField(TEXT("asset_name"), EffectiveAssetName);
	Data->SetNumberField(TEXT("width"), Texture->GetSizeX());
	Data->SetNumberField(TEXT("height"), Texture->GetSizeY());
	Data->SetStringField(TEXT("format"), UEnum::GetValueAsString(Texture->GetPixelFormat()));
	Data->SetBoolField(TEXT("saved"), bSaved);
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
