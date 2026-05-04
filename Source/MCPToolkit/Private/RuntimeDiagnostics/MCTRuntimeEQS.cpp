// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "RuntimeDiagnostics/MCTRuntimeEQS.h"

#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectIterator.h"

namespace MCPToolkit::RuntimeDiagnostics
{
namespace
{
FString EnvQueryStatusToString(EEnvQueryStatus::Type Status)
{
	switch (Status)
	{
	case EEnvQueryStatus::Processing:
		return TEXT("Processing");
	case EEnvQueryStatus::Success:
		return TEXT("Success");
	case EEnvQueryStatus::Failed:
		return TEXT("Failed");
	case EEnvQueryStatus::Aborted:
		return TEXT("Aborted");
	case EEnvQueryStatus::OwnerLost:
		return TEXT("OwnerLost");
	case EEnvQueryStatus::MissingParam:
		return TEXT("MissingParam");
	default:
		return FString::Printf(TEXT("EnvQueryStatus_%d"), static_cast<int32>(Status));
	}
}

FString EnvQueryRunModeToString(EEnvQueryRunMode::Type RunMode)
{
	switch (RunMode)
	{
	case EEnvQueryRunMode::SingleResult:
		return TEXT("SingleResult");
	case EEnvQueryRunMode::RandomBest5Pct:
		return TEXT("RandomBest5Pct");
	case EEnvQueryRunMode::RandomBest25Pct:
		return TEXT("RandomBest25Pct");
	case EEnvQueryRunMode::AllMatching:
		return TEXT("AllMatching");
	default:
		return FString::Printf(TEXT("EnvQueryRunMode_%d"), static_cast<int32>(RunMode));
	}
}

TSharedPtr<FJsonObject> BuildEQSManagerJson(UEnvQueryManager* Manager)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Manager);
	if (!Manager)
	{
		return Data;
	}

	Data->SetBoolField(TEXT("tickable_in_editor"), Manager->IsTickableInEditor());

	TSharedPtr<FJsonObject> ConfigJson = MakeShared<FJsonObject>();
	AddReflectedSettingsPropertyJson(ConfigJson, Manager, TEXT("MaxAllowedTestingTime"), TEXT("max_allowed_testing_time"));
	AddReflectedSettingsPropertyJson(ConfigJson, Manager, TEXT("bTestQueriesUsingBreadth"), TEXT("test_queries_using_breadth"));
	AddReflectedSettingsPropertyJson(ConfigJson, Manager, TEXT("QueryCountWarningThreshold"), TEXT("query_count_warning_threshold"));
	AddReflectedSettingsPropertyJson(ConfigJson, Manager, TEXT("QueryCountWarningInterval"), TEXT("query_count_warning_interval"));
	AddReflectedSettingsPropertyJson(ConfigJson, Manager, TEXT("ExecutionTimeWarningSeconds"), TEXT("execution_time_warning_seconds"));
	AddReflectedSettingsPropertyJson(ConfigJson, Manager, TEXT("HandlingResultTimeWarningSeconds"), TEXT("handling_result_time_warning_seconds"));
	AddReflectedSettingsPropertyJson(ConfigJson, Manager, TEXT("GenerationTimeWarningSeconds"), TEXT("generation_time_warning_seconds"));
	Data->SetObjectField(TEXT("config"), ConfigJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildEnvQueryResultSummaryJson(const FEnvQueryResult* Result)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Result != nullptr);
	if (!Result)
	{
		return Data;
	}

	int32 ValidItemCount = 0;
	int32 DiscardedItemCount = 0;
	for (const FEnvQueryItem& Item : Result->Items)
	{
		if (Item.IsValid())
		{
			++ValidItemCount;
		}
		if (Item.bIsDiscarded)
		{
			++DiscardedItemCount;
		}
	}

