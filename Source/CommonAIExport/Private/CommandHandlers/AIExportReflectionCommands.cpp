// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/AIExportReflectionCommands.h"

#include "Builders/AIDataAssetBuilder.h"
#include "CommandHandlers/AIExportCommandResponse.h"

#include "Async/Async.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace CommonAIExport::CommandHandlers::Reflection
{
namespace
{
UWorld* ResolveWorld(const FString& WorldMode)
{
	if (!GEditor)
	{
		return nullptr;
	}

	const FString Mode = WorldMode.ToLower();
	if ((Mode == TEXT("auto") || Mode == TEXT("pie") || Mode == TEXT("play")) && GEditor->PlayWorld)
	{
		return GEditor->PlayWorld;
	}
	if (Mode == TEXT("editor") || Mode == TEXT("auto") || Mode.IsEmpty())
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid())
	{
		return TEXT("");
	}

	FString StringValue;
	if (Value->TryGetString(StringValue))
	{
		return StringValue;
	}

	double NumberValue = 0.0;
	if (Value->TryGetNumber(NumberValue))
	{
		return FString::SanitizeFloat(NumberValue);
	}

	bool BoolValue = false;
	if (Value->TryGetBool(BoolValue))
	{
		return BoolValue ? TEXT("true") : TEXT("false");
	}

	const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
	if (Value->TryGetObject(ObjectValue) && ObjectValue && ObjectValue->IsValid())
	{
		FString ExplicitImportText;
		if ((*ObjectValue)->TryGetStringField(TEXT("_import_text"), ExplicitImportText))
		{
			return ExplicitImportText;
		}
	}

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT(""), Writer);
	return Serialized;
}

FString ExportPropertyText(FProperty* Property, const void* ValuePtr, UObject* Owner)
{
	FString Value;
	if (Property && ValuePtr)
	{
		Property->ExportText_Direct(Value, ValuePtr, nullptr, Owner, PPF_None);
	}
	return Value;
}

void AddObjectReference(TSharedPtr<FJsonObject> Data, const UObject* Object)
{
	if (!Object)
	{
		return;
	}

	Data->SetStringField(TEXT("name"), Object->GetName());
	Data->SetStringField(TEXT("path"), Object->GetPathName());
	Data->SetStringField(TEXT("class"), Object->GetClass() ? Object->GetClass()->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("outer"), Object->GetOuter() ? Object->GetOuter()->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("package"), Object->GetOutermost() ? Object->GetOutermost()->GetName() : TEXT(""));
	Data->SetBoolField(TEXT("is_asset"), Object->IsAsset());
	Data->SetBoolField(TEXT("is_class_default_object"), Object->HasAnyFlags(RF_ClassDefaultObject));
}

TSharedPtr<FJsonObject> BuildPropertySummary(FProperty* Property, const void* ContainerPtr, UObject* Owner, bool bIncludeValue)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Property)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Property->GetName());
	Data->SetStringField(TEXT("type"), Property->GetCPPType());
	Data->SetBoolField(TEXT("editable"), Property->HasAnyPropertyFlags(CPF_Edit));
	Data->SetBoolField(TEXT("blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
	Data->SetBoolField(TEXT("transient"), Property->HasAnyPropertyFlags(CPF_Transient));
	Data->SetBoolField(TEXT("config"), Property->HasAnyPropertyFlags(CPF_Config));
	if (bIncludeValue && ContainerPtr)
	{
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		Data->SetStringField(TEXT("value"), ExportPropertyText(Property, ValuePtr, Owner));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildFunctionSummary(UFunction* Function)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Function)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Function->GetName());
	Data->SetBoolField(TEXT("blueprint_callable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
	Data->SetBoolField(TEXT("blueprint_event"), Function->HasAnyFunctionFlags(FUNC_BlueprintEvent));
	Data->SetBoolField(TEXT("exec"), Function->HasAnyFunctionFlags(FUNC_Exec));
	Data->SetBoolField(TEXT("const"), Function->HasAnyFunctionFlags(FUNC_Const));
	Data->SetBoolField(TEXT("net"), Function->HasAnyFunctionFlags(FUNC_Net));

	TArray<TSharedPtr<FJsonValue>> Params;
	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		FProperty* Param = *It;
		if (!Param || !Param->HasAnyPropertyFlags(CPF_Parm))
		{
			continue;
		}

		TSharedPtr<FJsonObject> ParamJson = MakeShared<FJsonObject>();
		ParamJson->SetStringField(TEXT("name"), Param->GetName());
		ParamJson->SetStringField(TEXT("type"), Param->GetCPPType());
		ParamJson->SetBoolField(TEXT("out"), Param->HasAnyPropertyFlags(CPF_OutParm));
		ParamJson->SetBoolField(TEXT("return"), Param->HasAnyPropertyFlags(CPF_ReturnParm));
		Params.Add(MakeShared<FJsonValueObject>(ParamJson));
	}
	Data->SetArrayField(TEXT("params"), Params);
	return Data;
}

AActor* FindActor(UWorld* World, const FString& ActorPath, const FString& ActorLabel, const FString& ActorName)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (!ActorPath.IsEmpty() && Actor->GetPathName() == ActorPath) return Actor;
		if (!ActorLabel.IsEmpty() && Actor->GetActorLabel() == ActorLabel) return Actor;
		if (!ActorName.IsEmpty() && Actor->GetName() == ActorName) return Actor;
	}
	return nullptr;
}

