// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandDispatch/MCTCommandDispatch.h"
#include "CommandHandlers/MCTAsyncJobStore.h"
#include "CommandHandlers/MCTUtilityCommands.h"
#include "HttpMcp/MCTHttpMcpServer.h"
#include "Transport/MCTTcpTransport.h"
#include "HAL/CriticalSection.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeCounter.h"


/**
 * TCP Server Runnable for AI Export commands.
 * Listens on port 55560 for JSON commands from external tools and Python scripts.
 *
 * Supported commands:
 * - ping: Connection test
 * - list_commands: Registered command manifest
 * - export_widget: Export Widget Blueprint
 * - export_blueprint: Export regular Blueprint
 * - list_supported_types: List supported asset types
 *
 * Widget Builder commands:
 * - create_widget_blueprint: Create a new Widget Blueprint asset
 * - add_widget: Add a widget to the widget tree
 * - remove_widget: Remove a widget from the widget tree
 * - move_widget: Move a widget to a new parent
 * - set_widget_property: Set a widget property via reflection
 * - set_slot_property: Set a slot property via reflection
 * - set_canvas_slot_layout: Set canvas slot layout (convenience)
 * - set_widget_properties: Batch set multiple properties
 * - compile_and_save: Compile and save a Widget Blueprint
 * - get_widget_tree: Get widget tree as JSON
 * - list_widget_classes: List available widget classes
 *
 * CDO Property commands:
 * - set_cdo_property: Set a Class Default Object property
 * - get_cdo_properties: Get CDO properties as JSON
 *
 * CDO Array commands:
 * - add_cdo_array_element: Add element to CDO array property
 * - set_cdo_array_element_property: Set sub-property on array element
 * - remove_cdo_array_element: Remove element from CDO array
 * - get_cdo_array_length: Get CDO array length
 *
 * Blueprint Graph commands:
 * - add_event_node: Add event override node to graph
 * - add_custom_event: Add custom event node
 * - add_function_call: Add function call node
 * - add_variable_get_node: Add variable Get node
 * - add_variable_set_node: Add variable Set node
 * - add_make_struct_node: Add Make Struct node
 * - add_branch_node: Add Branch (if) node
 * - ensure_function_graph: Create or update a Blueprint function graph
 * - connect_pins: Connect pins between nodes
 * - set_pin_default: Set pin default value
 * - remove_graph_node: Remove a node from the graph
 * - get_graph: Get graph as JSON
 * - list_graphs: List all graphs
 *
 * Blueprint Variable commands:
 * - add_variable: Add a member variable
 * - set_variable_default: Set variable default value
 * - remove_variable: Remove a variable
 * - get_variables: Get all variables as JSON
 *
 * Blueprint Utility commands:
 * - reparent_blueprint: Change the parent class of a Blueprint
 *
 * Blueprint Component commands:
 * - blueprint_component_list: List SCS components on Actor Blueprints
 * - blueprint_component_add: Add an SCS component node
 * - blueprint_component_remove: Remove an SCS component node
 * - blueprint_component_set_property: Set a component template property
 *
 * Material Builder commands:
 * - create_material: Create a new Material asset
 * - set_material_property: Set material domain/blend/shading
 * - add_expression: Add expression node to material graph
 * - set_expression_property: Set expression property via reflection
 * - connect_expressions: Connect two expression nodes
 * - connect_to_material_property: Connect node to material root input
 * - disconnect_input: Disconnect an expression input
 * - remove_expression: Remove expression from graph
 * - compile_material: Recompile and save material
 * - get_material_graph: Get material graph as JSON
 * - list_expression_classes: List available expression types
 * - create_material_instance: Create Material Instance Constant
 * - set_instance_parameter: Set scalar/vector/texture parameter
 * - save_material_instance: Save MIC to disk
 * - get_material_instance_info: Get MIC info as JSON
 *
 * Data Asset commands:
 * - save_data_asset: Save a Data Asset to disk (no compile)
 *
 * Generic Asset Factory commands:
 * - create_asset: Create InputAction, InputMappingContext, Sound*, PhysicalMaterial
 * - set_asset_property: Set property on any loaded asset (reflection-based)
 * - get_asset_properties: Get all properties of any loaded asset as JSON
 * - save_asset: Save any loaded asset to disk
 * - rename_asset: Rename/move an asset (creates redirector, fixes references via AssetTools)
 * - delete_asset: Delete an asset from disk (ObjectTools::DeleteAssets, optional force=true bypasses ref check)
 *
 * Input Mapping Context commands:
 * - add_input_mapping: Add a key mapping to InputMappingContext
 * - remove_input_mapping: Remove a mapping by index
 * - get_input_mappings: Get all mappings as JSON
 *
 * AnimBlueprint Builder commands:
 * - create_anim_blueprint: Create AnimBlueprint with skeleton
 * - get_anim_blueprint_info: Get AnimBP info as JSON
 *
 * Animation Asset commands:
 * - animation_asset_info: Inspect AnimSequence/AnimMontage timing, skeleton, curves, notifies, tracks, sections, and slots
 *
 * Sequencer commands:
 * - sequencer_asset_info: Inspect LevelSequence MovieScene bindings, tracks, and sections
 *
 * Spline commands:
 * - spline_actor_create: Create an editor actor with a SplineComponent
 * - spline_component_info: Inspect spline components and control points
 * - spline_component_set_points: Replace spline control points on an editor actor
 *
 * Landscape commands:
 * - landscape_info: Inspect Landscape proxies, components, target layers, and Landscape splines
 * - landscape_sample_height: Sample Landscape height/normal at an XY point
 *
 * Foliage commands:
 * - foliage_info: Inspect InstancedFoliageActors, FoliageTypes, settings, components, and counts
 * - foliage_sample_instances: Sample Foliage instance transforms around a world-space point
 * - foliage_type_settings: Inspect a FoliageType asset's placement/rendering settings
 *
 * Level Structure commands:
 * - level_structure_info: Inspect streaming levels, World Partition, Data Layers, Level Instances, and HLOD
 *
 * PCG commands:
 * - pcg_graph_info: Inspect PCGGraph nodes, pins, edges, and settings
 * - pcg_component_info: Inspect PCGComponents and generation state in a world
 *
 * Niagara commands:
 * - niagara_asset_info: Inspect NiagaraSystem, NiagaraEmitter, and NiagaraScript diagnostics
 *
 * Static Mesh commands:
 * - static_mesh_info: Inspect StaticMesh LODs, sections, materials, sockets, collision, and Nanite data
 *
 * Skeletal Mesh commands:
 * - skeletal_mesh_info: Inspect SkeletalMesh LODs, sections, materials, sockets, skeleton, physics, bounds, and Nanite data
 *
 * Asset Import commands:
 * - import_texture: Import a texture file from disk into Content Browser
 * - import_font: Import font files (TTF/OTF) and create a Composite Font asset
 *
 * Widget Preview Capture commands (for IFTP verify loop):
 * - capture_widget_preview: Render a Widget Blueprint to PNG at one or more ratios
 *
 * Asset Lifecycle commands:
 * - reload_asset: Close asset editor, hard reload package, reopen editor (fixes cached tab after compile_and_save)
 */
