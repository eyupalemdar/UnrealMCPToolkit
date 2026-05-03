// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeCounter.h"
#include "HttpRouteHandle.h"
#include "Templates/Function.h"

class IHttpRouter;
struct FHttpServerRequest;
struct FHttpServerResponse;

namespace CommonAIExport::HttpMcp
{
struct FAIExportHttpMcpToolDescriptor
{
	FString Name;
	FString Category;
	FString RequiredScope = TEXT("read");
	bool bMutating = false;
	bool bSupportsDryRun = false;
};

struct FAIExportHttpMcpStatus
{
	int32 Port = 0;
	bool bRunning = false;
	bool bAuthRequired = false;
	bool bAuditEnabled = true;
	int32 SessionTtlSeconds = 3600;
	int32 ActiveSessionCount = 0;
	FString AuditLogPath;
	TArray<FString> AllowedOrigins;
	int32 HttpRequestCount = 0;
	int32 HttpRejectedRequestCount = 0;
	int32 McpRequestCount = 0;
	int32 McpRejectedRequestCount = 0;
	int32 McpSessionExpiredCount = 0;
};

struct FAIExportHttpMcpCallbacks
{
	TFunction<FString()> HandlePing;
	TFunction<FString()> HandleListCommands;
	TFunction<FString()> HandleProjectStatus;
	TFunction<FString()> HandleEditorIdentity;
	TFunction<FString(TSharedPtr<FJsonObject>)> HandleEditorLogRead;
	TFunction<FString(TSharedPtr<FJsonObject>)> HandleTaskEvents;
	TFunction<FString(TSharedPtr<FJsonObject>)> HandleTaskEventsWait;
	TFunction<FString(TSharedPtr<FJsonObject>)> BuildTaskEventsSse;
	TFunction<TSharedPtr<FJsonObject>(const FHttpServerRequest&)> BuildTaskEventParamsFromHttpRequest;
	TFunction<FString(const FString&)> ProcessCommand;
	TFunction<TArray<FAIExportHttpMcpToolDescriptor>()> GetToolDescriptors;
	TFunction<bool()> IsStopRequested;
	TFunction<int32(int32, int32)> FindAvailablePort;
};

class FAIExportHttpMcpServer
{
public:
	void Start(const FAIExportHttpMcpCallbacks& InCallbacks);
	void Stop();

	FAIExportHttpMcpStatus GetStatus() const;
	bool IsRunning() const { return bHttpServerRunning; }
	int32 GetPort() const { return HttpPort; }

private:
	struct FMcpHttpSession
	{
		FString SessionId;
		FString ClientName;
		FString CreatedAtUtc;
		FDateTime LastSeenUtc;
	};

	const TArray<FString>* FindHttpHeaderValues(const FHttpServerRequest& Request, const FString& HeaderName) const;
	FString GetHttpHeaderValue(const FHttpServerRequest& Request, const FString& HeaderName) const;
	bool IsHttpOriginAllowed(const FString& Origin) const;
	bool IsHttpRequestAllowed(const FHttpServerRequest& Request) const;
	bool IsMcpProtocolVersionAllowed(const FHttpServerRequest& Request, FString& OutError) const;
	void PruneExpiredMcpSessionsLocked(const FDateTime& NowUtc);
	bool DeleteMcpSession(const FString& SessionId);
	void AppendHttpAuditEvent(const FHttpServerRequest& Request, const FString& Route, int32 ResponseCode, bool bAllowed, const FString& Detail, const FString& McpSessionId = FString()) const;
	TUniquePtr<FHttpServerResponse> MakeHttpJsonResponse(const FString& Json, int32 ResponseCode = 200, const FString& McpSessionId = FString()) const;
	TUniquePtr<FHttpServerResponse> MakeHttpTextResponse(const FString& Text, const FString& ContentType, int32 ResponseCode = 200, const FString& McpSessionId = FString()) const;
	FString HandleMcpJsonRpc(const FString& RequestJson, const FString& RequestSessionId, FString& OutSessionId);

	FAIExportHttpMcpCallbacks Callbacks;

	int32 HttpPort = 0;
	TSharedPtr<IHttpRouter> HttpRouter;
	TArray<FHttpRouteHandle> HttpRouteHandles;
	bool bHttpServerRunning = false;
	FString HttpAuthToken;
	TArray<FString> HttpAllowedOrigins;
	FString HttpAllowedOriginsConfig;
	FString HttpAuditLogPath;
	bool bHttpAuditEnabled = true;
	int32 McpSessionTtlSeconds = 3600;
	TMap<FString, FMcpHttpSession> ActiveMcpSessions;
	mutable FCriticalSection ActiveMcpSessionsCriticalSection;
	mutable FCriticalSection HttpAuditCriticalSection;
	mutable FThreadSafeCounter HttpRequestCount;
	mutable FThreadSafeCounter HttpRejectedRequestCount;
	mutable FThreadSafeCounter McpRequestCount;
	mutable FThreadSafeCounter McpRejectedRequestCount;
	mutable FThreadSafeCounter McpSessionExpiredCount;
};
}
