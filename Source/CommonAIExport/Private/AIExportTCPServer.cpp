// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "CommandDispatch/AIExportCommandDispatch.h"
#include "CommandHandlers/AIExportUtilityCommands.h"
#include "CommonAIExportModule.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "HttpServerRequest.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

// Asset delete (HandleDeleteAsset) — ObjectTools lives in UnrealEd

// Static instance
TUniquePtr<FAIExportTCPServer> FAIExportTCPServerManager::Instance;

//////////////////////////////////////////////////////////////////////////
// FAIExportTCPServerManager

FAIExportTCPServer* FAIExportTCPServerManager::Get()
{
	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FAIExportTCPServer>();
	}
	return Instance.Get();
}

void FAIExportTCPServerManager::Start()
{
	FAIExportTCPServer* Server = Get();
	if (Server && !Server->IsRunning())
	{
		Server->StartServer();
	}
}

void FAIExportTCPServerManager::Stop()
{
	if (Instance.IsValid())
	{
		Instance->StopServer();
		Instance.Reset();
	}
}

//////////////////////////////////////////////////////////////////////////
// FAIExportTCPServer

FAIExportTCPServer::FAIExportTCPServer()
	: bIsRunning(false)
	, bStopRequested(false)
{
}

FAIExportTCPServer::~FAIExportTCPServer()
{
	StopServer();
}

int32 FAIExportTCPServer::FindAvailablePort(int32 StartPort, int32 EndPort)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to get socket subsystem for port discovery"));
		return StartPort;
	}

	for (int32 Port = StartPort; Port <= EndPort; ++Port)
	{
		// Try to bind to this port
		FSocket* TestSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("PortTest"), false);
		if (!TestSocket)
		{
			continue;
		}

		// DO NOT use SetReuseAddr(true) here - we want the bind to FAIL if port is in use
		// This ensures proper port discovery when multiple UE projects are running

		FIPv4Address LocalAddress;
		FIPv4Address::Parse(TEXT("127.0.0.1"), LocalAddress);
		TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		Addr->SetIp(LocalAddress.Value);
		Addr->SetPort(Port);

		bool bBound = TestSocket->Bind(*Addr);
		SocketSubsystem->DestroySocket(TestSocket);

		if (bBound)
		{
			UE_LOG(LogAIExport, Log, TEXT("Found available port: %d"), Port);
			return Port;
		}
		else
		{
			UE_LOG(LogAIExport, Verbose, TEXT("Port %d is in use, trying next..."), Port);
		}
	}

	UE_LOG(LogAIExport, Warning, TEXT("No available port found in range %d-%d, using %d"), StartPort, EndPort, StartPort);
	return StartPort;
}

void FAIExportTCPServer::WritePortFile(int32 Port)
{
	FString PortFilePath = GetPortFilePath();
	FString PortString = FString::FromInt(Port);

	if (FFileHelper::SaveStringToFile(PortString, *PortFilePath))
	{
		UE_LOG(LogAIExport, Log, TEXT("Written port %d to: %s"), Port, *PortFilePath);
	}
	else
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to write port file: %s"), *PortFilePath);
	}
}

FString FAIExportTCPServer::GetPortFilePath()
{
	return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("AIExport_port.txt"));
}

FString FAIExportTCPServer::GetEditorRegistryDir()
{
	return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("CommonAIExport"), TEXT("Editors"));
}

FString FAIExportTCPServer::GetEditorRegistryFilePath(int32 Port)
{
	return FPaths::Combine(
		GetEditorRegistryDir(),
		FString::Printf(TEXT("%u-%d.json"), FPlatformProcess::GetCurrentProcessId(), Port));
}

