// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FSocket;

/**
 * TCP Server Runnable for AI Export commands.
 * Listens on port 55560 for JSON commands from external tools (Claude Code, Python scripts).
 *
 * Supported commands:
 * - ping: Connection test
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
 */
class FAIExportTCPServer : public FRunnable
{
public:
	FAIExportTCPServer();
	virtual ~FAIExportTCPServer();

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

private:
	/** Process a single client connection */
	void HandleClientConnection(FSocket* ClientSocket);

	/** Process a JSON command and return JSON response */
	FString ProcessCommand(const FString& JsonCommand);

	/** Command handlers — Export */
	FString HandlePing();
	FString HandleExportWidget(TSharedPtr<class FJsonObject> Params);
	FString HandleExportBlueprint(TSharedPtr<class FJsonObject> Params);
	FString HandleListSupportedTypes();

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

	/** Create error response JSON */
	FString CreateErrorResponse(const FString& ErrorMessage);

	/** Create success response JSON */
	FString CreateSuccessResponse(TSharedPtr<class FJsonObject> Data = nullptr);

private:
	/** Server port - dynamically assigned to avoid conflicts */
	int32 ServerPort = 55560;

	/** Listener socket */
	FSocket* ListenerSocket = nullptr;

	/** Server thread */
	FRunnableThread* ServerThread = nullptr;

	/** Running flag - use atomic for thread safety */
	TAtomic<bool> bIsRunning;

	/** Stop requested flag */
	TAtomic<bool> bStopRequested;
};

/**
 * Global accessor for the TCP server instance.
 * Managed by FCommonAIExportModule.
 */
class COMMONAIEXPORT_API FAIExportTCPServerManager
{
public:
	/** Get or create the singleton server instance */
	static FAIExportTCPServer* Get();

	/** Start the server */
	static void Start();

	/** Stop and destroy the server */
	static void Stop();

private:
	static TUniquePtr<FAIExportTCPServer> Instance;
};
