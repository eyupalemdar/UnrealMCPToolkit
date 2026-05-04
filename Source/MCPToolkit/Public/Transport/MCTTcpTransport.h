// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Templates/Function.h"

class FSocket;

namespace MCPToolkit::Transport
{
struct FMCTTcpTransportCallbacks
{
	TFunction<FString(const FString& JsonCommand)> ProcessCommand;
	TFunction<bool()> IsStopRequested;
	TFunction<void()> OnListening;
	TFunction<void()> OnStopped;
};

class FMCTTcpTransport
{
public:
	uint32 Run(int32 ServerPort, const FMCTTcpTransportCallbacks& InCallbacks);
	void Stop();

	int32 GetActiveClientConnections() const { return ActiveClientConnections.GetValue(); }

private:
	void HandleClientConnection(FSocket* ClientSocket, const FMCTTcpTransportCallbacks& InCallbacks);

	FSocket* ListenerSocket = nullptr;
	FThreadSafeCounter ActiveClientConnections;
};
}