const TArray<FAIExportTCPServer::FCommandDescriptor>& FAIExportTCPServer::GetCommandDescriptors()
{
#define AI_COMMAND_PARAMS(CommandName, CommandCategory, bCommandMutating, CommandTimeout, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), true, bCommandMutating, CommandTimeout, bCommandMutating ? TEXT("write") : TEXT("read"), bCommandMutating, CommandTimeout >= 120, &FAIExportTCPServer::HandlerName, nullptr }
#define AI_COMMAND_PARAMS_SCOPE(CommandName, CommandCategory, bCommandMutating, CommandTimeout, CommandScope, bCommandDryRun, bCommandAsyncCandidate, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), true, bCommandMutating, CommandTimeout, TEXT(CommandScope), bCommandDryRun, bCommandAsyncCandidate, &FAIExportTCPServer::HandlerName, nullptr }
#define AI_COMMAND_OPTIONAL_PARAMS(CommandName, CommandCategory, bCommandMutating, CommandTimeout, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), false, bCommandMutating, CommandTimeout, bCommandMutating ? TEXT("write") : TEXT("read"), bCommandMutating, CommandTimeout >= 120, &FAIExportTCPServer::HandlerName, nullptr }
#define AI_COMMAND_NO_PARAMS(CommandName, CommandCategory, CommandTimeout, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), false, false, CommandTimeout, TEXT("read"), false, false, nullptr, &FAIExportTCPServer::HandlerName }
#define AI_COMMAND_NO_PARAMS_SCOPE(CommandName, CommandCategory, bCommandMutating, CommandTimeout, CommandScope, bCommandDryRun, bCommandAsyncCandidate, HandlerName) \
	{ TEXT(CommandName), TEXT(CommandCategory), false, bCommandMutating, CommandTimeout, TEXT(CommandScope), bCommandDryRun, bCommandAsyncCandidate, nullptr, &FAIExportTCPServer::HandlerName }

	static const TArray<FCommandDescriptor> Commands = {
		AI_COMMAND_NO_PARAMS("ping", "Utility", 0, HandlePing),
		AI_COMMAND_NO_PARAMS("list_commands", "Utility", 0, HandleListCommands),
		AI_COMMAND_NO_PARAMS("server_status", "Utility", 0, HandleServerStatus),
		AI_COMMAND_NO_PARAMS("editor_identity", "Utility", 0, HandleEditorIdentity),
		AI_COMMAND_OPTIONAL_PARAMS("command_manifest_export", "Utility", false, 30, HandleCommandManifestExport),
		AI_COMMAND_NO_PARAMS("project_status", "Workflow", 0, HandleProjectStatus),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_status", "Workflow", false, 30, HandleSourceControlStatus),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_log", "Workflow", false, 30, HandleSourceControlLog),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_show", "Workflow", false, 30, HandleSourceControlShow),
		AI_COMMAND_OPTIONAL_PARAMS("source_control_diff", "Workflow", false, 30, HandleSourceControlDiff),
		AI_COMMAND_OPTIONAL_PARAMS("build_project", "Workflow", true, 1200, HandleBuildProject),
		AI_COMMAND_OPTIONAL_PARAMS("generate_project_files", "Workflow", true, 300, HandleGenerateProjectFiles),
		AI_COMMAND_OPTIONAL_PARAMS("cook_project", "Workflow", true, 1800, HandleCookProject),
		AI_COMMAND_OPTIONAL_PARAMS("list_tests", "Workflow", false, 300, HandleListTests),
		AI_COMMAND_OPTIONAL_PARAMS("run_tests", "Workflow", false, 1800, HandleRunTests),
		AI_COMMAND_OPTIONAL_PARAMS("get_test_log", "Workflow", false, 30, HandleGetTestLog),
		AI_COMMAND_OPTIONAL_PARAMS("project_info", "Project", false, 30, HandleProjectInfo),
		AI_COMMAND_OPTIONAL_PARAMS("project_plugin_list", "Project", false, 30, HandleProjectPluginList),
		AI_COMMAND_PARAMS("project_plugin_set_enabled", "Project", true, 30, HandleProjectPluginSetEnabled),
		AI_COMMAND_OPTIONAL_PARAMS("project_module_list", "Project", false, 30, HandleProjectModuleList),
		AI_COMMAND_PARAMS("project_config_get", "ProjectConfig", false, 30, HandleProjectConfigGet),
		AI_COMMAND_PARAMS("project_config_set", "ProjectConfig", true, 30, HandleProjectConfigSet),
		AI_COMMAND_PARAMS("project_config_delete", "ProjectConfig", true, 30, HandleProjectConfigDelete),
		AI_COMMAND_OPTIONAL_PARAMS("project_config_list_sections", "ProjectConfig", false, 30, HandleProjectConfigListSections),
		AI_COMMAND_PARAMS("project_config_list_keys", "ProjectConfig", false, 30, HandleProjectConfigListKeys),
		AI_COMMAND_PARAMS("task_submit", "AsyncJob", false, 0, HandleTaskSubmit),
		AI_COMMAND_OPTIONAL_PARAMS("task_status", "AsyncJob", false, 0, HandleTaskStatus),
		AI_COMMAND_OPTIONAL_PARAMS("task_result", "AsyncJob", false, 0, HandleTaskResult),
		AI_COMMAND_OPTIONAL_PARAMS("task_cancel", "AsyncJob", false, 0, HandleTaskCancel),
		AI_COMMAND_OPTIONAL_PARAMS("task_events", "AsyncJob", false, 0, HandleTaskEvents),
		AI_COMMAND_OPTIONAL_PARAMS("task_events_wait", "AsyncJob", false, 30, HandleTaskEventsWait),
		AI_COMMAND_PARAMS("export_widget", "Export", false, 60, HandleExportWidget),
		AI_COMMAND_PARAMS("export_blueprint", "Export", false, 60, HandleExportBlueprint),
		AI_COMMAND_NO_PARAMS("list_supported_types", "Export", 0, HandleListSupportedTypes),

		AI_COMMAND_NO_PARAMS("editor_world_info", "Editor", 0, HandleEditorWorldInfo),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_world_info", "RuntimeInspector", false, 30, HandleRuntimeWorldInfo),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_player_list", "RuntimeInspector", false, 30, HandleRuntimePlayerList),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_component_list", "RuntimeInspector", false, 60, HandleRuntimeComponentList),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_input_routing", "RuntimeInspector", false, 60, HandleRuntimeInputRouting),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_replication_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeReplicationDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_ability_system_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAbilitySystemDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_ai_perception_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAIPerceptionDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_ai_controller_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAIControllerDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_eqs_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeEQSDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_gameplay_tags_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeGameplayTagsDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_commonui_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeCommonUIDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_audio_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAudioDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_navigation_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeNavigationDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_asset_streaming_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAssetStreamingDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_async_load_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeAsyncLoadDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_game_instance_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeGameInstanceDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_level_travel_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeLevelTravelDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_multiplayer_connection_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeMultiplayerConnectionDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_tick_timer_latent_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeTickTimerLatentDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_scheduler_performance_diagnostics", "RuntimeInspector", false, 60, HandleRuntimeSchedulerPerformanceDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("runtime_physics_collision_diagnostics", "RuntimeInspector", false, 60, HandleRuntimePhysicsCollisionDiagnostics),
		AI_COMMAND_OPTIONAL_PARAMS("actor_list", "EditorActor", false, 60, HandleActorList),
		AI_COMMAND_PARAMS("actor_spawn", "EditorActor", true, 60, HandleActorSpawn),
		AI_COMMAND_PARAMS("actor_set_transform", "EditorActor", true, 60, HandleActorSetTransform),
		AI_COMMAND_PARAMS_SCOPE("actor_delete", "EditorActor", true, 60, "destructive", true, false, HandleActorDelete),
		AI_COMMAND_PARAMS("level_open", "EditorLevel", true, 60, HandleLevelOpen),
		AI_COMMAND_NO_PARAMS_SCOPE("level_save_current", "EditorLevel", true, 60, "write", true, false, HandleLevelSaveCurrent),
		AI_COMMAND_OPTIONAL_PARAMS("level_structure_info", "EditorLevel", false, 60, HandleLevelStructureInfo),
		AI_COMMAND_NO_PARAMS("pie_status", "PIE", 0, HandlePIEStatus),
		AI_COMMAND_NO_PARAMS_SCOPE("pie_start", "PIE", true, 30, "write", true, false, HandlePIEStart),
		AI_COMMAND_NO_PARAMS_SCOPE("pie_stop", "PIE", true, 30, "write", true, false, HandlePIEStop),
		AI_COMMAND_PARAMS_SCOPE("editor_console_command", "Editor", true, 60, "destructive", true, false, HandleEditorConsoleCommand),
		AI_COMMAND_OPTIONAL_PARAMS("editor_log_read", "Workflow", false, 30, HandleEditorLogRead),
		AI_COMMAND_OPTIONAL_PARAMS("viewport_capture", "EditorViewport", true, 30, HandleViewportCapture),

		AI_COMMAND_PARAMS("create_widget_blueprint", "Widget", true, 60, HandleCreateWidgetBlueprint),
		AI_COMMAND_PARAMS("add_widget", "Widget", true, 60, HandleAddWidget),
		AI_COMMAND_PARAMS("remove_widget", "Widget", true, 60, HandleRemoveWidget),
		AI_COMMAND_PARAMS("move_widget", "Widget", true, 60, HandleMoveWidget),
		AI_COMMAND_PARAMS("set_widget_property", "Widget", true, 60, HandleSetWidgetProperty),
		AI_COMMAND_PARAMS("set_slot_property", "Widget", true, 60, HandleSetSlotProperty),
		AI_COMMAND_PARAMS("set_canvas_slot_layout", "Widget", true, 60, HandleSetCanvasSlotLayout),
		AI_COMMAND_PARAMS("set_widget_properties", "Widget", true, 60, HandleSetWidgetProperties),
		AI_COMMAND_PARAMS("compile_and_save", "Widget", true, 60, HandleCompileAndSave),
		AI_COMMAND_PARAMS("get_widget_tree", "Widget", false, 60, HandleGetWidgetTree),
		AI_COMMAND_NO_PARAMS("list_widget_classes", "Widget", 60, HandleListWidgetClasses),

		AI_COMMAND_PARAMS("set_cdo_property", "CDO", true, 120, HandleSetCDOProperty),
		AI_COMMAND_PARAMS("get_cdo_properties", "CDO", false, 60, HandleGetCDOProperties),
		AI_COMMAND_PARAMS("add_cdo_array_element", "CDOArray", true, 60, HandleAddCDOArrayElement),
		AI_COMMAND_PARAMS("set_cdo_array_element_property", "CDOArray", true, 60, HandleSetCDOArrayElementProperty),
		AI_COMMAND_PARAMS("remove_cdo_array_element", "CDOArray", true, 60, HandleRemoveCDOArrayElement),
		AI_COMMAND_PARAMS("get_cdo_array_length", "CDOArray", false, 60, HandleGetCDOArrayLength),
		AI_COMMAND_PARAMS("object_query", "Reflection", false, 60, HandleObjectQuery),
		AI_COMMAND_PARAMS("object_get_property", "Reflection", false, 60, HandleObjectGetProperty),
		AI_COMMAND_PARAMS("object_set_property", "Reflection", true, 60, HandleObjectSetProperty),
		AI_COMMAND_PARAMS("object_call_function", "Reflection", true, 60, HandleObjectCallFunction),
		AI_COMMAND_PARAMS("reflect_class", "Reflection", false, 60, HandleReflectClass),
		AI_COMMAND_PARAMS("reflect_struct", "Reflection", false, 60, HandleReflectStruct),
		AI_COMMAND_PARAMS("reflect_enum", "Reflection", false, 60, HandleReflectEnum),
		AI_COMMAND_OPTIONAL_PARAMS("list_classes", "Reflection", false, 60, HandleListClasses),
		AI_COMMAND_OPTIONAL_PARAMS("list_gameplay_tags", "Reflection", false, 60, HandleListGameplayTags),

		AI_COMMAND_PARAMS("add_event_node", "BlueprintGraph", true, 60, HandleAddEventNode),
		AI_COMMAND_PARAMS("add_custom_event", "BlueprintGraph", true, 60, HandleAddCustomEvent),
		AI_COMMAND_PARAMS("add_function_call", "BlueprintGraph", true, 60, HandleAddFunctionCallNode),
		AI_COMMAND_PARAMS("add_variable_get_node", "BlueprintGraph", true, 60, HandleAddVariableGetNode),
		AI_COMMAND_PARAMS("add_variable_set_node", "BlueprintGraph", true, 60, HandleAddVariableSetNode),
		AI_COMMAND_PARAMS("add_make_struct_node", "BlueprintGraph", true, 60, HandleAddMakeStructNode),
		AI_COMMAND_PARAMS("add_branch_node", "BlueprintGraph", true, 60, HandleAddBranchNode),
		AI_COMMAND_PARAMS("ensure_function_graph", "BlueprintGraph", true, 60, HandleEnsureFunctionGraph),
		AI_COMMAND_PARAMS("add_call_parent_function", "BlueprintGraph", true, 60, HandleAddCallParentFunction),
		AI_COMMAND_PARAMS("connect_pins", "BlueprintGraph", true, 60, HandleConnectPins),
		AI_COMMAND_PARAMS("set_pin_default", "BlueprintGraph", true, 60, HandleSetPinDefault),
		AI_COMMAND_PARAMS("remove_graph_node", "BlueprintGraph", true, 60, HandleRemoveGraphNode),
		AI_COMMAND_PARAMS("get_graph", "BlueprintGraph", false, 60, HandleGetGraph),
		AI_COMMAND_PARAMS("list_graphs", "BlueprintGraph", false, 60, HandleListGraphs),

		AI_COMMAND_PARAMS("add_variable", "BlueprintVariable", true, 60, HandleAddVariable),
		AI_COMMAND_PARAMS("set_variable_default", "BlueprintVariable", true, 60, HandleSetVariableDefault),
		AI_COMMAND_PARAMS("remove_variable", "BlueprintVariable", true, 60, HandleRemoveVariable),
		AI_COMMAND_PARAMS("get_variables", "BlueprintVariable", false, 60, HandleGetVariables),
		AI_COMMAND_PARAMS("reparent_blueprint", "BlueprintUtility", true, 60, HandleReparentBlueprint),
		AI_COMMAND_PARAMS("blueprint_component_list", "BlueprintComponent", false, 60, HandleBlueprintComponentList),
		AI_COMMAND_PARAMS("blueprint_component_add", "BlueprintComponent", true, 60, HandleBlueprintComponentAdd),
		AI_COMMAND_PARAMS("blueprint_component_remove", "BlueprintComponent", true, 60, HandleBlueprintComponentRemove),
		AI_COMMAND_PARAMS("blueprint_component_set_property", "BlueprintComponent", true, 60, HandleBlueprintComponentSetProperty),

		AI_COMMAND_PARAMS("create_material", "Material", true, 60, HandleCreateMaterial),
		AI_COMMAND_PARAMS("set_material_property", "Material", true, 60, HandleSetMaterialProperty),
		AI_COMMAND_PARAMS("add_expression", "Material", true, 60, HandleAddExpression),
		AI_COMMAND_PARAMS("set_expression_property", "Material", true, 60, HandleSetExpressionProperty),
		AI_COMMAND_PARAMS("connect_expressions", "Material", true, 60, HandleConnectExpressions),
		AI_COMMAND_PARAMS("connect_to_material_property", "Material", true, 60, HandleConnectToMaterialProperty),
		AI_COMMAND_PARAMS("disconnect_input", "Material", true, 60, HandleDisconnectInput),
		AI_COMMAND_PARAMS("remove_expression", "Material", true, 60, HandleRemoveExpression),
		AI_COMMAND_PARAMS("compile_material", "Material", true, 120, HandleCompileMaterial),
		AI_COMMAND_PARAMS("get_material_graph", "Material", false, 60, HandleGetMaterialGraph),
		AI_COMMAND_NO_PARAMS("list_expression_classes", "Material", 60, HandleListExpressionClasses),
		AI_COMMAND_PARAMS("create_material_instance", "Material", true, 60, HandleCreateMaterialInstance),
		AI_COMMAND_PARAMS("set_instance_parameter", "Material", true, 60, HandleSetInstanceParameter),
		AI_COMMAND_PARAMS("save_material_instance", "Material", true, 60, HandleSaveMaterialInstance),
		AI_COMMAND_PARAMS("get_material_instance_info", "Material", false, 60, HandleGetMaterialInstanceInfo),

		AI_COMMAND_PARAMS("save_data_asset", "DataAsset", true, 60, HandleSaveDataAsset),
		AI_COMMAND_PARAMS("create_datatable", "DataTable", true, 60, HandleCreateDataTable),
		AI_COMMAND_PARAMS("get_datatable_info", "DataTable", false, 60, HandleGetDataTableInfo),
		AI_COMMAND_PARAMS("read_datatable_rows", "DataTable", false, 60, HandleReadDataTableRows),
		AI_COMMAND_PARAMS("add_datatable_row", "DataTable", true, 60, HandleAddDataTableRow),
		AI_COMMAND_PARAMS("remove_datatable_row", "DataTable", true, 60, HandleRemoveDataTableRow),
		AI_COMMAND_PARAMS("import_datatable_csv", "DataTable", true, 60, HandleImportDataTableCsv),

		AI_COMMAND_PARAMS("create_asset", "Asset", true, 60, HandleCreateAsset),
		AI_COMMAND_PARAMS("set_asset_property", "Asset", true, 60, HandleSetAssetProperty),
		AI_COMMAND_PARAMS("get_asset_properties", "Asset", false, 60, HandleGetAssetProperties),
		AI_COMMAND_PARAMS("asset_exists", "Asset", false, 30, HandleAssetExists),
		AI_COMMAND_PARAMS("scan_asset_paths", "Asset", false, 60, HandleScanAssetPaths),
		AI_COMMAND_OPTIONAL_PARAMS("asset_search", "Asset", false, 60, HandleAssetSearch),
		AI_COMMAND_PARAMS("asset_validate_light", "Asset", false, 60, HandleAssetValidateLight),
		AI_COMMAND_PARAMS("save_asset", "Asset", true, 60, HandleSaveAsset),
		AI_COMMAND_PARAMS("rename_asset", "Asset", true, 120, HandleRenameAsset),
		AI_COMMAND_PARAMS("get_referencers", "Asset", false, 60, HandleGetReferencers),
		AI_COMMAND_PARAMS("get_dependencies", "Asset", false, 60, HandleGetDependencies),
		AI_COMMAND_PARAMS_SCOPE("delete_asset", "Asset", true, 120, "destructive", true, true, HandleDeleteAsset),
		AI_COMMAND_PARAMS("list_redirectors", "Asset", false, 60, HandleListRedirectors),
		AI_COMMAND_PARAMS("fixup_redirectors", "Asset", true, 120, HandleFixupRedirectors),
		AI_COMMAND_PARAMS("static_mesh_info", "StaticMesh", false, 60, HandleStaticMeshInfo),
		AI_COMMAND_PARAMS("skeletal_mesh_info", "SkeletalMesh", false, 60, HandleSkeletalMeshInfo),

		AI_COMMAND_PARAMS("add_input_mapping", "Input", true, 60, HandleAddInputMapping),
		AI_COMMAND_PARAMS("remove_input_mapping", "Input", true, 60, HandleRemoveInputMapping),
		AI_COMMAND_PARAMS("get_input_mappings", "Input", false, 60, HandleGetInputMappings),

		AI_COMMAND_PARAMS("create_anim_blueprint", "AnimBlueprint", true, 60, HandleCreateAnimBlueprint),
		AI_COMMAND_PARAMS("get_anim_blueprint_info", "AnimBlueprint", false, 60, HandleGetAnimBlueprintInfo),
		AI_COMMAND_PARAMS("animation_asset_info", "AnimationAsset", false, 60, HandleAnimationAssetInfo),
		AI_COMMAND_PARAMS("sequencer_asset_info", "Sequencer", false, 60, HandleSequencerAssetInfo),
		AI_COMMAND_PARAMS("spline_actor_create", "Spline", true, 60, HandleSplineActorCreate),
		AI_COMMAND_PARAMS("spline_component_info", "Spline", false, 60, HandleSplineComponentInfo),
		AI_COMMAND_PARAMS("spline_component_set_points", "Spline", true, 60, HandleSplineComponentSetPoints),
		AI_COMMAND_OPTIONAL_PARAMS("landscape_info", "Landscape", false, 60, HandleLandscapeInfo),
		AI_COMMAND_PARAMS("landscape_sample_height", "Landscape", false, 60, HandleLandscapeSampleHeight),
		AI_COMMAND_OPTIONAL_PARAMS("foliage_info", "Foliage", false, 60, HandleFoliageInfo),
		AI_COMMAND_PARAMS("foliage_sample_instances", "Foliage", false, 60, HandleFoliageSampleInstances),
		AI_COMMAND_PARAMS("foliage_type_settings", "Foliage", false, 60, HandleFoliageTypeSettings),
		AI_COMMAND_PARAMS("pcg_graph_info", "PCG", false, 60, HandlePCGGraphInfo),
		AI_COMMAND_OPTIONAL_PARAMS("pcg_component_info", "PCG", false, 60, HandlePCGComponentInfo),
		AI_COMMAND_PARAMS("niagara_asset_info", "Niagara", false, 60, HandleNiagaraAssetInfo),

		AI_COMMAND_PARAMS("import_texture", "Import", true, 60, HandleImportTexture),
		AI_COMMAND_PARAMS("import_font", "Import", true, 60, HandleImportFont),

		AI_COMMAND_PARAMS("capture_widget_preview", "WidgetPreview", false, 120, HandleCaptureWidgetPreview),
		AI_COMMAND_PARAMS("reload_asset", "AssetLifecycle", false, 30, HandleReloadAsset),
	};