UObject* ResolveObject(TSharedPtr<FJsonObject> Params, FString& OutError)
{
	if (!Params.IsValid())
	{
		OutError = TEXT("Missing 'params' object");
		return nullptr;
	}

	FString ObjectPath;
	FString AssetPath;
	FString ClassPath;
	FString ActorPath;
	FString ActorLabel;
	FString ActorName;
	FString WorldMode = TEXT("auto");
	bool bSelectedActor = false;
	Params->TryGetStringField(TEXT("object_path"), ObjectPath);
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	Params->TryGetStringField(TEXT("class_path"), ClassPath);
	Params->TryGetStringField(TEXT("actor_path"), ActorPath);
	Params->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Params->TryGetStringField(TEXT("actor_name"), ActorName);
	Params->TryGetStringField(TEXT("world"), WorldMode);
	Params->TryGetBoolField(TEXT("selected_actor"), bSelectedActor);

	if (!AssetPath.IsEmpty())
	{
		UObject* Asset = UAIDataAssetBuilder::LoadAssetObject(AssetPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		}
		return Asset;
	}

	if (!ObjectPath.IsEmpty())
	{
		UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!Object)
		{
			Object = FindObject<UObject>(nullptr, *ObjectPath);
		}
		if (!Object)
		{
			OutError = FString::Printf(TEXT("Object not found: %s"), *ObjectPath);
		}
		return Object;
	}

	if (!ClassPath.IsEmpty())
	{
		UClass* Class = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
		if (!Class)
		{
			OutError = FString::Printf(TEXT("Class not found: %s"), *ClassPath);
		}
		return Class;
	}

	if (bSelectedActor)
	{
		if (!GEditor)
		{
			OutError = TEXT("GEditor is not available");
			return nullptr;
		}
		USelection* Selection = GEditor->GetSelectedActors();
		UObject* SelectedObject = Selection ? Selection->GetTop<AActor>() : nullptr;
		if (!SelectedObject)
		{
			OutError = TEXT("No selected actor");
		}
		return SelectedObject;
	}

	if (!ActorPath.IsEmpty() || !ActorLabel.IsEmpty() || !ActorName.IsEmpty())
	{
		AActor* Actor = FindActor(ResolveWorld(WorldMode), ActorPath, ActorLabel, ActorName);
		if (!Actor)
		{
			OutError = TEXT("Actor not found");
		}
		return Actor;
	}

	OutError = TEXT("Expected one of: object_path, asset_path, class_path, selected_actor, actor_path, actor_label, actor_name");
	return nullptr;
}

