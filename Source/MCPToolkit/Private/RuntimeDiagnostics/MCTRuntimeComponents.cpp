// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/MCTRuntimeComponents.h"

#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Components/ActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace MCPToolkit::RuntimeDiagnostics
{
namespace
{
FString MobilityToString(const EComponentMobility::Type Mobility)
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
		return TEXT("Unknown");
	}
}

void AddSceneComponentBasics(TSharedPtr<FJsonObject> Data, USceneComponent* SceneComponent)
{
	if (!Data.IsValid())
	{
		return;
	}

	Data->SetBoolField(TEXT("scene_component"), SceneComponent != nullptr);
	if (!SceneComponent)
	{
		return;
	}

	Data->SetBoolField(TEXT("visible"), SceneComponent->IsVisible());
	Data->SetStringField(TEXT("mobility"), MobilityToString(SceneComponent->Mobility));

	TArray<USceneComponent*> Children;
	SceneComponent->GetChildrenComponents(false, Children);
	Data->SetNumberField(TEXT("child_count"), Children.Num());
}

void AddSceneComponentDetails(TSharedPtr<FJsonObject> Data, USceneComponent* SceneComponent)
{
	AddSceneComponentBasics(Data, SceneComponent);
	if (!Data.IsValid() || !SceneComponent)
	{
		return;
	}

	Data->SetObjectField(TEXT("relative_location"), BuildVectorJson(SceneComponent->GetRelativeLocation()));
	Data->SetObjectField(TEXT("relative_rotation"), BuildRotatorJson(SceneComponent->GetRelativeRotation()));
	Data->SetObjectField(TEXT("relative_scale"), BuildVectorJson(SceneComponent->GetRelativeScale3D()));
	Data->SetObjectField(TEXT("world_location"), BuildVectorJson(SceneComponent->GetComponentLocation()));
	Data->SetObjectField(TEXT("world_rotation"), BuildRotatorJson(SceneComponent->GetComponentRotation()));
	Data->SetObjectField(TEXT("world_scale"), BuildVectorJson(SceneComponent->GetComponentScale()));
	Data->SetObjectField(TEXT("attach_parent"), BuildObjectReferenceJson(SceneComponent->GetAttachParent()));
	Data->SetStringField(TEXT("attach_socket"), SceneComponent->GetAttachSocketName().ToString());

	if (const UInstancedStaticMeshComponent* InstancedStaticMesh = Cast<UInstancedStaticMeshComponent>(SceneComponent))
	{
		Data->SetNumberField(TEXT("instance_count"), InstancedStaticMesh->GetInstanceCount());
	}

	if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent))
	{
		Data->SetObjectField(TEXT("static_mesh"), BuildObjectReferenceJson(StaticMeshComponent->GetStaticMesh()));
	}
	else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(SceneComponent))
	{
		Data->SetObjectField(TEXT("skeletal_mesh"), BuildObjectReferenceJson(SkeletalMeshComponent->GetSkeletalMeshAsset()));
	}
}

TSharedPtr<FJsonObject> BuildComponentListItemJson(UActorComponent* Component, bool bIncludeSceneDetails)
{
	TSharedPtr<FJsonObject> Data = BuildComponentJson(Component);
	if (!Component || !bIncludeSceneDetails)
	{
		return Data;
	}

	AddSceneComponentDetails(Data, Cast<USceneComponent>(Component));
	return Data;
}

