// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportAsyncJobStore.h"

#include "HttpServerRequest.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CommonAIExport::CommandHandlers
{
namespace
{
int32 ReadClampedIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
{
	if (!Params.IsValid())
	{
		return DefaultValue;
	}

	double NumberValue = 0.0;
	if (!Params->TryGetNumberField(FieldName, NumberValue))
	{
		return DefaultValue;
	}
	return FMath::Clamp(static_cast<int32>(NumberValue), MinValue, MaxValue);
}
}

FString FAIExportAsyncJobStore::CreateQueuedTask(const FString& CommandName)
{
	const FString TaskId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	TSharedPtr<FAIExportAsyncCommandJob> Job = MakeShared<FAIExportAsyncCommandJob>();
	Job->TaskId = TaskId;
	Job->CommandName = CommandName;
	Job->Status = TEXT("queued");
	Job->SubmittedAt = FDateTime::UtcNow().ToIso8601();

	FScopeLock Lock(&CriticalSection);
	Jobs.Add(TaskId, Job);
	AppendTaskEventLocked(*Job, TEXT("queued"), TEXT("Task queued"));
	return TaskId;
}

bool FAIExportAsyncJobStore::MarkTaskRunning(const FString& TaskId)
{
	FScopeLock Lock(&CriticalSection);
	TSharedPtr<FAIExportAsyncCommandJob>* FoundJob = Jobs.Find(TaskId);
	if (!FoundJob || !FoundJob->IsValid())
	{
		return false;
	}
	if ((*FoundJob)->bCancelRequested)
	{
		(*FoundJob)->Status = TEXT("cancelled");
		(*FoundJob)->FinishedAt = FDateTime::UtcNow().ToIso8601();
		AppendTaskEventLocked(*FoundJob->Get(), TEXT("cancelled"), TEXT("Task cancelled before start"));
		return false;
	}
	(*FoundJob)->Status = TEXT("running");
	(*FoundJob)->StartedAt = FDateTime::UtcNow().ToIso8601();
	AppendTaskEventLocked(*FoundJob->Get(), TEXT("running"), TEXT("Task started"));
	return true;
}

void FAIExportAsyncJobStore::CompleteTask(const FString& TaskId, const FString& ResponseJson, bool bSuccess, const FString& ErrorMessage)
{
	FScopeLock Lock(&CriticalSection);
	TSharedPtr<FAIExportAsyncCommandJob>* FoundJob = Jobs.Find(TaskId);
	if (!FoundJob || !FoundJob->IsValid())
	{
		return;
	}

	(*FoundJob)->ResultJson = ResponseJson;
	(*FoundJob)->FinishedAt = FDateTime::UtcNow().ToIso8601();
	(*FoundJob)->Status = bSuccess ? TEXT("completed") : TEXT("failed");
	(*FoundJob)->ErrorMessage = ErrorMessage;
	if ((*FoundJob)->bCancelRequested && !bSuccess)
	{
		(*FoundJob)->Status = TEXT("cancelled");
	}
	AppendTaskEventLocked(
		*FoundJob->Get(),
		(*FoundJob)->Status,
		bSuccess ? TEXT("Task completed") : (ErrorMessage.IsEmpty() ? TEXT("Task failed") : ErrorMessage));
}

bool FAIExportAsyncJobStore::RequestCancelTask(const FString& TaskId, FAIExportAsyncCommandJob& OutJob)
{
	FScopeLock Lock(&CriticalSection);
	TSharedPtr<FAIExportAsyncCommandJob>* FoundJob = Jobs.Find(TaskId);
	if (!FoundJob || !FoundJob->IsValid())
	{
		return false;
	}

	(*FoundJob)->bCancelRequested = true;
	if ((*FoundJob)->Status == TEXT("queued"))
	{
		(*FoundJob)->Status = TEXT("cancelled");
		(*FoundJob)->FinishedAt = FDateTime::UtcNow().ToIso8601();
		AppendTaskEventLocked(*FoundJob->Get(), TEXT("cancelled"), TEXT("Task cancelled before start"));
	}
	else if ((*FoundJob)->Status == TEXT("running"))
	{
		AppendTaskEventLocked(*FoundJob->Get(), TEXT("cancel_requested"), TEXT("Cancellation requested"));
	}
	OutJob = *FoundJob->Get();
	return true;
}

bool FAIExportAsyncJobStore::TryCopyTask(const FString& TaskId, FAIExportAsyncCommandJob& OutJob) const
{
	FScopeLock Lock(&CriticalSection);
	const TSharedPtr<FAIExportAsyncCommandJob>* FoundJob = Jobs.Find(TaskId);
	if (!FoundJob || !FoundJob->IsValid())
	{
		return false;
	}

	OutJob = *FoundJob->Get();
	return true;
}

FAIExportAsyncJobCounts FAIExportAsyncJobStore::GetCounts() const
{
	FAIExportAsyncJobCounts Counts;
	FScopeLock Lock(&CriticalSection);
	for (const TPair<FString, TSharedPtr<FAIExportAsyncCommandJob>>& Pair : Jobs)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}

		const FString& Status = Pair.Value->Status;
		if (Status == TEXT("queued")) ++Counts.QueuedTasks;
		else if (Status == TEXT("running")) ++Counts.RunningTasks;
		else if (Status == TEXT("completed")) ++Counts.CompletedTasks;
		else if (Status == TEXT("failed")) ++Counts.FailedTasks;
		else if (Status == TEXT("cancelled")) ++Counts.CancelledTasks;
	}
	Counts.TaskEventCount = Events.Num();
	Counts.LatestTaskEventSequence = EventSequence;
	return Counts;
}

