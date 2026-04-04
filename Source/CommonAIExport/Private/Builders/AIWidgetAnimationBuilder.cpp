// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/AIWidgetAnimationBuilder.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneColorSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/PropertyAccessUtil.h"

DEFINE_LOG_CATEGORY_STATIC(LogAIWidgetAnimation, Log, All);

// =============================================================================
// CreateAnimation
// =============================================================================

UWidgetAnimation* UAIWidgetAnimationBuilder::CreateAnimation(
	UWidgetBlueprint* WidgetBP,
	const FString& AnimationName,
	float LengthSeconds)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("CreateAnimation: WidgetBP is null"));
		return nullptr;
	}

	// Check if animation already exists
	UWidgetAnimation* Existing = FindAnimation(WidgetBP, AnimationName);
	if (Existing)
	{
		UE_LOG(LogAIWidgetAnimation, Warning, TEXT("CreateAnimation: Animation '%s' already exists, returning existing"), *AnimationName);
		return Existing;
	}

	// Create animation object (Outer = WidgetBP so it's properly owned)
	UWidgetAnimation* NewAnim = NewObject<UWidgetAnimation>(WidgetBP, FName(*AnimationName));

	// Create movie scene (Outer = animation)
	UMovieScene* MovieScene = NewObject<UMovieScene>(NewAnim, FName(*(AnimationName + TEXT("_MovieScene"))));

	// Set MovieScene on the animation via reflection (it may be private in some UE versions)
	FObjectPropertyBase* MovieSceneProp = CastField<FObjectPropertyBase>(
		UWidgetAnimation::StaticClass()->FindPropertyByName(TEXT("MovieScene")));
	if (MovieSceneProp)
	{
		MovieSceneProp->SetObjectPropertyValue(
			MovieSceneProp->ContainerPtrToValuePtr<void>(NewAnim), MovieScene);
	}
	else
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("CreateAnimation: Could not find MovieScene property on UWidgetAnimation"));
		return nullptr;
	}

	// Set playback range
	FFrameRate TickRes = MovieScene->GetTickResolution();
	int32 EndFrameInt = FMath::RoundToInt32(LengthSeconds * TickRes.AsDecimal());
	MovieScene->SetPlaybackRange(
		TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(EndFrameInt + 1)),
		false);

	// Add to widget blueprint
	WidgetBP->Animations.Add(NewAnim);
	WidgetBP->MarkPackageDirty();

	UE_LOG(LogAIWidgetAnimation, Log, TEXT("CreateAnimation: Created '%s' (%.2fs, %d ticks)"),
		*AnimationName, LengthSeconds, EndFrameInt);

	return NewAnim;
}

// =============================================================================
// BindWidget
// =============================================================================

bool UAIWidgetAnimationBuilder::BindWidget(
	UWidgetBlueprint* WidgetBP,
	const FString& AnimationName,
	const FString& WidgetName)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("BindWidget: WidgetBP is null"));
		return false;
	}

	UWidgetAnimation* Animation = FindAnimation(WidgetBP, AnimationName);
	if (!Animation)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("BindWidget: Animation '%s' not found"), *AnimationName);
		return false;
	}

	UMovieScene* MovieScene = Animation->GetMovieScene();
	if (!MovieScene)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("BindWidget: Animation '%s' has no MovieScene"), *AnimationName);
		return false;
	}

	// Check if widget exists in the widget tree
	UWidget* FoundWidget = nullptr;
	if (WidgetBP->WidgetTree)
	{
		FoundWidget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	}
	if (!FoundWidget)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("BindWidget: Widget '%s' not found in widget tree"), *WidgetName);
		return false;
	}

	// Check if already bound
	FGuid ExistingGuid = FindWidgetBindingGuid(Animation, WidgetName);
	if (ExistingGuid.IsValid())
	{
		UE_LOG(LogAIWidgetAnimation, Warning, TEXT("BindWidget: Widget '%s' already bound to animation '%s'"),
			*WidgetName, *AnimationName);
		return true;
	}

	// Create a new possessable in the movie scene (simple overload creates both possessable + binding)
	FGuid NewGuid = MovieScene->AddPossessable(WidgetName, FoundWidget->GetClass());

	// Determine if this is the root widget
	bool bIsRoot = (FoundWidget == WidgetBP->WidgetTree->RootWidget);

	// Add FWidgetAnimationBinding to animation
	if (!AddAnimationBinding(Animation, FName(*WidgetName), NewGuid, bIsRoot))
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("BindWidget: Failed to add animation binding for '%s'"), *WidgetName);
		return false;
	}

	WidgetBP->MarkPackageDirty();

	UE_LOG(LogAIWidgetAnimation, Log, TEXT("BindWidget: Bound '%s' to animation '%s' (GUID: %s, Root: %s)"),
		*WidgetName, *AnimationName, *NewGuid.ToString(), bIsRoot ? TEXT("Yes") : TEXT("No"));

	return true;
}

