// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MCTAnimBlueprintBuilder.generated.h"

class UAnimBlueprint;
class FJsonObject;

/**
 * Static utility class for creating AnimBlueprint assets programmatically.
 *
 * Supports:
 * - Creating AnimBlueprints with skeleton and optional parent class
 * - Compiling and saving
 * - Extracting info as JSON
 *
 * EventGraph manipulation is handled by the existing MCTBlueprintGraphBuilder.
 * AnimGraph node manipulation is OUT OF SCOPE (requires Persona reimplementation).
 *
 * All functions are static. Call from Game Thread only.
 */
UCLASS()
class MCPTOOLKIT_API UMCTAnimBlueprintBuilder : public UObject
{
	GENERATED_BODY()

public:
	// =========================================================================
	// LIFECYCLE
	// =========================================================================

	/**
	 * Create a new AnimBlueprint asset.
	 *
	 * @param PackagePath   Content path, e.g. "/Game/Characters/Animations"
	 * @param AssetName     Asset name, e.g. "ABP_Character"
	 * @param SkeletonPath  Path to USkeleton asset, e.g. "/Game/Characters/Skeleton.Skeleton"
	 * @param ParentClass   Optional parent class name. Defaults to "AnimInstance".
	 *                      Can be: "AnimInstance", full class path, or short name.
	 * @return Created AnimBlueprint, or nullptr on failure
	 */
	static UAnimBlueprint* CreateAnimBlueprint(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& SkeletonPath,
		const FString& ParentClass = TEXT("AnimInstance"));

	/**
	 * Compile and save an AnimBlueprint.
	 *
	 * @param AnimBP        The AnimBlueprint to compile
	 * @param OutWarnings   Optional array to receive compiler warnings
	 * @return true if compilation succeeded
	 */
	static bool CompileAndSave(UAnimBlueprint* AnimBP, TArray<FString>* OutWarnings = nullptr);

	/**
	 * Get AnimBlueprint info as JSON (skeleton, parent class, graphs, variables).
	 */
	static TSharedPtr<FJsonObject> GetAnimBlueprintInfoAsJson(UAnimBlueprint* AnimBP);

	// =========================================================================
	// UTILITY
	// =========================================================================

	/** Load an AnimBlueprint by path. */
	static UAnimBlueprint* LoadAnimBlueprint(const FString& AssetPath);
};
