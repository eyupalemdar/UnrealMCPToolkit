// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/MCTRuntimeGameplay.h"

#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"

namespace MCPToolkit::RuntimeDiagnostics
{
namespace
{
TSharedPtr<FJsonObject> BuildGameplayTagContainerJson(const FGameplayTagContainer& Tags)
{
	TArray<FGameplayTag> TagArray;
	Tags.GetGameplayTagArray(TagArray);
	TagArray.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> TagValues;
	for (const FGameplayTag& Tag : TagArray)
	{
		TagValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("count"), TagArray.Num());
	Data->SetStringField(TEXT("text"), Tags.ToStringSimple());
	Data->SetArrayField(TEXT("items"), TagValues);
	return Data;
}

bool GameplayTagContainerMatchesFilter(const FGameplayTagContainer& Tags, const FString& TagFilter)
{
	if (TagFilter.IsEmpty())
	{
		return true;
	}

	TArray<FGameplayTag> TagArray;
	Tags.GetGameplayTagArray(TagArray);
	for (const FGameplayTag& Tag : TagArray)
	{
		if (Tag.ToString().Contains(TagFilter))
		{
			return true;
		}
	}
	return false;
}

TSharedPtr<FJsonObject> BuildGameplayTagDictionaryJson(const FString& TagFilter, int32 TagLimit)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	FGameplayTagContainer DictionaryTags;
	Manager.RequestAllGameplayTags(DictionaryTags, true);

	TArray<FGameplayTag> TagArray;
	DictionaryTags.GetGameplayTagArray(TagArray);
	TagArray.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> TagsJson;
	int32 MatchedTagCount = 0;
	for (const FGameplayTag& Tag : TagArray)
	{
		const FString TagText = Tag.ToString();
		if (!TagFilter.IsEmpty() && !TagText.Contains(TagFilter))
		{
			continue;
		}

		++MatchedTagCount;
		if (TagsJson.Num() < TagLimit)
		{
			TagsJson.Add(MakeShared<FJsonValueString>(TagText));
		}
	}

	TArray<FString> SourceSearchPaths;
	Manager.GetTagSourceSearchPaths(SourceSearchPaths);
	TArray<TSharedPtr<FJsonValue>> SourceSearchPathsJson;
	for (const FString& SourceSearchPath : SourceSearchPaths)
	{
		SourceSearchPathsJson.Add(MakeShared<FJsonValueString>(SourceSearchPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("tag_filter"), TagFilter);
	Data->SetNumberField(TEXT("dictionary_tag_count"), TagArray.Num());
	Data->SetNumberField(TEXT("matched_dictionary_tag_count"), MatchedTagCount);
	Data->SetNumberField(TEXT("returned_dictionary_tag_count"), TagsJson.Num());
	Data->SetNumberField(TEXT("tag_limit"), TagLimit);
	Data->SetBoolField(TEXT("dictionary_tags_truncated"), MatchedTagCount > TagsJson.Num());
	Data->SetArrayField(TEXT("dictionary_tags"), TagsJson);
	Data->SetNumberField(TEXT("tag_node_count"), Manager.GetNumGameplayTagNodes());
	Data->SetNumberField(TEXT("source_search_path_count"), SourceSearchPaths.Num());
	Data->SetArrayField(TEXT("source_search_paths"), SourceSearchPathsJson);
	Data->SetBoolField(TEXT("import_tags_from_ini"), Manager.ShouldImportTagsFromINI());
	Data->SetBoolField(TEXT("warn_on_invalid_tags"), Manager.ShouldWarnOnInvalidTags());
	Data->SetBoolField(TEXT("fast_replication"), Manager.ShouldUseFastReplication());
	Data->SetBoolField(TEXT("dynamic_replication"), Manager.ShouldUseDynamicReplication());
	return Data;
}

TSharedPtr<FJsonObject> BuildGameplayTagInterfaceObjectJson(UObject* Object, const FGameplayTagContainer& OwnedTags, const FString& ObjectType)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("object_type"), ObjectType);
	Data->SetObjectField(TEXT("object"), BuildObjectReferenceJson(Object));
	Data->SetObjectField(TEXT("owned_tags"), BuildGameplayTagContainerJson(OwnedTags));

