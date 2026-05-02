// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "AIExportFunctionLibrary.h"
#include "Builders/AIWidgetBlueprintBuilder.h"
#include "Builders/AIMaterialBuilder.h"
#include "Builders/AIBlueprintGraphBuilder.h"
#include "Builders/AIDataAssetBuilder.h"
#include "Builders/AIAssetFactory.h"
#include "Builders/AIAnimBlueprintBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "CommonAIExportModule.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#include "WidgetBlueprint.h"
#include "Components/Widget.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#include "Async/Async.h"
#include "HAL/RunnableThread.h"
#include "Misc/EngineVersion.h"
#include "Misc/App.h"

#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

// Asset rename (HandleRenameAsset)
#include "AssetToolsModule.h"
#include "IAssetTools.h"

// Asset delete (HandleDeleteAsset) — ObjectTools lives in UnrealEd
#include "ObjectTools.h"
#include "UObject/ObjectRedirector.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "InputMappingContext.h"
#include "Animation/AnimBlueprint.h"

// Widget Preview Capture includes (for HandleCaptureWidgetPreview)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "ContentStreaming.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "CommonActivatableWidget.h"
#include "ICommonInputModule.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"
#include "RenderingThread.h"
#include "Engine/Engine.h"
#include "RenderAssetUpdate.h"
#include "Misc/Base64.h"
#include "UObject/UnrealType.h"

// Asset Lifecycle includes (for HandleReloadAsset)
#include "Subsystems/AssetEditorSubsystem.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

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

bool FAIExportTCPServer::Init()
{
	return true;
}

uint32 FAIExportTCPServer::Run()
{
	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server thread started on port %d"), ServerPort);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to get socket subsystem"));
		return 1;
	}

	// Create listener socket
	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("AIExportTCPServer"), false);
	if (!ListenerSocket)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to create listener socket"));
		return 1;
	}

	// Set socket options
	ListenerSocket->SetReuseAddr(true);
	ListenerSocket->SetNoDelay(true);

	// Bind to localhost only (firewall-friendly)
	FIPv4Address LocalAddress;
	FIPv4Address::Parse(TEXT("127.0.0.1"), LocalAddress);
	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	Addr->SetIp(LocalAddress.Value);
	Addr->SetPort(ServerPort);

	if (!ListenerSocket->Bind(*Addr))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to bind to port %d"), ServerPort);
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return 1;
	}

	if (!ListenerSocket->Listen(8))
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to listen on socket"));
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
		return 1;
	}

	bIsRunning = true;
	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server listening on 127.0.0.1:%d"), ServerPort);

	// Main accept loop
	while (!bStopRequested)
	{
		bool bHasPendingConnection = false;
		if (ListenerSocket->WaitForPendingConnection(bHasPendingConnection, FTimespan::FromMilliseconds(100)))
		{
			if (bHasPendingConnection)
			{
				TSharedRef<FInternetAddr> RemoteAddress = SocketSubsystem->CreateInternetAddr();
				FSocket* ClientSocket = ListenerSocket->Accept(*RemoteAddress, TEXT("AIExportClient"));

				if (ClientSocket)
				{
					UE_LOG(LogAIExport, Verbose, TEXT("Client connected"));
					HandleClientConnection(ClientSocket);
					SocketSubsystem->DestroySocket(ClientSocket);
				}
			}
		}
	}

	// Cleanup
	if (ListenerSocket)
	{
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
	}

	bIsRunning = false;
	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server stopped"));

	return 0;
}

void FAIExportTCPServer::Stop()
{
	bStopRequested = true;
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

	// Write port to discovery file
	WritePortFile(ServerPort);

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

	bStopRequested = true;

	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}
}

void FAIExportTCPServer::HandleClientConnection(FSocket* ClientSocket)
{
	if (!ClientSocket)
	{
		return;
	}

	// Set receive buffer size
	int32 ActualSize = 0;
	ClientSocket->SetReceiveBufferSize(65536, ActualSize);

	// Read incoming data
	TArray<uint8> RecvBuffer;
	RecvBuffer.SetNumZeroed(65536);

	int32 BytesRead = 0;

	// Wait for data with timeout
	bool bHasData = false;
	ClientSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0));

	if (ClientSocket->Recv(RecvBuffer.GetData(), RecvBuffer.Num(), BytesRead))
	{
		if (BytesRead > 0)
		{
			// Convert to string
			FString JsonCommand = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(RecvBuffer.GetData())));
			JsonCommand = JsonCommand.Left(BytesRead);

			UE_LOG(LogAIExport, Verbose, TEXT("Received command: %s"), *JsonCommand.Left(200));

			// Process command
			FString Response = ProcessCommand(JsonCommand);

			// Send response
			FTCHARToUTF8 Converter(*Response);
			int32 BytesSent = 0;
			ClientSocket->Send(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length(), BytesSent);

			UE_LOG(LogAIExport, Verbose, TEXT("Sent response: %d bytes"), BytesSent);
		}
	}
	else
	{
		UE_LOG(LogAIExport, Warning, TEXT("Failed to receive data from client"));
	}
}

FString FAIExportTCPServer::ProcessCommand(const FString& JsonCommand)
{
	// Parse JSON
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonCommand);
	TSharedPtr<FJsonObject> RootObject;

	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid JSON format"));
	}

	// Get command type
	FString CommandType;
	if (!RootObject->TryGetStringField(TEXT("type"), CommandType))
	{
		return CreateErrorResponse(TEXT("Missing 'type' field"));
	}

	// Get params (optional)
	TSharedPtr<FJsonObject> Params = RootObject->GetObjectField(TEXT("params"));

	UE_LOG(LogAIExport, Log, TEXT("Processing command: %s"), *CommandType);

	// Dispatch command
	if (CommandType == TEXT("ping"))
	{
		return HandlePing();
	}
	else if (CommandType == TEXT("export_widget"))
	{
		return HandleExportWidget(Params);
	}
	else if (CommandType == TEXT("export_blueprint"))
	{
		return HandleExportBlueprint(Params);
	}
	else if (CommandType == TEXT("list_supported_types"))
	{
		return HandleListSupportedTypes();
	}
	// Widget Builder commands
	else if (CommandType == TEXT("create_widget_blueprint"))
	{
		return HandleCreateWidgetBlueprint(Params);
	}
	else if (CommandType == TEXT("add_widget"))
	{
		return HandleAddWidget(Params);
	}
	else if (CommandType == TEXT("remove_widget"))
	{
		return HandleRemoveWidget(Params);
	}
	else if (CommandType == TEXT("move_widget"))
	{
		return HandleMoveWidget(Params);
	}
	else if (CommandType == TEXT("set_widget_property"))
	{
		return HandleSetWidgetProperty(Params);
	}
	else if (CommandType == TEXT("set_slot_property"))
	{
		return HandleSetSlotProperty(Params);
	}
	else if (CommandType == TEXT("set_canvas_slot_layout"))
	{
		return HandleSetCanvasSlotLayout(Params);
	}
	else if (CommandType == TEXT("set_widget_properties"))
	{
		return HandleSetWidgetProperties(Params);
	}
	else if (CommandType == TEXT("compile_and_save"))
	{
		return HandleCompileAndSave(Params);
	}
	else if (CommandType == TEXT("get_widget_tree"))
	{
		return HandleGetWidgetTree(Params);
	}
	else if (CommandType == TEXT("list_widget_classes"))
	{
		return HandleListWidgetClasses();
	}
	// CDO Property commands
	else if (CommandType == TEXT("set_cdo_property"))
	{
		return HandleSetCDOProperty(Params);
	}
	else if (CommandType == TEXT("get_cdo_properties"))
	{
		return HandleGetCDOProperties(Params);
	}
	// CDO Array commands
	else if (CommandType == TEXT("add_cdo_array_element"))
	{
		return HandleAddCDOArrayElement(Params);
	}
	else if (CommandType == TEXT("set_cdo_array_element_property"))
	{
		return HandleSetCDOArrayElementProperty(Params);
	}
	else if (CommandType == TEXT("remove_cdo_array_element"))
	{
		return HandleRemoveCDOArrayElement(Params);
	}
	else if (CommandType == TEXT("get_cdo_array_length"))
	{
		return HandleGetCDOArrayLength(Params);
	}
	// Blueprint Graph commands
	else if (CommandType == TEXT("add_event_node"))
	{
		return HandleAddEventNode(Params);
	}
	else if (CommandType == TEXT("add_custom_event"))
	{
		return HandleAddCustomEvent(Params);
	}
	else if (CommandType == TEXT("add_function_call"))
	{
		return HandleAddFunctionCallNode(Params);
	}
	else if (CommandType == TEXT("add_variable_get_node"))
	{
		return HandleAddVariableGetNode(Params);
	}
	else if (CommandType == TEXT("add_variable_set_node"))
	{
		return HandleAddVariableSetNode(Params);
	}
	else if (CommandType == TEXT("add_make_struct_node"))
	{
		return HandleAddMakeStructNode(Params);
	}
	else if (CommandType == TEXT("add_branch_node"))
	{
		return HandleAddBranchNode(Params);
	}
	else if (CommandType == TEXT("ensure_function_graph"))
	{
		return HandleEnsureFunctionGraph(Params);
	}
	else if (CommandType == TEXT("add_call_parent_function"))
	{
		return HandleAddCallParentFunction(Params);
	}
	else if (CommandType == TEXT("connect_pins"))
	{
		return HandleConnectPins(Params);
	}
	else if (CommandType == TEXT("set_pin_default"))
	{
		return HandleSetPinDefault(Params);
	}
	else if (CommandType == TEXT("remove_graph_node"))
	{
		return HandleRemoveGraphNode(Params);
	}
	else if (CommandType == TEXT("get_graph"))
	{
		return HandleGetGraph(Params);
	}
	else if (CommandType == TEXT("list_graphs"))
	{
		return HandleListGraphs(Params);
	}
	// Blueprint Variable commands
	else if (CommandType == TEXT("add_variable"))
	{
		return HandleAddVariable(Params);
	}
	else if (CommandType == TEXT("set_variable_default"))
	{
		return HandleSetVariableDefault(Params);
	}
	else if (CommandType == TEXT("remove_variable"))
	{
		return HandleRemoveVariable(Params);
	}
	else if (CommandType == TEXT("get_variables"))
	{
		return HandleGetVariables(Params);
	}
	// Blueprint Utility commands
	else if (CommandType == TEXT("reparent_blueprint"))
	{
		return HandleReparentBlueprint(Params);
	}
	// Material Builder commands
	else if (CommandType == TEXT("create_material"))
	{
		return HandleCreateMaterial(Params);
	}
	else if (CommandType == TEXT("set_material_property"))
	{
		return HandleSetMaterialProperty(Params);
	}
	else if (CommandType == TEXT("add_expression"))
	{
		return HandleAddExpression(Params);
	}
	else if (CommandType == TEXT("set_expression_property"))
	{
		return HandleSetExpressionProperty(Params);
	}
	else if (CommandType == TEXT("connect_expressions"))
	{
		return HandleConnectExpressions(Params);
	}
	else if (CommandType == TEXT("connect_to_material_property"))
	{
		return HandleConnectToMaterialProperty(Params);
	}
	else if (CommandType == TEXT("disconnect_input"))
	{
		return HandleDisconnectInput(Params);
	}
	else if (CommandType == TEXT("remove_expression"))
	{
		return HandleRemoveExpression(Params);
	}
	else if (CommandType == TEXT("compile_material"))
	{
		return HandleCompileMaterial(Params);
	}
	else if (CommandType == TEXT("get_material_graph"))
	{
		return HandleGetMaterialGraph(Params);
	}
	else if (CommandType == TEXT("list_expression_classes"))
	{
		return HandleListExpressionClasses();
	}
	else if (CommandType == TEXT("create_material_instance"))
	{
		return HandleCreateMaterialInstance(Params);
	}
	else if (CommandType == TEXT("set_instance_parameter"))
	{
		return HandleSetInstanceParameter(Params);
	}
	else if (CommandType == TEXT("save_material_instance"))
	{
		return HandleSaveMaterialInstance(Params);
	}
	else if (CommandType == TEXT("get_material_instance_info"))
	{
		return HandleGetMaterialInstanceInfo(Params);
	}
	// Data Asset commands
	else if (CommandType == TEXT("save_data_asset"))
	{
		return HandleSaveDataAsset(Params);
	}
	// Generic Asset Factory commands
	else if (CommandType == TEXT("create_asset"))
	{
		return HandleCreateAsset(Params);
	}
	else if (CommandType == TEXT("set_asset_property"))
	{
		return HandleSetAssetProperty(Params);
	}
	else if (CommandType == TEXT("get_asset_properties"))
	{
		return HandleGetAssetProperties(Params);
	}
	else if (CommandType == TEXT("save_asset"))
	{
		return HandleSaveAsset(Params);
	}
	else if (CommandType == TEXT("rename_asset"))
	{
		return HandleRenameAsset(Params);
	}
	else if (CommandType == TEXT("get_referencers"))
	{
		return HandleGetReferencers(Params);
	}
	else if (CommandType == TEXT("get_dependencies"))
	{
		return HandleGetDependencies(Params);
	}
	else if (CommandType == TEXT("delete_asset"))
	{
		return HandleDeleteAsset(Params);
	}
	else if (CommandType == TEXT("list_redirectors"))
	{
		return HandleListRedirectors(Params);
	}
	else if (CommandType == TEXT("fixup_redirectors"))
	{
		return HandleFixupRedirectors(Params);
	}
	// Input Mapping Context commands
	else if (CommandType == TEXT("add_input_mapping"))
	{
		return HandleAddInputMapping(Params);
	}
	else if (CommandType == TEXT("remove_input_mapping"))
	{
		return HandleRemoveInputMapping(Params);
	}
	else if (CommandType == TEXT("get_input_mappings"))
	{
		return HandleGetInputMappings(Params);
	}
	// AnimBlueprint Builder commands
	else if (CommandType == TEXT("create_anim_blueprint"))
	{
		return HandleCreateAnimBlueprint(Params);
	}
	else if (CommandType == TEXT("get_anim_blueprint_info"))
	{
		return HandleGetAnimBlueprintInfo(Params);
	}
	// Asset Import commands
	else if (CommandType == TEXT("import_texture"))
	{
		return HandleImportTexture(Params);
	}
	else if (CommandType == TEXT("import_font"))
	{
		return HandleImportFont(Params);
	}
	// Widget Preview Capture commands (for IFTP verify loop)
	else if (CommandType == TEXT("capture_widget_preview"))
	{
		return HandleCaptureWidgetPreview(Params);
	}
	// Asset Lifecycle commands
	else if (CommandType == TEXT("reload_asset"))
	{
		return HandleReloadAsset(Params);
	}
	else
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown command: %s"), *CommandType));
	}
}

