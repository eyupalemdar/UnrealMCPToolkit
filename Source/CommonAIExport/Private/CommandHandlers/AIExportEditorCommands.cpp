// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportEditorCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"
#include "CommonAIExportModule.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"
#include "PlayInEditorDataTypes.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonTypes.h"
#include "UnrealClient.h"

namespace CommonAIExport::CommandHandlers::Editor
{
namespace
{
UWorld* GetAIEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void AddVectorJson(TSharedPtr<FJsonObject> Obj, const TCHAR* FieldName, const FVector& Value)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("x"), Value.X);
	Json->SetNumberField(TEXT("y"), Value.Y);
	Json->SetNumberField(TEXT("z"), Value.Z);
	Obj->SetObjectField(FieldName, Json);
}

void AddRotatorJson(TSharedPtr<FJsonObject> Obj, const TCHAR* FieldName, const FRotator& Value)
{
	TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("pitch"), Value.Pitch);
	Json->SetNumberField(TEXT("yaw"), Value.Yaw);
	Json->SetNumberField(TEXT("roll"), Value.Roll);
	Obj->SetObjectField(FieldName, Json);
}

FVector ReadVectorField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FVector& DefaultValue)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(FieldName, Obj) || !Obj || !Obj->IsValid())
	{
		return DefaultValue;
	}

	FVector Result = DefaultValue;
	double Value = 0.0;
	if ((*Obj)->TryGetNumberField(TEXT("x"), Value)) Result.X = Value;
	if ((*Obj)->TryGetNumberField(TEXT("y"), Value)) Result.Y = Value;
	if ((*Obj)->TryGetNumberField(TEXT("z"), Value)) Result.Z = Value;
	return Result;
}

FRotator ReadRotatorField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FRotator& DefaultValue)
{
	const TSharedPtr<FJsonObject>* Obj = nullptr;
	if (!Params.IsValid() || !Params->TryGetObjectField(FieldName, Obj) || !Obj || !Obj->IsValid())
	{
		return DefaultValue;
	}

	FRotator Result = DefaultValue;
	double Value = 0.0;
	if ((*Obj)->TryGetNumberField(TEXT("pitch"), Value)) Result.Pitch = Value;
	if ((*Obj)->TryGetNumberField(TEXT("yaw"), Value)) Result.Yaw = Value;
	if ((*Obj)->TryGetNumberField(TEXT("roll"), Value)) Result.Roll = Value;
	return Result;
}

TSharedPtr<FJsonObject> BuildActorJson(AActor* Actor)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Actor)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Actor->GetName());
	Data->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Data->SetStringField(TEXT("path"), Actor->GetPathName());
	Data->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("hidden"), Actor->IsHidden());
	AddVectorJson(Data, TEXT("location"), Actor->GetActorLocation());
	AddRotatorJson(Data, TEXT("rotation"), Actor->GetActorRotation());
	AddVectorJson(Data, TEXT("scale"), Actor->GetActorScale3D());
	return Data;
}

AActor* FindActorForAI(UWorld* World, const FString& ActorPath, const FString& ActorLabel, const FString& ActorName)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (!ActorPath.IsEmpty() && Actor->GetPathName() == ActorPath) return Actor;
		if (!ActorLabel.IsEmpty() && Actor->GetActorLabel() == ActorLabel) return Actor;
		if (!ActorName.IsEmpty() && Actor->GetName() == ActorName) return Actor;
	}
	return nullptr;
}
}

