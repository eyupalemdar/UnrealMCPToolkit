// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportAsyncCommands.h"
#include "CommandHandlers/AIExportCommandResponse.h"

#include "Async/Async.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CommonAIExport::CommandHandlers::AsyncCommands
{
FString HandleTaskSubmit(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store, const FAIExportAsyncSubmitCallbacks& Callbacks)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	FString CommandName;
	if (!Params->TryGetStringField(TEXT("command"), CommandName) && !Params->TryGetStringField(TEXT("command_name"), CommandName))
	{
		return CreateErrorResponse(TEXT("Missing 'command' parameter"));
	}

	FAIExportAsyncCommandDescriptor TargetDescriptor;
	if (!Callbacks.ResolveCommand || !Callbacks.ResolveCommand(CommandName, TargetDescriptor))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown command for task submission: %s"), *CommandName));
	}

	if (CommandName.StartsWith(TEXT("task_")))
	{
		return CreateErrorResponse(TEXT("Async task commands cannot submit other async task commands"));
	}

	TSharedPtr<FJsonObject> CommandParams;
	if (Params->HasField(TEXT("params")))
	{
		const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
		if (!Params->TryGetObjectField(TEXT("params"), ParamsObject) || !ParamsObject || !ParamsObject->IsValid())
		{
			return CreateErrorResponse(TEXT("'params' must be a JSON object when provided"));
		}
		CommandParams = *ParamsObject;
	}

	if (TargetDescriptor.bRequiresParams && !CommandParams.IsValid())
	{
		return CreateErrorResponse(FString::Printf(TEXT("Command '%s' requires a 'params' object"), *CommandName));
	}

	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* MetaObject = nullptr;
	if (Params->TryGetObjectField(TEXT("meta"), MetaObject) && MetaObject && MetaObject->IsValid())
	{
		Meta = *MetaObject;
	}

	FString ScopeOverride;
	if (Params->TryGetStringField(TEXT("scope"), ScopeOverride))
	{
		Meta->SetStringField(TEXT("scope"), ScopeOverride);
	}

	bool bDryRun = false;
	if (Params->TryGetBoolField(TEXT("dry_run"), bDryRun))
	{
		Meta->SetBoolField(TEXT("dry_run"), bDryRun);
	}

	if (Callbacks.ValidateCommand)
	{
		const FString ValidationError = Callbacks.ValidateCommand(CommandName, Meta);
		if (!ValidationError.IsEmpty())
		{
			return CreateErrorResponse(ValidationError);
		}
	}

	bool bMetaDryRun = false;
	Meta->TryGetBoolField(TEXT("dry_run"), bMetaDryRun);
	if (bMetaDryRun && TargetDescriptor.bMutating)
	{
		return Callbacks.CreateDryRunResponse ? Callbacks.CreateDryRunResponse(CommandName, Meta) : CreateErrorResponse(TEXT("Dry-run response callback is not configured"));
	}

	if (!Callbacks.DispatchCommand)
	{
		return CreateErrorResponse(TEXT("Async dispatch callback is not configured"));
	}

	const FString TaskId = Store.CreateQueuedTask(CommandName);
	FAIExportAsyncJobStore* StorePtr = &Store;
	Async(EAsyncExecution::ThreadPool, [StorePtr, TaskId, CommandName, CommandParams, Dispatch = Callbacks.DispatchCommand]()
	{
		if (!StorePtr->MarkTaskRunning(TaskId))
		{
			return;
		}

		const FString Response = Dispatch(CommandName, CommandParams);
		bool bSuccess = false;
		FString ErrorMessage;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);
		TSharedPtr<FJsonObject> ParsedResponse;
		if (FJsonSerializer::Deserialize(Reader, ParsedResponse) && ParsedResponse.IsValid())
		{
			ParsedResponse->TryGetBoolField(TEXT("success"), bSuccess);
			ParsedResponse->TryGetStringField(TEXT("error"), ErrorMessage);
		}

		StorePtr->CompleteTask(TaskId, Response, bSuccess, ErrorMessage);
	});

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	Data->SetStringField(TEXT("command"), CommandName);
	Data->SetStringField(TEXT("status"), TEXT("queued"));
	Data->SetBoolField(TEXT("async_candidate"), TargetDescriptor.bAsyncCandidate);
	Data->SetNumberField(TEXT("timeout_seconds"), TargetDescriptor.TimeoutSeconds);
	return CreateSuccessResponse(Data);
}

FString HandleTaskStatus(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store)
{
	FString TaskId;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
	}

	if (!TaskId.IsEmpty())
	{
		FAIExportAsyncCommandJob Job;
		if (!Store.TryCopyTask(TaskId, Job))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId));
		}
		return CreateSuccessResponse(Store.BuildTaskJson(Job, false));
	}

	TArray<TSharedPtr<FJsonValue>> Tasks = Store.BuildTaskList(false, false, false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tasks"), Tasks);
	Data->SetNumberField(TEXT("count"), Tasks.Num());
	return CreateSuccessResponse(Data);
}

FString HandleTaskResult(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store)
{
	FString TaskId;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
	}

	if (!TaskId.IsEmpty())
	{
		FAIExportAsyncCommandJob Job;
		if (!Store.TryCopyTask(TaskId, Job))
		{
			return CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId));
		}
		return CreateSuccessResponse(Store.BuildTaskJson(Job, true));
	}

	TArray<TSharedPtr<FJsonValue>> Tasks = Store.BuildTaskList(true, false, false);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("completed_tasks"), Tasks);
	Data->SetNumberField(TEXT("count"), Tasks.Num());
	Data->SetStringField(TEXT("message"), TEXT("Pass task_id to include the stored command response."));
	return CreateSuccessResponse(Data);
}

FString HandleTaskEvents(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store)
{
	return CreateSuccessResponse(Store.BuildTaskEventsJson(Params));
}

FString HandleTaskEventsWait(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store, TFunctionRef<bool()> IsStopRequested)
{
	return CreateSuccessResponse(Store.BuildTaskEventsWaitJson(Params, IsStopRequested));
}

FString HandleTaskCancel(TSharedPtr<FJsonObject> Params, FAIExportAsyncJobStore& Store)
{
	FString TaskId;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
	}

	if (TaskId.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> CancellableTasks = Store.BuildTaskList(false, true, false);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetArrayField(TEXT("cancellable_tasks"), CancellableTasks);
		Data->SetNumberField(TEXT("count"), CancellableTasks.Num());
		Data->SetStringField(TEXT("message"), TEXT("Pass task_id to request cancellation."));
		return CreateSuccessResponse(Data);
	}

	FAIExportAsyncCommandJob JobCopy;
	if (!Store.RequestCancelTask(TaskId, JobCopy))
	{
		return CreateErrorResponse(FString::Printf(TEXT("Unknown task_id: %s"), *TaskId));
	}

	TSharedPtr<FJsonObject> Data = Store.BuildTaskJson(JobCopy, false);
	Data->SetBoolField(TEXT("cancel_requested"), true);
	Data->SetStringField(TEXT("message"), TEXT("Cancellation is cooperative; already-running UE work may finish before the task observes cancellation."));
	return CreateSuccessResponse(Data);
}
}
