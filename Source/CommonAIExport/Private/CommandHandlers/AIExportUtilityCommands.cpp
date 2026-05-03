// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportUtilityCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonTypes.h"
#include "Serialization/JsonWriter.h"

namespace CommonAIExport::CommandHandlers::Utility
{
namespace
{
void WriteCommandDescriptorJson(const FAIExportUtilityCommandDescriptor& Descriptor, TSharedPtr<FJsonObject> OutObject)
{
	if (!OutObject.IsValid())
	{
		return;
	}

	OutObject->SetStringField(TEXT("name"), Descriptor.Name);
	OutObject->SetStringField(TEXT("category"), Descriptor.Category);
	OutObject->SetBoolField(TEXT("requires_params"), Descriptor.bRequiresParams);
	OutObject->SetBoolField(TEXT("mutating"), Descriptor.bMutating);
	OutObject->SetNumberField(TEXT("timeout_seconds"), Descriptor.TimeoutSeconds);
	OutObject->SetStringField(TEXT("required_scope"), Descriptor.RequiredScope.IsEmpty() ? TEXT("read") : Descriptor.RequiredScope);
	OutObject->SetBoolField(TEXT("supports_dry_run"), Descriptor.bSupportsDryRun);
	OutObject->SetBoolField(TEXT("async_candidate"), Descriptor.bAsyncCandidate);
}

TArray<TSharedPtr<FJsonValue>> BuildSupportedScopesJson()
{
	TArray<TSharedPtr<FJsonValue>> Scopes;
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("read")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("write")));
	Scopes.Add(MakeShared<FJsonValueString>(TEXT("destructive")));
	return Scopes;
}

TArray<TSharedPtr<FJsonValue>> BuildAllowedOriginsJson(const TArray<FString>& Origins)
{
	TArray<TSharedPtr<FJsonValue>> AllowedOrigins;
	for (const FString& Origin : Origins)
	{
		AllowedOrigins.Add(MakeShared<FJsonValueString>(Origin));
	}
	return AllowedOrigins;
}
}