TSharedPtr<FJsonObject> FAIExportAsyncJobStore::BuildTaskJson(const FAIExportAsyncCommandJob& Job, bool bIncludeResult) const
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), Job.TaskId);
	Data->SetStringField(TEXT("command"), Job.CommandName);
	Data->SetStringField(TEXT("status"), Job.Status);
	Data->SetStringField(TEXT("submitted_at"), Job.SubmittedAt);
	Data->SetBoolField(TEXT("cancel_requested"), Job.bCancelRequested);
	if (!Job.StartedAt.IsEmpty())
	{
		Data->SetStringField(TEXT("started_at"), Job.StartedAt);
	}
	if (!Job.FinishedAt.IsEmpty())
	{
		Data->SetStringField(TEXT("finished_at"), Job.FinishedAt);
	}
	if (!Job.ErrorMessage.IsEmpty())
	{
		Data->SetStringField(TEXT("error"), Job.ErrorMessage);
	}
	if (bIncludeResult && !Job.ResultJson.IsEmpty())
	{
		Data->SetStringField(TEXT("response_json"), Job.ResultJson);

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Job.ResultJson);
		TSharedPtr<FJsonObject> ParsedResult;
		if (FJsonSerializer::Deserialize(Reader, ParsedResult) && ParsedResult.IsValid())
		{
			Data->SetObjectField(TEXT("response"), ParsedResult);
		}
	}
	return Data;
}

TSharedPtr<FJsonObject> FAIExportAsyncJobStore::BuildTaskEventJson(const FAIExportAsyncCommandEvent& Event) const
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("sequence"), static_cast<double>(Event.Sequence));
	Data->SetStringField(TEXT("task_id"), Event.TaskId);
	Data->SetStringField(TEXT("command"), Event.CommandName);
	Data->SetStringField(TEXT("status"), Event.Status);
	Data->SetStringField(TEXT("event"), Event.EventType);
	Data->SetStringField(TEXT("timestamp_utc"), Event.TimestampUtc);
	if (!Event.Message.IsEmpty())
	{
		Data->SetStringField(TEXT("message"), Event.Message);
	}
	return Data;
}