FString FAIExportTCPServer::HandlePing()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("message"), TEXT("pong"));
	Data->SetStringField(TEXT("server"), TEXT("CommonAIExport"));
	Data->SetNumberField(TEXT("port"), ServerPort);
	Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Data->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - GStartTime);
	return CreateSuccessResponse(Data);
}

FString FAIExportTCPServer::HandleExportWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		FAIExportResult Result = UAIExportFunctionLibrary::ExportWidgetBlueprintByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}

FString FAIExportTCPServer::HandleExportBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString OutputDirectory;
	bool bBothFormats = true;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	// output_directory is optional — omitting it triggers auto-mirrored path
	Params->TryGetStringField(TEXT("output_directory"), OutputDirectory);

	Params->TryGetBoolField(TEXT("both_formats"), bBothFormats);

	// Execute on Game Thread and wait for result
	TSharedPtr<TPromise<FAIExportResult>> Promise = MakeShared<TPromise<FAIExportResult>>();
	TFuture<FAIExportResult> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, OutputDirectory, bBothFormats, Promise]()
	{
		// Use ExportAssetByPath to support all asset types (Blueprint, Audio, Input, etc.)
		FAIExportResult Result = UAIExportFunctionLibrary::ExportAssetByPath(
			AssetPath,
			OutputDirectory,
			bBothFormats
		);
		Promise->SetValue(Result);
	});

	// Wait for result with timeout
	Future.WaitFor(FTimespan::FromSeconds(60.0));

	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Export timed out"));
	}

	FAIExportResult Result = Future.Get();

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_name"), Result.AssetName);
		Data->SetStringField(TEXT("asset_type"), Result.AssetType);
		Data->SetStringField(TEXT("raw_file"), Result.RawFilePath);
		Data->SetStringField(TEXT("simplified_file"), Result.SimplifiedFilePath);
		if (!Result.StrippedFilePath.IsEmpty())
		{
			Data->SetStringField(TEXT("stripped_file"), Result.StrippedFilePath);
		}
		return CreateSuccessResponse(Data);
	}
	else
	{
		return CreateErrorResponse(Result.ErrorMessage);
	}
}

FString FAIExportTCPServer::HandleListSupportedTypes()
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> Types;
	// Blueprints
	Types.Add(MakeShared<FJsonValueString>(TEXT("WidgetBlueprint")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("Blueprint")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("AnimBlueprint")));
	// Data
	Types.Add(MakeShared<FJsonValueString>(TEXT("DataAsset")));
	// Input
	Types.Add(MakeShared<FJsonValueString>(TEXT("InputAction")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("InputMappingContext")));
	// Audio Foundation
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundClass")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundSubmix")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundConcurrency")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundAttenuation")));
	// Audio Modulation
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundControlBus")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundControlBusMix")));
	Types.Add(MakeShared<FJsonValueString>(TEXT("SoundModulationPatch")));
	// Physics
	Types.Add(MakeShared<FJsonValueString>(TEXT("PhysicalMaterial")));

	Data->SetArrayField(TEXT("types"), Types);
	return CreateSuccessResponse(Data);
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
// Widget Builder Command Handlers

FString FAIExportTCPServer::HandleCreateWidgetBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString PackagePath;
	FString AssetName;
	FString ParentClassPath;

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
	{
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	}
	Params->TryGetStringField(TEXT("parent_class"), ParentClassPath);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, ParentClassPath, Promise, this]()
	{
		UClass* ParentClass = nullptr;
		if (!ParentClassPath.IsEmpty())
		{
			ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
			if (!ParentClass)
			{
				ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
			}
			if (!ParentClass)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not find parent class: %s"), *ParentClassPath)));
				return;
			}
		}

		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::CreateWidgetBlueprint(PackagePath, AssetName, ParentClass);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create Widget Blueprint")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), WBP->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Create widget blueprint timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetClass, WidgetName, ParentName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_class"), WidgetClass))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_class' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	Params->TryGetStringField(TEXT("parent_name"), ParentName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetClass, WidgetName, ParentName, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		UWidget* Widget = UAIWidgetBlueprintBuilder::AddWidget(WBP, WidgetClass, WidgetName, ParentName);
		if (!Widget)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add widget '%s' of class '%s'"), *WidgetName, *WidgetClass)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("widget_name"), Widget->GetName());
		Data->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Add widget timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::RemoveWidget(WBP, WidgetName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove widget: %s"), *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("removed"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Remove widget timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleMoveWidget(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, NewParentName;
	double NewIndexDouble = -1.0;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("new_parent_name"), NewParentName))
	{
		return CreateErrorResponse(TEXT("Missing 'new_parent_name' parameter"));
	}
	Params->TryGetNumberField(TEXT("index"), NewIndexDouble);
	int32 NewIndex = (int32)NewIndexDouble;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, NewParentName, NewIndex, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::MoveWidget(WBP, WidgetName, NewParentName, NewIndex);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to move widget '%s' to '%s'"), *WidgetName, *NewParentName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("new_parent"), NewParentName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Move widget timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetWidgetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, PropertyName, Value, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::SetWidgetProperty(WBP, WidgetName, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s' on widget '%s'"), *PropertyName, *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set widget property timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetSlotProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, PropertyName, Value, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::SetSlotProperty(WBP, WidgetName, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set slot property '%s' on widget '%s'"), *PropertyName, *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set slot property timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetCanvasSlotLayout(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	// All layout params default to 0
	double PosX = 0, PosY = 0, SizeX = 0, SizeY = 0;
	double AnchorMinX = 0, AnchorMinY = 0, AnchorMaxX = 0, AnchorMaxY = 0;
	double AlignmentX = 0, AlignmentY = 0;

	Params->TryGetNumberField(TEXT("position_x"), PosX);
	Params->TryGetNumberField(TEXT("position_y"), PosY);
	Params->TryGetNumberField(TEXT("size_x"), SizeX);
	Params->TryGetNumberField(TEXT("size_y"), SizeY);
	Params->TryGetNumberField(TEXT("anchor_min_x"), AnchorMinX);
	Params->TryGetNumberField(TEXT("anchor_min_y"), AnchorMinY);
	Params->TryGetNumberField(TEXT("anchor_max_x"), AnchorMaxX);
	Params->TryGetNumberField(TEXT("anchor_max_y"), AnchorMaxY);
	Params->TryGetNumberField(TEXT("alignment_x"), AlignmentX);
	Params->TryGetNumberField(TEXT("alignment_y"), AlignmentY);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [=, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::SetCanvasSlotLayout(
			WBP, WidgetName,
			(float)PosX, (float)PosY, (float)SizeX, (float)SizeY,
			(float)AnchorMinX, (float)AnchorMinY, (float)AnchorMaxX, (float)AnchorMaxY,
			(float)AlignmentX, (float)AlignmentY);

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set canvas slot layout on widget '%s'"), *WidgetName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("widget_name"), WidgetName);
		Data->SetStringField(TEXT("summary"), FString::Printf(TEXT("Pos(%.0f,%.0f) Size(%.0f,%.0f)"), PosX, PosY, SizeX, SizeY));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set canvas slot layout timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetWidgetProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath, WidgetName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
	{
		return CreateErrorResponse(TEXT("Missing 'widget_name' parameter"));
	}

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropertiesObj) || !PropertiesObj || !(*PropertiesObj).IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'properties' object parameter"));
	}

	// Convert JSON object to TMap
	TMap<FString, FString> Properties;
	for (const auto& Pair : (*PropertiesObj)->Values)
	{
		FString StringValue;
		if (Pair.Value->TryGetString(StringValue))
		{
			Properties.Add(Pair.Key, StringValue);
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, WidgetName, Properties, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		TArray<FString> Failed;
		int32 SetCount = UAIWidgetBlueprintBuilder::SetWidgetProperties(WBP, WidgetName, Properties, &Failed);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("set_count"), SetCount);

		if (Failed.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> FailedArray;
			for (const FString& F : Failed)
			{
				FailedArray.Add(MakeShared<FJsonValueString>(F));
			}
			Data->SetArrayField(TEXT("failed"), FailedArray);
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Set widget properties timed out"));
	}
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Blueprint Utility Command Handlers

FString FAIExportTCPServer::HandleReparentBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	FString NewParentClassPath;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("new_parent_class"), NewParentClassPath))
	{
		return CreateErrorResponse(TEXT("Missing 'new_parent_class' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NewParentClassPath, Promise, this]()
	{
		// Load the Widget Blueprint
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		// Resolve new parent class
		UClass* NewParentClass = FindObject<UClass>(nullptr, *NewParentClassPath);
		if (!NewParentClass)
		{
			NewParentClass = LoadObject<UClass>(nullptr, *NewParentClassPath);
		}
		if (!NewParentClass)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not find parent class: %s"), *NewParentClassPath)));
			return;
		}

		FString OldParentName = WBP->ParentClass ? WBP->ParentClass->GetName() : TEXT("None");

		// Perform reparenting
		bool bSuccess = UAIWidgetBlueprintBuilder::ReparentBlueprint(WBP, NewParentClass);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to reparent blueprint")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("old_parent"), OldParentName);
		Data->SetStringField(TEXT("new_parent"), NewParentClass->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Reparent blueprint timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleCompileAndSave(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (WBP)
		{
			TArray<FString> Warnings;
			bool bSuccess = UAIWidgetBlueprintBuilder::CompileAndSave(WBP, &Warnings);

			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("compiled"), bSuccess);
			Data->SetBoolField(TEXT("saved"), bSuccess);

			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> WarningArray;
				for (const FString& W : Warnings)
				{
					WarningArray.Add(MakeShared<FJsonValueString>(W));
				}
				Data->SetArrayField(TEXT("warnings"), WarningArray);
			}

			Promise->SetValue(CreateSuccessResponse(Data));
		}
		else
		{
			// Fallback: Data Asset — just save (no compile step)
			UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
			if (!Asset)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
				return;
			}

			bool bSaved = UAIDataAssetBuilder::SaveAsset(Asset);
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetBoolField(TEXT("compiled"), false);
			Data->SetBoolField(TEXT("saved"), bSaved);
			Promise->SetValue(bSaved ? CreateSuccessResponse(Data) :
				CreateErrorResponse(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath)));
		}
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Compile and save timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetWidgetTree(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UWidgetBlueprint* WBP = UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(AssetPath);
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> TreeJson = UAIWidgetBlueprintBuilder::GetWidgetTreeAsJson(WBP);
		if (!TreeJson.IsValid())
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Widget tree is empty")));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetObjectField(TEXT("root"), TreeJson);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Get widget tree timed out"));
	}
	return Future.Get();
}