#undef AI_COMMAND_PARAMS
#undef AI_COMMAND_PARAMS_SCOPE
#undef AI_COMMAND_OPTIONAL_PARAMS
#undef AI_COMMAND_NO_PARAMS
#undef AI_COMMAND_NO_PARAMS_SCOPE

	return Commands;
}

const FAIExportTCPServer::FCommandDescriptor* FAIExportTCPServer::FindCommandDescriptor(const FString& CommandType)
{
	for (const FCommandDescriptor& Descriptor : GetCommandDescriptors())
	{
		if (CommandType == Descriptor.Name)
		{
			return &Descriptor;
		}
	}

	return nullptr;
}

CommonAIExport::CommandDispatch::FAIExportCommandDescriptor FAIExportTCPServer::BuildDispatchDescriptor(const FCommandDescriptor& Descriptor)
{
	CommonAIExport::CommandDispatch::FAIExportCommandDescriptor DispatchDescriptor;
	DispatchDescriptor.Name = Descriptor.Name;
	DispatchDescriptor.Category = Descriptor.Category;
	DispatchDescriptor.bRequiresParams = Descriptor.bRequiresParams;
	DispatchDescriptor.bMutating = Descriptor.bMutating;
	DispatchDescriptor.TimeoutSeconds = Descriptor.TimeoutSeconds;
	DispatchDescriptor.RequiredScope = Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read");
	DispatchDescriptor.bSupportsDryRun = Descriptor.bSupportsDryRun;
	DispatchDescriptor.bAsyncCandidate = Descriptor.bAsyncCandidate;
	return DispatchDescriptor;
}

