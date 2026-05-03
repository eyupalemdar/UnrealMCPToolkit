// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "Editor.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace CommonAIExport::RuntimeDiagnostics
{
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

double ReadClampedDoubleField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, double DefaultValue, double MinValue, double MaxValue)
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
	return FMath::Clamp(NumberValue, MinValue, MaxValue);
}

FString ReadLowerStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FString& DefaultValue)
{
	FString Value = DefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
	}
	Value.TrimStartAndEndInline();
	return Value.ToLower();
}

void AppendStringArrayField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, TArray<FString>& OutValues)
{
	if (!Params.IsValid())
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Params->TryGetArrayField(FieldName, Values) || !Values)
	{
		return;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Text;
		if (Value.IsValid() && Value->TryGetString(Text))
		{
			Text.TrimStartAndEndInline();
			if (!Text.IsEmpty())
			{
				OutValues.Add(Text);
			}
		}
	}
}

FString TruncateString(const FString& Value, int32 Limit)
{
	if (Limit <= 0 || Value.Len() <= Limit)
	{
		return Value;
	}
	return Value.Left(Limit) + TEXT("...");
}

FString WorldTypeToString(EWorldType::Type WorldType)
{
	switch (WorldType)
	{
	case EWorldType::None:
		return TEXT("None");
	case EWorldType::Game:
		return TEXT("Game");
	case EWorldType::Editor:
		return TEXT("Editor");
	case EWorldType::PIE:
		return TEXT("PIE");
	case EWorldType::EditorPreview:
		return TEXT("EditorPreview");
	case EWorldType::GamePreview:
		return TEXT("GamePreview");
	case EWorldType::GameRPC:
		return TEXT("GameRPC");
	case EWorldType::Inactive:
		return TEXT("Inactive");
	default:
		return TEXT("Unknown");
	}
}

FString NetModeToString(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone:
		return TEXT("Standalone");
	case NM_DedicatedServer:
		return TEXT("DedicatedServer");
	case NM_ListenServer:
		return TEXT("ListenServer");
	case NM_Client:
		return TEXT("Client");
	default:
		return TEXT("Unknown");
	}
}

UWorld* SelectWorld(const FString& RequestedWorld, FString& OutWorldSource)
{
	if (!GEditor)
	{
		OutWorldSource = TEXT("none");
		return nullptr;
	}

	const FString Selector = RequestedWorld.IsEmpty() ? TEXT("auto") : RequestedWorld.ToLower();
	if (Selector == TEXT("editor"))
	{
		OutWorldSource = TEXT("editor");
		return GEditor->GetEditorWorldContext().World();
	}
	if (Selector == TEXT("pie") || Selector == TEXT("runtime") || Selector == TEXT("play"))
	{
		OutWorldSource = TEXT("pie");
		return GEditor->PlayWorld;
	}

	if (GEditor->PlayWorld)
	{
		OutWorldSource = TEXT("pie");
		return GEditor->PlayWorld;
	}

	OutWorldSource = TEXT("editor");
	return GEditor->GetEditorWorldContext().World();
}

AActor* FindActor(UWorld* World, const FString& ActorPath, const FString& ActorLabel, const FString& ActorName)
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
		if (!ActorPath.IsEmpty() && Actor->GetPathName() == ActorPath)
		{
			return Actor;
		}
		if (!ActorLabel.IsEmpty() && Actor->GetActorLabel() == ActorLabel)
		{
			return Actor;
		}
		if (!ActorName.IsEmpty() && Actor->GetName() == ActorName)
		{
			return Actor;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> BuildPIEStateJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	Data->SetBoolField(TEXT("simulating"), GEditor && GEditor->bIsSimulatingInEditor);
	Data->SetStringField(TEXT("play_world"), (GEditor && GEditor->PlayWorld) ? GEditor->PlayWorld->GetName() : TEXT(""));
	return Data;
}

TSharedPtr<FJsonObject> BuildVectorJson(const FVector& Vector)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("x"), Vector.X);
	Data->SetNumberField(TEXT("y"), Vector.Y);
	Data->SetNumberField(TEXT("z"), Vector.Z);
	return Data;
}

TSharedPtr<FJsonObject> BuildRotatorJson(const FRotator& Rotator)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("pitch"), Rotator.Pitch);
	Data->SetNumberField(TEXT("yaw"), Rotator.Yaw);
	Data->SetNumberField(TEXT("roll"), Rotator.Roll);
	return Data;
}

TSharedPtr<FJsonObject> BuildVector2DJson(const FVector2D& Vector)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("x"), Vector.X);
	Data->SetNumberField(TEXT("y"), Vector.Y);
	return Data;
}

