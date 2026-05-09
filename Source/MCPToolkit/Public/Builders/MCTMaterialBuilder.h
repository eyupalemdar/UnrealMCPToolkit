// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "MaterialDomain.h"
#include "MCTMaterialBuilder.generated.h"

class UMaterial;
class UMaterialExpression;
class UMaterialInstanceConstant;
class FJsonObject;

/**
 * Static utility class for programmatic Material and Material Instance creation.
 *
 * Mirrors MCTWidgetBlueprintBuilder architecture:
 * - CreateMaterial: Create new UMaterial assets
 * - AddExpression: Add material expression nodes
 * - SetExpressionProperty: Set expression properties via reflection
 * - ConnectExpressions: Wire nodes together
 * - ConnectToMaterialProperty: Wire to root inputs (BaseColor, etc.)
 * - CompileMaterial: Recompile + save
 * - CreateMaterialInstance: Create UMaterialInstanceConstant
 * - SetInstanceParameter: Set scalar/vector/texture parameters
 *
 * Property values use UE ImportText format (same as widget builder).
 * All functions are static. Call from Game Thread only.
 */
UCLASS()
class MCPTOOLKIT_API UMCTMaterialBuilder : public UObject
{
	GENERATED_BODY()

public:
	// =========================================================================
	// MATERIAL LIFECYCLE
	// =========================================================================

	static UMaterial* CreateMaterial(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& Domain = TEXT("Surface"),
		const FString& BlendMode = TEXT("Opaque"),
		const FString& ShadingModel = TEXT("DefaultLit"),
		bool bTwoSided = false);

	static bool SetMaterialProperty(
		UMaterial* Material,
		const FString& PropertyName,
		const FString& Value);

	static bool CompileMaterial(
		UMaterial* Material,
		TArray<FString>* OutWarnings = nullptr);

	static UMaterial* LoadMaterial(const FString& AssetPath);

	// =========================================================================
	// EXPRESSION MANIPULATION
	// =========================================================================

	static UMaterialExpression* AddExpression(
		UMaterial* Material,
		const FString& ExpressionClass,
		const FString& NodeName,
		int32 PosX = 0,
		int32 PosY = 0);

	static bool SetExpressionProperty(
		UMaterial* Material,
		const FString& NodeName,
		const FString& PropertyName,
		const FString& Value);

	static bool ConnectExpressions(
		UMaterial* Material,
		const FString& FromNodeName,
		const FString& FromOutput,
		const FString& ToNodeName,
		const FString& ToInput);

	static bool ConnectToMaterialProperty(
		UMaterial* Material,
		const FString& FromNodeName,
		const FString& FromOutput,
		const FString& MaterialProperty);

	static bool DisconnectInput(
		UMaterial* Material,
		const FString& NodeName,
		const FString& InputName);

	static bool RemoveExpression(
		UMaterial* Material,
		const FString& NodeName);

	static TSharedPtr<FJsonObject> GetMaterialGraphAsJson(UMaterial* Material);

	// =========================================================================
	// MATERIAL INSTANCE
	// =========================================================================

	static UMaterialInstanceConstant* CreateMaterialInstance(
		const FString& PackagePath,
		const FString& AssetName,
		const FString& ParentMaterialPath);

	static bool SetInstanceParameter(
		UMaterialInstanceConstant* MIC,
		const FString& ParamName,
		const FString& ParamType,
		const FString& Value);

	static bool SaveMaterialInstance(UMaterialInstanceConstant* MIC);

	static UMaterialInstanceConstant* LoadMaterialInstance(const FString& AssetPath);

	static TSharedPtr<FJsonObject> GetMaterialInstanceInfoAsJson(UMaterialInstanceConstant* MIC);

	// =========================================================================
	// UTILITY
	// =========================================================================

	static UClass* ResolveExpressionClass(const FString& ClassNameOrPath);
	static UMaterialExpression* FindExpressionByName(UMaterial* Material, const FString& NodeName);
	static TArray<FString> GetAvailableExpressionClasses();
	static EMaterialProperty ResolveMaterialProperty(const FString& PropertyName);

private:
	static TMap<FString, UClass*>& GetExpressionClassMap();
	static bool SetPropertyByPath(UObject* Object, const FString& PropertyPath, const FString& Value);
	static bool SaveAsset(UObject* Asset);
	static TSharedPtr<FJsonObject> ExpressionToJson(UMaterialExpression* Expr, int32 Index);
	static FString MakePinMatchKey(const FString& PinName);
	static bool ResolveInputPinName(UMaterialExpression* Expr, const FString& RequestedInput, FString& OutResolvedInput);
	static bool ResolveOutputPinName(UMaterialExpression* Expr, const FString& RequestedOutput, FString& OutResolvedOutput, int32& OutOutputIndex);
	static bool IsExpressionInputConnected(UMaterialExpression* ToExpr, const FString& ToInput, UMaterialExpression* FromExpr, int32 FromOutputIndex);
	static FString GetExpressionJsonName(UMaterialExpression* Expr);
};