FString FAIExportTCPServer::DispatchCommand(const FCommandDescriptor& Descriptor, TSharedPtr<FJsonObject> Params)
{
	if (Descriptor.ParamsHandler)
	{
		return (this->*Descriptor.ParamsHandler)(Params);
	}

	if (Descriptor.NoParamsHandler)
	{
		return (this->*Descriptor.NoParamsHandler)();
	}

	return CreateErrorResponse(FString::Printf(TEXT("Command '%s' has no registered handler"), Descriptor.Name));
}

CommonAIExport::CommandHandlers::Utility::FAIExportUtilityContext FAIExportTCPServer::BuildUtilityContext() const
{
	CommonAIExport::CommandHandlers::Utility::FAIExportUtilityContext Context;
	Context.ServerPort = ServerPort;
	Context.ActiveClientConnections = TcpTransport.GetActiveClientConnections();
	Context.EditorInstanceId = EditorInstanceId;
	Context.EditorRegistryFilePath = EditorRegistryFilePath;
	Context.ServerStartedAtUtc = ServerStartedAtUtc;
	Context.PortFilePath = FPaths::ConvertRelativePathToFull(GetPortFilePath());
	Context.ManifestSource = TEXT("FAIExportTCPServer::GetCommandDescriptors");
	Context.HttpStatus = HttpMcpServer.GetStatus();
	Context.TaskCounts = AsyncJobStore.GetCounts();

	for (const FCommandDescriptor& Descriptor : GetCommandDescriptors())
	{
		CommonAIExport::CommandHandlers::Utility::FAIExportUtilityCommandDescriptor UtilityDescriptor;
		UtilityDescriptor.Name = Descriptor.Name;
		UtilityDescriptor.Category = Descriptor.Category;
		UtilityDescriptor.bRequiresParams = Descriptor.bRequiresParams;
		UtilityDescriptor.bMutating = Descriptor.bMutating;
		UtilityDescriptor.TimeoutSeconds = Descriptor.TimeoutSeconds;
		UtilityDescriptor.RequiredScope = Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read");
		UtilityDescriptor.bSupportsDryRun = Descriptor.bSupportsDryRun;
		UtilityDescriptor.bAsyncCandidate = Descriptor.bAsyncCandidate;
		Context.Commands.Add(MoveTemp(UtilityDescriptor));
	}

	return Context;
}