FString HandleEditorWorldInfo()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		int32 ActorCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			++ActorCount;
		}

		TArray<TSharedPtr<FJsonValue>> Levels;
		for (ULevel* Level : World->GetLevels())
		{
			if (!Level)
			{
				continue;
			}
			TSharedPtr<FJsonObject> LevelJson = MakeShared<FJsonObject>();
			LevelJson->SetStringField(TEXT("name"), Level->GetName());
			LevelJson->SetStringField(TEXT("package_name"), Level->GetOutermost() ? Level->GetOutermost()->GetName() : TEXT(""));
			LevelJson->SetBoolField(TEXT("is_persistent"), Level == World->PersistentLevel);
			Levels.Add(MakeShared<FJsonValueObject>(LevelJson));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("world_name"), World->GetName());
		Data->SetStringField(TEXT("package_name"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
		Data->SetStringField(TEXT("map_filename"), FEditorFileUtils::GetFilename(World));
		Data->SetStringField(TEXT("world_type"), LexToString(World->WorldType));
		Data->SetNumberField(TEXT("actor_count"), ActorCount);
		Data->SetNumberField(TEXT("level_count"), Levels.Num());
		Data->SetArrayField(TEXT("levels"), Levels);
		Data->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Editor world info timed out"));
	return Future.Get();
}

FString HandleActorList(TSharedPtr<FJsonObject> Params)
{
	FString NameFilter;
	FString ClassFilter;
	int32 Limit = 500;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		double LimitValue = 0.0;
		if (Params->TryGetNumberField(TEXT("limit"), LimitValue) && LimitValue > 0.0)
		{
			Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, 5000);
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [NameFilter, ClassFilter, Limit, Promise]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		TArray<TSharedPtr<FJsonValue>> Actors;
		int32 MatchedCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			const FString Label = Actor->GetActorLabel();
			const FString Name = Actor->GetName();
			const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
			if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
			{
				continue;
			}
			if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter))
			{
				continue;
			}

			++MatchedCount;
			if (Actors.Num() < Limit)
			{
				Actors.Add(MakeShared<FJsonValueObject>(BuildActorJson(Actor)));
			}
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("actors"), Actors);
		Data->SetNumberField(TEXT("count"), Actors.Num());
		Data->SetNumberField(TEXT("matched_count"), MatchedCount);
		Data->SetBoolField(TEXT("truncated"), MatchedCount > Actors.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor list timed out"));
	return Future.Get();
}

FString HandleActorSpawn(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString ClassPath;
	if (!Params->TryGetStringField(TEXT("class_path"), ClassPath))
		return CreateErrorResponse(TEXT("Missing 'class_path' parameter"));

	FString ActorLabel;
	Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
	const FVector Location = ReadVectorField(Params, TEXT("location"), FVector::ZeroVector);
	const FRotator Rotation = ReadRotatorField(Params, TEXT("rotation"), FRotator::ZeroRotator);
	const FVector Scale = ReadVectorField(Params, TEXT("scale"), FVector::OneVector);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [ClassPath, ActorLabel, Location, Rotation, Scale, Promise]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		UClass* ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, *ClassPath);
		if (!ActorClass)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load actor class: %s"), *ClassPath)));
			return;
		}

		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ActorSpawn", "AI Spawn Actor"));
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(ActorClass, Location, Rotation, SpawnParams);
		if (!Actor)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to spawn actor class: %s"), *ClassPath)));
			return;
		}

		Actor->Modify();
		Actor->SetActorScale3D(Scale);
		if (!ActorLabel.IsEmpty())
		{
			Actor->SetActorLabel(ActorLabel);
		}
		World->MarkPackageDirty();

		Promise->SetValue(CreateSuccessResponse(BuildActorJson(Actor)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor spawn timed out"));
	return Future.Get();
}

FString HandleActorSetTransform(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString ActorPath, ActorLabel, ActorName;
	Params->TryGetStringField(TEXT("actor_path"), ActorPath);
	Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Params->TryGetStringField(TEXT("actor_name"), ActorName);
	if (ActorPath.IsEmpty() && ActorLabel.IsEmpty() && ActorName.IsEmpty())
		return CreateErrorResponse(TEXT("Expected one of: actor_path, actor_label, actor_name"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, ActorPath, ActorLabel, ActorName, Promise]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		AActor* Actor = FindActorForAI(World, ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Actor not found")));
			return;
		}

		const FVector Location = ReadVectorField(Params, TEXT("location"), Actor->GetActorLocation());
		const FRotator Rotation = ReadRotatorField(Params, TEXT("rotation"), Actor->GetActorRotation());
		const FVector Scale = ReadVectorField(Params, TEXT("scale"), Actor->GetActorScale3D());
		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ActorSetTransform", "AI Set Actor Transform"));
		Actor->Modify();
		Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		Actor->SetActorScale3D(Scale);
		Actor->MarkPackageDirty();
		Promise->SetValue(CreateSuccessResponse(BuildActorJson(Actor)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor set transform timed out"));
	return Future.Get();
}

FString HandleActorDelete(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString ActorPath, ActorLabel, ActorName;
	Params->TryGetStringField(TEXT("actor_path"), ActorPath);
	Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Params->TryGetStringField(TEXT("actor_name"), ActorName);
	if (ActorPath.IsEmpty() && ActorLabel.IsEmpty() && ActorName.IsEmpty())
		return CreateErrorResponse(TEXT("Expected one of: actor_path, actor_label, actor_name"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [ActorPath, ActorLabel, ActorName, Promise]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world is available")));
			return;
		}

		AActor* Actor = FindActorForAI(World, ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Actor not found")));
			return;
		}

		const FString DeletedPath = Actor->GetPathName();
		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ActorDelete", "AI Delete Actor"));
		const bool bDeleted = World->EditorDestroyActor(Actor, true);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("deleted"), bDeleted);
		Data->SetStringField(TEXT("actor_path"), DeletedPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Actor delete timed out"));
	return Future.Get();
}

FString HandleLevelOpen(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString MapPath;
	if (!Params->TryGetStringField(TEXT("map_path"), MapPath))
		return CreateErrorResponse(TEXT("Missing 'map_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [MapPath, Promise]()
	{
		FString Filename = MapPath;
		if (MapPath.StartsWith(TEXT("/Game/")) || MapPath.StartsWith(TEXT("/Engine/")))
		{
			Filename = FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
		}

		const bool bLoaded = FEditorFileUtils::LoadMap(Filename, false, true);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("loaded"), bLoaded);
		Data->SetStringField(TEXT("map_path"), MapPath);
		Data->SetStringField(TEXT("filename"), Filename);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Level open timed out"));
	return Future.Get();
}

FString HandleLevelSaveCurrent()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise]()
	{
		UWorld* World = GetAIEditorWorld();
		if (!World || !World->PersistentLevel)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No editor world/persistent level is available")));
			return;
		}
		FString SavedFilename;
		const bool bSaved = FEditorFileUtils::SaveLevel(World->PersistentLevel, TEXT(""), &SavedFilename);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), bSaved);
		Data->SetStringField(TEXT("package_name"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
		Data->SetStringField(TEXT("filename"), SavedFilename);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Level save current timed out"));
	return Future.Get();
}

FString HandlePIEStatus()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	Data->SetBoolField(TEXT("simulating"), GEditor && GEditor->bIsSimulatingInEditor);
	Data->SetStringField(TEXT("play_world"), (GEditor && GEditor->PlayWorld) ? GEditor->PlayWorld->GetName() : TEXT(""));
	return CreateSuccessResponse(Data);
}

FString HandlePIEStart()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Promise]()
	{
		if (!GEditor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("GEditor is not available")));
			return;
		}
		if (GEditor->PlayWorld)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("PIE is already active")));
			return;
		}

		FRequestPlaySessionParams Params;
		GEditor->RequestPlaySession(Params);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("requested"), true);
		Promise->SetValue(CreateSuccessResponse(Data));
	});
	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("PIE start timed out"));
	return Future.Get();
}