class FMCTTcpServer : public FRunnable
{
public:
	FMCTTcpServer();
	virtual ~FMCTTcpServer();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

	/** Start the TCP server on a background thread */
	void StartServer();

	/** Stop the TCP server and cleanup */
	void StopServer();

	/** Check if server is running */
	bool IsRunning() const { return bIsRunning; }

	/** Get server port */
	int32 GetServerPort() const { return ServerPort; }

	/** Find an available port in the given range */
	static int32 FindAvailablePort(int32 StartPort = 55560, int32 EndPort = 55600);

	/** Write port to discovery file */
	static void WritePortFile(int32 Port);

	/** Get port file path */
	static FString GetPortFilePath();

	/** Get global multi-editor registry directory */
	static FString GetEditorRegistryDir();

	/** Get this process registry file path for a port */
	static FString GetEditorRegistryFilePath(int32 Port);

private:
	struct FCommandDescriptor
	{
		const TCHAR* Name;
		const TCHAR* Category;
		bool bRequiresParams;
		bool bMutating;
		int32 TimeoutSeconds;
		const TCHAR* RequiredScope;
		bool bSupportsDryRun;
		bool bAsyncCandidate;
		FString (FMCTTcpServer::*ParamsHandler)(TSharedPtr<class FJsonObject> Params);
		FString (FMCTTcpServer::*NoParamsHandler)();
	};

