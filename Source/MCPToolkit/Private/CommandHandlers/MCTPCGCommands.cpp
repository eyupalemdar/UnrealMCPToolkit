// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTPCGCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PCGCommon.h"
#include "PCGComponent.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGManagedResource.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"
#include "UObject/UnrealType.h"

namespace MCPToolkit::CommandHandlers::PCG
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FString& DefaultValue = TEXT(""))
{
	FString Value = DefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
		Value.TrimStartAndEndInline();
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const bool bDefaultValue)
{
	bool bValue = bDefaultValue;
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

FString EnumValueToString(const UEnum* Enum, const int64 Value)
{
	return Enum ? Enum->GetNameStringByValue(Value) : FString::FromInt(static_cast<int32>(Value));
}

FString GetPCGSettingsTypeString(const UPCGSettings* Settings)
{
#if WITH_EDITOR
	return Settings ? EnumValueToString(StaticEnum<EPCGSettingsType>(), static_cast<int64>(Settings->GetType())) : FString();
#else
	return FString();
#endif
}

bool MatchesTextFilter(const FString& Value, const FString& Filter)
{
	return Filter.IsEmpty() || Value.ToLower().Contains(Filter.ToLower());
}

bool MatchesActorSelector(const AActor* Actor, const FString& ActorPath, const FString& ActorLabel, const FString& ActorName, const FString& NameFilter)
{
	if (!Actor)
	{
		return false;
	}
	if (!ActorPath.IsEmpty() && Actor->GetPathName() != ActorPath)
	{
		return false;
	}
	if (!ActorLabel.IsEmpty() && Actor->GetActorLabel() != ActorLabel)
	{
		return false;
	}
	if (!ActorName.IsEmpty() && Actor->GetName() != ActorName)
	{
		return false;
	}
	if (!NameFilter.IsEmpty())
	{
		const FString Filter = NameFilter.ToLower();
		if (!Actor->GetName().ToLower().Contains(Filter)
			&& !Actor->GetActorLabel().ToLower().Contains(Filter)
			&& !Actor->GetPathName().ToLower().Contains(Filter))
		{
			return false;
		}
	}
	return true;
}

TSharedPtr<FJsonObject> BuildPinJson(const UPCGPin* Pin, const FString& Direction)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Pin != nullptr);
	Data->SetStringField(TEXT("direction"), Direction);
	if (!Pin)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Pin->GetName());
	Data->SetStringField(TEXT("label"), Pin->Properties.Label.ToString());
	Data->SetStringField(TEXT("usage"), EnumValueToString(StaticEnum<EPCGPinUsage>(), static_cast<int64>(Pin->Properties.Usage)));
	Data->SetStringField(TEXT("status"), EnumValueToString(StaticEnum<EPCGPinStatus>(), static_cast<int64>(Pin->Properties.PinStatus)));
	Data->SetStringField(TEXT("allowed_types"), Pin->Properties.AllowedTypes.ToString());
	Data->SetStringField(TEXT("allowed_types_display"), Pin->Properties.AllowedTypes.ToDisplayText().ToString());
	Data->SetBoolField(TEXT("allow_multiple_data"), Pin->Properties.bAllowMultipleData);
	Data->SetBoolField(TEXT("allow_multiple_connections"), Pin->Properties.AllowsMultipleConnections());
	Data->SetBoolField(TEXT("advanced"), Pin->Properties.IsAdvancedPin());
	Data->SetBoolField(TEXT("required"), Pin->Properties.IsRequiredPin());
	Data->SetBoolField(TEXT("invisible"), Pin->Properties.bInvisiblePin);
	Data->SetBoolField(TEXT("connected"), Pin->IsConnected());
	Data->SetNumberField(TEXT("edge_count"), Pin->EdgeCount());
#if WITH_EDITORONLY_DATA
	Data->SetStringField(TEXT("tooltip"), Pin->Properties.Tooltip.ToString());
#endif
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildPinArrayJson(const TArray<TObjectPtr<UPCGPin>>& Pins, const FString& Direction, const int32 PinLimit, bool& bOutTruncated)
{
	TArray<TSharedPtr<FJsonValue>> PinsJson;
	bOutTruncated = false;
	for (const TObjectPtr<UPCGPin>& Pin : Pins)
	{
		if (PinsJson.Num() >= PinLimit)
		{
			bOutTruncated = true;
			break;
		}
		PinsJson.Add(MakeShared<FJsonValueObject>(BuildPinJson(Pin.Get(), Direction)));
	}
	return PinsJson;
}

