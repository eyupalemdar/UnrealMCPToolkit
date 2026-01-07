// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExportTCPServer.h"
#include "AIExportFunctionLibrary.h"
#include "CommonAIExportModule.h"

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

	if (!Params->TryGetStringField(TEXT("output_directory"), OutputDirectory))
	{
		return CreateErrorResponse(TEXT("Missing 'output_directory' parameter"));
	}

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

	if (!Params->TryGetStringField(TEXT("output_directory"), OutputDirectory))
	{
		return CreateErrorResponse(TEXT("Missing 'output_directory' parameter"));
	}

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
