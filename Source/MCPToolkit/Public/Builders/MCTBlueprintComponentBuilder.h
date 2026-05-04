// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MCTBlueprintComponentBuilder.generated.h"

class FJsonObject;
class UBlueprint;
class UClass;
class USCS_Node;
class USimpleConstructionScript;

/**
 * Static utility class for Actor Blueprint SimpleConstructionScript component authoring.
 *
 * Command handlers own transport concerns; this builder owns Actor Blueprint
 * component resolution, SCS node mutation, component template property edits,
 * and compile/save bookkeeping. Call from the Game Thread only.
 */
UCLASS()
class MCPTOOLKIT_API UMCTBlueprintComponentBuilder : public UObject
{
	GENERATED_BODY()

public:
	/** Load an Actor Blueprint with a valid SimpleConstructionScript. */
	static UBlueprint* LoadActorBlueprint(const FString& AssetPath, FString& OutError);

	/** Resolve a concrete ActorComponent subclass from a class name or path. */
	static UClass* ResolveComponentClass(const FString& ComponentClassName, FString& OutError);

	/** Build the common JSON summary for an SCS component node. */
	static TSharedPtr<FJsonObject> BuildComponentNodeJson(USCS_Node* Node, USimpleConstructionScript* SCS);

	/** List all SCS components for an Actor Blueprint asset. */
	static TSharedPtr<FJsonObject> ListComponents(const FString& AssetPath, FString& OutError);

	/** Add a component node to an Actor Blueprint SCS. */
	static TSharedPtr<FJsonObject> AddComponent(
		const FString& AssetPath,
		const FString& ComponentName,
		const FString& ComponentClassName,
		const FString& ParentComponentName,
		bool bCompileBlueprint,
		bool bSaveAsset,
		FString& OutError);

	/** Remove a component node from an Actor Blueprint SCS. */
	static TSharedPtr<FJsonObject> RemoveComponent(
		const FString& AssetPath,
		const FString& ComponentName,
		bool bPromoteChildren,
		bool bCompileBlueprint,
		bool bSaveAsset,
		FString& OutError);

	/** Set a property on an SCS component template. */
	static TSharedPtr<FJsonObject> SetComponentProperty(
		const FString& AssetPath,
		const FString& ComponentName,
		const FString& PropertyPath,
		const FString& Value,
		bool bCompileBlueprint,
		bool bSaveAsset,
		FString& OutError);

private:
	static void FinishBlueprintMutation(
		UBlueprint* Blueprint,
		bool bStructural,
		bool bCompileBlueprint,
		bool bSaveAsset,
		TSharedPtr<FJsonObject> Data);
};