bool ParseSegment(const FString& Segment, FString& OutName, int32& OutIndex)
{
	OutIndex = INDEX_NONE;
	OutName = Segment;

	int32 BracketIndex = INDEX_NONE;
	if (!Segment.FindChar(TEXT('['), BracketIndex))
	{
		return !OutName.IsEmpty();
	}

	OutName = Segment.Left(BracketIndex);
	FString IndexString = Segment.Mid(BracketIndex + 1);
	IndexString.RemoveFromEnd(TEXT("]"));
	if (OutName.IsEmpty() || IndexString.IsEmpty() || !IndexString.IsNumeric())
	{
		return false;
	}

	OutIndex = FCString::Atoi(*IndexString);
	return OutIndex >= 0;
}

bool ResolvePropertyPath(UStruct* OwnerStruct, void* Container, UObject* OwnerObject, const TArray<FString>& Segments, int32 SegmentIndex, FProperty*& OutProperty, void*& OutValuePtr, UObject*& OutOwnerObject)
{
	if (!OwnerStruct || !Container || SegmentIndex >= Segments.Num())
	{
		return false;
	}

	FString PropertyName;
	int32 ArrayIndex = INDEX_NONE;
	if (!ParseSegment(Segments[SegmentIndex], PropertyName, ArrayIndex))
	{
		return false;
	}

	FProperty* Property = OwnerStruct->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
	if (ArrayIndex >= 0)
	{
		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property);
		if (!ArrayProperty)
		{
			return false;
		}
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		if (ArrayIndex >= Helper.Num())
		{
			return false;
		}
		ValuePtr = Helper.GetRawPtr(ArrayIndex);

		if (SegmentIndex == Segments.Num() - 1)
		{
			OutProperty = ArrayProperty->Inner;
			OutValuePtr = ValuePtr;
			OutOwnerObject = OwnerObject;
			return true;
		}

		if (FStructProperty* StructInner = CastField<FStructProperty>(ArrayProperty->Inner))
		{
			return ResolvePropertyPath(StructInner->Struct, ValuePtr, OwnerObject, Segments, SegmentIndex + 1, OutProperty, OutValuePtr, OutOwnerObject);
		}
		if (FObjectProperty* ObjectInner = CastField<FObjectProperty>(ArrayProperty->Inner))
		{
			UObject* InnerObject = ObjectInner->GetObjectPropertyValue(ValuePtr);
			return InnerObject && ResolvePropertyPath(InnerObject->GetClass(), InnerObject, InnerObject, Segments, SegmentIndex + 1, OutProperty, OutValuePtr, OutOwnerObject);
		}
		return false;
	}

	if (SegmentIndex == Segments.Num() - 1)
	{
		OutProperty = Property;
		OutValuePtr = ValuePtr;
		OutOwnerObject = OwnerObject;
		return true;
	}

	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		return ResolvePropertyPath(StructProperty->Struct, ValuePtr, OwnerObject, Segments, SegmentIndex + 1, OutProperty, OutValuePtr, OutOwnerObject);
	}
	if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
	{
		UObject* InnerObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
		return InnerObject && ResolvePropertyPath(InnerObject->GetClass(), InnerObject, InnerObject, Segments, SegmentIndex + 1, OutProperty, OutValuePtr, OutOwnerObject);
	}
	return false;
}

bool ResolvePropertyPath(UObject* Object, const FString& PropertyPath, FProperty*& OutProperty, void*& OutValuePtr, UObject*& OutOwnerObject)
{
	if (!Object || PropertyPath.IsEmpty())
	{
		return false;
	}

	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."));
	return ResolvePropertyPath(Object->GetClass(), Object, Object, Segments, 0, OutProperty, OutValuePtr, OutOwnerObject);
}

