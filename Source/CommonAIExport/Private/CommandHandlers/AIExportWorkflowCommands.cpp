// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportWorkflowCommands.h"
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


namespace
{
FString QuoteProcessArgument(const FString& Argument)
{
	FString Escaped = Argument;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

bool ResolveProjectScopedDirectory(const FString& RequestedPath, FString& OutDirectory, FString& OutError)
{
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);

	FString Candidate = RequestedPath;
	Candidate.TrimStartAndEndInline();
	if (Candidate.IsEmpty())
	{
		Candidate = ProjectDir;
	}
	else if (FPaths::IsRelative(Candidate))
	{
		Candidate = FPaths::Combine(ProjectDir, Candidate);
	}

	Candidate = FPaths::ConvertRelativePathToFull(Candidate);
	FPaths::NormalizeDirectoryName(Candidate);

	if (!Candidate.Equals(ProjectDir, ESearchCase::IgnoreCase) && !FPaths::IsUnderDirectory(Candidate, ProjectDir))
	{
		OutError = TEXT("repo_path must resolve under the project directory");
		return false;
	}
	if (!IFileManager::Get().DirectoryExists(*Candidate))
	{
		OutError = FString::Printf(TEXT("repo_path does not exist or is not a directory: %s"), *Candidate);
		return false;
	}

	OutDirectory = Candidate;
	return true;
}

struct FSourceControlCommandContext
{
	FString Provider = TEXT("auto");
	FString Executable;
	FString RepoDir;
	bool bHasDiversion = false;
	bool bHasGit = false;
};

bool ResolveSourceControlCommandContext(TSharedPtr<FJsonObject> Params, FSourceControlCommandContext& OutContext, FString& OutError)
{
	FString Provider = TEXT("auto");
	FString RepoPath;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("provider"), Provider);
		Params->TryGetStringField(TEXT("repo_path"), RepoPath);
	}

	Provider.TrimStartAndEndInline();
	Provider = Provider.IsEmpty() ? TEXT("auto") : Provider.ToLower();

	if (!ResolveProjectScopedDirectory(RepoPath, OutContext.RepoDir, OutError))
	{
		return false;
	}

	OutContext.bHasDiversion = IFileManager::Get().DirectoryExists(*FPaths::Combine(OutContext.RepoDir, TEXT(".diversion")));
	OutContext.bHasGit = IFileManager::Get().DirectoryExists(*FPaths::Combine(OutContext.RepoDir, TEXT(".git")));

	if ((Provider == TEXT("auto") && OutContext.bHasDiversion) || Provider == TEXT("dv") || Provider == TEXT("diversion"))
	{
		OutContext.Provider = TEXT("diversion");
		OutContext.Executable = TEXT("dv");
	}
	else if ((Provider == TEXT("auto") && OutContext.bHasGit) || Provider == TEXT("git"))
	{
		OutContext.Provider = TEXT("git");
		OutContext.Executable = TEXT("git");
	}
	else if (Provider == TEXT("auto"))
	{
		OutContext.Provider = TEXT("auto");
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported source-control provider: %s"), *Provider);
		return false;
	}

	return true;
}

void AddSourceControlContextJson(TSharedPtr<FJsonObject> Data, const FSourceControlCommandContext& Context)
{
	Data->SetStringField(TEXT("provider"), Context.Provider);
	Data->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Data->SetStringField(TEXT("repo_dir"), Context.RepoDir);
	Data->SetBoolField(TEXT("diversion_repo"), Context.bHasDiversion);
	Data->SetBoolField(TEXT("git_repo"), Context.bHasGit);
}

void AddSourceControlProcessResult(TSharedPtr<FJsonObject> Data, const FSourceControlCommandContext& Context, const FString& Arguments)
{
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	const bool bLaunched = FPlatformProcess::ExecProcess(*Context.Executable, *Arguments, &ReturnCode, &StdOut, &StdErr, *Context.RepoDir);
	Data->SetBoolField(TEXT("available"), bLaunched);
	Data->SetStringField(TEXT("executable"), Context.Executable);
	Data->SetStringField(TEXT("arguments"), Arguments);
	Data->SetNumberField(TEXT("return_code"), ReturnCode);
	Data->SetStringField(TEXT("stdout"), StdOut);
	Data->SetStringField(TEXT("stderr"), StdErr);
	Data->SetStringField(TEXT("status"), (bLaunched && ReturnCode == 0) ? TEXT("ok") : TEXT("failed"));
}