TSharedPtr<FJsonObject> FAIExportAsyncJobStore::BuildTaskEventsJson(TSharedPtr<FJsonObject> Params) const
{
	FString TaskId;
	double AfterSequenceNumber = 0.0;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("task_id"), TaskId);
		Params->TryGetNumberField(TEXT("after_sequence"), AfterSequenceNumber);
	}
	TaskId.TrimStartAndEndInline();
	const int64 AfterSequence = FMath::Max<int64>(0, static_cast<int64>(AfterSequenceNumber));
	const int32 Limit = ReadClampedIntField(Params, TEXT("limit"), 100, 1, 1000);

	TArray<TSharedPtr<FJsonValue>> ReturnedEvents;
	int32 MatchedCount = 0;
	int64 LatestSequence = 0;
	{
		FScopeLock Lock(&CriticalSection);
		LatestSequence = EventSequence;
		for (const FAIExportAsyncCommandEvent& Event : Events)
		{
			if (!TaskId.IsEmpty() && Event.TaskId != TaskId)
			{
				continue;
			}
			if (Event.Sequence <= AfterSequence)
			{
				continue;
			}

			++MatchedCount;
			if (ReturnedEvents.Num() < Limit)
			{
				ReturnedEvents.Add(MakeShared<FJsonValueObject>(BuildTaskEventJson(Event)));
			}
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("task_id"), TaskId);
	Data->SetNumberField(TEXT("after_sequence"), static_cast<double>(AfterSequence));
	Data->SetNumberField(TEXT("latest_sequence"), static_cast<double>(LatestSequence));
	Data->SetNumberField(TEXT("matched_count"), MatchedCount);
	Data->SetNumberField(TEXT("returned_count"), ReturnedEvents.Num());
	Data->SetNumberField(TEXT("limit"), Limit);
	Data->SetBoolField(TEXT("has_more"), MatchedCount > ReturnedEvents.Num());
	Data->SetArrayField(TEXT("events"), ReturnedEvents);
	return Data;
}

TSharedPtr<FJsonObject> FAIExportAsyncJobStore::BuildTaskEventsWaitJson(TSharedPtr<FJsonObject> Params, TFunctionRef<bool()> IsStopRequested) const
{
	const int32 TimeoutMs = ReadClampedIntField(Params, TEXT("timeout_ms"), 5000, 0, 30000);
	const int32 PollIntervalMs = ReadClampedIntField(Params, TEXT("poll_interval_ms"), 50, 10, 1000);
	const double StartSeconds = FPlatformTime::Seconds();
	const double DeadlineSeconds = StartSeconds + (static_cast<double>(TimeoutMs) / 1000.0);

	TSharedPtr<FJsonObject> Data;
	bool bTimedOut = false;
	bool bStopped = false;

	while (true)
	{
		Data = BuildTaskEventsJson(Params);
		const int32 ReturnedCount = Data.IsValid() ? static_cast<int32>(Data->GetNumberField(TEXT("returned_count"))) : 0;
		if (ReturnedCount > 0 || TimeoutMs <= 0)
		{
			break;
		}

		if (IsStopRequested())
		{
			bStopped = true;
			break;
		}

		const double RemainingSeconds = DeadlineSeconds - FPlatformTime::Seconds();
		if (RemainingSeconds <= 0.0)
		{
			bTimedOut = true;
			break;
		}

		const double SleepSeconds = FMath::Min(RemainingSeconds, static_cast<double>(PollIntervalMs) / 1000.0);
		FPlatformProcess::Sleep(static_cast<float>(SleepSeconds));
	}

	if (!Data.IsValid())
	{
		Data = BuildTaskEventsJson(Params);
	}

	const int32 ReturnedCount = Data.IsValid() ? static_cast<int32>(Data->GetNumberField(TEXT("returned_count"))) : 0;
	if (ReturnedCount <= 0 && TimeoutMs > 0 && !bStopped && FPlatformTime::Seconds() >= DeadlineSeconds)
	{
		bTimedOut = true;
	}

	const double WaitedMs = FMath::Max(0.0, (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	Data->SetBoolField(TEXT("waited"), true);
	Data->SetNumberField(TEXT("timeout_ms"), TimeoutMs);
	Data->SetNumberField(TEXT("poll_interval_ms"), PollIntervalMs);
	Data->SetNumberField(TEXT("waited_ms"), WaitedMs);
	Data->SetBoolField(TEXT("timed_out"), bTimedOut && ReturnedCount <= 0);
	Data->SetBoolField(TEXT("server_stopping"), bStopped);
	return Data;
}

FString FAIExportAsyncJobStore::BuildTaskEventsSse(TSharedPtr<FJsonObject> Params) const
{
	TSharedPtr<FJsonObject> Data = BuildTaskEventsJson(Params);
	FString Output = TEXT(": CommonAIExport async task events\nretry: 1000\n\n");

	FString WatermarkJson;
	{
		TSharedPtr<FJsonObject> Watermark = MakeShared<FJsonObject>();
		Watermark->SetNumberField(TEXT("latest_sequence"), Data->GetNumberField(TEXT("latest_sequence")));
		Watermark->SetNumberField(TEXT("returned_count"), Data->GetNumberField(TEXT("returned_count")));
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&WatermarkJson);
		FJsonSerializer::Serialize(Watermark.ToSharedRef(), Writer);
	}
	Output += FString::Printf(TEXT("event: watermark\ndata: %s\n\n"), *WatermarkJson);

	const TArray<TSharedPtr<FJsonValue>>* ReturnedEvents = nullptr;
	if (!Data->TryGetArrayField(TEXT("events"), ReturnedEvents) || !ReturnedEvents)
	{
		return Output;
	}

	for (const TSharedPtr<FJsonValue>& EventValue : *ReturnedEvents)
	{
		const TSharedPtr<FJsonObject> EventObject = EventValue.IsValid() ? EventValue->AsObject() : nullptr;
		if (!EventObject.IsValid())
		{
			continue;
		}

		const int64 Sequence = static_cast<int64>(EventObject->GetNumberField(TEXT("sequence")));
		const FString EventName = EventObject->GetStringField(TEXT("event"));
		FString EventJson;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&EventJson);
		FJsonSerializer::Serialize(EventObject.ToSharedRef(), Writer);
		Output += FString::Printf(TEXT("id: %lld\nevent: %s\ndata: %s\n\n"), Sequence, *EventName, *EventJson);
	}

	return Output;
}