TSharedPtr<FJsonObject> BuildEditorIdentityJson(const FAIExportUtilityContext& Context)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();

	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FString ProjectFile = FPaths::GetProjectFilePath();
	if (!ProjectFile.IsEmpty())
	{
		ProjectFile = FPaths::ConvertRelativePathToFull(ProjectFile);
	}

	FString PluginVersion = TEXT("unknown");
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CommonAIExport"));
	if (Plugin.IsValid())
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}

	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	Capabilities->SetNumberField(TEXT("command_count"), Context.Commands.Num());
	Capabilities->SetBoolField(TEXT("supports_scope_gate"), true);
	Capabilities->SetBoolField(TEXT("supports_dry_run"), true);
	Capabilities->SetBoolField(TEXT("supports_async_jobs"), true);
	Capabilities->SetBoolField(TEXT("supports_cross_project_routing"), false);
	Capabilities->SetBoolField(TEXT("supports_native_http_mcp"), Context.HttpStatus.bRunning);
	Capabilities->SetBoolField(TEXT("supports_http_mcp_sessions"), Context.HttpStatus.bRunning);
	Capabilities->SetBoolField(TEXT("supports_mcp_pagination"), Context.HttpStatus.bRunning);
	Capabilities->SetBoolField(TEXT("supports_http_audit"), Context.HttpStatus.bRunning);
	Capabilities->SetBoolField(TEXT("http_auth_required"), Context.HttpStatus.bAuthRequired);
	Capabilities->SetArrayField(TEXT("supported_scopes"), BuildSupportedScopesJson());

	TSharedPtr<FJsonObject> Transports = MakeShared<FJsonObject>();
	Transports->SetStringField(TEXT("editor_backend"), TEXT("tcp_json_bridge"));
	Transports->SetStringField(TEXT("mcp_wrapper"), TEXT("python_stdio_fastmcp"));
	Transports->SetStringField(TEXT("native_http_mcp"), Context.HttpStatus.bRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Context.HttpStatus.Port) : TEXT(""));
	Transports->SetBoolField(TEXT("native_http_auth_required"), Context.HttpStatus.bAuthRequired);
	Transports->SetStringField(TEXT("native_http_auth_env"), TEXT("COMMONAI_MCP_HTTP_TOKEN"));

	Data->SetNumberField(TEXT("schema_version"), 1);
	Data->SetStringField(TEXT("editor_id"), Context.EditorInstanceId.IsEmpty() ? FString::Printf(TEXT("%s-%u-%d"), FApp::GetProjectName(), FPlatformProcess::GetCurrentProcessId(), Context.ServerPort) : Context.EditorInstanceId);
	Data->SetStringField(TEXT("server"), TEXT("CommonAIExport"));
	Data->SetStringField(TEXT("host"), TEXT("127.0.0.1"));
	Data->SetNumberField(TEXT("port"), Context.ServerPort);
	Data->SetNumberField(TEXT("http_port"), Context.HttpStatus.Port);
	Data->SetStringField(TEXT("http_health_url"), Context.HttpStatus.bRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/commonai/health"), Context.HttpStatus.Port) : TEXT(""));
	Data->SetStringField(TEXT("mcp_http_url"), Context.HttpStatus.bRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Context.HttpStatus.Port) : TEXT(""));
	Data->SetStringField(TEXT("mcp_protocol_version"), TEXT("2025-06-18"));
	Data->SetNumberField(TEXT("mcp_session_ttl_seconds"), Context.HttpStatus.SessionTtlSeconds);
	Data->SetNumberField(TEXT("active_mcp_sessions"), Context.HttpStatus.ActiveSessionCount);
	Data->SetArrayField(TEXT("http_allowed_origins"), BuildAllowedOriginsJson(Context.HttpStatus.AllowedOrigins));
	Data->SetBoolField(TEXT("http_audit_enabled"), Context.HttpStatus.bAuditEnabled);
	Data->SetStringField(TEXT("http_audit_log_path"), Context.HttpStatus.AuditLogPath);
	Data->SetNumberField(TEXT("http_request_count"), Context.HttpStatus.HttpRequestCount);
	Data->SetNumberField(TEXT("http_rejected_request_count"), Context.HttpStatus.HttpRejectedRequestCount);
	Data->SetNumberField(TEXT("mcp_request_count"), Context.HttpStatus.McpRequestCount);
	Data->SetNumberField(TEXT("mcp_rejected_request_count"), Context.HttpStatus.McpRejectedRequestCount);
	Data->SetNumberField(TEXT("mcp_session_expired_count"), Context.HttpStatus.McpSessionExpiredCount);
	Data->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
	Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Data->SetStringField(TEXT("project_dir"), ProjectDir);
	Data->SetStringField(TEXT("project_file"), ProjectFile);
	Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Data->SetStringField(TEXT("plugin_name"), TEXT("CommonAIExport"));
	Data->SetStringField(TEXT("plugin_version"), PluginVersion);
	Data->SetStringField(TEXT("started_at_utc"), Context.ServerStartedAtUtc);
	Data->SetStringField(TEXT("last_seen_utc"), FDateTime::UtcNow().ToIso8601());
	Data->SetStringField(TEXT("port_file"), Context.PortFilePath);
	Data->SetStringField(TEXT("registry_file"), Context.EditorRegistryFilePath);
	Data->SetObjectField(TEXT("capabilities"), Capabilities);
	Data->SetObjectField(TEXT("transports"), Transports);
	return Data;
}

