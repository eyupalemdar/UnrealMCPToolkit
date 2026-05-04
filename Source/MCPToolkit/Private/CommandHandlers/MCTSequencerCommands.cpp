// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTSequencerCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"

#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTrack.h"

namespace MCPToolkit::CommandHandlers::Sequencer
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName)
{
	FString Value;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
		Value.TrimStartAndEndInline();
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const bool bDefault)
{
	bool bValue = bDefault;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

int32 ReadIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const int32 DefaultValue, const int32 MinValue, const int32 MaxValue)
{
	double NumberValue = static_cast<double>(DefaultValue);
	if (Params.IsValid())
	{
		Params->TryGetNumberField(FieldName, NumberValue);
	}
	return FMath::Clamp(FMath::RoundToInt(NumberValue), MinValue, MaxValue);
}

void SetOptionalFrameField(TSharedPtr<FJsonObject> Data, const TCHAR* FieldName, const TRangeBound<FFrameNumber>& Bound, const FFrameRate& TickResolution)
{
	if (!Data.IsValid())
	{
		return;
	}

	if (Bound.IsOpen())
	{
		Data->SetField(FieldName, MakeShared<FJsonValueNull>());
		Data->SetStringField(FString::Printf(TEXT("%s_bound"), FieldName), TEXT("open"));
		return;
	}

	const int32 FrameValue = Bound.GetValue().Value;
	Data->SetNumberField(FieldName, FrameValue);
	Data->SetNumberField(FString::Printf(TEXT("%s_seconds"), FieldName), TickResolution.AsSeconds(FFrameTime(Bound.GetValue())));
	Data->SetStringField(FString::Printf(TEXT("%s_bound"), FieldName), Bound.IsInclusive() ? TEXT("inclusive") : TEXT("exclusive"));
}

TSharedPtr<FJsonObject> BuildFrameRateJson(const FFrameRate& FrameRate)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("numerator"), FrameRate.Numerator);
	Data->SetNumberField(TEXT("denominator"), FrameRate.Denominator);
	Data->SetNumberField(TEXT("decimal"), FrameRate.AsDecimal());
	return Data;
}

TSharedPtr<FJsonObject> BuildRangeJson(const TRange<FFrameNumber>& Range, const FFrameRate& TickResolution)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	SetOptionalFrameField(Data, TEXT("start_frame"), Range.GetLowerBound(), TickResolution);
	SetOptionalFrameField(Data, TEXT("end_frame"), Range.GetUpperBound(), TickResolution);
	return Data;
}

