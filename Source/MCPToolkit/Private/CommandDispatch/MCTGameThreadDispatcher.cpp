// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandDispatch/MCTGameThreadDispatcher.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "MCTModule.h"

#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "RenderAssetUpdate.h"
#include "UObject/GarbageCollection.h"

namespace
{
enum class EMCTGameThreadDispatchState : uint8
{
	Pending,
	Started,
	Completed,
	Cancelled,
};
}

struct FMCTGameThreadDispatchTask
{
	FMCTGameThreadDispatchTask(
		const uint64 InTaskId,
		TFunction<void()>&& InWork,
		TFunction<void()>&& InTimeoutCompletion,
		TFunction<void()>&& InShutdownCompletion)
		: TaskId(InTaskId)
		, EnqueuedAtSeconds(FPlatformTime::Seconds())
		, Work(MoveTemp(InWork))
		, TimeoutCompletion(MoveTemp(InTimeoutCompletion))
		, ShutdownCompletion(MoveTemp(InShutdownCompletion))
	{
	}

	void SetDeadline(const FTimespan& Timeout)
	{
		const double TimeoutSeconds = FMath::Max(0.0, Timeout.GetTotalSeconds());
		FScopeLock Lock(&StateCriticalSection);
		if (State == EMCTGameThreadDispatchState::Pending)
		{
			const double NewDeadline = FPlatformTime::Seconds() + TimeoutSeconds;
			DeadlineSeconds = DeadlineSeconds > 0.0
				? FMath::Min(DeadlineSeconds, NewDeadline)
				: NewDeadline;
		}
	}

	bool HasDeadlineExpired(const double CurrentSeconds) const
	{
		FScopeLock Lock(&StateCriticalSection);
		return State == EMCTGameThreadDispatchState::Pending
			&& DeadlineSeconds > 0.0
			&& CurrentSeconds >= DeadlineSeconds;
	}

	bool TryStart()
	{
		FScopeLock Lock(&StateCriticalSection);
		if (State != EMCTGameThreadDispatchState::Pending)
		{
			return false;
		}

		State = EMCTGameThreadDispatchState::Started;
		return true;
	}

	void Complete()
	{
		FScopeLock Lock(&StateCriticalSection);
		if (State == EMCTGameThreadDispatchState::Started)
		{
			State = EMCTGameThreadDispatchState::Completed;
		}
	}

	bool CancelPending()
	{
		TFunction<void()> CompletionToRun;
		{
			FScopeLock Lock(&StateCriticalSection);
			if (State != EMCTGameThreadDispatchState::Pending)
			{
				return false;
			}

			State = EMCTGameThreadDispatchState::Cancelled;
			CompletionToRun = MoveTemp(TimeoutCompletion);
		}

		if (CompletionToRun)
		{
			CompletionToRun();
		}
		return true;
	}

	bool CancelPendingForShutdown()
	{
		TFunction<void()> CompletionToRun;
		{
			FScopeLock Lock(&StateCriticalSection);
			if (State != EMCTGameThreadDispatchState::Pending)
			{
				return false;
			}

			State = EMCTGameThreadDispatchState::Cancelled;
			CompletionToRun = MoveTemp(ShutdownCompletion);
		}

		if (CompletionToRun)
		{
			CompletionToRun();
		}
		return true;
	}

	bool IsPending() const
	{
		FScopeLock Lock(&StateCriticalSection);
		return State == EMCTGameThreadDispatchState::Pending;
	}

	bool IsCancelled() const
	{
		FScopeLock Lock(&StateCriticalSection);
		return State == EMCTGameThreadDispatchState::Cancelled;
	}

	uint64 TaskId = 0;
	double EnqueuedAtSeconds = 0.0;

private:
	mutable FCriticalSection StateCriticalSection;
	EMCTGameThreadDispatchState State = EMCTGameThreadDispatchState::Pending;
	double DeadlineSeconds = 0.0;

public:
	TFunction<void()> Work;
	TFunction<void()> TimeoutCompletion;
	TFunction<void()> ShutdownCompletion;
};

FMCTGameThreadDispatchHandle::FMCTGameThreadDispatchHandle(TSharedPtr<FMCTGameThreadDispatchTask> InTask)
	: Task(MoveTemp(InTask))
{
}