FString HandlePIEStop()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Promise]()
	{
		if (!GEditor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("GEditor is not available")));
			return;
		}
		if (!GEditor->PlayWorld)
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("requested"), false);
			Data->SetBoolField(TEXT("pie_active"), false);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}
		GEditor->RequestEndPlayMap();
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("requested"), true);
		Promise->SetValue(CreateSuccessResponse(Data));
	});
	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("PIE stop timed out"));
	return Future.Get();
}

FString HandleEditorConsoleCommand(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString Command;
	if (!Params->TryGetStringField(TEXT("command"), Command) || Command.TrimStartAndEnd().IsEmpty())
		return CreateErrorResponse(TEXT("Missing 'command' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [Command, Promise]()
	{
		if (!GEditor)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("GEditor is not available")));
			return;
		}

		FStringOutputDevice Output;
		UWorld* World = GetAIEditorWorld();
		const bool bHandled = GEditor->Exec(World, *Command, Output);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("command"), Command);
		Data->SetBoolField(TEXT("handled"), bHandled);
		Data->SetStringField(TEXT("output"), static_cast<const FString&>(Output));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Editor console command timed out"));
	return Future.Get();
}

FString HandleViewportCapture(TSharedPtr<FJsonObject> Params)
{
	FString OutputPath;
	bool bShowUI = true;
	bool bAddFilenameSuffix = false;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("output_path"), OutputPath);
		Params->TryGetBoolField(TEXT("show_ui"), bShowUI);
		Params->TryGetBoolField(TEXT("add_filename_suffix"), bAddFilenameSuffix);
	}

	if (OutputPath.IsEmpty())
	{
		const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
		OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("AIViewport"), FString::Printf(TEXT("Viewport_%s.png"), *Timestamp));
	}
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();
	AsyncTask(ENamedThreads::GameThread, [OutputPath, bShowUI, bAddFilenameSuffix, Promise]()
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
		FScreenshotRequest::RequestScreenshot(OutputPath, bShowUI, bAddFilenameSuffix);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("screenshot_requested"), true);
		Data->SetStringField(TEXT("output_path"), OutputPath);
		Data->SetBoolField(TEXT("show_ui"), bShowUI);
		Data->SetBoolField(TEXT("add_filename_suffix"), bAddFilenameSuffix);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Viewport capture request timed out"));
	return Future.Get();
}
}
