// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandDispatch/MCTGameThreadDispatcher.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CommandHandlers/MCTCommandResponse.h"
#include "Misc/AutomationTest.h"

namespace
{
TSharedPtr<TPromise<FString>> MakePromise(TFuture<FString>& OutFuture)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	OutFuture = Promise->GetFuture();
	return Promise;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCTGameThreadDispatcherSafetyGateTest,
	"MCPToolkit.GameThreadDispatcher.SafetyGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCTGameThreadDispatcherSafetyGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedRef<FMCTGameThreadSafetyState> SafetyState = MakeShared<FMCTGameThreadSafetyState>();
	SafetyState->bAssetStreamingSuspended = true;

	FMCTGameThreadDispatcher Dispatcher([SafetyState]()
	{
		return *SafetyState;
	});
	Dispatcher.Startup(false);

	int32 ExecutionCount = 0;
	TFuture<FString> Future;
	const TSharedPtr<TPromise<FString>> Promise = MakePromise(Future);
	Dispatcher.Enqueue(Promise, [Promise, &ExecutionCount]()
	{
		++ExecutionCount;
		Promise->SetValue(TEXT("executed"));
	});

	Dispatcher.TickForTesting();
	TestEqual(TEXT("Suspended streaming defers the command"), ExecutionCount, 0);

	SafetyState->bAssetStreamingSuspended = false;
	SafetyState->bGarbageCollecting = true;
	Dispatcher.TickForTesting();
	TestEqual(TEXT("Garbage collection also defers the command"), ExecutionCount, 0);

	SafetyState->bGarbageCollecting = false;
	Dispatcher.TickForTesting();
	TestEqual(TEXT("The command executes once after both gates clear"), ExecutionCount, 1);
	TestTrue(TEXT("Successful execution resolves its promise"), Future.IsReady());

	Dispatcher.TickForTesting();
	TestEqual(TEXT("A completed command is never repeated"), ExecutionCount, 1);

	Dispatcher.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCTGameThreadDispatcherFifoTest,
	"MCPToolkit.GameThreadDispatcher.FifoAndOnePerFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCTGameThreadDispatcherFifoTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FMCTGameThreadDispatcher Dispatcher([]()
	{
		return FMCTGameThreadSafetyState();
	});
	Dispatcher.Startup(false);

	TArray<int32> ExecutionOrder;
	TArray<TFuture<FString>> Futures;
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		TFuture<FString> Future;
		const TSharedPtr<TPromise<FString>> Promise = MakePromise(Future);
		Dispatcher.Enqueue(Promise, [Promise, &ExecutionOrder, Index]()
		{
			ExecutionOrder.Add(Index);
			Promise->SetValue(FString::FromInt(Index));
		});
		Futures.Add(MoveTemp(Future));
	}

	Dispatcher.TickForTesting();
	TestEqual(TEXT("First frame starts exactly one command"), ExecutionOrder.Num(), 1);
	TestEqual(TEXT("First command is FIFO head"), ExecutionOrder[0], 1);

	Dispatcher.TickForTesting();
	TestEqual(TEXT("Second frame starts exactly one more command"), ExecutionOrder.Num(), 2);
	TestEqual(TEXT("Second command preserves FIFO order"), ExecutionOrder[1], 2);

	Dispatcher.TickForTesting();
	TestEqual(TEXT("Third frame starts the final command"), ExecutionOrder.Num(), 3);
	TestEqual(TEXT("Third command completes FIFO order"), ExecutionOrder[2], 3);
	TestTrue(TEXT("First promise completed"), Futures[0].IsReady());
	TestTrue(TEXT("Second promise completed"), Futures[1].IsReady());
	TestTrue(TEXT("Third promise completed"), Futures[2].IsReady());

	Dispatcher.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCTGameThreadDispatcherCancellationTest,
	"MCPToolkit.GameThreadDispatcher.TimeoutCancellationAndShutdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCTGameThreadDispatcherCancellationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FMCTGameThreadDispatcher Dispatcher([]()
	{
		return FMCTGameThreadSafetyState();
	});
	Dispatcher.Startup(false);

	int32 MutationCount = 0;

	TFuture<FString> TimeoutFuture;
	const TSharedPtr<TPromise<FString>> TimeoutPromise = MakePromise(TimeoutFuture);
	const FMCTGameThreadDispatchHandle TimeoutHandle = Dispatcher.Enqueue(
		TimeoutPromise,
		[TimeoutPromise, &MutationCount]()
		{
			++MutationCount;
			TimeoutPromise->SetValue(TEXT("unexpected"));
		});
	TimeoutHandle.WaitFor(TimeoutFuture, FTimespan::Zero());
	TestTrue(TEXT("A wait timeout cancels pending work"), TimeoutHandle.IsCancelled());
	TestTrue(TEXT("A wait timeout resolves its promise"), TimeoutFuture.IsReady());
	Dispatcher.TickForTesting();
	TestEqual(TEXT("Timed-out work cannot mutate"), MutationCount, 0);

	TFuture<FString> CancelFuture;
	const TSharedPtr<TPromise<FString>> CancelPromise = MakePromise(CancelFuture);
	const FMCTGameThreadDispatchHandle CancelHandle = Dispatcher.Enqueue(
		CancelPromise,
		[CancelPromise, &MutationCount]()
		{
			++MutationCount;
			CancelPromise->SetValue(TEXT("unexpected"));
		});
	TestTrue(TEXT("Explicit cancellation succeeds while pending"), CancelHandle.Cancel());
	TestTrue(TEXT("Explicit cancellation resolves its promise"), CancelFuture.IsReady());
	Dispatcher.TickForTesting();
	TestEqual(TEXT("Explicitly cancelled work cannot mutate"), MutationCount, 0);

	TFuture<FString> ShutdownFuture;
	const TSharedPtr<TPromise<FString>> ShutdownPromise = MakePromise(ShutdownFuture);
	Dispatcher.Enqueue(ShutdownPromise, [ShutdownPromise, &MutationCount]()
	{
		++MutationCount;
		ShutdownPromise->SetValue(TEXT("unexpected"));
	});

	Dispatcher.BeginShutdown();
	TestTrue(TEXT("Shutdown resolves a queued promise"), ShutdownFuture.IsReady());
	if (ShutdownFuture.IsReady())
	{
		TestEqual(
			TEXT("Shutdown returns the standard editor-shutdown JSON error"),
			ShutdownFuture.Get(),
			MCPToolkit::CommandHandlers::CreateErrorResponse(TEXT("editor shutting down")));
	}
	TestEqual(TEXT("Shutdown cancellation cannot mutate"), MutationCount, 0);

	TFuture<FString> RejectedFuture;
	const TSharedPtr<TPromise<FString>> RejectedPromise = MakePromise(RejectedFuture);
	Dispatcher.Enqueue(RejectedPromise, [RejectedPromise, &MutationCount]()
	{
		++MutationCount;
		RejectedPromise->SetValue(TEXT("unexpected"));
	});
	TestTrue(TEXT("New work is rejected after shutdown begins"), RejectedFuture.IsReady());
	TestEqual(TEXT("Rejected work cannot mutate"), MutationCount, 0);

	Dispatcher.Shutdown();
	return true;
}

#endif