int32 ReadClampedIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
{
	if (!Params.IsValid())
	{
		return DefaultValue;
	}

	double NumberValue = 0.0;
	if (!Params->TryGetNumberField(FieldName, NumberValue))
	{
		return DefaultValue;
	}
	return FMath::Clamp(static_cast<int32>(NumberValue), MinValue, MaxValue);
}
}

namespace CommonAIExport::CommandHandlers::Workflow
{
FString HandleSourceControlStatus(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Path;
	bool bNoLimit = false;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetBoolField(TEXT("no_limit"), bNoLimit);
	}
	Path.TrimStartAndEndInline();

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		Arguments = TEXT("status");
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Path);
		}
		if (bNoLimit)
		{
			Arguments += TEXT(" --no-limit");
		}
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = TEXT("status --short");
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" -- ") + QuoteProcessArgument(Path);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("path"), Path);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}



FString HandleSourceControlLog(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Path;
	FString Since;
	FString Until;
	FString Ref;
	bool bOneline = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetStringField(TEXT("since"), Since);
		Params->TryGetStringField(TEXT("until"), Until);
		Params->TryGetStringField(TEXT("ref"), Ref);
		Params->TryGetBoolField(TEXT("oneline"), bOneline);
	}
	Path.TrimStartAndEndInline();
	Since.TrimStartAndEndInline();
	Until.TrimStartAndEndInline();
	Ref.TrimStartAndEndInline();
	const int32 Limit = ReadClampedIntField(Params, TEXT("limit"), 20, 1, 200);

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		if (!Ref.IsEmpty())
		{
			return CreateErrorResponse(TEXT("source_control_log ref is only supported for git"));
		}
		Arguments = FString::Printf(TEXT("log %s -n %d --date iso"), *(Path.IsEmpty() ? QuoteProcessArgument(TEXT(".")) : QuoteProcessArgument(Path)), Limit);
		if (bOneline)
		{
			Arguments += TEXT(" --oneline");
		}
		if (!Since.IsEmpty())
		{
			Arguments += TEXT(" --since ") + QuoteProcessArgument(Since);
		}
		if (!Until.IsEmpty())
		{
			Arguments += TEXT(" --until ") + QuoteProcessArgument(Until);
		}
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = FString::Printf(TEXT("log --max-count=%d --date=iso --color=never"), Limit);
		if (bOneline)
		{
			Arguments += TEXT(" --oneline");
		}
		if (!Since.IsEmpty())
		{
			Arguments += TEXT(" --since=") + QuoteProcessArgument(Since);
		}
		if (!Until.IsEmpty())
		{
			Arguments += TEXT(" --until=") + QuoteProcessArgument(Until);
		}
		if (!Ref.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Ref);
		}
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" -- ") + QuoteProcessArgument(Path);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("path"), Path);
	Data->SetNumberField(TEXT("limit"), Limit);
	Data->SetBoolField(TEXT("oneline"), bOneline);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}



FString HandleSourceControlShow(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Ref;
	bool bNameStatus = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("ref"), Ref);
		Params->TryGetBoolField(TEXT("name_status"), bNameStatus);
	}
	Ref.TrimStartAndEndInline();

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		Arguments = TEXT("show");
		if (!Ref.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Ref);
		}
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status");
		}
		Arguments += TEXT(" --date iso --color never");
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = TEXT("show --date=iso --color=never");
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status --format=fuller");
		}
		if (!Ref.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Ref);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("ref"), Ref);
	Data->SetBoolField(TEXT("name_status"), bNameStatus);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}