TArray<TSharedPtr<FJsonValue>> BuildReflectedPropertyArrayJson(const UObject* Object, const int32 PropertyLimit, const int32 StringLimit, bool& bOutTruncated, int32& OutPropertyCount)
{
	TArray<TSharedPtr<FJsonValue>> PropertiesJson;
	bOutTruncated = false;
	OutPropertyCount = 0;
	if (!Object || !Object->GetClass())
	{
		return PropertiesJson;
	}

	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
		{
			continue;
		}

		++OutPropertyCount;
		if (PropertiesJson.Num() >= PropertyLimit)
		{
			bOutTruncated = true;
			continue;
		}

		FString Value;
		Property->ExportText_InContainer(0, Value, Object, Object, const_cast<UObject*>(Object), PPF_None);

		TSharedPtr<FJsonObject> PropertyJson = MakeShared<FJsonObject>();
		PropertyJson->SetStringField(TEXT("name"), Property->GetName());
		PropertyJson->SetStringField(TEXT("type"), Property->GetCPPType());
		PropertyJson->SetStringField(TEXT("value"), MCPToolkit::RuntimeDiagnostics::TruncateString(Value, StringLimit));
		PropertiesJson.Add(MakeShared<FJsonValueObject>(PropertyJson));
	}
	return PropertiesJson;
}