FString FAIExportTCPServer::HandleListWidgetClasses()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		TArray<TPair<FString, bool>> Classes = UAIWidgetBlueprintBuilder::GetAvailableWidgetClasses();

		TArray<TSharedPtr<FJsonValue>> ClassArray;
		for (const auto& Pair : Classes)
		{
			TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
			ClassObj->SetStringField(TEXT("name"), Pair.Key);
			ClassObj->SetBoolField(TEXT("is_panel"), Pair.Value);
			ClassArray.Add(MakeShared<FJsonValueObject>(ClassObj));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("classes"), ClassArray);
		Data->SetNumberField(TEXT("count"), Classes.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("List widget classes timed out"));
	}
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Material Builder Handlers

FString FAIExportTCPServer::HandleCreateMaterial(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName;
	FString Domain = TEXT("Surface"), BlendMode = TEXT("Opaque"), ShadingModel = TEXT("DefaultLit");
	bool bTwoSided = false;

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));

	Params->TryGetStringField(TEXT("domain"), Domain);
	Params->TryGetStringField(TEXT("blend_mode"), BlendMode);
	Params->TryGetStringField(TEXT("shading_model"), ShadingModel);
	Params->TryGetBoolField(TEXT("two_sided"), bTwoSided);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, Domain, BlendMode, ShadingModel, bTwoSided, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::CreateMaterial(PackagePath, AssetName, Domain, BlendMode, ShadingModel, bTwoSided);
		if (!Material)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create material")));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Material->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create material timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyName, Value, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::SetMaterialProperty(Material, PropertyName, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set material property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddExpression(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ExprClass, NodeName;
	double PosX = 0, PosY = 0;

	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("expression_class"), ExprClass))
		return CreateErrorResponse(TEXT("Missing 'expression_class'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));

	Params->TryGetNumberField(TEXT("pos_x"), PosX);
	Params->TryGetNumberField(TEXT("pos_y"), PosY);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ExprClass, NodeName, PosX, PosY, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		UMaterialExpression* Expr = UAIMaterialBuilder::AddExpression(Material, ExprClass, NodeName, (int32)PosX, (int32)PosY);
		if (!Expr) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add expression '%s'"), *ExprClass))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
		Data->SetNumberField(TEXT("pos_x"), PosX);
		Data->SetNumberField(TEXT("pos_y"), PosY);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add expression timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetExpressionProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, PropertyName, Value, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::SetExpressionProperty(Material, NodeName, PropertyName, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set expression property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleConnectExpressions(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromOutput, ToNode, ToInput;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("from_node"), FromNode))
		return CreateErrorResponse(TEXT("Missing 'from_node'"));
	if (!Params->TryGetStringField(TEXT("from_output"), FromOutput))
		FromOutput = TEXT("");
	if (!Params->TryGetStringField(TEXT("to_node"), ToNode))
		return CreateErrorResponse(TEXT("Missing 'to_node'"));
	if (!Params->TryGetStringField(TEXT("to_input"), ToInput))
		ToInput = TEXT("");

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromOutput, ToNode, ToInput, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::ConnectExpressions(Material, FromNode, FromOutput, ToNode, ToInput);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect expressions timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleConnectToMaterialProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromOutput, MaterialProperty;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("from_node"), FromNode))
		return CreateErrorResponse(TEXT("Missing 'from_node'"));
	if (!Params->TryGetStringField(TEXT("from_output"), FromOutput))
		FromOutput = TEXT("");
	if (!Params->TryGetStringField(TEXT("material_property"), MaterialProperty))
		return CreateErrorResponse(TEXT("Missing 'material_property'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromOutput, MaterialProperty, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::ConnectToMaterialProperty(Material, FromNode, FromOutput, MaterialProperty);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect to material property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleDisconnectInput(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, InputName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));
	if (!Params->TryGetStringField(TEXT("input_name"), InputName))
		return CreateErrorResponse(TEXT("Missing 'input_name'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, InputName, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::DisconnectInput(Material, NodeName, InputName);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Disconnect input timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveExpression(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::RemoveExpression(Material, NodeName);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove expression timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleCompileMaterial(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		TArray<FString> Warnings;
		bool bCompiled = UAIMaterialBuilder::CompileMaterial(Material, &Warnings);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("compiled"), bCompiled);
		Data->SetBoolField(TEXT("saved"), true);

		if (Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarnArray;
			for (const FString& W : Warnings)
			{
				WarnArray.Add(MakeShared<FJsonValueString>(W));
			}
			Data->SetArrayField(TEXT("warnings"), WarnArray);
		}
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Compile material timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterial* Material = UAIMaterialBuilder::LoadMaterial(AssetPath);
		if (!Material) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Material not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIMaterialBuilder::GetMaterialGraphAsJson(Material);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get material graph timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleListExpressionClasses()
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, this]()
	{
		TArray<FString> Classes = UAIMaterialBuilder::GetAvailableExpressionClasses();

		TArray<TSharedPtr<FJsonValue>> ClassArray;
		for (const FString& ClassName : Classes)
		{
			ClassArray.Add(MakeShared<FJsonValueString>(ClassName));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("classes"), ClassArray);
		Data->SetNumberField(TEXT("count"), Classes.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List expression classes timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleCreateMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, ParentMaterialPath;
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path'"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name'"));
	if (!Params->TryGetStringField(TEXT("parent_material_path"), ParentMaterialPath))
		return CreateErrorResponse(TEXT("Missing 'parent_material_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, ParentMaterialPath, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::CreateMaterialInstance(PackagePath, AssetName, ParentMaterialPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to create material instance"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), MIC->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create material instance timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetInstanceParameter(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ParamName, ParamType, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
		return CreateErrorResponse(TEXT("Missing 'param_name'"));
	if (!Params->TryGetStringField(TEXT("param_type"), ParamType))
		return CreateErrorResponse(TEXT("Missing 'param_type'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ParamName, ParamType, Value, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::LoadMaterialInstance(AssetPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("MIC not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIMaterialBuilder::SetInstanceParameter(MIC, ParamName, ParamType, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set instance parameter timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSaveMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::LoadMaterialInstance(AssetPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("MIC not found: %s"), *AssetPath))); return; }

		bool bSaved = UAIMaterialBuilder::SaveMaterialInstance(MIC);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), bSaved);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Save material instance timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetMaterialInstanceInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UMaterialInstanceConstant* MIC = UAIMaterialBuilder::LoadMaterialInstance(AssetPath);
		if (!MIC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("MIC not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIMaterialBuilder::GetMaterialInstanceInfoAsJson(MIC);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get material instance info timed out"));
	return Future.Get();
}

// =============================================================================
// DATA ASSET COMMANDS
// =============================================================================

FString FAIExportTCPServer::HandleSaveDataAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSaved = UAIDataAssetBuilder::SaveAsset(Asset);
		if (!bSaved)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Save data asset timed out"));
	return Future.Get();
}

// =============================================================================
// Asset Import Commands
// =============================================================================

FString FAIExportTCPServer::HandleImportTexture(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString SourcePath, PackagePath, AssetName;
	FString Compression = TEXT("UserInterface2D");
	FString MipGen = TEXT("NoMipmaps");
	FString LODGroup = TEXT("UI");
	bool bSRGB = true;

	if (!Params->TryGetStringField(TEXT("source_path"), SourcePath))
		return CreateErrorResponse(TEXT("Missing 'source_path' parameter"));
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));

	Params->TryGetStringField(TEXT("asset_name"), AssetName);
	Params->TryGetStringField(TEXT("compression"), Compression);
	Params->TryGetStringField(TEXT("mip_gen"), MipGen);
	Params->TryGetStringField(TEXT("lod_group"), LODGroup);
	Params->TryGetBoolField(TEXT("srgb"), bSRGB);

	// Normalize path separators
	SourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Verify source file exists
	if (!FPaths::FileExists(SourcePath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Source file not found: %s"), *SourcePath));
	}

	// Derive asset name from filename if not provided
	if (AssetName.IsEmpty())
	{
		AssetName = FPaths::GetBaseFilename(SourcePath);
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [SourcePath, PackagePath, AssetName, Compression, MipGen, LODGroup, bSRGB, Promise, this]()
	{
		// Build full package name
		FString FullPackagePath = PackagePath / AssetName;

		// Create package
		UPackage* Package = CreatePackage(*FullPackagePath);
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *FullPackagePath)));
			return;
		}
		Package->FullyLoad();

		// Read file data
		TArray<uint8> FileData;
		if (!FFileHelper::LoadFileToArray(FileData, *SourcePath))
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to read file: %s"), *SourcePath)));
			return;
		}

		// Create texture factory
		UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
		TextureFactory->AddToRoot(); // Prevent GC during import
		TextureFactory->SuppressImportOverwriteDialog();

		// Import
		const uint8* DataPtr = FileData.GetData();
		UObject* ImportedObject = TextureFactory->FactoryCreateBinary(
			UTexture2D::StaticClass(),
			Package,
			*AssetName,
			RF_Public | RF_Standalone,
			nullptr,
			*FPaths::GetExtension(SourcePath),
			DataPtr,
			DataPtr + FileData.Num(),
			GWarn
		);

		TextureFactory->RemoveFromRoot();

		UTexture2D* Texture = Cast<UTexture2D>(ImportedObject);
		if (!Texture)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to import texture")));
			return;
		}

		// Apply compression settings
		if (Compression == TEXT("Default"))
			Texture->CompressionSettings = TC_Default;
		else if (Compression == TEXT("NormalMap") || Compression == TEXT("Normalmap"))
			Texture->CompressionSettings = TC_Normalmap;
		else if (Compression == TEXT("Masks"))
			Texture->CompressionSettings = TC_Masks;
		else if (Compression == TEXT("Grayscale") || Compression == TEXT("Displacementmap"))
			Texture->CompressionSettings = TC_Displacementmap;
		else if (Compression == TEXT("HDR"))
			Texture->CompressionSettings = TC_HDR;
		else if (Compression == TEXT("UserInterface2D") || Compression == TEXT("UI"))
			Texture->CompressionSettings = TC_EditorIcon;
		else if (Compression == TEXT("Alpha"))
			Texture->CompressionSettings = TC_Alpha;
		else
			Texture->CompressionSettings = TC_EditorIcon; // Default to UI

		// Apply sRGB
		Texture->SRGB = bSRGB;

		// Apply MipGen settings
		if (MipGen == TEXT("NoMipmaps"))
			Texture->MipGenSettings = TMGS_NoMipmaps;
		else if (MipGen == TEXT("FromTextureGroup"))
			Texture->MipGenSettings = TMGS_FromTextureGroup;
		else if (MipGen == TEXT("Sharpen0"))
			Texture->MipGenSettings = TMGS_Sharpen0;
		else if (MipGen == TEXT("Sharpen"))
			Texture->MipGenSettings = TMGS_Sharpen0;
		else if (MipGen == TEXT("Blur"))
			Texture->MipGenSettings = TMGS_Blur1;
		else
			Texture->MipGenSettings = TMGS_NoMipmaps;

		// Apply LOD Group
		if (LODGroup == TEXT("World"))
			Texture->LODGroup = TEXTUREGROUP_World;
		else if (LODGroup == TEXT("WorldNormalMap"))
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
		else if (LODGroup == TEXT("WorldSpecular"))
			Texture->LODGroup = TEXTUREGROUP_WorldSpecular;
		else if (LODGroup == TEXT("Character"))
			Texture->LODGroup = TEXTUREGROUP_Character;
		else if (LODGroup == TEXT("CharacterNormalMap"))
			Texture->LODGroup = TEXTUREGROUP_CharacterNormalMap;
		else if (LODGroup == TEXT("Effects"))
			Texture->LODGroup = TEXTUREGROUP_Effects;
		else if (LODGroup == TEXT("UI"))
			Texture->LODGroup = TEXTUREGROUP_UI;
		else if (LODGroup == TEXT("Lightmap"))
			Texture->LODGroup = TEXTUREGROUP_Lightmap;
		else if (LODGroup == TEXT("Shadowmap"))
			Texture->LODGroup = TEXTUREGROUP_Shadowmap;
		else
			Texture->LODGroup = TEXTUREGROUP_UI;

		// Apply changes and save
		Texture->PostEditChange();
		Texture->UpdateResource();
		Package->MarkPackageDirty();

		// Save the package
		FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, Texture, *PackageFilename, SaveArgs);

		// Notify asset registry
		FAssetRegistryModule::AssetCreated(Texture);

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Texture->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetNumberField(TEXT("width"), Texture->GetSizeX());
		Data->SetNumberField(TEXT("height"), Texture->GetSizeY());
		Data->SetStringField(TEXT("format"), UEnum::GetValueAsString(Texture->GetPixelFormat()));
		Data->SetBoolField(TEXT("saved"), bSaved);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Import texture timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleImportFont(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, FontName;
	FString Hinting = TEXT("Auto");

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("font_name"), FontName))
		return CreateErrorResponse(TEXT("Missing 'font_name' parameter"));

	Params->TryGetStringField(TEXT("hinting"), Hinting);

	// Parse faces array
	const TArray<TSharedPtr<FJsonValue>>* FacesArray = nullptr;
	if (!Params->TryGetArrayField(TEXT("faces"), FacesArray) || !FacesArray || FacesArray->Num() == 0)
	{
		return CreateErrorResponse(TEXT("Missing or empty 'faces' array. Each entry needs 'source_path' and 'name' (e.g. 'Regular', 'Bold')."));
	}

	// Validate all face entries before processing
	struct FFaceEntry
	{
		FString SourcePath;
		FString Name;
	};
	TArray<FFaceEntry> Faces;

	for (const auto& FaceValue : *FacesArray)
	{
		const TSharedPtr<FJsonObject>* FaceObj = nullptr;
		if (!FaceValue->TryGetObject(FaceObj) || !FaceObj || !(*FaceObj).IsValid())
		{
			return CreateErrorResponse(TEXT("Each face entry must be a JSON object with 'source_path' and 'name'"));
		}

		FFaceEntry Entry;
		if (!(*FaceObj)->TryGetStringField(TEXT("source_path"), Entry.SourcePath))
			return CreateErrorResponse(TEXT("Face entry missing 'source_path'"));
		if (!(*FaceObj)->TryGetStringField(TEXT("name"), Entry.Name))
			return CreateErrorResponse(TEXT("Face entry missing 'name' (e.g. 'Regular', 'Bold', 'Medium')"));

		Entry.SourcePath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (!FPaths::FileExists(Entry.SourcePath))
			return CreateErrorResponse(FString::Printf(TEXT("Font file not found: %s"), *Entry.SourcePath));

		Faces.Add(MoveTemp(Entry));
	}

	// Resolve hinting enum
	EFontHinting HintingEnum = EFontHinting::Auto;
	if (Hinting == TEXT("None"))
		HintingEnum = EFontHinting::None;
	else if (Hinting == TEXT("Auto"))
		HintingEnum = EFontHinting::Auto;
	else if (Hinting == TEXT("AutoLight"))
		HintingEnum = EFontHinting::AutoLight;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, FontName, Faces, HintingEnum, Promise, this]()
	{
		TArray<TSharedPtr<FJsonObject>> FaceResults;

		// Step 1: Create UFontFace for each TTF/OTF
		TArray<UFontFace*> FontFaceAssets;
		for (const auto& Face : Faces)
		{
			FString FaceName = FontName + TEXT("-") + Face.Name;
			FString FacePackagePath = PackagePath / FaceName;

			UPackage* FacePackage = CreatePackage(*FacePackagePath);
			if (!FacePackage)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create package for font face: %s"), *FaceName)));
				return;
			}
			FacePackage->FullyLoad();

			// Read font file data
			TArray<uint8> FontData;
			if (!FFileHelper::LoadFileToArray(FontData, *Face.SourcePath))
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to read font file: %s"), *Face.SourcePath)));
				return;
			}

			// Create UFontFace
			UFontFace* FontFace = NewObject<UFontFace>(FacePackage, *FaceName, RF_Public | RF_Standalone);
			FontFace->SourceFilename = Face.SourcePath;
			FontFace->Hinting = HintingEnum;
			FontFace->LoadingPolicy = EFontLoadingPolicy::Inline;

			// Load font data into the asset
			FontFace->FontFaceData = MakeShared<FFontFaceData, ESPMode::ThreadSafe>(MoveTemp(FontData));

			FontFace->PostEditChange();
			FacePackage->MarkPackageDirty();

			// Save FontFace
			FString FaceFilename = FPackageName::LongPackageNameToFilename(FacePackagePath, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(FacePackage, FontFace, *FaceFilename, SaveArgs);

			FAssetRegistryModule::AssetCreated(FontFace);
			FontFaceAssets.Add(FontFace);

			// Track result
			TSharedPtr<FJsonObject> FaceResult = MakeShared<FJsonObject>();
			FaceResult->SetStringField(TEXT("name"), Face.Name);
			FaceResult->SetStringField(TEXT("asset_path"), FontFace->GetPathName());
			FaceResults.Add(FaceResult);
		}

		// Step 2: Create Composite UFont
		FString CompositePath = PackagePath / FontName;
		UPackage* FontPackage = CreatePackage(*CompositePath);
		if (!FontPackage)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create composite font package")));
			return;
		}
		FontPackage->FullyLoad();

		UFont* CompositeFont = NewObject<UFont>(FontPackage, *FontName, RF_Public | RF_Standalone);
		CompositeFont->FontCacheType = EFontCacheType::Runtime;

		// Build typeface entries
		FTypeface& DefaultTypeface = CompositeFont->GetMutableInternalCompositeFont().DefaultTypeface;
		DefaultTypeface.Fonts.Empty();

		for (int32 i = 0; i < FontFaceAssets.Num(); ++i)
		{
			FTypefaceEntry& Entry = DefaultTypeface.Fonts.AddDefaulted_GetRef();
			Entry.Name = *Faces[i].Name;
			Entry.Font = FFontData(FontFaceAssets[i]);
		}

		CompositeFont->PostEditChange();
		FontPackage->MarkPackageDirty();

		// Save composite font
		FString FontFilename = FPackageName::LongPackageNameToFilename(CompositePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(FontPackage, CompositeFont, *FontFilename, SaveArgs);

		FAssetRegistryModule::AssetCreated(CompositeFont);

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("font_asset_path"), CompositeFont->GetPathName());
		Data->SetStringField(TEXT("font_name"), FontName);
		Data->SetNumberField(TEXT("face_count"), FontFaceAssets.Num());
		Data->SetBoolField(TEXT("saved"), bSaved);

		TArray<TSharedPtr<FJsonValue>> FaceResultValues;
		for (const auto& FR : FaceResults)
		{
			FaceResultValues.Add(MakeShared<FJsonValueObject>(FR));
		}
		Data->SetArrayField(TEXT("faces"), FaceResultValues);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Import font timed out"));
	return Future.Get();
}