TSharedPtr<FJsonObject> FAIExportAsyncJobStore::BuildTaskEventParamsFromHttpRequest(const FHttpServerRequest& Request) const
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (const FString* TaskId = Request.QueryParams.Find(TEXT("task_id")))
	{
		Params->SetStringField(TEXT("task_id"), *TaskId);
	}
	if (const FString* AfterSequence = Request.QueryParams.Find(TEXT("after_sequence")))
	{
		Params->SetNumberField(TEXT("after_sequence"), FCString::Atod(**AfterSequence));
	}
	if (const FString* Limit = Request.QueryParams.Find(TEXT("limit")))
	{
		Params->SetNumberField(TEXT("limit"), FCString::Atod(**Limit));
	}
	if (const FString* TimeoutMs = Request.QueryParams.Find(TEXT("timeout_ms")))
	{
		Params->SetNumberField(TEXT("timeout_ms"), FCString::Atod(**TimeoutMs));
	}
	if (const FString* PollIntervalMs = Request.QueryParams.Find(TEXT("poll_interval_ms")))
	{
		Params->SetNumberField(TEXT("poll_interval_ms"), FCString::Atod(**PollIntervalMs));
	}
	return Params;
}

TArray<TSharedPtr<FJsonValue>> FAIExportAsyncJobStore::BuildTaskList(bool bIncludeFinishedOnly, bool bIncludeCancellableOnly, bool bIncludeResult) const
{
	TArray<TSharedPtr<FJsonValue>> TaskList;
	FScopeLock Lock(&CriticalSection);
	for (const TPair<FString, TSharedPtr<FAIExportAsyncCommandJob>>& Pair : Jobs)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}

		const FString& Status = Pair.Value->Status;
		if (bIncludeFinishedOnly && !(Status == TEXT("completed") || Status == TEXT("failed") || Status == TEXT("cancelled")))
		{
			continue;
		}
		if (bIncludeCancellableOnly && !(Status == TEXT("queued") || Status == TEXT("running")))
		{
			continue;
		}

		TaskList.Add(MakeShared<FJsonValueObject>(BuildTaskJson(*Pair.Value, bIncludeResult)));
	}
	return TaskList;
}

void FAIExportAsyncJobStore::AppendTaskEventLocked(const FAIExportAsyncCommandJob& Job, const FString& EventType, const FString& Message)
{
	FAIExportAsyncCommandEvent Event;
	Event.Sequence = ++EventSequence;
	Event.TaskId = Job.TaskId;
	Event.CommandName = Job.CommandName;
	Event.Status = Job.Status;
	Event.EventType = EventType;
	Event.TimestampUtc = FDateTime::UtcNow().ToIso8601();
	Event.Message = Message;

	Events.Add(Event);
	while (Events.Num() > MaxEvents)
	{
		Events.RemoveAt(0, 1, EAllowShrinking::No);
	}
}
}