void FMCTGameThreadDispatchHandle::SetDeadline(const FTimespan& Timeout) const
{
	if (Task.IsValid())
	{
		Task->SetDeadline(Timeout);
	}
}

bool FMCTGameThreadDispatchHandle::Cancel() const
{
	return Task.IsValid() && Task->CancelPending();
}

bool FMCTGameThreadDispatchHandle::IsValid() const
{
	return Task.IsValid();
}

bool FMCTGameThreadDispatchHandle::IsPending() const
{
	return Task.IsValid() && Task->IsPending();
}

bool FMCTGameThreadDispatchHandle::IsCancelled() const
{
	return Task.IsValid() && Task->IsCancelled();
}

FMCTGameThreadDispatcher::FMCTGameThreadDispatcher(FSafetyStateProvider InSafetyStateProvider)
	: SafetyStateProvider(MoveTemp(InSafetyStateProvider))
{
}

FMCTGameThreadDispatcher::~FMCTGameThreadDispatcher()
{
	ensureMsgf(
		!TickerHandle.IsValid(),
		TEXT("MCPToolkit Game Thread dispatcher destroyed before Shutdown removed its ticker"));
}

FMCTGameThreadDispatcher& FMCTGameThreadDispatcher::Get()
{
	static FMCTGameThreadDispatcher Dispatcher;
	return Dispatcher;
}

void FMCTGameThreadDispatcher::Startup(const bool bRegisterTicker)
{
	check(IsInGameThread());

	{
		FScopeLock Lock(&QueueCriticalSection);
		bShuttingDown.Store(false);
		bAcceptingCommands = true;
	}

	if (bRegisterTicker && !TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FMCTGameThreadDispatcher::Tick));
	}

	UE_LOG(LogMCT, Verbose, TEXT("MCPToolkit Game Thread dispatcher started"));
}

void FMCTGameThreadDispatcher::BeginShutdown()
{
	TArray<TSharedPtr<FMCTGameThreadDispatchTask>> CommandsToCancel;
	{
		FScopeLock Lock(&QueueCriticalSection);
		bAcceptingCommands = false;
		bShuttingDown.Store(true);

		TSharedPtr<FMCTGameThreadDispatchTask> Command;
		while (PendingCommands.Dequeue(Command))
		{
			if (Command.IsValid())
			{
				CommandsToCancel.Add(MoveTemp(Command));
			}
		}
	}

	for (const TSharedPtr<FMCTGameThreadDispatchTask>& Command : CommandsToCancel)
	{
		if (Command->CancelPendingForShutdown())
		{
			const double WaitedMilliseconds =
				(FPlatformTime::Seconds() - Command->EnqueuedAtSeconds) * 1000.0;
			UE_LOG(
				LogMCT,
				Verbose,
				TEXT("Game Thread command %llu cancelled after %.2f ms: editor shutting down"),
				Command->TaskId,
				WaitedMilliseconds);
		}
	}
}

void FMCTGameThreadDispatcher::Shutdown()
{
	check(IsInGameThread());
	BeginShutdown();

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	UE_LOG(LogMCT, Verbose, TEXT("MCPToolkit Game Thread dispatcher stopped"));
}

FMCTGameThreadDispatchHandle FMCTGameThreadDispatcher::Enqueue(
	const TSharedPtr<TPromise<FString>>& CompletionPromise,
	TFunction<void()>&& Work)
{
	TFunction<void()> ShutdownCompletion = [CompletionPromise]()
	{
		if (CompletionPromise.IsValid())
		{
			CompletionPromise->SetValue(
				MCPToolkit::CommandHandlers::CreateErrorResponse(TEXT("editor shutting down")));
		}
	};
	TFunction<void()> TimeoutCompletion = [CompletionPromise]()
	{
		if (CompletionPromise.IsValid())
		{
			CompletionPromise->SetValue(
				MCPToolkit::CommandHandlers::CreateErrorResponse(
					TEXT("game thread command timed out before execution")));
		}
	};
	return EnqueueWithCompletions(
		MoveTemp(Work),
		MoveTemp(TimeoutCompletion),
		MoveTemp(ShutdownCompletion));
}