// =============================================================================
// CDO PROPERTY HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleSetCDOProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyName, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyName, Value, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		if (WBP)
		{
			bool bSuccess = UAIWidgetBlueprintBuilder::SetCDOProperty(WBP, PropertyName, Value);
			if (!bSuccess)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set CDO property '%s'"), *PropertyName)));
				return;
			}
		}
		else
		{
			// Non-WBP path: if Asset is a Blueprint (e.g. CommonButtonStyle subclass),
			// redirect writes to its generated class CDO so we can set parent-class
			// properties like NormalBase / NormalHovered (not present on the BP itself).
			UObject* TargetForSet = Asset;
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				UClass* GenClass = BP->GeneratedClass;
				if (!GenClass)
				{
					FKismetEditorUtilities::CompileBlueprint(BP);
					GenClass = BP->GeneratedClass;
				}
				if (!GenClass)
				{
					Promise->SetValue(CreateErrorResponse(TEXT("Blueprint has no GeneratedClass")));
					return;
				}
				TargetForSet = GenClass->GetDefaultObject();
				if (!TargetForSet)
				{
					Promise->SetValue(CreateErrorResponse(TEXT("Blueprint generated class has no CDO")));
					return;
				}
			}

			bool bSuccess = UAIDataAssetBuilder::SetProperty(TargetForSet, PropertyName, Value);
			if (!bSuccess)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set property '%s'"), *PropertyName)));
				return;
			}
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Data->SetStringField(TEXT("value"), Value);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set CDO property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetCDOProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		if (WBP)
		{
			TSharedPtr<FJsonObject> PropsJson = UAIWidgetBlueprintBuilder::GetCDOPropertiesAsJson(WBP);
			Promise->SetValue(CreateSuccessResponse(PropsJson));
		}
		else
		{
			// Non-WBP path: if Asset is a Blueprint, read properties from its CDO so
			// inherited fields (e.g. CommonButtonStyle::NormalBase) are visible.
			UObject* TargetForRead = Asset;
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				UClass* GenClass = BP->GeneratedClass;
				if (!GenClass)
				{
					FKismetEditorUtilities::CompileBlueprint(BP);
					GenClass = BP->GeneratedClass;
				}
				if (GenClass && GenClass->GetDefaultObject())
				{
					TargetForRead = GenClass->GetDefaultObject();
				}
			}
			TSharedPtr<FJsonObject> PropsJson = UAIDataAssetBuilder::GetPropertiesAsJson(TargetForRead);
			Promise->SetValue(CreateSuccessResponse(PropsJson));
		}
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get CDO properties timed out"));
	return Future.Get();
}