// =============================================================================
// AddTrack
// =============================================================================

bool UAIWidgetAnimationBuilder::AddTrack(
	UWidgetBlueprint* WidgetBP,
	const FString& AnimationName,
	const FString& WidgetName,
	const FString& PropertyType,
	const FString& PropertyPath)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddTrack: WidgetBP is null"));
		return false;
	}

	UWidgetAnimation* Animation = FindAnimation(WidgetBP, AnimationName);
	if (!Animation)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddTrack: Animation '%s' not found"), *AnimationName);
		return false;
	}

	UMovieScene* MovieScene = Animation->GetMovieScene();
	if (!MovieScene)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddTrack: No MovieScene"));
		return false;
	}

	// Find the binding GUID for this widget
	FGuid BindingGuid = FindWidgetBindingGuid(Animation, WidgetName);
	if (!BindingGuid.IsValid())
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddTrack: Widget '%s' not bound to animation '%s'. Call bind_animation_widget first."),
			*WidgetName, *AnimationName);
		return false;
	}

	// Check if track already exists
	UMovieSceneTrack* ExistingTrack = FindTrackByPropertyPath(MovieScene, BindingGuid, PropertyPath);
	if (ExistingTrack)
	{
		UE_LOG(LogAIWidgetAnimation, Warning, TEXT("AddTrack: Track for '%s' already exists on widget '%s'"),
			*PropertyPath, *WidgetName);
		return true;
	}

	// Extract leaf property name from path
	FString LeafPropertyName = PropertyPath;
	int32 DotIndex;
	if (PropertyPath.FindLastChar('.', DotIndex))
	{
		LeafPropertyName = PropertyPath.Mid(DotIndex + 1);
	}

	// Create appropriate track type
	FString TypeLower = PropertyType.ToLower();
	UMovieSceneTrack* NewTrack = nullptr;

	if (TypeLower == TEXT("float"))
	{
		UMovieSceneFloatTrack* FloatTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
		if (FloatTrack)
		{
			FloatTrack->SetPropertyNameAndPath(FName(*LeafPropertyName), PropertyPath);
			NewTrack = FloatTrack;
		}
	}
	else if (TypeLower == TEXT("color"))
	{
		UMovieSceneColorTrack* ColorTrack = MovieScene->AddTrack<UMovieSceneColorTrack>(BindingGuid);
		if (ColorTrack)
		{
			ColorTrack->SetPropertyNameAndPath(FName(*LeafPropertyName), PropertyPath);
			NewTrack = ColorTrack;
		}
	}
	else if (TypeLower == TEXT("transform2d"))
	{
		UMovieScene2DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene2DTransformTrack>(BindingGuid);
		if (TransformTrack)
		{
			TransformTrack->SetPropertyNameAndPath(FName(*LeafPropertyName), PropertyPath);
			NewTrack = TransformTrack;
		}
	}
	else
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddTrack: Unknown property type '%s'. Use 'float', 'color', or 'transform2d'."),
			*PropertyType);
		return false;
	}

	if (!NewTrack)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddTrack: Failed to create track for '%s'"), *PropertyPath);
		return false;
	}

	// Create a default section covering the full animation range
	UMovieSceneSection* Section = NewTrack->CreateNewSection();
	if (Section)
	{
		// Use infinite range so keyframes can be placed anywhere
		Section->SetRange(TRange<FFrameNumber>::All());
		NewTrack->AddSection(*Section);
	}

	WidgetBP->MarkPackageDirty();

	UE_LOG(LogAIWidgetAnimation, Log, TEXT("AddTrack: Created %s track for '%s' on widget '%s' in animation '%s'"),
		*PropertyType, *PropertyPath, *WidgetName, *AnimationName);

	return true;
}

