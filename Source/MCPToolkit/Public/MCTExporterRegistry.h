// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MCTExporterRegistry.generated.h"

class UMCTExporterBase;

/**
 * Registry for AI Asset Exporters.
 *
 * Manages exporter instances and provides type dispatch functionality.
 * Exporters are registered with priorities - higher priority exporters
 * are checked first, allowing derived classes (WidgetBlueprint) to take
 * precedence over base classes (Blueprint).
 *
 * Usage:
 *   UMCTExporterRegistry* Registry = UMCTExporterRegistry::Get();
 *   if (UMCTExporterBase* Exporter = Registry->FindExporterForAsset(MyAsset))
 *   {
 *       FString ExportedText = Exporter->Export(MyAsset, true);
 *   }
 */
UCLASS()
class MCPTOOLKIT_API UMCTExporterRegistry : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Get the singleton instance of the registry.
	 * Creates and initializes the registry if it doesn't exist.
	 */
	static UMCTExporterRegistry* Get();

	/**
	 * Register an exporter class.
	 * Creates an instance and adds it to the registry.
	 * @param ExporterClass The exporter class to register
	 */
	void RegisterExporter(TSubclassOf<UMCTExporterBase> ExporterClass);

	/**
	 * Register an exporter instance directly.
	 * @param Exporter The exporter instance to register
	 */
	void RegisterExporter(UMCTExporterBase* Exporter);

	/**
	 * Find an appropriate exporter for the given asset.
	 * Returns the highest priority exporter that can handle the asset.
	 * @param Asset The asset to find an exporter for
	 * @return The exporter, or nullptr if no suitable exporter is found
	 */
	UMCTExporterBase* FindExporterForAsset(UObject* Asset) const;

	/**
	 * Check if an asset type is supported by any registered exporter.
	 * @param Asset The asset to check
	 * @return true if the asset can be exported
	 */
	bool IsAssetSupported(UObject* Asset) const;

	/**
	 * Get all classes supported by registered exporters.
	 * @return Array of supported UClass types
	 */
	TArray<UClass*> GetAllSupportedClasses() const;

	/**
	 * Get all registered exporters.
	 * @return Array of registered exporter instances
	 */
	const TArray<UMCTExporterBase*>& GetRegisteredExporters() const { return RegisteredExporters; }

	/**
	 * Get the number of registered exporters.
	 */
	int32 GetExporterCount() const { return RegisteredExporters.Num(); }

	/**
	 * Clear all registered exporters (mainly for testing).
	 */
	void ClearExporters();

protected:
	/**
	 * Register all default exporters.
	 * Called automatically when the registry is first accessed.
	 */
	void RegisterDefaultExporters();

	/**
	 * Sort exporters by priority (highest first).
	 */
	void SortExportersByPriority();

private:
	/** Singleton instance */
	static UMCTExporterRegistry* Instance;

	/** Registered exporter instances, sorted by priority */
	UPROPERTY()
	TArray<TObjectPtr<UMCTExporterBase>> RegisteredExporters;

	/** Flag to track if default exporters have been registered */
	bool bDefaultExportersRegistered = false;
};
