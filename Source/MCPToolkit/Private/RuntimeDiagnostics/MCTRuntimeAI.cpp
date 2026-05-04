// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/MCTRuntimeAI.h"

#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BrainComponent.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISenseConfig.h"

namespace MCPToolkit::RuntimeDiagnostics
{
namespace
{
TSharedPtr<FJsonObject> BuildColorJson(const FColor& Color)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("r"), Color.R);
	Data->SetNumberField(TEXT("g"), Color.G);
	Data->SetNumberField(TEXT("b"), Color.B);
	Data->SetNumberField(TEXT("a"), Color.A);
	return Data;
}

FString PathFollowingStatusToString(EPathFollowingStatus::Type Status)
{
	switch (Status)
	{
	case EPathFollowingStatus::Idle:
		return TEXT("Idle");
	case EPathFollowingStatus::Waiting:
		return TEXT("Waiting");
	case EPathFollowingStatus::Paused:
		return TEXT("Paused");
	case EPathFollowingStatus::Moving:
		return TEXT("Moving");
	default:
		return FString::Printf(TEXT("PathFollowingStatus_%d"), static_cast<int32>(Status));
	}
}

TSharedPtr<FJsonObject> BuildAIRequestIdJson(const FAIRequestID& RequestId)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), RequestId.IsValid());
	Data->SetNumberField(TEXT("id"), static_cast<double>(RequestId.GetID()));
	Data->SetStringField(TEXT("text"), RequestId.ToString());
	return Data;
}

TSharedPtr<FJsonObject> BuildNavLocationJson(const FNavLocation& Location)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetObjectField(TEXT("location"), BuildVectorJson(Location.Location));
	Data->SetBoolField(TEXT("has_node_ref"), Location.HasNodeRef());
	Data->SetStringField(TEXT("node_ref"), FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Location.NodeRef)));
	return Data;
}

TSharedPtr<FJsonObject> BuildAISenseIDJson(const FAISenseID& SenseID, const UAIPerceptionComponent* Perception)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), SenseID.IsValid());
	Data->SetNumberField(TEXT("index"), SenseID.IsValid() ? static_cast<int32>(SenseID.Index) : -1);
	Data->SetStringField(TEXT("name"), SenseID.Name.ToString());
	if (Perception && SenseID.IsValid())
	{
		if (const UAISenseConfig* SenseConfig = Perception->GetSenseConfig(SenseID))
		{
			Data->SetStringField(TEXT("config_name"), SenseConfig->GetSenseName());
			Data->SetObjectField(TEXT("config"), BuildObjectReferenceJson(SenseConfig));
			if (UClass* SenseClass = SenseConfig->GetSenseImplementation().Get())
			{
				Data->SetObjectField(TEXT("sense_class"), BuildObjectReferenceJson(SenseClass));
			}
		}
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildAISenseConfigJson(const UAISenseConfig* SenseConfig, const UAIPerceptionComponent* Perception)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(SenseConfig);
	if (!SenseConfig)
	{
		return Data;
	}

	const FAISenseID SenseID = SenseConfig->GetSenseID();
	Data->SetStringField(TEXT("sense_name"), SenseConfig->GetSenseName());
	Data->SetObjectField(TEXT("sense_id"), BuildAISenseIDJson(SenseID, Perception));
	Data->SetNumberField(TEXT("max_age"), SenseConfig->GetMaxAge());
	Data->SetBoolField(TEXT("starts_enabled"), SenseConfig->GetStartsEnabled());
	Data->SetObjectField(TEXT("debug_color"), BuildColorJson(SenseConfig->GetDebugColor()));
	if (UClass* SenseClass = SenseConfig->GetSenseImplementation().Get())
	{
		Data->SetObjectField(TEXT("sense_class"), BuildObjectReferenceJson(SenseClass));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildAIStimulusJson(const FAIStimulus& Stimulus, const UAIPerceptionComponent* Perception)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), Stimulus.IsValid());
	Data->SetBoolField(TEXT("active"), Stimulus.IsActive());
	Data->SetBoolField(TEXT("expired"), Stimulus.IsExpired());
	Data->SetBoolField(TEXT("successfully_sensed"), Stimulus.WasSuccessfullySensed());
	Data->SetNumberField(TEXT("age"), Stimulus.GetAge());
	Data->SetNumberField(TEXT("strength"), Stimulus.Strength);
	Data->SetStringField(TEXT("tag"), Stimulus.Tag.ToString());
	Data->SetObjectField(TEXT("sense_id"), BuildAISenseIDJson(Stimulus.Type, Perception));
	if (Perception)
	{
		if (UClass* SenseClass = UAIPerceptionSystem::GetSenseClassForStimulus(const_cast<UAIPerceptionComponent*>(Perception), Stimulus).Get())
		{
			Data->SetObjectField(TEXT("sense_class"), BuildObjectReferenceJson(SenseClass));
		}
	}
	Data->SetObjectField(TEXT("stimulus_location"), BuildVectorJson(Stimulus.StimulusLocation));
	Data->SetObjectField(TEXT("receiver_location"), BuildVectorJson(Stimulus.ReceiverLocation));
	return Data;
}