FString HandleSourceControlDiff(TSharedPtr<FJsonObject> Params)
{
	FSourceControlCommandContext Context;
	FString Error;
	if (!ResolveSourceControlCommandContext(Params, Context, Error))
	{
		return CreateErrorResponse(Error);
	}

	FString Path;
	FString Base;
	FString Compare;
	bool bNameStatus = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("path"), Path);
		Params->TryGetStringField(TEXT("base"), Base);
		Params->TryGetStringField(TEXT("compare"), Compare);
		Params->TryGetBoolField(TEXT("name_status"), bNameStatus);
	}
	Path.TrimStartAndEndInline();
	Base.TrimStartAndEndInline();
	Compare.TrimStartAndEndInline();

	FString Arguments;
	if (Context.Provider == TEXT("diversion"))
	{
		Arguments = TEXT("diff");
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Path);
		}
		if (!Base.IsEmpty())
		{
			Arguments += TEXT(" --base ") + QuoteProcessArgument(Base);
		}
		if (!Compare.IsEmpty())
		{
			Arguments += TEXT(" --compare ") + QuoteProcessArgument(Compare);
		}
		Arguments += TEXT(" --color never");
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status");
		}
	}
	else if (Context.Provider == TEXT("git"))
	{
		Arguments = TEXT("diff --color=never");
		if (bNameStatus)
		{
			Arguments += TEXT(" --name-status");
		}
		if (!Base.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Base);
		}
		if (!Compare.IsEmpty())
		{
			Arguments += TEXT(" ") + QuoteProcessArgument(Compare);
		}
		if (!Path.IsEmpty())
		{
			Arguments += TEXT(" -- ") + QuoteProcessArgument(Path);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddSourceControlContextJson(Data, Context);
	Data->SetStringField(TEXT("path"), Path);
	Data->SetStringField(TEXT("base"), Base);
	Data->SetStringField(TEXT("compare"), Compare);
	Data->SetBoolField(TEXT("name_status"), bNameStatus);
	if (Context.Executable.IsEmpty())
	{
		Data->SetBoolField(TEXT("available"), false);
		Data->SetStringField(TEXT("status"), TEXT("not_configured"));
		return CreateSuccessResponse(Data);
	}

	AddSourceControlProcessResult(Data, Context, Arguments);
	return CreateSuccessResponse(Data);
}



FString HandleEditorLogRead(TSharedPtr<FJsonObject> Params)
{
	int32 MaxLines = 200;
	FString Filter;
	FString LogName;
	if (Params.IsValid())
	{
		double MaxLinesValue = 0.0;
		if (Params->TryGetNumberField(TEXT("max_lines"), MaxLinesValue) && MaxLinesValue > 0.0)
		{
			MaxLines = FMath::Clamp(static_cast<int32>(MaxLinesValue), 1, 5000);
		}
		Params->TryGetStringField(TEXT("filter"), Filter);
		Params->TryGetStringField(TEXT("log_name"), LogName);
	}

	if (LogName.IsEmpty())
	{
		LogName = FString::Printf(TEXT("%s.log"), FApp::GetProjectName());
	}

	const FString LogDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectLogDir());
	const FString LogFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(LogDir, FPaths::GetCleanFilename(LogName)));
	if (!FPaths::IsUnderDirectory(LogFilePath, LogDir))
	{
		return CreateErrorResponse(TEXT("log_name must resolve under the project log directory"));
	}

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *LogFilePath, FFileHelper::EHashOptions::None, FILEREAD_AllowWrite))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Could not read log file: %s"), *LogFilePath));
	}

	TArray<FString> Lines;
	Contents.ParseIntoArrayLines(Lines, false);

	TArray<TSharedPtr<FJsonValue>> OutLines;
	const int32 StartIndex = FMath::Max(0, Lines.Num() - MaxLines);
	int32 MatchedCount = 0;
	for (int32 Index = StartIndex; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		if (!Filter.IsEmpty() && !Line.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		++MatchedCount;
		OutLines.Add(MakeShared<FJsonValueString>(Line));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("log_path"), LogFilePath);
	Data->SetStringField(TEXT("filter"), Filter);
	Data->SetNumberField(TEXT("max_lines"), MaxLines);
	Data->SetNumberField(TEXT("total_lines"), Lines.Num());
	Data->SetNumberField(TEXT("returned_count"), OutLines.Num());
	Data->SetNumberField(TEXT("matched_count"), MatchedCount);
	Data->SetArrayField(TEXT("lines"), OutLines);
	return CreateSuccessResponse(Data);
}


}