void FAIExportTCPServer::StartHttpServer()
{
	CommonAIExport::HttpMcp::FAIExportHttpMcpCallbacks Callbacks;
	Callbacks.HandlePing = [this]() { return HandlePing(); };
	Callbacks.HandleListCommands = [this]() { return HandleListCommands(); };
	Callbacks.HandleProjectStatus = [this]() { return HandleProjectStatus(); };
	Callbacks.HandleEditorIdentity = [this]() { return HandleEditorIdentity(); };
	Callbacks.HandleEditorLogRead = [this](TSharedPtr<FJsonObject> Params) { return HandleEditorLogRead(Params); };
	Callbacks.HandleTaskEvents = [this](TSharedPtr<FJsonObject> Params) { return HandleTaskEvents(Params); };
	Callbacks.HandleTaskEventsWait = [this](TSharedPtr<FJsonObject> Params) { return HandleTaskEventsWait(Params); };
	Callbacks.BuildTaskEventsSse = [this](TSharedPtr<FJsonObject> Params) { return AsyncJobStore.BuildTaskEventsSse(Params); };
	Callbacks.BuildTaskEventParamsFromHttpRequest = [this](const FHttpServerRequest& Request)
	{
		return AsyncJobStore.BuildTaskEventParamsFromHttpRequest(Request);
	};
	Callbacks.ProcessCommand = [this](const FString& JsonCommand) { return ProcessCommand(JsonCommand); };
	Callbacks.GetToolDescriptors = []()
	{
		TArray<CommonAIExport::HttpMcp::FAIExportHttpMcpToolDescriptor> Tools;
		for (const FCommandDescriptor& Descriptor : GetCommandDescriptors())
		{
			CommonAIExport::HttpMcp::FAIExportHttpMcpToolDescriptor Tool;
			Tool.Name = Descriptor.Name;
			Tool.Category = Descriptor.Category;
			Tool.RequiredScope = Descriptor.RequiredScope ? Descriptor.RequiredScope : TEXT("read");
			Tool.bMutating = Descriptor.bMutating;
			Tool.bSupportsDryRun = Descriptor.bSupportsDryRun;
			Tools.Add(MoveTemp(Tool));
		}
		return Tools;
	};
	Callbacks.IsStopRequested = [this]() { return bStopRequested.Load(); };
	Callbacks.FindAvailablePort = [](int32 StartPort, int32 EndPort)
	{
		return FAIExportTCPServer::FindAvailablePort(StartPort, EndPort);
	};

	HttpMcpServer.Start(Callbacks);
}