TSharedPtr<FJsonObject> BuildCommandManifestJson(const FAIExportUtilityContext& Context)
{
	TArray<TSharedPtr<FJsonValue>> Commands;
	for (const FAIExportUtilityCommandDescriptor& Descriptor : Context.Commands)
	{
		TSharedPtr<FJsonObject> Command = MakeShared<FJsonObject>();
		WriteCommandDescriptorJson(Descriptor, Command);
		Commands.Add(MakeShared<FJsonValueObject>(Command));
	}

	TSharedPtr<FJsonObject> Transports = MakeShared<FJsonObject>();
	Transports->SetStringField(TEXT("editor_backend"), TEXT("tcp_json_bridge"));
	Transports->SetStringField(TEXT("mcp_wrapper"), TEXT("python_stdio_fastmcp"));
	Transports->SetStringField(TEXT("native_http_mcp"), Context.HttpStatus.bRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Context.HttpStatus.Port) : TEXT(""));
	Transports->SetBoolField(TEXT("native_http_auth_required"), Context.HttpStatus.bAuthRequired);
	Transports->SetStringField(TEXT("native_http_auth_env"), TEXT("COMMONAI_MCP_HTTP_TOKEN"));

	TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetNumberField(TEXT("schema_version"), 2);
	Manifest->SetStringField(TEXT("server"), TEXT("CommonAIExport"));
	Manifest->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Manifest->SetStringField(TEXT("manifest_source"), Context.ManifestSource);
	Manifest->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Manifest->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Manifest->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Manifest->SetNumberField(TEXT("tcp_port"), Context.ServerPort);
	Manifest->SetNumberField(TEXT("http_port"), Context.HttpStatus.Port);
	Manifest->SetStringField(TEXT("mcp_http_url"), Context.HttpStatus.bRunning ? FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Context.HttpStatus.Port) : TEXT(""));
	Manifest->SetStringField(TEXT("scope_model"), TEXT("read < write < destructive; destructive commands require explicit meta.scope"));
	Manifest->SetBoolField(TEXT("http_auth_required"), Context.HttpStatus.bAuthRequired);
	Manifest->SetBoolField(TEXT("supports_http_mcp_sessions"), Context.HttpStatus.bRunning);
	Manifest->SetBoolField(TEXT("supports_mcp_pagination"), Context.HttpStatus.bRunning);
	Manifest->SetBoolField(TEXT("supports_http_audit"), Context.HttpStatus.bRunning);
	Manifest->SetBoolField(TEXT("http_audit_enabled"), Context.HttpStatus.bAuditEnabled);
	Manifest->SetStringField(TEXT("http_audit_log_path"), Context.HttpStatus.AuditLogPath);
	Manifest->SetStringField(TEXT("mcp_protocol_version"), TEXT("2025-06-18"));
	Manifest->SetNumberField(TEXT("mcp_session_ttl_seconds"), Context.HttpStatus.SessionTtlSeconds);
	Manifest->SetArrayField(TEXT("http_allowed_origins"), BuildAllowedOriginsJson(Context.HttpStatus.AllowedOrigins));
	Manifest->SetArrayField(TEXT("supported_scopes"), BuildSupportedScopesJson());
	Manifest->SetObjectField(TEXT("transports"), Transports);
	Manifest->SetNumberField(TEXT("command_count"), Commands.Num());
	Manifest->SetArrayField(TEXT("commands"), Commands);
	return Manifest;
}

FString HandlePing(const FAIExportUtilityContext& Context)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("message"), TEXT("pong"));
	Data->SetStringField(TEXT("server"), TEXT("CommonAIExport"));
	Data->SetStringField(TEXT("editor_id"), Context.EditorInstanceId);
	Data->SetNumberField(TEXT("port"), Context.ServerPort);
	Data->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Data->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Data->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - GStartTime);
	return CreateSuccessResponse(Data);
}

FString HandleListCommands(const FAIExportUtilityContext& Context)
{
	TSharedPtr<FJsonObject> Data = BuildCommandManifestJson(Context);
	Data->SetNumberField(TEXT("count"), Context.Commands.Num());
	return CreateSuccessResponse(Data);
}

