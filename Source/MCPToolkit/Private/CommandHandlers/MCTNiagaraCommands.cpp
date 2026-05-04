// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTNiagaraCommands.h"

#include "CommandHandlers/MCTCommandResponse.h"
#include "RuntimeDiagnostics/MCTRuntimeDiagnosticsUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ProfilingDebugging/ResourceSize.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace MCPToolkit::CommandHandlers::Niagara
{
namespace
{
struct FNiagaraInfoOptions
{
	bool bIncludeEmitters = true;
	bool bIncludeRenderers = true;
	bool bIncludeScripts = true;
	bool bIncludeParameters = true;
	bool bIncludeProperties = false;
	bool bIncludeAssetRegistry = true;
	int32 EmitterLimit = 64;
	int32 RendererLimit = 128;
	int32 ScriptLimit = 128;
	int32 ParameterLimit = 256;
	int32 AttributeLimit = 256;
	int32 DataInterfaceLimit = 128;
	int32 FunctionLimit = 128;
	int32 PropertyLimit = 80;
	int32 StringLimit = 512;
};

FString ReadStringField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const FString& DefaultValue = TEXT(""))
{
	FString Value = DefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetStringField(FieldName, Value);
		Value.TrimStartAndEndInline();
	}
	return Value;
}

bool ReadBoolField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const bool bDefaultValue)
{
	bool bValue = bDefaultValue;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(FieldName, bValue);
	}
	return bValue;
}

int32 ReadIntField(TSharedPtr<FJsonObject> Params, const TCHAR* FieldName, const int32 DefaultValue, const int32 MinValue, const int32 MaxValue)
{
	double NumberValue = static_cast<double>(DefaultValue);
	if (Params.IsValid())
	{
		Params->TryGetNumberField(FieldName, NumberValue);
	}
	return FMath::Clamp(FMath::RoundToInt(NumberValue), MinValue, MaxValue);
}

FString EnumValueToString(const UEnum* Enum, const int64 Value)
{
	return Enum ? Enum->GetNameStringByValue(Value) : FString::FromInt(static_cast<int32>(Value));
}

FString GuidToString(const FGuid& Guid)
{
	return Guid.IsValid() ? Guid.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
}

FString StripObjectPathDecorators(FString Value)
{
	Value.TrimStartAndEndInline();
	Value.TrimQuotesInline();

	int32 FirstQuote = INDEX_NONE;
	int32 LastQuote = INDEX_NONE;
	if (Value.FindChar(TEXT('\''), FirstQuote) && Value.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
	{
		Value = Value.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		Value.TrimStartAndEndInline();
		Value.TrimQuotesInline();
	}
	return Value;
}

FString ToObjectPath(const FString& AssetPath)
{
	FString CleanPath = StripObjectPathDecorators(AssetPath);
	if (CleanPath.Contains(TEXT(".")))
	{
		return CleanPath;
	}
	if (FPackageName::IsValidLongPackageName(CleanPath))
	{
		return CleanPath + TEXT(".") + FPackageName::GetLongPackageAssetName(CleanPath);
	}
	return CleanPath;
}

FName ToPackageName(const FString& AssetPath)
{
	FString PackageName = StripObjectPathDecorators(AssetPath);
	int32 DotIndex = INDEX_NONE;
	if (PackageName.FindChar(TEXT('.'), DotIndex))
	{
		PackageName = PackageName.Left(DotIndex);
	}
	return FName(*PackageName);
}

bool IsNiagaraAssetClass(const FTopLevelAssetPath& AssetClassPath)
{
	return AssetClassPath == UNiagaraSystem::StaticClass()->GetClassPathName()
		|| AssetClassPath == UNiagaraEmitter::StaticClass()->GetClassPathName()
		|| AssetClassPath == UNiagaraScript::StaticClass()->GetClassPathName();
}

TSharedPtr<FJsonObject> BuildAssetDataJson(const FAssetData& AssetData)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), AssetData.IsValid());
	if (!AssetData.IsValid())
	{
		return Data;
	}

	Data->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
	Data->SetStringField(TEXT("package_name"), AssetData.PackageName.ToString());
	Data->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
	Data->SetStringField(TEXT("class_path"), AssetData.AssetClassPath.ToString());
	Data->SetStringField(TEXT("object_path"), AssetData.GetSoftObjectPath().ToString());
	return Data;
}

FAssetData FindAssetData(const FString& AssetPath)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	const FString ObjectPath = ToObjectPath(AssetPath);
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
	if (AssetData.IsValid())
	{
		return AssetData;
	}

	TArray<FAssetData> PackageAssets;
	AssetRegistry.GetAssetsByPackageName(ToPackageName(AssetPath), PackageAssets);
	for (const FAssetData& PackageAsset : PackageAssets)
	{
		if (IsNiagaraAssetClass(PackageAsset.AssetClassPath))
		{
			return PackageAsset;
		}
	}
	return FAssetData();
}

