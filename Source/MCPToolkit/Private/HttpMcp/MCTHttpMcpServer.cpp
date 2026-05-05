// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "HttpMcp/MCTHttpMcpServer.h"

#include "MCTModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include "Serialization/JsonWriter.h"

namespace MCPToolkit::HttpMcp
{
namespace
{
FString HttpVerbToString(EHttpServerRequestVerbs Verb)
{
	if (Verb == EHttpServerRequestVerbs::VERB_GET) return TEXT("GET");
	if (Verb == EHttpServerRequestVerbs::VERB_POST) return TEXT("POST");
	if (Verb == EHttpServerRequestVerbs::VERB_DELETE) return TEXT("DELETE");
	if (Verb == EHttpServerRequestVerbs::VERB_OPTIONS) return TEXT("OPTIONS");
	if (Verb == EHttpServerRequestVerbs::VERB_PUT) return TEXT("PUT");
	if (Verb == EHttpServerRequestVerbs::VERB_PATCH) return TEXT("PATCH");
	return TEXT("UNKNOWN");
}

FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Object)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	return Output;
}

FString MakeRpcResponse(const TSharedPtr<FJsonValue>& IdValue, const TSharedPtr<FJsonObject>& Result)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Response->SetField(TEXT("id"), IdValue.IsValid() ? IdValue : MakeShared<FJsonValueNull>());
	Response->SetObjectField(TEXT("result"), Result);
	return SerializeJsonObject(Response);
}

FString MakeRpcError(const TSharedPtr<FJsonValue>& IdValue, int32 Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetNumberField(TEXT("code"), Code);
	Error->SetStringField(TEXT("message"), Message);

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Response->SetField(TEXT("id"), IdValue.IsValid() ? IdValue : MakeShared<FJsonValueNull>());
	Response->SetObjectField(TEXT("error"), Error);
	return SerializeJsonObject(Response);
}
}

FMCTHttpMcpStatus FMCTHttpMcpServer::GetStatus() const
{
	FMCTHttpMcpStatus Status;
	Status.Port = HttpPort;
	Status.bRunning = bHttpServerRunning;
	Status.bAuthRequired = !HttpAuthToken.IsEmpty();
	Status.bAuditEnabled = bHttpAuditEnabled;
	Status.SessionTtlSeconds = McpSessionTtlSeconds;
	Status.AuditLogPath = HttpAuditLogPath;
	Status.AllowedOrigins = HttpAllowedOrigins;
	Status.HttpRequestCount = HttpRequestCount.GetValue();
	Status.HttpRejectedRequestCount = HttpRejectedRequestCount.GetValue();
	Status.McpRequestCount = McpRequestCount.GetValue();
	Status.McpRejectedRequestCount = McpRejectedRequestCount.GetValue();
	Status.McpSessionExpiredCount = McpSessionExpiredCount.GetValue();

	{
		FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
		Status.ActiveSessionCount = ActiveMcpSessions.Num();
	}

	return Status;
}

const TArray<FString>* FMCTHttpMcpServer::FindHttpHeaderValues(const FHttpServerRequest& Request, const FString& HeaderName) const
{
	if (const TArray<FString>* DirectValues = Request.Headers.Find(HeaderName))
	{
		return DirectValues;
	}

	for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
	{
		if (Pair.Key.Equals(HeaderName, ESearchCase::IgnoreCase))
		{
			return &Pair.Value;
		}
	}

	return nullptr;
}

FString FMCTHttpMcpServer::GetHttpHeaderValue(const FHttpServerRequest& Request, const FString& HeaderName) const
{
	if (const TArray<FString>* Values = FindHttpHeaderValues(Request, HeaderName))
	{
		if (Values->Num() > 0)
		{
			FString Value = (*Values)[0];
			Value.TrimStartAndEndInline();
			return Value;
		}
	}

	return FString();
}