// =============================================================================
// AddKeyframe
// =============================================================================

bool UAIWidgetAnimationBuilder::AddKeyframe(
	UWidgetBlueprint* WidgetBP,
	const FString& AnimationName,
	const FString& WidgetName,
	const FString& PropertyPath,
	float TimeSeconds,
	const FString& Value,
	const FString& Interpolation)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: WidgetBP is null"));
		return false;
	}

	UWidgetAnimation* Animation = FindAnimation(WidgetBP, AnimationName);
	if (!Animation)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: Animation '%s' not found"), *AnimationName);
		return false;
	}

	UMovieScene* MovieScene = Animation->GetMovieScene();
	if (!MovieScene)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: No MovieScene"));
		return false;
	}

	// Find binding GUID
	FGuid BindingGuid = FindWidgetBindingGuid(Animation, WidgetName);
	if (!BindingGuid.IsValid())
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: Widget '%s' not bound"), *WidgetName);
		return false;
	}

	// Find the track
	UMovieSceneTrack* Track = FindTrackByPropertyPath(MovieScene, BindingGuid, PropertyPath);
	if (!Track)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: No track found for property '%s' on widget '%s'. Call add_animation_track first."),
			*PropertyPath, *WidgetName);
		return false;
	}

	// Get the first section (we always create one when adding a track)
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (Sections.Num() == 0)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: Track has no sections"));
		return false;
	}

	UMovieSceneSection* Section = Sections[0];
	FFrameNumber Frame = SecondsToFrame(MovieScene, TimeSeconds);
	ERichCurveInterpMode InterpMode = ParseInterpMode(Interpolation);

	// Determine track type and add keyframe accordingly
	if (Cast<UMovieSceneFloatTrack>(Track))
	{
		float FloatValue = FCString::Atof(*Value);
		if (!AddFloatKeyToSection(Section, Frame, FloatValue, InterpMode))
		{
			return false;
		}
	}
	else if (Cast<UMovieSceneColorTrack>(Track))
	{
		FLinearColor Color;
		if (!Color.InitFromString(Value))
		{
			UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: Failed to parse color value '%s'. Use format: (R=1.0,G=0.5,B=0.0,A=1.0)"),
				*Value);
			return false;
		}
		if (!AddColorKeyToSection(Section, Frame, Color, InterpMode))
		{
			return false;
		}
	}
	else if (Cast<UMovieScene2DTransformTrack>(Track))
	{
		// Transform2D: parse as comma-separated values: "TransX,TransY,Rotation,ScaleX,ScaleY,ShearX,ShearY"
		// Or as a struct: "(Translation=(X=0,Y=0),Scale=(X=1,Y=1),Shear=(X=0,Y=0),Angle=0)"
		// For simplicity, handle the 7-value CSV format
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","));

		FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
		TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();

		// Transform2D has 7 channels: TransX, TransY, ScaleX, ScaleY, ShearX, ShearY, Angle
		if (Parts.Num() >= 7 && Channels.Num() >= 7)
		{
			for (int32 i = 0; i < 7 && i < Parts.Num(); ++i)
			{
				float Val = FCString::Atof(*Parts[i].TrimStartAndEnd());
				switch (InterpMode)
				{
				case RCIM_Linear:
					Channels[i]->AddLinearKey(Frame, Val);
					break;
				case RCIM_Constant:
					Channels[i]->AddConstantKey(Frame, Val);
					break;
				default:
					Channels[i]->AddCubicKey(Frame, Val);
					break;
				}
			}
		}
		else
		{
			UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: Transform2D requires 7 comma-separated values (TransX,TransY,ScaleX,ScaleY,ShearX,ShearY,Angle). Got %d values, %d channels."),
				Parts.Num(), Channels.Num());
			return false;
		}
	}
	else
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddKeyframe: Unsupported track type for property '%s'"), *PropertyPath);
		return false;
	}

	WidgetBP->MarkPackageDirty();

	UE_LOG(LogAIWidgetAnimation, Log, TEXT("AddKeyframe: Added key at %.3fs for '%s' on '%s' (value: %s, interp: %s)"),
		TimeSeconds, *PropertyPath, *WidgetName, *Value, *Interpolation);

	return true;
}

