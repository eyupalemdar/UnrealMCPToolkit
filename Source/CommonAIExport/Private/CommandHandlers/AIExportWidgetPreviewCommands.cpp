// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportWidgetPreviewCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"
#include "CommonAIExportModule.h"
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


namespace CommonAIExport::CommandHandlers::WidgetPreview
{
FString HandleCaptureWidgetPreview(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	// Parse primitive parameters (use double then clamp/cast)
	int32 Width = 1920;
	int32 Height = 1080;
	int32 WarmupFrames = 3;
	float DPIScale = 1.0f;
	bool bTransparentBG = false;
	bool bReturnBase64 = false;
	FString OutputPath;
	FString PreviewMode = TEXT("runtime");
	struct FPreviewFunctionCall
	{
		FString WidgetName;
		FString FunctionName;
		TMap<FString, FString> Args;
	};
	TArray<FPreviewFunctionCall> PreviewFunctionCalls;

	{
		double DVal = 0.0;
		if (Params->TryGetNumberField(TEXT("width"), DVal))         Width = FMath::Clamp((int32)DVal, 16, 8192);
		if (Params->TryGetNumberField(TEXT("height"), DVal))        Height = FMath::Clamp((int32)DVal, 16, 8192);
		if (Params->TryGetNumberField(TEXT("warmup_frames"), DVal)) WarmupFrames = FMath::Clamp((int32)DVal, 1, 10);
		if (Params->TryGetNumberField(TEXT("dpi_scale"), DVal))     DPIScale = FMath::Clamp((float)DVal, 0.1f, 8.0f);
	}
	Params->TryGetBoolField(TEXT("transparent_bg"), bTransparentBG);
	Params->TryGetBoolField(TEXT("return_base64"), bReturnBase64);
	Params->TryGetStringField(TEXT("output_path"), OutputPath);
	Params->TryGetStringField(TEXT("preview_mode"), PreviewMode);
	PreviewMode.TrimStartAndEndInline();
	PreviewMode.ToLowerInline();
	if (PreviewMode.IsEmpty())
	{
		PreviewMode = TEXT("runtime");
	}
	if (PreviewMode != TEXT("runtime") && PreviewMode != TEXT("designer"))
	{
		return CreateErrorResponse(TEXT("Invalid 'preview_mode'. Expected 'runtime' or 'designer'."));
	}

	const TArray<TSharedPtr<FJsonValue>>* FunctionCallsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("preview_function_calls"), FunctionCallsArray) && FunctionCallsArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *FunctionCallsArray)
		{
			const TSharedPtr<FJsonObject>* CallObj = nullptr;
			if (!Entry->TryGetObject(CallObj) || !CallObj->IsValid())
			{
				continue;
			}

			FPreviewFunctionCall Call;
			(*CallObj)->TryGetStringField(TEXT("widget_name"), Call.WidgetName);
			if (!(*CallObj)->TryGetStringField(TEXT("function_name"), Call.FunctionName) || Call.FunctionName.IsEmpty())
			{
				return CreateErrorResponse(TEXT("Invalid 'preview_function_calls' entry: missing 'function_name'."));
			}

			const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
			if ((*CallObj)->TryGetObjectField(TEXT("args"), ArgsObj) && ArgsObj->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& ArgPair : (*ArgsObj)->Values)
				{
					FString ArgValue;
					if (ArgPair.Value->TryGetString(ArgValue))
					{
						Call.Args.Add(ArgPair.Key, ArgValue);
					}
					else
					{
						Call.Args.Add(ArgPair.Key, ArgPair.Value->AsString());
					}
				}
			}

			PreviewFunctionCalls.Add(MoveTemp(Call));
		}
	}

	// Parse optional ratios array (multi-ratio mode)
	// Each entry: { "width": 2560, "height": 1080, "label": "21x9" }
	struct FRatioEntry
	{
		int32 Width;
		int32 Height;
		FString Label;
	};
	TArray<FRatioEntry> Ratios;
	const TArray<TSharedPtr<FJsonValue>>* RatiosArray = nullptr;
	if (Params->TryGetArrayField(TEXT("ratios"), RatiosArray) && RatiosArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *RatiosArray)
		{
			const TSharedPtr<FJsonObject>* RatioObj = nullptr;
			if (!Entry->TryGetObject(RatioObj) || !RatioObj->IsValid()) continue;
			FRatioEntry R;
			double RW = 1920.0, RH = 1080.0;
			(*RatioObj)->TryGetNumberField(TEXT("width"), RW);
			(*RatioObj)->TryGetNumberField(TEXT("height"), RH);
			R.Width = FMath::Clamp((int32)RW, 16, 8192);
			R.Height = FMath::Clamp((int32)RH, 16, 8192);
			(*RatioObj)->TryGetStringField(TEXT("label"), R.Label);
			Ratios.Add(R);
		}
	}

	if (Ratios.Num() == 0)
	{
		FRatioEntry R;
		R.Width = Width;
		R.Height = Height;
		Ratios.Add(R);
	}

	// Output directory
	FString DefaultOutputDir = FPaths::ProjectIntermediateDir() / TEXT("WidgetCaptures");
	FString OutputDir = OutputPath.IsEmpty() ? DefaultOutputDir : FPaths::GetPath(OutputPath);
	IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Ratios, WarmupFrames, DPIScale, bTransparentBG, bReturnBase64, OutputPath, OutputDir, PreviewMode, PreviewFunctionCalls, Promise]()
	{
		// 1) Load Widget Blueprint
		UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
		if (!WidgetBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath)));
			return;
		}

		UClass* WidgetClass = WidgetBP->GeneratedClass;
		if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Invalid WidgetBlueprint GeneratedClass")));
			return;
		}

		// 2) Get editor world for widget context
		UWorld* World = nullptr;
		if (GEditor)
		{
			World = GEditor->GetEditorWorldContext().World();
		}
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No valid editor world for widget instantiation")));
			return;
		}

		// 3) Create widget instance
		UUserWidget* UserWidget = CreateWidget<UUserWidget>(World, WidgetClass);
		if (!UserWidget)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to instantiate UserWidget")));
			return;
		}
		UserWidget->AddToRoot();  // Prevent GC during rendering