TSharedPtr<FJsonObject> BuildSectionJson(UMovieSceneSection* Section, const FFrameRate& TickResolution)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Section != nullptr);
	if (!Section)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Section->GetName());
	Data->SetStringField(TEXT("class"), Section->GetClass() ? Section->GetClass()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("class_path"), Section->GetClass() ? Section->GetClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("active"), Section->IsActive());
	Data->SetBoolField(TEXT("locked"), Section->IsLocked());
	Data->SetNumberField(TEXT("row_index"), Section->GetRowIndex());
	Data->SetNumberField(TEXT("pre_roll_frames"), Section->GetPreRollFrames());
	Data->SetNumberField(TEXT("post_roll_frames"), Section->GetPostRollFrames());
	Data->SetObjectField(TEXT("range"), BuildRangeJson(Section->GetRange(), TickResolution));

	if (const UEnum* CompletionEnum = StaticEnum<EMovieSceneCompletionMode>())
	{
		Data->SetStringField(TEXT("completion_mode"), CompletionEnum->GetNameStringByValue(static_cast<int64>(Section->GetCompletionMode())));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildTrackJson(
	UMovieSceneTrack* Track,
	const FFrameRate& TickResolution,
	const bool bIncludeSections,
	const int32 MaxSections,
	int32& InOutSectionsEmitted)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Track != nullptr);
	if (!Track)
	{
		return Data;
	}

	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	Data->SetStringField(TEXT("name"), Track->GetName());
	Data->SetStringField(TEXT("track_name"), Track->GetTrackName().ToString());
	Data->SetStringField(TEXT("display_name"), Track->GetDisplayName().ToString());
	Data->SetStringField(TEXT("class"), Track->GetClass() ? Track->GetClass()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("class_path"), Track->GetClass() ? Track->GetClass()->GetPathName() : TEXT(""));
	Data->SetNumberField(TEXT("section_count"), Sections.Num());

	if (bIncludeSections)
	{
		TArray<TSharedPtr<FJsonValue>> SectionsJson;
		for (UMovieSceneSection* Section : Sections)
		{
			if (InOutSectionsEmitted >= MaxSections)
			{
				break;
			}
			SectionsJson.Add(MakeShared<FJsonValueObject>(BuildSectionJson(Section, TickResolution)));
			++InOutSectionsEmitted;
		}
		Data->SetArrayField(TEXT("sections"), SectionsJson);
		Data->SetBoolField(TEXT("sections_truncated"), SectionsJson.Num() < Sections.Num());
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildBindingMetadataJson(UMovieScene* MovieScene, const FGuid& BindingGuid)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("guid"), BindingGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Data->SetStringField(TEXT("display_name"), MovieScene ? MovieScene->GetObjectDisplayName(BindingGuid).ToString() : TEXT(""));

	if (!MovieScene)
	{
		Data->SetStringField(TEXT("kind"), TEXT("unknown"));
		return Data;
	}

	if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(BindingGuid))
	{
		Data->SetStringField(TEXT("kind"), TEXT("spawnable"));
		Data->SetStringField(TEXT("name"), Spawnable->GetName());
		Data->SetStringField(TEXT("level_name"), Spawnable->GetLevelName().ToString());
		if (const UEnum* OwnershipEnum = StaticEnum<ESpawnOwnership>())
		{
			Data->SetStringField(TEXT("spawn_ownership"), OwnershipEnum->GetNameStringByValue(static_cast<int64>(Spawnable->GetSpawnOwnership())));
		}

		if (const UObject* Template = Spawnable->GetObjectTemplate())
		{
			Data->SetStringField(TEXT("template_name"), Template->GetName());
			Data->SetStringField(TEXT("template_path"), Template->GetPathName());
			Data->SetStringField(TEXT("template_class"), Template->GetClass() ? Template->GetClass()->GetName() : TEXT(""));
			Data->SetStringField(TEXT("template_class_path"), Template->GetClass() ? Template->GetClass()->GetPathName() : TEXT(""));
		}

		TArray<TSharedPtr<FJsonValue>> ChildrenJson;
		for (const FGuid& ChildGuid : Spawnable->GetChildPossessables())
		{
			ChildrenJson.Add(MakeShared<FJsonValueString>(ChildGuid.ToString(EGuidFormats::DigitsWithHyphens)));
		}
		Data->SetArrayField(TEXT("child_possessables"), ChildrenJson);
		return Data;
	}

	if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
	{
		Data->SetStringField(TEXT("kind"), TEXT("possessable"));
		Data->SetStringField(TEXT("name"), Possessable->GetName());
		Data->SetStringField(TEXT("parent_guid"), Possessable->GetParent().IsValid() ? Possessable->GetParent().ToString(EGuidFormats::DigitsWithHyphens) : TEXT(""));
#if WITH_EDITORONLY_DATA
		if (const UClass* PossessedClass = Possessable->GetLoadedPossessedObjectClass())
		{
			Data->SetStringField(TEXT("possessed_class"), PossessedClass->GetName());
			Data->SetStringField(TEXT("possessed_class_path"), PossessedClass->GetPathName());
		}
#endif
		return Data;
	}

	Data->SetStringField(TEXT("kind"), TEXT("unresolved"));
	return Data;
}