FString HandleServerStatus(const FAIExportUtilityContext& Context)
{
	TSharedPtr<FJsonObject> Data = BuildEditorIdentityJson(Context);
	Data->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - GStartTime);
	Data->SetNumberField(TEXT("active_client_connections"), Context.ActiveClientConnections);
	Data->SetNumberField(TEXT("command_count"), Context.Commands.Num());
	Data->SetArrayField(TEXT("supported_scopes"), BuildSupportedScopesJson());
	Data->SetNumberField(TEXT("tasks_queued"), Context.TaskCounts.QueuedTasks);
	Data->SetNumberField(TEXT("tasks_running"), Context.TaskCounts.RunningTasks);
	Data->SetNumberField(TEXT("tasks_completed"), Context.TaskCounts.CompletedTasks);
	Data->SetNumberField(TEXT("tasks_failed"), Context.TaskCounts.FailedTasks);
	Data->SetNumberField(TEXT("tasks_cancelled"), Context.TaskCounts.CancelledTasks);
	Data->SetNumberField(TEXT("task_event_count"), Context.TaskCounts.TaskEventCount);
	Data->SetNumberField(TEXT("latest_task_event_sequence"), static_cast<double>(Context.TaskCounts.LatestTaskEventSequence));
	return CreateSuccessResponse(Data);
}

FString HandleEditorIdentity(const FAIExportUtilityContext& Context)
{
	return CreateSuccessResponse(BuildEditorIdentityJson(Context));
}

FString HandleCommandManifestExport(TSharedPtr<FJsonObject> Params, const FAIExportUtilityContext& Context)
{
	FString OutputPath;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("output_path"), OutputPath);
	}

	if (OutputPath.IsEmpty())
	{
		OutputPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AIManifests"), TEXT("CommonAIExport_CommandManifest.json"));
	}
	OutputPath = FPaths::ConvertRelativePathToFull(OutputPath);

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!FPaths::IsUnderDirectory(OutputPath, ProjectDir))
	{
		return CreateErrorResponse(TEXT("output_path must resolve under the project directory"));
	}

	FString OutputJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
	FJsonSerializer::Serialize(BuildCommandManifestJson(Context).ToSharedRef(), Writer);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
	if (!FFileHelper::SaveStringToFile(OutputJson, *OutputPath))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to write command manifest: %s"), *OutputPath));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("output_path"), OutputPath);
	Data->SetNumberField(TEXT("command_count"), Context.Commands.Num());
	Data->SetStringField(TEXT("manifest_source"), Context.ManifestSource);
	Data->SetBoolField(TEXT("written"), true);
	return CreateSuccessResponse(Data);
}

FString HandleProjectStatus(const FAIExportUtilityContext& Context)
{
	TSharedPtr<FJsonObject> Data = BuildEditorIdentityJson(Context);
	Data->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - GStartTime);
	Data->SetNumberField(TEXT("command_count"), Context.Commands.Num());
	Data->SetBoolField(TEXT("port_file_exists"), IFileManager::Get().FileExists(*Context.PortFilePath));
	Data->SetBoolField(TEXT("project_file_exists"), IFileManager::Get().FileExists(*FPaths::GetProjectFilePath()));
	Data->SetBoolField(TEXT("diversion_repo"), IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectDir(), TEXT(".diversion"))));
	Data->SetBoolField(TEXT("git_repo"), IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectDir(), TEXT(".git"))));
	Data->SetBoolField(TEXT("vs_solution_exists"), IFileManager::Get().FileExists(*FPaths::Combine(FPaths::ProjectDir(), FString::Printf(TEXT("%s.sln"), FApp::GetProjectName()))));
	Data->SetStringField(TEXT("last_build_log"), FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("Logs"), TEXT("LastBuild.log"))));
	Data->SetBoolField(TEXT("last_build_log_exists"), IFileManager::Get().FileExists(*FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("Logs"), TEXT("LastBuild.log"))));

	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *FPaths::Combine(FPaths::ProjectLogDir(), TEXT("*.log")), true, false);
	Data->SetNumberField(TEXT("log_file_count"), LogFiles.Num());

	TSharedPtr<FJsonObject> EditorState = MakeShared<FJsonObject>();
	EditorState->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	EditorState->SetBoolField(TEXT("simulating"), GEditor && GEditor->bIsSimulatingInEditor);
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	EditorState->SetStringField(TEXT("world_name"), World ? World->GetName() : TEXT(""));
	EditorState->SetStringField(TEXT("world_package"), (World && World->GetOutermost()) ? World->GetOutermost()->GetName() : TEXT(""));
	Data->SetObjectField(TEXT("editor_state"), EditorState);

	return CreateSuccessResponse(Data);
}
}
