// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Transport/AIExportTcpTransport.h"
#include "CommonAIExportModule.h"

#include "Async/Async.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace CommonAIExport::Transport
{
uint32 FAIExportTcpTransport::Run(int32 ServerPort, const FAIExportTcpTransportCallbacks& InCallbacks)
{
	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server thread started on port %d"), ServerPort);

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to get socket subsystem"));
		return 1;
	}

	ListenerSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("AIExportTCPServer"), false);
	if (!ListenerSocket)
	{
		UE_LOG(LogAIExport, Error, TEXT("Failed to create listener socket"));
		return 1;
	}

	ListenerSocket->SetReuseAddr(true);
	ListenerSocket->SetNoDelay(true);

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

	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server listening on 127.0.0.1:%d"), ServerPort);
	if (InCallbacks.OnListening)
	{
		InCallbacks.OnListening();
	}

	while (!InCallbacks.IsStopRequested || !InCallbacks.IsStopRequested())
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
					ActiveClientConnections.Increment();
					Async(EAsyncExecution::ThreadPool, [this, ClientSocket, SocketSubsystem, InCallbacks]()
					{
						HandleClientConnection(ClientSocket, InCallbacks);
						SocketSubsystem->DestroySocket(ClientSocket);
						ActiveClientConnections.Decrement();
					});
				}
			}
		}
	}

	if (ListenerSocket)
	{
		SocketSubsystem->DestroySocket(ListenerSocket);
		ListenerSocket = nullptr;
	}

	while (ActiveClientConnections.GetValue() > 0)
	{
		FPlatformProcess::Sleep(0.01f);
	}

	if (InCallbacks.OnStopped)
	{
		InCallbacks.OnStopped();
	}

	UE_LOG(LogAIExport, Log, TEXT("AIExport TCP Server stopped"));
	return 0;
}

void FAIExportTcpTransport::Stop()
{
	if (ListenerSocket)
	{
		ListenerSocket->Close();
	}
}

void FAIExportTcpTransport::HandleClientConnection(FSocket* ClientSocket, const FAIExportTcpTransportCallbacks& InCallbacks)
{
	if (!ClientSocket)
	{
		return;
	}

	int32 ActualSize = 0;
	ClientSocket->SetReceiveBufferSize(65536, ActualSize);

	TArray<uint8> RecvBuffer;
	RecvBuffer.SetNumZeroed(65536);

	int32 BytesRead = 0;
	ClientSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5.0));

	if (ClientSocket->Recv(RecvBuffer.GetData(), RecvBuffer.Num(), BytesRead))
	{
		if (BytesRead > 0)
		{
			FString JsonCommand = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(RecvBuffer.GetData())));
			JsonCommand = JsonCommand.Left(BytesRead);

			UE_LOG(LogAIExport, Verbose, TEXT("Received command: %s"), *JsonCommand.Left(200));

			const FString Response = InCallbacks.ProcessCommand ? InCallbacks.ProcessCommand(JsonCommand) : TEXT("{\"success\":false,\"error\":\"TCP transport command callback is not configured\"}");
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
}