#if WITH_EDITOR
		if (PreviewMode == TEXT("designer"))
		{
			// Explicit designer preview path for widgets with IsDesignTime()-gated
			// sample data. Runtime acceptance captures must not set these flags.
			UserWidget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
			UserWidget->SynchronizeProperties();
		}
#endif

		// Runtime CommonUI construction can bind default input actions before any
		// LocalPlayer/CommonInputSubsystem exists in this offscreen editor context.
		ICommonInputModule::GetSettings().LoadData();

		// 4) Take Slate widget — triggers outer widget's Initialize + PreConstruct.
		TSharedRef<SWidget> SlateWidget = UserWidget->TakeWidget();

		// CommonUI screens often synchronize state during activation rather than
		// designer PreConstruct. Offscreen captures need that lifecycle too, or
		// button text, selected tabs, and settings rows can render with defaults.
		if (UCommonActivatableWidget* ActivatableWidget = Cast<UCommonActivatableWidget>(UserWidget))
		{
			ActivatableWidget->ActivateWidget();
		}

		int32 PreviewFunctionCallCount = 0;
		for (const FPreviewFunctionCall& Call : PreviewFunctionCalls)
		{
			UObject* Target = UserWidget;
			if (!Call.WidgetName.IsEmpty())
			{
				Target = UserWidget->WidgetTree ? UserWidget->WidgetTree->FindWidget(FName(*Call.WidgetName)) : nullptr;
			}
			if (!Target)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("preview_function_calls target widget not found: %s"), *Call.WidgetName)));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			UFunction* Function = Target->FindFunction(FName(*Call.FunctionName));
			if (!Function)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("preview_function_calls function not found: %s on %s"), *Call.FunctionName, *Target->GetName())));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			TArray<uint8> ParamBuffer;
			void* ParamData = nullptr;
			if (Function->ParmsSize > 0)
			{
				ParamBuffer.SetNumZeroed(Function->ParmsSize);
				ParamData = ParamBuffer.GetData();
				Function->InitializeStruct(ParamData);
			}

			bool bParamImportSucceeded = true;
			FString ParamError;
			for (TFieldIterator<FProperty> It(Function); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Parm) || Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					continue;
				}

				const FString* ArgValue = Call.Args.Find(Prop->GetName());
				if (!ArgValue)
				{
					continue;
				}

				if (!ParamData)
				{
					bParamImportSucceeded = false;
					ParamError = FString::Printf(TEXT("Function %s has no parameter storage for preview arg %s"), *Call.FunctionName, *Prop->GetName());
					break;
				}

				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(ParamData);
				if (!Prop->ImportText_Direct(**ArgValue, PropAddr, Target, PPF_None))
				{
					bParamImportSucceeded = false;
					ParamError = FString::Printf(TEXT("Failed to import preview function arg %s=%s for %s"), *Prop->GetName(), **ArgValue, *Call.FunctionName);
					break;
				}
			}

			if (!bParamImportSucceeded)
			{
				if (ParamData)
				{
					Function->DestroyStruct(ParamData);
				}
				Promise->SetValue(CreateErrorResponse(ParamError));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			Target->ProcessEvent(Function, ParamData);
			if (ParamData)
			{
				Function->DestroyStruct(ParamData);
			}
			++PreviewFunctionCallCount;
		}

		// 4a) Force-initialize all nested UUserWidget components, then preload textures.
		//     Nested UUserWidgets in the WidgetTree are NOT auto-initialized by the outer
		//     widget's TakeWidget() — they init lazily when first painted. That means their
		//     BP PreConstruct (which calls SetBrushFromTexture(NavIcon) etc.) has not yet run,
		//     so Brush.ResourceObject is null when we try to force-stream textures.
		//     Fix: explicitly Initialize() each nested UserWidget first, then walk again to
		//     collect the now-populated brush textures and force their mip residency.
		FlushAsyncLoading();
		int32 InitCount = 0;
		int32 TexCount = 0;
		int32 SyncCount = 0;
		int32 StreamingWaitSkippedCount = 0;
		{
			auto ForceTextureResidentForCapture = [&TexCount, &StreamingWaitSkippedCount](UTexture2D* Tex)
			{
				if (!Tex)
				{
					return;
				}

				Tex->SetForceMipLevelsToBeResident(30.0f, true);
				if (!IsAssetStreamingSuspended())
				{
					Tex->WaitForStreaming();
				}
				else
				{
					++StreamingWaitSkippedCount;
				}
				++TexCount;
			};

			// Pass 1: force Initialize() on all nested UUserWidget instances (recursive)
			TFunction<void(UWidgetTree*)> InitTree;
			InitTree = [&InitTree, &InitCount, &PreviewMode](UWidgetTree* Tree)
			{
				if (!Tree) return;
				Tree->ForEachWidget([&InitTree, &InitCount, &PreviewMode](UWidget* W)
				{
					if (UUserWidget* NestedUW = Cast<UUserWidget>(W))
					{
#if WITH_EDITOR
						if (PreviewMode == TEXT("designer"))
						{
							NestedUW->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
						}
#endif
						if (!NestedUW->IsConstructed())
						{
							NestedUW->Initialize();  // runs BP PreConstruct on this nested instance
							++InitCount;
						}
						NestedUW->TakeWidget();
						InitTree(NestedUW->WidgetTree);
					}
				});
			};
			InitTree(UserWidget->WidgetTree);

			// Pass 2: collect + stream all brush textures (recursive), then SynchronizeProperties
			//         to push the updated brush into the already-built SImage slate widget.
			//         PreConstruct's SetBrushFromTexture() only pushes to Slate if MyImage.IsValid()
			//         at call time. When PreConstruct runs during nested Initialize(), MyImage is
			//         usually null (slate widget not yet built), so the brush sits in the UImage
			//         struct but is never copied to the SImage. A manual SynchronizeProperties()
			//         after the slate tree is built (i.e. after TakeWidget) does exactly that copy.
			TFunction<void(UWidgetTree*)> PreloadTree;
			PreloadTree = [&PreloadTree, &ForceTextureResidentForCapture, &SyncCount](UWidgetTree* Tree)
			{
				if (!Tree) return;
				Tree->ForEachWidget([&PreloadTree, &ForceTextureResidentForCapture, &SyncCount](UWidget* W)
				{
					W->SynchronizeProperties();
					++SyncCount;

					if (UImage* Img = Cast<UImage>(W))
					{
						if (UTexture2D* Tex = Cast<UTexture2D>(Img->Brush.GetResourceObject()))
						{
							ForceTextureResidentForCapture(Tex);
						}
					}
					else if (UBorder* Brd = Cast<UBorder>(W))
					{
						// Same bridging issue as UImage: SBorder built before CDO Background
						// was pushed via reflection ImportText. Force a resync so
						// set_widget_property changes land on the Slate side.
						if (UTexture2D* Tex = Cast<UTexture2D>(Brd->Background.GetResourceObject()))
						{
							ForceTextureResidentForCapture(Tex);
						}
					}
					else if (UUserWidget* NestedUW = Cast<UUserWidget>(W))
					{
						NestedUW->SynchronizeProperties();
						PreloadTree(NestedUW->WidgetTree);
					}
				});
			};
			PreloadTree(UserWidget->WidgetTree);
		}
		// Final global streaming flush — catches anything ForceMipLevelsToBeResident missed.
		if (!IsAssetStreamingSuspended())
		{
			IStreamingManager::Get().StreamAllResources(0.0f);
		}
		else
		{
			++StreamingWaitSkippedCount;
		}
		UE_LOG(LogAIExport, Log, TEXT("CaptureWidgetPreview[%s]: applied %d preview function calls, initialized %d nested widgets, synchronized %d widgets, streamed %d textures, skipped %d streaming waits"), *PreviewMode, PreviewFunctionCallCount, InitCount, SyncCount, TexCount, StreamingWaitSkippedCount);

		// 5) Get ImageWrapper module
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

		// 6) Derive base filename
		FString AssetBaseName = FPaths::GetBaseFilename(AssetPath);

		// 7) Widget renderer (gamma correction on for sRGB output)
		TSharedPtr<FWidgetRenderer> Renderer = MakeShared<FWidgetRenderer>(/*bUseGammaCorrection=*/true);

		TArray<TSharedPtr<FJsonValue>> PngResults;
		bool bAllSucceeded = true;
		FString LastError;

		for (const FRatioEntry& Ratio : Ratios)
		{
			const int32 RW = Ratio.Width;
			const int32 RH = Ratio.Height;

			// Create render target — RTF_RGBA8 (raw UNORM) + TargetGamma=0 (use DisplayGamma default).
			// FWidgetRenderer(bUseGammaCorrection=true) already applies linear›sRGB in shader
			// using RT->GetDisplayGamma(). Setting TargetGamma=2.2 + sRGB format causes double
			// gamma (values look washed out). Setting TargetGamma=0 lets it fall through to
			// Engine->DisplayGamma (2.2) applied exactly once. Matches editor viewport.
			UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
			RT->ClearColor = bTransparentBG ? FLinearColor::Transparent : FLinearColor::Black;
			RT->TargetGamma = 0.0f;
			RT->RenderTargetFormat = RTF_RGBA8;
			RT->InitAutoFormat(RW, RH);
			RT->UpdateResourceImmediate(true);
			RT->AddToRoot();

			// Warmup + final render passes (absorb texture streaming delay)
			const FVector2D DrawSize(RW, RH);
			for (int32 i = 0; i < WarmupFrames; ++i)
			{
				Renderer->DrawWidget(RT, SlateWidget, DrawSize, 0.016f, false);
			}

			// Flush GPU work before reading pixels
			FlushRenderingCommands();

			// Read pixels
			TArray<FColor> Bitmap;
			FRenderTarget* RenderTargetResource = RT->GameThread_GetRenderTargetResource();
			if (!RenderTargetResource || !RenderTargetResource->ReadPixels(Bitmap))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to read pixels for %dx%d"), RW, RH);
				RT->RemoveFromRoot();
				continue;
			}

			// Encode PNG
			TSharedPtr<IImageWrapper> PNGWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (!PNGWrapper.IsValid() ||
				!PNGWrapper->SetRaw(Bitmap.GetData(),
									Bitmap.Num() * sizeof(FColor),
									RW, RH,
									ERGBFormat::BGRA, 8))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to encode PNG for %dx%d"), RW, RH);
				RT->RemoveFromRoot();
				continue;
			}

			const TArray64<uint8>& CompressedPng = PNGWrapper->GetCompressed(100);

			// Flatten into TArray<uint8> for FFileHelper + FBase64 compatibility
			TArray<uint8> FlatPng;
			FlatPng.SetNumUninitialized((int32)CompressedPng.Num());
			FMemory::Memcpy(FlatPng.GetData(), CompressedPng.GetData(), CompressedPng.Num());

			// Derive output file path
			FString OutPath;
			if (Ratios.Num() == 1 && !OutputPath.IsEmpty())
			{
				OutPath = OutputPath;
			}
			else
			{
				FString Suffix = Ratio.Label.IsEmpty()
					? FString::Printf(TEXT("_%dx%d"), RW, RH)
					: FString::Printf(TEXT("_%s"), *Ratio.Label);
				Suffix.ReplaceInline(TEXT(":"), TEXT(""));
				Suffix.ReplaceInline(TEXT("/"), TEXT("_"));
				Suffix.ReplaceInline(TEXT("\\"), TEXT("_"));
				OutPath = OutputDir / (AssetBaseName + Suffix + TEXT(".png"));
			}

			// Save to disk
			if (!FFileHelper::SaveArrayToFile(FlatPng, *OutPath))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to write PNG: %s"), *OutPath);
				RT->RemoveFromRoot();
				continue;
			}

			// Build JSON entry
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("png_path"), OutPath);
			Entry->SetNumberField(TEXT("width"), RW);
			Entry->SetNumberField(TEXT("height"), RH);
			Entry->SetNumberField(TEXT("size_bytes"), FlatPng.Num());
			Entry->SetStringField(TEXT("preview_mode"), PreviewMode);
			if (!Ratio.Label.IsEmpty())
			{
				Entry->SetStringField(TEXT("label"), Ratio.Label);
			}
			if (bReturnBase64)
			{
				FString B64 = FBase64::Encode(FlatPng);
				Entry->SetStringField(TEXT("png_base64"), B64);
			}
			PngResults.Add(MakeShared<FJsonValueObject>(Entry));

			// Cleanup RT
			RT->RemoveFromRoot();
		}

		// Cleanup widget
		if (UCommonActivatableWidget* ActivatableWidget = Cast<UCommonActivatableWidget>(UserWidget))
		{
			if (ActivatableWidget->IsActivated())
			{
				ActivatableWidget->DeactivateWidget();
			}
		}
		UserWidget->ReleaseSlateResources(true);
		UserWidget->RemoveFromRoot();

		if (PngResults.Num() == 0)
		{
			Promise->SetValue(CreateErrorResponse(LastError.IsEmpty() ? TEXT("No previews produced") : LastError));
			return;
		}

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("preview_mode"), PreviewMode);
		Data->SetNumberField(TEXT("preview_function_calls_applied"), PreviewFunctionCallCount);
		Data->SetArrayField(TEXT("pngs"), PngResults);
		Data->SetNumberField(TEXT("count"), PngResults.Num());
		if (!bAllSucceeded)
		{
			Data->SetStringField(TEXT("partial_error"), LastError);
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Widget preview capture timed out"));
	return Future.Get();
}


}