TSharedPtr<FJsonObject> BuildSceneHierarchyJson(
	USceneComponent* Component,
	bool bIncludeSceneDetails,
	int32 Depth,
	int32 MaxDepth,
	int32 ComponentLimit,
	int32& ReturnedComponentCount,
	bool& bTruncated)
{
	if (!Component)
	{
		return nullptr;
	}

	if (ReturnedComponentCount >= ComponentLimit)
	{
		bTruncated = true;
		return nullptr;
	}

	++ReturnedComponentCount;
	TSharedPtr<FJsonObject> Data = BuildComponentListItemJson(Component, bIncludeSceneDetails);
	if (bIncludeSceneDetails)
	{
		AddSceneComponentDetails(Data, Component);
	}
	else
	{
		AddSceneComponentBasics(Data, Component);
	}

	TArray<USceneComponent*> Children;
	Component->GetChildrenComponents(false, Children);
	if (Children.Num() == 0)
	{
		return Data;
	}

	if (Depth >= MaxDepth)
	{
		Data->SetBoolField(TEXT("children_truncated_by_depth"), true);
		bTruncated = true;
		return Data;
	}

	TArray<TSharedPtr<FJsonValue>> ChildrenJson;
	for (USceneComponent* Child : Children)
	{
		TSharedPtr<FJsonObject> ChildJson = BuildSceneHierarchyJson(
			Child,
			bIncludeSceneDetails,
			Depth + 1,
			MaxDepth,
			ComponentLimit,
			ReturnedComponentCount,
			bTruncated);
		if (ChildJson.IsValid())
		{
			ChildrenJson.Add(MakeShared<FJsonValueObject>(ChildJson));
		}
	}

	Data->SetArrayField(TEXT("children"), ChildrenJson);
	Data->SetNumberField(TEXT("returned_child_count"), ChildrenJson.Num());
	Data->SetBoolField(TEXT("children_truncated"), Children.Num() > ChildrenJson.Num());
	if (Children.Num() > ChildrenJson.Num())
	{
		bTruncated = true;
	}
	return Data;
}
}