UObject* LoadNiagaraAsset(const FString& AssetPath, FAssetData& OutAssetData, FString& OutResolvedObjectPath)
{
	OutAssetData = FindAssetData(AssetPath);
	if (OutAssetData.IsValid())
	{
		OutResolvedObjectPath = OutAssetData.GetSoftObjectPath().ToString();
		if (UObject* Asset = OutAssetData.GetAsset())
		{
			if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>())
			{
				return Asset;
			}
		}
	}

	OutResolvedObjectPath = ToObjectPath(AssetPath);
	if (UObject* Asset = LoadObject<UObject>(nullptr, *OutResolvedObjectPath))
	{
		if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>())
		{
			return Asset;
		}
	}

	const FString CleanPath = StripObjectPathDecorators(AssetPath);
	if (CleanPath != OutResolvedObjectPath)
	{
		OutResolvedObjectPath = CleanPath;
		if (UObject* Asset = LoadObject<UObject>(nullptr, *CleanPath))
		{
			if (Asset->IsA<UNiagaraSystem>() || Asset->IsA<UNiagaraEmitter>() || Asset->IsA<UNiagaraScript>())
			{
				return Asset;
			}
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> BuildResourceSizeJson(UObject* Object)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), Object != nullptr);
	if (!Object)
	{
		return Data;
	}

	FResourceSizeEx ResourceSize(EResourceSizeMode::EstimatedTotal);
	Object->GetResourceSizeEx(ResourceSize);
	Data->SetNumberField(TEXT("estimated_total_bytes"), static_cast<double>(ResourceSize.GetTotalMemoryBytes()));
	return Data;
}

bool TryReadBoolProperty(const UObject* Object, const FName PropertyName, bool& OutValue)
{
	if (!Object)
	{
		return false;
	}

	const FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName);
	if (!BoolProperty)
	{
		return false;
	}

	OutValue = BoolProperty->GetPropertyValue_InContainer(Object);
	return true;
}

TSharedPtr<FJsonObject> BuildVersionJson(const FNiagaraAssetVersion& Version)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("major"), Version.MajorVersion);
	Data->SetNumberField(TEXT("minor"), Version.MinorVersion);
	Data->SetStringField(TEXT("guid"), GuidToString(Version.VersionGuid));
	Data->SetBoolField(TEXT("visible_in_selector"), Version.bIsVisibleInVersionSelector);
	return Data;
}

TSharedPtr<FJsonObject> BuildVariableJson(const FNiagaraVariableBase& Variable)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Variable.GetName().ToString());
	Data->SetStringField(TEXT("type"), Variable.GetType().GetName());
	Data->SetNumberField(TEXT("size_bytes"), Variable.GetSizeInBytes());
	return Data;
}

template<typename TVariable>
TArray<TSharedPtr<FJsonValue>> BuildVariableArrayJsonImpl(TConstArrayView<TVariable> Variables, const int32 Limit, bool& bOutTruncated)
{
	TArray<TSharedPtr<FJsonValue>> VariablesJson;
	bOutTruncated = false;
	for (const TVariable& Variable : Variables)
	{
		if (VariablesJson.Num() >= Limit)
		{
			bOutTruncated = true;
			break;
		}
		VariablesJson.Add(MakeShared<FJsonValueObject>(BuildVariableJson(static_cast<const FNiagaraVariableBase&>(Variable))));
	}
	return VariablesJson;
}

TArray<TSharedPtr<FJsonValue>> BuildVariableArrayJson(TConstArrayView<FNiagaraVariableBase> Variables, const int32 Limit, bool& bOutTruncated)
{
	return BuildVariableArrayJsonImpl<FNiagaraVariableBase>(Variables, Limit, bOutTruncated);
}

TArray<TSharedPtr<FJsonValue>> BuildVariableArrayJson(TConstArrayView<FNiagaraVariable> Variables, const int32 Limit, bool& bOutTruncated)
{
	return BuildVariableArrayJsonImpl<FNiagaraVariable>(Variables, Limit, bOutTruncated);
}

TSharedPtr<FJsonObject> BuildParameterStoreJson(const FNiagaraParameterStore& Store, const int32 ParameterLimit)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	const TArrayView<const FNiagaraVariableWithOffset> Parameters = Store.ReadParameterVariables();
	Data->SetNumberField(TEXT("parameter_count"), Parameters.Num());
	Data->SetBoolField(TEXT("parameters_dirty"), Store.GetParametersDirty());
	Data->SetNumberField(TEXT("data_bytes"), Store.GetParameterDataArray().Num());
	Data->SetNumberField(TEXT("data_interface_count"), Store.GetDataInterfaces().Num());
	Data->SetNumberField(TEXT("uobject_count"), Store.GetUObjects().Num());

	TArray<TSharedPtr<FJsonValue>> ParametersJson;
	bool bTruncated = false;
	for (const FNiagaraVariableWithOffset& Parameter : Parameters)
	{
		if (ParametersJson.Num() >= ParameterLimit)
		{
			bTruncated = true;
			break;
		}
		TSharedPtr<FJsonObject> ParameterJson = BuildVariableJson(Parameter);
		ParameterJson->SetNumberField(TEXT("offset"), Parameter.Offset);
		ParametersJson.Add(MakeShared<FJsonValueObject>(ParameterJson));
	}
	Data->SetBoolField(TEXT("parameters_truncated"), bTruncated);
	Data->SetArrayField(TEXT("parameters"), ParametersJson);
	return Data;
}