// =============================================================================
// Utility: FindAnimation
// =============================================================================

UWidgetAnimation* UAIWidgetAnimationBuilder::FindAnimation(
	UWidgetBlueprint* WidgetBP,
	const FString& AnimationName)
{
	if (!WidgetBP)
	{
		return nullptr;
	}

	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (Anim && Anim->GetName() == AnimationName)
		{
			return Anim;
		}
	}

	return nullptr;
}

// =============================================================================
// Utility: GetAnimationsAsJson
// =============================================================================

TSharedPtr<FJsonObject> UAIWidgetAnimationBuilder::GetAnimationsAsJson(UWidgetBlueprint* WidgetBP)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	if (!WidgetBP)
	{
		return Root;
	}

	TArray<TSharedPtr<FJsonValue>> AnimArray;

	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (!Anim)
		{
			continue;
		}

		TSharedPtr<FJsonObject> AnimObj = MakeShared<FJsonObject>();
		AnimObj->SetStringField(TEXT("name"), Anim->GetName());
		AnimObj->SetNumberField(TEXT("start_time"), Anim->GetStartTime());
		AnimObj->SetNumberField(TEXT("end_time"), Anim->GetEndTime());

		// Bound widgets
		const TArray<FWidgetAnimationBinding>& Bindings = Anim->GetBindings();
		TArray<TSharedPtr<FJsonValue>> BindingArray;
		for (const FWidgetAnimationBinding& Binding : Bindings)
		{
			TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();
			BindingObj->SetStringField(TEXT("widget_name"), Binding.WidgetName.ToString());
			BindingObj->SetStringField(TEXT("guid"), Binding.AnimationGuid.ToString());
			BindingObj->SetBoolField(TEXT("is_root"), Binding.bIsRootWidget);
			BindingArray.Add(MakeShared<FJsonValueObject>(BindingObj));
		}
		AnimObj->SetArrayField(TEXT("bindings"), BindingArray);

		// Tracks
		UMovieScene* MovieScene = Anim->GetMovieScene();
		if (MovieScene)
		{
			TArray<TSharedPtr<FJsonValue>> TrackArray;
			const TArray<FMovieSceneBinding>& MSBindings = static_cast<const UMovieScene*>(MovieScene)->GetBindings();
			for (const FMovieSceneBinding& MSBinding : MSBindings)
			{
				// Find widget name for this binding
				FString WidgetName;
				FMovieScenePossessable* Possessable = MovieScene->FindPossessable(MSBinding.GetObjectGuid());
				if (Possessable)
				{
					WidgetName = Possessable->GetName();
				}

				for (UMovieSceneTrack* Track : MSBinding.GetTracks())
				{
					if (!Track) continue;

					TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
					TrackObj->SetStringField(TEXT("widget"), WidgetName);
					TrackObj->SetStringField(TEXT("type"), Track->GetClass()->GetName());
					TrackObj->SetStringField(TEXT("display_name"), Track->GetDisplayName().ToString());

					// Count keyframes
					int32 KeyCount = 0;
					for (UMovieSceneSection* Section : Track->GetAllSections())
					{
						if (!Section) continue;
						FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
						TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();
						for (FMovieSceneFloatChannel* Channel : Channels)
						{
							KeyCount += Channel->GetTimes().Num();
						}
					}
					TrackObj->SetNumberField(TEXT("keyframe_count"), KeyCount);

					TrackArray.Add(MakeShared<FJsonValueObject>(TrackObj));
				}
			}
			AnimObj->SetArrayField(TEXT("tracks"), TrackArray);
		}

		AnimArray.Add(MakeShared<FJsonValueObject>(AnimObj));
	}

	Root->SetArrayField(TEXT("animations"), AnimArray);
	return Root;
}