TSharedPtr<FJsonObject> BuildObjectQuery(UObject* Object, TSharedPtr<FJsonObject> Params)
{
	bool bIncludeProperties = true;
	bool bIncludeValues = true;
	bool bIncludeFunctions = true;
	bool bIncludeComponents = true;
	int32 Limit = 200;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_properties"), bIncludeProperties);
		Params->TryGetBoolField(TEXT("include_values"), bIncludeValues);
		Params->TryGetBoolField(TEXT("include_functions"), bIncludeFunctions);
		Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
		double LimitValue = 0.0;
		if (Params->TryGetNumberField(TEXT("limit"), LimitValue) && LimitValue > 0.0)
		{
			Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, 5000);
		}
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	AddObjectReference(Data, Object);

	if (bIncludeProperties)
	{
		TArray<TSharedPtr<FJsonValue>> Properties;
		int32 TotalCount = 0;
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			++TotalCount;
			if (Properties.Num() < Limit)
			{
				Properties.Add(MakeShared<FJsonValueObject>(BuildPropertySummary(Property, Object, Object, bIncludeValues)));
			}
		}
		Data->SetArrayField(TEXT("properties"), Properties);
		Data->SetNumberField(TEXT("property_count"), TotalCount);
		Data->SetBoolField(TEXT("properties_truncated"), TotalCount > Properties.Num());
	}

	if (bIncludeFunctions)
	{
		TArray<TSharedPtr<FJsonValue>> Functions;
		int32 TotalCount = 0;
		for (TFieldIterator<UFunction> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			UFunction* Function = *It;
			if (!Function)
			{
				continue;
			}

			++TotalCount;
			if (Functions.Num() < Limit)
			{
				Functions.Add(MakeShared<FJsonValueObject>(BuildFunctionSummary(Function)));
			}
		}
		Data->SetArrayField(TEXT("functions"), Functions);
		Data->SetNumberField(TEXT("function_count"), TotalCount);
		Data->SetBoolField(TEXT("functions_truncated"), TotalCount > Functions.Num());
	}

	if (bIncludeComponents)
	{
		if (AActor* Actor = Cast<AActor>(Object))
		{
			TArray<UActorComponent*> Components;
			Actor->GetComponents(Components);

			TArray<TSharedPtr<FJsonValue>> ComponentArray;
			for (UActorComponent* Component : Components)
			{
				if (!Component)
				{
					continue;
				}

				TSharedPtr<FJsonObject> ComponentJson = MakeShared<FJsonObject>();
				AddObjectReference(ComponentJson, Component);
				ComponentJson->SetBoolField(TEXT("registered"), Component->IsRegistered());
				ComponentJson->SetBoolField(TEXT("active"), Component->IsActive());
				ComponentArray.Add(MakeShared<FJsonValueObject>(ComponentJson));
			}
			Data->SetArrayField(TEXT("components"), ComponentArray);
			Data->SetNumberField(TEXT("component_count"), ComponentArray.Num());
		}
	}

	return Data;
}
}

FString HandleObjectQuery(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise]()
	{
		FString Error;
		UObject* Object = ResolveObject(Params, Error);
		if (!Object)
		{
			Promise->SetValue(CreateErrorResponse(Error));
			return;
		}
		Promise->SetValue(CreateSuccessResponse(BuildObjectQuery(Object, Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Object query timed out"));
	return Future.Get();
}

FString HandleObjectGetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PropertyPath;
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath) || PropertyPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'property_path' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, PropertyPath, Promise]()
	{
		FString Error;
		UObject* Object = ResolveObject(Params, Error);
		if (!Object)
		{
			Promise->SetValue(CreateErrorResponse(Error));
			return;
		}

		FProperty* Property = nullptr;
		void* ValuePtr = nullptr;
		UObject* OwnerObject = nullptr;
		if (!ResolvePropertyPath(Object, PropertyPath, Property, ValuePtr, OwnerObject))
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Property not found or unsupported path: %s"), *PropertyPath)));
			return;
		}

		TSharedPtr<FJsonObject> Data = BuildPropertySummary(Property, OwnerObject, OwnerObject, false);
		Data->SetStringField(TEXT("object_path"), Object->GetPathName());
		Data->SetStringField(TEXT("property_path"), PropertyPath);
		Data->SetStringField(TEXT("value"), ExportPropertyText(Property, ValuePtr, OwnerObject));
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Object get property timed out"));
	return Future.Get();
}