	const EEnvQueryStatus::Type Status = Result->GetRawStatus();
	Data->SetStringField(TEXT("status"), EnvQueryStatusToString(Status));
	Data->SetNumberField(TEXT("status_value"), static_cast<int32>(Status));
	Data->SetBoolField(TEXT("finished"), Result->IsFinished());
	Data->SetBoolField(TEXT("successful"), Result->IsSuccessful());
	Data->SetBoolField(TEXT("aborted"), Result->IsAborted());
	Data->SetNumberField(TEXT("query_id"), Result->QueryID);
	Data->SetNumberField(TEXT("option_index"), Result->OptionIndex);
	Data->SetObjectField(TEXT("owner"), BuildObjectReferenceJson(Result->Owner.Get()));
	Data->SetObjectField(TEXT("item_type"), BuildObjectReferenceJson(Result->ItemType.Get()));
	Data->SetNumberField(TEXT("item_count"), Result->Items.Num());
	Data->SetNumberField(TEXT("valid_item_count"), ValidItemCount);
	Data->SetNumberField(TEXT("discarded_item_count"), DiscardedItemCount);
	Data->SetNumberField(TEXT("raw_data_bytes"), Result->RawData.Num());
	return Data;
}

TSharedPtr<FJsonObject> BuildEnvQueryInstanceSummaryJson(const FEnvQueryInstance* Instance, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = BuildEnvQueryResultSummaryJson(Instance);
	Data->SetBoolField(TEXT("present"), Instance != nullptr);
	if (!Instance)
	{
		return Data;
	}

	Data->SetStringField(TEXT("query_name"), Instance->QueryName);
	Data->SetStringField(TEXT("unique_name"), Instance->UniqueName.ToString());
	Data->SetStringField(TEXT("run_mode"), EnvQueryRunModeToString(Instance->Mode));
	Data->SetObjectField(TEXT("world"), BuildObjectReferenceJson(Instance->World.Get()));
	Data->SetObjectField(TEXT("owner"), BuildObjectReferenceJson(Instance->Owner.Get()));
	Data->SetNumberField(TEXT("current_test"), Instance->CurrentTest);
	Data->SetNumberField(TEXT("current_test_starting_item"), Instance->CurrentTestStartingItem);
	Data->SetNumberField(TEXT("num_valid_items"), Instance->NumValidItems);
	Data->SetNumberField(TEXT("value_size"), Instance->ValueSize);
	Data->SetBoolField(TEXT("found_single_result"), Instance->bFoundSingleResult != 0);
	Data->SetBoolField(TEXT("pass_on_single_result"), Instance->bPassOnSingleResult != 0);
	Data->SetBoolField(TEXT("has_logged_time_limit_warning"), Instance->bHasLoggedTimeLimitWarning != 0);
	Data->SetBoolField(TEXT("currently_running_async"), Instance->IsCurrentlyRunningAsync());
	Data->SetNumberField(TEXT("start_time"), Instance->StartTime);
	Data->SetNumberField(TEXT("total_execution_time"), Instance->TotalExecutionTime);
	Data->SetStringField(TEXT("execution_time_description"), TruncateString(Instance->GetExecutionTimeDescription(), StringLimit));
	return Data;
}

bool WrapperBelongsToWorld(const UEnvQueryInstanceBlueprintWrapper* Wrapper, const UEnvQueryManager* Manager, const UWorld* World)
{
	if (!Wrapper)
	{
		return false;
	}
	if (Manager && Wrapper->GetOuter() == Manager)
	{
		return true;
	}
	if (const FEnvQueryInstance* Instance = Wrapper->GetQueryInstance())
	{
		return Instance->World.Get() == World;
	}
	return Wrapper->GetWorld() == World;
}