// =============================================================================
// Utility: LoadWidgetBlueprint
// =============================================================================

UWidgetBlueprint* UAIWidgetAnimationBuilder::LoadWidgetBlueprint(const FString& AssetPath)
{
	return Cast<UWidgetBlueprint>(StaticLoadObject(UWidgetBlueprint::StaticClass(), nullptr, *AssetPath));
}

// =============================================================================
// Private: FindWidgetBindingGuid
// =============================================================================

FGuid UAIWidgetAnimationBuilder::FindWidgetBindingGuid(
	UWidgetAnimation* Animation,
	const FString& WidgetName)
{
	if (!Animation)
	{
		return FGuid();
	}

	const TArray<FWidgetAnimationBinding>& Bindings = Animation->GetBindings();
	for (const FWidgetAnimationBinding& Binding : Bindings)
	{
		if (Binding.WidgetName.ToString() == WidgetName)
		{
			return Binding.AnimationGuid;
		}
	}

	return FGuid();
}

// =============================================================================
// Private: FindTrackByPropertyPath
// =============================================================================

UMovieSceneTrack* UAIWidgetAnimationBuilder::FindTrackByPropertyPath(
	UMovieScene* MovieScene,
	const FGuid& BindingGuid,
	const FString& PropertyPath)
{
	if (!MovieScene)
	{
		return nullptr;
	}

	const TArray<FMovieSceneBinding>& Bindings = static_cast<const UMovieScene*>(MovieScene)->GetBindings();
	for (const FMovieSceneBinding& Binding : Bindings)
	{
		if (Binding.GetObjectGuid() == BindingGuid)
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (!Track) continue;

				// Check if this is a property track with matching path
				FText DisplayName = Track->GetDisplayName();
				FString DisplayStr = DisplayName.ToString();

				// Property tracks store the property path - check display name match
				if (DisplayStr == PropertyPath || DisplayStr.EndsWith(PropertyPath))
				{
					return Track;
				}

				// Also check via the track's property name if available
				// UMovieScenePropertyTrack stores PropertyName and PropertyPath
				FProperty* PropNameField = Track->GetClass()->FindPropertyByName(TEXT("PropertyName"));
				if (PropNameField)
				{
					FName StoredName;
					PropNameField->GetValue_InContainer(Track, &StoredName);

					// Extract leaf name from requested path
					FString LeafName = PropertyPath;
					int32 DotIdx;
					if (PropertyPath.FindLastChar('.', DotIdx))
					{
						LeafName = PropertyPath.Mid(DotIdx + 1);
					}

					if (StoredName.ToString() == LeafName || StoredName.ToString() == PropertyPath)
					{
						return Track;
					}
				}
			}
		}
	}

	return nullptr;
}

// =============================================================================
// Private: SecondsToFrame
// =============================================================================

FFrameNumber UAIWidgetAnimationBuilder::SecondsToFrame(UMovieScene* MovieScene, float Seconds)
{
	if (!MovieScene)
	{
		return FFrameNumber(0);
	}

	FFrameRate TickRes = MovieScene->GetTickResolution();
	int32 FrameInt = FMath::RoundToInt32(Seconds * TickRes.AsDecimal());
	return FFrameNumber(FrameInt);
}

