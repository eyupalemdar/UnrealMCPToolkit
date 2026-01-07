// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/AIBlueprintExporter.h"
#include "AIWidgetBlueprintExporter.generated.h"

class UWidgetBlueprint;
class UWidget;
class UWidgetTree;

/**
 * Exporter for UWidgetBlueprint assets.
 *
 * Extends Blueprint exporter with Widget-specific functionality:
 * - Widget tree hierarchy
 * - Widget properties (Visibility, Anchors, etc.)
 * - Slot configuration
 *
 * Priority: 100 (highest - checked before base Blueprint exporter)
 */
UCLASS()
class COMMONAIEXPORT_API UAIWidgetBlueprintExporter : public UAIBlueprintExporter
{
	GENERATED_BODY()

public:
	//~ Begin UAIExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 100; }
	virtual FString GetExporterDisplayName() const override { return TEXT("WidgetBlueprintExporter"); }
	//~ End UAIExporterBase Interface

protected:
	/**
	 * Export the widget tree hierarchy.
	 * @param RootWidget The root widget of the tree
	 * @param IndentLevel Current indentation level
	 * @param bFilterDefaults Whether to filter default property values
	 * @return Formatted widget tree text
	 */
	FString ExportWidgetTree(UWidget* RootWidget, int32 IndentLevel, bool bFilterDefaults);

	/**
	 * Export a single widget's properties.
	 */
	FString ExportWidgetProperties(UWidget* Widget, int32 IndentLevel, bool bFilterDefaults);

	/**
	 * Export widget slot properties (alignment, padding, etc.).
	 */
	FString ExportSlotProperties(UWidget* Widget, int32 IndentLevel);

	/**
	 * Get a friendly name for a widget class.
	 */
	FString GetWidgetTypeName(UWidget* Widget) const;

	/**
	 * Export widget properties using Unreal reflection system.
	 * This captures ALL UPROPERTY values including protected members.
	 */
	FString ExportWidgetPropertiesViaReflection(UWidget* Widget, int32 IndentLevel, bool bFilterDefaults);

	/**
	 * Export slot properties using Unreal reflection system.
	 * This captures all slot properties that may be missed by type-specific export.
	 */
	FString ExportSlotPropertiesViaReflection(class UPanelSlot* Slot, int32 IndentLevel, bool bFilterDefaults);

	/**
	 * Check if a widget property should be skipped during reflection export.
	 * Filters out internal/redundant properties.
	 */
	bool ShouldSkipWidgetProperty(FProperty* Property, const FName& PropertyName) const;

	/**
	 * Check if a slot property should be skipped during reflection export.
	 */
	bool ShouldSkipSlotProperty(FProperty* Property, const FName& PropertyName) const;

	/**
	 * Export widget animations (UWidgetAnimation) with their tracks and bindings.
	 */
	FString ExportAnimations(class UWidgetBlueprint* WidgetBP, bool bFilterDefaults);

	/**
	 * Export tracks from a MovieScene (used by animations).
	 */
	FString ExportMovieSceneTracks(class UMovieScene* MovieScene, bool bFilterDefaults);

	/**
	 * Export tracks in simplified format grouped by widget.
	 * Shows: WidgetName → PropertyName: StartValue→EndValue @ TimeRange
	 */
	FString ExportMovieSceneTracksSimplified(class UMovieScene* MovieScene);

	/**
	 * Export a single track in simplified format.
	 */
	FString ExportTrackSimplified(class UMovieSceneTrack* Track, const struct FFrameRate& FrameRate);

	/**
	 * Extract keyframe values from a section for simplified display.
	 * Returns format like "0.0→1.0" for float, "Y: -8→0" for transform.
	 */
	FString ExtractKeyframeValues(class UMovieSceneSection* Section, const struct FFrameRate& FrameRate, float& OutStartTime, float& OutEndTime);
};