FMCTGameThreadDispatchHandle FMCTGameThreadDispatcher::EnqueueWithCompletions(
	TFunction<void()>&& Work,
	TFunction<void()>&& TimeoutCompletion,
	TFunction<void()>&& ShutdownCompletion)
{
	TSharedPtr<FMCTGameThreadDispatchTask> Command;
	{
		FScopeLock Lock(&QueueCriticalSection);
		if (bAcceptingCommands && !bShuttingDown.Load())
		{
			Command = MakeShared<FMCTGameThreadDispatchTask>(
				NextTaskId++,
				MoveTemp(Work),
				MoveTemp(TimeoutCompletion),
				MoveTemp(ShutdownCompletion));
			PendingCommands.Enqueue(Command);
		}
	}

	if (!Command.IsValid())
	{
		if (ShutdownCompletion)
		{
			ShutdownCompletion();
		}
		return FMCTGameThreadDispatchHandle();
	}
	return FMCTGameThreadDispatchHandle(MoveTemp(Command));
}

bool FMCTGameThreadDispatcher::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	check(IsInGameThread());

	if (bShuttingDown.Load())
	{
		return true;
	}

	while (true)
	{
		TSharedPtr<FMCTGameThreadDispatchTask> Command;
		{
			FScopeLock Lock(&QueueCriticalSection);
			if (!PendingCommands.Peek(Command))
			{
				return true;
			}
		}

		if (!Command.IsValid())
		{
			FScopeLock Lock(&QueueCriticalSection);
			PendingCommands.Pop();
			continue;
		}

		const double CurrentSeconds = FPlatformTime::Seconds();
		if (Command->HasDeadlineExpired(CurrentSeconds))
		{
			Command->CancelPending();
			{
				FScopeLock Lock(&QueueCriticalSection);
				PendingCommands.Pop();
			}
			UE_LOG(
				LogMCT,
				Verbose,
				TEXT("Game Thread command %llu timed out in queue after %.2f ms"),
				Command->TaskId,
				(CurrentSeconds - Command->EnqueuedAtSeconds) * 1000.0);
			continue;
		}

		if (!Command->IsPending())
		{
			FScopeLock Lock(&QueueCriticalSection);
			PendingCommands.Pop();
			continue;
		}

		const FMCTGameThreadSafetyState SafetyState = GetSafetyState();
		if (SafetyState.bEditorShuttingDown)
		{
			BeginShutdown();
			return true;
		}

		if (SafetyState.bAssetStreamingSuspended || SafetyState.bGarbageCollecting)
		{
			const TCHAR* DeferralReason = SafetyState.bAssetStreamingSuspended
				? TEXT("asset streaming suspended")
				: TEXT("garbage collection active");
			UE_LOG(
				LogMCT,
				Verbose,
				TEXT("Game Thread command %llu deferred for %.2f ms: %s"),
				Command->TaskId,
				(CurrentSeconds - Command->EnqueuedAtSeconds) * 1000.0,
				DeferralReason);
			return true;
		}

		{
			FScopeLock Lock(&QueueCriticalSection);
			PendingCommands.Pop();
		}

		if (!Command->TryStart())
		{
			continue;
		}

		UE_LOG(
			LogMCT,
			Verbose,
			TEXT("Game Thread command %llu starting after %.2f ms in queue"),
			Command->TaskId,
			(CurrentSeconds - Command->EnqueuedAtSeconds) * 1000.0);

		Command->Work();
		Command->Complete();

		// Starting exactly one command is the per-frame throughput contract.
		return true;
	}
}

FMCTGameThreadSafetyState FMCTGameThreadDispatcher::GetSafetyState() const
{
	if (SafetyStateProvider)
	{
		return SafetyStateProvider();
	}

	FMCTGameThreadSafetyState State;
	State.bAssetStreamingSuspended = IsAssetStreamingSuspended();
	State.bGarbageCollecting = IsGarbageCollecting();
	State.bEditorShuttingDown = bShuttingDown.Load() || IsEngineExitRequested();
	return State;
}

#if WITH_DEV_AUTOMATION_TESTS
bool FMCTGameThreadDispatcher::TickForTesting(const float DeltaTime)
{
	return Tick(DeltaTime);
}
#endif
