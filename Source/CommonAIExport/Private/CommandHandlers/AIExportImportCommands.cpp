// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportImportCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"
#include "AIExportFunctionLibrary.h"
#include "Builders/AIWidgetBlueprintBuilder.h"
#include "Builders/AIMaterialBuilder.h"
#include "Builders/AIBlueprintGraphBuilder.h"
#include "Builders/AIDataAssetBuilder.h"
#include "Builders/AIAssetFactory.h"
#include "Builders/AIAnimBlueprintBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#include "WidgetBlueprint.h"
#include "Components/Widget.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/StringOutputDevice.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "CoreGlobals.h"
#include "HAL/RunnableThread.h"
#include "Misc/EngineVersion.h"
#include "Misc/App.h"

#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

// Asset rename (HandleRenameAsset)
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// Asset delete (HandleDeleteAsset) — ObjectTools lives in UnrealEd
#include "ObjectTools.h"
#include "UObject/ObjectRedirector.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "InputMappingContext.h"
#include "Animation/AnimBlueprint.h"

// Widget Preview Capture includes (for HandleCaptureWidgetPreview)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "ContentStreaming.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "CommonActivatableWidget.h"
#include "CommonUserWidget.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBinding.h"
#include "Input/UIActionBindingHandle.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "ICommonInputModule.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "PlayInEditorDataTypes.h"
#include "RenderingThread.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreaming.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/StreamableRenderAsset.h"
#include "Engine/GameViewportClient.h"
#include "RenderAssetUpdate.h"
#include "Misc/Base64.h"
#include "ScopedTransaction.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"
#include "Engine/GameInstance.h"
#include "Engine/LatentActionManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "CommonInputSubsystem.h"
#include "AudioDeviceHandle.h"
#include "AudioDeviceManager.h"
#include "Components/InputComponent.h"
#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundEffectSource.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/OnlineSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "GenericTeamAgentInterface.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/RecastNavMesh.h"
#include "AI/Navigation/NavigationBounds.h"
#include "AI/Navigation/NavigationDataResolution.h"
#include "AI/Navigation/NavigationInvokerPriority.h"
#include "AI/Navigation/NavAgentSelector.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISenseConfig.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "TimerManager.h"

// Asset Lifecycle includes (for HandleReloadAsset)
#include "Subsystems/AssetEditorSubsystem.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Interfaces/IPluginManager.h"


