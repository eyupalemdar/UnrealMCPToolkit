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

	/** Command handlers */
	FString HandlePing();
	FString HandleExportWidget(TSharedPtr<class FJsonObject> Params);
	FString HandleExportBlueprint(TSharedPtr<class FJsonObject> Params);
	FString HandleListSupportedTypes();

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
