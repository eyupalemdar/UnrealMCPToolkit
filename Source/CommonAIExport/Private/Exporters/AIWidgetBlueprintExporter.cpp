// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/AIWidgetBlueprintExporter.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
// Section types for keyframe extraction
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneColorSection.h"
// 2D Transform is in UMG module, not MovieSceneTracks
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/Spacer.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "CommonTextBlock.h"
#include "UObject/PropertyIterator.h"

bool UAIWidgetBlueprintExporter::CanExport(UObject* Asset) const
{
	return Asset && Asset->IsA<UWidgetBlueprint>();
}

TArray<UClass*> UAIWidgetBlueprintExporter::GetSupportedClasses() const
{
	return { UWidgetBlueprint::StaticClass() };
}

FString UAIWidgetBlueprintExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset);
	if (!WidgetBP)
	{
		return TEXT("Error: Not a Widget Blueprint\n");
	}

	FString Output;

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("WIDGET BLUEPRINT: %s"), *WidgetBP->GetName()));

	// Parent class
	Output += FString::Printf(TEXT("ParentClass: %s\n"),
		WidgetBP->ParentClass ? *WidgetBP->ParentClass->GetName() : TEXT("None"));
	Output += TEXT("\n");

	// Widget Tree
	if (WidgetBP->WidgetTree)
	{
		Output += MakeSectionHeader(TEXT("WIDGET TREE"));

		if (UWidget* RootWidget = WidgetBP->WidgetTree->RootWidget)
		{
			Output += ExportWidgetTree(RootWidget, 0, bFilterDefaults);
		}
		else
		{
			Output += TEXT("(no root widget)\n");
		}
		Output += TEXT("\n");
	}

	// Named slots
	if (WidgetBP->WidgetTree)
	{
		TArray<FName> SlotNames;
		WidgetBP->WidgetTree->GetSlotNames(SlotNames);

		if (SlotNames.Num() > 0)
		{
			Output += MakeSectionHeader(TEXT("NAMED SLOTS"));
			for (const FName& SlotName : SlotNames)
			{
				Output += FString::Printf(TEXT("- %s\n"), *SlotName.ToString());
			}
			Output += TEXT("\n");
		}
	}

	// Widget animations
	if (WidgetBP->Animations.Num() > 0)
	{
		Output += ExportAnimations(WidgetBP, bFilterDefaults);
	}

	// Blueprint portion (graphs, variables, etc.) - use parent implementation
	Output += ExportBlueprint(WidgetBP, bFilterDefaults);

	return Output;
}