FString RunOnGameThread(TFunction<FString()>&& Work, const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, Work = MoveTemp(Work)]()
	{
		Promise->SetValue(Work());
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleSequencerAssetInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const bool bIncludeSections = ReadBoolField(Params, TEXT("include_sections"), true);
	const int32 BindingLimit = ReadIntField(Params, TEXT("binding_limit"), 200, 0, 5000);
	const int32 TrackLimit = ReadIntField(Params, TEXT("track_limit"), 200, 0, 10000);
	const int32 SectionLimit = ReadIntField(Params, TEXT("section_limit"), 500, 0, 20000);

	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	return RunOnGameThread([AssetPath, bIncludeSections, BindingLimit, TrackLimit, SectionLimit]()
	{
		ULevelSequence* LevelSequence = LoadObject<ULevelSequence>(nullptr, *AssetPath);
		if (!LevelSequence)
		{
			return CreateErrorResponse(FString::Printf(TEXT("Could not load LevelSequence: %s"), *AssetPath));
		}

		UMovieScene* MovieScene = LevelSequence->GetMovieScene();
		if (!MovieScene)
		{
			return CreateErrorResponse(FString::Printf(TEXT("LevelSequence has no MovieScene: %s"), *AssetPath));
		}

		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetTracks();
		const UMovieScene* ConstMovieScene = MovieScene;
		const TArray<FMovieSceneBinding>& Bindings = ConstMovieScene->GetBindings();

		int32 TracksEmitted = 0;
		int32 SectionsEmitted = 0;

		TArray<TSharedPtr<FJsonValue>> MasterTracksJson;
		for (UMovieSceneTrack* Track : MasterTracks)
		{
			if (TracksEmitted >= TrackLimit)
			{
				break;
			}
			MasterTracksJson.Add(MakeShared<FJsonValueObject>(BuildTrackJson(Track, TickResolution, bIncludeSections, SectionLimit, SectionsEmitted)));
			++TracksEmitted;
		}

		TArray<TSharedPtr<FJsonValue>> BindingsJson;
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			if (BindingsJson.Num() >= BindingLimit)
			{
				break;
			}

			TSharedPtr<FJsonObject> BindingJson = BuildBindingMetadataJson(MovieScene, Binding.GetObjectGuid());
			const TArray<UMovieSceneTrack*>& BindingTracks = Binding.GetTracks();
			BindingJson->SetNumberField(TEXT("track_count"), BindingTracks.Num());

			TArray<TSharedPtr<FJsonValue>> TracksJson;
			for (UMovieSceneTrack* Track : BindingTracks)
			{
				if (TracksEmitted >= TrackLimit)
				{
					break;
				}
				TracksJson.Add(MakeShared<FJsonValueObject>(BuildTrackJson(Track, TickResolution, bIncludeSections, SectionLimit, SectionsEmitted)));
				++TracksEmitted;
			}
			BindingJson->SetArrayField(TEXT("tracks"), TracksJson);
			BindingJson->SetBoolField(TEXT("tracks_truncated"), TracksJson.Num() < BindingTracks.Num());
			BindingsJson.Add(MakeShared<FJsonValueObject>(BindingJson));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("sequence_name"), LevelSequence->GetName());
		Data->SetStringField(TEXT("sequence_class"), LevelSequence->GetClass() ? LevelSequence->GetClass()->GetName() : TEXT(""));
		Data->SetObjectField(TEXT("display_rate"), BuildFrameRateJson(MovieScene->GetDisplayRate()));
		Data->SetObjectField(TEXT("tick_resolution"), BuildFrameRateJson(TickResolution));
		Data->SetStringField(TEXT("evaluation_type"), StaticEnum<EMovieSceneEvaluationType>() ? StaticEnum<EMovieSceneEvaluationType>()->GetNameStringByValue(static_cast<int64>(MovieScene->GetEvaluationType())) : TEXT(""));
		Data->SetObjectField(TEXT("playback_range"), BuildRangeJson(MovieScene->GetPlaybackRange(), TickResolution));
		Data->SetNumberField(TEXT("spawnable_count"), MovieScene->GetSpawnableCount());
		Data->SetNumberField(TEXT("possessable_count"), MovieScene->GetPossessableCount());
		Data->SetNumberField(TEXT("binding_count"), Bindings.Num());
		Data->SetNumberField(TEXT("master_track_count"), MasterTracks.Num());
		Data->SetNumberField(TEXT("tracks_emitted"), TracksEmitted);
		Data->SetNumberField(TEXT("sections_emitted"), SectionsEmitted);
		Data->SetBoolField(TEXT("bindings_truncated"), BindingsJson.Num() < Bindings.Num());
		Data->SetBoolField(TEXT("master_tracks_truncated"), MasterTracksJson.Num() < MasterTracks.Num());
		Data->SetBoolField(TEXT("tracks_truncated"), TracksEmitted >= TrackLimit);
		Data->SetBoolField(TEXT("sections_truncated"), SectionsEmitted >= SectionLimit);
		Data->SetArrayField(TEXT("master_tracks"), MasterTracksJson);
		Data->SetArrayField(TEXT("bindings"), BindingsJson);

		return CreateSuccessResponse(Data);
	}, TEXT("Sequencer asset info timed out"));
}
}