bool WrapperMatchesFilters(const UEnvQueryInstanceBlueprintWrapper* Wrapper, const FString& NameFilter, const FString& ClassFilter)
{
	if (!Wrapper)
	{
		return false;
	}

	const FEnvQueryResult* Result = Wrapper->GetQueryResult();
	const FEnvQueryInstance* Instance = Wrapper->GetQueryInstance();
	const UObject* Owner = Instance ? Instance->Owner.Get() : (Result ? Result->Owner.Get() : nullptr);
	const UClass* ItemTypeClass = Instance ? Instance->ItemType.Get() : (Result ? Result->ItemType.Get() : nullptr);

	const FString NameText = FString::Printf(
		TEXT("%s %s %s %s %s %s"),
		*Wrapper->GetName(),
		*Wrapper->GetPathName(),
		Instance ? *Instance->QueryName : TEXT(""),
		Instance ? *Instance->UniqueName.ToString() : TEXT(""),
		Owner ? *Owner->GetName() : TEXT(""),
		Owner ? *Owner->GetPathName() : TEXT(""));
	const FString ClassText = FString::Printf(
		TEXT("%s %s %s"),
		Wrapper->GetClass() ? *Wrapper->GetClass()->GetPathName() : TEXT(""),
		ItemTypeClass ? *ItemTypeClass->GetPathName() : TEXT(""),
		(Owner && Owner->GetClass()) ? *Owner->GetClass()->GetPathName() : TEXT(""));

	if (!NameFilter.IsEmpty() && !NameText.Contains(NameFilter))
	{
		return false;
	}
	if (!ClassFilter.IsEmpty() && !ClassText.Contains(ClassFilter))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> BuildWrapperJson(UEnvQueryInstanceBlueprintWrapper* Wrapper, int32 StringLimit)
{
	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Wrapper);
	if (!Wrapper)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("outer"), BuildObjectReferenceJson(Wrapper->GetOuter()));
	Data->SetStringField(TEXT("run_mode"), EnvQueryRunModeToString(Wrapper->GetRunMode()));
	Data->SetBoolField(TEXT("query_result_present"), Wrapper->GetQueryResult() != nullptr);
	Data->SetBoolField(TEXT("query_instance_present"), Wrapper->GetQueryInstance() != nullptr);
	Data->SetObjectField(TEXT("query_result"), BuildEnvQueryResultSummaryJson(Wrapper->GetQueryResult()));
	Data->SetObjectField(TEXT("query_instance"), BuildEnvQueryInstanceSummaryJson(Wrapper->GetQueryInstance(), StringLimit));
	return Data;
}
}

