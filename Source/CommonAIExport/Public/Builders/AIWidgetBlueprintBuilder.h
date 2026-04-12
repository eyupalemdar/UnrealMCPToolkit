// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AIWidgetBlueprintBuilder.generated.h"

class UWidgetBlueprint;
class UWidget;
class UPanelWidget;
class FJsonObject;

/**
 * Static utility class for programmatic Widget Blueprint creation and modification.
 *
 * Mirrors the Exporters/ architecture but for writing:
 * - CreateWidgetBlueprint: Create new WBP assets
 * - AddWidget/RemoveWidget/MoveWidget: Manipulate widget tree
 * - SetWidgetProperty/SetSlotProperty: Set properties via reflection (ImportText)
 * - CompileAndSave: Compile and persist to disk
 *
 * Property values use UE ImportText format (same as ExportText produces),
 * enabling round-trip compatibility with the export system.
 *
 * All functions are static. Call from Game Thread only.
 */
UCLASS()
class COMMONAIEXPORT_API UAIWidgetBlueprintBuilder : public UObject
{
	GENERATED_BODY()

public:
	// =========================================================================
	// BLUEPRINT LIFECYCLE
	// =========================================================================

	/**
	 * Create a new Widget Blueprint asset.
	 * @param PackagePath Content path, e.g. "/Game/UI"
	 * @param AssetName Asset name, e.g. "WBP_MyWidget"
	 * @param ParentClass Parent class (nullptr = UUserWidget). Must be UUserWidget subclass.
	 * @return The created UWidgetBlueprint, or nullptr on failure
	 */
	static UWidgetBlueprint* CreateWidgetBlueprint(
		const FString& PackagePath,
		const FString& AssetName,
		UClass* ParentClass = nullptr);

	/**
	 * Compile and save a Widget Blueprint to disk.
	 * @param WidgetBP The Widget Blueprint to compile and save
	 * @param OutWarnings Optional array to receive compiler warnings
	 * @return true if compilation succeeded without errors
	 */
	static bool CompileAndSave(
		UWidgetBlueprint* WidgetBP,
		TArray<FString>* OutWarnings = nullptr);

	// =========================================================================
	// WIDGET TREE MANIPULATION
	// =========================================================================

	/**
	 * Add a widget to the widget tree.
	 * @param WidgetBP Target Widget Blueprint
	 * @param WidgetClassName Widget class short name ("TextBlock", "Button", "VerticalBox") or full path
	 * @param WidgetName Desired name for the new widget
	 * @param ParentWidgetName Name of the parent panel widget (empty = set as root)
	 * @return The created widget, or nullptr on failure. Check GetName() for the actual assigned name.
	 */
	static UWidget* AddWidget(
		UWidgetBlueprint* WidgetBP,
		const FString& WidgetClassName,
		const FString& WidgetName,
		const FString& ParentWidgetName = TEXT(""));

	/**
	 * Remove a widget from the widget tree.
	 * @param WidgetBP Target Widget Blueprint
	 * @param WidgetName Name of the widget to remove
	 * @return true if removal succeeded
	 */
	static bool RemoveWidget(
		UWidgetBlueprint* WidgetBP,
		const FString& WidgetName);

	/**
	 * Move a widget to a new parent in the widget tree.
	 * @param WidgetBP Target Widget Blueprint
	 * @param WidgetName Name of the widget to move
	 * @param NewParentName Name of the new parent panel widget (empty = make root)
	 * @param NewIndex Position among siblings (-1 = append at end)
	 * @return true if move succeeded
	 */
	static bool MoveWidget(
		UWidgetBlueprint* WidgetBP,
		const FString& WidgetName,
		const FString& NewParentName,
		int32 NewIndex = -1);

	// =========================================================================
	// PROPERTY SETTING
	// =========================================================================

	/**
	 * Set a widget property using UE reflection.
	 * Supports dot-notation for struct properties: "Font.Size", "ColorAndOpacity.A"
	 * Values use UE ImportText format (same format the export system produces).
	 * @param WidgetBP Target Widget Blueprint
	 * @param WidgetName Name of the target widget
	 * @param PropertyName Property name (supports dot-notation for structs)
	 * @param Value String value in ImportText format
	 * @return true if property was set successfully
	 */
	static bool SetWidgetProperty(
		UWidgetBlueprint* WidgetBP,
		const FString& WidgetName,
		const FString& PropertyName,
		const FString& Value);

	/**
	 * Set a slot property on a widget.
	 * @param WidgetBP Target Widget Blueprint
	 * @param WidgetName Name of the widget whose slot to modify
	 * @param PropertyName Slot property name (supports dot-notation)
	 * @param Value String value in ImportText format
	 * @return true if property was set successfully
	 */
	static bool SetSlotProperty(
		UWidgetBlueprint* WidgetBP,
		const FString& WidgetName,
		const FString& PropertyName,
		const FString& Value);