bool FMCTHttpMcpServer::IsHttpOriginAllowed(const FString& Origin) const
{
	if (Origin.IsEmpty())
	{
		return true;
	}

	for (const FString& AllowedOrigin : HttpAllowedOrigins)
	{
		if (AllowedOrigin == TEXT("*"))
		{
			return true;
		}
		if (Origin.Equals(AllowedOrigin, ESearchCase::IgnoreCase) || Origin.StartsWith(AllowedOrigin, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool FMCTHttpMcpServer::IsHttpRequestAllowed(const FHttpServerRequest& Request) const
{
	if (Request.PeerAddress.IsValid())
	{
		const FString PeerIp = Request.PeerAddress->ToString(false);
		if (PeerIp != TEXT("127.0.0.1") && PeerIp != TEXT("::1") && PeerIp != TEXT("0:0:0:0:0:0:0:1"))
		{
			return false;
		}
	}

	const TArray<FString>* OriginValues = FindHttpHeaderValues(Request, TEXT("Origin"));
	if (OriginValues)
	{
		for (const FString& Origin : *OriginValues)
		{
			if (!IsHttpOriginAllowed(Origin))
			{
				return false;
			}
		}
	}

	if (!HttpAuthToken.IsEmpty())
	{
		bool bAuthorized = false;
		const TArray<FString>* AuthorizationValues = FindHttpHeaderValues(Request, TEXT("Authorization"));
		if (AuthorizationValues)
		{
			const FString ExpectedBearer = FString::Printf(TEXT("Bearer %s"), *HttpAuthToken);
			for (FString Authorization : *AuthorizationValues)
			{
				Authorization.TrimStartAndEndInline();
				if (Authorization == ExpectedBearer || Authorization == HttpAuthToken)
				{
					bAuthorized = true;
					break;
				}
			}
		}

		if (!bAuthorized)
		{
			return false;
		}
	}

	return true;
}

bool FMCTHttpMcpServer::IsMcpProtocolVersionAllowed(const FHttpServerRequest& Request, FString& OutError) const
{
	const TArray<FString>* ProtocolVersions = FindHttpHeaderValues(Request, TEXT("MCP-Protocol-Version"));
	if (!ProtocolVersions)
	{
		return true;
	}

	for (FString ProtocolVersion : *ProtocolVersions)
	{
		ProtocolVersion.TrimStartAndEndInline();
		if (ProtocolVersion.IsEmpty() || ProtocolVersion == TEXT("2025-06-18"))
		{
			return true;
		}
	}

	OutError = TEXT("Unsupported MCP protocol version. Supported version: 2025-06-18");
	return false;
}

void FMCTHttpMcpServer::PruneExpiredMcpSessionsLocked(const FDateTime& NowUtc)
{
	TArray<FString> ExpiredSessionIds;
	for (const TPair<FString, FMcpHttpSession>& Pair : ActiveMcpSessions)
	{
		const FTimespan IdleTime = NowUtc - Pair.Value.LastSeenUtc;
		if (IdleTime.GetTotalSeconds() > McpSessionTtlSeconds)
		{
			ExpiredSessionIds.Add(Pair.Key);
		}
	}

	for (const FString& SessionId : ExpiredSessionIds)
	{
		ActiveMcpSessions.Remove(SessionId);
		McpSessionExpiredCount.Increment();
	}
}

bool FMCTHttpMcpServer::DeleteMcpSession(const FString& SessionId)
{
	FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
	PruneExpiredMcpSessionsLocked(FDateTime::UtcNow());
	return ActiveMcpSessions.Remove(SessionId) > 0;
}

void FMCTHttpMcpServer::AppendHttpAuditEvent(const FHttpServerRequest& Request, const FString& Route, int32 ResponseCode, bool bAllowed, const FString& Detail, const FString& McpSessionId) const
{
	if (!bHttpAuditEnabled || HttpAuditLogPath.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
	Event->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Event->SetStringField(TEXT("route"), Route);
	Event->SetStringField(TEXT("verb"), HttpVerbToString(Request.Verb));
	Event->SetNumberField(TEXT("response_code"), ResponseCode);
	Event->SetBoolField(TEXT("allowed"), bAllowed);
	Event->SetStringField(TEXT("detail"), Detail);
	if (!McpSessionId.IsEmpty())
	{
		Event->SetStringField(TEXT("mcp_session_id"), McpSessionId);
	}
	if (Request.PeerAddress.IsValid())
	{
		Event->SetStringField(TEXT("peer"), Request.PeerAddress->ToString(false));
	}
	const FString Origin = GetHttpHeaderValue(Request, TEXT("Origin"));
	if (!Origin.IsEmpty())
	{
		Event->SetStringField(TEXT("origin"), Origin);
	}
	const FString ProtocolVersion = GetHttpHeaderValue(Request, TEXT("MCP-Protocol-Version"));
	if (!ProtocolVersion.IsEmpty())
	{
		Event->SetStringField(TEXT("mcp_protocol_version"), ProtocolVersion);
	}

	FString Line;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Event.ToSharedRef(), Writer);
	Line += LINE_TERMINATOR;

	FScopeLock Lock(&HttpAuditCriticalSection);
	FFileHelper::SaveStringToFile(Line, *HttpAuditLogPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

TUniquePtr<FHttpServerResponse> FMCTHttpMcpServer::MakeHttpJsonResponse(const FString& Json, int32 ResponseCode, const FString& McpSessionId) const
{
	return MakeHttpTextResponse(Json, TEXT("application/json"), ResponseCode, McpSessionId);
}

TUniquePtr<FHttpServerResponse> FMCTHttpMcpServer::MakeHttpTextResponse(const FString& Text, const FString& ContentType, int32 ResponseCode, const FString& McpSessionId) const
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Text, ContentType);
	Response->Code = static_cast<EHttpServerResponseCodes>(ResponseCode);
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("http://localhost") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Accept, MCP-Protocol-Version, Authorization, Mcp-Session-Id") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, DELETE, OPTIONS") });
	Response->Headers.Add(TEXT("MCP-Protocol-Version"), { TEXT("2025-06-18") });
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-store") });
	Response->Headers.Add(TEXT("X-Content-Type-Options"), { TEXT("nosniff") });
	if (ContentType.StartsWith(TEXT("text/event-stream")))
	{
		Response->Headers.Add(TEXT("X-Accel-Buffering"), { TEXT("no") });
	}
	if (!McpSessionId.IsEmpty())
	{
		Response->Headers.Add(TEXT("Mcp-Session-Id"), { McpSessionId });
	}
	return Response;
}

void FMCTHttpMcpServer::Start(const FMCTHttpMcpCallbacks& InCallbacks)
{
	if (bHttpServerRunning)
	{
		return;
	}

	Callbacks = InCallbacks;

	HttpAuthToken = FPlatformMisc::GetEnvironmentVariable(TEXT("MCPTOOLKIT_HTTP_TOKEN"));
	HttpAuthToken.TrimStartAndEndInline();
	if (HttpAuthToken.IsEmpty())
	{
		HttpAuthToken = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_HTTP_TOKEN"));
		HttpAuthToken.TrimStartAndEndInline();
	}
	if (HttpAuthToken.IsEmpty())
	{
		HttpAuthToken = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAIEXPORT_HTTP_TOKEN"));
		HttpAuthToken.TrimStartAndEndInline();
	}

	HttpAllowedOrigins.Reset();
	HttpAllowedOriginsConfig = FPlatformMisc::GetEnvironmentVariable(TEXT("MCPTOOLKIT_HTTP_ALLOWED_ORIGINS"));
	HttpAllowedOriginsConfig.TrimStartAndEndInline();
	if (HttpAllowedOriginsConfig.IsEmpty())
	{
		HttpAllowedOriginsConfig = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_HTTP_ALLOWED_ORIGINS"));
		HttpAllowedOriginsConfig.TrimStartAndEndInline();
	}
	if (HttpAllowedOriginsConfig.IsEmpty())
	{
		HttpAllowedOriginsConfig = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAIEXPORT_HTTP_ALLOWED_ORIGINS"));
		HttpAllowedOriginsConfig.TrimStartAndEndInline();
	}
	if (!HttpAllowedOriginsConfig.IsEmpty())
	{
		TArray<FString> ConfiguredOrigins;
		HttpAllowedOriginsConfig.ParseIntoArray(ConfiguredOrigins, TEXT(","), true);
		for (FString Origin : ConfiguredOrigins)
		{
			Origin.TrimStartAndEndInline();
			if (!Origin.IsEmpty())
			{
				HttpAllowedOrigins.Add(Origin);
			}
		}
	}
	if (HttpAllowedOrigins.Num() == 0)
	{
		HttpAllowedOrigins.Add(TEXT("http://localhost"));
		HttpAllowedOrigins.Add(TEXT("http://127.0.0.1"));
		HttpAllowedOrigins.Add(TEXT("http://[::1]"));
	}

	FString SessionTtl = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_SESSION_TTL_SECONDS"));
	SessionTtl.TrimStartAndEndInline();
	if (!SessionTtl.IsEmpty())
	{
		const int32 ParsedTtl = FCString::Atoi(*SessionTtl);
		if (ParsedTtl >= 60)
		{
			McpSessionTtlSeconds = ParsedTtl;
		}
	}

	FString AuditEnabled = FPlatformMisc::GetEnvironmentVariable(TEXT("MCPTOOLKIT_HTTP_AUDIT"));
	AuditEnabled.TrimStartAndEndInline();
	if (AuditEnabled.IsEmpty())
	{
		AuditEnabled = FPlatformMisc::GetEnvironmentVariable(TEXT("COMMONAI_MCP_HTTP_AUDIT"));
		AuditEnabled.TrimStartAndEndInline();
	}
	bHttpAuditEnabled = !AuditEnabled.Equals(TEXT("0"), ESearchCase::IgnoreCase)
		&& !AuditEnabled.Equals(TEXT("false"), ESearchCase::IgnoreCase)
		&& !AuditEnabled.Equals(TEXT("off"), ESearchCase::IgnoreCase);
	HttpAuditLogPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("MCPToolkit_HTTP_Audit.jsonl")));

	HttpRequestCount.Reset();
	HttpRejectedRequestCount.Reset();
	McpRequestCount.Reset();
	McpRejectedRequestCount.Reset();
	McpSessionExpiredCount.Reset();

	HttpPort = Callbacks.FindAvailablePort ? Callbacks.FindAvailablePort(55610, 55650) : 0;
	HttpRouter = FHttpServerModule::Get().GetHttpRouter(static_cast<uint32>(HttpPort), true);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogMCT, Warning, TEXT("Could not create HTTP router on port %d"), HttpPort);
		HttpPort = 0;
		return;
	}

	auto BindRoute = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, TFunction<FString(const FHttpServerRequest&)> Handler)
	{
		const FString Route(Path);
		FHttpRouteHandle Handle = HttpRouter->BindRoute(
			FHttpPath(Path),
			Verbs,
			FHttpRequestHandler::CreateLambda([this, Handler, Route](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				HttpRequestCount.Increment();
				if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
				{
					AppendHttpAuditEvent(Request, Route, 200, true, TEXT("preflight"));
					OnComplete(MakeHttpJsonResponse(TEXT("{}")));
					return true;
				}
				if (!IsHttpRequestAllowed(Request))
				{
					HttpRejectedRequestCount.Increment();
					UE_LOG(LogMCT, Warning, TEXT("Rejected MCPToolkit HTTP request for non-MCP route"));
					AppendHttpAuditEvent(Request, Route, 403, false, TEXT("forbidden"));
					OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Forbidden origin, peer address, or authorization\"}"), 403));
					return true;
				}
				const FString ResponseJson = Handler(Request);
				AppendHttpAuditEvent(Request, Route, 200, true, TEXT("ok"));
				OnComplete(MakeHttpJsonResponse(ResponseJson));
				return true;
			}));
		if (Handle.IsValid())
		{
			HttpRouteHandles.Add(Handle);
		}
	};

	auto BindTextRoute = [this](const TCHAR* Path, EHttpServerRequestVerbs Verbs, const FString& ContentType, TFunction<FString(const FHttpServerRequest&)> Handler)
	{
		const FString Route(Path);
		FHttpRouteHandle Handle = HttpRouter->BindRoute(
			FHttpPath(Path),
			Verbs,
			FHttpRequestHandler::CreateLambda([this, Handler, Route, ContentType](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
			{
				HttpRequestCount.Increment();
				if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
				{
					AppendHttpAuditEvent(Request, Route, 200, true, TEXT("preflight"));
					OnComplete(MakeHttpTextResponse(TEXT(""), ContentType));
					return true;
				}
				if (!IsHttpRequestAllowed(Request))
				{
					HttpRejectedRequestCount.Increment();
					UE_LOG(LogMCT, Warning, TEXT("Rejected MCPToolkit HTTP text request for non-MCP route"));
					AppendHttpAuditEvent(Request, Route, 403, false, TEXT("forbidden"));
					OnComplete(MakeHttpTextResponse(TEXT("Forbidden origin, peer address, or authorization"), TEXT("text/plain"), 403));
					return true;
				}
				const FString ResponseText = Handler(Request);
				AppendHttpAuditEvent(Request, Route, 200, true, TEXT("ok"));
				OnComplete(MakeHttpTextResponse(ResponseText, ContentType));
				return true;
			}));
		if (Handle.IsValid())
		{
			HttpRouteHandles.Add(Handle);
		}
	};

	BindRoute(TEXT("/commonai/health"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest&)
	{
		return Callbacks.HandlePing ? Callbacks.HandlePing() : TEXT("{\"success\":false,\"error\":\"HTTP MCP callbacks not configured\"}");
	});
	BindRoute(TEXT("/commonai/commands"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest&)
	{
		return Callbacks.HandleListCommands ? Callbacks.HandleListCommands() : TEXT("{\"success\":false,\"error\":\"HTTP MCP callbacks not configured\"}");
	});
	BindRoute(TEXT("/commonai/command"), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest& Request)
	{
		FUTF8ToTCHAR BodyText(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
		return Callbacks.ProcessCommand ? Callbacks.ProcessCommand(FString(BodyText.Length(), BodyText.Get())) : TEXT("{\"success\":false,\"error\":\"HTTP MCP callbacks not configured\"}");
	});
	BindRoute(TEXT("/commonai/tasks/events"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest& Request)
	{
		TSharedPtr<FJsonObject> Params = Callbacks.BuildTaskEventParamsFromHttpRequest ? Callbacks.BuildTaskEventParamsFromHttpRequest(Request) : MakeShared<FJsonObject>();
		return Callbacks.HandleTaskEvents ? Callbacks.HandleTaskEvents(Params) : TEXT("{\"success\":false,\"error\":\"HTTP MCP callbacks not configured\"}");
	});
	BindRoute(TEXT("/commonai/tasks/events/wait"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, [this](const FHttpServerRequest& Request)
	{
		TSharedPtr<FJsonObject> Params = Callbacks.BuildTaskEventParamsFromHttpRequest ? Callbacks.BuildTaskEventParamsFromHttpRequest(Request) : MakeShared<FJsonObject>();
		return Callbacks.HandleTaskEventsWait ? Callbacks.HandleTaskEventsWait(Params) : TEXT("{\"success\":false,\"error\":\"HTTP MCP callbacks not configured\"}");
	});
	BindTextRoute(TEXT("/commonai/tasks/events/sse"), EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_OPTIONS, TEXT("text/event-stream"), [this](const FHttpServerRequest& Request)
	{
		TSharedPtr<FJsonObject> Params = Callbacks.BuildTaskEventParamsFromHttpRequest ? Callbacks.BuildTaskEventParamsFromHttpRequest(Request) : MakeShared<FJsonObject>();
		return Callbacks.BuildTaskEventsSse ? Callbacks.BuildTaskEventsSse(Params) : TEXT("");
	});

	FHttpRouteHandle McpRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_DELETE | EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			HttpRequestCount.Increment();
			if (Request.Verb == EHttpServerRequestVerbs::VERB_OPTIONS)
			{
				AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, true, TEXT("preflight"));
				OnComplete(MakeHttpJsonResponse(TEXT("{}")));
				return true;
			}
			if (!IsHttpRequestAllowed(Request))
			{
				HttpRejectedRequestCount.Increment();
				McpRejectedRequestCount.Increment();
				UE_LOG(LogMCT, Warning, TEXT("Rejected MCPToolkit MCP HTTP request"));
				AppendHttpAuditEvent(Request, TEXT("/mcp"), 403, false, TEXT("forbidden"));
				OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Forbidden origin, peer address, or authorization\"}"), 403));
				return true;
			}

			FString ProtocolError;
			if (!IsMcpProtocolVersionAllowed(Request, ProtocolError))
			{
				HttpRejectedRequestCount.Increment();
				McpRejectedRequestCount.Increment();
				const FString SafeProtocolError = ProtocolError.Replace(TEXT("\""), TEXT("\\\""));
				const FString ErrorJson = FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32002,\"message\":\"%s\"}}"), *SafeProtocolError);
				AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, false, TEXT("unsupported_protocol"));
				OnComplete(MakeHttpJsonResponse(ErrorJson));
				return true;
			}

			McpRequestCount.Increment();
			const FString RequestSessionId = GetHttpHeaderValue(Request, TEXT("Mcp-Session-Id"));

			if (Request.Verb == EHttpServerRequestVerbs::VERB_DELETE)
			{
				if (RequestSessionId.IsEmpty())
				{
					McpRejectedRequestCount.Increment();
					AppendHttpAuditEvent(Request, TEXT("/mcp"), 400, false, TEXT("missing_session_for_delete"));
					OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Mcp-Session-Id header is required to delete a session\"}"), 400));
					return true;
				}

				if (!DeleteMcpSession(RequestSessionId))
				{
					McpRejectedRequestCount.Increment();
					AppendHttpAuditEvent(Request, TEXT("/mcp"), 404, false, TEXT("unknown_session_delete"), RequestSessionId);
					OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":false,\"error\":\"Unknown or expired MCP session id\"}"), 404));
					return true;
				}

				AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, true, TEXT("session_deleted"), RequestSessionId);
				OnComplete(MakeHttpJsonResponse(TEXT("{\"success\":true,\"message\":\"MCP session deleted\"}")));
				return true;
			}

			FUTF8ToTCHAR BodyText(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
			FString ResponseSessionId;
			const FString ResponseJson = HandleMcpJsonRpc(FString(BodyText.Length(), BodyText.Get()), RequestSessionId, ResponseSessionId);
			AppendHttpAuditEvent(Request, TEXT("/mcp"), 200, !ResponseJson.Contains(TEXT("\"error\"")), ResponseJson.Contains(TEXT("\"error\"")) ? TEXT("jsonrpc_error") : TEXT("ok"), ResponseSessionId.IsEmpty() ? RequestSessionId : ResponseSessionId);
			OnComplete(MakeHttpJsonResponse(ResponseJson, 200, ResponseSessionId));
			return true;
		}));
	if (McpRouteHandle.IsValid())
	{
		HttpRouteHandles.Add(McpRouteHandle);
	}

	FHttpServerModule::Get().StartAllListeners();
	bHttpServerRunning = HttpRouteHandles.Num() > 0;

	if (bHttpServerRunning)
	{
		const FString HttpPortPath = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("MCTExport_http_port.txt"));
		FFileHelper::SaveStringToFile(FString::FromInt(HttpPort), *HttpPortPath);
		UE_LOG(LogMCT, Log, TEXT("MCPToolkit HTTP/MCP routes listening on http://127.0.0.1:%d/mcp"), HttpPort);
	}
}