FString UAIWidgetBlueprintExporter::ExportWidgetTree(UWidget* Widget, int32 IndentLevel, bool bFilterDefaults)
{
	if (!Widget)
	{
		return TEXT("");
	}

	FString Output;
	FString Indent = GetIndent(IndentLevel);

	// Widget header: name and type
	FString WidgetType = GetWidgetTypeName(Widget);
	Output += FString::Printf(TEXT("%s[%s] %s\n"), *Indent, *Widget->GetName(), *WidgetType);

	// Export key widget properties (legacy hardcoded export for backward compatibility)
	Output += ExportWidgetProperties(Widget, IndentLevel + 1, bFilterDefaults);

	// Export ALL widget properties via reflection (captures protected members too)
	Output += ExportWidgetPropertiesViaReflection(Widget, IndentLevel + 1, bFilterDefaults);

	// Export slot properties (legacy hardcoded export)
	Output += ExportSlotProperties(Widget, IndentLevel + 1);

	// Export ALL slot properties via reflection
	if (Widget->Slot)
	{
		Output += ExportSlotPropertiesViaReflection(Widget->Slot, IndentLevel + 1, bFilterDefaults);
	}

	// Recurse into children
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			if (UWidget* Child = Panel->GetChildAt(i))
			{
				Output += ExportWidgetTree(Child, IndentLevel + 1, bFilterDefaults);
			}
		}
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::ExportWidgetProperties(UWidget* Widget, int32 IndentLevel, bool bFilterDefaults)
{
	if (!Widget)
	{
		return TEXT("");
	}

	FString Output;
	FString Indent = GetIndent(IndentLevel);

	// Visibility (always include if not Visible)
	if (Widget->GetVisibility() != ESlateVisibility::Visible)
	{
		Output += FString::Printf(TEXT("%sVisibility: %s\n"), *Indent,
			*UEnum::GetValueAsString(Widget->GetVisibility()));
	}

	// Is Variable (for BP access)
	if (Widget->bIsVariable)
	{
		Output += FString::Printf(TEXT("%sbIsVariable: true\n"), *Indent);
	}

	// Tool Tip (if set)
	FText ToolTip = Widget->GetToolTipText();
	if (!ToolTip.IsEmpty())
	{
		Output += FString::Printf(TEXT("%sToolTip: %s\n"), *Indent, *ToolTip.ToString());
	}

	// For simplified export, skip most default properties
	if (!bFilterDefaults)
	{
		// RenderTransform, RenderOpacity, etc.
		if (Widget->GetRenderOpacity() != 1.0f)
		{
			Output += FString::Printf(TEXT("%sRenderOpacity: %.2f\n"), *Indent, Widget->GetRenderOpacity());
		}
	}

	// ========== Widget-Specific Properties ==========

	// USpacer - Size
	if (USpacer* Spacer = Cast<USpacer>(Widget))
	{
		FVector2D Size = Spacer->GetSize();
		if (Size.X != 1.0f || Size.Y != 1.0f)
		{
			Output += FString::Printf(TEXT("%sSize: (%.1f, %.1f)\n"), *Indent, Size.X, Size.Y);
		}
	}

	// UImage - Brush
	if (UImage* Image = Cast<UImage>(Widget))
	{
		FSlateBrush Brush = Image->GetBrush();

		// Resource
		if (Brush.GetResourceObject())
		{
			Output += FString::Printf(TEXT("%sBrush.Resource: %s\n"), *Indent, *Brush.GetResourceObject()->GetPathName());
		}

		// ImageSize (if not default 32x32)
		if (Brush.ImageSize.X != 32.0f || Brush.ImageSize.Y != 32.0f)
		{
			Output += FString::Printf(TEXT("%sBrush.ImageSize: (%.1f, %.1f)\n"), *Indent, Brush.ImageSize.X, Brush.ImageSize.Y);
		}

		// DrawAs (if not Image)
		if (Brush.DrawAs != ESlateBrushDrawType::Image)
		{
			Output += FString::Printf(TEXT("%sBrush.DrawAs: %s\n"), *Indent, *UEnum::GetValueAsString(Brush.DrawAs));
		}

		// Margin (if not zero)
		if (Brush.Margin.Left != 0.0f || Brush.Margin.Top != 0.0f || Brush.Margin.Right != 0.0f || Brush.Margin.Bottom != 0.0f)
		{
			Output += FString::Printf(TEXT("%sBrush.Margin: (%.2f, %.2f, %.2f, %.2f)\n"), *Indent,
				Brush.Margin.Left, Brush.Margin.Top, Brush.Margin.Right, Brush.Margin.Bottom);
		}
	}

	// UBorder - Brush and Padding
	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		// Background brush - access via property
		const FSlateBrush& Brush = Border->Background;

		// Resource
		if (Brush.GetResourceObject())
		{
			Output += FString::Printf(TEXT("%sBackground.Resource: %s\n"), *Indent, *Brush.GetResourceObject()->GetPathName());
		}

		// ImageSize
		if (Brush.ImageSize.X != 32.0f || Brush.ImageSize.Y != 32.0f)
		{
			Output += FString::Printf(TEXT("%sBackground.ImageSize: (%.1f, %.1f)\n"), *Indent, Brush.ImageSize.X, Brush.ImageSize.Y);
		}

		// DrawAs
		if (Brush.DrawAs != ESlateBrushDrawType::Image)
		{
			Output += FString::Printf(TEXT("%sBackground.DrawAs: %s\n"), *Indent, *UEnum::GetValueAsString(Brush.DrawAs));
		}

		// Margin
		if (Brush.Margin.Left != 0.0f || Brush.Margin.Top != 0.0f || Brush.Margin.Right != 0.0f || Brush.Margin.Bottom != 0.0f)
		{
			Output += FString::Printf(TEXT("%sBackground.Margin: (%.2f, %.2f, %.2f, %.2f)\n"), *Indent,
				Brush.Margin.Left, Brush.Margin.Top, Brush.Margin.Right, Brush.Margin.Bottom);
		}

		// Content Padding
		FMargin Padding = Border->GetPadding();
		if (Padding.Left != 0.0f || Padding.Top != 0.0f || Padding.Right != 0.0f || Padding.Bottom != 0.0f)
		{
			Output += FString::Printf(TEXT("%sContentPadding: (%.1f, %.1f, %.1f, %.1f)\n"), *Indent,
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
		}
	}

	// UTextBlock - Font
	if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
	{
		FSlateFontInfo Font = TextBlock->GetFont();

		// Font Object
		if (Font.FontObject)
		{
			Output += FString::Printf(TEXT("%sFont.Object: %s\n"), *Indent, *Font.FontObject->GetPathName());
		}

		// Font Material
		if (Font.FontMaterial)
		{
			Output += FString::Printf(TEXT("%sFont.Material: %s\n"), *Indent, *Font.FontMaterial->GetPathName());
		}

		// Typeface
		if (!Font.TypefaceFontName.IsNone())
		{
			Output += FString::Printf(TEXT("%sFont.Typeface: %s\n"), *Indent, *Font.TypefaceFontName.ToString());
		}

		// Size (if not default)
		if (Font.Size != 24.0f)
		{
			Output += FString::Printf(TEXT("%sFont.Size: %.0f\n"), *Indent, Font.Size);
		}

		// Letter Spacing
		if (Font.LetterSpacing != 0)
		{
			Output += FString::Printf(TEXT("%sFont.LetterSpacing: %d\n"), *Indent, Font.LetterSpacing);
		}

		// Note: Justification is protected in UTextLayoutWidget, cannot access

		// Text
		FText Text = TextBlock->GetText();
		if (!Text.IsEmpty())
		{
			Output += FString::Printf(TEXT("%sText: \"%s\"\n"), *Indent, *Text.ToString());
		}
	}

	// UCommonTextBlock - similar to TextBlock but from CommonUI
	if (UCommonTextBlock* CommonTextBlock = Cast<UCommonTextBlock>(Widget))
	{
		// TextTransformPolicy
		ETextTransformPolicy TransformPolicy = CommonTextBlock->GetTextTransformPolicy();
		if (TransformPolicy != ETextTransformPolicy::None)
		{
			Output += FString::Printf(TEXT("%sTextTransformPolicy: %s\n"), *Indent, *UEnum::GetValueAsString(TransformPolicy));
		}
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::ExportSlotProperties(UWidget* Widget, int32 IndentLevel)
{
	if (!Widget)
	{
		return TEXT("");
	}

	UPanelSlot* Slot = Widget->Slot;
	if (!Slot)
	{
		return TEXT("");
	}

	FString Output;
	FString Indent = GetIndent(IndentLevel);

	// Canvas slot
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		auto Anchors = CanvasSlot->GetAnchors();
		auto Offsets = CanvasSlot->GetOffsets();

		// Only show non-default anchors
		if (Anchors.Minimum != FVector2D(0.f, 0.f) || Anchors.Maximum != FVector2D(0.f, 0.f))
		{
			Output += FString::Printf(TEXT("%sAnchors: Min(%.2f, %.2f) Max(%.2f, %.2f)\n"),
				*Indent, Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y);
		}

		// Offsets (position/size)
		if (Offsets.Left != 0 || Offsets.Top != 0 || Offsets.Right != 0 || Offsets.Bottom != 0)
		{
			Output += FString::Printf(TEXT("%sOffsets: L=%.1f T=%.1f R=%.1f B=%.1f\n"),
				*Indent, Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom);
		}

		// Alignment
		FVector2D Alignment = CanvasSlot->GetAlignment();
		if (Alignment != FVector2D(0.f, 0.f))
		{
			Output += FString::Printf(TEXT("%sAlignment: (%.2f, %.2f)\n"),
				*Indent, Alignment.X, Alignment.Y);
		}

		// Z-Order
		int32 ZOrder = CanvasSlot->GetZOrder();
		if (ZOrder != 0)
		{
			Output += FString::Printf(TEXT("%sZOrder: %d\n"), *Indent, ZOrder);
		}
	}
	// Horizontal box slot
	else if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		auto Size = HBoxSlot->GetSize();
		if (Size.SizeRule != ESlateSizeRule::Automatic)
		{
			Output += FString::Printf(TEXT("%sSize: %s (%.2f)\n"), *Indent,
				Size.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"), Size.Value);
		}

		EHorizontalAlignment HAlign = HBoxSlot->GetHorizontalAlignment();
		EVerticalAlignment VAlign = HBoxSlot->GetVerticalAlignment();
		if (HAlign != EHorizontalAlignment::HAlign_Fill || VAlign != EVerticalAlignment::VAlign_Fill)
		{
			Output += FString::Printf(TEXT("%sAlign: H=%s V=%s\n"), *Indent,
				*UEnum::GetValueAsString(HAlign), *UEnum::GetValueAsString(VAlign));
		}

		// Padding
		FMargin Padding = HBoxSlot->GetPadding();
		if (Padding.Left != 0.0f || Padding.Top != 0.0f || Padding.Right != 0.0f || Padding.Bottom != 0.0f)
		{
			Output += FString::Printf(TEXT("%sSlotPadding: (%.1f, %.1f, %.1f, %.1f)\n"), *Indent,
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
		}
	}
	// Vertical box slot
	else if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		auto Size = VBoxSlot->GetSize();
		if (Size.SizeRule != ESlateSizeRule::Automatic)
		{
			Output += FString::Printf(TEXT("%sSize: %s (%.2f)\n"), *Indent,
				Size.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"), Size.Value);
		}

		EHorizontalAlignment HAlign = VBoxSlot->GetHorizontalAlignment();
		EVerticalAlignment VAlign = VBoxSlot->GetVerticalAlignment();
		if (HAlign != EHorizontalAlignment::HAlign_Fill || VAlign != EVerticalAlignment::VAlign_Fill)
		{
			Output += FString::Printf(TEXT("%sAlign: H=%s V=%s\n"), *Indent,
				*UEnum::GetValueAsString(HAlign), *UEnum::GetValueAsString(VAlign));
		}

		// Padding
		FMargin Padding = VBoxSlot->GetPadding();
		if (Padding.Left != 0.0f || Padding.Top != 0.0f || Padding.Right != 0.0f || Padding.Bottom != 0.0f)
		{
			Output += FString::Printf(TEXT("%sSlotPadding: (%.1f, %.1f, %.1f, %.1f)\n"), *Indent,
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
		}
	}
	// Overlay slot
	else if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Slot))
	{
		EHorizontalAlignment HAlign = OverlaySlot->GetHorizontalAlignment();
		EVerticalAlignment VAlign = OverlaySlot->GetVerticalAlignment();
		if (HAlign != EHorizontalAlignment::HAlign_Fill || VAlign != EVerticalAlignment::VAlign_Fill)
		{
			Output += FString::Printf(TEXT("%sAlign: H=%s V=%s\n"), *Indent,
				*UEnum::GetValueAsString(HAlign), *UEnum::GetValueAsString(VAlign));
		}

		// Padding
		FMargin Padding = OverlaySlot->GetPadding();
		if (Padding.Left != 0.0f || Padding.Top != 0.0f || Padding.Right != 0.0f || Padding.Bottom != 0.0f)
		{
			Output += FString::Printf(TEXT("%sSlotPadding: (%.1f, %.1f, %.1f, %.1f)\n"), *Indent,
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
		}
	}
	// Widget Switcher slot
	else if (UWidgetSwitcherSlot* SwitcherSlot = Cast<UWidgetSwitcherSlot>(Slot))
	{
		EHorizontalAlignment HAlign = SwitcherSlot->GetHorizontalAlignment();
		EVerticalAlignment VAlign = SwitcherSlot->GetVerticalAlignment();
		if (HAlign != EHorizontalAlignment::HAlign_Fill || VAlign != EVerticalAlignment::VAlign_Fill)
		{
			Output += FString::Printf(TEXT("%sAlign: H=%s V=%s\n"), *Indent,
				*UEnum::GetValueAsString(HAlign), *UEnum::GetValueAsString(VAlign));
		}

		// Padding
		FMargin Padding = SwitcherSlot->GetPadding();
		if (Padding.Left != 0.0f || Padding.Top != 0.0f || Padding.Right != 0.0f || Padding.Bottom != 0.0f)
		{
			Output += FString::Printf(TEXT("%sSlotPadding: (%.1f, %.1f, %.1f, %.1f)\n"), *Indent,
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
		}
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::GetWidgetTypeName(UWidget* Widget) const
{
	if (!Widget)
	{
		return TEXT("Unknown");
	}

	FString TypeName = Widget->GetClass()->GetName();

	// Remove common prefixes for cleaner output
	TypeName.RemoveFromStart(TEXT("U"));

	return TypeName;
}

//==========================================================================
// Reflection-Based Property Export
//==========================================================================

FString UAIWidgetBlueprintExporter::ExportWidgetPropertiesViaReflection(UWidget* Widget, int32 IndentLevel, bool bFilterDefaults)
{
	if (!Widget)
	{
		return TEXT("");
	}

	FString Output;
	FString Indent = GetIndent(IndentLevel);

	// Get archetype for default value comparison
	UObject* Archetype = bFilterDefaults ? Widget->GetArchetype() : nullptr;

	// Ensure archetype is of compatible class
	if (Archetype && !Archetype->GetClass()->IsChildOf(Widget->GetClass()) && !Widget->GetClass()->IsChildOf(Archetype->GetClass()))
	{
		Archetype = nullptr;
	}

	// Iterate over ALL UPROPERTY fields using reflection
	for (TFieldIterator<FProperty> PropIt(Widget->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		FName PropertyName = Property->GetFName();

		// Skip transient/deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
		{
			continue;
		}

		// Skip widget-specific internal properties
		if (ShouldSkipWidgetProperty(Property, PropertyName))
		{
			continue;
		}

		// Compare with archetype to filter default values
		if (bFilterDefaults && Archetype)
		{
			if (Archetype->GetClass()->FindPropertyByName(PropertyName))
			{
				if (Property->Identical_InContainer(Widget, Archetype))
				{
					continue;
				}
			}
		}

		// Export property value using reflection
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Widget);
		FString ValueStr;
		Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Widget, PPF_None);

		// Filter empty/None values
		if (ValueStr.IsEmpty() || ValueStr == TEXT("None") || ValueStr == TEXT("()"))
		{
			continue;
		}

		// Truncate very long values for readability
		if (ValueStr.Len() > 400)
		{
			ValueStr = ValueStr.Left(400) + TEXT("...(truncated)");
		}

		Output += FString::Printf(TEXT("%s%s=%s\n"), *Indent, *PropertyName.ToString(), *ValueStr);
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::ExportSlotPropertiesViaReflection(UPanelSlot* Slot, int32 IndentLevel, bool bFilterDefaults)
{
	if (!Slot)
	{
		return TEXT("");
	}

	FString Output;
	FString Indent = GetIndent(IndentLevel);

	// Get archetype for default value comparison
	UObject* Archetype = bFilterDefaults ? Slot->GetArchetype() : nullptr;

	// Ensure archetype is of compatible class
	if (Archetype && !Archetype->GetClass()->IsChildOf(Slot->GetClass()) && !Slot->GetClass()->IsChildOf(Archetype->GetClass()))
	{
		Archetype = nullptr;
	}

	// Iterate over ALL slot UPROPERTY fields using reflection
	for (TFieldIterator<FProperty> PropIt(Slot->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		FName PropertyName = Property->GetFName();

		// Skip transient/deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
		{
			continue;
		}

		// Skip slot-specific internal properties
		if (ShouldSkipSlotProperty(Property, PropertyName))
		{
			continue;
		}

		// Compare with archetype to filter default values
		if (bFilterDefaults && Archetype)
		{
			if (Archetype->GetClass()->FindPropertyByName(PropertyName))
			{
				if (Property->Identical_InContainer(Slot, Archetype))
				{
					continue;
				}
			}
		}

		// Export property value using reflection
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Slot);
		FString ValueStr;
		Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Slot, PPF_None);

		// Filter empty/None values
		if (ValueStr.IsEmpty() || ValueStr == TEXT("None") || ValueStr == TEXT("()"))
		{
			continue;
		}

		// Truncate very long values
		if (ValueStr.Len() > 400)
		{
			ValueStr = ValueStr.Left(400) + TEXT("...(truncated)");
		}

		// Prefix with "Slot." for clarity
		Output += FString::Printf(TEXT("%sSlot.%s=%s\n"), *Indent, *PropertyName.ToString(), *ValueStr);
	}

	return Output;
}

bool UAIWidgetBlueprintExporter::ShouldSkipWidgetProperty(FProperty* Property, const FName& PropertyName) const
{
	if (!Property)
	{
		return true;
	}

	// Skip internal UWidget/UObject properties that aren't useful for export
	static TSet<FName> SkipProperties = {
		// UObject internals
		TEXT("NativeClass"),
		TEXT("ObjectArchetype"),
		TEXT("ObjectFlags"),
		TEXT("ExternalPackage"),

		// UWidget internals - already handled separately or redundant
		TEXT("Slot"),                    // Handled separately with slot-specific export
		TEXT("bIsVariable"),             // Already handled in ExportWidgetProperties
		TEXT("ToolTipText"),             // Already handled in ExportWidgetProperties
		TEXT("Visibility"),              // Already handled in ExportWidgetProperties
		TEXT("RenderOpacity"),           // Already handled in ExportWidgetProperties
		TEXT("bIsEnabled"),              // Usually default
		TEXT("bOverride_Cursor"),        // Rarely needed
		TEXT("Cursor"),                  // Rarely needed
		TEXT("Clipping"),                // Usually default
		TEXT("bIsVolatile"),             // Performance hint
		TEXT("Navigation"),              // Complex object, rarely needed
		TEXT("FlowDirectionPreference"), // Usually default
		TEXT("bCreatedByConstructionScript"), // Internal
		TEXT("AccessibleBehavior"),      // Accessibility
		TEXT("AccessibleSummaryBehavior"), // Accessibility
		TEXT("AccessibleText"),          // Accessibility
		TEXT("AccessibleSummaryText"),   // Accessibility
	};

	return SkipProperties.Contains(PropertyName);
}

bool UAIWidgetBlueprintExporter::ShouldSkipSlotProperty(FProperty* Property, const FName& PropertyName) const
{
	if (!Property)
	{
		return true;
	}

	// Skip internal slot properties
	static TSet<FName> SkipProperties = {
		// UObject internals
		TEXT("NativeClass"),
		TEXT("ObjectArchetype"),
		TEXT("ObjectFlags"),

		// UPanelSlot internals
		TEXT("Parent"),  // Reference to parent panel
		TEXT("Content"), // Reference to contained widget
	};

	return SkipProperties.Contains(PropertyName);
}

//==========================================================================
// Widget Animation Export
//==========================================================================

FString UAIWidgetBlueprintExporter::ExportAnimations(UWidgetBlueprint* WidgetBP, bool bFilterDefaults)
{
	if (!WidgetBP || WidgetBP->Animations.Num() == 0)
	{
		return TEXT("");
	}

	FString Output;
	Output += MakeSectionHeader(TEXT("WIDGET ANIMATIONS"));

	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (!Anim)
		{
			continue;
		}

		// Animation header with name
		Output += FString::Printf(TEXT("[%s]\n"), *Anim->GetName());

		// Duration info
		float StartTime = Anim->GetStartTime();
		float EndTime = Anim->GetEndTime();
		float Duration = EndTime - StartTime;
		Output += FString::Printf(TEXT("  Duration: %.2fs (%.2f - %.2f)\n"), Duration, StartTime, EndTime);

		// Bound widgets from AnimationBindings
		const TArray<FWidgetAnimationBinding>& Bindings = Anim->GetBindings();
		if (Bindings.Num() > 0)
		{
			Output += TEXT("  Bound Widgets:\n");
			for (const FWidgetAnimationBinding& Binding : Bindings)
			{
				FString WidgetInfo = Binding.WidgetName.ToString();
				if (Binding.bIsRootWidget)
				{
					WidgetInfo += TEXT(" (Root)");
				}
				if (!Binding.SlotWidgetName.IsNone())
				{
					WidgetInfo += FString::Printf(TEXT(" [Slot: %s]"), *Binding.SlotWidgetName.ToString());
				}
				Output += FString::Printf(TEXT("    - %s\n"), *WidgetInfo);
			}
		}

		// Tracks from MovieScene
		if (UMovieScene* MovieScene = Anim->GetMovieScene())
		{
			Output += ExportMovieSceneTracks(MovieScene, bFilterDefaults);
		}

		Output += TEXT("\n");
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::ExportMovieSceneTracks(UMovieScene* MovieScene, bool bFilterDefaults)
{
	if (!MovieScene)
	{
		return TEXT("");
	}

	// For simplified export, use the new widget-grouped format
	if (bFilterDefaults)
	{
		return ExportMovieSceneTracksSimplified(MovieScene);
	}

	// Raw export - detailed format with sections
	FString Output;
	bool bHasTracks = false;

	// Object bindings - these are widget-specific tracks
	const TArray<FMovieSceneBinding>& Bindings = static_cast<const UMovieScene*>(MovieScene)->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		const TArray<UMovieSceneTrack*>& Tracks = Binding.GetTracks();
		if (Tracks.Num() > 0)
		{
			if (!bHasTracks)
			{
				Output += TEXT("  Tracks:\n");
				bHasTracks = true;
			}

			for (UMovieSceneTrack* Track : Tracks)
			{
				if (!Track)
				{
					continue;
				}

				// Get track type name (e.g., "MovieSceneFloatTrack" -> "Float")
				FString TrackType = Track->GetClass()->GetName();
				TrackType.RemoveFromStart(TEXT("MovieScene"));
				TrackType.RemoveFromEnd(TEXT("Track"));

				// Get display name
				FText DisplayName = Track->GetDisplayName();
				FString DisplayNameStr = DisplayName.IsEmpty() ? TEXT("Unnamed") : DisplayName.ToString();

				Output += FString::Printf(TEXT("    [%s] %s\n"), *TrackType, *DisplayNameStr);

				// Export sections for more detail
				const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
				for (UMovieSceneSection* Section : Sections)
				{
					if (!Section)
					{
						continue;
					}

					// Get section range
					TRange<FFrameNumber> SectionRange = Section->GetRange();
					FString RangeStr;
					if (SectionRange.HasLowerBound() && SectionRange.HasUpperBound())
					{
						RangeStr = FString::Printf(TEXT("[%d - %d]"),
							SectionRange.GetLowerBoundValue().Value,
							SectionRange.GetUpperBoundValue().Value);
					}
					else
					{
						RangeStr = TEXT("[Infinite]");
					}

					FString SectionType = Section->GetClass()->GetName();
					SectionType.RemoveFromStart(TEXT("MovieScene"));
					SectionType.RemoveFromEnd(TEXT("Section"));

					Output += FString::Printf(TEXT("      %s %s\n"), *SectionType, *RangeStr);
				}
			}
		}
	}

	// Master tracks (not bound to specific objects)
	const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetTracks();
	if (MasterTracks.Num() > 0)
	{
		Output += TEXT("  Master Tracks:\n");
		for (UMovieSceneTrack* Track : MasterTracks)
		{
			if (!Track)
			{
				continue;
			}

			FString TrackType = Track->GetClass()->GetName();
			TrackType.RemoveFromStart(TEXT("MovieScene"));
			TrackType.RemoveFromEnd(TEXT("Track"));

			FText DisplayName = Track->GetDisplayName();
			FString DisplayNameStr = DisplayName.IsEmpty() ? TEXT("Unnamed") : DisplayName.ToString();

			Output += FString::Printf(TEXT("    [%s] %s\n"), *TrackType, *DisplayNameStr);
		}
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::ExportMovieSceneTracksSimplified(UMovieScene* MovieScene)
{
	if (!MovieScene)
	{
		return TEXT("");
	}

	FString Output;
	FFrameRate FrameRate = MovieScene->GetTickResolution();

	// Group tracks by widget using ObjectGuid -> Possessable mapping
	const TArray<FMovieSceneBinding>& Bindings = static_cast<const UMovieScene*>(MovieScene)->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		// Get widget name from Possessable
		FString WidgetName;
		FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Binding.GetObjectGuid());
		if (Possessable)
		{
			WidgetName = Possessable->GetName();
		}

		const TArray<UMovieSceneTrack*>& Tracks = Binding.GetTracks();
		if (Tracks.Num() > 0 && !WidgetName.IsEmpty())
		{
			Output += FString::Printf(TEXT("  %s:\n"), *WidgetName);

			for (UMovieSceneTrack* Track : Tracks)
			{
				Output += ExportTrackSimplified(Track, FrameRate);
			}
		}
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::ExportTrackSimplified(UMovieSceneTrack* Track, const FFrameRate& FrameRate)
{
	if (!Track)
	{
		return TEXT("");
	}

	FString Output;

	// Get property name from track display name
	FText DisplayName = Track->GetDisplayName();
	FString PropertyName = DisplayName.IsEmpty() ? TEXT("Unknown") : DisplayName.ToString();

	// Process each section
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	for (UMovieSceneSection* Section : Sections)
	{
		if (!Section)
		{
			continue;
		}

		float StartTime = 0.0f;
		float EndTime = 0.0f;
		FString ValueStr = ExtractKeyframeValues(Section, FrameRate, StartTime, EndTime);

		if (!ValueStr.IsEmpty())
		{
			Output += FString::Printf(TEXT("    %s: %s @ %.2f-%.2fs\n"),
				*PropertyName, *ValueStr, StartTime, EndTime);
		}
	}

	return Output;
}

FString UAIWidgetBlueprintExporter::ExtractKeyframeValues(UMovieSceneSection* Section, const FFrameRate& FrameRate, float& OutStartTime, float& OutEndTime)
{
	if (!Section)
	{
		return TEXT("");
	}

	// Float Section (RenderOpacity, etc.)
	if (UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(Section))
	{
		const FMovieSceneFloatChannel& Channel = FloatSection->GetChannel();
		TArrayView<const FFrameNumber> Times = Channel.GetTimes();
		TArrayView<const FMovieSceneFloatValue> Values = Channel.GetValues();

		if (Values.Num() >= 2 && Times.Num() >= 2)
		{
			// Get actual keyframe times
			OutStartTime = FrameRate.AsSeconds(Times[0]);
			OutEndTime = FrameRate.AsSeconds(Times[Times.Num() - 1]);

			float StartVal = Values[0].Value;
			float EndVal = Values[Values.Num() - 1].Value;
			return FString::Printf(TEXT("%.2f→%.2f"), StartVal, EndVal);
		}
		else if (Values.Num() == 1)
		{
			return FString::Printf(TEXT("%.2f (constant)"), Values[0].Value);
		}
	}

	// Color Section (BrushColor, ColorAndOpacity, etc.)
	if (UMovieSceneColorSection* ColorSection = Cast<UMovieSceneColorSection>(Section))
	{
		// Get RGBA channels
		const FMovieSceneFloatChannel& RedChannel = ColorSection->GetRedChannel();
		const FMovieSceneFloatChannel& GreenChannel = ColorSection->GetGreenChannel();
		const FMovieSceneFloatChannel& BlueChannel = ColorSection->GetBlueChannel();
		const FMovieSceneFloatChannel& AlphaChannel = ColorSection->GetAlphaChannel();

		// Check which channels are animated (have different start/end values)
		TArray<FString> AnimatedComponents;

		// Helper to extract times from a channel
		auto GetChannelTimes = [&FrameRate, &OutStartTime, &OutEndTime](const FMovieSceneFloatChannel& Ch)
		{
			TArrayView<const FFrameNumber> Times = Ch.GetTimes();
			if (Times.Num() >= 2)
			{
				OutStartTime = FrameRate.AsSeconds(Times[0]);
				OutEndTime = FrameRate.AsSeconds(Times[Times.Num() - 1]);
			}
		};

		auto CheckChannel = [](const FMovieSceneFloatChannel& Ch, const TCHAR* Name) -> FString
		{
			TArrayView<const FMovieSceneFloatValue> Vals = Ch.GetValues();
			if (Vals.Num() >= 2)
			{
				float Start = Vals[0].Value;
				float End = Vals[Vals.Num() - 1].Value;
				if (!FMath::IsNearlyEqual(Start, End, 0.01f))
				{
					return FString::Printf(TEXT("%s: %.2f→%.2f"), Name, Start, End);
				}
			}
			return TEXT("");
		};

		FString R = CheckChannel(RedChannel, TEXT("R"));
		FString G = CheckChannel(GreenChannel, TEXT("G"));
		FString B = CheckChannel(BlueChannel, TEXT("B"));
		FString A = CheckChannel(AlphaChannel, TEXT("A"));

		if (!R.IsEmpty()) { AnimatedComponents.Add(R); GetChannelTimes(RedChannel); }
		if (!G.IsEmpty()) { AnimatedComponents.Add(G); GetChannelTimes(GreenChannel); }
		if (!B.IsEmpty()) { AnimatedComponents.Add(B); GetChannelTimes(BlueChannel); }
		if (!A.IsEmpty()) { AnimatedComponents.Add(A); GetChannelTimes(AlphaChannel); }

		if (AnimatedComponents.Num() > 0)
		{
			return FString::Join(AnimatedComponents, TEXT(", "));
		}
		return TEXT("(color change)");
	}

	// 2D Transform Section (Translation, Rotation, Scale, Shear)
	if (UMovieScene2DTransformSection* TransformSection = Cast<UMovieScene2DTransformSection>(Section))
	{
		TArray<FString> AnimatedComponents;

		// Helper to extract times from a channel
		auto GetChannelTimes = [&FrameRate, &OutStartTime, &OutEndTime](const FMovieSceneFloatChannel& Ch)
		{
			TArrayView<const FFrameNumber> Times = Ch.GetTimes();
			if (Times.Num() >= 2)
			{
				OutStartTime = FrameRate.AsSeconds(Times[0]);
				OutEndTime = FrameRate.AsSeconds(Times[Times.Num() - 1]);
			}
		};

		// Helper lambda to extract start/end values from a channel
		auto ExtractChannelDelta = [&GetChannelTimes](const FMovieSceneFloatChannel& Ch) -> TOptional<TPair<float, float>>
		{
			TArrayView<const FMovieSceneFloatValue> Vals = Ch.GetValues();
			if (Vals.Num() >= 2)
			{
				float Start = Vals[0].Value;
				float End = Vals[Vals.Num() - 1].Value;
				if (!FMath::IsNearlyEqual(Start, End, 0.01f))
				{
					GetChannelTimes(Ch);
					return TPair<float, float>(Start, End);
				}
			}
			return {};
		};

		// Translation X, Y (stored in Translation[2] array)
		if (auto TransX = ExtractChannelDelta(TransformSection->Translation[0]))
		{
			AnimatedComponents.Add(FString::Printf(TEXT("X: %.1f→%.1f"), TransX->Key, TransX->Value));
		}
		if (auto TransY = ExtractChannelDelta(TransformSection->Translation[1]))
		{
			AnimatedComponents.Add(FString::Printf(TEXT("Y: %.1f→%.1f"), TransY->Key, TransY->Value));
		}

		// Rotation (single channel)
		if (auto Rot = ExtractChannelDelta(TransformSection->Rotation))
		{
			AnimatedComponents.Add(FString::Printf(TEXT("Rot: %.1f→%.1f°"), Rot->Key, Rot->Value));
		}

		// Scale X, Y (stored in Scale[2] array)
		if (auto ScaleX = ExtractChannelDelta(TransformSection->Scale[0]))
		{
			AnimatedComponents.Add(FString::Printf(TEXT("ScaleX: %.2f→%.2f"), ScaleX->Key, ScaleX->Value));
		}
		if (auto ScaleY = ExtractChannelDelta(TransformSection->Scale[1]))
		{
			AnimatedComponents.Add(FString::Printf(TEXT("ScaleY: %.2f→%.2f"), ScaleY->Key, ScaleY->Value));
		}

		// Shear X, Y (stored in Shear[2] array)
		if (auto ShearX = ExtractChannelDelta(TransformSection->Shear[0]))
		{
			AnimatedComponents.Add(FString::Printf(TEXT("ShearX: %.2f→%.2f"), ShearX->Key, ShearX->Value));
		}
		if (auto ShearY = ExtractChannelDelta(TransformSection->Shear[1]))
		{
			AnimatedComponents.Add(FString::Printf(TEXT("ShearY: %.2f→%.2f"), ShearY->Key, ShearY->Value));
		}

		if (AnimatedComponents.Num() > 0)
		{
			return FString::Join(AnimatedComponents, TEXT(", "));
		}
		return TEXT("(transform change)");
	}

	return TEXT("");
}