TSharedPtr<FJsonObject> BuildEQSDiagnostics(TSharedPtr<FJsonObject> Params)
{
	const FString WorldSelector = ReadLowerStringField(Params, TEXT("world"), TEXT("auto"));
	FString NameFilter;
	FString ClassFilter;
	bool bIncludeRegisteredItemTypes = true;
	bool bIncludeWrappers = true;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("name_filter"), NameFilter);
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Params->TryGetBoolField(TEXT("include_registered_item_types"), bIncludeRegisteredItemTypes);
		Params->TryGetBoolField(TEXT("include_wrappers"), bIncludeWrappers);
	}
	NameFilter.TrimStartAndEndInline();
	ClassFilter.TrimStartAndEndInline();

	const int32 RegisteredItemTypeLimit = ReadClampedIntField(Params, TEXT("registered_item_type_limit"), 100, 0, 1000);
	const int32 WrapperLimit = ReadClampedIntField(Params, TEXT("wrapper_limit"), 100, 0, 1000);
	const int32 DebugStringLimit = ReadClampedIntField(Params, TEXT("debug_string_limit"), 1000, 0, 20000);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("requested_world"), WorldSelector);
	Data->SetObjectField(TEXT("pie"), BuildPIEStateJson());
	Data->SetStringField(TEXT("name_filter"), NameFilter);
	Data->SetStringField(TEXT("class_filter"), ClassFilter);
	Data->SetBoolField(TEXT("include_registered_item_types"), bIncludeRegisteredItemTypes);
	Data->SetBoolField(TEXT("include_wrappers"), bIncludeWrappers);
	Data->SetNumberField(TEXT("registered_item_type_limit"), RegisteredItemTypeLimit);
	Data->SetNumberField(TEXT("wrapper_limit"), WrapperLimit);
	Data->SetNumberField(TEXT("debug_string_limit"), DebugStringLimit);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	FString WorldSource;
	UWorld* World = SelectWorld(WorldSelector, WorldSource);
	Data->SetStringField(TEXT("world_source"), WorldSource);
	Data->SetBoolField(TEXT("world_available"), World != nullptr);
	if (!World)
	{
		Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("No %s world is available"), *WorldSelector)));
		Data->SetArrayField(TEXT("warnings"), Warnings);
		return Data;
	}

	if (WorldSource == TEXT("editor") && (WorldSelector == TEXT("auto") || WorldSelector == TEXT("pie") || WorldSelector == TEXT("runtime") || WorldSelector == TEXT("play")))
	{
		Warnings.Add(MakeShared<FJsonValueString>(TEXT("PIE is inactive; EQS diagnostics reflect the editor world")));
	}

	Data->SetObjectField(TEXT("world"), BuildWorldJson(World, WorldSource));

	UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(World);
	Data->SetBoolField(TEXT("eqs_manager_available"), Manager != nullptr);
	Data->SetObjectField(TEXT("eqs_manager"), BuildEQSManagerJson(Manager));

	TArray<TSharedPtr<FJsonValue>> RegisteredItemTypesJson;
	if (bIncludeRegisteredItemTypes)
	{
		for (const TSubclassOf<UEnvQueryItemType>& ItemType : UEnvQueryManager::RegisteredItemTypes)
		{
			if (RegisteredItemTypesJson.Num() >= RegisteredItemTypeLimit)
			{
				break;
			}
			RegisteredItemTypesJson.Add(MakeShared<FJsonValueObject>(BuildObjectReferenceJson(ItemType.Get())));
		}
	}
	Data->SetNumberField(TEXT("registered_item_type_count"), UEnvQueryManager::RegisteredItemTypes.Num());
	Data->SetNumberField(TEXT("returned_registered_item_type_count"), RegisteredItemTypesJson.Num());
	Data->SetBoolField(TEXT("registered_item_types_truncated"), bIncludeRegisteredItemTypes && UEnvQueryManager::RegisteredItemTypes.Num() > RegisteredItemTypesJson.Num());
	Data->SetArrayField(TEXT("registered_item_types"), RegisteredItemTypesJson);

	int32 MatchedWrapperCount = 0;
	int32 RunningWrapperCount = 0;
	int32 FinishedWrapperCount = 0;
	int32 SuccessfulWrapperCount = 0;
	TArray<TSharedPtr<FJsonValue>> WrappersJson;
	for (TObjectIterator<UEnvQueryInstanceBlueprintWrapper> It; It; ++It)
	{
		UEnvQueryInstanceBlueprintWrapper* Wrapper = *It;
		if (!Wrapper || !WrapperBelongsToWorld(Wrapper, Manager, World) || !WrapperMatchesFilters(Wrapper, NameFilter, ClassFilter))
		{
			continue;
		}

		++MatchedWrapperCount;
		const FEnvQueryInstance* Instance = Wrapper->GetQueryInstance();
		const FEnvQueryResult* Result = Wrapper->GetQueryResult();
		if (Instance)
		{
			++RunningWrapperCount;
		}
		if (Result && Result->IsFinished())
		{
			++FinishedWrapperCount;
		}
		if (Result && Result->IsSuccessful())
		{
			++SuccessfulWrapperCount;
		}

		if (bIncludeWrappers && WrappersJson.Num() < WrapperLimit)
		{
			WrappersJson.Add(MakeShared<FJsonValueObject>(BuildWrapperJson(Wrapper, DebugStringLimit)));
		}
	}

	Data->SetNumberField(TEXT("matched_wrapper_count"), MatchedWrapperCount);
	Data->SetNumberField(TEXT("returned_wrapper_count"), WrappersJson.Num());
	Data->SetBoolField(TEXT("wrappers_truncated"), bIncludeWrappers && MatchedWrapperCount > WrappersJson.Num());
	Data->SetNumberField(TEXT("running_wrapper_count"), RunningWrapperCount);
	Data->SetNumberField(TEXT("finished_wrapper_count"), FinishedWrapperCount);
	Data->SetNumberField(TEXT("successful_wrapper_count"), SuccessfulWrapperCount);
	Data->SetArrayField(TEXT("wrappers"), WrappersJson);
	Data->SetArrayField(TEXT("warnings"), Warnings);
	return Data;
}
}