TSharedPtr<FJsonObject> BuildUserParameterStoreJson(const FNiagaraUserRedirectionParameterStore& Store, const int32 ParameterLimit)
{
	TSharedPtr<FJsonObject> Data = BuildParameterStoreJson(Store, ParameterLimit);

	TArray<FNiagaraVariable> UserParameters;
	Store.GetUserParameters(UserParameters);
	Data->SetNumberField(TEXT("user_parameter_count"), UserParameters.Num());

	TArray<TSharedPtr<FJsonValue>> UserParametersJson;
	bool bTruncated = false;
	for (const FNiagaraVariable& UserParameter : UserParameters)
	{
		if (UserParametersJson.Num() >= ParameterLimit)
		{
			bTruncated = true;
			break;
		}
		UserParametersJson.Add(MakeShared<FJsonValueObject>(BuildVariableJson(UserParameter)));
	}
	Data->SetBoolField(TEXT("user_parameters_truncated"), bTruncated);
	Data->SetArrayField(TEXT("user_parameters"), UserParametersJson);
	return Data;
}

TArray<TSharedPtr<FJsonValue>> BuildReflectedPropertyArrayJson(const UObject* Object, const int32 PropertyLimit, const int32 StringLimit, bool& bOutTruncated, int32& OutPropertyCount)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TArray<TSharedPtr<FJsonValue>> PropertiesJson;
	bOutTruncated = false;
	OutPropertyCount = 0;
	if (!Object || !Object->GetClass())
	{
		return PropertiesJson;
	}

	for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
		{
			continue;
		}

		++OutPropertyCount;
		if (PropertiesJson.Num() >= PropertyLimit)
		{
			bOutTruncated = true;
			continue;
		}

		FString Value;
		Property->ExportText_InContainer(0, Value, Object, Object, const_cast<UObject*>(Object), PPF_None);

		TSharedPtr<FJsonObject> PropertyJson = MakeShared<FJsonObject>();
		PropertyJson->SetStringField(TEXT("name"), Property->GetName());
		PropertyJson->SetStringField(TEXT("type"), Property->GetCPPType());
		PropertyJson->SetStringField(TEXT("value"), TruncateString(Value, StringLimit));
		PropertiesJson.Add(MakeShared<FJsonValueObject>(PropertyJson));
	}
	return PropertiesJson;
}

TSharedPtr<FJsonObject> BuildScriptJson(const UNiagaraScript* Script, const FNiagaraInfoOptions& Options)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Script);
	if (!Script)
	{
		return Data;
	}

	Data->SetStringField(TEXT("usage"), EnumValueToString(StaticEnum<ENiagaraScriptUsage>(), static_cast<int64>(Script->GetUsage())));
	Data->SetStringField(TEXT("usage_id"), GuidToString(Script->GetUsageId()));
	Data->SetBoolField(TEXT("ready_to_run_cpu"), Script->IsReadyToRun(ENiagaraSimTarget::CPUSim));
	Data->SetBoolField(TEXT("ready_to_run_gpu"), Script->IsReadyToRun(ENiagaraSimTarget::GPUComputeSim));

#if WITH_EDITORONLY_DATA
	Data->SetBoolField(TEXT("versioning_enabled"), Script->IsVersioningEnabled());
	Data->SetObjectField(TEXT("exposed_version"), BuildVersionJson(Script->GetExposedVersion()));
	Data->SetBoolField(TEXT("script_source_synchronized"), Script->AreScriptAndSourceSynchronized());
	Data->SetBoolField(TEXT("shader_synchronized"), Script->IsScriptShaderSynchronized());
#endif

	if (const UNiagaraScriptSourceBase* Source = Script->GetLatestSource())
	{
		Data->SetObjectField(TEXT("source"), BuildObjectReferenceJson(Source));
	}

	const FNiagaraVMExecutableData& ExecutableData = Script->GetVMExecutableData();
	Data->SetBoolField(TEXT("executable_valid"), ExecutableData.IsValid());
	Data->SetBoolField(TEXT("has_byte_code"), ExecutableData.HasByteCode());
	Data->SetNumberField(TEXT("byte_code_length"), ExecutableData.ByteCode.GetLength());
	Data->SetBoolField(TEXT("byte_code_compressed"), ExecutableData.ByteCode.IsCompressed());
	Data->SetNumberField(TEXT("num_user_ptrs"), ExecutableData.NumUserPtrs);
	Data->SetStringField(TEXT("last_compile_status"), EnumValueToString(StaticEnum<ENiagaraScriptCompileStatus>(), static_cast<int64>(ExecutableData.LastCompileStatus)));
	Data->SetNumberField(TEXT("attribute_count"), ExecutableData.Attributes.Num());
	Data->SetNumberField(TEXT("attribute_written_count"), ExecutableData.AttributesWritten.Num());
	Data->SetNumberField(TEXT("data_interface_count"), ExecutableData.DataInterfaceInfo.Num());
	Data->SetNumberField(TEXT("resolved_data_interface_count"), Script->GetResolvedDataInterfaces().Num());
	Data->SetNumberField(TEXT("external_function_count"), ExecutableData.CalledVMExternalFunctions.Num());
	Data->SetNumberField(TEXT("read_dataset_count"), ExecutableData.ReadDataSets.Num());
	Data->SetNumberField(TEXT("write_dataset_count"), ExecutableData.WriteDataSets.Num());
	Data->SetNumberField(TEXT("stat_scope_count"), ExecutableData.StatScopes.Num());

