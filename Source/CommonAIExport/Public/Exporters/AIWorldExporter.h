// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/AIExporterBase.h"
#include "AIWorldExporter.generated.h"

class UWorld;
class ULevel;
class AActor;
class ULevelStreaming;

/**
 * Exporter for UWorld (Map/Level) assets.
 *
 * This is a NEW capability for CommonAIExport - Map export support.
 *
 * Exports:
 * - World metadata (name, type, bounds)
 * - World Settings
 * - All actors in the level (grouped by class)
 * - Streaming levels
 * - Level blueprints
 *
 * Priority: 50 (standard)
 */
UCLASS()
class COMMONAIEXPORT_API UAIWorldExporter : public UAIExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UAIExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 50; }
	virtual FString GetExporterDisplayName() const override { return TEXT("WorldExporter"); }
	//~ End UAIExporterBase Interface

protected:
	/**
	 * Export world metadata (name, bounds, etc.)
	 */
	FString ExportWorldMetadata(UWorld* World);

	/**
	 * Export world settings actor properties.
	 */
	FString ExportWorldSettings(UWorld* World, bool bFilterDefaults);

	/**
	 * Export all actors in the world.
	 * Groups actors by class for better readability.
	 */
	FString ExportActors(UWorld* World, bool bFilterDefaults);

	/**
	 * Export a single actor.
	 */
	FString ExportActor(AActor* Actor, int32 IndentLevel, bool bFilterDefaults);

	/**
	 * Export streaming levels configuration.
	 */
	FString ExportStreamingLevels(UWorld* World);

	/**
	 * Get actor classes to skip in export (internal/hidden actors).
	 */
	bool ShouldSkipActor(AActor* Actor) const;

	/**
	 * Get a friendly class name for an actor.
	 */
	FString GetActorClassName(AActor* Actor) const;
};
