// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/AIExporterBase.h"
#include "AITextureExporter.generated.h"

class UTexture2D;

/**
 * Exporter for Texture assets to PNG format.
 *
 * Handles:
 * - UTexture2D -> PNG file export
 *
 * Unlike other exporters that output text, this exporter produces:
 * - A metadata text file (texture info: size, format, etc.)
 * - A PNG image file (actual texture data)
 *
 * Priority: 50 (standard)
 */
UCLASS()
class COMMONAIEXPORT_API UAITextureExporter : public UAIExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UAIExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 50; }
	virtual FString GetExporterDisplayName() const override { return TEXT("TextureExporter"); }
	//~ End UAIExporterBase Interface

	/**
	 * Export texture to PNG file.
	 * @param Texture The texture to export
	 * @param OutputPath Full path for the PNG file (including .png extension)
	 * @return true if export was successful
	 */
	bool ExportTextureToPNG(UTexture2D* Texture, const FString& OutputPath);

protected:
	/** Export texture metadata as text */
	FString ExportTextureMetadata(UTexture2D* Texture, bool bFilterDefaults);
};