#if WITH_EDITORONLY_DATA
	Data->SetStringField(TEXT("error"), TruncateString(ExecutableData.ErrorMsg, Options.StringLimit));
	Data->SetNumberField(TEXT("last_compile_event_count"), ExecutableData.LastCompileEvents.Num());
	Data->SetNumberField(TEXT("parameter_collection_path_count"), ExecutableData.ParameterCollectionPaths.Num());
#endif

	bool bAttributesTruncated = false;
	Data->SetArrayField(TEXT("attributes"), BuildVariableArrayJson(MakeArrayView(ExecutableData.Attributes), Options.AttributeLimit, bAttributesTruncated));
	Data->SetBoolField(TEXT("attributes_truncated"), bAttributesTruncated);

	bool bWrittenTruncated = false;
	Data->SetArrayField(TEXT("attributes_written"), BuildVariableArrayJson(MakeArrayView(ExecutableData.AttributesWritten), Options.AttributeLimit, bWrittenTruncated));
	Data->SetBoolField(TEXT("attributes_written_truncated"), bWrittenTruncated);

	TArray<TSharedPtr<FJsonValue>> DataInterfacesJson;
	bool bDataInterfacesTruncated = false;
	for (const FNiagaraScriptDataInterfaceCompileInfo& DataInterfaceInfo : ExecutableData.DataInterfaceInfo)
	{
		if (DataInterfacesJson.Num() >= Options.DataInterfaceLimit)
		{
			bDataInterfacesTruncated = true;
			break;
		}

		TSharedPtr<FJsonObject> DIJson = MakeShared<FJsonObject>();
		DIJson->SetStringField(TEXT("name"), DataInterfaceInfo.Name.ToString());
		DIJson->SetStringField(TEXT("type"), DataInterfaceInfo.Type.GetName());
		DIJson->SetNumberField(TEXT("user_ptr_index"), DataInterfaceInfo.UserPtrIdx);
		DIJson->SetStringField(TEXT("source_emitter_name"), DataInterfaceInfo.SourceEmitterName);
		DIJson->SetBoolField(TEXT("placeholder"), DataInterfaceInfo.bIsPlaceholder);
		DIJson->SetBoolField(TEXT("can_execute_cpu"), DataInterfaceInfo.CanExecuteOnTarget(ENiagaraSimTarget::CPUSim));
		DIJson->SetBoolField(TEXT("can_execute_gpu"), DataInterfaceInfo.CanExecuteOnTarget(ENiagaraSimTarget::GPUComputeSim));
		if (UNiagaraDataInterface* DefaultDI = DataInterfaceInfo.GetDefaultDataInterface())
		{
			DIJson->SetObjectField(TEXT("default_data_interface"), BuildObjectReferenceJson(DefaultDI));
		}
		DataInterfacesJson.Add(MakeShared<FJsonValueObject>(DIJson));
	}
	Data->SetBoolField(TEXT("data_interfaces_truncated"), bDataInterfacesTruncated);
	Data->SetArrayField(TEXT("data_interfaces"), DataInterfacesJson);

	TArray<TSharedPtr<FJsonValue>> ExternalFunctionsJson;
	bool bExternalFunctionsTruncated = false;
	for (const FVMExternalFunctionBindingInfo& FunctionInfo : ExecutableData.CalledVMExternalFunctions)
	{
		if (ExternalFunctionsJson.Num() >= Options.FunctionLimit)
		{
			bExternalFunctionsTruncated = true;
			break;
		}

		TSharedPtr<FJsonObject> FunctionJson = MakeShared<FJsonObject>();
		FunctionJson->SetStringField(TEXT("name"), FunctionInfo.Name.ToString());
		FunctionJson->SetStringField(TEXT("owner_name"), FunctionInfo.OwnerName.ToString());
		FunctionJson->SetNumberField(TEXT("input_count"), FunctionInfo.GetNumInputs());
		FunctionJson->SetNumberField(TEXT("output_count"), FunctionInfo.GetNumOutputs());
		FunctionJson->SetNumberField(TEXT("variadic_input_count"), FunctionInfo.VariadicInputs.Num());
		FunctionJson->SetNumberField(TEXT("variadic_output_count"), FunctionInfo.VariadicOutputs.Num());
		FunctionJson->SetNumberField(TEXT("specifier_count"), FunctionInfo.FunctionSpecifiers.Num());
		ExternalFunctionsJson.Add(MakeShared<FJsonValueObject>(FunctionJson));
	}
	Data->SetBoolField(TEXT("external_functions_truncated"), bExternalFunctionsTruncated);
	Data->SetArrayField(TEXT("external_functions"), ExternalFunctionsJson);

	if (Options.bIncludeParameters)
	{
		Data->SetObjectField(TEXT("rapid_iteration_parameters"), BuildParameterStoreJson(Script->RapidIterationParameters, Options.ParameterLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildRendererJson(const UNiagaraRendererProperties* Renderer, const FNiagaraInfoOptions& Options)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Renderer);
	if (!Renderer)
	{
		return Data;
	}

	Data->SetStringField(TEXT("class_name"), Renderer->GetClass() ? Renderer->GetClass()->GetName() : FString());
	Data->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());
	Data->SetBoolField(TEXT("active"), Renderer->GetIsActive());
	Data->SetBoolField(TEXT("allow_in_cull_proxies"), Renderer->bAllowInCullProxies);
	Data->SetNumberField(TEXT("sort_order_hint"), Renderer->SortOrderHint);
	Data->SetStringField(TEXT("source_mode"), EnumValueToString(StaticEnum<ENiagaraRendererSourceDataMode>(), static_cast<int64>(Renderer->GetCurrentSourceMode())));
	Data->SetStringField(TEXT("motion_vector_setting"), EnumValueToString(StaticEnum<ENiagaraRendererMotionVectorSetting>(), static_cast<int64>(Renderer->MotionVectorSetting)));
	Data->SetBoolField(TEXT("supports_cpu_sim"), Renderer->IsSimTargetSupported(ENiagaraSimTarget::CPUSim));
	Data->SetBoolField(TEXT("supports_gpu_sim"), Renderer->IsSimTargetSupported(ENiagaraSimTarget::GPUComputeSim));
	Data->SetBoolField(TEXT("needs_mids_for_materials"), Renderer->NeedsMIDsForMaterials());
	Data->SetBoolField(TEXT("needs_system_post_tick"), Renderer->NeedsSystemPostTick());
	Data->SetBoolField(TEXT("needs_system_completion"), Renderer->NeedsSystemCompletion());
	Data->SetBoolField(TEXT("needs_precise_motion_vectors"), Renderer->NeedsPreciseMotionVectors());
	Data->SetBoolField(TEXT("uses_heterogeneous_volumes"), Renderer->UseHeterogeneousVolumes());

#if WITH_EDITORONLY_DATA
	const TArray<FNiagaraVariable> BoundAttributes = Renderer->GetBoundAttributes();
	bool bBoundAttributesTruncated = false;
	Data->SetNumberField(TEXT("bound_attribute_count"), BoundAttributes.Num());
	Data->SetArrayField(TEXT("bound_attributes"), BuildVariableArrayJson(MakeArrayView(BoundAttributes), Options.AttributeLimit, bBoundAttributesTruncated));
	Data->SetBoolField(TEXT("bound_attributes_truncated"), bBoundAttributesTruncated);
	Data->SetStringField(TEXT("display_name"), Renderer->GetWidgetDisplayName().ToString());
#endif

	if (Options.bIncludeProperties)
	{
		bool bPropertiesTruncated = false;
		int32 PropertyCount = 0;
		TArray<TSharedPtr<FJsonValue>> PropertiesJson = BuildReflectedPropertyArrayJson(Renderer, Options.PropertyLimit, Options.StringLimit, bPropertiesTruncated, PropertyCount);
		Data->SetNumberField(TEXT("property_count"), PropertyCount);
		Data->SetBoolField(TEXT("properties_truncated"), bPropertiesTruncated);
		Data->SetArrayField(TEXT("properties"), PropertiesJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildEmitterDataJson(const FVersionedNiagaraEmitterData* EmitterData, const FNiagaraInfoOptions& Options)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("present"), EmitterData != nullptr);
	if (!EmitterData)
	{
		return Data;
	}

	Data->SetObjectField(TEXT("version"), BuildVersionJson(EmitterData->Version));
	Data->SetBoolField(TEXT("deprecated"), EmitterData->bDeprecated);
	Data->SetStringField(TEXT("deprecation_message"), EmitterData->DeprecationMessage.ToString());
	Data->SetBoolField(TEXT("local_space"), EmitterData->bLocalSpace);
	Data->SetBoolField(TEXT("determinism"), EmitterData->bDeterminism);
	Data->SetNumberField(TEXT("random_seed"), EmitterData->RandomSeed);
	Data->SetStringField(TEXT("interpolated_spawn_mode"), EnumValueToString(StaticEnum<ENiagaraInterpolatedSpawnMode>(), static_cast<int64>(EmitterData->InterpolatedSpawnMode)));
	Data->SetBoolField(TEXT("uses_interpolated_spawning"), EmitterData->UsesInterpolatedSpawning());
	Data->SetStringField(TEXT("sim_target"), EnumValueToString(StaticEnum<ENiagaraSimTarget>(), static_cast<int64>(EmitterData->SimTarget)));
	Data->SetStringField(TEXT("calculate_bounds_mode"), EnumValueToString(StaticEnum<ENiagaraEmitterCalculateBoundMode>(), static_cast<int64>(EmitterData->CalculateBoundsMode)));
	Data->SetObjectField(TEXT("fixed_bounds"), BuildBoxJson(EmitterData->FixedBounds));
	Data->SetBoolField(TEXT("requires_persistent_ids"), EmitterData->RequiresPersistentIDs());
	Data->SetBoolField(TEXT("ready_to_run"), EmitterData->IsReadyToRun());
	Data->SetBoolField(TEXT("allowed_to_execute"), EmitterData->IsAllowedToExecute());
	Data->SetBoolField(TEXT("allowed_by_scalability"), EmitterData->IsAllowedByScalability());
	Data->SetNumberField(TEXT("max_gpu_particles_spawn_per_frame"), EmitterData->MaxGPUParticlesSpawnPerFrame);
	Data->SetStringField(TEXT("allocation_mode"), EnumValueToString(StaticEnum<EParticleAllocationMode>(), static_cast<int64>(EmitterData->AllocationMode)));
	Data->SetNumberField(TEXT("pre_allocation_count"), EmitterData->PreAllocationCount);
	Data->SetNumberField(TEXT("event_handler_count"), EmitterData->GetEventHandlers().Num());
	Data->SetNumberField(TEXT("emitter_dependency_count"), EmitterData->EmitterDependencies.Num());
	Data->SetNumberField(TEXT("simulation_stage_count"), EmitterData->GetSimulationStages().Num());
	Data->SetNumberField(TEXT("renderer_count"), EmitterData->GetRenderers().Num());
	Data->SetNumberField(TEXT("max_instance_count"), static_cast<double>(EmitterData->GetMaxInstanceCount()));
	Data->SetNumberField(TEXT("max_allocation_count"), static_cast<double>(EmitterData->GetMaxAllocationCount()));
	Data->SetBoolField(TEXT("requires_view_uniform_buffer"), EmitterData->RequiresViewUniformBuffer());
	Data->SetBoolField(TEXT("needs_partial_depth_texture"), EmitterData->NeedsPartialDepthTexture());
	Data->SetBoolField(TEXT("pso_precache_failed"), EmitterData->DidPSOPrecacheFail());

	if (Options.bIncludeRenderers)
	{
		TArray<TSharedPtr<FJsonValue>> RenderersJson;
		bool bRenderersTruncated = false;
		for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
		{
			if (RenderersJson.Num() >= Options.RendererLimit)
			{
				bRenderersTruncated = true;
				break;
			}
			RenderersJson.Add(MakeShared<FJsonValueObject>(BuildRendererJson(Renderer, Options)));
		}
		Data->SetBoolField(TEXT("renderers_truncated"), bRenderersTruncated);
		Data->SetArrayField(TEXT("renderers"), RenderersJson);
	}

	if (Options.bIncludeScripts)
	{
		TArray<UNiagaraScript*> Scripts;
		EmitterData->GetScripts(Scripts, false, false);

		TArray<TSharedPtr<FJsonValue>> ScriptsJson;
		bool bScriptsTruncated = false;
		for (UNiagaraScript* Script : Scripts)
		{
			if (ScriptsJson.Num() >= Options.ScriptLimit)
			{
				bScriptsTruncated = true;
				break;
			}
			ScriptsJson.Add(MakeShared<FJsonValueObject>(BuildScriptJson(Script, Options)));
		}
		Data->SetNumberField(TEXT("script_count"), Scripts.Num());
		Data->SetBoolField(TEXT("scripts_truncated"), bScriptsTruncated);
		Data->SetArrayField(TEXT("scripts"), ScriptsJson);
	}

	if (Options.bIncludeParameters)
	{
		Data->SetObjectField(TEXT("renderer_bindings"), BuildParameterStoreJson(EmitterData->RendererBindings, Options.ParameterLimit));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildEmitterHandleJson(const FNiagaraEmitterHandle& Handle, const FNiagaraInfoOptions& Options)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("valid"), Handle.IsValid());
	Data->SetStringField(TEXT("name"), Handle.GetName().ToString());
	Data->SetStringField(TEXT("id"), GuidToString(Handle.GetId()));
	Data->SetStringField(TEXT("id_name"), Handle.GetIdName().ToString());
	Data->SetStringField(TEXT("unique_instance_name"), Handle.GetUniqueInstanceName());
	Data->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
	Data->SetBoolField(TEXT("allowed_by_scalability"), Handle.IsAllowedByScalability());
	Data->SetStringField(TEXT("emitter_mode"), EnumValueToString(StaticEnum<ENiagaraEmitterMode>(), static_cast<int64>(Handle.GetEmitterMode())));

#if WITH_EDITORONLY_DATA
	Data->SetBoolField(TEXT("isolated"), Handle.IsIsolated());
	Data->SetBoolField(TEXT("debug_show_bounds"), Handle.GetDebugShowBounds());
	Data->SetBoolField(TEXT("needs_recompile"), Handle.NeedsRecompile());
#endif

	const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
	if (UNiagaraEmitter* InstanceEmitter = Instance.Emitter)
	{
		Data->SetObjectField(TEXT("instance"), MCPToolkit::RuntimeDiagnostics::BuildObjectReferenceJson(InstanceEmitter));
		Data->SetStringField(TEXT("instance_version"), GuidToString(Instance.Version));
	}

	if (Options.bIncludeEmitters)
	{
		Data->SetObjectField(TEXT("emitter_data"), BuildEmitterDataJson(Handle.GetEmitterData(), Options));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildSystemJson(UNiagaraSystem* System, const FNiagaraInfoOptions& Options)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(System);
	if (!System)
	{
		return Data;
	}

	bool bFixedBounds = false;
	TryReadBoolProperty(System, TEXT("bFixedBounds"), bFixedBounds);

	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	Data->SetStringField(TEXT("niagara_asset_type"), TEXT("system"));
	Data->SetNumberField(TEXT("emitter_count"), EmitterHandles.Num());
	Data->SetBoolField(TEXT("fixed_bounds_enabled"), bFixedBounds);
	Data->SetObjectField(TEXT("fixed_bounds"), BuildBoxJson(System->GetFixedBounds()));
	Data->SetBoolField(TEXT("needs_warmup"), System->NeedsWarmup());
	Data->SetNumberField(TEXT("warmup_time"), System->GetWarmupTime());
	Data->SetNumberField(TEXT("warmup_tick_count"), System->GetWarmupTickCount());
	Data->SetNumberField(TEXT("warmup_tick_delta"), System->GetWarmupTickDelta());
	Data->SetBoolField(TEXT("determinism"), System->NeedsDeterminism());
	Data->SetNumberField(TEXT("random_seed"), System->GetRandomSeed());
	Data->SetBoolField(TEXT("needs_gpu_context_init_for_data_interfaces"), System->NeedsGPUContextInitForDataInterfaces());
	Data->SetBoolField(TEXT("should_use_rapid_iteration_parameters"), System->ShouldUseRapidIterationParameters());
	Data->SetBoolField(TEXT("should_trim_attributes"), System->ShouldTrimAttributes());
	Data->SetBoolField(TEXT("should_ignore_particle_reads_for_attribute_trim"), System->ShouldIgnoreParticleReadsForAttributeTrim());
	Data->SetBoolField(TEXT("should_compress_attributes"), System->ShouldCompressAttributes());
	Data->SetBoolField(TEXT("should_disable_debug_switches"), System->ShouldDisableDebugSwitches());

	if (Options.bIncludeParameters)
	{
		Data->SetObjectField(TEXT("exposed_parameters"), BuildUserParameterStoreJson(System->GetExposedParameters(), Options.ParameterLimit));
	}

	if (Options.bIncludeScripts)
	{
		TSharedPtr<FJsonObject> ScriptsJson = MakeShared<FJsonObject>();
		ScriptsJson->SetObjectField(TEXT("system_spawn"), BuildScriptJson(System->GetSystemSpawnScript(), Options));
		ScriptsJson->SetObjectField(TEXT("system_update"), BuildScriptJson(System->GetSystemUpdateScript(), Options));
		Data->SetObjectField(TEXT("system_scripts"), ScriptsJson);
	}

	if (Options.bIncludeEmitters)
	{
		TArray<TSharedPtr<FJsonValue>> EmittersJson;
		bool bEmittersTruncated = false;
		for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
		{
			if (EmittersJson.Num() >= Options.EmitterLimit)
			{
				bEmittersTruncated = true;
				break;
			}
			EmittersJson.Add(MakeShared<FJsonValueObject>(BuildEmitterHandleJson(Handle, Options)));
		}
		Data->SetBoolField(TEXT("emitters_truncated"), bEmittersTruncated);
		Data->SetArrayField(TEXT("emitters"), EmittersJson);
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildStandaloneEmitterJson(UNiagaraEmitter* Emitter, const FNiagaraInfoOptions& Options)
{
	using namespace MCPToolkit::RuntimeDiagnostics;

	TSharedPtr<FJsonObject> Data = BuildObjectReferenceJson(Emitter);
	if (!Emitter)
	{
		return Data;
	}

	Data->SetStringField(TEXT("niagara_asset_type"), TEXT("emitter"));

#if WITH_EDITORONLY_DATA
	Data->SetBoolField(TEXT("versioning_enabled"), Emitter->IsVersioningEnabled());
	const FNiagaraAssetVersion ExposedVersion = Emitter->GetExposedVersion();
	Data->SetObjectField(TEXT("exposed_version"), BuildVersionJson(ExposedVersion));

	const TArray<FNiagaraAssetVersion> Versions = Emitter->GetAllAvailableVersions();
	TArray<TSharedPtr<FJsonValue>> VersionsJson;
	for (const FNiagaraAssetVersion& Version : Versions)
	{
		VersionsJson.Add(MakeShared<FJsonValueObject>(BuildVersionJson(Version)));
	}
	Data->SetNumberField(TEXT("version_count"), Versions.Num());
	Data->SetArrayField(TEXT("versions"), VersionsJson);
	Data->SetObjectField(TEXT("emitter_data"), BuildEmitterDataJson(Emitter->GetEmitterData(ExposedVersion.VersionGuid), Options));
#else
	Data->SetObjectField(TEXT("emitter_data"), BuildEmitterDataJson(nullptr, Options));
#endif
	return Data;
}

TSharedPtr<FJsonObject> BuildStandaloneScriptJson(UNiagaraScript* Script, const FNiagaraInfoOptions& Options)
{
	TSharedPtr<FJsonObject> Data = BuildScriptJson(Script, Options);
	Data->SetStringField(TEXT("niagara_asset_type"), TEXT("script"));
	return Data;
}

FString BuildNiagaraAssetInfoResponse(
	const FString& AssetPath,
	const FNiagaraInfoOptions& Options)
{
	FAssetData AssetData;
	FString ResolvedObjectPath;
	UObject* Asset = LoadNiagaraAsset(AssetPath, AssetData, ResolvedObjectPath);
	if (!Asset)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Niagara asset not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Data;
	if (UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset))
	{
		Data = BuildSystemJson(System, Options);
	}
	else if (UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Asset))
	{
		Data = BuildStandaloneEmitterJson(Emitter, Options);
	}
	else if (UNiagaraScript* Script = Cast<UNiagaraScript>(Asset))
	{
		Data = BuildStandaloneScriptJson(Script, Options);
	}
	else
	{
		return CreateErrorResponse(FString::Printf(TEXT("Asset is not a supported Niagara asset: %s"), *Asset->GetPathName()));
	}

	Data->SetStringField(TEXT("asset_path"), AssetPath);
	Data->SetStringField(TEXT("resolved_object_path"), ResolvedObjectPath);
	Data->SetStringField(TEXT("package_name"), Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : FString());
	Data->SetStringField(TEXT("asset_name"), Asset->GetName());
	Data->SetStringField(TEXT("class_path"), Asset->GetClass() ? Asset->GetClass()->GetClassPathName().ToString() : FString());
	if (Options.bIncludeAssetRegistry)
	{
		Data->SetObjectField(TEXT("asset_registry"), BuildAssetDataJson(AssetData));
	}
	Data->SetObjectField(TEXT("resource_size"), BuildResourceSizeJson(Asset));
	return CreateSuccessResponse(Data);
}
}

FString HandleNiagaraAssetInfo(TSharedPtr<FJsonObject> Params)
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

	FNiagaraInfoOptions Options;
	Options.bIncludeEmitters = ReadBoolField(Params, TEXT("include_emitters"), true);
	Options.bIncludeRenderers = ReadBoolField(Params, TEXT("include_renderers"), true);
	Options.bIncludeScripts = ReadBoolField(Params, TEXT("include_scripts"), true);
	Options.bIncludeParameters = ReadBoolField(Params, TEXT("include_parameters"), true);
	Options.bIncludeProperties = ReadBoolField(Params, TEXT("include_properties"), false);
	Options.bIncludeAssetRegistry = ReadBoolField(Params, TEXT("include_asset_registry"), true);
	Options.EmitterLimit = ReadIntField(Params, TEXT("emitter_limit"), 64, 0, 1024);
	Options.RendererLimit = ReadIntField(Params, TEXT("renderer_limit"), 128, 0, 4096);
	Options.ScriptLimit = ReadIntField(Params, TEXT("script_limit"), 128, 0, 4096);
	Options.ParameterLimit = ReadIntField(Params, TEXT("parameter_limit"), 256, 0, 8192);
	Options.AttributeLimit = ReadIntField(Params, TEXT("attribute_limit"), 256, 0, 8192);
	Options.DataInterfaceLimit = ReadIntField(Params, TEXT("data_interface_limit"), 128, 0, 4096);
	Options.FunctionLimit = ReadIntField(Params, TEXT("function_limit"), 128, 0, 4096);
	Options.PropertyLimit = ReadIntField(Params, TEXT("property_limit"), 80, 0, 2048);
	Options.StringLimit = ReadIntField(Params, TEXT("string_limit"), 512, 32, 8192);

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [AssetPath, Options, Promise]()
	{
		Promise->SetValue(BuildNiagaraAssetInfoResponse(AssetPath, Options));
	});

	return Future.Get();
}
}
