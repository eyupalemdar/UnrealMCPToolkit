// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Curves/RichCurve.h"
#include "AIWidgetAnimationBuilder.generated.h"

class UWidgetBlueprint;
class UWidgetAnimation;
class UMovieScene;
class UMovieSceneTrack;
class UMovieSceneSection;
class FJsonObject;

/**
 * Static utility class for Widget Animation creation and manipulation.
 *
 * Provides programmatic creation of UWidgetAnimation objects:
 * - CreateAnimation: Create a new animation on a Widget Blueprint
 * - BindWidget: Bind a widget to an animation (movie scene possessable)
 * - AddTrack: Add a property track (float, color, transform2d)
 * - AddKeyframe: Add keyframes to a track
 *
 * Supported track types:
 * - float: RenderOpacity, any float property
 * - color: ColorAndOpacity, BrushColor, TintColor (FLinearColor)
 * - transform2d: RenderTransform (FWidgetTransform)
 *
 * All functions are static. Call from Game Thread only.
 */
UCLASS()
class COMMONAIEXPORT_API UAIWidgetAnimationBuilder : public UObject
{
	GENERATED_BODY()

public:
	// =========================================================================
	// ANIMATION LIFECYCLE
	// =========================================================================

	/**
	 * Create a new widget animation on a Widget Blueprint.
	 * @param WidgetBP Target Widget Blueprint
	 * @param AnimationName Name for the animation (e.g. "Selected", "Hovered", "FadeIn")
	 * @param LengthSeconds Animation duration in seconds (default 1.0)
	 * @return The created UWidgetAnimation, or existing one if name matches
	 */
	static UWidgetAnimation* CreateAnimation(
		UWidgetBlueprint* WidgetBP,
		const FString& AnimationName,
		float LengthSeconds = 1.0f);

	// =========================================================================
	// WIDGET BINDING
	// =========================================================================

	/**
	 * Bind a widget to an animation.
	 * Creates a MovieScene possessable + FWidgetAnimationBinding entry.
	 * Must be called before adding tracks for that widget.
	 * @param WidgetBP Target Widget Blueprint
	 * @param AnimationName Name of the animation
	 * @param WidgetName Name of the widget to bind (must exist in widget tree)
	 * @return true if binding succeeded
	 */
	static bool BindWidget(
		UWidgetBlueprint* WidgetBP,
		const FString& AnimationName,
		const FString& WidgetName);

	// =========================================================================
	// TRACK MANAGEMENT
	// =========================================================================

	/**
	 * Add a property track to an animation for a bound widget.
	 * @param WidgetBP Target Widget Blueprint
	 * @param AnimationName Name of the animation
	 * @param WidgetName Name of the bound widget
	 * @param PropertyType Track type: "float", "color", "transform2d"
	 * @param PropertyPath UE property path (e.g. "RenderOpacity", "ColorAndOpacity")
	 * @return true if track was created
	 */
	static bool AddTrack(
		UWidgetBlueprint* WidgetBP,
		const FString& AnimationName,
		const FString& WidgetName,
		const FString& PropertyType,
		const FString& PropertyPath);

	// =========================================================================
	// KEYFRAME MANAGEMENT
	// =========================================================================

	/**
	 * Add a keyframe to an animation track.
	 * @param WidgetBP Target Widget Blueprint
	 * @param AnimationName Name of the animation
	 * @param WidgetName Name of the bound widget
	 * @param PropertyPath Property path that identifies the track
	 * @param TimeSeconds Keyframe time in seconds
	 * @param Value Keyframe value:
	 *              float: "0.5"
	 *              color: "(R=1.0,G=0.5,B=0.0,A=1.0)"
	 * @param Interpolation "Linear", "Cubic", "Constant" (default: "Cubic")
	 * @return true if keyframe was added
	 */
	static bool AddKeyframe(
		UWidgetBlueprint* WidgetBP,
		const FString& AnimationName,
		const FString& WidgetName,
		const FString& PropertyPath,
		float TimeSeconds,
		const FString& Value,
		const FString& Interpolation = TEXT("Cubic"));

	// =========================================================================
	// UTILITY
	// =========================================================================

	/** Find an animation by name in a Widget Blueprint. */
	static UWidgetAnimation* FindAnimation(
		UWidgetBlueprint* WidgetBP,
		const FString& AnimationName);

	/** Get all animations as JSON for verification. */
	static TSharedPtr<FJsonObject> GetAnimationsAsJson(UWidgetBlueprint* WidgetBP);

	/** Load a Widget Blueprint by asset path. */
	static UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath);

private:
	/** Find the MovieScene binding GUID for a widget in an animation. */
	static FGuid FindWidgetBindingGuid(
		UWidgetAnimation* Animation,
		const FString& WidgetName);

	/** Find a property track by property path within a binding. */
	static UMovieSceneTrack* FindTrackByPropertyPath(
		UMovieScene* MovieScene,
		const FGuid& BindingGuid,
		const FString& PropertyPath);

	/** Convert seconds to frame number using movie scene tick resolution. */
	static FFrameNumber SecondsToFrame(UMovieScene* MovieScene, float Seconds);

	/** Parse interpolation mode string. */
	static ERichCurveInterpMode ParseInterpMode(const FString& Interpolation);

	/** Add float keyframe to a section's first float channel. */
	static bool AddFloatKeyToSection(
		UMovieSceneSection* Section,
		FFrameNumber Frame,
		float Value,
		ERichCurveInterpMode InterpMode);

	/** Add color keyframe (RGBA) to a color section's 4 channels. */
	static bool AddColorKeyToSection(
		UMovieSceneSection* Section,
		FFrameNumber Frame,
		const FLinearColor& Color,
		ERichCurveInterpMode InterpMode);

	/** Add FWidgetAnimationBinding to an animation (accesses private array via reflection). */
	static bool AddAnimationBinding(
		UWidgetAnimation* Animation,
		const FName& WidgetName,
		const FGuid& BindingGuid,
		bool bIsRootWidget = false);
};