	/**
	 * Set multiple properties on a widget in one call.
	 * @param WidgetBP Target Widget Blueprint
	 * @param WidgetName Name of the target widget
	 * @param Properties Map of PropertyName -> Value pairs
	 * @param OutFailed Optional array to receive names of properties that failed to set
	 * @return Number of properties successfully set
	 */
	static int32 SetWidgetProperties(
		UWidgetBlueprint* WidgetBP,
		const FString& WidgetName,
		const TMap<FString, FString>& Properties,
		TArray<FString>* OutFailed = nullptr);

	/**
	 * Convenience: Set canvas slot layout (position, size, anchors, alignment).
	 * Only works for widgets whose parent is a CanvasPanel.
	 * @return true if layout was set
	 */
	static bool SetCanvasSlotLayout(
		UWidgetBlueprint* WidgetBP,
		const FString& WidgetName,
		float PositionX, float PositionY,
		float SizeX, float SizeY,
		float AnchorMinX = 0.f, float AnchorMinY = 0.f,
		float AnchorMaxX = 0.f, float AnchorMaxY = 0.f,
		float AlignmentX = 0.f, float AlignmentY = 0.f);

	// =========================================================================
	// CDO (CLASS DEFAULT OBJECT) PROPERTIES
	// =========================================================================

	/**
	 * Set a CDO (Class Default Object) property on a Blueprint.
	 * Works on the Blueprint's generated class CDO, not widget instances.
	 * Use for properties like bSelectable, bIsFocusable, ClickMethod, etc.
	 * @param WidgetBP Target Widget Blueprint
	 * @param PropertyName Property name (supports dot-notation for structs)
	 * @param Value String value in ImportText format
	 * @return true if property was set successfully
	 */
	static bool SetCDOProperty(
		UWidgetBlueprint* WidgetBP,
		const FString& PropertyName,
		const FString& Value);

	/**
	 * Get CDO properties as JSON for inspection.
	 * Returns own properties (not inherited from engine base classes).
	 */
	static TSharedPtr<FJsonObject> GetCDOPropertiesAsJson(UWidgetBlueprint* WidgetBP);

	// =========================================================================
	// ARRAY PROPERTY SUPPORT
	// =========================================================================

	/**
	 * Add an element to an array property on an object and set its sub-property values.
	 * @param Object Target object (CDO, widget, etc.)
	 * @param ArrayPropertyName Name of the TArray property
	 * @param ElementValues Map of sub-property name → ImportText value
	 * @return Index of the newly added element, or -1 on failure
	 */
	static int32 AddArrayElement(
		UObject* Object,
		const FString& ArrayPropertyName,
		const TMap<FString, FString>& ElementValues,
		const FString& ClassName);

	/**
	 * Remove an element from an array property by index.
	 */
	static bool RemoveArrayElement(
		UObject* Object,
		const FString& ArrayPropertyName,
		int32 Index);

	/**
	 * Get array property length.
	 */
	static int32 GetArrayLength(
		UObject* Object,
		const FString& ArrayPropertyName);

	/**
	 * Set a sub-property on a specific array element.
	 * @param Object Target object
	 * @param ArrayPropertyName Array property name
	 * @param Index Element index
	 * @param SubPropertyName Sub-property within the element (supports dot-notation)
	 * @param Value ImportText value
	 */
	static bool SetArrayElementProperty(
		UObject* Object,
		const FString& ArrayPropertyName,
		int32 Index,
		const FString& SubPropertyName,
		const FString& Value);

	// =========================================================================
	// BLUEPRINT REPARENTING
	// =========================================================================

	/**
	 * Change the parent class of a Widget Blueprint.
	 * @param WidgetBP Target Widget Blueprint to reparent
	 * @param NewParentClass New parent class (must be UUserWidget subclass)
	 * @return true if reparenting succeeded
	 */
	static bool ReparentBlueprint(
		UWidgetBlueprint* WidgetBP,
		UClass* NewParentClass);

	// =========================================================================
	// UTILITY
	// =========================================================================

	/** Resolve a widget class short name to UClass*. Thread-safe after first call. */
	static UClass* ResolveWidgetClass(const FString& ClassNameOrPath);

	/** Find a widget by name in the widget tree. */
	static UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBP, const FString& WidgetName);

	/** Load a Widget Blueprint by asset path. */
	static UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath);

	/** Get the widget tree as a JSON object for verification. */
	static TSharedPtr<FJsonObject> GetWidgetTreeAsJson(UWidgetBlueprint* WidgetBP);

	/** Get all available non-abstract widget class names. */
	static TArray<TPair<FString, bool>> GetAvailableWidgetClasses();

private:
	/** Lazy-initialized widget class name → UClass* map */
	static TMap<FString, UClass*>& GetWidgetClassMap();

	/** Set a property on a UObject using dot-notation path and ImportText */
	static bool SetPropertyByPath(UObject* Object, const FString& PropertyPath, const FString& Value);

	/** Mark the widget blueprint as structurally modified */
	static void MarkModified(UWidgetBlueprint* WidgetBP);

	/** Recursively build JSON for a widget and its children */
	static TSharedPtr<FJsonObject> WidgetToJson(UWidget* Widget);

	/** Recursively collect all descendant widgets of a panel widget */
	static void CollectAllDescendants(UPanelWidget* Parent, TArray<UWidget*>& OutDescendants);
};
