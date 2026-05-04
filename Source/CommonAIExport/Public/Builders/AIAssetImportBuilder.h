// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AIAssetImportBuilder.generated.h"

class FJsonObject;

struct FAIImportFontFaceSpec
{
	FString SourcePath;
	FString Name;
};

/**
 * Static utility class for importing external files into Unreal assets.
 *
 * Command handlers own transport concerns; this builder owns file validation,
 * import factories, package creation, asset registry notifications, and save
 * bookkeeping. Call from the Game Thread only.
 */
UCLASS()
class COMMONAIEXPORT_API UAIAssetImportBuilder : public UObject
{
	GENERATED_BODY()

public:
	/** Import an image file as a UTexture2D asset. */
	static TSharedPtr<FJsonObject> ImportTexture(
		const FString& SourcePath,
		const FString& PackagePath,
		const FString& AssetName,
		const FString& Compression,
		const FString& MipGen,
		const FString& LODGroup,
		bool bSRGB,
		FString& OutError);

	/** Import font face files and build a composite UFont asset. */
	static TSharedPtr<FJsonObject> ImportFont(
		const FString& PackagePath,
		const FString& FontName,
		const TArray<FAIImportFontFaceSpec>& Faces,
		const FString& Hinting,
		FString& OutError);
};
