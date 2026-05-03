// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/AIExportRuntimePhysics.h"

#include "RuntimeDiagnostics/AIExportRuntimeDiagnosticsUtils.h"

#include "Components/PrimitiveComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "UObject/UnrealType.h"

namespace CommonAIExport::RuntimeDiagnostics
{
namespace
{
FString CollisionEnabledToString(ECollisionEnabled::Type Value)
{
	switch (Value)
	{
	case ECollisionEnabled::NoCollision:
		return TEXT("NoCollision");
	case ECollisionEnabled::QueryOnly:
		return TEXT("QueryOnly");
	case ECollisionEnabled::PhysicsOnly:
		return TEXT("PhysicsOnly");
	case ECollisionEnabled::QueryAndPhysics:
		return TEXT("QueryAndPhysics");
	case ECollisionEnabled::ProbeOnly:
		return TEXT("ProbeOnly");
	case ECollisionEnabled::QueryAndProbe:
		return TEXT("QueryAndProbe");
	default:
		return FString::Printf(TEXT("CollisionEnabled_%d"), static_cast<int32>(Value));
	}
}

FString CollisionResponseToString(ECollisionResponse Value)
{
	switch (Value)
	{
	case ECR_Ignore:
		return TEXT("Ignore");
	case ECR_Overlap:
		return TEXT("Overlap");
	case ECR_Block:
		return TEXT("Block");
	default:
		return FString::Printf(TEXT("CollisionResponse_%d"), static_cast<int32>(Value));
	}
}

FString CollisionChannelToString(ECollisionChannel Channel)
{
	if (const UEnum* ChannelEnum = StaticEnum<ECollisionChannel>())
	{
		return ChannelEnum->GetNameStringByValue(static_cast<int64>(Channel));
	}
	return FString::Printf(TEXT("CollisionChannel_%d"), static_cast<int32>(Channel));
}

FString ComponentMobilityToString(EComponentMobility::Type Mobility)
{
	switch (Mobility)
	{
	case EComponentMobility::Static:
		return TEXT("Static");
	case EComponentMobility::Stationary:
		return TEXT("Stationary");
	case EComponentMobility::Movable:
		return TEXT("Movable");
	default:
		return FString::Printf(TEXT("Mobility_%d"), static_cast<int32>(Mobility));
	}
}

void IncrementStringCounter(TMap<FString, int32>& Counter, const FString& Key)
{
	++Counter.FindOrAdd(Key);
}

TArray<TSharedPtr<FJsonValue>> BuildStringCounterJson(const TMap<FString, int32>& Counter, const TCHAR* NameField)
{
	TArray<FString> Keys;
	Counter.GetKeys(Keys);
	Keys.Sort();

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& Key : Keys)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(NameField, Key);
		Entry->SetNumberField(TEXT("count"), Counter.FindRef(Key));
		Values.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return Values;
}

void AddReflectedSettingsPropertyJson(TSharedPtr<FJsonObject> Target, const UObject* Settings, const TCHAR* PropertyName, const TCHAR* JsonName)
{
	if (!Target.IsValid() || !Settings)
	{
		return;
	}

	const FProperty* Property = Settings->GetClass() ? Settings->GetClass()->FindPropertyByName(PropertyName) : nullptr;
	if (!Property)
	{
		return;
	}

	FString Value;
	Property->ExportText_InContainer(0, Value, Settings, Settings, const_cast<UObject*>(Settings), PPF_None);
	Target->SetStringField(JsonName, Value);
}

TSharedPtr<FJsonObject> BuildRuntimePhysicsSettingsJson()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const UPhysicsSettings* Settings = UPhysicsSettings::Get();
	Data->SetBoolField(TEXT("present"), Settings != nullptr);
	if (!Settings)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("object"), BuildObjectReferenceJson(Settings));
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("DefaultGravityZ"), TEXT("default_gravity_z"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("DefaultTerminalVelocity"), TEXT("default_terminal_velocity"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("DefaultFluidFriction"), TEXT("default_fluid_friction"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("SimulateScratchMemorySize"), TEXT("simulate_scratch_memory_size"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("RagdollAggregateThreshold"), TEXT("ragdoll_aggregate_threshold"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("bEnableEnhancedDeterminism"), TEXT("enable_enhanced_determinism"));
	AddReflectedSettingsPropertyJson(Properties, Settings, TEXT("MinDeltaVelocityForHitEvents"), TEXT("min_delta_velocity_for_hit_events"));
	Data->SetObjectField(TEXT("properties"), Properties);
	return Data;
}

TSharedPtr<FJsonObject> BuildRuntimePhysicsWorldJson(UWorld* World)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), World != nullptr);
	if (!World)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("physics_scene_present"), World->GetPhysicsScene() != nullptr);
	Data->SetNumberField(TEXT("gravity_z"), World->GetGravityZ());
	Data->SetNumberField(TEXT("default_gravity_z"), World->GetDefaultGravityZ());
	Data->SetBoolField(TEXT("default_physics_volume_created"), World->HasDefaultPhysicsVolume());
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildCollisionResponsesJson(UPrimitiveComponent* Component, int32& OutBlockCount, int32& OutOverlapCount, int32& OutIgnoreCount)
{
	OutBlockCount = 0;
	OutOverlapCount = 0;
	OutIgnoreCount = 0;

	TArray<TSharedPtr<FJsonValue>> Responses;
	if (!Component)
	{
		return Responses;
	}

	const int32 MaxSerializableChannel = static_cast<int32>(ECC_OverlapAll_Deprecated);
	for (int32 ChannelIndex = 0; ChannelIndex < MaxSerializableChannel; ++ChannelIndex)
	{
		const ECollisionChannel Channel = static_cast<ECollisionChannel>(ChannelIndex);
		const ECollisionResponse Response = Component->GetCollisionResponseToChannel(Channel);
		if (Response == ECR_Block)
		{
			++OutBlockCount;
		}
		else if (Response == ECR_Overlap)
		{
			++OutOverlapCount;
		}
		else if (Response == ECR_Ignore)
		{
			++OutIgnoreCount;
		}

		TSharedPtr<FJsonObject> ResponseJson = MakeShared<FJsonObject>();
		ResponseJson->SetStringField(TEXT("channel"), CollisionChannelToString(Channel));
		ResponseJson->SetNumberField(TEXT("channel_index"), ChannelIndex);
		ResponseJson->SetStringField(TEXT("response"), CollisionResponseToString(Response));
		Responses.Add(MakeShared<FJsonValueObject>(ResponseJson));
	}
	return Responses;
}

