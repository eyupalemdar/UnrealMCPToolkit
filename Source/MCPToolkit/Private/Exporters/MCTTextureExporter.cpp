// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTTextureExporter.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"

bool UMCTTextureExporter::CanExport(UObject* Asset) const
{
	if (!Asset)
	{
		return false;
	}

	return Asset->IsA<UTexture2D>();
}

TArray<UClass*> UMCTTextureExporter::GetSupportedClasses() const
{
	return {
		UTexture2D::StaticClass()
	};
}

FString UMCTTextureExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		return ExportTextureMetadata(Texture, bFilterDefaults);
	}

	return TEXT("Error: Not a valid Texture2D asset\n");
}

FString UMCTTextureExporter::ExportTextureMetadata(UTexture2D* Texture, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("TEXTURE: %s"), *Texture->GetName()));

	// Basic Info
	Output += FString::Printf(TEXT("Path: %s\n"), *Texture->GetPathName());
	Output += FString::Printf(TEXT("Class: %s\n"), *Texture->GetClass()->GetName());

	// Dimensions
	const int32 SizeX = Texture->GetSizeX();
	const int32 SizeY = Texture->GetSizeY();
	Output += FString::Printf(TEXT("Size: %dx%d\n"), SizeX, SizeY);

	// Format
	const EPixelFormat PixelFormat = Texture->GetPixelFormat();
	Output += FString::Printf(TEXT("PixelFormat: %s\n"), GetPixelFormatString(PixelFormat));

	// Mip Levels
	const int32 NumMips = Texture->GetNumMips();
	Output += FString::Printf(TEXT("MipLevels: %d\n"), NumMips);

	// Compression Settings
	Output += TEXT("\n");
	Output += MakeSubsectionHeader(TEXT("Compression"));
	Output += FString::Printf(TEXT("CompressionSettings: %s\n"),
		*UEnum::GetValueAsString(Texture->CompressionSettings));

	// LOD Settings
	Output += TEXT("\n");
	Output += MakeSubsectionHeader(TEXT("LOD"));
	Output += FString::Printf(TEXT("LODGroup: %s\n"),
		*UEnum::GetValueAsString(Texture->LODGroup));
	Output += FString::Printf(TEXT("LODBias: %d\n"), Texture->LODBias);

	// Filter & Address Mode
	Output += TEXT("\n");
	Output += MakeSubsectionHeader(TEXT("Sampling"));
	Output += FString::Printf(TEXT("Filter: %s\n"),
		*UEnum::GetValueAsString(Texture->Filter));

	// sRGB
	Output += FString::Printf(TEXT("SRGB: %s\n"),
		Texture->SRGB ? TEXT("True") : TEXT("False"));

	// Source Info (if available)
	if (Texture->Source.IsValid())
	{
		Output += TEXT("\n");
		Output += MakeSubsectionHeader(TEXT("Source"));
		Output += FString::Printf(TEXT("SourceWidth: %d\n"), Texture->Source.GetSizeX());
		Output += FString::Printf(TEXT("SourceHeight: %d\n"), Texture->Source.GetSizeY());
		Output += FString::Printf(TEXT("SourceFormat: %s\n"),
			*UEnum::GetValueAsString(Texture->Source.GetFormat()));
	}

	return Output;
}

