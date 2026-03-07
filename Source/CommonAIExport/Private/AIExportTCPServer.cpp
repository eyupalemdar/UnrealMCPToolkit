// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "AIExportFunctionLibrary.h"
#include "Builders/AIWidgetBlueprintBuilder.h"
#include "Builders/AIMaterialBuilder.h"
#include "Builders/AIBlueprintGraphBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "CommonAIExportModule.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#include "WidgetBlueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
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

#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

#include "Engine/Font.h"
#include "Engine/FontFace.h"

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
	else if (CommandType == TEXT("create_blueprint"))
	{
		return HandleCreateBlueprint(Params);
	}
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
	// Asset Import commands
	else if (CommandType == TEXT("import_texture"))
	{
		return HandleImportTexture(Params);
	}
	else if (CommandType == TEXT("import_font"))
	{
		return HandleImportFont(Params);
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

FString FAIExportTCPServer::HandleCreateBlueprint(TSharedPtr<FJsonObject> Params)
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
	if (!Params->TryGetStringField(TEXT("parent_class"), ParentClassPath))
	{
		return CreateErrorResponse(TEXT("Missing 'parent_class' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [PackagePath, AssetName, ParentClassPath, Promise, this]()
	{
		// Resolve parent class
		UClass* ParentClass = FindObject<UClass>(nullptr, *ParentClassPath);
		if (!ParentClass)
		{
			ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
		}
		if (!ParentClass)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not find parent class: %s"), *ParentClassPath)));
			return;
		}

		// Build full package path
		FString FullPath = PackagePath / AssetName;

		// Check if already exists
		if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *FullPath))
		{
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("asset_path"), Existing->GetPathName());
			Data->SetStringField(TEXT("asset_name"), AssetName);
			Data->SetBoolField(TEXT("already_existed"), true);
			Promise->SetValue(CreateSuccessResponse(Data));
			return;
		}

		// Create package
		UPackage* Package = CreatePackage(*FullPath);
		if (!Package)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to create package at '%s'"), *FullPath)));
			return;
		}

		// Create the Blueprint
		UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			FName(*AssetName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);

		if (!NewBP)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("FKismetEditorUtilities::CreateBlueprint failed")));
			return;
		}

		// Compile
		FKismetEditorUtilities::CompileBlueprint(NewBP);

		// Notify asset registry
		FAssetRegistryModule::AssetCreated(NewBP);
		NewBP->MarkPackageDirty();

		// Save to disk
		FString PackageFilename = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, NewBP, *PackageFilename, SaveArgs);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("asset_path"), NewBP->GetPathName());
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
		Data->SetBoolField(TEXT("saved"), bSaved);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TEXT("Create blueprint timed out"));
	}
	return Future.Get();
}

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
		// Try Widget Blueprint first, then generic Blueprint
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
			return;
		}

		// Fallback: generic Blueprint compile and save
		UBlueprint* BP = UAIWidgetBlueprintBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath)));
			return;
		}

		FKismetEditorUtilities::CompileBlueprint(BP);

		FString PackagePath = BP->GetPackage()->GetPathName();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(BP->GetPackage(), BP, *PackageFilename, SaveArgs);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("compiled"), true);
		Data->SetBoolField(TEXT("saved"), bSaved);
		Promise->SetValue(CreateSuccessResponse(Data));
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
		FTypeface& DefaultTypeface = CompositeFont->CompositeFont.DefaultTypeface;
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
		UBlueprint* BP = UAIWidgetBlueprintBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath)));
			return;
		}

		bool bSuccess = UAIWidgetBlueprintBuilder::SetCDOProperty(BP, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set CDO property '%s'"), *PropertyName)));
			return;
		}

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
		UBlueprint* BP = UAIWidgetBlueprintBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath)));
			return;
		}

		TSharedPtr<FJsonObject> PropsJson = UAIWidgetBlueprintBuilder::GetCDOPropertiesAsJson(BP);
		Promise->SetValue(CreateSuccessResponse(PropsJson));
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

	FString AssetPath, ArrayName, ElementValuesJson;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("array_name"), ArrayName))
		return CreateErrorResponse(TEXT("Missing 'array_name' parameter"));
	Params->TryGetStringField(TEXT("element_values"), ElementValuesJson);

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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, ArrayName, ElementValues, Promise, this]()
	{
		UBlueprint* BP = UAIWidgetBlueprintBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath)));
			return;
		}

		// Get CDO
		UClass* GenClass = BP->GeneratedClass;
		if (!GenClass)
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
			GenClass = BP->GeneratedClass;
		}
		if (!GenClass)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass")));
			return;
		}

		UObject* CDO = GenClass->GetDefaultObject();
		if (!CDO)
		{
			Promise->SetValue(CreateErrorResponse(TEXT("No CDO")));
			return;
		}

		int32 NewIndex = UAIWidgetBlueprintBuilder::AddArrayElement(CDO, ArrayName, ElementValues);
		if (NewIndex < 0)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add element to '%s'"), *ArrayName)));
			return;
		}

		BP->MarkPackageDirty();

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
		UBlueprint* BP = UAIWidgetBlueprintBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath)));
			return;
		}

		UClass* GenClass = BP->GeneratedClass;
		if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
		if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
		UObject* CDO = GenClass->GetDefaultObject();
		if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

		bool bSuccess = UAIWidgetBlueprintBuilder::SetArrayElementProperty(CDO, ArrayName, ElementIndex, PropertyName, Value);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to set '%s' on element %d of '%s'"),
				*PropertyName, ElementIndex, *ArrayName)));
			return;
		}

		BP->MarkPackageDirty();

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
		UBlueprint* BP = UAIWidgetBlueprintBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath)));
			return;
		}

		UClass* GenClass = BP->GeneratedClass;
		if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
		if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
		UObject* CDO = GenClass->GetDefaultObject();
		if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

		bool bSuccess = UAIWidgetBlueprintBuilder::RemoveArrayElement(CDO, ArrayName, ElementIndex);
		if (!bSuccess)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to remove element %d from '%s'"),
				ElementIndex, *ArrayName)));
			return;
		}

		BP->MarkPackageDirty();

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
		UBlueprint* BP = UAIWidgetBlueprintBuilder::LoadBlueprint(AssetPath);
		if (!BP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Blueprint: %s"), *AssetPath)));
			return;
		}

		UClass* GenClass = BP->GeneratedClass;
		if (!GenClass) { FKismetEditorUtilities::CompileBlueprint(BP); GenClass = BP->GeneratedClass; }
		if (!GenClass) { Promise->SetValue(CreateErrorResponse(TEXT("No GeneratedClass"))); return; }
		UObject* CDO = GenClass->GetDefaultObject();
		if (!CDO) { Promise->SetValue(CreateErrorResponse(TEXT("No CDO"))); return; }

		int32 Length = UAIWidgetBlueprintBuilder::GetArrayLength(CDO, ArrayName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddEventNode(BP, EventName, NodeName, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, EventName, NodeName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddCustomEvent(BP, EventName, NodeName, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add custom event '%s'"), *EventName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("event_name"), EventName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, TargetClass, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddFunctionCallNode(BP, FunctionName, NodeName, TargetClass, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add function call '%s'"), *FunctionName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddVariableGetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Get '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VariableName, NodeName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddVariableSetNode(BP, VariableName, NodeName, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add Set '%s'"), *VariableName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("variable_name"), VariableName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, StructName, NodeName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddMakeStructNode(BP, StructName, NodeName, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add MakeStruct '%s'"), *StructName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("struct_name"), StructName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddBranchNode(BP, NodeName, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add branch node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
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

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FunctionName, NodeName, PosX, PosY, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		UK2Node* Node = UAIBlueprintGraphBuilder::AddCallParentFunctionNode(BP, FunctionName, NodeName, (int32)PosX, (int32)PosY);
		if (!Node) { Promise->SetValue(CreateErrorResponse(TEXT("Failed to add call parent function node"))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("node_name"), NodeName);
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetStringField(TEXT("node_class"), TEXT("K2Node_CallParentFunction"));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Add call parent function timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleConnectPins(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, FromNode, FromPin, ToNode, ToPin;
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

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, FromNode, FromPin, ToNode, ToPin, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::ConnectPins(BP, FromNode, FromPin, ToNode, ToPin);
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
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Connect pins timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleSetPinDefault(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName, PinName, DefaultValue;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));
	if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
		return CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
	if (!Params->TryGetStringField(TEXT("default_value"), DefaultValue))
		return CreateErrorResponse(TEXT("Missing 'default_value' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, PinName, DefaultValue, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::SetPinDefaultValue(BP, NodeName, PinName, DefaultValue);
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
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Set pin default timed out"));
	return Future.Get();
}

FString FAIExportTCPServer::HandleRemoveGraphNode(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString AssetPath, NodeName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("node_name"), NodeName))
		return CreateErrorResponse(TEXT("Missing 'node_name' parameter"));

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, NodeName, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::RemoveNode(BP, NodeName);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Node '%s' not found"), *NodeName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("removed"), NodeName);
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
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("var_name"), VarName))
		return CreateErrorResponse(TEXT("Missing 'var_name' parameter"));
	if (!Params->TryGetStringField(TEXT("var_type"), VarType))
		return CreateErrorResponse(TEXT("Missing 'var_type' parameter"));
	Params->TryGetBoolField(TEXT("instance_editable"), bInstanceEditable);
	Params->TryGetStringField(TEXT("category"), Category);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, VarName, VarType, bInstanceEditable, Category, Promise, this]()
	{
		UBlueprint* BP = UAIBlueprintGraphBuilder::LoadBlueprint(AssetPath);
		if (!BP) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load: %s"), *AssetPath))); return; }

		bool bSuccess = UAIBlueprintGraphBuilder::AddVariable(BP, VarName, VarType, bInstanceEditable, false, Category);
		if (!bSuccess) { Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to add variable '%s'"), *VarName))); return; }

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("var_name"), VarName);
		Data->SetStringField(TEXT("var_type"), VarType);
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

#undef GRAPH_NODE_HANDLER_BODY
