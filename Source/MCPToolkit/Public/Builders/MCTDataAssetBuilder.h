// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MCTDataAssetBuilder.generated.h"

class FJsonObject;

/**
 * Static utility class for Data Asset manipulation via TCP commands.
 *
 * Provides generic UObject asset loading (Data Assets, etc.) and nested path
 * resolution for array/property operations. Works alongside MCTWidgetBlueprintBuilder
 * as a fallback for non-Widget-Blueprint assets.
 *
 * Nested path syntax: "Actions[0].Widgets" navigates through instanced subobjects.
 *
 * All functions are static. Call from Game Thread only.
 */
UCLASS()
class MCPTOOLKIT_API UMCTDataAssetBuilder : public UObject
{
	GENERATED_BODY()

public:
	// =========================================================================
	// ASSET LIFECYCLE
	// =========================================================================

	/**
	 * Load any UObject asset by path (Data Asset, Blueprint, etc.).
	 * Normalizes path by appending object name if missing dot-suffix.
	 */
	static UObject* LoadAssetObject(const FString& AssetPath);

	/**
	 * Save an asset to disk (no compile step, just SavePackage).
	 */
	static bool SaveAsset(UObject* Asset);

	// =========================================================================
	// NESTED PATH RESOLUTION
	// =========================================================================

	/**
	 * Resolve a nested path like "Actions[0].Widgets" on a root object.
	 *
	 * Navigates through array elements and object/struct properties to find
	 * the target object and leaf property name.
	 *
	 * @param RootObject     The root UObject to start from
	 * @param FullPath       Dot-separated path with optional array indices
	 * @param OutTargetObject  Receives the object that owns the leaf property
	 * @param OutPropertyName  Receives the leaf property name
	 * @return true if the path was resolved successfully
	 */
	static bool ResolveNestedPath(
		UObject* RootObject,
		const FString& FullPath,
		UObject*& OutTargetObject,
		FString& OutPropertyName);

	// =========================================================================
	// NESTED PATH ARRAY OPERATIONS
	// =========================================================================

	/**
	 * Add element to an array property, supporting nested paths.
	 * Delegates to UMCTWidgetBlueprintBuilder::AddArrayElement after path resolution.
	 */
	static int32 AddArrayElement(
		UObject* Asset,
		const FString& ArrayPath,
		const TMap<FString, FString>& ElementValues,
		const FString& ClassName);

	/**
	 * Remove element from an array property by index, supporting nested paths.
	 */
	static bool RemoveArrayElement(
		UObject* Asset,
		const FString& ArrayPath,
		int32 Index);

	/**
	 * Get array length, supporting nested paths.
	 */
	static int32 GetArrayLength(
		UObject* Asset,
		const FString& ArrayPath);

	/**
	 * Set a sub-property on a specific array element, supporting nested paths.
	 */
	static bool SetArrayElementProperty(
		UObject* Asset,
		const FString& ArrayPath,
		int32 Index,
		const FString& SubPropertyName,
		const FString& Value);

	// =========================================================================
	// NESTED PATH PROPERTY OPERATIONS
	// =========================================================================

	/**
	 * Set a property value, supporting nested paths.
	 */
	static bool SetProperty(
		UObject* Asset,
		const FString& PropertyPath,
		const FString& Value);

	/**
	 * Get all properties as JSON.
	 */
	static TSharedPtr<FJsonObject> GetPropertiesAsJson(UObject* Asset);
};