TSharedPtr<FJsonObject> BuildSettingsJson(const UPCGSettings* Settings, const bool bIncludeSettings, const int32 PropertyLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Settings);
	if (!Settings)
	{
		return Data;
	}

	Data->SetStringField(TEXT("class_name"), Settings->GetClass() ? Settings->GetClass()->GetName() : FString());
	Data->SetStringField(TEXT("settings_type"), GetPCGSettingsTypeString(Settings));
	Data->SetBoolField(TEXT("enabled"), Settings->bEnabled);
	Data->SetBoolField(TEXT("debug"), Settings->bDebug);
	Data->SetNumberField(TEXT("seed"), Settings->Seed);
	Data->SetStringField(TEXT("additional_title_information"), Settings->GetAdditionalTitleInformation());
	Data->SetBoolField(TEXT("dynamic_pins"), Settings->HasDynamicPins());
	Data->SetBoolField(TEXT("output_pins_can_deactivate"), Settings->OutputPinsCanBeDeactivated());
	Data->SetBoolField(TEXT("can_cull_if_unwired"), Settings->CanCullTaskIfUnwired());

	if (bIncludeSettings)
	{
		bool bPropertiesTruncated = false;
		int32 PropertyCount = 0;
		TArray<TSharedPtr<FJsonValue>> PropertiesJson = BuildReflectedPropertyArrayJson(Settings, PropertyLimit, 512, bPropertiesTruncated, PropertyCount);
		Data->SetNumberField(TEXT("property_count"), PropertyCount);
		Data->SetBoolField(TEXT("properties_truncated"), bPropertiesTruncated);
		Data->SetArrayField(TEXT("properties"), PropertiesJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildNodeJson(
	const UPCGNode* Node,
	const int32 Index,
	const bool bIncludePins,
	const bool bIncludeSettings,
	const int32 PinLimit,
	const int32 PropertyLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Node);
	Data->SetNumberField(TEXT("index"), Index);
	if (!Node)
	{
		return Data;
	}

	Data->SetStringField(TEXT("title"), Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString());
	Data->SetStringField(TEXT("full_title"), Node->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());
	Data->SetStringField(TEXT("authored_title"), Node->GetAuthoredTitleName().ToString());
	Data->SetBoolField(TEXT("has_authored_title"), Node->HasAuthoredTitle());
	Data->SetBoolField(TEXT("instance_settings"), Node->IsInstance());
	Data->SetBoolField(TEXT("has_inbound_edges"), Node->HasInboundEdges());
	Data->SetNumberField(TEXT("inbound_edge_count"), Node->GetInboundEdgesNum());
#if WITH_EDITOR
	int32 PositionX = 0;
	int32 PositionY = 0;
	Node->GetNodePosition(PositionX, PositionY);
	Data->SetNumberField(TEXT("position_x"), PositionX);
	Data->SetNumberField(TEXT("position_y"), PositionY);
	Data->SetStringField(TEXT("tooltip"), Node->GetNodeTooltipText().ToString());
	Data->SetBoolField(TEXT("hidden"), Node->IsHidden());
#endif

	Data->SetObjectField(TEXT("settings"), BuildSettingsJson(Node->GetSettings(), bIncludeSettings, PropertyLimit));
	Data->SetNumberField(TEXT("input_pin_count"), Node->GetInputPins().Num());
	Data->SetNumberField(TEXT("output_pin_count"), Node->GetOutputPins().Num());
	if (bIncludePins)
	{
		bool bInputPinsTruncated = false;
		bool bOutputPinsTruncated = false;
		Data->SetArrayField(TEXT("input_pins"), BuildPinArrayJson(Node->GetInputPins(), TEXT("input"), PinLimit, bInputPinsTruncated));
		Data->SetArrayField(TEXT("output_pins"), BuildPinArrayJson(Node->GetOutputPins(), TEXT("output"), PinLimit, bOutputPinsTruncated));
		Data->SetBoolField(TEXT("input_pins_truncated"), bInputPinsTruncated);
		Data->SetBoolField(TEXT("output_pins_truncated"), bOutputPinsTruncated);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildEdgeJson(const UPCGEdge* Edge, const int32 Index)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Edge);
	Data->SetNumberField(TEXT("index"), Index);
	if (!Edge)
	{
		return Data;
	}

	const UPCGPin* FromPin = Edge->InputPin.Get();
	const UPCGPin* ToPin = Edge->OutputPin.Get();
	const UPCGNode* FromNode = FromPin ? FromPin->Node.Get() : nullptr;
	const UPCGNode* ToNode = ToPin ? ToPin->Node.Get() : nullptr;
	Data->SetBoolField(TEXT("valid"), Edge->IsValid());
	Data->SetStringField(TEXT("from_node"), FromNode ? FromNode->GetName() : FString());
	Data->SetStringField(TEXT("from_title"), FromNode ? FromNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString() : FString());
	Data->SetStringField(TEXT("from_pin"), FromPin ? FromPin->Properties.Label.ToString() : FString());
	Data->SetObjectField(TEXT("from_node_ref"), BuildObjectReferenceJson(FromNode));
	Data->SetStringField(TEXT("to_node"), ToNode ? ToNode->GetName() : FString());
	Data->SetStringField(TEXT("to_title"), ToNode ? ToNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString() : FString());
	Data->SetStringField(TEXT("to_pin"), ToPin ? ToPin->Properties.Label.ToString() : FString());
	Data->SetObjectField(TEXT("to_node_ref"), BuildObjectReferenceJson(ToNode));
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildGraphEdgesJson(const UPCGGraph* Graph, const int32 EdgeLimit, bool& bOutTruncated, int32& OutEdgeCount)
{
	TArray<TSharedPtr<FJsonValue>> EdgesJson;
	bOutTruncated = false;
	OutEdgeCount = 0;
	if (!Graph)
	{
		return EdgesJson;
	}

	TSet<const UPCGEdge*> SeenEdges;
	auto VisitNode = [&EdgesJson, &SeenEdges, &bOutTruncated, &OutEdgeCount, EdgeLimit](const UPCGNode* Node)
	{
		if (!Node)
		{
			return;
		}

		for (const TObjectPtr<UPCGPin>& Pin : Node->GetOutputPins())
		{
			if (!Pin)
			{
				continue;
			}

			for (const TObjectPtr<UPCGEdge>& Edge : Pin->Edges)
			{
				const UPCGEdge* EdgePtr = Edge.Get();
				if (!EdgePtr || SeenEdges.Contains(EdgePtr))
				{
					continue;
				}

				SeenEdges.Add(EdgePtr);
				++OutEdgeCount;
				if (EdgesJson.Num() >= EdgeLimit)
				{
					bOutTruncated = true;
					continue;
				}
				EdgesJson.Add(MakeShared<FJsonValueObject>(BuildEdgeJson(EdgePtr, EdgesJson.Num())));
			}
		}
	};

	VisitNode(Graph->GetInputNode());
	VisitNode(Graph->GetOutputNode());
	for (const UPCGNode* Node : Graph->GetNodes())
	{
		VisitNode(Node);
	}
	return EdgesJson;
}

TSharedPtr<FJsonObject> BuildPCGGraphJson(
	const UPCGGraph* Graph,
	const bool bIncludeNodes,
	const bool bIncludePins,
	const bool bIncludeEdges,
	const bool bIncludeSettings,
	const int32 NodeLimit,
	const int32 PinLimit,
	const int32 EdgeLimit,
	const int32 PropertyLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Graph);
	if (!Graph)
	{
		return Data;
	}

	Data->SetStringField(TEXT("package_name"), Graph->GetOutermost() ? Graph->GetOutermost()->GetName() : FString());
	Data->SetBoolField(TEXT("standalone_graph"), Graph->IsStandaloneGraph());
	Data->SetBoolField(TEXT("hierarchical_generation_enabled"), Graph->IsHierarchicalGenerationEnabled());
	Data->SetBoolField(TEXT("use_2d_grid"), Graph->Use2DGrid());
	Data->SetNumberField(TEXT("default_grid_size"), Graph->GetDefaultGridSize());
	Data->SetBoolField(TEXT("landscape_uses_metadata"), Graph->bLandscapeUsesMetadata);
#if WITH_EDITORONLY_DATA
	Data->SetStringField(TEXT("category"), Graph->Category.ToString());
	Data->SetStringField(TEXT("description"), Graph->Description.ToString());
	Data->SetBoolField(TEXT("ignore_landscape_tracking"), Graph->bIgnoreLandscapeTracking);
	Data->SetBoolField(TEXT("expose_to_library"), Graph->bExposeToLibrary);
	Data->SetBoolField(TEXT("is_template"), Graph->bIsTemplate);
#endif

	const TArray<UPCGNode*>& Nodes = Graph->GetNodes();
	Data->SetNumberField(TEXT("node_count"), Nodes.Num());
	Data->SetObjectField(TEXT("input_node"), BuildNodeJson(Graph->GetInputNode(), INDEX_NONE, bIncludePins, bIncludeSettings, PinLimit, PropertyLimit));
	Data->SetObjectField(TEXT("output_node"), BuildNodeJson(Graph->GetOutputNode(), INDEX_NONE, bIncludePins, bIncludeSettings, PinLimit, PropertyLimit));

	if (bIncludeNodes)
	{
		TArray<TSharedPtr<FJsonValue>> NodesJson;
		for (const UPCGNode* Node : Nodes)
		{
			if (NodesJson.Num() >= NodeLimit)
			{
				break;
			}
			NodesJson.Add(MakeShared<FJsonValueObject>(BuildNodeJson(Node, NodesJson.Num(), bIncludePins, bIncludeSettings, PinLimit, PropertyLimit)));
		}
		Data->SetArrayField(TEXT("nodes"), NodesJson);
		Data->SetBoolField(TEXT("nodes_truncated"), NodesJson.Num() < Nodes.Num());
	}

	bool bEdgesTruncated = false;
	int32 EdgeCount = 0;
	TArray<TSharedPtr<FJsonValue>> EdgesJson = BuildGraphEdgesJson(Graph, bIncludeEdges ? EdgeLimit : 0, bEdgesTruncated, EdgeCount);
	Data->SetNumberField(TEXT("edge_count"), EdgeCount);
	Data->SetBoolField(TEXT("edges_truncated"), bIncludeEdges && bEdgesTruncated);
	if (bIncludeEdges)
	{
		Data->SetArrayField(TEXT("edges"), EdgesJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildManagedResourceSummaryJson(const UPCGComponent* Component, const int32 ResourceLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ResourcesJson;
	int32 ResourceCount = 0;
	if (Component && Component->AreManagedResourcesAccessible())
	{
		Component->ForEachConstManagedResource([&ResourcesJson, &ResourceCount, ResourceLimit](const UPCGManagedResource* Resource)
		{
			++ResourceCount;
			if (ResourcesJson.Num() >= ResourceLimit)
			{
				return;
			}

			TSharedPtr<FJsonObject> ResourceJson = BuildObjectReferenceJson(Resource);
			ResourcesJson.Add(MakeShared<FJsonValueObject>(ResourceJson));
		});
	}

	Data->SetBoolField(TEXT("accessible"), Component && Component->AreManagedResourcesAccessible());
	Data->SetNumberField(TEXT("count"), ResourceCount);
	Data->SetBoolField(TEXT("truncated"), ResourcesJson.Num() < ResourceCount);
	Data->SetArrayField(TEXT("resources"), ResourcesJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildPCGComponentJson(
	UPCGComponent* Component,
	const bool bIncludeGraph,
	const bool bIncludeResources,
	const int32 ResourceLimit)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildComponentJson(Component);
	if (!Component)
	{
		return Data;
	}

	AActor* Owner = Component->GetOwner();
	Data->SetObjectField(TEXT("owner"), BuildActorJson(Owner));
	Data->SetObjectField(TEXT("graph"), BuildObjectReferenceJson(Component->GetGraph()));
	Data->SetObjectField(TEXT("graph_instance"), BuildObjectReferenceJson(Component->GetGraphInstance()));
	Data->SetNumberField(TEXT("seed"), Component->Seed);
	Data->SetBoolField(TEXT("activated"), Component->bActivated);
	Data->SetBoolField(TEXT("generated"), Component->bGenerated);
	Data->SetBoolField(TEXT("runtime_generated"), Component->bRuntimeGenerated);
	Data->SetBoolField(TEXT("was_generated_this_session"), Component->WasGeneratedThisSession());
	Data->SetBoolField(TEXT("is_generating"), Component->IsGenerating());
	Data->SetBoolField(TEXT("is_cleaning_up"), Component->IsCleaningUp());
	Data->SetBoolField(TEXT("partitioned"), Component->IsPartitioned());
	Data->SetBoolField(TEXT("can_partition"), Component->CanPartition());
	Data->SetBoolField(TEXT("procedural_instances_in_use"), Component->AreProceduralInstancesInUse());
	Data->SetStringField(TEXT("generation_trigger"), EnumValueToString(StaticEnum<EPCGComponentGenerationTrigger>(), static_cast<int64>(Component->GenerationTrigger)));
	Data->SetStringField(TEXT("input_type"), EnumValueToString(StaticEnum<EPCGComponentInput>(), static_cast<int64>(Component->InputType)));
	Data->SetNumberField(TEXT("generation_grid_size"), Component->GetGenerationGridSize());
	Data->SetObjectField(TEXT("last_generated_bounds"), BuildBoxJson(Component->GetLastGeneratedBounds()));
#if WITH_EDITORONLY_DATA
	Data->SetBoolField(TEXT("dirty_generated"), Component->bDirtyGenerated);
	Data->SetBoolField(TEXT("regenerate_in_editor"), Component->bRegenerateInEditor);
	Data->SetBoolField(TEXT("only_track_itself"), Component->bOnlyTrackItself);
	Data->SetBoolField(TEXT("ignore_landscape_tracking"), Component->bIgnoreLandscapeTracking);
	Data->SetNumberField(TEXT("tracking_priority"), Component->TrackingPriority);
#endif
#if WITH_EDITOR
	Data->SetStringField(TEXT("editing_mode"), EnumValueToString(StaticEnum<EPCGEditorDirtyMode>(), static_cast<int64>(Component->GetEditingMode())));
	Data->SetStringField(TEXT("serialized_editing_mode"), EnumValueToString(StaticEnum<EPCGEditorDirtyMode>(), static_cast<int64>(Component->GetSerializedEditingMode())));
	Data->SetBoolField(TEXT("preview_mode"), Component->IsInPreviewMode());
	Data->SetBoolField(TEXT("generation_in_progress"), Component->IsGenerationInProgress());
	Data->SetBoolField(TEXT("refresh_in_progress"), Component->IsRefreshInProgress());
#endif

	if (bIncludeGraph && Component->GetGraph())
	{
		Data->SetObjectField(TEXT("graph_summary"), BuildPCGGraphJson(Component->GetGraph(), false, false, false, false, 0, 0, 0, 0));
	}
	if (bIncludeResources)
	{
		Data->SetObjectField(TEXT("managed_resources"), BuildManagedResourceSummaryJson(Component, ResourceLimit));
	}
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

FString HandlePCGGraphInfo(TSharedPtr<FJsonObject> Params)
{
	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing required 'asset_path' parameter"));
	}

	const bool bIncludeNodes = ReadBoolField(Params, TEXT("include_nodes"), true);
	const bool bIncludePins = ReadBoolField(Params, TEXT("include_pins"), true);
	const bool bIncludeEdges = ReadBoolField(Params, TEXT("include_edges"), true);
	const bool bIncludeSettings = ReadBoolField(Params, TEXT("include_settings"), true);
	const int32 NodeLimit = ReadIntField(Params, TEXT("node_limit"), 200, 0, 10000);
	const int32 PinLimit = ReadIntField(Params, TEXT("pin_limit"), 64, 0, 1000);
	const int32 EdgeLimit = ReadIntField(Params, TEXT("edge_limit"), 500, 0, 50000);
	const int32 PropertyLimit = ReadIntField(Params, TEXT("setting_property_limit"), 40, 0, 500);

	return RunOnGameThread([AssetPath, bIncludeNodes, bIncludePins, bIncludeEdges, bIncludeSettings, NodeLimit, PinLimit, EdgeLimit, PropertyLimit]()
	{
		UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetPath);
		if (!Graph)
		{
			return CreateErrorResponse(FString::Printf(TEXT("PCG graph not found: %s"), *AssetPath));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetObjectField(TEXT("graph"), BuildPCGGraphJson(Graph, bIncludeNodes, bIncludePins, bIncludeEdges, bIncludeSettings, NodeLimit, PinLimit, EdgeLimit, PropertyLimit));
		return CreateSuccessResponse(Data);
	}, TEXT("PCG graph info timed out"));
}

FString HandlePCGComponentInfo(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadStringField(Params, TEXT("world"), TEXT("editor"));
	const FString ActorPath = ReadStringField(Params, TEXT("actor_path"));
	const FString ActorLabel = ReadStringField(Params, TEXT("actor_label"));
	const FString ActorName = ReadStringField(Params, TEXT("actor_name"));
	const FString NameFilter = ReadStringField(Params, TEXT("name_filter"));
	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const bool bIncludeGraph = ReadBoolField(Params, TEXT("include_graph"), true);
	const bool bIncludeResources = ReadBoolField(Params, TEXT("include_resources"), false);
	const int32 ComponentLimit = ReadIntField(Params, TEXT("component_limit"), 100, 1, 5000);
	const int32 ResourceLimit = ReadIntField(Params, TEXT("resource_limit"), 100, 0, 10000);

	return RunOnGameThread([WorldSelector, ActorPath, ActorLabel, ActorName, NameFilter, ComponentName, bIncludeGraph, bIncludeResources, ComponentLimit, ResourceLimit]()
	{
		using namespace MCPToolkit::RuntimeDiagnostics;

		FString WorldSource;
		UWorld* World = SelectWorld(WorldSelector, WorldSource);
		if (!World)
		{
			return CreateErrorResponse(TEXT("PCG component world is not available"));
		}

		TArray<TSharedPtr<FJsonValue>> ComponentsJson;
		int32 MatchedActorCount = 0;
		int32 MatchedComponentCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!MatchesActorSelector(Actor, ActorPath, ActorLabel, ActorName, NameFilter))
			{
				continue;
			}

			TArray<UPCGComponent*> PCGComponents;
			Actor->GetComponents<UPCGComponent>(PCGComponents);
			if (PCGComponents.Num() == 0)
			{
				continue;
			}

			bool bActorMatched = false;
			for (UPCGComponent* Component : PCGComponents)
			{
				if (!Component || !MatchesTextFilter(Component->GetName(), ComponentName))
				{
					continue;
				}

				if (!bActorMatched)
				{
					++MatchedActorCount;
					bActorMatched = true;
				}
				++MatchedComponentCount;
				if (ComponentsJson.Num() >= ComponentLimit)
				{
					continue;
				}
				ComponentsJson.Add(MakeShared<FJsonValueObject>(BuildPCGComponentJson(Component, bIncludeGraph, bIncludeResources, ResourceLimit)));
			}
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));
		Data->SetStringField(TEXT("actor_path"), ActorPath);
		Data->SetStringField(TEXT("actor_label"), ActorLabel);
		Data->SetStringField(TEXT("actor_name"), ActorName);
		Data->SetStringField(TEXT("name_filter"), NameFilter);
		Data->SetStringField(TEXT("component_name"), ComponentName);
		Data->SetNumberField(TEXT("matched_actor_count"), MatchedActorCount);
		Data->SetNumberField(TEXT("matched_component_count"), MatchedComponentCount);
		Data->SetNumberField(TEXT("returned_component_count"), ComponentsJson.Num());
		Data->SetBoolField(TEXT("components_truncated"), ComponentsJson.Num() < MatchedComponentCount);
		Data->SetArrayField(TEXT("components"), ComponentsJson);
		return CreateSuccessResponse(Data);
	}, TEXT("PCG component info timed out"));
}
}