// =============================================================================
// CDO ARRAY PROPERTY HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleAddCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName, ElementValuesJson, ClassName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	Params->TryGetStringField(TEXT("element_values"), ElementValuesJson);
	Params->TryGetStringField(TEXT("class_name"), ClassName);

	// Parse element_values JSON string into a map
	TMap<FString, FString> ElementValues;
	if (!ElementValuesJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ElementValuesJson);
		TSharedPtr<FJsonObject> ValuesObj;
		if (FJsonSerializer::Deserialize(Reader, ValuesObj) && ValuesObj.IsValid())
		{
			for (const auto& Pair : ValuesObj->Values)
			{
				FString Val;
				if (Pair.Value->TryGetString(Val))
				{
					ElementValues.Add(Pair.Key, Val);
				}
			}
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementValues, ClassName, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		int32 NewIndex = -1;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass)
			{
				FKismetEditorUtilities::CompileBlueprint(WBP);
				GenClass = WBP->GeneratedClass;
			}
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }

			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			NewIndex = UAIWidgetBlueprintBuilder::AddArrayElement(CDO, ArrayName, ElementValues, ClassName);
		}
		else if (BP)
		{
			// Non-WBP Blueprint (BPTYPE_Const data assets, etc.)
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass)
			{
				FKismetEditorUtilities::CompileBlueprint(BP);
				GenClass = BP->GeneratedClass;
			}
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }

			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			NewIndex = UAIWidgetBlueprintBuilder::AddArrayElement(CDO, ArrayName, ElementValues, ClassName);
		}
		else
		{
			NewIndex = UAIDataAssetBuilder::AddArrayElement(Asset, ArrayName, ElementValues, ClassName);
		}

		if (NewIndex < 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add element to '%s'"), *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("index"), NewIndex);
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add CDO array element timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetCDOArrayElementProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName, PropertyName, Value;
	double Index = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return CreateErrorResponse(TEXT("Missing 'index' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));

	int32 ElementIndex = (int32)Index;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementIndex, PropertyName, Value, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = false;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::SetArrayElementProperty(CDO, ArrayName, ElementIndex, PropertyName, Value);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::SetArrayElementProperty(CDO, ArrayName, ElementIndex, PropertyName, Value);
		}
		else
		{
			bSuccess = UAIDataAssetBuilder::SetArrayElementProperty(Asset, ArrayName, ElementIndex, PropertyName, Value);
		}

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set '%s' on element %d of '%s'"),
				*PropertyName, ElementIndex, *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("index"), ElementIndex);
		Data->SetStringField(TEXT("property_name"), PropertyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set CDO array element property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveCDOArrayElement(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName;
	double Index = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return CreateErrorResponse(TEXT("Missing 'index' parameter"));

	int32 ElementIndex = (int32)Index;

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementIndex, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = false;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::RemoveArrayElement(CDO, ArrayName, ElementIndex);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			bSuccess = UAIWidgetBlueprintBuilder::RemoveArrayElement(CDO, ArrayName, ElementIndex);
		}
		else
		{
			bSuccess = UAIDataAssetBuilder::RemoveArrayElement(Asset, ArrayName, ElementIndex);
		}

		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove element %d from '%s'"),
				ElementIndex, *ArrayName)));
			return;
		}

		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("removed_index"), ElementIndex);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove CDO array element timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetCDOArrayLength(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, ArrayName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load asset: %s"), *AssetPath)));
			return;
		}

		int32 Length = -1;
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (WBP)
		{
			UClass* GenClass = WBP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(WBP); GenClass = WBP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

			Length = UAIWidgetBlueprintBuilder::GetArrayLength(CDO, ArrayName);
		}
		else if (BP)
		{
			UClass* GenClass = BP->GeneratedClass;
			if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
			if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass for Blueprint"))); return; }
			UObject* CDO = GenClass->GetDefaultObject();
			if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO for Blueprint"))); return; }

			Length = UAIWidgetBlueprintBuilder::GetArrayLength(CDO, ArrayName);
		}
		else
		{
			Length = UAIDataAssetBuilder::GetArrayLength(Asset, ArrayName);
		}

		if (Length < 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Array '%s' not found"), *ArrayName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("array_name"), ArrayName);
		Data->SetNumberField(TEXT("length"), Length);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get CDO array length timed out"));
	return Future.Get();
}

// =============================================================================
// BLUEPRINT GRAPH HANDLERS
// =============================================================================

// Helper macro for graph node creation handlers (they all follow the same pattern)
#define GRAPH_NODE_HANDLER_BODY(HandlerName, BuilderCall)                                      \
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));        \
	FString AssetPath, NodeName;                                                               \
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))                             \
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));                    \
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))                               \
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));                     \
	FString GraphName;                                                                         \
	Params->TryGetStringField(TEXT("graph_name"), GraphName);                                  \
	double PosX = 0, PosY = 0;                                                                \
	Params->TryGetNumberField(TEXT("pos_x"), PosX);                                            \
	Params->TryGetNumberField(TEXT("pos_y"), PosY);

FString FAIExportTCPServer::HandleAddEventNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddEventNode, AddEventNode)

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddEventNode(BP, EventName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Data->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add event node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddCustomEvent(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddCustomEvent, AddCustomEvent)

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddCustomEvent(BP, EventName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add custom event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add custom event timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddFunctionCallNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddFunctionCallNode, AddFunctionCallNode)

	FString FunctionName, TargetClass;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	Params->TryGetStringField(TEXT("target_class"), TargetClass);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, TargetClass, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddFunctionCallNode(BP, FunctionName, NodeName, TargetClass, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add function call '%s'"), *FunctionName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add function call timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddVariableGetNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddVariableGetNode, AddVariableGetNode)

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
		return CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddVariableGetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Get '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable get node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddVariableSetNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddVariableSetNode, AddVariableSetNode)

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
		return CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddVariableSetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Set '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable set node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddMakeStructNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddMakeStructNode, AddMakeStructNode)

	FString StructName;
	if (!Params->TryGetStringField(TEXT("struct_name"), StructName))
		return CreateErrorResponse(TEXT("Missing 'struct_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, StructName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddMakeStructNode(BP, StructName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add MakeStruct '%s'"), *StructName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("struct_name"), StructName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add make struct node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddBranchNode(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddBranchNode, AddBranchNode)

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddBranchNode(BP, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add branch node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add branch node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleAddCallParentFunction(TSharedPtr<FJsonObject> Params)
{
	GRAPH_NODE_HANDLER_BODY(HandleAddCallParentFunction, AddCallParentFunctionNode)

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, GraphName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddCallParentFunctionNode(BP, FunctionName, NodeName, (int32)PosX, (int32)PosY, GraphName);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add call parent function node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Data->SetStringField(TEXT("node_class"), TEXT("K2Node_CallParentFunction"));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add call parent function timed out"));
	return Future.Get();
}

namespace
{
static TArray<FAIBlueprintGraphPinSpec> ParseGraphPinSpecs(
	const TSharedPtr<FJsonObject>& Params,
	const FString& FieldName)
{
	TArray<FAIBlueprintGraphPinSpec> Specs;

	const TArray<TSharedPtr<FJsonValue>>* PinArray = nullptr;
	if (!Params.IsValid() || !Params->TryGetArrayField(FieldName, PinArray) || !PinArray)
	{
		return Specs;
	}

	for (const TSharedPtr<FJsonValue>& Value : *PinArray)
	{
		const TSharedPtr<FJsonObject>* PinObj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(PinObj) || !PinObj || !PinObj->IsValid())
		{
			continue;
		}

		FAIBlueprintGraphPinSpec Spec;
		(*PinObj)->TryGetStringField(TEXT("name"), Spec.Name);
		(*PinObj)->TryGetStringField(TEXT("type"), Spec.Type);
		(*PinObj)->TryGetStringField(TEXT("default_value"), Spec.DefaultValue);
		if (Spec.DefaultValue.IsEmpty())
		{
			(*PinObj)->TryGetStringField(TEXT("default"), Spec.DefaultValue);
		}

		if (!Spec.Name.IsEmpty())
		{
			Specs.Add(MoveTemp(Spec));
		}
	}

	return Specs;
}
}

FString FAIExportTCPServer::HandleEnsureFunctionGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FunctionName, EntryNodeName, ResultNodeName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	Params->TryGetStringField(TEXT("entry_node_name"), EntryNodeName);
	Params->TryGetStringField(TEXT("result_node_name"), ResultNodeName);

	TArray<FAIBlueprintGraphPinSpec> Inputs = ParseGraphPinSpecs(Params, TEXT("inputs"));
	TArray<FAIBlueprintGraphPinSpec> Outputs = ParseGraphPinSpecs(Params, TEXT("outputs"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, Inputs, Outputs, EntryNodeName, ResultNodeName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath)));
			return;
		}

		UEdGraph* Graph = UAIBlueprintGraphBuilder::EnsureFunctionGraph(
			BP,
			FunctionName,
			Inputs,
			Outputs,
			EntryNodeName,
			ResultNodeName);
		if (!Graph)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to ensure function graph '%s'"), *FunctionName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("graph_name"), Graph->GetName());
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetNumberField(TEXT("input_count"), Inputs.Num());
		Data->SetNumberField(TEXT("output_count"), Outputs.Num());
		Data->SetStringField(TEXT("entry_node_name"), EntryNodeName.IsEmpty() ? FString::Printf(TEXT("%s_Entry"), *FunctionName) : EntryNodeName);
		if (Outputs.Num() > 0)
		{
			Data->SetStringField(TEXT("result_node_name"), ResultNodeName.IsEmpty() ? FString::Printf(TEXT("%s_Result"), *FunctionName) : ResultNodeName);
		}
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Ensure function graph timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleConnectPins(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromPin, ToNode, ToPin, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("from_node"), FromNode))
		return CreateErrorResponse(TEXT("Missing 'from_node' parameter"));
	if (!Params->TryGetStringField(TEXT("from_pin"), FromPin))
		return CreateErrorResponse(TEXT("Missing 'from_pin' parameter"));
	if (!Params->TryGetStringField(TEXT("to_node"), ToNode))
		return CreateErrorResponse(TEXT("Missing 'to_node' parameter"));
	if (!Params->TryGetStringField(TEXT("to_pin"), ToPin))
		return CreateErrorResponse(TEXT("Missing 'to_pin' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromPin, ToNode, ToPin, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::ConnectPins(BP, FromNode, FromPin, ToNode, ToPin, GraphName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to connect %s.%s -> %s.%s"),
				*FromNode, *FromPin, *ToNode, *ToPin)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("from_node"), FromNode);
		Data->SetStringField(TEXT("from_pin"), FromPin);
		Data->SetStringField(TEXT("to_node"), ToNode);
		Data->SetStringField(TEXT("to_pin"), ToPin);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect pins timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetPinDefault(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, PinName, DefaultValue, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));
	if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
		return CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
		return CreateErrorResponse(TEXT("Missing 'default_value' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, PinName, DefaultValue, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::SetPinDefaultValue(BP, NodeName, PinName, DefaultValue, GraphName);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set pin default %s.%s"),
				*NodeName, *PinName)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("pin_name"), PinName);
		Data->SetStringField(TEXT("default_value"), DefaultValue);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set pin default timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::RemoveNode(BP, NodeName, GraphName);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Node '%s' not found"), *NodeName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("removed"), NodeName);
		Data->SetStringField(TEXT("graph_name"), GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove graph node timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetGraph(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, GraphName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	Params->TryGetStringField(TEXT("graph_name"), GraphName);
	if (GraphName.IsEmpty()) GraphName = TEXT("EventGraph");

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, GraphName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> GraphJson = UAIBlueprintGraphBuilder::GetGraphAsJson(BP, GraphName);
		Promise->SetValue(CreateSuccessResponse(GraphJson));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get graph timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleListGraphs(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TArray<FString> Graphs = UAIBlueprintGraphBuilder::ListGraphs(BP);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> GraphArray;
		for (const FString& Name : Graphs)
		{
			GraphArray.Add(MakeShared<FJsonValueString>(Name));
		}
		Data->SetArrayField(TEXT("graphs"), GraphArray);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List graphs timed out"));
	return Future.Get();
}

// =============================================================================
// BLUEPRINT VARIABLE HANDLERS
// =============================================================================

FString FAIExportTCPServer::HandleAddVariable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName, VarType, Category;
	bool bInstanceEditable = false;
	bool bBlueprintReadOnly = false;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));
	if (!Params->TryGetStringField(TEXT("var_type"), VarType))
		return CreateErrorResponse(TEXT("Missing 'var_type' parameter"));
	Params->TryGetBoolField(TEXT("instance_editable"), bInstanceEditable);
	Params->TryGetBoolField(TEXT("blueprint_read_only"), bBlueprintReadOnly);
	Params->TryGetStringField(TEXT("category"), Category);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, VarType, bInstanceEditable, bBlueprintReadOnly, Category, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::AddVariable(BP, VarName, VarType, bInstanceEditable, bBlueprintReadOnly, Category);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add variable '%s'"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("var_name"), VarName);
		Data->SetStringField(TEXT("var_type"), VarType);
		Data->SetBoolField(TEXT("blueprint_read_only"), bBlueprintReadOnly);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add variable timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetVariableDefault(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName, DefaultValue;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
		return CreateErrorResponse(TEXT("Missing 'default_value' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, DefaultValue, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::SetVariableDefault(BP, VarName, DefaultValue);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set default for '%s'"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("var_name"), VarName);
		Data->SetStringField(TEXT("default_value"), DefaultValue);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set variable default timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveVariable(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, VarName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::RemoveVariable(BP, VarName);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Variable '%s' not found"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("removed"), VarName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove variable timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetVariables(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> VarsJson = UAIBlueprintGraphBuilder::GetVariablesAsJson(BP);
		Promise->SetValue(CreateSuccessResponse(VarsJson));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get variables timed out"));
	return Future.Get();
}

// =============================================================================
// Generic Asset Factory Command Handlers
// =============================================================================

FString FAIExportTCPServer::HandleCreateAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, AssetType;
	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_type"), AssetType))
		return CreateErrorResponse(TEXT("Missing 'asset_type' parameter"));

	// Parse optional initial properties
	TMap<FString, FString> InitialProperties;
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FString Val;
			if (Pair.Value->TryGetString(Val))
			{
				InitialProperties.Add(Pair.Key, Val);
			}
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, AssetType, InitialProperties, Promise, this]()
	{
		UObject* Asset = UAIAssetFactory::CreateAsset(PackagePath, AssetName, AssetType, InitialProperties);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create %s"), *AssetType)));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("asset_type"), AssetType);
		Data->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create asset timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetAssetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, PropertyPath, Value;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath))
		return CreateErrorResponse(TEXT("Missing 'property_path'"));
	if (!Params->TryGetStringField(TEXT("value"), Value))
		return CreateErrorResponse(TEXT("Missing 'value'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, PropertyPath, Value, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		bool bSuccess = UAIDataAssetBuilder::SetProperty(Asset, PropertyPath, Value);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), bSuccess);
		Data->SetStringField(TEXT("property_path"), PropertyPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set asset property timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetAssetProperties(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Props = UAIDataAssetBuilder::GetPropertiesAsJson(Asset);
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		Data->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
		Data->SetObjectField(TEXT("properties"), Props);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get asset properties timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSaveAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath))); return; }

		bool bSaved = UAIDataAssetBuilder::SaveAsset(Asset);
		if (!bSaved) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to save: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("saved"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Save asset timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRenameAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	// Either or both may be provided. Empty = keep current value.
	FString NewPackagePath, NewAssetName;
	Params->TryGetStringField(TEXT("new_package_path"), NewPackagePath);
	Params->TryGetStringField(TEXT("new_asset_name"), NewAssetName);

	if (NewPackagePath.IsEmpty() && NewAssetName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("At least one of 'new_package_path' or 'new_asset_name' must be provided"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NewPackagePath, NewAssetName, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		// For Blueprint assets, the FAssetRenameData target should be the BP itself, not its generated class.
		// LoadAssetObject typically returns the BP UObject, which is what we want.

		const UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Asset has no outer package")));
			return;
		}

		const FString CurrentPackageName = Package->GetName();
		const FString CurrentPackagePath = FPackageName::GetLongPackagePath(CurrentPackageName);
		const FString CurrentAssetName = Asset->GetName();

		const FString FinalPackagePath = NewPackagePath.IsEmpty() ? CurrentPackagePath : NewPackagePath;
		const FString FinalAssetName = NewAssetName.IsEmpty() ? CurrentAssetName : NewAssetName;

		if (FinalPackagePath == CurrentPackagePath && FinalAssetName == CurrentAssetName)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("New path equals current path; nothing to rename")));
			return;
		}

		TArray<FAssetRenameData> AssetsToRename;
		AssetsToRename.Add(FAssetRenameData(Asset, FinalPackagePath, FinalAssetName));

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		IAssetTools& AssetTools = AssetToolsModule.Get();
		bool RenameResult = AssetTools.RenameAssets(AssetsToRename);

		if (!RenameResult)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(
				TEXT("RenameAssets failed (result=%d) for %s -> %s/%s"),
				RenameResult, *AssetPath, *FinalPackagePath, *FinalAssetName)));
			return;
		}

		const FString NewAssetPath = FinalPackagePath / FinalAssetName;

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("renamed"), true);
		Data->SetStringField(TEXT("old_path"), AssetPath);
		Data->SetStringField(TEXT("new_path"), NewAssetPath);
		Data->SetStringField(TEXT("new_package_path"), FinalPackagePath);
		Data->SetStringField(TEXT("new_asset_name"), FinalAssetName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Rename asset timed out"));
	return Future.Get();
}