bool UMCTTextureExporter::ExportTextureToPNG(UTexture2D* Texture, const FString& OutputPath)
{
	if (!Texture)
	{
		UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Texture is null"));
		return false;
	}

	// Ensure texture is fully loaded
	Texture->SetForceMipLevelsToBeResident(30.0f);
	Texture->WaitForStreaming();

	// Get platform data
	FTexturePlatformData* PlatformData = Texture->GetPlatformData();
	if (!PlatformData || PlatformData->Mips.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: No platform data or mips available for %s"), *Texture->GetName());
		return false;
	}

	// Get the first mip level
	FTexture2DMipMap& Mip = PlatformData->Mips[0];
	const int32 Width = Mip.SizeX;
	const int32 Height = Mip.SizeY;

	// Load ImageWrapper module
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Failed to create ImageWrapper for PNG"));
		return false;
	}

	// Determine the pixel format and set raw data
	const EPixelFormat PixelFormat = PlatformData->PixelFormat;
	bool bSetRawSuccess = false;

	// For uncompressed formats, use platform data directly
	if (PixelFormat == PF_B8G8R8A8 || PixelFormat == PF_R8G8B8A8 || PixelFormat == PF_G8)
	{
		// Lock the bulk data for reading
		const void* RawData = Mip.BulkData.LockReadOnly();
		if (!RawData)
		{
			UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Failed to lock texture data for %s"), *Texture->GetName());
			return false;
		}

		ERGBFormat RGBFormat = ERGBFormat::BGRA;
		int32 DataSize = Width * Height * 4;

		if (PixelFormat == PF_B8G8R8A8)
		{
			RGBFormat = ERGBFormat::BGRA;
		}
		else if (PixelFormat == PF_R8G8B8A8)
		{
			RGBFormat = ERGBFormat::RGBA;
		}
		else if (PixelFormat == PF_G8)
		{
			RGBFormat = ERGBFormat::Gray;
			DataSize = Width * Height;
		}

		bSetRawSuccess = ImageWrapper->SetRaw(RawData, DataSize, Width, Height, RGBFormat, 8);
		Mip.BulkData.Unlock();
	}
	else
	{
		// For compressed formats (DXT, BC, etc.), we need to use the texture source data
		if (Texture->Source.IsValid())
		{
			TArray64<uint8> SourceData;
			if (Texture->Source.GetMipData(SourceData, 0))
			{
				const int32 SourceWidth = Texture->Source.GetSizeX();
				const int32 SourceHeight = Texture->Source.GetSizeY();
				const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();

				ERGBFormat RGBFormat = ERGBFormat::BGRA;
				int32 BitDepth = 8;

				switch (SourceFormat)
				{
				case TSF_BGRA8:
					RGBFormat = ERGBFormat::BGRA;
					break;
				case TSF_G8:
					RGBFormat = ERGBFormat::Gray;
					break;
				case TSF_RGBA16:
				case TSF_RGBA16F:
					RGBFormat = ERGBFormat::RGBA;
					BitDepth = 16;
					break;
				case TSF_G16:
					RGBFormat = ERGBFormat::Gray;
					BitDepth = 16;
					break;
				default:
					// TSF_BGRA8 is the most common format, use as fallback
					UE_LOG(LogTemp, Log, TEXT("MCTTextureExporter: Source format %d for %s, using BGRA conversion"),
						(int32)SourceFormat, *Texture->GetName());
					RGBFormat = ERGBFormat::BGRA;
					break;
				}

				bSetRawSuccess = ImageWrapper->SetRaw(SourceData.GetData(), SourceData.Num(), SourceWidth, SourceHeight, RGBFormat, BitDepth);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Failed to get source mip data for %s"), *Texture->GetName());
				return false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Compressed texture %s (format %d) has no source data available"),
				*Texture->GetName(), (int32)PixelFormat);
			return false;
		}
	}

	if (!bSetRawSuccess)
	{
		UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Failed to set raw image data for %s"), *Texture->GetName());
		return false;
	}

	// Get compressed PNG data - GetCompressed returns the array directly
	TArray64<uint8> PNGData = ImageWrapper->GetCompressed(0); // 0 = default quality
	if (PNGData.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Failed to compress image to PNG for %s"), *Texture->GetName());
		return false;
	}

	// Ensure output directory exists
	const FString OutputDir = FPaths::GetPath(OutputPath);
	if (!FPaths::DirectoryExists(OutputDir))
	{
		IFileManager::Get().MakeDirectory(*OutputDir, true);
	}

	// Save to file
	if (!FFileHelper::SaveArrayToFile(PNGData, *OutputPath))
	{
		UE_LOG(LogTemp, Error, TEXT("MCTTextureExporter: Failed to save PNG to %s"), *OutputPath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("MCTTextureExporter: Successfully exported %s to %s (%dx%d)"),
		*Texture->GetName(), *OutputPath, Width, Height);

	return true;
}
