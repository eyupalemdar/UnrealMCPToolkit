// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Dom/JsonObject.h"

struct FHttpServerRequest;

namespace MCPToolkit::CommandHandlers
{
struct FMCTAsyncCommandJob
{
	FString TaskId;
	FString CommandName;
	FString Status;
	FString SubmittedAt;
	FString StartedAt;
	FString FinishedAt;
	FString ResultJson;
	FString ErrorMessage;
	bool bCancelRequested = false;
};

struct FMCTAsyncCommandEvent
{
	int64 Sequence = 0;
	FString TaskId;
	FString CommandName;
	FString Status;
	FString EventType;
	FString TimestampUtc;
	FString Message;
};

struct FMCTAsyncJobCounts
{
	int32 QueuedTasks = 0;
	int32 RunningTasks = 0;
	int32 CompletedTasks = 0;
	int32 FailedTasks = 0;
	int32 CancelledTasks = 0;
	int32 TaskEventCount = 0;
	int64 LatestTaskEventSequence = 0;
};

class FMCTAsyncJobStore
{
public:
	FString CreateQueuedTask(const FString& CommandName);
	bool MarkTaskRunning(const FString& TaskId);
	void CompleteTask(const FString& TaskId, const FString& ResponseJson, bool bSuccess, const FString& ErrorMessage);
	bool RequestCancelTask(const FString& TaskId, FMCTAsyncCommandJob& OutJob);
	bool TryCopyTask(const FString& TaskId, FMCTAsyncCommandJob& OutJob) const;

	FMCTAsyncJobCounts GetCounts() const;
	TSharedPtr<FJsonObject> BuildTaskJson(const FMCTAsyncCommandJob& Job, bool bIncludeResult) const;
	TSharedPtr<FJsonObject> BuildTaskEventsJson(TSharedPtr<FJsonObject> Params) const;
	TSharedPtr<FJsonObject> BuildTaskEventsWaitJson(TSharedPtr<FJsonObject> Params, TFunctionRef<bool()> IsStopRequested) const;
	FString BuildTaskEventsSse(TSharedPtr<FJsonObject> Params) const;
	TSharedPtr<FJsonObject> BuildTaskEventParamsFromHttpRequest(const FHttpServerRequest& Request) const;

	TArray<TSharedPtr<FJsonValue>> BuildTaskList(bool bIncludeFinishedOnly, bool bIncludeCancellableOnly, bool bIncludeResult) const;

private:
	TSharedPtr<FJsonObject> BuildTaskEventJson(const FMCTAsyncCommandEvent& Event) const;
	void AppendTaskEventLocked(const FMCTAsyncCommandJob& Job, const FString& EventType, const FString& Message);

	mutable FCriticalSection CriticalSection;
	TMap<FString, TSharedPtr<FMCTAsyncCommandJob>> Jobs;
	TArray<FMCTAsyncCommandEvent> Events;
	int64 EventSequence = 0;
	static constexpr int32 MaxEvents = 1000;
};
}
