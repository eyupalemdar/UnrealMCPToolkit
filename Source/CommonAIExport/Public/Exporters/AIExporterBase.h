// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AIExporterBase.generated.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
struct FEdGraphPinType;

/**
 * Base class for all AI asset exporters.
 *
 * Each exporter handles a specific asset type (Blueprint, Widget, DataAsset, etc.)
 * and provides both raw and simplified export formats.
 *
 * To add a new exporter:
 * 1. Create a new class inheriting from UAIExporterBase
 * 2. Implement CanExport(), GetSupportedClasses(), and Export()
 * 3. Register it with UAIExporterRegistry
 */
UCLASS(Abstract)
class COMMONAIEXPORT_API UAIExporterBase : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Check if this exporter can handle the given asset.
	 * @param Asset The asset to check
	 * @return true if this exporter can export the asset
	 */
	virtual bool CanExport(UObject* Asset) const PURE_VIRTUAL(UAIExporterBase::CanExport, return false;);

	/**
	 * Get the list of UClass types this exporter supports.
	 * Used for type registration and discovery.
	 */
	virtual TArray<UClass*> GetSupportedClasses() const PURE_VIRTUAL(UAIExporterBase::GetSupportedClasses, return {};);

	/**
	 * Export the asset to text format.
	 * @param Asset The asset to export
	 * @param bFilterDefaults If true, filter out default values for simplified output
	 * @return The exported text content
	 */
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) PURE_VIRTUAL(UAIExporterBase::Export, return TEXT(""););

	/**
	 * Get the priority of this exporter.
	 * Higher priority exporters are checked first during type dispatch.
	 * This allows derived classes (e.g., WidgetBlueprint) to take precedence over base classes (e.g., Blueprint).
	 *
	 * Default priorities:
	 * - WidgetBlueprint: 100
	 * - AnimBlueprint: 90
	 * - Blueprint: 50
	 * - DataAsset: 40
	 * - Others: 50
	 */
	virtual int32 GetPriority() const { return 50; }

	/**
	 * Get a display name for this exporter (for logging/debugging).
	 */
	virtual FString GetExporterDisplayName() const { return GetClass()->GetName(); }

protected:
	//==========================================================================
	// Property Export Helpers
	//==========================================================================

	/**
	 * Export all properties of an object to text format.
	 * @param Object The object to export properties from
	 * @param IndentLevel Indentation level for formatting
	 * @param bFilterDefaults If true, skip properties that have default values
	 * @return Formatted property text
	 */
	FString ExportObjectProperties(UObject* Object, int32 IndentLevel = 0, bool bFilterDefaults = false);

	/**
	 * Export Class Default Object (CDO) properties.
	 * @param Class The class to export CDO from
	 * @param bFilterDefaults If true, skip default values
	 * @return Formatted CDO properties text
	 */
	FString ExportCDOProperties(UClass* Class, bool bFilterDefaults = false);

	/**
	 * Check if a property should be skipped during export.
	 */
	bool ShouldSkipProperty(FProperty* Property, UObject* Object, bool bFilterDefaults) const;

	/**
	 * Format a property value as string.
	 */
	FString FormatPropertyValue(FProperty* Property, const void* ValuePtr, UObject* Object) const;

	/**
	 * Export all properties of an object recursively, including instanced sub-objects.
	 * This provides deep export capability for embedded objects like GameFeatureActions.
	 * Output format: PropertyPath=Value (e.g., "Actions[0].ComponentList[0].ActorClass=...")
	 * @param Object The object to export properties from
	 * @param PathPrefix Current property path prefix (empty for root)
	 * @param Depth Current recursion depth (limited to 10)
	 * @param bFilterDefaults If true, skip properties with default values
	 * @return Formatted property text with full paths
	 */
	FString ExportObjectPropertiesDeep(UObject* Object, const FString& PathPrefix = TEXT(""), int32 Depth = 0, bool bFilterDefaults = false);

	/**
	 * Export a single property with its full path, handling arrays, objects, and structs recursively.
	 * @param Property The property to export
	 * @param ValuePtr Pointer to the property value
	 * @param Object The owning object
	 * @param CurrentPath Current property path
	 * @param Depth Current recursion depth
	 * @param bFilterDefaults If true, skip default values
	 * @param OutResult Output string to append results
	 */
	void ExportPropertyWithPath(FProperty* Property, const void* ValuePtr, UObject* Object, const FString& CurrentPath, int32 Depth, bool bFilterDefaults, FString& OutResult);

	/**
	 * Check if a property contains instanced object references.
	 */
	bool IsInstancedObjectProperty(FProperty* Property) const;

	//==========================================================================
	// Graph Export Helpers (for Blueprints)
	//==========================================================================

	/**
	 * Export a Blueprint graph to text format.
	 * @param Graph The graph to export
	 * @return Formatted graph text
	 */
	FString ExportGraphToText(UEdGraph* Graph);

	/**
	 * Export a Blueprint graph to simplified text format.
	 * @param Graph The graph to export
	 * @return Simplified graph text
	 */
	FString ExportGraphToTextSimplified(UEdGraph* Graph);

	/**
	 * Export a graph node to simplified format.
	 */
	FString ExportNodeSimplified(UEdGraphNode* Node);

	/**
	 * Export a graph pin to simplified format.
	 */
	FString ExportPinSimplified(UEdGraphPin* Pin);

	/**
	 * Get a string representation of a pin type.
	 */
	FString GetPinTypeString(const FEdGraphPinType& PinType);

	//==========================================================================
	// Formatting Helpers
	//==========================================================================

	/**
	 * Get indentation string for a given level.
	 */
	FString GetIndent(int32 Level) const;

	/**
	 * Create a section header (e.g., "=== BLUEPRINT: MyBP ===")
	 */
	FString MakeSectionHeader(const FString& Title) const;

	/**
	 * Create a subsection header (e.g., "--- Properties ---")
	 */
	FString MakeSubsectionHeader(const FString& Title) const;

	/**
	 * Sanitize a string for use in export output.
	 */
	FString SanitizeString(const FString& Input) const;
};