TSharedPtr<FJsonObject> BuildActorPerceptionInfoJson(const FActorPerceptionInfo& Info, const UAIPerceptionComponent* Perception, bool bIncludeStimuli, int32 StimulusLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AActor* Target = Info.Target.Get();
	Data->SetBoolField(TEXT("target_valid"), Target != nullptr);
	Data->SetObjectField(TEXT("target"), Target ? BuildActorJson(Target) : BuildObjectReferenceJson(nullptr));
	Data->SetBoolField(TEXT("hostile"), Info.bIsHostile != 0);
	Data->SetBoolField(TEXT("friendly"), Info.bIsFriendly != 0);
	Data->SetBoolField(TEXT("has_any_known_stimulus"), Info.HasAnyKnownStimulus());
	Data->SetBoolField(TEXT("has_any_current_stimulus"), Info.HasAnyCurrentStimulus());
	Data->SetObjectField(TEXT("dominant_sense"), BuildAISenseIDJson(Info.DominantSense, Perception));

	float LastStimulusAge = 0.0f;
	const FVector LastStimulusLocation = Info.GetLastStimulusLocation(&LastStimulusAge);
	Data->SetNumberField(TEXT("last_stimulus_age"), LastStimulusAge);
	Data->SetObjectField(TEXT("last_stimulus_location"), BuildVectorJson(LastStimulusLocation));

	int32 ValidStimulusCount = 0;
	int32 ActiveStimulusCount = 0;
	int32 SuccessfulStimulusCount = 0;
	int32 ExpiredStimulusCount = 0;
	TArray<TSharedPtr<FJsonValue>> StimuliJson;
	for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
	{
		if (!Stimulus.IsValid())
		{
			continue;
		}
		++ValidStimulusCount;
		ActiveStimulusCount += Stimulus.IsActive() ? 1 : 0;
		SuccessfulStimulusCount += Stimulus.WasSuccessfullySensed() ? 1 : 0;
		ExpiredStimulusCount += Stimulus.IsExpired() ? 1 : 0;
		if (bIncludeStimuli && StimuliJson.Num() < StimulusLimit)
		{
			StimuliJson.Add(MakeShared<FJsonValueObject>(BuildAIStimulusJson(Stimulus, Perception)));
		}
	}

	Data->SetNumberField(TEXT("stored_stimulus_slot_count"), Info.LastSensedStimuli.Num());
	Data->SetNumberField(TEXT("valid_stimulus_count"), ValidStimulusCount);
	Data->SetNumberField(TEXT("active_stimulus_count"), ActiveStimulusCount);
	Data->SetNumberField(TEXT("successful_stimulus_count"), SuccessfulStimulusCount);
	Data->SetNumberField(TEXT("expired_stimulus_count"), ExpiredStimulusCount);
	Data->SetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
	if (bIncludeStimuli)
	{
		Data->SetNumberField(TEXT("returned_stimulus_count"), StimuliJson.Num());
		Data->SetBoolField(TEXT("stimuli_truncated"), ValidStimulusCount > StimuliJson.Num());
		Data->SetArrayField(TEXT("stimuli"), StimuliJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildAIPerceptionComponentJson(UAIPerceptionComponent* Perception, const FString& TargetNameFilter, bool bIncludeStimuli, int32 TargetLimit, int32 StimulusLimit)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(Perception);
	if (!Perception)
	{
		return Data;
	}

	const FPerceptionListenerID ListenerId = Perception->GetListenerId();
	Data->SetNumberField(TEXT("listener_id"), ListenerId.IsValid() ? static_cast<int32>(ListenerId.Index) : -1);
	Data->SetNumberField(TEXT("team_id"), Perception->GetTeamIdentifier().GetId());
	Data->SetObjectField(TEXT("dominant_sense"), BuildAISenseIDJson(Perception->GetDominantSenseID(), Perception));
	Data->SetObjectField(TEXT("body_actor"), BuildObjectReferenceJson(Perception->GetBodyActor()));

	TArray<AActor*> CurrentlyPerceivedActors;
	Perception->GetCurrentlyPerceivedActors(nullptr, CurrentlyPerceivedActors);
	TArray<AActor*> KnownPerceivedActors;
	Perception->GetKnownPerceivedActors(nullptr, KnownPerceivedActors);
	TArray<AActor*> HostileActors;
	Perception->GetHostileActors(HostileActors);
	Data->SetNumberField(TEXT("currently_perceived_actor_count"), CurrentlyPerceivedActors.Num());
	Data->SetNumberField(TEXT("known_perceived_actor_count"), KnownPerceivedActors.Num());
	Data->SetNumberField(TEXT("hostile_actor_count"), HostileActors.Num());

	TArray<TSharedPtr<FJsonValue>> SenseConfigsJson;
	for (UAIPerceptionComponent::TAISenseConfigConstIterator It = Perception->GetSensesConfigIterator(); It; ++It)
	{
		const UAISenseConfig* SenseConfig = *It;
		if (SenseConfig)
		{
			SenseConfigsJson.Add(MakeShared<FJsonValueObject>(BuildAISenseConfigJson(SenseConfig, Perception)));
		}
	}
	Data->SetNumberField(TEXT("sense_config_count"), SenseConfigsJson.Num());
	Data->SetArrayField(TEXT("sense_configs"), SenseConfigsJson);

	auto TargetMatchesFilter = [&TargetNameFilter](AActor* Target)
	{
		return TargetNameFilter.IsEmpty()
			|| (Target && (Target->GetName().Contains(TargetNameFilter) || Target->GetActorLabel().Contains(TargetNameFilter)));
	};

	int32 MatchedTargetCount = 0;
	int32 ActiveTargetCount = 0;
	int32 KnownTargetCount = 0;
	int32 ValidStimulusCount = 0;
	int32 ActiveStimulusCount = 0;
	int32 ExpiredStimulusCount = 0;
	TArray<TSharedPtr<FJsonValue>> TargetsJson;
	for (UAIPerceptionComponent::FActorPerceptionContainer::TConstIterator It = Perception->GetPerceptualDataConstIterator(); It; ++It)
	{
		const FActorPerceptionInfo& Info = It->Value;
		if (!TargetMatchesFilter(Info.Target.Get()))
		{
			continue;
		}

		++MatchedTargetCount;
		KnownTargetCount += Info.HasAnyKnownStimulus() ? 1 : 0;
		ActiveTargetCount += Info.HasAnyCurrentStimulus() ? 1 : 0;
		for (const FAIStimulus& Stimulus : Info.LastSensedStimuli)
		{
			if (!Stimulus.IsValid())
			{
				continue;
			}
			++ValidStimulusCount;
			ActiveStimulusCount += Stimulus.IsActive() ? 1 : 0;
			ExpiredStimulusCount += Stimulus.IsExpired() ? 1 : 0;
		}
		if (TargetsJson.Num() < TargetLimit)
		{
			TargetsJson.Add(MakeShared<FJsonValueObject>(BuildActorPerceptionInfoJson(Info, Perception, bIncludeStimuli, StimulusLimit)));
		}
	}

	Data->SetStringField(TEXT("target_name_filter"), TargetNameFilter);
	Data->SetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
	Data->SetNumberField(TEXT("target_limit"), TargetLimit);
	Data->SetNumberField(TEXT("stimulus_limit"), StimulusLimit);
	Data->SetNumberField(TEXT("matched_target_count"), MatchedTargetCount);
	Data->SetNumberField(TEXT("returned_target_count"), TargetsJson.Num());
	Data->SetBoolField(TEXT("targets_truncated"), MatchedTargetCount > TargetsJson.Num());
	Data->SetNumberField(TEXT("known_target_count"), KnownTargetCount);
	Data->SetNumberField(TEXT("active_target_count"), ActiveTargetCount);
	Data->SetNumberField(TEXT("valid_stimulus_count"), ValidStimulusCount);
	Data->SetNumberField(TEXT("active_stimulus_count"), ActiveStimulusCount);
	Data->SetNumberField(TEXT("expired_stimulus_count"), ExpiredStimulusCount);
	Data->SetArrayField(TEXT("targets"), TargetsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildBlackboardKeyJson(UBlackboardComponent* Blackboard, UBlackboardData* Asset, FBlackboard::FKey KeyId, bool bIncludeValue, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("key_id"), static_cast<int32>(KeyId));
	if (!Blackboard || !Asset || !Asset->IsValidKey(KeyId))
	{
		Data->SetBoolField(TEXT("valid"), false);
		return Data;
	}

	const FName KeyName = Asset->GetKeyName(KeyId);
	UBlackboardKeyType* KeyType = Asset->GetKeyType(KeyId).GetDefaultObject();
	Data->SetBoolField(TEXT("valid"), true);
	Data->SetStringField(TEXT("name"), KeyName.ToString());
	Data->SetObjectField(TEXT("type"), BuildObjectReferenceJson(KeyType));
	Data->SetBoolField(TEXT("instance_synced"), Blackboard->IsKeyInstanceSynced(KeyId));
	Data->SetBoolField(TEXT("vector_value_set"), Blackboard->IsVectorValueSet(KeyId));
	if (bIncludeValue)
	{
		Data->SetStringField(TEXT("value"), TruncateString(Blackboard->DescribeKeyValue(KeyId, EBlackboardDescription::OnlyValue), StringLimit));
		Data->SetStringField(TEXT("description"), TruncateString(Blackboard->DescribeKeyValue(KeyId, EBlackboardDescription::DetailedKeyWithValue), StringLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildBlackboardComponentJson(UBlackboardComponent* Blackboard, bool bIncludeValues, int32 KeyLimit, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(Blackboard);
	Data->SetBoolField(TEXT("present"), Blackboard != nullptr);
	if (!Blackboard)
	{
		return Data;
	}

	UBlackboardData* Asset = Blackboard->GetBlackboardAsset();
	Data->SetObjectField(TEXT("asset"), BuildObjectReferenceJson(Asset));
	Data->SetBoolField(TEXT("has_asset"), Asset != nullptr);
	Data->SetBoolField(TEXT("include_values"), bIncludeValues);
	Data->SetNumberField(TEXT("key_limit"), KeyLimit);
	if (!Asset)
	{
		Data->SetNumberField(TEXT("key_count"), 0);
		Data->SetNumberField(TEXT("returned_key_count"), 0);
		Data->SetArrayField(TEXT("keys"), TArray<TSharedPtr<FJsonValue>>());
		return Data;
	}

	const int32 KeyCount = Asset->GetNumKeys();
	Data->SetNumberField(TEXT("key_count"), KeyCount);
	Data->SetBoolField(TEXT("asset_valid"), Asset->IsValid());
	Data->SetBoolField(TEXT("has_synchronized_keys"), Asset->HasSynchronizedKeys());
	Data->SetObjectField(TEXT("parent_asset"), BuildObjectReferenceJson(Asset->Parent));

	TArray<TSharedPtr<FJsonValue>> KeysJson;
	for (int32 KeyIndex = 0; KeyIndex < KeyCount && KeysJson.Num() < KeyLimit; ++KeyIndex)
	{
		const FBlackboard::FKey KeyId = static_cast<FBlackboard::FKey>(KeyIndex);
		if (Asset->IsValidKey(KeyId))
		{
			KeysJson.Add(MakeShared<FJsonValueObject>(BuildBlackboardKeyJson(Blackboard, Asset, KeyId, bIncludeValues, StringLimit)));
		}
	}

	Data->SetNumberField(TEXT("returned_key_count"), KeysJson.Num());
	Data->SetBoolField(TEXT("keys_truncated"), KeyCount > KeysJson.Num());
	Data->SetArrayField(TEXT("keys"), KeysJson);
	if (bIncludeValues)
	{
		Data->SetStringField(TEXT("debug_info"), TruncateString(Blackboard->GetDebugInfoString(EBlackboardDescription::Full), StringLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildBehaviorTreeComponentJson(UBehaviorTreeComponent* BehaviorTree, bool bIncludeDebugStrings, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(BehaviorTree);
	Data->SetBoolField(TEXT("present"), BehaviorTree != nullptr);
	if (!BehaviorTree)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("tree_has_been_started"), BehaviorTree->TreeHasBeenStarted());
	Data->SetBoolField(TEXT("running"), BehaviorTree->IsRunning());
	Data->SetBoolField(TEXT("paused"), BehaviorTree->IsPaused());
	Data->SetBoolField(TEXT("resource_locked"), BehaviorTree->IsResourceLocked());
	Data->SetBoolField(TEXT("instance_stack_empty"), BehaviorTree->IsInstanceStackEmpty());
	Data->SetNumberField(TEXT("accumulated_tick_delta_time"), BehaviorTree->GetAccumulatedTickDeltaTime());
	Data->SetBoolField(TEXT("include_debug_strings"), bIncludeDebugStrings);
	if (bIncludeDebugStrings)
	{
		Data->SetStringField(TEXT("debug_info"), TruncateString(BehaviorTree->GetDebugInfoString(), StringLimit));
		Data->SetStringField(TEXT("active_tasks"), TruncateString(BehaviorTree->DescribeActiveTasks(), StringLimit));
		Data->SetStringField(TEXT("active_trees"), TruncateString(BehaviorTree->DescribeActiveTrees(), StringLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildBrainComponentJson(UBrainComponent* Brain, bool bIncludeDebugStrings, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(Brain);
	Data->SetBoolField(TEXT("present"), Brain != nullptr);
	if (!Brain)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("running"), Brain->IsRunning());
	Data->SetBoolField(TEXT("paused"), Brain->IsPaused());
	Data->SetBoolField(TEXT("resource_locked"), Brain->IsResourceLocked());
	Data->SetObjectField(TEXT("ai_owner"), BuildObjectReferenceJson(Brain->GetAIOwner()));
	Data->SetBoolField(TEXT("include_debug_strings"), bIncludeDebugStrings);
	if (bIncludeDebugStrings)
	{
		Data->SetStringField(TEXT("debug_info"), TruncateString(Brain->GetDebugInfoString(), StringLimit));
	}

	Data->SetObjectField(TEXT("behavior_tree"), BuildBehaviorTreeComponentJson(Cast<UBehaviorTreeComponent>(Brain), bIncludeDebugStrings, StringLimit));
	return Data;
}

TSharedPtr<FJsonObject> BuildNavigationPathJson(const FNavigationPath* Path, int32 PathPointLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Path != nullptr);
	Data->SetNumberField(TEXT("path_point_limit"), PathPointLimit);
	if (!Path)
	{
		Data->SetNumberField(TEXT("path_point_count"), 0);
		Data->SetNumberField(TEXT("returned_path_point_count"), 0);
		Data->SetArrayField(TEXT("path_points"), TArray<TSharedPtr<FJsonValue>>());
		return Data;
	}

	Data->SetBoolField(TEXT("valid"), Path->IsValid());
	Data->SetBoolField(TEXT("ready"), Path->IsReady());
	Data->SetBoolField(TEXT("up_to_date"), Path->IsUpToDate());
	Data->SetBoolField(TEXT("partial"), Path->IsPartial());
	Data->SetBoolField(TEXT("waiting_for_repath"), Path->IsWaitingForRepath());
	Data->SetBoolField(TEXT("search_reached_limit"), Path->DidSearchReachedLimit());
	Data->SetBoolField(TEXT("error_start_location_non_navigable"), Path->IsErrorStartLocationNonNavigable());
	Data->SetBoolField(TEXT("error_end_location_non_navigable"), Path->IsErrorEndLocationNonNavigable());
	Data->SetBoolField(TEXT("auto_recalculate_on_invalidation"), Path->WillRecalculateOnInvalidation());
	Data->SetBoolField(TEXT("ignore_invalidation"), Path->GetIgnoreInvalidation());
	Data->SetNumberField(TEXT("length"), Path->GetLength());
	Data->SetNumberField(TEXT("cost"), Path->GetCost());
	Data->SetNumberField(TEXT("last_update_time"), Path->GetLastUpdateTime());
	Data->SetObjectField(TEXT("start_location"), BuildVectorJson(Path->GetStartLocation()));
	Data->SetObjectField(TEXT("end_location"), BuildVectorJson(Path->GetEndLocation()));
	Data->SetObjectField(TEXT("destination_location"), BuildVectorJson(Path->GetDestinationLocation()));
	Data->SetObjectField(TEXT("base_actor"), BuildObjectReferenceJson(Path->GetBaseActor()));
	Data->SetObjectField(TEXT("source_actor"), BuildObjectReferenceJson(Path->GetSourceActor()));
	Data->SetObjectField(TEXT("querier"), BuildObjectReferenceJson(Path->GetQuerier()));
	Data->SetObjectField(TEXT("navigation_data"), BuildObjectReferenceJson(Path->GetNavigationDataUsed()));

	const TArray<FNavPathPoint>& PathPoints = Path->GetPathPoints();
	TArray<TSharedPtr<FJsonValue>> PointsJson;
	for (int32 PointIndex = 0; PointIndex < PathPoints.Num() && PointsJson.Num() < PathPointLimit; ++PointIndex)
	{
		const FNavPathPoint& PathPoint = PathPoints[PointIndex];
		TSharedPtr<FJsonObject> PointJson = MakeShared<FJsonObject>();
		PointJson->SetNumberField(TEXT("index"), PointIndex);
		PointJson->SetObjectField(TEXT("location"), BuildVectorJson(PathPoint.Location));
		PointJson->SetBoolField(TEXT("has_node_ref"), PathPoint.HasNodeRef());
		PointJson->SetStringField(TEXT("node_ref"), FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(PathPoint.NodeRef)));
		PointJson->SetNumberField(TEXT("flags"), PathPoint.Flags);
		PointJson->SetBoolField(TEXT("custom_nav_link_id_valid"), PathPoint.CustomNavLinkId.IsValid());
		PointJson->SetStringField(TEXT("custom_nav_link_id"), FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(PathPoint.CustomNavLinkId.GetId())));
		PointsJson.Add(MakeShared<FJsonValueObject>(PointJson));
	}

	Data->SetNumberField(TEXT("path_point_count"), PathPoints.Num());
	Data->SetNumberField(TEXT("returned_path_point_count"), PointsJson.Num());
	Data->SetBoolField(TEXT("path_points_truncated"), PathPoints.Num() > PointsJson.Num());
	Data->SetArrayField(TEXT("path_points"), PointsJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildPathFollowingComponentJson(UPathFollowingComponent* PathFollowing, bool bIncludePathPoints, int32 PathPointLimit, bool bIncludeDebugStrings, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(PathFollowing);
	Data->SetBoolField(TEXT("present"), PathFollowing != nullptr);
	if (!PathFollowing)
	{
		return Data;
	}

	Data->SetStringField(TEXT("status"), PathFollowingStatusToString(PathFollowing->GetStatus()));
	Data->SetStringField(TEXT("status_description"), PathFollowing->GetStatusDesc());
	Data->SetObjectField(TEXT("current_request_id"), BuildAIRequestIdJson(PathFollowing->GetCurrentRequestId()));
	Data->SetNumberField(TEXT("current_path_index"), PathFollowing->GetCurrentPathIndex());
	Data->SetNumberField(TEXT("next_path_index"), PathFollowing->GetNextPathIndex());
	Data->SetNumberField(TEXT("remaining_path_cost"), PathFollowing->GetRemainingPathCost());
	Data->SetNumberField(TEXT("acceptance_radius"), PathFollowing->GetAcceptanceRadius());
	Data->SetNumberField(TEXT("default_acceptance_radius"), PathFollowing->GetDefaultAcceptanceRadius());
	Data->SetBoolField(TEXT("valid_path"), PathFollowing->HasValidPath());
	Data->SetBoolField(TEXT("direct_path"), PathFollowing->HasDirectPath());
	Data->SetBoolField(TEXT("partial_path"), PathFollowing->HasPartialPath());
	Data->SetBoolField(TEXT("did_move_reach_goal"), PathFollowing->DidMoveReachGoal());
	Data->SetBoolField(TEXT("block_detection_active"), PathFollowing->IsBlockDetectionActive());
	Data->SetBoolField(TEXT("decelerating"), PathFollowing->IsDecelerating());
	Data->SetBoolField(TEXT("stop_movement_on_finish_active"), PathFollowing->IsStopMovementOnFinishActive());
	Data->SetBoolField(TEXT("started_nav_link_move"), PathFollowing->HasStartedNavLinkMove());
	Data->SetBoolField(TEXT("current_segment_navigation_link"), PathFollowing->IsCurrentSegmentNavigationLink());
	Data->SetBoolField(TEXT("movement_authority"), PathFollowing->HasMovementAuthority());
	Data->SetObjectField(TEXT("current_nav_location"), BuildNavLocationJson(PathFollowing->GetCurrentNavLocation()));
	Data->SetObjectField(TEXT("current_target_location"), BuildVectorJson(PathFollowing->GetCurrentTargetLocation()));
	Data->SetObjectField(TEXT("move_goal_location_offset"), BuildVectorJson(PathFollowing->GetMoveGoalLocationOffset()));
	Data->SetObjectField(TEXT("current_direction"), BuildVectorJson(PathFollowing->GetCurrentDirection()));
	Data->SetObjectField(TEXT("current_move_input"), BuildVectorJson(PathFollowing->GetCurrentMoveInput()));
	Data->SetObjectField(TEXT("move_goal"), BuildObjectReferenceJson(PathFollowing->GetMoveGoal()));
	Data->SetObjectField(TEXT("current_custom_link_object"), BuildObjectReferenceJson(PathFollowing->GetCurrentCustomLinkOb()));
	Data->SetBoolField(TEXT("include_path_points"), bIncludePathPoints);
	Data->SetNumberField(TEXT("path_point_limit"), PathPointLimit);
	const FNavPathSharedPtr CurrentPath = PathFollowing->GetPath();
	Data->SetObjectField(TEXT("path"), bIncludePathPoints ? BuildNavigationPathJson(CurrentPath.Get(), PathPointLimit) : BuildObjectReferenceJson(CurrentPath.IsValid() ? CurrentPath->GetNavigationDataUsed() : nullptr));
	if (bIncludeDebugStrings)
	{
		Data->SetStringField(TEXT("debug_string"), TruncateString(PathFollowing->GetDebugString(), StringLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildAIControllerJson(AAIController* Controller, bool bIncludeBlackboardValues, bool bIncludeBehaviorDebug, bool bIncludePathPoints, bool bIncludePerception, int32 BlackboardKeyLimit, int32 PathPointLimit, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = BuildActorJson(Controller);
	Data->SetBoolField(TEXT("present"), Controller != nullptr);
	if (!Controller)
	{
		return Data;
	}

	APawn* Pawn = Controller->GetPawn();
	Data->SetNumberField(TEXT("team_id"), Controller->GetGenericTeamId().GetId());
	Data->SetStringField(TEXT("move_status"), PathFollowingStatusToString(Controller->GetMoveStatus()));
	Data->SetObjectField(TEXT("current_move_request_id"), BuildAIRequestIdJson(Controller->GetCurrentMoveRequestID()));
	Data->SetBoolField(TEXT("partial_path"), Controller->HasPartialPath());
	Data->SetBoolField(TEXT("following_path"), Controller->IsFollowingAPath());
	Data->SetObjectField(TEXT("immediate_move_destination"), BuildVectorJson(Controller->GetImmediateMoveDestination()));
	Data->SetObjectField(TEXT("focal_point"), BuildVectorJson(Controller->GetFocalPoint()));
	Data->SetObjectField(TEXT("focus_actor"), BuildObjectReferenceJson(Controller->GetFocusActor()));
	Data->SetObjectField(TEXT("default_navigation_filter_class"), BuildObjectReferenceJson(Controller->GetDefaultNavigationFilterClass().Get()));
	Data->SetObjectField(TEXT("pawn"), Pawn ? BuildActorJson(Pawn) : BuildObjectReferenceJson(nullptr));
	Data->SetObjectField(TEXT("player_state"), BuildObjectReferenceJson(Controller->PlayerState));
	Data->SetObjectField(TEXT("brain"), BuildBrainComponentJson(Controller->GetBrainComponent(), bIncludeBehaviorDebug, StringLimit));
	Data->SetObjectField(TEXT("blackboard"), BuildBlackboardComponentJson(Controller->GetBlackboardComponent(), bIncludeBlackboardValues, BlackboardKeyLimit, StringLimit));
	Data->SetObjectField(TEXT("path_following"), BuildPathFollowingComponentJson(Controller->GetPathFollowingComponent(), bIncludePathPoints, PathPointLimit, bIncludeBehaviorDebug, StringLimit));

	UAIPerceptionComponent* Perception = Controller->GetAIPerceptionComponent();
	Data->SetBoolField(TEXT("perception_component_present"), Perception != nullptr);
	Data->SetObjectField(TEXT("perception"), bIncludePerception ? BuildAIPerceptionComponentJson(Perception, TEXT(""), false, 0, 0) : BuildComponentJson(Perception));
	return Data;
}
}

TSharedPtr<FJsonObject> BuildAIPerceptionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	FString TargetNameFilter;
	bool bIncludeStimuli = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("target_name_filter"), TargetNameFilter);
		Params->TryGetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	TargetNameFilter.TrimStartAndEndInline();
	const int32 ListenerLimit = ReadClampedIntField(Params, TEXT("listener_limit"), 100, 1, 1000);
	const int32 TargetLimit = ReadClampedIntField(Params, TEXT("target_limit"), 100, 0, 1000);
	const int32 StimulusLimit = ReadClampedIntField(Params, TEXT("stimulus_limit"), 100, 0, 1000);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("requested_world"), WorldSelector);
	Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());

	TArray<TSharedPtr<FJsonValue>> Warnings;
	FString WorldSource;
	UWorld* World = SelectWorld(WorldSelector, WorldSource);
	Data->SetStringField(TEXT("world_source"), WorldSource);
	Data->SetBoolField(TEXT("world_available"), World != nullptr);
	if (!World)
	{
		Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
	}

	if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
	{
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; AI perception diagnostics reflect the editor world")));
	}

	Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
	Data->SetBoolField(TEXT("include_stimuli"), bIncludeStimuli);
	Data->SetNumberField(TEXT("listener_limit"), ListenerLimit);
	Data->SetNumberField(TEXT("target_limit"), TargetLimit);
	Data->SetNumberField(TEXT("stimulus_limit"), StimulusLimit);
	Data->SetStringField(TEXT("target_name_filter"), TargetNameFilter);

	const bool bSpecificActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
	TArray<UAIPerceptionComponent*> ComponentsToInspect;
	int32 MatchedListenerCount = 0;
	int32 PerceptionOwnerActorCount = 0;
	int32 TotalKnownTargetCount = 0;
	int32 TotalCurrentlyPerceivedTargetCount = 0;
	int32 TotalHostileTargetCount = 0;
	int32 TotalValidStimulusCount = 0;
	int32 TotalActiveStimulusCount = 0;
	int32 TotalExpiredStimulusCount = 0;

	auto AccumulatePerceptionCounters = [&PerceptionOwnerActorCount, &TotalKnownTargetCount, &TotalCurrentlyPerceivedTargetCount, &TotalHostileTargetCount, &TotalValidStimulusCount, &TotalActiveStimulusCount, &TotalExpiredStimulusCount](AActor* Actor)
	{
		if (!Actor)
		{
			return 0;
		}

		TArray<UAIPerceptionComponent*> PerceptionComponents;
		Actor->GetComponents<UAIPerceptionComponent>(PerceptionComponents);
		if (PerceptionComponents.IsEmpty())
		{
			return 0;
		}

		++PerceptionOwnerActorCount;
		for (UAIPerceptionComponent* Perception : PerceptionComponents)
		{
			if (!Perception)
			{
				continue;
			}

			TArray<AActor*> KnownActors;
			Perception->GetKnownPerceivedActors(nullptr, KnownActors);
			TotalKnownTargetCount += KnownActors.Num();

			TArray<AActor*> CurrentActors;
			Perception->GetCurrentlyPerceivedActors(nullptr, CurrentActors);
			TotalCurrentlyPerceivedTargetCount += CurrentActors.Num();

			TArray<AActor*> HostileActors;
			Perception->GetHostileActors(HostileActors);
			TotalHostileTargetCount += HostileActors.Num();

			for (UAIPerceptionComponent::FActorPerceptionContainer::TConstIterator It = Perception->GetPerceptualDataConstIterator(); It; ++It)
			{
				for (const FAIStimulus& Stimulus : It->Value.LastSensedStimuli)
				{
					if (!Stimulus.IsValid())
					{
						continue;
					}
					++TotalValidStimulusCount;
					TotalActiveStimulusCount += Stimulus.IsActive() ? 1 : 0;
					TotalExpiredStimulusCount += Stimulus.IsExpired() ? 1 : 0;
				}
			}
		}

		return PerceptionComponents.Num();
	};

	if (bSpecificActorRequested)
	{
		AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName);
		Data->SetBoolField(TEXT("selected_actor_requested"), true);
		Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
		if (Actor)
		{
			MatchedListenerCount = AccumulatePerceptionCounters(Actor);
			Actor->GetComponents<UAIPerceptionComponent>(ComponentsToInspect);
			if (MatchedListenerCount == 0)
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor has no AIPerceptionComponent")));
			}
		}
		else
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
		}
	}
	else
	{
		Data->SetBoolField(TEXT("selected_actor_requested"), false);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}

			const FString Label = Actor->GetActorLabel();
			const FString Name = Actor->GetName();
			const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
			if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
			{
				continue;
			}
			if (!ClassFilter.IsEmpty() && !ActorClassPath.Contains(ClassFilter))
			{
				continue;
			}

			TArray<UAIPerceptionComponent*> PerceptionComponents;
			Actor->GetComponents<UAIPerceptionComponent>(PerceptionComponents);
			if (PerceptionComponents.IsEmpty())
			{
				continue;
			}

			MatchedListenerCount += PerceptionComponents.Num();
			AccumulatePerceptionCounters(Actor);
			for (UAIPerceptionComponent* Perception : PerceptionComponents)
			{
				if (Perception && ComponentsToInspect.Num() < ListenerLimit)
				{
					ComponentsToInspect.Add(Perception);
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ListenersJson;
	for (UAIPerceptionComponent* Perception : ComponentsToInspect)
	{
		if (Perception)
		{
			ListenersJson.Add(MakeShared<FJsonValueObject>(BuildAIPerceptionComponentJson(Perception, TargetNameFilter, bIncludeStimuli, TargetLimit, StimulusLimit)));
		}
	}

	Data->SetNumberField(TEXT("matched_listener_count"), MatchedListenerCount);
	Data->SetNumberField(TEXT("returned_listener_count"), ListenersJson.Num());
	Data->SetBoolField(TEXT("listeners_truncated"), MatchedListenerCount > ListenersJson.Num());
	Data->SetNumberField(TEXT("perception_owner_actor_count"), PerceptionOwnerActorCount);
	Data->SetNumberField(TEXT("total_known_target_count"), TotalKnownTargetCount);
	Data->SetNumberField(TEXT("total_currently_perceived_target_count"), TotalCurrentlyPerceivedTargetCount);
	Data->SetNumberField(TEXT("total_hostile_target_count"), TotalHostileTargetCount);
	Data->SetNumberField(TEXT("total_valid_stimulus_count"), TotalValidStimulusCount);
	Data->SetNumberField(TEXT("total_active_stimulus_count"), TotalActiveStimulusCount);
	Data->SetNumberField(TEXT("total_expired_stimulus_count"), TotalExpiredStimulusCount);
	Data->SetArrayField(TEXT("listeners"), ListenersJson);
	Data->SetArrayField(TEXT("warnings"), Warnings);
	return Data;
}

TSharedPtr<FJsonObject> BuildAIControllerDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	FString PawnFilter;
	bool bIncludeBlackboardValues = true;
	bool bIncludeBehaviorDebug = true;
	bool bIncludePathPoints = true;
	bool bIncludePerception = false;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("pawn_filter"), PawnFilter);
		Params->TryGetBoolField(TEXT("include_blackboard_values"), bIncludeBlackboardValues);
		Params->TryGetBoolField(TEXT("include_behavior_debug"), bIncludeBehaviorDebug);
		Params->TryGetBoolField(TEXT("include_path_points"), bIncludePathPoints);
		Params->TryGetBoolField(TEXT("include_perception"), bIncludePerception);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	PawnFilter.TrimStartAndEndInline();
	const int32 ControllerLimit = ReadClampedIntField(Params, TEXT("controller_limit"), 100, 1, 1000);
	const int32 BlackboardKeyLimit = ReadClampedIntField(Params, TEXT("blackboard_key_limit"), 50, 0, 1000);
	const int32 PathPointLimit = ReadClampedIntField(Params, TEXT("path_point_limit"), 25, 0, 1000);
	const int32 DebugStringLimit = ReadClampedIntField(Params, TEXT("debug_string_limit"), 4000, 0, 20000);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("requested_world"), WorldSelector);
	Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
	Data->SetStringField(TEXT("name_filter"), NameFilter);
	Data->SetStringField(TEXT("class_filter"), ClassFilter);
	Data->SetStringField(TEXT("pawn_filter"), PawnFilter);
	Data->SetBoolField(TEXT("include_blackboard_values"), bIncludeBlackboardValues);
	Data->SetBoolField(TEXT("include_behavior_debug"), bIncludeBehaviorDebug);
	Data->SetBoolField(TEXT("include_path_points"), bIncludePathPoints);
	Data->SetBoolField(TEXT("include_perception"), bIncludePerception);
	Data->SetNumberField(TEXT("controller_limit"), ControllerLimit);
	Data->SetNumberField(TEXT("blackboard_key_limit"), BlackboardKeyLimit);
	Data->SetNumberField(TEXT("path_point_limit"), PathPointLimit);
	Data->SetNumberField(TEXT("debug_string_limit"), DebugStringLimit);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	FString WorldSource;
	UWorld* World = SelectWorld(WorldSelector, WorldSource);
	Data->SetStringField(TEXT("world_source"), WorldSource);
	Data->SetBoolField(TEXT("world_available"), World != nullptr);
	if (!World)
	{
		Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
	}

	if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
	{
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; AI controller diagnostics reflect the editor world")));
	}

	Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));

	TArray<AAIController*> AllControllers;
	int32 AIControllerCount = 0;
	int32 PossessedControllerCount = 0;
	int32 BrainComponentCount = 0;
	int32 RunningBrainCount = 0;
	int32 PausedBrainCount = 0;
	int32 BlackboardComponentCount = 0;
	int32 BehaviorTreeComponentCount = 0;
	int32 PathFollowingComponentCount = 0;
	int32 MovingControllerCount = 0;
	int32 PerceptionComponentCount = 0;
	for (TActorIterator<AAIController> It(World); It; ++It)
	{
		AAIController* Controller = *It;
		if (!Controller)
		{
			continue;
		}

		AllControllers.Add(Controller);
		++AIControllerCount;
		PossessedControllerCount += Controller->GetPawn() ? 1 : 0;
		if (UBrainComponent* Brain = Controller->GetBrainComponent())
		{
			++BrainComponentCount;
			RunningBrainCount += Brain->IsRunning() ? 1 : 0;
			PausedBrainCount += Brain->IsPaused() ? 1 : 0;
			BehaviorTreeComponentCount += Cast<UBehaviorTreeComponent>(Brain) ? 1 : 0;
		}
		BlackboardComponentCount += Controller->GetBlackboardComponent() ? 1 : 0;
		PathFollowingComponentCount += Controller->GetPathFollowingComponent() ? 1 : 0;
		MovingControllerCount += Controller->GetMoveStatus() == EPathFollowingStatus::Moving ? 1 : 0;
		PerceptionComponentCount += Controller->GetAIPerceptionComponent() ? 1 : 0;
	}

	auto ControllerMatchesFilters = [&NameFilter, &ClassFilter, &PawnFilter](AAIController* Controller)
	{
		if (!Controller)
		{
			return false;
		}

		const FString ControllerName = Controller->GetName();
		const FString ControllerLabel = Controller->GetActorLabel();
		const FString ControllerPath = Controller->GetPathName();
		const FString ControllerClassPath = Controller->GetClass() ? Controller->GetClass()->GetPathName() : TEXT("");
		APawn* Pawn = Controller->GetPawn();
		const FString PawnName = Pawn ? Pawn->GetName() : TEXT("");
		const FString PawnLabel = Pawn ? Pawn->GetActorLabel() : TEXT("");
		const FString PawnPath = Pawn ? Pawn->GetPathName() : TEXT("");
		const FString PawnClassPath = (Pawn && Pawn->GetClass()) ? Pawn->GetClass()->GetPathName() : TEXT("");

		if (!NameFilter.IsEmpty()
			&& !ControllerName.Contains(NameFilter)
			&& !ControllerLabel.Contains(NameFilter)
			&& !ControllerPath.Contains(NameFilter)
			&& !PawnName.Contains(NameFilter)
			&& !PawnLabel.Contains(NameFilter)
			&& !PawnPath.Contains(NameFilter))
		{
			return false;
		}
		if (!ClassFilter.IsEmpty() && !ControllerClassPath.Contains(ClassFilter))
		{
			return false;
		}
		if (!PawnFilter.IsEmpty()
			&& !PawnName.Contains(PawnFilter)
			&& !PawnLabel.Contains(PawnFilter)
			&& !PawnPath.Contains(PawnFilter)
			&& !PawnClassPath.Contains(PawnFilter))
		{
			return false;
		}
		return true;
	};

	const bool bSpecificActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
	TArray<AAIController*> ControllersToInspect;
	int32 MatchedAIControllerCount = 0;
	Data->SetBoolField(TEXT("selected_actor_requested"), bSpecificActorRequested);
	if (bSpecificActorRequested)
	{
		AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName);
		Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
		AAIController* SelectedController = Cast<AAIController>(Actor);
		if (!SelectedController)
		{
			if (APawn* SelectedPawn = Cast<APawn>(Actor))
			{
				SelectedController = Cast<AAIController>(SelectedPawn->GetController());
			}
		}

		Data->SetBoolField(TEXT("selected_ai_controller_found"), SelectedController != nullptr);
		if (SelectedController)
		{
			MatchedAIControllerCount = 1;
			ControllersToInspect.Add(SelectedController);
		}
		else if (Actor)
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor is not an AIController and is not possessed by an AIController")));
		}
		else
		{
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
		}
	}
	else
	{
		Data->SetBoolField(TEXT("selected_actor_found"), false);
		Data->SetBoolField(TEXT("selected_ai_controller_found"), false);
		for (AAIController* Controller : AllControllers)
		{
			if (!Controller || !ControllerMatchesFilters(Controller))
			{
				continue;
			}
			++MatchedAIControllerCount;
			if (ControllersToInspect.Num() < ControllerLimit)
			{
				ControllersToInspect.Add(Controller);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ControllersJson;
	for (AAIController* Controller : ControllersToInspect)
	{
		if (Controller)
		{
			ControllersJson.Add(MakeShared<FJsonValueObject>(BuildAIControllerJson(
				Controller,
				bIncludeBlackboardValues,
				bIncludeBehaviorDebug,
				bIncludePathPoints,
				bIncludePerception,
				BlackboardKeyLimit,
				PathPointLimit,
				DebugStringLimit)));
		}
	}

	Data->SetNumberField(TEXT("ai_controller_count"), AIControllerCount);
	Data->SetNumberField(TEXT("matched_ai_controller_count"), MatchedAIControllerCount);
	Data->SetNumberField(TEXT("returned_ai_controller_count"), ControllersJson.Num());
	Data->SetBoolField(TEXT("ai_controllers_truncated"), !bSpecificActorRequested && MatchedAIControllerCount > ControllersJson.Num());
	Data->SetNumberField(TEXT("possessed_controller_count"), PossessedControllerCount);
	Data->SetNumberField(TEXT("brain_component_count"), BrainComponentCount);
	Data->SetNumberField(TEXT("running_brain_count"), RunningBrainCount);
	Data->SetNumberField(TEXT("paused_brain_count"), PausedBrainCount);
	Data->SetNumberField(TEXT("blackboard_component_count"), BlackboardComponentCount);
	Data->SetNumberField(TEXT("behavior_tree_component_count"), BehaviorTreeComponentCount);
	Data->SetNumberField(TEXT("path_following_component_count"), PathFollowingComponentCount);
	Data->SetNumberField(TEXT("moving_controller_count"), MovingControllerCount);
	Data->SetNumberField(TEXT("perception_component_count"), PerceptionComponentCount);
	Data->SetArrayField(TEXT("ai_controllers"), ControllersJson);
	Data->SetArrayField(TEXT("warnings"), Warnings);
	return Data;
}
}