namespace
{
	// Accepts "/Game/Path/Asset" OR "/Game/Path/Asset.Asset" — strips object suffix.
	static FName NormalizePackageName(const FString& InAssetPath)
	{
		FString PackageName = InAssetPath;
		int32 DotIndex = INDEX_NONE;
		if (PackageName.FindChar(TEXT('.'), DotIndex))
		{
			PackageName = PackageName.Left(DotIndex);
		}
		return FName(*PackageName);
	}
}

FString FAIExportTCPServer::HandleGetReferencers(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			// Block briefly so reference results are not falsely empty.
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FName> Referencers;
		AR.GetReferencers(PackageName, Referencers);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Referencers.Num());
		for (const FName& Ref : Referencers)
		{
			Array.Add(MakeShared<FJsonValueString>(Ref.ToString()));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("referencers"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get referencers timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetDependencies(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		const FName PackageName = NormalizePackageName(AssetPath);
		TArray<FName> Dependencies;
		AR.GetDependencies(PackageName, Dependencies);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Dependencies.Num());
		for (const FName& Dep : Dependencies)
		{
			Array.Add(MakeShared<FJsonValueString>(Dep.ToString()));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName.ToString());
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("dependencies"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get dependencies timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleDeleteAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	bool bForce = false;
	Params->TryGetBoolField(TEXT("force"), bForce);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, bForce, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		const FString PackageName = Asset->GetOutermost()->GetName();

		int32 NumDeleted = 0;
		if (bForce)
		{
			TArray<UObject*> Objects;
			Objects.Add(Asset);
			NumDeleted = ObjectTools::ForceDeleteObjects(Objects, /*bShowConfirmation=*/false);
		}
		else
		{
			TArray<FAssetData> AssetsToDelete;
			AssetsToDelete.Add(FAssetData(Asset));
			NumDeleted = ObjectTools::DeleteAssets(AssetsToDelete, /*bShowConfirmation=*/false);
		}

		if (NumDeleted == 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(
				TEXT("Delete returned 0 for %s (check referencers with get_referencers, or pass force=true to bypass reference check)"),
				*AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("deleted"), true);
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("package_name"), PackageName);
		Data->SetNumberField(TEXT("num_deleted"), NumDeleted);
		Data->SetBoolField(TEXT("force"), bForce);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Delete asset timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleListRedirectors(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath))
		return CreateErrorResponse(TEXT("Missing 'folder_path' parameter"));

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [FolderPath, bRecursive, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*FolderPath));
		Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<TSharedPtr<FJsonValue>> Array;
		Array.Reserve(Assets.Num());
		for (const FAssetData& AssetData : Assets)
		{
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset());
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
			Entry->SetStringField(TEXT("destination_path"),
				(Redirector && Redirector->DestinationObject)
					? Redirector->DestinationObject->GetPathName()
					: TEXT(""));
			Entry->SetBoolField(TEXT("stale"),
				!(Redirector && Redirector->DestinationObject));
			Array.Add(MakeShared<FJsonValueObject>(Entry));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("folder_path"), FolderPath);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("count"), Array.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("redirectors"), Array);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List redirectors timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleFixupRedirectors(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString FolderPath;
	if (!Params->TryGetStringField(TEXT("folder_path"), FolderPath))
		return CreateErrorResponse(TEXT("Missing 'folder_path' parameter"));

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [FolderPath, bRecursive, Promise, this]()
	{
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARM.Get();

		const bool bScanIncomplete = AR.IsLoadingAssets();
		if (bScanIncomplete)
		{
			AR.WaitForCompletion();
		}

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*FolderPath));
		Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = bRecursive;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		TArray<UObjectRedirector*> Redirectors;
		TArray<TSharedPtr<FJsonValue>> Skipped;
		Redirectors.Reserve(Assets.Num());
		for (const FAssetData& AssetData : Assets)
		{
			UObjectRedirector* Redirector = Cast<UObjectRedirector>(AssetData.GetAsset());
			if (!Redirector)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
				Entry->SetStringField(TEXT("reason"), TEXT("load_failed"));
				Skipped.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			if (!Redirector->DestinationObject)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("redirector_path"), AssetData.GetSoftObjectPath().ToString());
				Entry->SetStringField(TEXT("reason"), TEXT("stale_no_destination"));
				Skipped.Add(MakeShared<FJsonValueObject>(Entry));
				continue;
			}
			Redirectors.Add(Redirector);
		}

		const int32 FoundCount = Assets.Num();
		const int32 FixedCount = Redirectors.Num();

		if (Redirectors.Num() > 0)
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
			AssetToolsModule.Get().FixupReferencers(Redirectors, /*bCheckoutDialogPrompt=*/false);
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("folder_path"), FolderPath);
		Data->SetBoolField(TEXT("recursive"), bRecursive);
		Data->SetNumberField(TEXT("redirectors_found"), FoundCount);
		Data->SetNumberField(TEXT("redirectors_fixed"), FixedCount);
		Data->SetNumberField(TEXT("skipped_count"), Skipped.Num());
		Data->SetBoolField(TEXT("scan_was_incomplete"), bScanIncomplete);
		Data->SetArrayField(TEXT("skipped"), Skipped);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Fixup redirectors timed out"));
	return Future.Get();
}

// =============================================================================
// Input Mapping Context Command Handlers
// =============================================================================

