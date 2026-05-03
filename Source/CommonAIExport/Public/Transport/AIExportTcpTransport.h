// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "Templates/Function.h"

class FSocket;

namespace CommonAIExport::Transport
{
struct FAIExportTcpTransportCallbacks
{
	TFunction<FString(const FString& JsonCommand)> ProcessCommand;
	TFunction<bool()> IsStopRequested;
	TFunction<void()> OnListening;
	TFunction<void()> OnStopped;
};

class FAIExportTcpTransport
{
public:
	uint32 Run(int32 ServerPort, const FAIExportTcpTransportCallbacks& InCallbacks);
	void Stop();

	int32 GetActiveClientConnections() const { return ActiveClientConnections.GetValue(); }

private:
	void HandleClientConnection(FSocket* ClientSocket, const FAIExportTcpTransportCallbacks& InCallbacks);

	FSocket* ListenerSocket = nullptr;
	FThreadSafeCounter ActiveClientConnections;
};
}