void FMCTHttpMcpServer::Stop()
{
	if (HttpRouter.IsValid())
	{
		for (const FHttpRouteHandle& Handle : HttpRouteHandles)
		{
			if (Handle.IsValid())
			{
				HttpRouter->UnbindRoute(Handle);
			}
		}
	}

	HttpRouteHandles.Reset();
	HttpRouter.Reset();
	bHttpServerRunning = false;
	HttpAuthToken.Reset();
	HttpAllowedOrigins.Reset();
	HttpAllowedOriginsConfig.Reset();
	HttpAuditLogPath.Reset();
	bHttpAuditEnabled = true;
	Callbacks = FMCTHttpMcpCallbacks();
	{
		FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
		ActiveMcpSessions.Reset();
	}
}

FString FMCTHttpMcpServer::HandleMcpJsonRpc(const FString& RequestJson, const FString& RequestSessionId, FString& OutSessionId)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return MakeRpcError(nullptr, -32700, TEXT("Parse error"));
	}

	TSharedPtr<FJsonValue> IdValue = Root->TryGetField(TEXT("id"));
	FString Method;
	if (!Root->TryGetStringField(TEXT("method"), Method))
	{
		return MakeRpcError(IdValue, -32600, TEXT("Invalid Request: missing method"));
	}

	const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
	Root->TryGetObjectField(TEXT("params"), ParamsObj);

	if (!RequestSessionId.IsEmpty() && Method != TEXT("initialize"))
	{
		bool bKnownSession = false;
		{
			FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
			const FDateTime NowUtc = FDateTime::UtcNow();
			PruneExpiredMcpSessionsLocked(NowUtc);
			if (FMcpHttpSession* Session = ActiveMcpSessions.Find(RequestSessionId))
			{
				Session->LastSeenUtc = NowUtc;
				bKnownSession = true;
			}
		}

		if (!bKnownSession)
		{
			McpRejectedRequestCount.Increment();
			return MakeRpcError(IdValue, -32001, TEXT("Unknown or expired MCP session id"));
		}
		OutSessionId = RequestSessionId;
	}

	if (Method == TEXT("initialize"))
	{
		const FString NewSessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		FString ClientName = TEXT("unknown");
		if (ParamsObj && ParamsObj->IsValid())
		{
			const TSharedPtr<FJsonObject>* ClientInfo = nullptr;
			if ((*ParamsObj)->TryGetObjectField(TEXT("clientInfo"), ClientInfo) && ClientInfo && ClientInfo->IsValid())
			{
				(*ClientInfo)->TryGetStringField(TEXT("name"), ClientName);
			}
		}
		{
			FScopeLock Lock(&ActiveMcpSessionsCriticalSection);
			const FDateTime NowUtc = FDateTime::UtcNow();
			PruneExpiredMcpSessionsLocked(NowUtc);

			FMcpHttpSession Session;
			Session.SessionId = NewSessionId;
			Session.ClientName = ClientName;
			Session.CreatedAtUtc = NowUtc.ToIso8601();
			Session.LastSeenUtc = NowUtc;
			ActiveMcpSessions.Add(NewSessionId, Session);
		}
		OutSessionId = NewSessionId;

		TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
		Capabilities->SetObjectField(TEXT("tools"), MakeShared<FJsonObject>());
		Capabilities->SetObjectField(TEXT("resources"), MakeShared<FJsonObject>());
		Capabilities->SetObjectField(TEXT("prompts"), MakeShared<FJsonObject>());
		TSharedPtr<FJsonObject> Experimental = MakeShared<FJsonObject>();
		Experimental->SetBoolField(TEXT("sessions"), true);
		Experimental->SetBoolField(TEXT("pagination"), true);
		Capabilities->SetObjectField(TEXT("experimental"), Experimental);

		TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
		ServerInfo->SetStringField(TEXT("name"), TEXT("MCPToolkit"));
		ServerInfo->SetStringField(TEXT("version"), TEXT("0.3.0"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("protocolVersion"), TEXT("2025-06-18"));
		Result->SetObjectField(TEXT("capabilities"), Capabilities);
		Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
		Result->SetStringField(TEXT("sessionId"), NewSessionId);
		Result->SetNumberField(TEXT("sessionTtlSeconds"), McpSessionTtlSeconds);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("notifications/initialized"))
	{
		return MakeRpcResponse(IdValue, MakeShared<FJsonObject>());
	}

	if (Method == TEXT("tools/list"))
	{
		int32 StartIndex = 0;
		if (ParamsObj && ParamsObj->IsValid())
		{
			FString Cursor;
			if ((*ParamsObj)->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.IsEmpty())
			{
				if (Cursor.StartsWith(TEXT("offset:")))
				{
					Cursor = Cursor.Mid(7);
				}
				StartIndex = FMath::Max(0, FCString::Atoi(*Cursor));
			}
		}

		const int32 PageSize = 50;
		const TArray<FMCTHttpMcpToolDescriptor> Descriptors = Callbacks.GetToolDescriptors ? Callbacks.GetToolDescriptors() : TArray<FMCTHttpMcpToolDescriptor>();
		const int32 EndIndex = FMath::Min(StartIndex + PageSize, Descriptors.Num());

		TArray<TSharedPtr<FJsonValue>> Tools;
		for (int32 Index = StartIndex; Index < EndIndex; ++Index)
		{
			const FMCTHttpMcpToolDescriptor& Descriptor = Descriptors[Index];
			TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetBoolField(TEXT("additionalProperties"), true);

			TSharedPtr<FJsonObject> Annotations = MakeShared<FJsonObject>();
			Annotations->SetBoolField(TEXT("readOnlyHint"), !Descriptor.bMutating);
			Annotations->SetBoolField(TEXT("destructiveHint"), Descriptor.RequiredScope == TEXT("destructive"));
			Annotations->SetBoolField(TEXT("idempotentHint"), !Descriptor.bMutating);

			TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
			Tool->SetStringField(TEXT("name"), Descriptor.Name);
			Tool->SetStringField(TEXT("description"), FString::Printf(TEXT("MCPToolkit %s command. category=%s scope=%s dry_run=%s"), *Descriptor.Name, *Descriptor.Category, *Descriptor.RequiredScope, Descriptor.bSupportsDryRun ? TEXT("true") : TEXT("false")));
			Tool->SetObjectField(TEXT("inputSchema"), Schema);
			Tool->SetObjectField(TEXT("annotations"), Annotations);
			Tools.Add(MakeShared<FJsonValueObject>(Tool));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("tools"), Tools);
		if (EndIndex < Descriptors.Num())
		{
			Result->SetStringField(TEXT("nextCursor"), FString::Printf(TEXT("offset:%d"), EndIndex));
		}
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("tools/call"))
	{
		if (!ParamsObj || !ParamsObj->IsValid())
		{
			return MakeRpcError(IdValue, -32602, TEXT("tools/call requires params"));
		}

		FString Name;
		if (!(*ParamsObj)->TryGetStringField(TEXT("name"), Name))
		{
			return MakeRpcError(IdValue, -32602, TEXT("tools/call requires params.name"));
		}

		const TSharedPtr<FJsonObject>* ArgumentsObj = nullptr;
		(*ParamsObj)->TryGetObjectField(TEXT("arguments"), ArgumentsObj);

		TSharedPtr<FJsonObject> Command = MakeShared<FJsonObject>();
		Command->SetStringField(TEXT("type"), Name);
		if (ArgumentsObj && ArgumentsObj->IsValid())
		{
			Command->SetObjectField(TEXT("params"), *ArgumentsObj);

			TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
			FString Scope;
			bool bDryRun = false;
			if ((*ArgumentsObj)->TryGetStringField(TEXT("scope"), Scope) && !Scope.IsEmpty())
			{
				Meta->SetStringField(TEXT("scope"), Scope);
			}
			if ((*ArgumentsObj)->TryGetBoolField(TEXT("dry_run"), bDryRun) && bDryRun)
			{
				Meta->SetBoolField(TEXT("dry_run"), true);
			}
			if (Meta->Values.Num() > 0)
			{
				Command->SetObjectField(TEXT("meta"), Meta);
			}
		}

		const FString CommandJson = SerializeJsonObject(Command);
		const FString RawResponse = Callbacks.ProcessCommand ? Callbacks.ProcessCommand(CommandJson) : TEXT("{\"success\":false,\"error\":\"HTTP MCP callbacks not configured\"}");

		bool bSuccess = false;
		TSharedPtr<FJsonObject> ParsedResponse;
		TSharedRef<TJsonReader<>> ResponseReader = TJsonReaderFactory<>::Create(RawResponse);
		if (FJsonSerializer::Deserialize(ResponseReader, ParsedResponse) && ParsedResponse.IsValid())
		{
			ParsedResponse->TryGetBoolField(TEXT("success"), bSuccess);
		}

		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), RawResponse);

		TArray<TSharedPtr<FJsonValue>> Content;
		Content.Add(MakeShared<FJsonValueObject>(TextContent));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("content"), Content);
		Result->SetBoolField(TEXT("isError"), !bSuccess);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("resources/list"))
	{
		TArray<TSharedPtr<FJsonValue>> Resources;
		auto AddResource = [&Resources](const TCHAR* Uri, const TCHAR* Name, const TCHAR* Description)
		{
			TSharedPtr<FJsonObject> Resource = MakeShared<FJsonObject>();
			Resource->SetStringField(TEXT("uri"), Uri);
			Resource->SetStringField(TEXT("name"), Name);
			Resource->SetStringField(TEXT("description"), Description);
			Resource->SetStringField(TEXT("mimeType"), TEXT("application/json"));
			Resources.Add(MakeShared<FJsonValueObject>(Resource));
		};
		AddResource(TEXT("commonai://project/status"), TEXT("Project Status"), TEXT("Current MCPToolkit project/editor status"));
		AddResource(TEXT("commonai://commands/manifest"), TEXT("Command Manifest"), TEXT("Command descriptor manifest"));
		AddResource(TEXT("commonai://editor/status"), TEXT("Editor Identity"), TEXT("Current editor identity and capabilities"));
		AddResource(TEXT("commonai://logs/latest"), TEXT("Latest Log"), TEXT("Recent project log lines"));
		AddResource(TEXT("commonai://audit/http"), TEXT("HTTP MCP Audit"), TEXT("Recent MCPToolkit native HTTP/MCP audit JSONL events"));
		AddResource(TEXT("commonai://tasks/events"), TEXT("Async Task Events"), TEXT("Recent MCPToolkit async task lifecycle events"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("resources"), Resources);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("resources/read"))
	{
		if (!ParamsObj || !ParamsObj->IsValid())
		{
			return MakeRpcError(IdValue, -32602, TEXT("resources/read requires params"));
		}

		FString Uri;
		if (!(*ParamsObj)->TryGetStringField(TEXT("uri"), Uri))
		{
			return MakeRpcError(IdValue, -32602, TEXT("resources/read requires params.uri"));
		}

		FString Text;
		if (Uri == TEXT("commonai://project/status")) Text = Callbacks.HandleProjectStatus ? Callbacks.HandleProjectStatus() : TEXT("{}");
		else if (Uri == TEXT("commonai://commands/manifest")) Text = Callbacks.HandleListCommands ? Callbacks.HandleListCommands() : TEXT("{}");
		else if (Uri == TEXT("commonai://editor/status")) Text = Callbacks.HandleEditorIdentity ? Callbacks.HandleEditorIdentity() : TEXT("{}");
		else if (Uri == TEXT("commonai://logs/latest"))
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetNumberField(TEXT("max_lines"), 200);
			Text = Callbacks.HandleEditorLogRead ? Callbacks.HandleEditorLogRead(Params) : TEXT("{}");
		}
		else if (Uri == TEXT("commonai://audit/http"))
		{
			TArray<FString> Lines;
			if (!HttpAuditLogPath.IsEmpty() && FPaths::FileExists(HttpAuditLogPath))
			{
				FFileHelper::LoadFileToStringArray(Lines, *HttpAuditLogPath);
			}

			TArray<TSharedPtr<FJsonValue>> Events;
			const int32 StartIndex = FMath::Max(0, Lines.Num() - 200);
			for (int32 Index = StartIndex; Index < Lines.Num(); ++Index)
			{
				const FString& Line = Lines[Index];
				if (Line.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}

				TSharedPtr<FJsonObject> ParsedEvent;
				TSharedRef<TJsonReader<>> EventReader = TJsonReaderFactory<>::Create(Line);
				if (FJsonSerializer::Deserialize(EventReader, ParsedEvent) && ParsedEvent.IsValid())
				{
					Events.Add(MakeShared<FJsonValueObject>(ParsedEvent));
				}
				else
				{
					TSharedPtr<FJsonObject> ErrorEvent = MakeShared<FJsonObject>();
					ErrorEvent->SetBoolField(TEXT("parse_error"), true);
					ErrorEvent->SetStringField(TEXT("raw"), Line);
					Events.Add(MakeShared<FJsonValueObject>(ErrorEvent));
				}
			}

			TSharedPtr<FJsonObject> Audit = MakeShared<FJsonObject>();
			Audit->SetBoolField(TEXT("success"), true);
			Audit->SetStringField(TEXT("log_path"), HttpAuditLogPath);
			Audit->SetNumberField(TEXT("line_count"), Lines.Num());
			Audit->SetNumberField(TEXT("returned_count"), Events.Num());
			Audit->SetArrayField(TEXT("events"), Events);
			Text = SerializeJsonObject(Audit);
		}
		else if (Uri == TEXT("commonai://tasks/events"))
		{
			TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetNumberField(TEXT("limit"), 200);
			Text = Callbacks.HandleTaskEvents ? Callbacks.HandleTaskEvents(Params) : TEXT("{}");
		}
		else return MakeRpcError(IdValue, -32602, FString::Printf(TEXT("Unknown resource URI: %s"), *Uri));

		TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
		Content->SetStringField(TEXT("uri"), Uri);
		Content->SetStringField(TEXT("mimeType"), TEXT("application/json"));
		Content->SetStringField(TEXT("text"), Text);
		TArray<TSharedPtr<FJsonValue>> Contents;
		Contents.Add(MakeShared<FJsonValueObject>(Content));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("contents"), Contents);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("prompts/list"))
	{
		TArray<TSharedPtr<FJsonValue>> Prompts;
		auto AddPrompt = [&Prompts](const TCHAR* Name, const TCHAR* Description)
		{
			TSharedPtr<FJsonObject> Prompt = MakeShared<FJsonObject>();
			Prompt->SetStringField(TEXT("name"), Name);
			Prompt->SetStringField(TEXT("description"), Description);
			Prompts.Add(MakeShared<FJsonValueObject>(Prompt));
		};
		AddPrompt(TEXT("build_fix_test"), TEXT("Guarded project build/fix/test workflow"));
		AddPrompt(TEXT("asset_safety_review"), TEXT("Review an Unreal asset before copying, deleting, or mutating it"));
		AddPrompt(TEXT("multi_editor_transfer"), TEXT("Plan guarded transfer between open Unreal projects"));
		AddPrompt(TEXT("ui_transfer_validation"), TEXT("Validate UI transfer tasks before production Widget Blueprint mutation"));
		AddPrompt(TEXT("blueprint_graph_inspection"), TEXT("Inspect Blueprint graph structure before graph mutation"));
		AddPrompt(TEXT("runtime_debug_triage"), TEXT("Triage runtime/editor issues with status, logs, PIE, and audit context"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("prompts"), Prompts);
		return MakeRpcResponse(IdValue, Result);
	}

	if (Method == TEXT("prompts/get"))
	{
		if (!ParamsObj || !ParamsObj->IsValid())
		{
			return MakeRpcError(IdValue, -32602, TEXT("prompts/get requires params"));
		}
		FString Name;
		(*ParamsObj)->TryGetStringField(TEXT("name"), Name);
		FString PromptText;
		if (Name == TEXT("build_fix_test"))
		{
			PromptText = TEXT("Use the host project's guarded build workflow. Do not use Live Coding. If the editor must be relaunched, use the host project's documented launch/debug workflow. Check project_status, guarded build logs, and editor logs before and after fixes.");
		}
		else if (Name == TEXT("asset_safety_review"))
		{
			PromptText = TEXT("Before mutating an asset, run asset_validate_light, get_dependencies, get_referencers, and dry-run any destructive operation. Require explicit destructive scope for deletes/overwrites.");
		}
		else if (Name == TEXT("multi_editor_transfer"))
		{
			PromptText = TEXT("Use editors_list/editor registry, plan with asset_transfer_plan or code_transfer_plan, execute only after collision and scope review, then verify and run guarded build/status checks.");
		}
		else if (Name == TEXT("ui_transfer_validation"))
		{
			PromptText = TEXT("Before mutating production UI assets, read Docs/AI_UI_Transfer/README.md, Docs/AI_UI_Transfer/START_HERE.md, CommonUI architecture docs, and relevant component recipes. Ensure a TSpec exists and passes Resources/Scripts/ValidateUITSpecs.ps1. Probe uncertain components under /Game/UI/_AIProbe first.");
		}
		else if (Name == TEXT("blueprint_graph_inspection"))
		{
			PromptText = TEXT("Before editing Blueprint graphs, inspect with get_graph/list_graphs, identify existing events/functions/variables, and make narrowly scoped graph changes. Compile/save and inspect graph state after changes.");
		}
		else if (Name == TEXT("runtime_debug_triage"))
		{
			PromptText = TEXT("Start with project_status, server_status, pie_status, editor_log_read(filter='Error'), commonai://audit/http, and guarded_build_status. Reproduce in PIE only when needed; rerun smoke_mcp_runtime.py after fixes.");
		}
		else
		{
			return MakeRpcError(IdValue, -32602, FString::Printf(TEXT("Unknown prompt: %s"), *Name));
		}

		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), PromptText);
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), TEXT("user"));
		Message->SetObjectField(TEXT("content"), TextContent);
		TArray<TSharedPtr<FJsonValue>> Messages;
		Messages.Add(MakeShared<FJsonValueObject>(Message));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("description"), Name);
		Result->SetArrayField(TEXT("messages"), Messages);
		return MakeRpcResponse(IdValue, Result);
	}

	return MakeRpcError(IdValue, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
}
}