FString FAIExportTCPServer::HandleAddInputMapping(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, InputActionPath, KeyName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("input_action_path"), InputActionPath))
		return CreateErrorResponse(TEXT("Missing 'input_action_path'"));
	if (!Params->TryGetStringField(TEXT("key"), KeyName))
		return CreateErrorResponse(TEXT("Missing 'key'"));

	// Parse optional trigger/modifier arrays
	TArray<FString> TriggerClasses, ModifierClasses;
	const TArray<TSharedPtr<FJsonValue>>* TriggersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("triggers"), TriggersArr) && TriggersArr)
	{
		for (const auto& Val : *TriggersArr)
		{
			FString Str;
			if (Val->TryGetString(Str)) TriggerClasses.Add(Str);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArr = nullptr;
	if (Params->TryGetArrayField(TEXT("modifiers"), ModifiersArr) && ModifiersArr)
	{
		for (const auto& Val : *ModifiersArr)
		{
			FString Str;
			if (Val->TryGetString(Str)) ModifierClasses.Add(Str);
		}
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, InputActionPath, KeyName, TriggerClasses, ModifierClasses, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
		if (!IMC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Not an InputMappingContext: %s"), *AssetPath))); return; }

		bool bSuccess = UAIAssetFactory::AddInputMapping(IMC, InputActionPath, KeyName, TriggerClasses, ModifierClasses);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add input mapping"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("success"), true);
		Data->SetStringField(TEXT("action"), InputActionPath);
		Data->SetStringField(TEXT("key"), KeyName);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add input mapping timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveInputMapping(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	double MappingIndex = 0;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetNumberField(TEXT("mapping_index"), MappingIndex))
		return CreateErrorResponse(TEXT("Missing 'mapping_index'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	int32 Index = static_cast<int32>(MappingIndex);

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Index, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
		if (!IMC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Not an InputMappingContext: %s"), *AssetPath))); return; }

		bool bSuccess = UAIAssetFactory::RemoveInputMapping(IMC, Index);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove mapping at index %d"), Index))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("removed"), true);
		Data->SetNumberField(TEXT("mapping_index"), Index);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Remove input mapping timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetInputMappings(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset);
		if (!IMC) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Not an InputMappingContext: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIAssetFactory::GetInputMappingsAsJson(IMC);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get input mappings timed out"));
	return Future.Get();
}

// =============================================================================
// AnimBlueprint Builder Command Handlers
// =============================================================================

FString FAIExportTCPServer::HandleCreateAnimBlueprint(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PackagePath, AssetName, SkeletonPath;
	FString ParentClass = TEXT("AnimInstance");

	if (!Params->TryGetStringField(TEXT("package_path"), PackagePath))
		return CreateErrorResponse(TEXT("Missing 'package_path' parameter"));
	if (!Params->TryGetStringField(TEXT("asset_name"), AssetName))
		return CreateErrorResponse(TEXT("Missing 'asset_name' parameter"));
	if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		return CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));

	Params->TryGetStringField(TEXT("parent_class"), ParentClass);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, SkeletonPath, ParentClass, Promise, this]()
	{
		UAnimBlueprint* AnimBP = UAIAnimBlueprintBuilder::CreateAnimBlueprint(PackagePath, AssetName, SkeletonPath, ParentClass);
		if (!AnimBP)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to create AnimBlueprint")));
			return;
		}
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AnimBP->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("skeleton"), SkeletonPath);
		Data->SetStringField(TEXT("parent_class"), AnimBP->ParentClass ? AnimBP->ParentClass->GetName() : TEXT("None"));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Create AnimBlueprint timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleGetAnimBlueprintInfo(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path'"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Promise, this]()
	{
		UAnimBlueprint* AnimBP = UAIAnimBlueprintBuilder::LoadAnimBlueprint(AssetPath);
		if (!AnimBP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AssetPath))); return; }

		TSharedPtr<FJsonObject> Data = UAIAnimBlueprintBuilder::GetAnimBlueprintInfoAsJson(AnimBP);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Get AnimBlueprint info timed out"));
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Widget Preview Capture — IFTP verify loop

FString FAIExportTCPServer::HandleCaptureWidgetPreview(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	// Parse primitive parameters (use double then clamp/cast)
	int32 Width = 1920;
	int32 Height = 1080;
	int32 WarmupFrames = 3;
	float DPIScale = 1.0f;
	bool bTransparentBG = false;
	bool bReturnBase64 = false;
	FString OutputPath;
	FString PreviewMode = TEXT("runtime");
	struct FPreviewFunctionCall
	{
		FString WidgetName;
		FString FunctionName;
		TMap<FString, FString> Args;
	};
	TArray<FPreviewFunctionCall> PreviewFunctionCalls;

	{
		double DVal = 0.0;
		if (Params->TryGetNumberField(TEXT("width"), DVal))         Width = FMath::Clamp((int32)DVal, 16, 8192);
		if (Params->TryGetNumberField(TEXT("height"), DVal))        Height = FMath::Clamp((int32)DVal, 16, 8192);
		if (Params->TryGetNumberField(TEXT("warmup_frames"), DVal)) WarmupFrames = FMath::Clamp((int32)DVal, 1, 10);
		if (Params->TryGetNumberField(TEXT("dpi_scale"), DVal))     DPIScale = FMath::Clamp((float)DVal, 0.1f, 8.0f);
	}
	Params->TryGetBoolField(TEXT("transparent_bg"), bTransparentBG);
	Params->TryGetBoolField(TEXT("return_base64"), bReturnBase64);
	Params->TryGetStringField(TEXT("output_path"), OutputPath);
	Params->TryGetStringField(TEXT("preview_mode"), PreviewMode);
	PreviewMode.TrimStartAndEndInline();
	PreviewMode.ToLowerInline();
	if (PreviewMode.IsEmpty())
	{
		PreviewMode = TEXT("runtime");
	}
	if (PreviewMode != TEXT("runtime") && PreviewMode != TEXT("designer"))
	{
		return CreateErrorResponse(TEXT("Invalid 'preview_mode'. Expected 'runtime' or 'designer'."));
	}

	const TArray<TSharedPtr<FJsonValue>>* FunctionCallsArray = nullptr;
	if (Params->TryGetArrayField(TEXT("preview_function_calls"), FunctionCallsArray) && FunctionCallsArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *FunctionCallsArray)
		{
			const TSharedPtr<FJsonObject>* CallObj = nullptr;
			if (!Entry->TryGetObject(CallObj) || !CallObj->IsValid())
			{
				continue;
			}

			FPreviewFunctionCall Call;
			(*CallObj)->TryGetStringField(TEXT("widget_name"), Call.WidgetName);
			if (!(*CallObj)->TryGetStringField(TEXT("function_name"), Call.FunctionName) || Call.FunctionName.IsEmpty())
			{
				return CreateErrorResponse(TEXT("Invalid 'preview_function_calls' entry: missing 'function_name'."));
			}

			const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
			if ((*CallObj)->TryGetObjectField(TEXT("args"), ArgsObj) && ArgsObj->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& ArgPair : (*ArgsObj)->Values)
				{
					FString ArgValue;
					if (ArgPair.Value->TryGetString(ArgValue))
					{
						Call.Args.Add(ArgPair.Key, ArgValue);
					}
					else
					{
						Call.Args.Add(ArgPair.Key, ArgPair.Value->AsString());
					}
				}
			}

			PreviewFunctionCalls.Add(MoveTemp(Call));
		}
	}

	// Parse optional ratios array (multi-ratio mode)
	// Each entry: { "width": 2560, "height": 1080, "label": "21x9" }
	struct FRatioEntry
	{
		int32 Width;
		int32 Height;
		FString Label;
	};
	TArray<FRatioEntry> Ratios;
	const TArray<TSharedPtr<FJsonValue>>* RatiosArray = nullptr;
	if (Params->TryGetArrayField(TEXT("ratios"), RatiosArray) && RatiosArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *RatiosArray)
		{
			const TSharedPtr<FJsonObject>* RatioObj = nullptr;
			if (!Entry->TryGetObject(RatioObj) || !RatioObj->IsValid()) continue;
			FRatioEntry R;
			double RW = 1920.0, RH = 1080.0;
			(*RatioObj)->TryGetNumberField(TEXT("width"), RW);
			(*RatioObj)->TryGetNumberField(TEXT("height"), RH);
			R.Width = FMath::Clamp((int32)RW, 16, 8192);
			R.Height = FMath::Clamp((int32)RH, 16, 8192);
			(*RatioObj)->TryGetStringField(TEXT("label"), R.Label);
			Ratios.Add(R);
		}
	}

	if (Ratios.Num() == 0)
	{
		FRatioEntry R;
		R.Width = Width;
		R.Height = Height;
		Ratios.Add(R);
	}

	// Output directory
	FString DefaultOutputDir = FPaths::ProjectIntermediateDir() / TEXT("WidgetCaptures");
	FString OutputDir = OutputPath.IsEmpty() ? DefaultOutputDir : FPaths::GetPath(OutputPath);
	IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Ratios, WarmupFrames, DPIScale, bTransparentBG, bReturnBase64, OutputPath, OutputDir, PreviewMode, PreviewFunctionCalls, Promise, this]()
	{
		// 1) Load Widget Blueprint
		UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
		if (!WidgetBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath)));
			return;
		}

		UClass* WidgetClass = WidgetBP->GeneratedClass;
		if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Invalid WidgetBlueprint GeneratedClass")));
			return;
		}

		// 2) Get editor world for widget context
		UWorld* World = nullptr;
		if (GEditor)
		{
			World = GEditor->GetEditorWorldContext().World();
		}
		if (!World)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No valid editor world for widget instantiation")));
			return;
		}

		// 3) Create widget instance
		UUserWidget* UserWidget = CreateWidget<UUserWidget>(World, WidgetClass);
		if (!UserWidget)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Failed to instantiate UserWidget")));
			return;
		}
		UserWidget->AddToRoot();  // Prevent GC during rendering

#if WITH_EDITOR
		if (PreviewMode == TEXT("designer"))
		{
			// Explicit designer preview path for widgets with IsDesignTime()-gated
			// sample data. Runtime acceptance captures must not set these flags.
			UserWidget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
			UserWidget->SynchronizeProperties();
		}
