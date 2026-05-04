// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTWorldExporter.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/GameModeBase.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Brush.h"
#include "Engine/Light.h"
#include "Kismet/GameplayStatics.h"
#include "Algo/Sort.h"

bool UMCTWorldExporter::CanExport(UObject* Asset) const
{
	return Asset && Asset->IsA<UWorld>();
}

TArray<UClass*> UMCTWorldExporter::GetSupportedClasses() const
{
	return { UWorld::StaticClass() };
}

FString UMCTWorldExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	UWorld* World = Cast<UWorld>(Asset);
	if (!World)
	{
		return TEXT("Error: Not a World asset\n");
	}

	FString Output;

	// Metadata
	Output += ExportWorldMetadata(World);

	// World Settings
	Output += ExportWorldSettings(World, bFilterDefaults);

	// Streaming levels
	Output += ExportStreamingLevels(World);

	// Actors
	Output += ExportActors(World, bFilterDefaults);

	return Output;
}

FString UMCTWorldExporter::ExportWorldMetadata(UWorld* World)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("WORLD: %s"), *World->GetName()));

	// World type
	FString WorldType;
	switch (World->WorldType)
	{
		case EWorldType::None: WorldType = TEXT("None"); break;
		case EWorldType::Game: WorldType = TEXT("Game"); break;
		case EWorldType::Editor: WorldType = TEXT("Editor"); break;
		case EWorldType::PIE: WorldType = TEXT("PIE"); break;
		case EWorldType::EditorPreview: WorldType = TEXT("EditorPreview"); break;
		case EWorldType::GamePreview: WorldType = TEXT("GamePreview"); break;
		case EWorldType::GameRPC: WorldType = TEXT("GameRPC"); break;
		case EWorldType::Inactive: WorldType = TEXT("Inactive"); break;
		default: WorldType = TEXT("Unknown"); break;
	}
	Output += FString::Printf(TEXT("WorldType: %s\n"), *WorldType);

	// Path
	Output += FString::Printf(TEXT("Path: %s\n"), *World->GetPathName());

	// Persistent level
	if (ULevel* PersistentLevel = World->PersistentLevel)
	{
		Output += FString::Printf(TEXT("PersistentLevel: %s\n"), *PersistentLevel->GetName());
	}

	// Level count
	Output += FString::Printf(TEXT("LevelCount: %d\n"), World->GetNumLevels());

	// Actor count
	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!ShouldSkipActor(*It))
		{
			ActorCount++;
		}
	}
	Output += FString::Printf(TEXT("ActorCount: %d\n"), ActorCount);

	Output += TEXT("\n");
	return Output;
}

FString UMCTWorldExporter::ExportWorldSettings(UWorld* World, bool bFilterDefaults)
{
	AWorldSettings* Settings = World->GetWorldSettings();
	if (!Settings)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("WORLD SETTINGS"));

	// Key world settings (always include these)
	Output += FString::Printf(TEXT("bEnableWorldBoundsChecks: %s\n"),
		Settings->bEnableWorldBoundsChecks ? TEXT("True") : TEXT("False"));

	Output += FString::Printf(TEXT("bEnableWorldComposition: %s\n"),
		Settings->bEnableWorldComposition ? TEXT("True") : TEXT("False"));

	// Default Game Mode
	if (Settings->DefaultGameMode)
	{
		Output += FString::Printf(TEXT("DefaultGameMode: %s\n"),
			*Settings->DefaultGameMode->GetPathName());
	}

	// Kill Z
	Output += FString::Printf(TEXT("KillZ: %.2f\n"), Settings->KillZ);

	// World Gravity
	Output += FString::Printf(TEXT("WorldGravityZ: %.2f\n"), Settings->WorldGravityZ);

	// Global Gravity Scale
	Output += FString::Printf(TEXT("GlobalGravityZ: %.2f\n"), Settings->GlobalGravityZ);

	// Export non-default properties (critical for AI understanding)
	// In simplified mode: only non-default values
	// In raw mode: all properties
	Output += TEXT("\n");
	if (bFilterDefaults)
	{
		// Simplified: Export only non-default properties (these are the important customizations)
		FString NonDefaultProps = ExportObjectProperties(Settings, 0, true);
		if (!NonDefaultProps.IsEmpty())
		{
			Output += MakeSubsectionHeader(TEXT("Custom Properties"));
			Output += NonDefaultProps;
		}
	}
	else
	{
		// Raw: Export all properties
		Output += MakeSubsectionHeader(TEXT("All Properties"));
		Output += ExportObjectProperties(Settings, 0, false);
	}

	Output += TEXT("\n");
	return Output;
}

FString UMCTWorldExporter::ExportStreamingLevels(UWorld* World)
{
	const TArray<ULevelStreaming*>& StreamingLevels = World->GetStreamingLevels();

	if (StreamingLevels.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("STREAMING LEVELS"));
	Output += FString::Printf(TEXT("Count: %d\n\n"), StreamingLevels.Num());

	for (int32 i = 0; i < StreamingLevels.Num(); ++i)
	{
		ULevelStreaming* StreamingLevel = StreamingLevels[i];
		if (!StreamingLevel)
		{
			continue;
		}

		Output += FString::Printf(TEXT("[%d] %s\n"), i, *StreamingLevel->GetWorldAssetPackageName());

		// Streaming settings
		Output += FString::Printf(TEXT("  bShouldBeLoaded: %s\n"),
			StreamingLevel->ShouldBeLoaded() ? TEXT("True") : TEXT("False"));

		Output += FString::Printf(TEXT("  bShouldBeVisible: %s\n"),
			StreamingLevel->ShouldBeVisible() ? TEXT("True") : TEXT("False"));

		Output += FString::Printf(TEXT("  bIsStatic: %s\n"),
			StreamingLevel->bIsStatic ? TEXT("True") : TEXT("False"));

		// Level Transform
		FTransform LevelTransform = StreamingLevel->LevelTransform;
		if (!LevelTransform.Equals(FTransform::Identity))
		{
			Output += FString::Printf(TEXT("  Transform: %s\n"), *LevelTransform.ToString());
		}

		Output += TEXT("\n");
	}

	return Output;
}