// =============================================================================
// Private: ParseInterpMode
// =============================================================================

ERichCurveInterpMode UAIWidgetAnimationBuilder::ParseInterpMode(const FString& Interpolation)
{
	FString Lower = Interpolation.ToLower();
	if (Lower == TEXT("linear"))
	{
		return RCIM_Linear;
	}
	else if (Lower == TEXT("constant"))
	{
		return RCIM_Constant;
	}
	// Default: Cubic
	return RCIM_Cubic;
}

// =============================================================================
// Private: AddFloatKeyToSection
// =============================================================================

bool UAIWidgetAnimationBuilder::AddFloatKeyToSection(
	UMovieSceneSection* Section,
	FFrameNumber Frame,
	float Value,
	ERichCurveInterpMode InterpMode)
{
	if (!Section)
	{
		return false;
	}

	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();

	if (Channels.Num() == 0)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddFloatKeyToSection: Section has no float channels"));
		return false;
	}

	FMovieSceneFloatChannel* Channel = Channels[0];
	switch (InterpMode)
	{
	case RCIM_Linear:
		Channel->AddLinearKey(Frame, Value);
		break;
	case RCIM_Constant:
		Channel->AddConstantKey(Frame, Value);
		break;
	default:
		Channel->AddCubicKey(Frame, Value);
		break;
	}

	return true;
}

// =============================================================================
// Private: AddColorKeyToSection
// =============================================================================

bool UAIWidgetAnimationBuilder::AddColorKeyToSection(
	UMovieSceneSection* Section,
	FFrameNumber Frame,
	const FLinearColor& Color,
	ERichCurveInterpMode InterpMode)
{
	if (!Section)
	{
		return false;
	}

	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();

	if (Channels.Num() < 4)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddColorKeyToSection: Expected 4 channels (RGBA), got %d"), Channels.Num());
		return false;
	}

	float Values[4] = { Color.R, Color.G, Color.B, Color.A };

	for (int32 i = 0; i < 4; ++i)
	{
		switch (InterpMode)
		{
		case RCIM_Linear:
			Channels[i]->AddLinearKey(Frame, Values[i]);
			break;
		case RCIM_Constant:
			Channels[i]->AddConstantKey(Frame, Values[i]);
			break;
		default:
			Channels[i]->AddCubicKey(Frame, Values[i]);
			break;
		}
	}

	return true;
}

// =============================================================================
// Private: AddAnimationBinding
// =============================================================================

bool UAIWidgetAnimationBuilder::AddAnimationBinding(
	UWidgetAnimation* Animation,
	const FName& WidgetName,
	const FGuid& BindingGuid,
	bool bIsRootWidget)
{
	if (!Animation)
	{
		return false;
	}

	// AnimationBindings is a private UPROPERTY on UWidgetAnimation.
	// Access it via reflection.
	FProperty* Prop = UWidgetAnimation::StaticClass()->FindPropertyByName(TEXT("AnimationBindings"));
	FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
	if (!ArrayProp)
	{
		UE_LOG(LogAIWidgetAnimation, Error, TEXT("AddAnimationBinding: Could not find AnimationBindings property"));
		return false;
	}

	void* ArrayPtr = ArrayProp->ContainerPtrToValuePtr<void>(Animation);
	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayPtr);
	int32 NewIndex = ArrayHelper.AddValue();

	// Get pointer to the new element
	FWidgetAnimationBinding* NewBinding = reinterpret_cast<FWidgetAnimationBinding*>(ArrayHelper.GetRawPtr(NewIndex));
	if (NewBinding)
	{
		NewBinding->WidgetName = WidgetName;
		NewBinding->AnimationGuid = BindingGuid;
		NewBinding->bIsRootWidget = bIsRootWidget;
		return true;
	}

	return false;
}