#endif

		// Runtime CommonUI construction can bind default input actions before any
		// LocalPlayer/CommonInputSubsystem exists in this offscreen editor context.
		ICommonInputModule::GetSettings().LoadData();

		// 4) Take Slate widget — triggers outer widget's Initialize + PreConstruct.
		TSharedRef<SWidget> SlateWidget = UserWidget->TakeWidget();

		// CommonUI screens often synchronize state during activation rather than
		// designer PreConstruct. Offscreen captures need that lifecycle too, or
		// button text, selected tabs, and settings rows can render with defaults.
		if (UCommonActivatableWidget* ActivatableWidget = Cast<UCommonActivatableWidget>(UserWidget))
		{
			ActivatableWidget->ActivateWidget();
		}

		int32 PreviewFunctionCallCount = 0;
		for (const FPreviewFunctionCall& Call : PreviewFunctionCalls)
		{
			UObject* Target = UserWidget;
			if (!Call.WidgetName.IsEmpty())
			{
				Target = UserWidget->WidgetTree ? UserWidget->WidgetTree->FindWidget(FName(*Call.WidgetName)) : nullptr;
			}
			if (!Target)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("preview_function_calls target widget not found: %s"), *Call.WidgetName)));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			UFunction* Function = Target->FindFunction(FName(*Call.FunctionName));
			if (!Function)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("preview_function_calls function not found: %s on %s"), *Call.FunctionName, *Target->GetName())));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			TArray<uint8> ParamBuffer;
			void* ParamData = nullptr;
			if (Function->ParmsSize > 0)
			{
				ParamBuffer.SetNumZeroed(Function->ParmsSize);
				ParamData = ParamBuffer.GetData();
				Function->InitializeStruct(ParamData);
			}

			bool bParamImportSucceeded = true;
			FString ParamError;
			for (TFieldIterator<FProperty> It(Function); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop->HasAnyPropertyFlags(CPF_Parm) || Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					continue;
				}

				const FString* ArgValue = Call.Args.Find(Prop->GetName());
				if (!ArgValue)
				{
					continue;
				}

				if (!ParamData)
				{
					bParamImportSucceeded = false;
					ParamError = FString::Printf(TEXT("Function %s has no parameter storage for preview arg %s"), *Call.FunctionName, *Prop->GetName());
					break;
				}

				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(ParamData);
				if (!Prop->ImportText_Direct(**ArgValue, PropAddr, Target, PPF_None))
				{
					bParamImportSucceeded = false;
					ParamError = FString::Printf(TEXT("Failed to import preview function arg %s=%s for %s"), *Prop->GetName(), **ArgValue, *Call.FunctionName);
					break;
				}
			}

			if (!bParamImportSucceeded)
			{
				if (ParamData)
				{
					Function->DestroyStruct(ParamData);
				}
				Promise->SetValue(CreateErrorResponse(ParamError));
				UserWidget->ReleaseSlateResources(true);
				UserWidget->RemoveFromRoot();
				return;
			}

			Target->ProcessEvent(Function, ParamData);
			if (ParamData)
			{
				Function->DestroyStruct(ParamData);
			}
			++PreviewFunctionCallCount;
		}

		// 4a) Force-initialize all nested UUserWidget components, then preload textures.
		//     Nested UUserWidgets in the WidgetTree are NOT auto-initialized by the outer
		//     widget's TakeWidget() — they init lazily when first painted. That means their
		//     BP PreConstruct (which calls SetBrushFromTexture(NavIcon) etc.) has not yet run,
		//     so Brush.ResourceObject is null when we try to force-stream textures.
		//     Fix: explicitly Initialize() each nested UserWidget first, then walk again to
		//     collect the now-populated brush textures and force their mip residency.
		FlushAsyncLoading();
		int32 InitCount = 0;
		int32 TexCount = 0;
		int32 SyncCount = 0;
		int32 StreamingWaitSkippedCount = 0;
		{
			auto ForceTextureResidentForCapture = [&TexCount, &StreamingWaitSkippedCount](UTexture2D* Tex)
			{
				if (!Tex)
				{
					return;
				}

				Tex->SetForceMipLevelsToBeResident(30.0f, true);
				if (!IsAssetStreamingSuspended())
				{
					Tex->WaitForStreaming();
				}
				else
				{
					++StreamingWaitSkippedCount;
				}
				++TexCount;
			};

			// Pass 1: force Initialize() on all nested UUserWidget instances (recursive)
			TFunction<void(UWidgetTree*)> InitTree;
			InitTree = [&InitTree, &InitCount, &PreviewMode](UWidgetTree* Tree)
			{
				if (!Tree) return;
				Tree->ForEachWidget([&InitTree, &InitCount, &PreviewMode](UWidget* W)
				{
					if (UUserWidget* NestedUW = Cast<UUserWidget>(W))
					{
#if WITH_EDITOR
						if (PreviewMode == TEXT("designer"))
						{
							NestedUW->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
						}
#endif
						if (!NestedUW->IsConstructed())
						{
							NestedUW->Initialize();  // runs BP PreConstruct on this nested instance
							++InitCount;
						}
						NestedUW->TakeWidget();
						InitTree(NestedUW->WidgetTree);
					}
				});
			};
			InitTree(UserWidget->WidgetTree);

			// Pass 2: collect + stream all brush textures (recursive), then SynchronizeProperties
			//         to push the updated brush into the already-built SImage slate widget.
			//         PreConstruct's SetBrushFromTexture() only pushes to Slate if MyImage.IsValid()
			//         at call time. When PreConstruct runs during nested Initialize(), MyImage is
			//         usually null (slate widget not yet built), so the brush sits in the UImage
			//         struct but is never copied to the SImage. A manual SynchronizeProperties()
			//         after the slate tree is built (i.e. after TakeWidget) does exactly that copy.
			TFunction<void(UWidgetTree*)> PreloadTree;
			PreloadTree = [&PreloadTree, &ForceTextureResidentForCapture, &SyncCount](UWidgetTree* Tree)
			{
				if (!Tree) return;
				Tree->ForEachWidget([&PreloadTree, &ForceTextureResidentForCapture, &SyncCount](UWidget* W)
				{
					W->SynchronizeProperties();
					++SyncCount;

					if (UImage* Img = Cast<UImage>(W))
					{
						if (UTexture2D* Tex = Cast<UTexture2D>(Img->Brush.GetResourceObject()))
						{
							ForceTextureResidentForCapture(Tex);
						}
					}
					else if (UBorder* Brd = Cast<UBorder>(W))
					{
						// Same bridging issue as UImage: SBorder built before CDO Background
						// was pushed via reflection ImportText. Force a resync so
						// set_widget_property changes land on the Slate side.
						if (UTexture2D* Tex = Cast<UTexture2D>(Brd->Background.GetResourceObject()))
						{
							ForceTextureResidentForCapture(Tex);
						}
					}
					else if (UUserWidget* NestedUW = Cast<UUserWidget>(W))
					{
						NestedUW->SynchronizeProperties();
						PreloadTree(NestedUW->WidgetTree);
					}
				});
			};
			PreloadTree(UserWidget->WidgetTree);
		}
		// Final global streaming flush — catches anything ForceMipLevelsToBeResident missed.
		if (!IsAssetStreamingSuspended())
		{
			IStreamingManager::Get().StreamAllResources(0.0f);
		}
		else
		{
			++StreamingWaitSkippedCount;
		}
		UE_LOG(LogAIExport, Log, TEXT("CaptureWidgetPreview[%s]: applied %d preview function calls, initialized %d nested widgets, synchronized %d widgets, streamed %d textures, skipped %d streaming waits"), *PreviewMode, PreviewFunctionCallCount, InitCount, SyncCount, TexCount, StreamingWaitSkippedCount);

		// 5) Get ImageWrapper module
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

		// 6) Derive base filename
		FString AssetBaseName = FPaths::GetBaseFilename(AssetPath);

		// 7) Widget renderer (gamma correction on for sRGB output)
		TSharedPtr<FWidgetRenderer> Renderer = MakeShared<FWidgetRenderer>(/*bUseGammaCorrection=*/true);

		TArray<TSharedPtr<FJsonValue>> PngResults;
		bool bAllSucceeded = true;
		FString LastError;

		for (const FRatioEntry& Ratio : Ratios)
		{
			const int32 RW = Ratio.Width;
			const int32 RH = Ratio.Height;

			// Create render target — RTF_RGBA8 (raw UNORM) + TargetGamma=0 (use DisplayGamma default).
			// FWidgetRenderer(bUseGammaCorrection=true) already applies linear→sRGB in shader
			// using RT->GetDisplayGamma(). Setting TargetGamma=2.2 + sRGB format causes double
			// gamma (values look washed out). Setting TargetGamma=0 lets it fall through to
			// Engine->DisplayGamma (2.2) applied exactly once. Matches editor viewport.
			UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
			RT->ClearColor = bTransparentBG ? FLinearColor::Transparent : FLinearColor::Black;
			RT->TargetGamma = 0.0f;
			RT->RenderTargetFormat = RTF_RGBA8;
			RT->InitAutoFormat(RW, RH);
			RT->UpdateResourceImmediate(true);
			RT->AddToRoot();

			// Warmup + final render passes (absorb texture streaming delay)
			const FVector2D DrawSize(RW, RH);
			for (int32 i = 0; i < WarmupFrames; ++i)
			{
				Renderer->DrawWidget(RT, SlateWidget, DrawSize, 0.016f, false);
			}

			// Flush GPU work before reading pixels
			FlushRenderingCommands();

			// Read pixels
			TArray<FColor> Bitmap;
			FRenderTarget* RenderTargetResource = RT->GameThread_GetRenderTargetResource();
			if (!RenderTargetResource || !RenderTargetResource->ReadPixels(Bitmap))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to read pixels for %dx%d"), RW, RH);
				RT->RemoveFromRoot();
				continue;
			}

			// Encode PNG
			TSharedPtr<IImageWrapper> PNGWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			if (!PNGWrapper.IsValid() ||
				!PNGWrapper->SetRaw(Bitmap.GetData(),
									Bitmap.Num() * sizeof(FColor),
									RW, RH,
									ERGBFormat::BGRA, 8))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to encode PNG for %dx%d"), RW, RH);
				RT->RemoveFromRoot();
				continue;
			}

			const TArray64<uint8>& CompressedPng = PNGWrapper->GetCompressed(100);

			// Flatten into TArray<uint8> for FFileHelper + FBase64 compatibility
			TArray<uint8> FlatPng;
			FlatPng.SetNumUninitialized((int32)CompressedPng.Num());
			FMemory::Memcpy(FlatPng.GetData(), CompressedPng.GetData(), CompressedPng.Num());

			// Derive output file path
			FString OutPath;
			if (Ratios.Num() == 1 && !OutputPath.IsEmpty())
			{
				OutPath = OutputPath;
			}
			else
			{
				FString Suffix = Ratio.Label.IsEmpty()
					? FString::Printf(TEXT("_%dx%d"), RW, RH)
					: FString::Printf(TEXT("_%s"), *Ratio.Label);
				Suffix.ReplaceInline(TEXT(":"), TEXT(""));
				Suffix.ReplaceInline(TEXT("/"), TEXT("_"));
				Suffix.ReplaceInline(TEXT("\\"), TEXT("_"));
				OutPath = OutputDir / (AssetBaseName + Suffix + TEXT(".png"));
			}

			// Save to disk
			if (!FFileHelper::SaveArrayToFile(FlatPng, *OutPath))
			{
				bAllSucceeded = false;
				LastError = FString::Printf(TEXT("Failed to write PNG: %s"), *OutPath);
				RT->RemoveFromRoot();
				continue;
			}

			// Build JSON entry
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("png_path"), OutPath);
			Entry->SetNumberField(TEXT("width"), RW);
			Entry->SetNumberField(TEXT("height"), RH);
			Entry->SetNumberField(TEXT("size_bytes"), FlatPng.Num());
			Entry->SetStringField(TEXT("preview_mode"), PreviewMode);
			if (!Ratio.Label.IsEmpty())
			{
				Entry->SetStringField(TEXT("label"), Ratio.Label);
			}
			if (bReturnBase64)
			{
				FString B64 = FBase64::Encode(FlatPng);
				Entry->SetStringField(TEXT("png_base64"), B64);
			}
			PngResults.Add(MakeShared<FJsonValueObject>(Entry));

			// Cleanup RT
			RT->RemoveFromRoot();
		}

		// Cleanup widget
		if (UCommonActivatableWidget* ActivatableWidget = Cast<UCommonActivatableWidget>(UserWidget))
		{
			if (ActivatableWidget->IsActivated())
			{
				ActivatableWidget->DeactivateWidget();
			}
		}
		UserWidget->ReleaseSlateResources(true);
		UserWidget->RemoveFromRoot();

		if (PngResults.Num() == 0)
		{
			Promise->SetValue(CreateErrorResponse(LastError.IsEmpty() ? TEXT("No previews produced") : LastError));
			return;
		}

		// Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetStringField(TEXT("preview_mode"), PreviewMode);
		Data->SetNumberField(TEXT("preview_function_calls_applied"), PreviewFunctionCallCount);
		Data->SetArrayField(TEXT("pngs"), PngResults);
		Data->SetNumberField(TEXT("count"), PngResults.Num());
		if (!bAllSucceeded)
		{
			Data->SetStringField(TEXT("partial_error"), LastError);
		}

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(120.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Widget preview capture timed out"));
	return Future.Get();
}

//////////////////////////////////////////////////////////////////////////
// Asset Lifecycle — Reload asset (fixes cached editor tab after compile_and_save)

FString FAIExportTCPServer::HandleReloadAsset(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));

	bool bReopenAfter = true;
	Params->TryGetBoolField(TEXT("reopen_after"), bReopenAfter);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, bReopenAfter, Promise, this]()
	{
		// 1) Silent existence check BEFORE LoadObject (avoids UE log spam on bad paths)
		//    Extract package path from asset path (strip .ObjectName suffix if present)
		FString PackagePath = AssetPath;
		int32 DotIdx;
		if (PackagePath.FindChar('.', DotIdx))
		{
			PackagePath = PackagePath.Left(DotIdx);
		}
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Package does not exist on disk: %s"), *PackagePath)));
			return;
		}

		// 2) Load the asset (safe now — package verified to exist)
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)));
			return;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Asset has no outer package")));
			return;
		}

		// 2) Check if asset editor is currently open, remember state
		bool bWasOpen = false;
		UAssetEditorSubsystem* EditorSubsystem = nullptr;
		if (GEditor)
		{
			EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (EditorSubsystem)
			{
				// FindEditorsForAsset returns nullptr if not open
				bWasOpen = EditorSubsystem->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false) != nullptr;

				// Close all editors for this asset (clears cached widget instance)
				if (bWasOpen)
				{
					EditorSubsystem->CloseAllEditorsForAsset(Asset);
				}
			}
		}

		// 3) Hard reload the package (reloads from disk, discards in-memory cache)
		TArray<UPackage*> PackagesToReload;
		PackagesToReload.Add(Package);

		FText ErrorMsg;
		bool bReloaded = UPackageTools::ReloadPackages(PackagesToReload, ErrorMsg, EReloadPackagesInteractionMode::AssumePositive);

		// 4) Reopen editor if it was previously open and reopen_after flag is true
		bool bReopened = false;
		if (bReopenAfter && bWasOpen && EditorSubsystem)
		{
			// Load fresh reference after reload
			UObject* FreshAsset = LoadObject<UObject>(nullptr, *AssetPath);
			if (FreshAsset)
			{
				bReopened = EditorSubsystem->OpenEditorForAsset(FreshAsset);
			}
		}

		// 5) Build response
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), AssetPath);
		Data->SetBoolField(TEXT("was_open"), bWasOpen);
		Data->SetBoolField(TEXT("reloaded"), bReloaded);
		Data->SetBoolField(TEXT("reopened"), bReopened);

		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(30.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Reload asset timed out"));
	return Future.Get();
}

#undef GRAPH_NODE_HANDLER_BODY