FString UMCTWorldExporter::ExportActors(UWorld* World, bool bFilterDefaults)
{
	FString Output;
	Output += MakeSectionHeader(TEXT("ACTORS"));

	// Group actors by class
	TMap<UClass*, TArray<AActor*>> ActorsByClass;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!ShouldSkipActor(Actor))
		{
			ActorsByClass.FindOrAdd(Actor->GetClass()).Add(Actor);
		}
	}

	// Sort classes by name for consistent output
	TArray<UClass*> SortedClasses;
	ActorsByClass.GetKeys(SortedClasses);
	Algo::Sort(SortedClasses, [](UClass* A, UClass* B)
	{
		return A->GetName() < B->GetName();
	});

	// Export each class group
	for (UClass* Class : SortedClasses)
	{
		const TArray<AActor*>& Actors = ActorsByClass[Class];

		Output += FString::Printf(TEXT("\n--- %s (%d) ---\n"),
			*Class->GetName(), Actors.Num());

		// For simplified export, limit actors per class
		int32 MaxActors = bFilterDefaults ? 10 : 50;
		int32 ExportedCount = 0;

		for (AActor* Actor : Actors)
		{
			if (ExportedCount >= MaxActors)
			{
				Output += FString::Printf(TEXT("  ... and %d more\n"), Actors.Num() - MaxActors);
				break;
			}

			Output += ExportActor(Actor, 1, bFilterDefaults);
			ExportedCount++;
		}
	}

	Output += TEXT("\n");
	return Output;
}

FString UMCTWorldExporter::ExportActor(AActor* Actor, int32 IndentLevel, bool bFilterDefaults)
{
	if (!Actor)
	{
		return TEXT("");
	}

	FString Output;
	FString Indent = GetIndent(IndentLevel);

	// Actor header
	Output += FString::Printf(TEXT("%s[%s]\n"), *Indent, *Actor->GetName());

	// Location/Rotation/Scale
	FVector Location = Actor->GetActorLocation();
	FRotator Rotation = Actor->GetActorRotation();
	FVector Scale = Actor->GetActorScale3D();

	if (!Location.IsNearlyZero())
	{
		Output += FString::Printf(TEXT("%s  Location: (%.1f, %.1f, %.1f)\n"),
			*Indent, Location.X, Location.Y, Location.Z);
	}

	if (!Rotation.IsNearlyZero())
	{
		Output += FString::Printf(TEXT("%s  Rotation: (%.1f, %.1f, %.1f)\n"),
			*Indent, Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
	}

	if (!Scale.Equals(FVector::OneVector))
	{
		Output += FString::Printf(TEXT("%s  Scale: (%.2f, %.2f, %.2f)\n"),
			*Indent, Scale.X, Scale.Y, Scale.Z);
	}

	// Tags
	if (Actor->Tags.Num() > 0)
	{
		FString TagsStr;
		for (const FName& Tag : Actor->Tags)
		{
			if (!TagsStr.IsEmpty()) TagsStr += TEXT(", ");
			TagsStr += Tag.ToString();
		}
		Output += FString::Printf(TEXT("%s  Tags: [%s]\n"), *Indent, *TagsStr);
	}

	// Components (always include for context)
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	if (Components.Num() > 0 && !bFilterDefaults)
	{
		Output += FString::Printf(TEXT("%s  Components: %d\n"), *Indent, Components.Num());
		for (UActorComponent* Comp : Components)
		{
			if (Comp)
			{
				Output += FString::Printf(TEXT("%s    - %s (%s)\n"),
					*Indent, *Comp->GetName(), *Comp->GetClass()->GetName());
			}
		}
	}

	// Actor properties
	// Simplified: only non-default values (the important customizations)
	// Raw: all properties
	FString PropsStr = ExportObjectProperties(Actor, IndentLevel + 2, bFilterDefaults);
	if (!PropsStr.IsEmpty())
	{
		if (bFilterDefaults)
		{
			// For simplified, only show if there are non-default props
			Output += FString::Printf(TEXT("%s  Custom:\n"), *Indent);
		}
		else
		{
			Output += FString::Printf(TEXT("%s  Properties:\n"), *Indent);
		}
		Output += PropsStr;
	}

	return Output;
}

bool UMCTWorldExporter::ShouldSkipActor(AActor* Actor) const
{
	if (!Actor)
	{
		return true;
	}

	// Skip pending kill
	if (!IsValid(Actor))
	{
		return true;
	}

	// Skip hidden/transient actors
	if (Actor->IsHidden())
	{
		return true;
	}

	// Skip world settings (exported separately)
	if (Actor->IsA<AWorldSettings>())
	{
		return true;
	}

	// Skip brushes by default (they're often editor/BSP infrastructure)
	if (Actor->IsA<ABrush>())
	{
		return true;
	}

	// Skip internal actors (names starting with underscore)
	FString ActorName = Actor->GetName();
	if (ActorName.StartsWith(TEXT("_")))
	{
		return true;
	}

	return false;
}

FString UMCTWorldExporter::GetActorClassName(AActor* Actor) const
{
	if (!Actor)
	{
		return TEXT("Unknown");
	}

	FString ClassName = Actor->GetClass()->GetName();

	// Remove common prefixes for cleaner output
	ClassName.RemoveFromStart(TEXT("A"));

	return ClassName;
}