void FAIExportTCPServer::StopHttpServer()
{
	HttpMcpServer.Stop();
}

void FAIExportTCPServer::WriteEditorRegistryFile()
{
	if (EditorRegistryFilePath.IsEmpty())
	{
		EditorRegistryFilePath = GetEditorRegistryFilePath(ServerPort);
	}

	const FString RegistryDir = GetEditorRegistryDir();
	IFileManager::Get().MakeDirectory(*RegistryDir, true);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(CommonAIExport::CommandHandlers::Utility::BuildEditorIdentityJson(BuildUtilityContext()).ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *EditorRegistryFilePath))
	{
		UE_LOG(LogAIExport, Log, TEXT("Written editor registry entry: %s"), *EditorRegistryFilePath);
	}
	else
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to write editor registry entry: %s"), *EditorRegistryFilePath);
	}
}

void FAIExportTCPServer::RemoveEditorRegistryFile()
{
	if (EditorRegistryFilePath.IsEmpty())
	{
		return;
	}

	if (IFileManager::Get().FileExists(*EditorRegistryFilePath))
	{
		IFileManager::Get().Delete(*EditorRegistryFilePath, false, true, true);
		UE_LOG(LogAIExport, Log, TEXT("Removed editor registry entry: %s"), *EditorRegistryFilePath);
	}
}