namespace CommonAIExport::CommandHandlers::Import
{
FString HandleImportTexture(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString SourcePath, PackagePath, AssetName;
	FString Compression = TEXT("UserInterface2D");
	FString MipGen = TEXT("NoMipmaps");
	FString LODGroup = TEXT("UI");
	bool bSRGB = true;

	if (!Params->TryGetStringField(TEXT("source_path"), SourcePath))
		return CreateErrorResponse(TEXT("Missing 'source_path' parameter"));
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));

	Params->TryGetStringField(TEXT("asset_name"), AssetName);
	Params->TryGetStringField(TEXT("compression"), Compression);
	Params->TryGetStringField(TEXT("mip_gen"), MipGen);
	Params->TryGetStringField(TEXT("lod_group"), LODGroup);
	Params->TryGetBoolField(TEXT("srgb"), bSRGB);

	// Normalize path separators
	SourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Verify source file exists
	if (!FPaths::FileExists(SourcePath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Source file not found: %s"), *SourcePath));
	}

	// Derive asset name from filename if not provided
	if (AssetName.IsEmpty())
	{
		AssetName = FPaths::GetBaseFilename(SourcePath);
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [SourcePath, PackagePath, AssetName, Compression, MipGen, LODGroup, bSRGB, Promise]()
	{
		// Build full package name
		FString FullPackagePath = PackagePath / AssetName;

		// Create package
		UPackage* Package = CreatePackage(*FullPackagePath);
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *FullPackagePath)));
			return;
		}
		Package->FullyLoad();

		// Read file data
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *SourcePath))
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to read file: %s"), *SourcePath)));
			return;
		}

		// Create texture factory
		UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
		TextureFactory->AddToRoot(); // Prevent GC during import
		TextureFactory->SuppressImportOverwriteDialog();

		// Import
		const uint8* DataPtr = FileData.GetData();
		UObject* ImportedObject = TextureFactory->FactoryCreateBinary(
			UTexture2D::StaticClass(),
			Package,
			*AssetName,
			RF_Public | RF_Standalone,
			nullptr,
			*FPaths::GetExtension(SourcePath),
			DataPtr,
			DataPtr + FileData.Num(),
			GWarn
		);

		TextureFactory->RemoveFromRoot();

		UTexture2D* Texture = Cast<UTexture2D>(ImportedObject);
		if (!Texture)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to import texture")));
			return;
		}

		// Apply compression settings
		if (Compression == TEXT("Default"))
			Texture->CompressionSettings = TC_Default;
		else if (Compression == TEXT("NormalMap") || Compression == TEXT("Normalmap"))
			Texture->CompressionSettings = TC_Normalmap;
		else if (Compression == TEXT("Masks"))
			Texture->CompressionSettings = TC_Masks;
		else if (Compression == TEXT("Grayscale") || Compression == TEXT("Displacementmap"))
			Texture->CompressionSettings = TC_Displacementmap;
		else if (Compression == TEXT("HDR"))
			Texture->CompressionSettings = TC_HDR;
		else if (Compression == TEXT("UserInterface2D") || Compression == TEXT("UI"))
			Texture->CompressionSettings = TC_EditorIcon;
		else if (Compression == TEXT("Alpha"))
			Texture->CompressionSettings = TC_Alpha;
		else
			Texture->CompressionSettings = TC_EditorIcon; // Default to UI

		// Apply sRGB
		Texture->SRGB = bSRGB;

		// Apply MipGen settings
		if (MipGen == TEXT("NoMipmaps"))
			Texture->MipGenSettings = TMGS_NoMipmaps;
		else if (MipGen == TEXT("FromTextureGroup"))
			Texture->MipGenSettings = TMGS_FromTextureGroup;
		else if (MipGen == TEXT("Sharpen0"))
			Texture->MipGenSettings = TMGS_Sharpen0;
		else if (MipGen == TEXT("Sharpen"))
			Texture->MipGenSettings = TMGS_Sharpen0;
		else if (MipGen == TEXT("Blur"))
			Texture->MipGenSettings = TMGS_Blur1;
		else
			Texture->MipGenSettings = TMGS_NoMipmaps;

		// Apply LOD Group
		if (LODGroup == TEXT("World"))
			Texture->LODGroup = TEXTUREGROUP_World;
		else if (LODGroup == TEXT("WorldNormalMap"))
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
		else if (LODGroup == TEXT("WorldSpecular"))
			Texture->LODGroup = TEXTUREGROUP_WorldSpecular;
		else if (LODGroup == TEXT("Character"))
			Texture->LODGroup = TEXTUREGROUP_Character;
		else if (LODGroup == TEXT("CharacterNormalMap"))
			Texture->LODGroup = TEXTUREGROUP_CharacterNormalMap;
		else if (LODGroup == TEXT("Effects"))
			Texture->LODGroup = TEXTUREGROUP_Effects;
		else if (LODGroup == TEXT("UI"))
			Texture->LODGroup = TEXTUREGROUP_UI;
		else if (LODGroup == TEXT("Lightmap"))
			Texture->LODGroup = TEXTUREGROUP_Lightmap;
		else if (LODGroup == TEXT("Shadowmap"))
			Texture->LODGroup = TEXTUREGROUP_Shadowmap;
		else
			Texture->LODGroup = TEXTUREGROUP_UI;

		// Apply changes and save
		Texture->PostEditChange();
		Texture->UpdateResource();
		Package->MarkPackageDirty();

		// Save the package
		FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs);

		// Notify asset registry
		FAssetRegistryModule::AssetCreated(Texture);

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Texture->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetNumberField(TEXT("width"), Texture->GetSizeX());
		Data->SetNumberField(TEXT("height"), Texture->GetSizeY());
		Data->SetStringField(TEXT("format"), UEnum::GetValueAsString(Texture->GetPixelFormat()));
		Data->SetBoolField(TEXT("saved"), bSaved);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Import texture timed out"));
	return Future.Get();
}



