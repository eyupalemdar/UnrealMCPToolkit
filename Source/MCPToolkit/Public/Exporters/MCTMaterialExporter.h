// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/MCTExporterBase.h"
#include "MCTMaterialExporter.generated.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstance;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UMaterialExpression;
class UMaterialExpressionParameter;

/**
 * Exporter for Material and MaterialInstance assets.
 *
 * Handles:
 * - UMaterial (full graph export with all expressions and connections)
 * - UMaterialInstance (parent reference + parameter overrides only)
 * - UMaterialInstanceConstant
 * - UMaterialInstanceDynamic
 *
 * Priority: 45 (between DataAsset:40 and Blueprint:50)
 *
 * Export Format for Materials:
 * - Material metadata (Domain, BlendMode, ShadingModel, etc.)
 * - Material settings (TwoSided, bUsedWithStaticMesh, etc.)
 * - Parameters (Scalar, Vector, Texture, StaticSwitch)
 * - Root pin connections
 * - Full material graph with all expressions and connections
 *
 * Export Format for MaterialInstances:
 * - Parent material reference
 * - Parameter values (Scalar, Vector, Texture)
 * - Static switch parameter values
 */
UCLASS()
class MCPTOOLKIT_API UMCTMaterialExporter : public UMCTExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UMCTExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 45; }
	virtual FString GetExporterDisplayName() const override { return TEXT("MaterialExporter"); }
	//~ End UMCTExporterBase Interface

protected:
	/**
	 * Export a Material asset with full graph.
	 */
	FString ExportMaterial(UMaterial* Material, bool bFilterDefaults);

	/**
	 * Export a MaterialInstance with parent reference and parameter overrides only.
	 */
	FString ExportMaterialInstance(UMaterialInstance* MaterialInstance, bool bFilterDefaults);

	/**
	 * Export material metadata (Domain, BlendMode, ShadingModel, etc.)
	 */
	FString ExportMaterialMetadata(UMaterial* Material);

	/**
	 * Export material settings (TwoSided, usage flags, etc.)
	 */
	FString ExportMaterialSettings(UMaterial* Material);

	/**
	 * Export all parameters from a material or material instance.
	 * @param Material The material interface to get parameters from
	 * @param bIncludeDefaults If true, includes default values; if false, only overridden values
	 */
	FString ExportMaterialParameters(UMaterialInterface* Material, bool bIncludeDefaults = true);

	/**
	 * Export the full material graph with all expressions and connections.
	 */
	FString ExportMaterialGraph(UMaterial* Material);

	/**
	 * Export root pin connections (which expressions connect to the root node).
	 */
	FString ExportRootConnections(UMaterial* Material);

	/**
	 * Export a single material expression to text format.
	 */
	FString ExportMaterialExpression(UMaterialExpression* Expression);

	/**
	 * Get the expression type name (e.g., "Add", "Lerp", "TextureSample").
	 */
	FString GetExpressionTypeName(UMaterialExpression* Expression) const;

	/**
	 * Get the expression's display name or description.
	 */
	FString GetExpressionDisplayName(UMaterialExpression* Expression) const;

	/**
	 * Export expression input connections.
	 */
	FString ExportExpressionInputs(UMaterialExpression* Expression);

	/**
	 * Export expression output connections.
	 */
	FString ExportExpressionOutputs(UMaterialExpression* Expression);

	/**
	 * Export expression-specific properties (e.g., ConstValue for Constant, ParameterName for Parameter).
	 */
	FString ExportExpressionProperties(UMaterialExpression* Expression);

	/**
	 * Get the name of the material property (root pin) that an expression connects to.
	 * @param PropertyIndex The EMaterialProperty index
	 * @return Property name like "BaseColor", "Metallic", "Opacity", etc.
	 */
	FString GetMaterialPropertyName(int32 PropertyIndex) const;
};