TSharedPtr<FJsonObject> BuildBoxJson(const FBox& Box)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), Box.IsValid != 0);
	Data->SetObjectField(TEXT("min"), BuildVectorJson(Box.Min));
	Data->SetObjectField(TEXT("max"), BuildVectorJson(Box.Max));
	Data->SetObjectField(TEXT("center"), BuildVectorJson(Box.GetCenter()));
	Data->SetObjectField(TEXT("extent"), BuildVectorJson(Box.GetExtent()));
	Data->SetObjectField(TEXT("size"), BuildVectorJson(Box.GetSize()));
	return Data;
}

TSharedPtr<FJsonObject> BuildObjectReferenceJson(const UObject* Object)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Object != nullptr);
	if (!Object)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Object->GetName());
	Data->SetStringField(TEXT("path"), Object->GetPathName());
	Data->SetStringField(TEXT("class"), Object->GetClass() ? Object->GetClass()->GetPathName() : TEXT(""));
	return Data;
}

TSharedPtr<FJsonObject> BuildActorJson(AActor* Actor)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Actor != nullptr);
	if (!Actor)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Actor->GetName());
	Data->SetStringField(TEXT("label"), Actor->GetActorLabel());
	Data->SetStringField(TEXT("path"), Actor->GetPathName());
	Data->SetStringField(TEXT("class"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT(""));
	Data->SetObjectField(TEXT("location"), BuildVectorJson(Actor->GetActorLocation()));
	Data->SetObjectField(TEXT("rotation"), BuildRotatorJson(Actor->GetActorRotation()));
	Data->SetObjectField(TEXT("scale"), BuildVectorJson(Actor->GetActorScale3D()));
	Data->SetBoolField(TEXT("hidden"), Actor->IsHidden());
	Data->SetBoolField(TEXT("pending_kill_pending"), Actor->IsActorBeingDestroyed());
	return Data;
}

TSharedPtr<FJsonObject> BuildComponentJson(UActorComponent* Component)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Component);
	if (!Component)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("registered"), Component->IsRegistered());
	Data->SetBoolField(TEXT("active"), Component->IsActive());
	if (AActor* Owner = Component->GetOwner())
	{
		Data->SetStringField(TEXT("owner_name"), Owner->GetName());
		Data->SetStringField(TEXT("owner_label"), Owner->GetActorLabel());
		Data->SetStringField(TEXT("owner_path"), Owner->GetPathName());
	}

	TArray<TSharedPtr<FJsonValue>> Tags;
	for (const FName& Tag : Component->ComponentTags)
	{
		Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	Data->SetArrayField(TEXT("tags"), Tags);
	return Data;
}

TSharedPtr<FJsonObject> BuildWorldJson(UWorld* World, const FString& WorldSource)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(World);
	Data->SetStringField(TEXT("source"), WorldSource);
	if (!World)
	{
		return Data;
	}

	Data->SetStringField(TEXT("world_type"), WorldTypeToString(World->WorldType));
	Data->SetStringField(TEXT("net_mode"), NetModeToString(World->GetNetMode()));
	Data->SetStringField(TEXT("map_name"), World->GetMapName());
	Data->SetNumberField(TEXT("time_seconds"), World->GetTimeSeconds());
	Data->SetNumberField(TEXT("real_time_seconds"), World->GetRealTimeSeconds());
	Data->SetNumberField(TEXT("delta_time_seconds"), World->GetDeltaSeconds());
	Data->SetBoolField(TEXT("has_begun_play"), World->HasBegunPlay());
	Data->SetBoolField(TEXT("is_game_world"), World->IsGameWorld());
	Data->SetStringField(TEXT("package_name"), World->GetOutermost() ? World->GetOutermost()->GetName() : TEXT(""));
	Data->SetObjectField(TEXT("persistent_level"), BuildObjectReferenceJson(World->PersistentLevel));
	if (AWorldSettings* Settings = World->GetWorldSettings(false))
	{
		Data->SetObjectField(TEXT("world_settings"), BuildObjectReferenceJson(Settings));
		Data->SetNumberField(TEXT("time_dilation"), Settings->GetEffectiveTimeDilation());
	}
	return Data;
}

void AddReflectedSettingsPropertyJson(TSharedPtr<FJsonObject> Target, const UObject* Settings, const TCHAR* PropertyName, const TCHAR* JsonName)
{
	if (!Target.IsValid() || !Settings)
	{
		return;
	}

	const FProperty* Property = Settings->GetClass() ? Settings->GetClass()->FindPropertyByName(PropertyName) : nullptr;
	if (!Property)
	{
		return;
	}

	FString Value;
	Property->ExportText_InContainer(0, Value, Settings, Settings, const_cast<UObject*>(Settings), PPF_None);
	Target->SetStringField(JsonName, Value);
}
}
