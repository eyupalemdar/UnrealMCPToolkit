// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandDispatch/MCTCommandRegistry.h"
#include "MCTModule.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace MCPToolkit::CommandDispatch
{
namespace
{
struct FMCTRegisteredCommandEntry
{
	FMCTCommandDescriptor Descriptor;
	TSharedPtr<FMCTRegisteredCommandHandler> Handler;
};

FCriticalSection RegistryCriticalSection;
TMap<FString, FMCTRegisteredCommandEntry> RegisteredCommands;

FString CommandKey(const FString& CommandName)
{
	return CommandName.ToLower();
}
}

bool FMCTCommandRegistry::RegisterCommand(
	const FMCTCommandDescriptor& Descriptor,
	FMCTRegisteredCommandHandler Handler,
	FString& OutError)
{
	if (Descriptor.Name.IsEmpty())
	{
		OutError = TEXT("Command descriptor name is required");
		return false;
	}

	if (!Handler)
	{
		OutError = FString::Printf(TEXT("Command '%s' handler is not bound"), *Descriptor.Name);
		return false;
	}

	FScopeLock Lock(&RegistryCriticalSection);
	const FString Key = CommandKey(Descriptor.Name);
	if (RegisteredCommands.Contains(Key))
	{
		OutError = FString::Printf(TEXT("Command '%s' is already registered"), *Descriptor.Name);
		return false;
	}

	FMCTRegisteredCommandEntry Entry;
	Entry.Descriptor = Descriptor;
	Entry.Descriptor.RequiredScope = Entry.Descriptor.RequiredScope.IsEmpty() ? TEXT("read") : Entry.Descriptor.RequiredScope.ToLower();
	Entry.Handler = MakeShared<FMCTRegisteredCommandHandler>(MoveTemp(Handler));
	RegisteredCommands.Add(Key, MoveTemp(Entry));

	UE_LOG(LogMCT, Log, TEXT("Registered external MCPToolkit command: %s"), *Descriptor.Name);
	return true;
}

void FMCTCommandRegistry::UnregisterCommand(const FString& CommandName)
{
	FScopeLock Lock(&RegistryCriticalSection);
	if (RegisteredCommands.Remove(CommandKey(CommandName)) > 0)
	{
		UE_LOG(LogMCT, Log, TEXT("Unregistered external MCPToolkit command: %s"), *CommandName);
	}
}

bool FMCTCommandRegistry::FindCommand(const FString& CommandName, FMCTCommandDescriptor& OutDescriptor)
{
	FScopeLock Lock(&RegistryCriticalSection);
	const FMCTRegisteredCommandEntry* Entry = RegisteredCommands.Find(CommandKey(CommandName));
	if (!Entry)
	{
		return false;
	}

	OutDescriptor = Entry->Descriptor;
	return true;
}

bool FMCTCommandRegistry::ExecuteCommand(const FString& CommandName, TSharedPtr<FJsonObject> Params, FString& OutResponse)
{
	TSharedPtr<FMCTRegisteredCommandHandler> Handler;
	{
		FScopeLock Lock(&RegistryCriticalSection);
		const FMCTRegisteredCommandEntry* Entry = RegisteredCommands.Find(CommandKey(CommandName));
		if (!Entry || !Entry->Handler.IsValid())
		{
			return false;
		}
		Handler = Entry->Handler;
	}

	OutResponse = (*Handler)(Params);
	return true;
}

TArray<FMCTCommandDescriptor> FMCTCommandRegistry::ListCommands()
{
	TArray<FMCTCommandDescriptor> Descriptors;
	FScopeLock Lock(&RegistryCriticalSection);
	for (const TPair<FString, FMCTRegisteredCommandEntry>& Pair : RegisteredCommands)
	{
		Descriptors.Add(Pair.Value.Descriptor);
	}
	return Descriptors;
}
}
