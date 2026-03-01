// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "AIExportFunctionLibrary.h"
#include "Builders/AIWidgetBlueprintBuilder.h"
#include "CommonAIExportModule.h"

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
		if (!WBP)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Could not load Widget Blueprint: %s"), *AssetPath)));
			return;
		}

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
