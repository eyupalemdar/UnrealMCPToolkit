// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"

struct FMCTGameThreadDispatchTask;

/**
 * Snapshot of the editor conditions that gate remote command execution.
 *
 * The provider is injectable so the queue policy can be exercised by
 * automation tests without mutating global streaming or GC state.
 */
struct FMCTGameThreadSafetyState
{
	bool bAssetStreamingSuspended = false;
	bool bGarbageCollecting = false;
	bool bEditorShuttingDown = false;
};

/**
 * A caller-side reference to one queued Game Thread command.
 *
 * WaitFor mirrors the existing handler wait contract while also publishing the
 * same deadline to the dispatcher. If the deadline expires before the command
 * starts, the queued work is cancelled, its promise is completed with an error,
 * and it can no longer mutate editor state. Work that has already started is
 * never interrupted.
 */
class MCPTOOLKIT_API FMCTGameThreadDispatchHandle
{
public:
	FMCTGameThreadDispatchHandle() = default;

	template<typename ResultType>
	void WaitFor(TFuture<ResultType>& Future, const FTimespan& Timeout) const
	{
		SetDeadline(Timeout);
		if (!Future.WaitFor(Timeout))
		{
			// A TPromise must be completed before its final owner releases it.
			// Cancellation therefore resolves the promise as a timeout while
			// guaranteeing pending work cannot run.
			Cancel();
		}
	}

	bool Cancel() const;
	bool IsValid() const;
	bool IsPending() const;
	bool IsCancelled() const;

private:
	friend class FMCTGameThreadDispatcher;

	explicit FMCTGameThreadDispatchHandle(TSharedPtr<FMCTGameThreadDispatchTask> InTask);
	void SetDeadline(const FTimespan& Timeout) const;

	TSharedPtr<FMCTGameThreadDispatchTask> Task;
};

/**
 * FIFO dispatcher for all TCP/HTTP command work that must touch the Game Thread.
 *
 * Producers may enqueue from multiple threads. The CoreTicker is the sole
 * consumer and starts at most one command per tick, after streaming and GC
 * safety gates have cleared.
 */
class MCPTOOLKIT_API FMCTGameThreadDispatcher
{
public:
	using FSafetyStateProvider = TFunction<FMCTGameThreadSafetyState()>;

	explicit FMCTGameThreadDispatcher(FSafetyStateProvider InSafetyStateProvider = FSafetyStateProvider());
	~FMCTGameThreadDispatcher();

	static FMCTGameThreadDispatcher& Get();

	/** Accept commands and optionally register the production CoreTicker. */
	void Startup(bool bRegisterTicker = true);

	/** Reject new work and resolve all queued promises as editor shutdown errors. */
	void BeginShutdown();

	/** Begin shutdown and unregister the CoreTicker. */
	void Shutdown();

	/**
	 * Queue one promise-backed Game Thread command.
	 *
	 * The command body owns its normal promise completion, preserving all
	 * existing handler responses. The dispatcher only completes the promise
	 * itself when timeout, explicit cancellation, or editor shutdown cancels
	 * work before it starts.
	 */
	FMCTGameThreadDispatchHandle Enqueue(
		const TSharedPtr<TPromise<FString>>& CompletionPromise,
		TFunction<void()>&& Work);

	/**
	 * Typed variant for the small number of handlers whose internal promise is
	 * not the final JSON string. The result factory is used only when shutdown
	 * cancels the command before it starts.
	 */
	template<
		typename ResultType,
		typename TimeoutResultFactoryType,
		typename ShutdownResultFactoryType>
	FMCTGameThreadDispatchHandle Enqueue(
		const TSharedPtr<TPromise<ResultType>>& CompletionPromise,
		TFunction<void()>&& Work,
		TimeoutResultFactoryType&& TimeoutResultFactory,
		ShutdownResultFactoryType&& ShutdownResultFactory)
	{
		TFunction<void()> TimeoutCompletion =
			[CompletionPromise, Factory = Forward<TimeoutResultFactoryType>(TimeoutResultFactory)]() mutable
			{
				CompletionPromise->SetValue(Factory());
			};
		TFunction<void()> ShutdownCompletion =
			[CompletionPromise, Factory = Forward<ShutdownResultFactoryType>(ShutdownResultFactory)]() mutable
			{
				CompletionPromise->SetValue(Factory());
			};
		return EnqueueWithCompletions(
			MoveTemp(Work),
			MoveTemp(TimeoutCompletion),
			MoveTemp(ShutdownCompletion));
	}

#if WITH_DEV_AUTOMATION_TESTS
	/** Treat one call as one frame for deterministic queue-policy tests. */
	bool TickForTesting(float DeltaTime = 0.0f);
#endif

private:
	FMCTGameThreadDispatchHandle EnqueueWithCompletions(
		TFunction<void()>&& Work,
		TFunction<void()>&& TimeoutCompletion,
		TFunction<void()>&& ShutdownCompletion);

	bool Tick(float DeltaTime);
	FMCTGameThreadSafetyState GetSafetyState() const;

	FSafetyStateProvider SafetyStateProvider;
	TQueue<TSharedPtr<FMCTGameThreadDispatchTask>, EQueueMode::Mpsc> PendingCommands;
	mutable FCriticalSection QueueCriticalSection;
	FTSTicker::FDelegateHandle TickerHandle;
	uint64 NextTaskId = 1;
	bool bAcceptingCommands = false;
	TAtomic<bool> bShuttingDown{false};
};
