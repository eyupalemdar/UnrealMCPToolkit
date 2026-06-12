// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandDispatch/MCTCommandDispatch.h"

class FJsonObject;

namespace MCPToolkit::CommandDispatch
{
using FMCTRegisteredCommandHandler = TFunction<FString(TSharedPtr<FJsonObject> Params)>;

/**
 * Process-local extension registry for editor TCP commands owned by other
 * plugins. MCPToolkit remains the transport, scope, dry-run, and serialization
 * gate; extension plugins own their command handlers.
 */
class MCPTOOLKIT_API FMCTCommandRegistry
{
public:
	static bool RegisterCommand(const FMCTCommandDescriptor& Descriptor, FMCTRegisteredCommandHandler Handler, FString& OutError);
	static void UnregisterCommand(const FString& CommandName);
	static bool FindCommand(const FString& CommandName, FMCTCommandDescriptor& OutDescriptor);
	static bool ExecuteCommand(const FString& CommandName, TSharedPtr<FJsonObject> Params, FString& OutResponse);
	static TArray<FMCTCommandDescriptor> ListCommands();
};
}