FString HandleImportFont(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, FontName;
	FString Hinting = TEXT("Auto");

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("font_name"), FontName))
		return CreateErrorResponse(TEXT("Missing 'font_name' parameter"));

	Params->TryGetStringField(TEXT("hinting"), Hinting);

	// Parse faces array
	const TArray<TSharedPtr<FJsonValue>>* FacesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("faces"), FacesArray) || !FacesArray || FacesArray->Num() == 0)
	{
		return CreateErrorResponse(TEXT("Missing or empty 'faces' array. Each entry needs 'source_path' and 'name' (e.g. 'Regular', 'Bold')."));
	}

	// Validate all face entries before processing
	struct FFaceEntry
	{
		FString SourcePath;
		FString Name;
	};
	TArray<FFaceEntry> Faces;

	for (const auto& FaceValue : *FacesArray)
	{
		const TSharedPtr<FJsonObject>* FaceObj = nullptr;
		if (!FaceValue->TryGetObject(FaceObj) || !FaceObj || !(*FaceObj).IsValid())
		{
			return CreateErrorResponse(TEXT("Each face entry must be a JSON object with 'source_path' and 'name'"));
		}

		FFaceEntry Entry;
		if (!(*FaceObj)->TryGetStringField(TEXT("source_path"), Entry.SourcePath))
			return CreateErrorResponse(TEXT("Face entry missing 'source_path'"));
		if (!(*FaceObj)->TryGetStringField(TEXT("name"), Entry.Name))
			return CreateErrorResponse(TEXT("Face entry missing 'name' (e.g. 'Regular', 'Bold', 'Medium')"));

		Entry.SourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (!FPaths::FileExists(Entry.SourcePath))
			return CreateErrorResponse(FString::Printf(TEXT("Font file not found: %s"), *Entry.SourcePath));

		Faces.Add(MoveTemp(Entry));
	}

	// Resolve hinting enum
	EFontHinting HintingEnum = EFontHinting::Auto;
	if (Hinting == TEXT("None"))
		HintingEnum = EFontHinting::None;
	else if (Hinting == TEXT("Auto"))
		HintingEnum = EFontHinting::Auto;
	else if (Hinting == TEXT("AutoLight"))
		HintingEnum = EFontHinting::AutoLight;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, FontName, Faces, HintingEnum, Promise]()
	{
		TArray<TSharedPtr<FJsonObject>> FaceResults;

		// Step 1: Create UFontFace for each TTF/OTF
		TArray<UFontFace*> FontFaceAssets;
		for (const auto& Face : Faces)
		{
			FString FaceName = FontName + TEXT("-") + Face.Name;
			FString FacePackagePath = PackagePath / FaceName;

			UPackage* FacePackage = CreatePackage(*FacePackagePath);
			if (!FacePackage)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create package for font face: %s"), *FaceName)));
				return;
			}
			FacePackage->FullyLoad();

			// Read font file data
			TArray<uint8> FontData;
			if (!FFileHelper::LoadFileToArray(FontData, *Face.SourcePath))
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to read font file: %s"), *Face.SourcePath)));
				return;
			}

			// Create UFontFace
			UFontFace* FontFace = NewObject<UFontFace>(FacePackage, *FaceName, RF_Public | RF_Standalone);
			FontFace->SourceFilename = Face.SourcePath;
			FontFace->Hinting = HintingEnum;
			FontFace->LoadingPolicy = EFontLoadingPolicy::Inline;

			// Load font data into the asset
			FontFace->FontFaceData = MakeShared<FFontFaceData, ESPMode::ThreadSafe>(MoveTemp(FontData));

			FontFace->PostEditChange();
			FacePackage->MarkPackageDirty();

			// Save FontFace
			FString FaceFilename = FPackageName::LongPackageNameToFilename(FacePackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(FacePackage, FontFace, *FaceFilename, SaveArgs);

			FAssetRegistryModule::AssetCreated(FontFace);
			FontFaceAssets.Add(FontFace);

			// Track result
			TSharedPtr<FJsonObject> FaceResult = MakeShared<FJsonObject>();
			FaceResult->SetStringField(TEXT("name"), Face.Name);
			FaceResult->SetStringField(TEXT("asset_path"), FontFace->GetPathName());
			FaceResults.Add(FaceResult);
		}

		// Step 2: Create Composite UFont
		FString CompositePath = PackagePath / FontName;
		UPackage* FontPackage = CreatePackage(*CompositePath);
		if (!FontPackage)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create composite font package")));
			return;
		}
		FontPackage->FullyLoad();

		UFont* CompositeFont = NewObject<UFont>(FontPackage, *FontName, RF_Public | RF_Standalone);
		CompositeFont->FontCacheType = EFontCacheType::Runtime;

		// Build typeface entries
		FTypeface& DefaultTypeface = CompositeFont->GetMutableInternalCompositeFont().DefaultTypeface;
		DefaultTypeface.Fonts.Empty();

		for (int32 i = 0; i < FontFaceAssets.Num(); ++i)
		{
			FTypefaceEntry& Entry = DefaultTypeface.Fonts.AddDefaulted_GetRef();
			Entry.Name = *Faces[i].Name;
			Entry.Font = FFontData(FontFaceAssets[i]);
		}

		CompositeFont->PostEditChange();
		FontPackage->MarkPackageDirty();

		// Save composite font
		FString FontFilename = FPackageName::LongPackageNameToFilename(CompositePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(FontPackage, CompositeFont, *FontFilename, SaveArgs);

		FAssetRegistryModule::AssetCreated(CompositeFont);

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("font_asset_path"), CompositeFont->GetPathName());
		Data->SetStringField(TEXT("font_name"), FontName);
		Data->SetNumberField(TEXT("face_count"), FontFaceAssets.Num());
		Data->SetBoolField(TEXT("saved"), bSaved);

		TArray<TSharedPtr<FJsonValue>> FaceResultValues;
		for (const auto& FR : FaceResults)
		{
			FaceResultValues.Add(MakeShared<FJsonValueObject>(FR));
		}
		Data->SetArrayField(TEXT("faces"), FaceResultValues);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Import font timed out"));
	return Future.Get();
}


}