TSharedPtr<FJsonObject> BuildRuntimePrimitivePhysicsJson(UPrimitiveComponent* Component, bool bIncludeResponses)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(Component);
	if (!Component)
	{
		return Data;
	}

	const ECollisionEnabled::Type CollisionEnabled = Component->GetCollisionEnabled();
	const ECollisionChannel ObjectType = Component->GetCollisionObjectType();
	FBodyInstance* BodyInstance = Component->GetBodyInstance();
	const bool bBodyInstancePresent = BodyInstance != nullptr;
	const float Mass = bBodyInstancePresent ? Component->GetMass() : 0.0f;

	Data->SetStringField(TEXT("mobility"), ComponentMobilityToString(Component->Mobility.GetValue()));
	Data->SetObjectField(TEXT("component_location"), BuildVectorJson(Component->GetComponentLocation()));
	Data->SetObjectField(TEXT("component_velocity"), BuildVectorJson(Component->GetComponentVelocity()));
	Data->SetObjectField(TEXT("bounds_origin"), BuildVectorJson(Component->Bounds.Origin));
	Data->SetObjectField(TEXT("bounds_box_extent"), BuildVectorJson(Component->Bounds.BoxExtent));
	Data->SetNumberField(TEXT("bounds_sphere_radius"), Component->Bounds.SphereRadius);
	Data->SetStringField(TEXT("collision_profile_name"), Component->GetCollisionProfileName().ToString());
	Data->SetStringField(TEXT("collision_enabled"), CollisionEnabledToString(CollisionEnabled));
	Data->SetBoolField(TEXT("query_collision_enabled"), CollisionEnabledHasQuery(CollisionEnabled));
	Data->SetBoolField(TEXT("physics_collision_enabled"), CollisionEnabledHasPhysics(CollisionEnabled));
	Data->SetBoolField(TEXT("probe_collision_enabled"), CollisionEnabledHasProbe(CollisionEnabled));
	Data->SetStringField(TEXT("collision_object_type"), CollisionChannelToString(ObjectType));
	Data->SetNumberField(TEXT("collision_object_type_index"), static_cast<int32>(ObjectType));
	Data->SetBoolField(TEXT("generate_overlap_events"), Component->GetGenerateOverlapEvents());
	Data->SetBoolField(TEXT("simulating_physics"), Component->IsSimulatingPhysics());
	Data->SetBoolField(TEXT("gravity_enabled"), Component->IsGravityEnabled());
	Data->SetBoolField(TEXT("body_instance_present"), bBodyInstancePresent);
	Data->SetNumberField(TEXT("mass_kg"), Mass);

	if (BodyInstance)
	{
		Data->SetStringField(TEXT("body_collision_enabled"), CollisionEnabledToString(BodyInstance->GetCollisionEnabled()));
		Data->SetStringField(TEXT("body_collision_profile_name"), BodyInstance->GetCollisionProfileName().ToString());
	}

	int32 BlockResponseCount = 0;
	int32 OverlapResponseCount = 0;
	int32 IgnoreResponseCount = 0;
	TArray<TSharedPtr<FJsonValue>> Responses = BuildCollisionResponsesJson(Component, BlockResponseCount, OverlapResponseCount, IgnoreResponseCount);
	Data->SetNumberField(TEXT("block_response_count"), BlockResponseCount);
	Data->SetNumberField(TEXT("overlap_response_count"), OverlapResponseCount);
	Data->SetNumberField(TEXT("ignore_response_count"), IgnoreResponseCount);
	Data->SetBoolField(TEXT("include_responses"), bIncludeResponses);
	if (bIncludeResponses)
	{
		Data->SetArrayField(TEXT("responses"), Responses);
	}
	return Data;
}

}