TSharedPtr<FJsonObject> BuildComponentList(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString NameFilter;
	FString ActorClassFilter;
	FString ComponentClassFilter;
	bool bIncludeSceneDetails = false;
	bool bIncludeHierarchy = false;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor_path"), ActorPath);
		Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Params->TryGetStringField(TEXT("actor_name"), ActorName);
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("actor_class_filter"), ActorClassFilter);
		Params->TryGetStringField(TEXT("component_class_filter"), ComponentClassFilter);
		Params->TryGetBoolField(TEXT("include_scene_details"), bIncludeSceneDetails);
		Params->TryGetBoolField(TEXT("include_hierarchy"), bIncludeHierarchy);
	}
	ActorPath.TrimStartAndEndInline();
	ActorLabel.TrimStartAndEndInline();
	ActorName.TrimStartAndEndInline();
	NameFilter.TrimStartAndEndInline();
	ActorClassFilter.TrimStartAndEndInline();
	ComponentClassFilter.TrimStartAndEndInline();

	const int32 Limit = ReadClampedIntField(Params, TEXT("limit"), 500, 1, 5000);
	const int32 HierarchyDepth = ReadClampedIntField(Params, TEXT("hierarchy_depth"), 16, 0, 64);
	const int32 HierarchyActorLimit = ReadClampedIntField(Params, TEXT("hierarchy_actor_limit"), 10, 1, 200);
	const int32 HierarchyComponentLimit = ReadClampedIntField(Params, TEXT("hierarchy_component_limit"), 250, 1, 5000);

	FString WorldSource;
	UWorld* World = SelectWorld(WorldSelector, WorldSource);
	if (!World)
	{
		return nullptr;
	}

	TArray<AActor*> ActorsToInspect;
	const bool bHasSpecificActor = !ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty();
	if (bHasSpecificActor)
	{
		AActor* Actor = FindActor(World, ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			return nullptr;
		}
		ActorsToInspect.Add(Actor);
	}
	else
	{
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
			if (!ActorClassFilter.IsEmpty() && !ClassPath.Contains(ActorClassFilter))
			{
				continue;
			}
			ActorsToInspect.Add(Actor);
		}
	}

	TArray<TSharedPtr<FJsonValue>> ComponentsJson;
	int32 MatchedComponentCount = 0;
	int32 InspectedActorCount = 0;
	for (AActor* Actor : ActorsToInspect)
	{
		if (!Actor)
		{
			continue;
		}
		++InspectedActorCount;

		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			const FString ComponentClassPath = Component->GetClass() ? Component->GetClass()->GetPathName() : TEXT("");
			if (!ComponentClassFilter.IsEmpty() && !ComponentClassPath.Contains(ComponentClassFilter))
			{
				continue;
			}

			++MatchedComponentCount;
			if (ComponentsJson.Num() < Limit)
			{
				ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildComponentListItemJson(Component, bIncludeSceneDetails)));
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("world_source"), WorldSource);
	Data->SetStringField(TEXT("world_name"), World->GetName());
	Data->SetBoolField(TEXT("include_scene_details"), bIncludeSceneDetails);
	Data->SetBoolField(TEXT("include_hierarchy"), bIncludeHierarchy);
	Data->SetNumberField(TEXT("inspected_actor_count"), InspectedActorCount);
	Data->SetNumberField(TEXT("matched_component_count"), MatchedComponentCount);
	Data->SetNumberField(TEXT("count"), ComponentsJson.Num());
	Data->SetBoolField(TEXT("truncated"), MatchedComponentCount > ComponentsJson.Num());
	Data->SetArrayField(TEXT("components"), ComponentsJson);

	if (bIncludeHierarchy)
	{
		TArray<TSharedPtr<FJsonValue>> HierarchiesJson;
		int32 HierarchyActorCount = 0;
		int32 HierarchyComponentCount = 0;
		bool bHierarchyTruncated = false;

		for (AActor* Actor : ActorsToInspect)
		{
			if (!Actor)
			{
				continue;
			}

			if (HierarchyActorCount >= HierarchyActorLimit)
			{
				bHierarchyTruncated = true;
				break;
			}

			TSharedPtr<FJsonObject> ActorHierarchyJson = MakeShared<FJsonObject>();
			ActorHierarchyJson->SetObjectField(TEXT("actor"), BuildActorJson(Actor));
			if (USceneComponent* RootComponent = Actor->GetRootComponent())
			{
				const int32 ActorComponentStart = HierarchyComponentCount;
				TSharedPtr<FJsonObject> RootJson = BuildSceneHierarchyJson(
					RootComponent,
					bIncludeSceneDetails,
					0,
					HierarchyDepth,
					HierarchyComponentLimit,
					HierarchyComponentCount,
					bHierarchyTruncated);
				ActorHierarchyJson->SetObjectField(TEXT("root_component"), RootJson.IsValid() ? RootJson : BuildObjectReferenceJson(nullptr));
				ActorHierarchyJson->SetNumberField(TEXT("returned_component_count"), HierarchyComponentCount - ActorComponentStart);
			}
			else
			{
				ActorHierarchyJson->SetObjectField(TEXT("root_component"), BuildObjectReferenceJson(nullptr));
				ActorHierarchyJson->SetNumberField(TEXT("returned_component_count"), 0);
			}

			HierarchiesJson.Add(MakeShared<FJsonValueObject>(ActorHierarchyJson));
			++HierarchyActorCount;

			if (HierarchyComponentCount >= HierarchyComponentLimit)
			{
				bHierarchyTruncated = true;
				break;
			}
		}

		Data->SetNumberField(TEXT("hierarchy_depth"), HierarchyDepth);
		Data->SetNumberField(TEXT("hierarchy_actor_limit"), HierarchyActorLimit);
		Data->SetNumberField(TEXT("hierarchy_component_limit"), HierarchyComponentLimit);
		Data->SetNumberField(TEXT("hierarchy_actor_count"), HierarchyActorCount);
		Data->SetNumberField(TEXT("hierarchy_component_count"), HierarchyComponentCount);
		Data->SetBoolField(TEXT("hierarchy_actor_truncated"), ActorsToInspect.Num() > HierarchyActorCount);
		Data->SetBoolField(TEXT("hierarchy_truncated"), bHierarchyTruncated || ActorsToInspect.Num() > HierarchyActorCount);
		Data->SetArrayField(TEXT("component_hierarchies"), HierarchiesJson);
	}

	return Data;
}
}