	if (AActor* Actor = Cast<AActor>(Object))
	{
		Data->SetObjectField(TEXT("actor"), BuildActorJson(Actor));
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		Data->SetObjectField(TEXT("component"), BuildComponentJson(Component));
		if (AActor* Owner = Component->GetOwner())
		{
			Data->SetObjectField(TEXT("owner_actor"), BuildActorJson(Owner));
		}
	}

	return Data;
}

FString GameplayEffectReplicationModeToString(EGameplayEffectReplicationMode Mode)
{
	switch (Mode)
	{
	case EGameplayEffectReplicationMode::Minimal:
		return TEXT("Minimal");
	case EGameplayEffectReplicationMode::Mixed:
		return TEXT("Mixed");
	case EGameplayEffectReplicationMode::Full:
		return TEXT("Full");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityInstancingPolicyToString(EGameplayAbilityInstancingPolicy::Type Policy)
{
	switch (static_cast<int32>(Policy))
	{
	case 0:
		return TEXT("NonInstanced");
	case 1:
		return TEXT("InstancedPerActor");
	case 2:
		return TEXT("InstancedPerExecution");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityReplicationPolicyToString(EGameplayAbilityReplicationPolicy::Type Policy)
{
	switch (Policy)
	{
	case EGameplayAbilityReplicationPolicy::ReplicateNo:
		return TEXT("ReplicateNo");
	case EGameplayAbilityReplicationPolicy::ReplicateYes:
		return TEXT("ReplicateYes");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityNetExecutionPolicyToString(EGameplayAbilityNetExecutionPolicy::Type Policy)
{
	switch (Policy)
	{
	case EGameplayAbilityNetExecutionPolicy::LocalPredicted:
		return TEXT("LocalPredicted");
	case EGameplayAbilityNetExecutionPolicy::LocalOnly:
		return TEXT("LocalOnly");
	case EGameplayAbilityNetExecutionPolicy::ServerInitiated:
		return TEXT("ServerInitiated");
	case EGameplayAbilityNetExecutionPolicy::ServerOnly:
		return TEXT("ServerOnly");
	default:
		return TEXT("Unknown");
	}
}

FString GameplayAbilityNetSecurityPolicyToString(EGameplayAbilityNetSecurityPolicy::Type Policy)
{
	switch (Policy)
	{
	case EGameplayAbilityNetSecurityPolicy::ClientOrServer:
		return TEXT("ClientOrServer");
	case EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution:
		return TEXT("ServerOnlyExecution");
	case EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination:
		return TEXT("ServerOnlyTermination");
	case EGameplayAbilityNetSecurityPolicy::ServerOnly:
		return TEXT("ServerOnly");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildGameplayAbilitySpecJson(const FGameplayAbilitySpec& Spec)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("handle"), Spec.Handle.ToString());
	Data->SetNumberField(TEXT("level"), Spec.Level);
	Data->SetNumberField(TEXT("input_id"), Spec.InputID);
	Data->SetNumberField(TEXT("active_count"), Spec.ActiveCount);
	Data->SetBoolField(TEXT("active"), Spec.IsActive());
	Data->SetBoolField(TEXT("input_pressed"), Spec.InputPressed != 0);
	Data->SetBoolField(TEXT("remove_after_activation"), Spec.RemoveAfterActivation != 0);
	Data->SetBoolField(TEXT("pending_remove"), Spec.PendingRemove != 0);
	Data->SetBoolField(TEXT("activate_once"), Spec.bActivateOnce != 0);
	Data->SetObjectField(TEXT("source_object"), BuildObjectReferenceJson(Spec.SourceObject.Get()));
	Data->SetObjectField(TEXT("dynamic_source_tags"), BuildGameplayTagContainerJson(Spec.GetDynamicSpecSourceTags()));
	Data->SetNumberField(TEXT("replicated_instance_count"), Spec.ReplicatedInstances.Num());
	Data->SetNumberField(TEXT("non_replicated_instance_count"), Spec.NonReplicatedInstances.Num());
	Data->SetNumberField(TEXT("ability_instance_count"), Spec.GetAbilityInstances().Num());
	Data->SetStringField(TEXT("gameplay_effect_handle"), Spec.GameplayEffectHandle.ToString());

	UGameplayAbility* Ability = Spec.Ability.Get();
	Data->SetObjectField(TEXT("ability"), BuildObjectReferenceJson(Ability));
	if (Ability)
	{
		Data->SetStringField(TEXT("ability_class"), Ability->GetClass() ? Ability->GetClass()->GetPathName() : TEXT(""));
		Data->SetStringField(TEXT("instancing_policy"), GameplayAbilityInstancingPolicyToString(Ability->GetInstancingPolicy()));
		Data->SetStringField(TEXT("replication_policy"), GameplayAbilityReplicationPolicyToString(Ability->GetReplicationPolicy()));
		Data->SetStringField(TEXT("net_execution_policy"), GameplayAbilityNetExecutionPolicyToString(Ability->GetNetExecutionPolicy()));
		Data->SetStringField(TEXT("net_security_policy"), GameplayAbilityNetSecurityPolicyToString(Ability->GetNetSecurityPolicy()));
		Data->SetBoolField(TEXT("replicate_input_directly"), Ability->bReplicateInputDirectly != 0);
		Data->SetObjectField(TEXT("asset_tags"), BuildGameplayTagContainerJson(Ability->GetAssetTags()));
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildActiveGameplayEffectJson(const FActiveGameplayEffect& ActiveEffect, UWorld* World)
{
	const FGameplayEffectSpec& Spec = ActiveEffect.Spec;
	const UGameplayEffect* EffectDef = Spec.Def.Get();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("handle"), ActiveEffect.Handle.ToString());
	Data->SetBoolField(TEXT("handle_valid"), ActiveEffect.Handle.IsValid());
	Data->SetObjectField(TEXT("effect"), BuildObjectReferenceJson(EffectDef));
	Data->SetNumberField(TEXT("level"), Spec.GetLevel());
	Data->SetNumberField(TEXT("stack_count"), Spec.GetStackCount());
	Data->SetNumberField(TEXT("duration"), ActiveEffect.GetDuration());
	Data->SetNumberField(TEXT("period"), ActiveEffect.GetPeriod());
	Data->SetNumberField(TEXT("start_world_time"), ActiveEffect.StartWorldTime);
	Data->SetNumberField(TEXT("start_server_world_time"), ActiveEffect.StartServerWorldTime);
	Data->SetNumberField(TEXT("end_time"), ActiveEffect.GetEndTime());
	if (World)
	{
		Data->SetNumberField(TEXT("time_remaining"), ActiveEffect.GetTimeRemaining(World->GetTimeSeconds()));
	}
	Data->SetBoolField(TEXT("inhibited"), ActiveEffect.bIsInhibited);
	Data->SetBoolField(TEXT("pending_remove"), ActiveEffect.IsPendingRemove);
	Data->SetBoolField(TEXT("post_predict_object"), ActiveEffect.bPostPredictObject);
	Data->SetNumberField(TEXT("granted_ability_handle_count"), ActiveEffect.GrantedAbilityHandles.Num());

	FGameplayTagContainer AssetTags;
	Spec.GetAllAssetTags(AssetTags);
	FGameplayTagContainer GrantedTags;
	Spec.GetAllGrantedTags(GrantedTags);
	FGameplayTagContainer BlockedAbilityTags;
	Spec.GetAllBlockedAbilityTags(BlockedAbilityTags);
	Data->SetObjectField(TEXT("asset_tags"), BuildGameplayTagContainerJson(AssetTags));
	Data->SetObjectField(TEXT("granted_tags"), BuildGameplayTagContainerJson(GrantedTags));
	Data->SetObjectField(TEXT("blocked_ability_tags"), BuildGameplayTagContainerJson(BlockedAbilityTags));
	Data->SetObjectField(TEXT("dynamic_asset_tags"), BuildGameplayTagContainerJson(Spec.GetDynamicAssetTags()));
	Data->SetObjectField(TEXT("dynamic_granted_tags"), BuildGameplayTagContainerJson(Spec.DynamicGrantedTags));

	if (EffectDef)
	{
		Data->SetObjectField(TEXT("definition_asset_tags"), BuildGameplayTagContainerJson(EffectDef->GetAssetTags()));
		Data->SetObjectField(TEXT("definition_granted_tags"), BuildGameplayTagContainerJson(EffectDef->GetGrantedTags()));
		Data->SetObjectField(TEXT("definition_blocked_ability_tags"), BuildGameplayTagContainerJson(EffectDef->GetBlockedAbilityTags()));
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildAbilitySystemComponentJson(
	UAbilitySystemComponent* AbilitySystem,
	UWorld* World,
	bool bIncludeAbilities,
	bool bIncludeEffects,
	bool bIncludeAttributes,
	int32 AbilityLimit,
	int32 EffectLimit,
	int32 AttributeLimit)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(AbilitySystem);
	if (!AbilitySystem)
	{
		return Data;
	}

	Data->SetStringField(TEXT("replication_mode"), GameplayEffectReplicationModeToString(AbilitySystem->ReplicationMode));
	Data->SetNumberField(TEXT("generic_confirm_input_id"), AbilitySystem->GenericConfirmInputID);
	Data->SetNumberField(TEXT("generic_cancel_input_id"), AbilitySystem->GenericCancelInputID);
	Data->SetObjectField(TEXT("owner_actor"), BuildActorJson(AbilitySystem->GetOwnerActor()));
	Data->SetObjectField(TEXT("avatar_actor"), BuildActorJson(AbilitySystem->GetAvatarActor()));
	Data->SetObjectField(TEXT("owned_tags"), BuildGameplayTagContainerJson(AbilitySystem->GetOwnedGameplayTags()));

	FGameplayTagContainer BlockedAbilityTags;
	AbilitySystem->GetBlockedAbilityTags(BlockedAbilityTags);
	Data->SetObjectField(TEXT("blocked_ability_tags"), BuildGameplayTagContainerJson(BlockedAbilityTags));

	TArray<TSharedPtr<FJsonValue>> AttributeSetJson;
	for (const auto& AttributeSetObject : AbilitySystem->GetSpawnedAttributes())
	{
		const UAttributeSet* AttributeSet = AttributeSetObject;
		AttributeSetJson.Add(MakeShared<FJsonValueObject>(BuildObjectReferenceJson(AttributeSet)));
	}
	Data->SetNumberField(TEXT("attribute_set_count"), AttributeSetJson.Num());
	Data->SetArrayField(TEXT("attribute_sets"), AttributeSetJson);

	const TArray<FGameplayAbilitySpec>& Abilities = AbilitySystem->GetActivatableAbilities();
	int32 ActiveAbilityCount = 0;
	for (const FGameplayAbilitySpec& Spec : Abilities)
	{
		if (Spec.IsActive())
		{
			++ActiveAbilityCount;
		}
	}
	Data->SetNumberField(TEXT("ability_count"), Abilities.Num());
	Data->SetNumberField(TEXT("active_ability_count"), ActiveAbilityCount);
	Data->SetBoolField(TEXT("include_abilities"), bIncludeAbilities);
	if (bIncludeAbilities)
	{
		TArray<TSharedPtr<FJsonValue>> AbilityJson;
		for (const FGameplayAbilitySpec& Spec : Abilities)
		{
			if (AbilityJson.Num() >= AbilityLimit)
			{
				break;
			}
			AbilityJson.Add(MakeShared<FJsonValueObject>(BuildGameplayAbilitySpecJson(Spec)));
		}
		Data->SetNumberField(TEXT("returned_ability_count"), AbilityJson.Num());
		Data->SetBoolField(TEXT("abilities_truncated"), Abilities.Num() > AbilityJson.Num());
		Data->SetArrayField(TEXT("abilities"), AbilityJson);
	}

	const FActiveGameplayEffectsContainer& ActiveEffects = AbilitySystem->GetActiveGameplayEffects();
	const int32 ActiveEffectCount = ActiveEffects.GetNumGameplayEffects();
	Data->SetNumberField(TEXT("active_effect_count"), ActiveEffectCount);
	Data->SetBoolField(TEXT("include_effects"), bIncludeEffects);
	if (bIncludeEffects)
	{
		TArray<TSharedPtr<FJsonValue>> EffectJson;
		for (auto It = ActiveEffects.CreateConstIterator(); It && EffectJson.Num() < EffectLimit; ++It)
		{
			const FActiveGameplayEffect& ActiveEffect = *It;
			EffectJson.Add(MakeShared<FJsonValueObject>(BuildActiveGameplayEffectJson(ActiveEffect, World)));
		}
		Data->SetNumberField(TEXT("returned_effect_count"), EffectJson.Num());
		Data->SetBoolField(TEXT("effects_truncated"), ActiveEffectCount > EffectJson.Num());
		Data->SetArrayField(TEXT("active_effects"), EffectJson);
	}

	TArray<FGameplayAttribute> Attributes;
	if (bIncludeAttributes)
	{
		AbilitySystem->GetAllAttributes(Attributes);
	}
	Data->SetNumberField(TEXT("attribute_count"), Attributes.Num());
	Data->SetBoolField(TEXT("include_attributes"), bIncludeAttributes);
	if (bIncludeAttributes)
	{
		TArray<TSharedPtr<FJsonValue>> AttributeJson;
		for (const FGameplayAttribute& Attribute : Attributes)
		{
			if (AttributeJson.Num() >= AttributeLimit)
			{
				break;
			}

			TSharedPtr<FJsonObject> AttributeData = MakeShared<FJsonObject>();
			AttributeData->SetStringField(TEXT("name"), Attribute.GetName());
			if (FProperty* AttributeProperty = Attribute.GetUProperty())
			{
				AttributeData->SetStringField(TEXT("property_path"), AttributeProperty->GetPathName());
				UClass* AttributeSetClass = Attribute.GetAttributeSetClass();
				AttributeData->SetStringField(TEXT("attribute_set_class"), AttributeSetClass ? AttributeSetClass->GetPathName() : TEXT(""));
			}

			bool bFound = false;
			const float CurrentValue = AbilitySystem->GetGameplayAttributeValue(Attribute, bFound);
			AttributeData->SetBoolField(TEXT("found"), bFound);
			AttributeData->SetNumberField(TEXT("current_value"), CurrentValue);
			if (bFound)
			{
				AttributeData->SetNumberField(TEXT("base_value"), AbilitySystem->GetNumericAttributeBase(Attribute));
			}
			AttributeJson.Add(MakeShared<FJsonValueObject>(AttributeData));
		}
		Data->SetNumberField(TEXT("returned_attribute_count"), AttributeJson.Num());
		Data->SetBoolField(TEXT("attributes_truncated"), Attributes.Num() > AttributeJson.Num());
		Data->SetArrayField(TEXT("attributes"), AttributeJson);
	}

	return Data;
}


}

TSharedPtr<FJsonObject> BuildGameplayTagsDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	FString ComponentClassFilter;
	FString TagFilter;
	bool bIncludeDictionary = true;
	bool bIncludeComponents = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Params->TryGetStringField(TEXT("tag_filter"), TagFilter);
		Params->TryGetBoolField(TEXT("include_dictionary"), bIncludeDictionary);
		Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	ComponentClassFilter.TrimStartAndEndInline();
	TagFilter.TrimStartAndEndInline();
	const int32 ActorLimit = ReadClampedIntField(Params, TEXT("actor_limit"), 100, 0, 1000);
	const int32 ComponentLimit = ReadClampedIntField(Params, TEXT("component_limit"), 200, 0, 2000);
	const int32 TagLimit = ReadClampedIntField(Params, TEXT("tag_limit"), 500, 0, 5000);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("requested_world"), WorldSelector);
		Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
		Data->SetStringField(TEXT("tag_filter"), TagFilter);
		Data->SetBoolField(TEXT("include_dictionary"), bIncludeDictionary);
		Data->SetBoolField(TEXT("include_components"), bIncludeComponents);
		Data->SetNumberField(TEXT("actor_limit"), ActorLimit);
		Data->SetNumberField(TEXT("component_limit"), ComponentLimit);
		Data->SetNumberField(TEXT("tag_limit"), TagLimit);

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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; gameplay tag diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		if (bIncludeDictionary)
		{
			Data->SetObjectField(TEXT("tag_manager"), BuildGameplayTagDictionaryJson(TagFilter, TagLimit));
		}

		auto ActorMatchesFilters = [&NameFilter, &ClassFilter](AActor* Actor)
		{
			if (!Actor)
			{
				return false;
			}
			if (!NameFilter.IsEmpty()
				&& !Actor->GetName().Contains(NameFilter)
				&& !Actor->GetActorLabel().Contains(NameFilter)
				&& !Actor->GetPathName().Contains(NameFilter))
			{
				return false;
			}
			const FString ActorClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
			if (!ClassFilter.IsEmpty() && !ActorClassPath.Contains(ClassFilter))
			{
				return false;
			}
			return true;
		};

		auto ComponentMatchesFilters = [&NameFilter, &ComponentClassFilter](UActorComponent* Component)
		{
			if (!Component)
			{
				return false;
			}
			if (!NameFilter.IsEmpty()
				&& !Component->GetName().Contains(NameFilter)
				&& !Component->GetPathName().Contains(NameFilter))
			{
				return false;
			}
			const FString ComponentClassPath = Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT("");
			if (!ComponentClassFilter.IsEmpty() && !ComponentClassPath.Contains(ComponentClassFilter))
			{
				return false;
			}
			return true;
		};

		TArray<AActor*> ActorsToInspect;
		const bool bActorTargetRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		if (bActorTargetRequested)
		{
			if (AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName))
			{
				ActorsToInspect.Add(Actor);
			}
			else
			{
				Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor was not found in the selected world")));
			}
		}
		else
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				ActorsToInspect.Add(*It);
			}
		}

		int32 InspectedActorCount = 0;
		int32 ActorInterfaceCount = 0;
		int32 MatchedActorInterfaceCount = 0;
		int32 ComponentInterfaceCount = 0;
		int32 MatchedComponentInterfaceCount = 0;
		TArray<TSharedPtr<FJsonValue>> ActorsJson;
		TArray<TSharedPtr<FJsonValue>> ComponentsJson;

		for (AActor* Actor : ActorsToInspect)
		{
			if (!Actor)
			{
				continue;
			}
			++InspectedActorCount;

			if (IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Actor))
			{
				++ActorInterfaceCount;
				FGameplayTagContainer OwnedTags;
				TagInterface->GetOwnedGameplayTags(OwnedTags);
				if (ActorMatchesFilters(Actor) && GameplayTagContainerMatchesFilter(OwnedTags, TagFilter))
				{
					++MatchedActorInterfaceCount;
					if (ActorsJson.Num() < ActorLimit)
					{
						ActorsJson.Add(MakeShared<FJsonValueObject>(BuildGameplayTagInterfaceObjectJson(Actor, OwnedTags, TEXT("actor"))));
					}
				}
			}

			if (!bIncludeComponents)
			{
				continue;
			}

			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}
				if (IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Component))
				{
					++ComponentInterfaceCount;
					FGameplayTagContainer OwnedTags;
					TagInterface->GetOwnedGameplayTags(OwnedTags);
					if (ComponentMatchesFilters(Component) && GameplayTagContainerMatchesFilter(OwnedTags, TagFilter))
					{
						++MatchedComponentInterfaceCount;
						if (ComponentsJson.Num() < ComponentLimit)
						{
							ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildGameplayTagInterfaceObjectJson(Component, OwnedTags, TEXT("component"))));
						}
					}
				}
			}
		}

		TSharedPtr<FJsonObject> TagAssets = MakeShared<FJsonObject>();
		TagAssets->SetNumberField(TEXT("inspected_actor_count"), InspectedActorCount);
		TagAssets->SetNumberField(TEXT("actor_interface_count"), ActorInterfaceCount);
		TagAssets->SetNumberField(TEXT("matched_actor_interface_count"), MatchedActorInterfaceCount);
		TagAssets->SetNumberField(TEXT("returned_actor_count"), ActorsJson.Num());
		TagAssets->SetBoolField(TEXT("actors_truncated"), MatchedActorInterfaceCount > ActorsJson.Num());
		TagAssets->SetArrayField(TEXT("actors"), ActorsJson);
		TagAssets->SetNumberField(TEXT("component_interface_count"), ComponentInterfaceCount);
		TagAssets->SetNumberField(TEXT("matched_component_interface_count"), MatchedComponentInterfaceCount);
		TagAssets->SetNumberField(TEXT("returned_component_count"), ComponentsJson.Num());
		TagAssets->SetBoolField(TEXT("components_truncated"), MatchedComponentInterfaceCount > ComponentsJson.Num());
		TagAssets->SetArrayField(TEXT("components"), ComponentsJson);
		Data->SetObjectField(TEXT("tag_assets"), TagAssets);
		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


