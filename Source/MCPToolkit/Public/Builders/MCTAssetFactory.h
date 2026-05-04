// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MCTAssetFactory.generated.h"

class UInputMappingContext;
class FJsonObject;

/**
 * Generic asset factory for creating common Unreal Engine assets.
 *
 * Supports:
 * - InputAction, InputMappingContext (Enhanced Input)
 * - SoundClass, SoundSubmix, SoundConcurrency, SoundAttenuation (Audio Foundation)
 * - SoundControlBus, SoundControlBusMix, SoundModulationPatch (Audio Modulation)
 * - PhysicalMaterial (Physics)
 *
 * For type-specific editing after creation, use UMCTDataAssetBuilder::SetProperty().
 * All functions are static. Call from Game Thread only.
 */
UCLASS()
class MCPTOOLKIT_API UMCTAssetFactory : public UObject
{
	GENERATED_BODY()

public:
	// =========================================================================
	// GENERIC ASSET CREATION
	// =========================================================================

	/**
	 * Create an asset of the given type.
	 *
	 * @param PackagePath  Content path, e.g. "/Game/Input"
	 * @param AssetName    Asset name, e.g. "IA_Jump"
	 * @param AssetType    Type string: "InputAction", "InputMappingContext",
	 *                     "SoundClass", "SoundSubmix", "SoundConcurrency",
	 *                     "SoundAttenuation", "SoundControlBus", "SoundControlBusMix",
	 *                     "SoundModulationPatch", "PhysicalMaterial"
	 * @param InitialProperties  Optional property→value map (ImportText format)
	 * @return Created asset, or nullptr on failure
	 */
	static UObject* CreateAsset(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& AssetType,
		const TMap<FString, FString>& InitialProperties = TMap<FString, FString>());

	// =========================================================================
	// INPUT MAPPING CONTEXT — CONVENIENCE METHODS
	// =========================================================================

	/**
	 * Add an input mapping to an InputMappingContext.
	 *
	 * @param IMC               The InputMappingContext asset
	 * @param InputActionPath   Path to UInputAction, e.g. "/Game/Input/IA_Jump"
	 * @param KeyName           FKey name, e.g. "SpaceBar", "Gamepad_FaceButton_Bottom"
	 * @param TriggerClasses    Optional trigger class short names, e.g. ["Pressed", "Hold"]
	 * @param ModifierClasses   Optional modifier class short names, e.g. ["Negate", "SwizzleAxis"]
	 * @return true on success
	 */
	static bool AddInputMapping(
		UInputMappingContext* IMC,
		const FString& InputActionPath,
		const FString& KeyName,
		const TArray<FString>& TriggerClasses = TArray<FString>(),
		const TArray<FString>& ModifierClasses = TArray<FString>());

	/**
	 * Remove an input mapping by index from an InputMappingContext.
	 *
	 * @param IMC           The InputMappingContext asset
	 * @param MappingIndex  Index of the mapping to remove
	 * @return true on success
	 */
	static bool RemoveInputMapping(UInputMappingContext* IMC, int32 MappingIndex);

	/**
	 * Get InputMappingContext mappings as JSON.
	 */
	static TSharedPtr<FJsonObject> GetInputMappingsAsJson(UInputMappingContext* IMC);

	// =========================================================================
	// UTILITY
	// =========================================================================

	/** Resolve asset type string to UClass*. Returns nullptr for unsupported types. */
	static UClass* ResolveAssetClass(const FString& AssetType);

	/** Get list of supported asset type names. */
	static TArray<FString> GetSupportedTypes();

private:
	static bool SaveAsset(UObject* Asset);
};