FString HandleObjectSetProperty(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString PropertyPath;
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath) || PropertyPath.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'property_path' parameter"));
	}

	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));
	if (!Value.IsValid())
	{
		return CreateErrorResponse(TEXT("Missing 'value' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, PropertyPath, Value, Promise]()
	{
		FString Error;
		UObject* Object = ResolveObject(Params, Error);
		if (!Object)
		{
			Promise->SetValue(CreateErrorResponse(Error));
			return;
		}

		const FString ImportText = JsonValueToImportText(Value);
		const FScopedTransaction Transaction(NSLOCTEXT("CommonAIExport", "ObjectSetProperty", "AI Set Object Property"));
		Object->Modify();
		const bool bSet = UAIDataAssetBuilder::SetProperty(Object, PropertyPath, ImportText);

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("set"), bSet);
		Data->SetStringField(TEXT("object_path"), Object->GetPathName());
		Data->SetStringField(TEXT("property_path"), PropertyPath);
		Data->SetStringField(TEXT("import_text"), ImportText);
		Promise->SetValue(bSet ? CreateSuccessResponse(Data) : CreateErrorResponse(FString::Printf(TEXT("Failed to set property: %s"), *PropertyPath)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Object set property timed out"));
	return Future.Get();
}

FString HandleObjectCallFunction(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid()) return CreateErrorResponse(TEXT("Missing 'params' object"));

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName) || FunctionName.IsEmpty())
	{
		return CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, FunctionName, Promise]()
	{
		FString Error;
		UObject* Object = ResolveObject(Params, Error);
		if (!Object)
		{
			Promise->SetValue(CreateErrorResponse(Error));
			return;
		}

		UFunction* Function = Object->FindFunction(FName(*FunctionName));
		if (!Function)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Function not found: %s"), *FunctionName)));
			return;
		}
		if (Function->HasAnyFunctionFlags(FUNC_Net | FUNC_Delegate))
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Function is not allowed through object_call_function: %s"), *FunctionName)));
			return;
		}

		const TSharedPtr<FJsonObject>* ArgsObject = nullptr;
		Params->TryGetObjectField(TEXT("args"), ArgsObject);

		TArray<uint8> ParamBuffer;
		ParamBuffer.SetNumZeroed(Function->ParmsSize);
		void* ParamData = ParamBuffer.GetData();
		Function->InitializeStruct(ParamData);
		ON_SCOPE_EXIT
		{
			Function->DestroyStruct(ParamData);
		};

		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Param = *It;
			if (!Param || !Param->HasAnyPropertyFlags(CPF_Parm) || Param->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm))
			{
				continue;
			}

			TSharedPtr<FJsonValue> ArgValue;
			if (ArgsObject && ArgsObject->IsValid())
			{
				ArgValue = (*ArgsObject)->TryGetField(Param->GetName());
			}
			if (!ArgValue.IsValid())
			{
				continue;
			}

			void* ValuePtr = Param->ContainerPtrToValuePtr<void>(ParamData);
			const FString ImportText = JsonValueToImportText(ArgValue);
			if (!Param->ImportText_Direct(*ImportText, ValuePtr, Object, PPF_None))
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Failed to import argument %s for %s"), *Param->GetName(), *FunctionName)));
				return;
			}
		}

		Object->Modify();
		Object->ProcessEvent(Function, ParamData);

		TArray<TSharedPtr<FJsonValue>> OutParams;
		FString ReturnValue;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			FProperty* Param = *It;
			if (!Param || !Param->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			void* ValuePtr = Param->ContainerPtrToValuePtr<void>(ParamData);
			const FString ValueText = ExportPropertyText(Param, ValuePtr, Object);
			if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				ReturnValue = ValueText;
				continue;
			}
			if (Param->HasAnyPropertyFlags(CPF_OutParm))
			{
				TSharedPtr<FJsonObject> OutParam = MakeShared<FJsonObject>();
				OutParam->SetStringField(TEXT("name"), Param->GetName());
				OutParam->SetStringField(TEXT("type"), Param->GetCPPType());
				OutParam->SetStringField(TEXT("value"), ValueText);
				OutParams.Add(MakeShared<FJsonValueObject>(OutParam));
			}
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("object_path"), Object->GetPathName());
		Data->SetStringField(TEXT("function_name"), FunctionName);
		Data->SetBoolField(TEXT("called"), true);
		Data->SetStringField(TEXT("return_value"), ReturnValue);
		Data->SetArrayField(TEXT("out_params"), OutParams);
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Object call function timed out"));
	return Future.Get();
}
}