TSharedPtr<FJsonObject> BuildAbilitySystemDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ClassFilter;
	bool bIncludeAbilities = true;
	bool bIncludeEffects = true;
	bool bIncludeAttributes = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("include_abilities"), bIncludeAbilities);
		Params->TryGetBoolField(TEXT("include_effects"), bIncludeEffects);
		Params->TryGetBoolField(TEXT("include_attributes"), bIncludeAttributes);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();
	const int32 ActorLimit = ReadClampedIntField(Params, TEXT("actor_limit"), 100, 1, 1000);
	const int32 AbilityLimit = ReadClampedIntField(Params, TEXT("ability_limit"), 100, 0, 1000);
	const int32 EffectLimit = ReadClampedIntField(Params, TEXT("effect_limit"), 100, 0, 1000);
	const int32 AttributeLimit = ReadClampedIntField(Params, TEXT("attribute_limit"), 100, 0, 1000);
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
			Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; ability system diagnostics reflect the editor world")));
		}

		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetStringField(TEXT("world_net_mode"), NetModeToString(World->GetNetMode()));
		Data->SetBoolField(TEXT("include_abilities"), bIncludeAbilities);
		Data->SetBoolField(TEXT("include_effects"), bIncludeEffects);
		Data->SetBoolField(TEXT("include_attributes"), bIncludeAttributes);
		Data->SetNumberField(TEXT("actor_limit"), ActorLimit);
		Data->SetNumberField(TEXT("ability_limit"), AbilityLimit);
		Data->SetNumberField(TEXT("effect_limit"), EffectLimit);
		Data->SetNumberField(TEXT("attribute_limit"), AttributeLimit);

		const bool bSpecificActorRequested = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
		TArray<AActor*> ActorsToInspect;
		int32 MatchedActorCount = 0;
		int32 AbilitySystemActorCount = 0;
		int32 AbilitySystemComponentCount = 0;
		int32 TotalAbilityCount = 0;
		int32 TotalActiveAbilityCount = 0;
		int32 TotalActiveEffectCount = 0;
		int32 TotalAttributeSetCount = 0;
		int32 TotalAttributeCount = 0;

		auto AccumulateAbilitySystemCounters = [&AbilitySystemActorCount, &AbilitySystemComponentCount, &TotalAbilityCount, &TotalActiveAbilityCount, &TotalActiveEffectCount, &TotalAttributeSetCount, &TotalAttributeCount](AActor* Actor)
		{
			if (!Actor)
			{
				return 0;
			}

			TArray<UAbilitySystemComponent*> AbilitySystems;
			Actor->GetComponents<UAbilitySystemComponent>(AbilitySystems);
			if (AbilitySystems.IsEmpty())
			{
				return 0;
			}

			++AbilitySystemActorCount;
			for (UAbilitySystemComponent* AbilitySystem : AbilitySystems)
			{
				if (!AbilitySystem)
				{
					continue;
				}

				++AbilitySystemComponentCount;
				const TArray<FGameplayAbilitySpec>& Abilities = AbilitySystem->GetActivatableAbilities();
				TotalAbilityCount += Abilities.Num();
				for (const FGameplayAbilitySpec& Spec : Abilities)
				{
					if (Spec.IsActive())
					{
						++TotalActiveAbilityCount;
					}
				}
				TotalActiveEffectCount += AbilitySystem->GetActiveGameplayEffects().GetNumGameplayEffects();
				TotalAttributeSetCount += AbilitySystem->GetSpawnedAttributes().Num();
				TArray<FGameplayAttribute> Attributes;
				AbilitySystem->GetAllAttributes(Attributes);
				TotalAttributeCount += Attributes.Num();
			}

			return AbilitySystems.Num();
		};

		if (bSpecificActorRequested)
		{
			AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName);
			Data->SetBoolField(TEXT("selected_actor_requested"), true);
			Data->SetBoolField(TEXT("selected_actor_found"), Actor != nullptr);
			if (Actor)
			{
				MatchedActorCount = 1;
				const int32 AbilitySystemCount = AccumulateAbilitySystemCounters(Actor);
				ActorsToInspect.Add(Actor);
				if (AbilitySystemCount == 0)
				{
					Warnings.Add(MakeShared<FJsonValueString>(TEXT("Selected actor has no AbilitySystemComponent")));
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
				const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : TEXT("");
				if (!NameFilter.IsEmpty() && !Name.Contains(NameFilter) && !Label.Contains(NameFilter))
				{
					continue;
				}
				if (!ClassFilter.IsEmpty() && !ClassPath.Contains(ClassFilter))
				{
					continue;
				}

				++MatchedActorCount;
				const int32 AbilitySystemCount = AccumulateAbilitySystemCounters(Actor);
				if (AbilitySystemCount > 0 && ActorsToInspect.Num() < ActorLimit)
				{
					ActorsToInspect.Add(Actor);
				}
			}
		}

		TArray<TSharedPtr<FJsonValue>> ActorsJson;
		for (AActor* Actor : ActorsToInspect)
		{
			if (!Actor)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ActorJson = BuildActorJson(Actor);
			TArray<UAbilitySystemComponent*> AbilitySystems;
			Actor->GetComponents<UAbilitySystemComponent>(AbilitySystems);
			TArray<TSharedPtr<FJsonValue>> AbilitySystemJson;
			for (UAbilitySystemComponent* AbilitySystem : AbilitySystems)
			{
				if (!AbilitySystem)
				{
					continue;
				}
				AbilitySystemJson.Add(MakeShared<FJsonValueObject>(BuildAbilitySystemComponentJson(AbilitySystem, World, bIncludeAbilities, bIncludeEffects, bIncludeAttributes, AbilityLimit, EffectLimit, AttributeLimit)));
			}

			ActorJson->SetNumberField(TEXT("ability_system_component_count"), AbilitySystemJson.Num());
			ActorJson->SetArrayField(TEXT("ability_system_components"), AbilitySystemJson);
			ActorsJson.Add(MakeShared<FJsonValueObject>(ActorJson));
		}

		Data->SetNumberField(TEXT("matched_actor_count"), MatchedActorCount);
		Data->SetNumberField(TEXT("ability_system_actor_count"), AbilitySystemActorCount);
		Data->SetNumberField(TEXT("ability_system_component_count"), AbilitySystemComponentCount);
		Data->SetNumberField(TEXT("total_ability_count"), TotalAbilityCount);
		Data->SetNumberField(TEXT("total_active_ability_count"), TotalActiveAbilityCount);
		Data->SetNumberField(TEXT("total_active_effect_count"), TotalActiveEffectCount);
		Data->SetNumberField(TEXT("total_attribute_set_count"), TotalAttributeSetCount);
		Data->SetNumberField(TEXT("total_attribute_count"), TotalAttributeCount);
		Data->SetNumberField(TEXT("returned_actor_count"), ActorsJson.Num());
		Data->SetBoolField(TEXT("actors_truncated"), !bSpecificActorRequested && AbilitySystemActorCount > ActorsJson.Num());
		Data->SetArrayField(TEXT("actors"), ActorsJson);
		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
}


}