bool FAIExportTCPServer::Init()
{
	return true;
}

uint32 FAIExportTCPServer::Run()
{
	CommonAIExport::Transport::FAIExportTcpTransportCallbacks Callbacks;
	Callbacks.ProcessCommand = [this](const FString& JsonCommand)
	{
		return ProcessCommand(JsonCommand);
	};
	Callbacks.IsStopRequested = [this]()
	{
		return bStopRequested.Load();
	};
	Callbacks.OnListening = [this]()
	{
		bIsRunning = true;
		WriteEditorRegistryFile();
	};
	Callbacks.OnStopped = [this]()
	{
		bIsRunning = false;
	};
	return TcpTransport.Run(ServerPort, Callbacks);
}

void FAIExportTCPServer::Stop()
{
	bStopRequested = true;
	TcpTransport.Stop();
}

void FAIExportTCPServer::Exit()
{
	bIsRunning = false;
}

void FAIExportTCPServer::StartServer()
{
	if (bIsRunning)
	{
		UE_LOG(LogAIExport, Warning, TEXT("TCP Server already running"));
		return;
	}

	// Find an available port
	ServerPort = FindAvailablePort(55560, 55600);
	EditorInstanceId = FString::Printf(TEXT("%s-%u-%d"), FApp::GetProjectName(), FPlatformProcess::GetCurrentProcessId(), ServerPort);
	EditorRegistryFilePath = GetEditorRegistryFilePath(ServerPort);
	ServerStartedAtUtc = FDateTime::UtcNow().ToIso8601();

	// Write port to discovery file
	WritePortFile(ServerPort);
	StartHttpServer();

	bStopRequested = false;
	ServerThread = FRunnableThread::Create(this, TEXT("AIExportTCPServerThread"), 0, TPri_Normal);

	if (!ServerThread)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to create server thread"));
	}
}