	/** Registered TCP command descriptors used for dispatch and introspection */
	static const TArray<FCommandDescriptor>& GetCommandDescriptors();

	/** Find a registered command descriptor by protocol command name */
	static const FCommandDescriptor* FindCommandDescriptor(const FString& CommandType);

	/** Convert the private handler descriptor to the shared dispatch shape */
	static MCPToolkit::CommandDispatch::FMCTCommandDescriptor BuildDispatchDescriptor(const FCommandDescriptor& Descriptor);

	/** Dispatch a descriptor after validation has completed */
	FString DispatchCommand(const FCommandDescriptor& Descriptor, TSharedPtr<class FJsonObject> Params);

	/** Build utility/status context passed to the utility command module */
	MCPToolkit::CommandHandlers::Utility::FMCTUtilityContext BuildUtilityContext() const;

	/** Start/stop native localhost HTTP/MCP compatibility routes */
	void StartHttpServer();
	void StopHttpServer();

	/** Write this editor instance to the global discovery registry */
	void WriteEditorRegistryFile();

	/** Remove this editor instance from the global discovery registry */
	void RemoveEditorRegistryFile();

	/** Process a JSON command and return JSON response */
	FString ProcessCommand(const FString& JsonCommand);

	/** Command handlers — Export */
	FString HandlePing();
	FString HandleListCommands();
	FString HandleServerStatus();
	FString HandleEditorIdentity();
	FString HandleCommandManifestExport(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectStatus();
	FString HandleSourceControlStatus(TSharedPtr<class FJsonObject> Params);
	FString HandleSourceControlLog(TSharedPtr<class FJsonObject> Params);
	FString HandleSourceControlShow(TSharedPtr<class FJsonObject> Params);
	FString HandleSourceControlDiff(TSharedPtr<class FJsonObject> Params);
	FString HandleBuildProject(TSharedPtr<class FJsonObject> Params);
	FString HandleGenerateProjectFiles(TSharedPtr<class FJsonObject> Params);
	FString HandleCookProject(TSharedPtr<class FJsonObject> Params);
	FString HandleListTests(TSharedPtr<class FJsonObject> Params);
	FString HandleRunTests(TSharedPtr<class FJsonObject> Params);
	FString HandleGetTestLog(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectPluginList(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectPluginSetEnabled(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectModuleList(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectConfigGet(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectConfigSet(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectConfigDelete(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectConfigListSections(TSharedPtr<class FJsonObject> Params);
	FString HandleProjectConfigListKeys(TSharedPtr<class FJsonObject> Params);
	FString HandleTaskSubmit(TSharedPtr<class FJsonObject> Params);
	FString HandleTaskStatus(TSharedPtr<class FJsonObject> Params);
	FString HandleTaskResult(TSharedPtr<class FJsonObject> Params);
	FString HandleTaskCancel(TSharedPtr<class FJsonObject> Params);
	FString HandleTaskEvents(TSharedPtr<class FJsonObject> Params);
	FString HandleTaskEventsWait(TSharedPtr<class FJsonObject> Params);
	FString HandleExportWidget(TSharedPtr<class FJsonObject> Params);
	FString HandleExportBlueprint(TSharedPtr<class FJsonObject> Params);
	FString HandleListSupportedTypes();

	/** Command handlers - Editor / Level / Actor */
	FString HandleEditorWorldInfo();
	FString HandleRuntimeWorldInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimePlayerList(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeComponentList(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeInputRouting(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeReplicationDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeAbilitySystemDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeAIPerceptionDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeAIControllerDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeEQSDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeGameplayTagsDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeCommonUIDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeAudioDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeNavigationDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeAssetStreamingDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeAsyncLoadDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeGameInstanceDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeLevelTravelDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeMultiplayerConnectionDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeTickTimerLatentDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimeSchedulerPerformanceDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleRuntimePhysicsCollisionDiagnostics(TSharedPtr<class FJsonObject> Params);
	FString HandleActorList(TSharedPtr<class FJsonObject> Params);
	FString HandleActorSpawn(TSharedPtr<class FJsonObject> Params);
	FString HandleActorSetTransform(TSharedPtr<class FJsonObject> Params);
	FString HandleActorDelete(TSharedPtr<class FJsonObject> Params);
	FString HandleLevelOpen(TSharedPtr<class FJsonObject> Params);
	FString HandleLevelSaveCurrent();
	FString HandleLevelStructureInfo(TSharedPtr<class FJsonObject> Params);
	FString HandlePIEStatus();
	FString HandlePIEStart();
	FString HandlePIEStop();
	FString HandleEditorConsoleCommand(TSharedPtr<class FJsonObject> Params);
	FString HandleEditorLogRead(TSharedPtr<class FJsonObject> Params);
	FString HandleViewportCapture(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Widget Builder */
	FString HandleCreateWidgetBlueprint(TSharedPtr<class FJsonObject> Params);
	FString HandleAddWidget(TSharedPtr<class FJsonObject> Params);
	FString HandleRemoveWidget(TSharedPtr<class FJsonObject> Params);
	FString HandleMoveWidget(TSharedPtr<class FJsonObject> Params);
	FString HandleSetWidgetProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleSetSlotProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleSetCanvasSlotLayout(TSharedPtr<class FJsonObject> Params);
	FString HandleSetWidgetProperties(TSharedPtr<class FJsonObject> Params);
	FString HandleCompileAndSave(TSharedPtr<class FJsonObject> Params);
	FString HandleGetWidgetTree(TSharedPtr<class FJsonObject> Params);
	FString HandleListWidgetClasses();

	/** Command handlers — CDO Properties */
	FString HandleSetCDOProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleGetCDOProperties(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — CDO Array Properties */
	FString HandleAddCDOArrayElement(TSharedPtr<class FJsonObject> Params);
	FString HandleSetCDOArrayElementProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleRemoveCDOArrayElement(TSharedPtr<class FJsonObject> Params);
	FString HandleGetCDOArrayLength(TSharedPtr<class FJsonObject> Params);
	FString HandleObjectQuery(TSharedPtr<class FJsonObject> Params);
	FString HandleObjectGetProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleObjectSetProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleObjectCallFunction(TSharedPtr<class FJsonObject> Params);
	FString HandleReflectClass(TSharedPtr<class FJsonObject> Params);
	FString HandleReflectStruct(TSharedPtr<class FJsonObject> Params);
	FString HandleReflectEnum(TSharedPtr<class FJsonObject> Params);
	FString HandleListClasses(TSharedPtr<class FJsonObject> Params);
	FString HandleListGameplayTags(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Blueprint Graph */
	FString HandleAddEventNode(TSharedPtr<class FJsonObject> Params);
	FString HandleAddCustomEvent(TSharedPtr<class FJsonObject> Params);
	FString HandleAddFunctionCallNode(TSharedPtr<class FJsonObject> Params);
	FString HandleAddVariableGetNode(TSharedPtr<class FJsonObject> Params);
	FString HandleAddVariableSetNode(TSharedPtr<class FJsonObject> Params);
	FString HandleAddMakeStructNode(TSharedPtr<class FJsonObject> Params);
	FString HandleAddBranchNode(TSharedPtr<class FJsonObject> Params);
	FString HandleAddCallParentFunction(TSharedPtr<class FJsonObject> Params);
	FString HandleEnsureFunctionGraph(TSharedPtr<class FJsonObject> Params);
	FString HandleConnectPins(TSharedPtr<class FJsonObject> Params);
	FString HandleSetPinDefault(TSharedPtr<class FJsonObject> Params);
	FString HandleRemoveGraphNode(TSharedPtr<class FJsonObject> Params);
	FString HandleGetGraph(TSharedPtr<class FJsonObject> Params);
	FString HandleListGraphs(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Blueprint Variables */
	FString HandleAddVariable(TSharedPtr<class FJsonObject> Params);
	FString HandleSetVariableDefault(TSharedPtr<class FJsonObject> Params);
	FString HandleRemoveVariable(TSharedPtr<class FJsonObject> Params);
	FString HandleGetVariables(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Blueprint Utility */
	FString HandleReparentBlueprint(TSharedPtr<class FJsonObject> Params);
	FString HandleBlueprintComponentList(TSharedPtr<class FJsonObject> Params);
	FString HandleBlueprintComponentAdd(TSharedPtr<class FJsonObject> Params);
	FString HandleBlueprintComponentRemove(TSharedPtr<class FJsonObject> Params);
	FString HandleBlueprintComponentSetProperty(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Material Builder */
	FString HandleCreateMaterial(TSharedPtr<class FJsonObject> Params);
	FString HandleSetMaterialProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleAddExpression(TSharedPtr<class FJsonObject> Params);
	FString HandleSetExpressionProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleConnectExpressions(TSharedPtr<class FJsonObject> Params);
	FString HandleConnectToMaterialProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleDisconnectInput(TSharedPtr<class FJsonObject> Params);
	FString HandleRemoveExpression(TSharedPtr<class FJsonObject> Params);
	FString HandleCompileMaterial(TSharedPtr<class FJsonObject> Params);
	FString HandleGetMaterialGraph(TSharedPtr<class FJsonObject> Params);
	FString HandleListExpressionClasses();
	FString HandleCreateMaterialInstance(TSharedPtr<class FJsonObject> Params);
	FString HandleSetInstanceParameter(TSharedPtr<class FJsonObject> Params);
	FString HandleSaveMaterialInstance(TSharedPtr<class FJsonObject> Params);
	FString HandleGetMaterialInstanceInfo(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Data Asset */
	FString HandleSaveDataAsset(TSharedPtr<class FJsonObject> Params);
	FString HandleCreateDataTable(TSharedPtr<class FJsonObject> Params);
	FString HandleGetDataTableInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleReadDataTableRows(TSharedPtr<class FJsonObject> Params);
	FString HandleAddDataTableRow(TSharedPtr<class FJsonObject> Params);
	FString HandleRemoveDataTableRow(TSharedPtr<class FJsonObject> Params);
	FString HandleImportDataTableCsv(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Generic Asset Factory */
	FString HandleCreateAsset(TSharedPtr<class FJsonObject> Params);
	FString HandleSetAssetProperty(TSharedPtr<class FJsonObject> Params);
	FString HandleGetAssetProperties(TSharedPtr<class FJsonObject> Params);
	FString HandleAssetExists(TSharedPtr<class FJsonObject> Params);
	FString HandleScanAssetPaths(TSharedPtr<class FJsonObject> Params);
	FString HandleAssetSearch(TSharedPtr<class FJsonObject> Params);
	FString HandleAssetValidateLight(TSharedPtr<class FJsonObject> Params);
	FString HandleSaveAsset(TSharedPtr<class FJsonObject> Params);
	FString HandleRenameAsset(TSharedPtr<class FJsonObject> Params);
	FString HandleGetReferencers(TSharedPtr<class FJsonObject> Params);
	FString HandleGetDependencies(TSharedPtr<class FJsonObject> Params);
	FString HandleDeleteAsset(TSharedPtr<class FJsonObject> Params);
	FString HandleListRedirectors(TSharedPtr<class FJsonObject> Params);
	FString HandleFixupRedirectors(TSharedPtr<class FJsonObject> Params);
	FString HandleStaticMeshInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleSkeletalMeshInfo(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Input Mapping Context */
	FString HandleAddInputMapping(TSharedPtr<class FJsonObject> Params);
	FString HandleRemoveInputMapping(TSharedPtr<class FJsonObject> Params);
	FString HandleGetInputMappings(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — AnimBlueprint Builder */
	FString HandleCreateAnimBlueprint(TSharedPtr<class FJsonObject> Params);
	FString HandleGetAnimBlueprintInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleAnimationAssetInfo(TSharedPtr<class FJsonObject> Params);
	/** Command handlers - Sequencer */
	FString HandleSequencerAssetInfo(TSharedPtr<class FJsonObject> Params);

	/** Command handlers - Spline */
	FString HandleSplineActorCreate(TSharedPtr<class FJsonObject> Params);
	FString HandleSplineComponentInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleSplineComponentSetPoints(TSharedPtr<class FJsonObject> Params);

	/** Command handlers - Landscape */
	FString HandleLandscapeInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleLandscapeSampleHeight(TSharedPtr<class FJsonObject> Params);

	/** Command handlers - Foliage */
	FString HandleFoliageInfo(TSharedPtr<class FJsonObject> Params);
	FString HandleFoliageSampleInstances(TSharedPtr<class FJsonObject> Params);
	FString HandleFoliageTypeSettings(TSharedPtr<class FJsonObject> Params);

	/** Command handlers - PCG */
	FString HandlePCGGraphInfo(TSharedPtr<class FJsonObject> Params);
	FString HandlePCGComponentInfo(TSharedPtr<class FJsonObject> Params);

	/** Command handlers - Niagara */
	FString HandleNiagaraAssetInfo(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Asset Import */
	FString HandleImportTexture(TSharedPtr<class FJsonObject> Params);
	FString HandleImportFont(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Widget Preview Capture (for IFTP verify loop) */
	FString HandleCaptureWidgetPreview(TSharedPtr<class FJsonObject> Params);

	/** Command handlers — Asset Lifecycle */
	FString HandleReloadAsset(TSharedPtr<class FJsonObject> Params);

	/** Create error response JSON */
	FString CreateErrorResponse(const FString& ErrorMessage);

	/** Create success response JSON */
	FString CreateSuccessResponse(TSharedPtr<class FJsonObject> Data = nullptr);

private:
	/** Server port - dynamically assigned to avoid conflicts */
	int32 ServerPort = 55560;

	/** Native HTTP/MCP port - dynamically assigned to avoid conflicts */
	int32 HttpPort = 0;

	/** Stable identity for this editor TCP server instance */
	FString EditorInstanceId;

	/** Registry file path written by this server instance */
	FString EditorRegistryFilePath;

	/** Server start time in UTC ISO-8601 */
	FString ServerStartedAtUtc;

	/** Native HTTP/MCP compatibility routes */
	MCPToolkit::HttpMcp::FMCTHttpMcpServer HttpMcpServer;

	/** TCP socket transport */
	MCPToolkit::Transport::FMCTTcpTransport TcpTransport;

	/** Server thread */
	FRunnableThread* ServerThread = nullptr;

	/** Running flag - use atomic for thread safety */
	TAtomic<bool> bIsRunning;

	/** Stop requested flag */
	TAtomic<bool> bStopRequested;

	/** Async job registry for long-running commands */
	MCPToolkit::CommandHandlers::FMCTAsyncJobStore AsyncJobStore;

	/** Serializes mutating command execution across TCP/HTTP/thread-pool clients. */
	FCriticalSection MutatingCommandCriticalSection;
};

/**
 * Global accessor for the TCP server instance.
 * Managed by FMCTModule.
 */
class MCPTOOLKIT_API FMCTTcpServerManager
{
public:
	/** Get or create the singleton server instance */
	static FMCTTcpServer* Get();

	/** Start the server */
	static void Start();

	/** Stop and destroy the server */
	static void Stop();

private:
	static TUniquePtr<FMCTTcpServer> Instance;
};
