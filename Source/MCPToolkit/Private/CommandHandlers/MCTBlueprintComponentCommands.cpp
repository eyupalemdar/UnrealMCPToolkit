// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTBlueprintComponentCommands.h"

#include "Builders/MCTBlueprintComponentBuilder.h"
#include "CommandHandlers/MCTCommandResponse.h"

#include "Async/Async.h"

namespace MCPToolkit::CommandHandlers::BlueprintComponent
{
namespace
{
FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName)
{
	FString Value;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
		Value.TrimStartAndEndInline();
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, bool bDefault)
{
	bool bValue = bDefault;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

FString CreateBuilderErrorResponse(const FString& Error, const TCHAR* Fallback)
{
	return CreateErrorResponse(Error.IsEmpty() ? FString(Fallback) : Error);
}

FString RunOnGameThread(TFunction<FString()>&& Work, const TCHAR* TimeoutError)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Promise, Work = MoveTemp(Work)]()
	{
		Promise->SetValue(Work());
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady())
	{
		return CreateErrorResponse(TimeoutError);
	}
	return Future.Get();
}
}

FString HandleBlueprintComponentList(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	return RunOnGameThread([AssetPath]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTBlueprintComponentBuilder::ListComponents(AssetPath, Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to list Blueprint components"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component list timed out"));
}

FString HandleBlueprintComponentAdd(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const FString ComponentClassName = ReadStringField(Params, TEXT("component_class"));
	const FString ParentComponentName = ReadStringField(Params, TEXT("parent_component_name"));
	const bool bCompileBlueprint = ReadBoolField(Params, TEXT("compile_blueprint"), true);
	const bool bSaveAsset = ReadBoolField(Params, TEXT("save_asset"), false);

	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (ComponentName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	return RunOnGameThread([AssetPath, ComponentName, ComponentClassName, ParentComponentName, bCompileBlueprint, bSaveAsset]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTBlueprintComponentBuilder::AddComponent(
			AssetPath,
			ComponentName,
			ComponentClassName,
			ParentComponentName,
			bCompileBlueprint,
			bSaveAsset,
			Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to add Blueprint component"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component add timed out"));
}

FString HandleBlueprintComponentRemove(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const bool bPromoteChildren = ReadBoolField(Params, TEXT("promote_children"), true);
	const bool bCompileBlueprint = ReadBoolField(Params, TEXT("compile_blueprint"), true);
	const bool bSaveAsset = ReadBoolField(Params, TEXT("save_asset"), false);

	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (ComponentName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	return RunOnGameThread([AssetPath, ComponentName, bPromoteChildren, bCompileBlueprint, bSaveAsset]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTBlueprintComponentBuilder::RemoveComponent(
			AssetPath,
			ComponentName,
			bPromoteChildren,
			bCompileBlueprint,
			bSaveAsset,
			Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to remove Blueprint component"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component remove timed out"));
}

FString HandleBlueprintComponentSetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'params' object"));
	}

	const FString AssetPath = ReadStringField(Params, TEXT("asset_path"));
	const FString ComponentName = ReadStringField(Params, TEXT("component_name"));
	const FString PropertyPath = ReadStringField(Params, TEXT("property_path"));
	const FString Value = ReadStringField(Params, TEXT("value"));
	const bool bCompileBlueprint = ReadBoolField(Params, TEXT("compile_blueprint"), false);
	const bool bSaveAsset = ReadBoolField(Params, TEXT("save_asset"), false);

	if (AssetPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	if (ComponentName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}
	if (PropertyPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'property_path' parameter"));
	}

	return RunOnGameThread([AssetPath, ComponentName, PropertyPath, Value, bCompileBlueprint, bSaveAsset]()
	{
		FString Error;
		TSharedPtr<FJsonObject> Data = UMCTBlueprintComponentBuilder::SetComponentProperty(
			AssetPath,
			ComponentName,
			PropertyPath,
			Value,
			bCompileBlueprint,
			bSaveAsset,
			Error);
		if (!Data.IsValid())
		{
			return CreateBuilderErrorResponse(Error, TEXT("Failed to set Blueprint component property"));
		}
		return CreateSuccessResponse(Data);
	}, TEXT("Blueprint component set property timed out"));
}
}