TSharedPtr<FJsonObject> BuildPhysicsCollisionDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString NameFilter;
	FString ClassFilter;
	FString ComponentClassFilter;
	bool bIncludeComponents = true;
	bool bIncludeResponses = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
		Params->TryGetBoolField(TEXT("include_responses"), bIncludeResponses);
	}
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	ComponentClassFilter.TrimStartAndEndInline();
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 200, 0, 2000);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetStringField(TEXT("name_filter"), NameFilter);
		Data->SetStringField(TEXT("class_filter"), ClassFilter);
		Data->SetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Data->SetBoolField(TEXT("include_components"), bIncludeComponents);
		Data->SetBoolField(TEXT("include_responses"), bIncludeResponses);
		Data->SetNumberField(TEXT("component_limit"), ComponentLimit);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
		Data->SetObjectField(TEXT("physics_settings"), BuildRuntimePhysicsSettingsJson());

		TArray<TSharedPtr<FJsonValue>> Limitations;
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("This endpoint is read-only and does not execute sweep, trace, overlap, or contact queries")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("Physics solver internals, broadphase details, and live contact pairs are not exposed through public runtime API")));
		Limitations.Add(MakeShared<FJsonValueString>(TEXT("Mass, bounds, and body-instance fields are point-in-time component snapshots")));
		Data->SetArrayField(TEXT("limitations"), Limitations);

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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; physics/collision diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetObjectField(TEXT("physics_world"), BuildRuntimePhysicsWorldJson(World));

		auto ActorMatchesClassFilter = [&ClassFilter](AActor* Actor)
		{
			if (!Actor)
			{
				return false;
			}
			const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
			return ClassFilter.IsEmpty() || ActorClassPath.Contains(ClassFilter);
		};

		auto ComponentMatchesFilters = [&NameFilter, &ComponentClassFilter](UPrimitiveComponent* Component)
		{
			if (!Component)
			{
				return false;
			}

			const FString ComponentClassPath = Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT("");
			if (!ComponentClassFilter.IsEmpty() && !ComponentClassPath.Contains(ComponentClassFilter))
			{
				return false;
			}

			if (NameFilter.IsEmpty())
			{
				return true;
			}

			if (Component->GetName().Contains(NameFilter) || Component->GetPathName().Contains(NameFilter))
			{
				return true;
			}

			if (AActor* Owner = Component->GetOwner())
			{
				return Owner->GetName().Contains(NameFilter) || Owner->GetActorLabel().Contains(NameFilter) || Owner->GetPathName().Contains(NameFilter);
			}
			return false;
		};

		int32 TotalActorCount = 0;
		int32 ActorClassFilterMatchCount = 0;
		int32 TotalPrimitiveComponentCount = 0;
		int32 MatchedPrimitiveComponentCount = 0;
		int32 RegisteredPrimitiveComponentCount = 0;
		int32 CollisionEnabledCount = 0;
		int32 NoCollisionCount = 0;
		int32 QueryCollisionEnabledCount = 0;
		int32 PhysicsCollisionEnabledCount = 0;
		int32 ProbeCollisionEnabledCount = 0;
		int32 OverlapEventsEnabledCount = 0;
		int32 SimulatingPhysicsCount = 0;
		int32 GravityEnabledCount = 0;
		int32 BodyInstancePresentCount = 0;
		int32 BlockingResponseComponentCount = 0;
		int32 OverlapResponseComponentCount = 0;
		int32 IgnoreResponseComponentCount = 0;
		double TotalMassKg = 0.0;
		TMap<FString, int32> CollisionEnabledCounts;
		TMap<FString, int32> ObjectTypeCounts;
		TMap<FString, int32> ProfileCounts;
		TArray<TSharedPtr<FJsonValue>> ComponentsJson;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			++TotalActorCount;

			TArray<UPrimitiveComponent*> PrimitiveComponents;
			Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			TotalPrimitiveComponentCount += PrimitiveComponents.Num();

			if (!ActorMatchesClassFilter(Actor))
			{
				continue;
			}
			++ActorClassFilterMatchCount;

			for (UPrimitiveComponent* Component : PrimitiveComponents)
			{
				if (!Component || !ComponentMatchesFilters(Component))
				{
					continue;
				}

				++MatchedPrimitiveComponentCount;
				if (Component->IsRegistered()) ++RegisteredPrimitiveComponentCount;

				const ECollisionEnabled::Type CollisionEnabled = Component->GetCollisionEnabled();
				IncrementStringCounter(CollisionEnabledCounts, CollisionEnabledToString(CollisionEnabled));
				IncrementStringCounter(ObjectTypeCounts, CollisionChannelToString(Component->GetCollisionObjectType()));
				IncrementStringCounter(ProfileCounts, Component->GetCollisionProfileName().ToString());
				if (CollisionEnabled == ECollisionEnabled::NoCollision)
				{
					++NoCollisionCount;
				}
				else
				{
					++CollisionEnabledCount;
				}
				if (CollisionEnabledHasQuery(CollisionEnabled)) ++QueryCollisionEnabledCount;
				if (CollisionEnabledHasPhysics(CollisionEnabled)) ++PhysicsCollisionEnabledCount;
				if (CollisionEnabledHasProbe(CollisionEnabled)) ++ProbeCollisionEnabledCount;
				if (Component->GetGenerateOverlapEvents()) ++OverlapEventsEnabledCount;
				if (Component->IsSimulatingPhysics()) ++SimulatingPhysicsCount;
				if (Component->IsGravityEnabled()) ++GravityEnabledCount;

				FBodyInstance* BodyInstance = Component->GetBodyInstance();
				if (BodyInstance)
				{
					++BodyInstancePresentCount;
					TotalMassKg += Component->GetMass();
				}

				bool bHasBlockResponse = false;
				bool bHasOverlapResponse = false;
				bool bHasIgnoreResponse = false;
				const int32 MaxSerializableChannel = static_cast<int32>(ECC_OverlapAll_Deprecated);
				for (int32 ChannelIndex = 0; ChannelIndex < MaxSerializableChannel; ++ChannelIndex)
				{
					const ECollisionResponse Response = Component->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(ChannelIndex));
					bHasBlockResponse = bHasBlockResponse || Response == ECR_Block;
					bHasOverlapResponse = bHasOverlapResponse || Response == ECR_Overlap;
					bHasIgnoreResponse = bHasIgnoreResponse || Response == ECR_Ignore;
				}
				if (bHasBlockResponse) ++BlockingResponseComponentCount;
				if (bHasOverlapResponse) ++OverlapResponseComponentCount;
				if (bHasIgnoreResponse) ++IgnoreResponseComponentCount;

				if (bIncludeComponents && ComponentsJson.Num() < ComponentLimit)
				{
					ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildRuntimePrimitivePhysicsJson(Component, bIncludeResponses)));
				}
			}
		}

		TSharedPtr<FJsonObject> SummaryJson = MakeShared<FJsonObject>();
		SummaryJson->SetNumberField(TEXT("total_actor_count"), TotalActorCount);
		SummaryJson->SetNumberField(TEXT("actor_class_filter_match_count"), ActorClassFilterMatchCount);
		SummaryJson->SetNumberField(TEXT("total_primitive_component_count"), TotalPrimitiveComponentCount);
		SummaryJson->SetNumberField(TEXT("matched_primitive_component_count"), MatchedPrimitiveComponentCount);
		SummaryJson->SetNumberField(TEXT("registered_primitive_component_count"), RegisteredPrimitiveComponentCount);
		SummaryJson->SetNumberField(TEXT("collision_enabled_count"), CollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("no_collision_count"), NoCollisionCount);
		SummaryJson->SetNumberField(TEXT("query_collision_enabled_count"), QueryCollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("physics_collision_enabled_count"), PhysicsCollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("probe_collision_enabled_count"), ProbeCollisionEnabledCount);
		SummaryJson->SetNumberField(TEXT("overlap_events_enabled_count"), OverlapEventsEnabledCount);
		SummaryJson->SetNumberField(TEXT("simulating_physics_count"), SimulatingPhysicsCount);
		SummaryJson->SetNumberField(TEXT("gravity_enabled_count"), GravityEnabledCount);
		SummaryJson->SetNumberField(TEXT("body_instance_present_count"), BodyInstancePresentCount);
		SummaryJson->SetNumberField(TEXT("blocking_response_component_count"), BlockingResponseComponentCount);
		SummaryJson->SetNumberField(TEXT("overlap_response_component_count"), OverlapResponseComponentCount);
		SummaryJson->SetNumberField(TEXT("ignore_response_component_count"), IgnoreResponseComponentCount);
		SummaryJson->SetNumberField(TEXT("total_mass_kg"), TotalMassKg);
		SummaryJson->SetBoolField(TEXT("include_components"), bIncludeComponents);
		SummaryJson->SetBoolField(TEXT("include_responses"), bIncludeResponses);
		SummaryJson->SetNumberField(TEXT("component_limit"), ComponentLimit);
		SummaryJson->SetNumberField(TEXT("returned_component_count"), ComponentsJson.Num());
		SummaryJson->SetBoolField(TEXT("components_truncated"), bIncludeComponents && MatchedPrimitiveComponentCount > ComponentsJson.Num());
		SummaryJson->SetArrayField(TEXT("collision_enabled_counts"), BuildStringCounterJson(CollisionEnabledCounts, TEXT("collision_enabled")));
		SummaryJson->SetArrayField(TEXT("object_type_counts"), BuildStringCounterJson(ObjectTypeCounts, TEXT("object_type")));
		SummaryJson->SetArrayField(TEXT("profile_counts"), BuildStringCounterJson(ProfileCounts, TEXT("profile")));
		Data->SetObjectField(TEXT("summary"), SummaryJson);
		if (bIncludeComponents)
		{
			Data->SetArrayField(TEXT("components"), ComponentsJson);
		}

		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


}