void FAIExportTCPServer::StopServer()
{
	if (!bIsRunning && !ServerThread)
	{
		return;
	}

	UE_LOG(LogAIExport, Log, TEXT("Stopping AIExport TCP Server..."));

	StopHttpServer();
	bStopRequested = true;
	TcpTransport.Stop();

	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}

	RemoveEditorRegistryFile();
}

FString FAIExportTCPServer::ProcessCommand(const FString& JsonCommand)
{
	CommonAIExport::CommandDispatch::FAIExportCommandProcessorCallbacks Callbacks;
	Callbacks.ResolveCommand = [](const FString& CommandName, CommonAIExport::CommandDispatch::FAIExportCommandDescriptor& OutDescriptor)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		if (!Descriptor)
		{
			return false;
		}

		OutDescriptor = BuildDispatchDescriptor(*Descriptor);
		return true;
	};
	Callbacks.DispatchCommand = [this](const FString& CommandName, TSharedPtr<FJsonObject> Params)
	{
		const FCommandDescriptor* Descriptor = FindCommandDescriptor(CommandName);
		return Descriptor ? DispatchCommand(*Descriptor, Params) : CreateErrorResponse(FString::Printf(TEXT("Unknown command: %s"), *CommandName));
	};
	return CommonAIExport::CommandDispatch::ProcessCommandEnvelope(JsonCommand, Callbacks);
}

FString FAIExportTCPServer::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	return OutputString;
}

FString FAIExportTCPServer::CreateSuccessResponse(TSharedPtr<FJsonObject> Data)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), true);

	if (Data.IsValid())
	{
		Response->SetObjectField(TEXT("data"), Data);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	return OutputString;
}


//////////////////////////////////////////////////////////////////////////
// Widget Preview Capture — IFTP verify loop

//////////////////////////////////////////////////////////////////////////
// Asset Lifecycle — Reload asset (fixes cached editor tab after compile_and_save)

