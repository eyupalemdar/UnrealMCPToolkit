// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "CommandHandlers/MCTReflectionCommands.h"

#include "Builders/MCTDataAssetBuilder.h"
#include "CommandHandlers/MCTCommandResponse.h"

#include "Async/Async.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace MCPToolkit::CommandHandlers::Reflection
{
namespace
{
TSharedPtr<FJsonObject> BuildPropertySummary(FProperty* Property, const void* ContainerPtr, UObject* Owner, bool bIncludeValue);
TSharedPtr<FJsonObject> BuildFunctionSummary(UFunction* Function);

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

int32 ReadLimit(TSharedPtr<FJsonObject> Params, int32 DefaultLimit, int32 MaxLimit)
{
	int32 Limit = DefaultLimit;
	if (Params.IsValid())
	{
		double LimitValue = 0.0;
		if (Params->TryGetNumberField(TEXT("limit"), LimitValue) && LimitValue > 0.0)
		{
			Limit = FMath::Clamp(static_cast<int32>(LimitValue), 1, MaxLimit);
		}
	}
	return Limit;
}

bool MatchesFilter(const FString& Name, const FString& Path, const FString& Query)
{
	return Query.IsEmpty() || Name.Contains(Query, ESearchCase::IgnoreCase) || Path.Contains(Query, ESearchCase::IgnoreCase);
}

template <typename TFieldType>
TSharedPtr<FJsonObject> BuildKnownMetadataJson(const TFieldType* Field)
{
	TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
	if (!Field)
	{
		return Metadata;
	}

	static const FName MetadataKeys[] = {
		TEXT("DisplayName"),
		TEXT("Category"),
		TEXT("ToolTip"),
		TEXT("ShortTooltip"),
		TEXT("BlueprintType"),
		TEXT("Blueprintable"),
		TEXT("NotBlueprintable"),
		TEXT("BlueprintSpawnableComponent"),
		TEXT("ClassGroup"),
		TEXT("ModuleRelativePath"),
		TEXT("DeprecatedFunction"),
		TEXT("DeprecationMessage"),
	};

	for (const FName& Key : MetadataKeys)
	{
		if (Field->HasMetaData(Key))
		{
			Metadata->SetStringField(Key.ToString(), Field->GetMetaData(Key));
		}
	}
	return Metadata;
}

TArray<TSharedPtr<FJsonValue>> BuildClassFlagArray(const UClass* Class)
{
	TArray<TSharedPtr<FJsonValue>> Flags;
	if (!Class)
	{
		return Flags;
	}

	auto AddFlag = [&Flags](bool bCondition, const TCHAR* Name)
	{
		if (bCondition)
		{
			Flags.Add(MakeShared<FJsonValueString>(Name));
		}
	};

	AddFlag(Class->HasAnyClassFlags(CLASS_Abstract), TEXT("Abstract"));
	AddFlag(Class->HasAnyClassFlags(CLASS_Native), TEXT("Native"));
	AddFlag(Class->HasAnyClassFlags(CLASS_Interface), TEXT("Interface"));
	AddFlag(Class->HasAnyClassFlags(CLASS_Config), TEXT("Config"));
	AddFlag(Class->HasAnyClassFlags(CLASS_DefaultConfig), TEXT("DefaultConfig"));
	AddFlag(Class->HasAnyClassFlags(CLASS_Transient), TEXT("Transient"));
	AddFlag(Class->HasAnyClassFlags(CLASS_Deprecated), TEXT("Deprecated"));
	AddFlag(Class->HasAnyClassFlags(CLASS_NotPlaceable), TEXT("NotPlaceable"));
	return Flags;
}

TSharedPtr<FJsonObject> BuildPropertyTypeDetails(FProperty* Property)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Property)
	{
		return Data;
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		Data->SetStringField(TEXT("object_class"), ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetPathName() : TEXT(""));
	}
	if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		Data->SetStringField(TEXT("meta_class"), ClassProperty->MetaClass ? ClassProperty->MetaClass->GetPathName() : TEXT(""));
	}
	if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		Data->SetStringField(TEXT("struct"), StructProperty->Struct ? StructProperty->Struct->GetPathName() : TEXT(""));
	}
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
	{
		Data->SetStringField(TEXT("enum"), EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetPathName() : TEXT(""));
	}
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
	{
		if (ByteProperty->Enum)
		{
			Data->SetStringField(TEXT("enum"), ByteProperty->Enum->GetPathName());
		}
	}
	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		Data->SetStringField(TEXT("container"), TEXT("array"));
		Data->SetStringField(TEXT("inner_type"), ArrayProperty->Inner ? ArrayProperty->Inner->GetCPPType() : TEXT(""));
	}
	if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		Data->SetStringField(TEXT("container"), TEXT("map"));
		Data->SetStringField(TEXT("key_type"), MapProperty->KeyProp ? MapProperty->KeyProp->GetCPPType() : TEXT(""));
		Data->SetStringField(TEXT("value_type"), MapProperty->ValueProp ? MapProperty->ValueProp->GetCPPType() : TEXT(""));
	}
	if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		Data->SetStringField(TEXT("container"), TEXT("set"));
		Data->SetStringField(TEXT("element_type"), SetProperty->ElementProp ? SetProperty->ElementProp->GetCPPType() : TEXT(""));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildPropertyDefinition(FProperty* Property, bool bIncludeMetadata)
{
	TSharedPtr<FJsonObject> Data = BuildPropertySummary(Property, nullptr, nullptr, false);
	if (!Property)
	{
		return Data;
	}

	Data->SetStringField(TEXT("owner"), Property->GetOwnerStruct() ? Property->GetOwnerStruct()->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("authored_name"), Property->GetAuthoredName());
	Data->SetNumberField(TEXT("array_dim"), Property->ArrayDim);
	Data->SetStringField(TEXT("flags_hex"), FString::Printf(TEXT("0x%016llx"), static_cast<unsigned long long>(Property->GetPropertyFlags())));
	Data->SetObjectField(TEXT("type_details"), BuildPropertyTypeDetails(Property));
	if (bIncludeMetadata)
	{
		Data->SetObjectField(TEXT("metadata"), BuildKnownMetadataJson(Property));
	}
	return Data;
}

TSharedPtr<FJsonObject> BuildFunctionDefinition(UFunction* Function, bool bIncludeMetadata)
{
	TSharedPtr<FJsonObject> Data = BuildFunctionSummary(Function);
	if (!Function)
	{
		return Data;
	}

	Data->SetStringField(TEXT("path"), Function->GetPathName());
	Data->SetStringField(TEXT("owner"), Function->GetOwnerClass() ? Function->GetOwnerClass()->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("flags_hex"), FString::Printf(TEXT("0x%08x"), Function->FunctionFlags));
	Data->SetBoolField(TEXT("static"), Function->HasAnyFunctionFlags(FUNC_Static));
	Data->SetBoolField(TEXT("pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
	Data->SetBoolField(TEXT("editor_only"), Function->HasAnyFunctionFlags(FUNC_EditorOnly));
	if (bIncludeMetadata)
	{
		Data->SetObjectField(TEXT("metadata"), BuildKnownMetadataJson(Function));
	}
	return Data;
}

FString EnumCppFormToString(UEnum::ECppForm Form)
{
	switch (Form)
	{
	case UEnum::ECppForm::Regular:
		return TEXT("Regular");
	case UEnum::ECppForm::Namespaced:
		return TEXT("Namespaced");
	case UEnum::ECppForm::EnumClass:
		return TEXT("EnumClass");
	default:
		return TEXT("Unknown");
	}
}

TSharedPtr<FJsonObject> BuildEnumMetadataJson(const UEnum* Enum, int32 Index = INDEX_NONE)
{
	TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
	if (!Enum)
	{
		return Metadata;
	}

	static const TCHAR* MetadataKeys[] = {
		TEXT("DisplayName"),
		TEXT("ToolTip"),
		TEXT("Hidden"),
		TEXT("Spacer"),
		TEXT("Bitflags"),
		TEXT("UseEnumValuesAsMaskValuesInEditor"),
		TEXT("ModuleRelativePath"),
	};

	for (const TCHAR* Key : MetadataKeys)
	{
		if (Enum->HasMetaData(Key, Index))
		{
			Metadata->SetStringField(Key, Enum->GetMetaData(Key, Index));
		}
	}
	return Metadata;
}

UClass* ResolveClassByNameOrPath(const FString& ClassNameOrPath)
{
	const FString Query = ClassNameOrPath.TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* Class = FindObject<UClass>(nullptr, *Query))
	{
		return Class;
	}
	if (UClass* Class = LoadObject<UClass>(nullptr, *Query))
	{
		return Class;
	}
	if (UClass* Class = StaticLoadClass(UObject::StaticClass(), nullptr, *Query))
	{
		return Class;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class)
		{
			continue;
		}

		const FString AuthoredCppName = FString(Class->GetPrefixCPP()) + Class->GetAuthoredName();
		if (Class->GetName().Equals(Query, ESearchCase::IgnoreCase) ||
			Class->GetAuthoredName().Equals(Query, ESearchCase::IgnoreCase) ||
			AuthoredCppName.Equals(Query, ESearchCase::IgnoreCase) ||
			Class->GetPathName().Equals(Query, ESearchCase::IgnoreCase))
		{
			return Class;
		}
	}

	return nullptr;
}

UScriptStruct* ResolveStructByNameOrPath(const FString& StructNameOrPath)
{
	const FString Query = StructNameOrPath.TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		return nullptr;
	}

	if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *Query))
	{
		return Struct;
	}
	if (UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Query))
	{
		return Struct;
	}

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct)
		{
			continue;
		}

		const FString AuthoredCppName = FString(Struct->GetPrefixCPP()) + Struct->GetAuthoredName();
		if (Struct->GetName().Equals(Query, ESearchCase::IgnoreCase) ||
			Struct->GetAuthoredName().Equals(Query, ESearchCase::IgnoreCase) ||
			AuthoredCppName.Equals(Query, ESearchCase::IgnoreCase) ||
			Struct->GetPathName().Equals(Query, ESearchCase::IgnoreCase))
		{
			return Struct;
		}
	}

	return nullptr;
}

UEnum* ResolveEnumByNameOrPath(const FString& EnumNameOrPath)
{
	const FString Query = EnumNameOrPath.TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		return nullptr;
	}

	if (UEnum* Enum = FindObject<UEnum>(nullptr, *Query))
	{
		return Enum;
	}
	if (UEnum* Enum = LoadObject<UEnum>(nullptr, *Query))
	{
		return Enum;
	}

	for (TObjectIterator<UEnum> It; It; ++It)
	{
		UEnum* Enum = *It;
		if (!Enum)
		{
			continue;
		}

		if (Enum->GetName().Equals(Query, ESearchCase::IgnoreCase) ||
			Enum->GetAuthoredName().Equals(Query, ESearchCase::IgnoreCase) ||
			Enum->CppType.Equals(Query, ESearchCase::IgnoreCase) ||
			Enum->GetPathName().Equals(Query, ESearchCase::IgnoreCase))
		{
			return Enum;
		}
	}

	return nullptr;
}

TSharedPtr<FJsonObject> BuildStructReflection(UStruct* Struct, TSharedPtr<FJsonObject> Params, const FString& Kind)
{
	bool bIncludeProperties = true;
	bool bIncludeSuper = true;
	bool bIncludeMetadata = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_properties"), bIncludeProperties);
		Params->TryGetBoolField(TEXT("include_super"), bIncludeSuper);
		Params->TryGetBoolField(TEXT("include_metadata"), bIncludeMetadata);
	}
	const int32 Limit = ReadLimit(Params, 500, 10000);
	const EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeSuper ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("kind"), Kind);
	Data->SetStringField(TEXT("name"), Struct ? Struct->GetName() : TEXT(""));
	Data->SetStringField(TEXT("authored_name"), Struct ? Struct->GetAuthoredName() : TEXT(""));
	Data->SetStringField(TEXT("cpp_name"), Struct ? FString(Struct->GetPrefixCPP()) + Struct->GetAuthoredName() : TEXT(""));
	Data->SetStringField(TEXT("path"), Struct ? Struct->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("package"), Struct && Struct->GetOutermost() ? Struct->GetOutermost()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("super"), Struct && Struct->GetSuperStruct() ? Struct->GetSuperStruct()->GetPathName() : TEXT(""));
	if (bIncludeMetadata && Struct)
	{
		Data->SetObjectField(TEXT("metadata"), BuildKnownMetadataJson(Struct));
	}

	if (bIncludeProperties && Struct)
	{
		TArray<TSharedPtr<FJsonValue>> Properties;
		int32 TotalCount = 0;
		for (TFieldIterator<FProperty> It(Struct, SuperFlags); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			++TotalCount;
			if (Properties.Num() < Limit)
			{
				Properties.Add(MakeShared<FJsonValueObject>(BuildPropertyDefinition(Property, bIncludeMetadata)));
			}
		}
		Data->SetArrayField(TEXT("properties"), Properties);
		Data->SetNumberField(TEXT("property_count"), TotalCount);
		Data->SetBoolField(TEXT("properties_truncated"), TotalCount > Properties.Num());
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildClassReflection(UClass* Class, TSharedPtr<FJsonObject> Params)
{
	bool bIncludeFunctions = true;
	bool bIncludeSuper = true;
	bool bIncludeMetadata = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_functions"), bIncludeFunctions);
		Params->TryGetBoolField(TEXT("include_super"), bIncludeSuper);
		Params->TryGetBoolField(TEXT("include_metadata"), bIncludeMetadata);
	}
	const int32 Limit = ReadLimit(Params, 500, 10000);
	const EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeSuper ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper;

	TSharedPtr<FJsonObject> Data = BuildStructReflection(Class, Params, TEXT("class"));
	if (!Class)
	{
		return Data;
	}

	Data->SetArrayField(TEXT("class_flags"), BuildClassFlagArray(Class));
	Data->SetBoolField(TEXT("abstract"), Class->HasAnyClassFlags(CLASS_Abstract));
	Data->SetBoolField(TEXT("native"), Class->HasAnyClassFlags(CLASS_Native));
	Data->SetBoolField(TEXT("interface"), Class->HasAnyClassFlags(CLASS_Interface));
	Data->SetStringField(TEXT("config_name"), Class->ClassConfigName.ToString());
	Data->SetStringField(TEXT("default_object"), Class->GetDefaultObject(false) ? Class->GetDefaultObject(false)->GetPathName() : TEXT(""));
	if (UObject* GeneratedBy = Class->ClassGeneratedBy.Get())
	{
		Data->SetStringField(TEXT("generated_by"), GeneratedBy->GetPathName());
	}

	if (bIncludeFunctions)
	{
		TArray<TSharedPtr<FJsonValue>> Functions;
		int32 TotalCount = 0;
		for (TFieldIterator<UFunction> It(Class, SuperFlags); It; ++It)
		{
			UFunction* Function = *It;
			if (!Function)
			{
				continue;
			}

			++TotalCount;
			if (Functions.Num() < Limit)
			{
				Functions.Add(MakeShared<FJsonValueObject>(BuildFunctionDefinition(Function, bIncludeMetadata)));
			}
		}
		Data->SetArrayField(TEXT("functions"), Functions);
		Data->SetNumberField(TEXT("function_count"), TotalCount);
		Data->SetBoolField(TEXT("functions_truncated"), TotalCount > Functions.Num());
	}

	return Data;
}

TSharedPtr<FJsonObject> BuildEnumReflection(UEnum* Enum, TSharedPtr<FJsonObject> Params)
{
	bool bIncludeHidden = false;
	bool bIncludeMetadata = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("include_hidden"), bIncludeHidden);
		Params->TryGetBoolField(TEXT("include_metadata"), bIncludeMetadata);
	}
	const int32 Limit = ReadLimit(Params, 500, 10000);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("kind"), TEXT("enum"));
	Data->SetStringField(TEXT("name"), Enum ? Enum->GetName() : TEXT(""));
	Data->SetStringField(TEXT("authored_name"), Enum ? Enum->GetAuthoredName() : TEXT(""));
	Data->SetStringField(TEXT("path"), Enum ? Enum->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("package"), Enum && Enum->GetOutermost() ? Enum->GetOutermost()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("cpp_type"), Enum ? Enum->CppType : TEXT(""));
	Data->SetStringField(TEXT("cpp_form"), Enum ? EnumCppFormToString(Enum->GetCppForm()) : TEXT(""));
	Data->SetBoolField(TEXT("flags_enum"), Enum ? Enum->HasAnyEnumFlags(EEnumFlags::Flags) : false);
	if (bIncludeMetadata && Enum)
	{
		Data->SetObjectField(TEXT("metadata"), BuildEnumMetadataJson(Enum));
	}

	TArray<TSharedPtr<FJsonValue>> Enumerators;
	int32 TotalCount = 0;
	if (Enum)
	{
		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			const bool bHidden = Enum->HasMetaData(TEXT("Hidden"), Index);
			if (bHidden && !bIncludeHidden)
			{
				continue;
			}

			++TotalCount;
			if (Enumerators.Num() >= Limit)
			{
				continue;
			}

			TSharedPtr<FJsonObject> Enumerator = MakeShared<FJsonObject>();
			const int64 Value = Enum->GetValueByIndex(Index);
			Enumerator->SetStringField(TEXT("name"), Enum->GetNameStringByIndex(Index));
			Enumerator->SetStringField(TEXT("authored_name"), Enum->GetAuthoredNameStringByIndex(Index));
			Enumerator->SetStringField(TEXT("display_name"), Enum->GetDisplayNameTextByIndex(Index).ToString());
			Enumerator->SetNumberField(TEXT("value"), static_cast<double>(Value));
			Enumerator->SetStringField(TEXT("value_string"), LexToString(Value));
			Enumerator->SetBoolField(TEXT("hidden"), bHidden);
			if (bIncludeMetadata)
			{
				Enumerator->SetObjectField(TEXT("metadata"), BuildEnumMetadataJson(Enum, Index));
			}
			Enumerators.Add(MakeShared<FJsonValueObject>(Enumerator));
		}
	}

	Data->SetArrayField(TEXT("enumerators"), Enumerators);
	Data->SetNumberField(TEXT("enumerator_count"), TotalCount);
	Data->SetBoolField(TEXT("enumerators_truncated"), TotalCount > Enumerators.Num());
	return Data;
}

TSharedPtr<FJsonObject> BuildClassListItem(UClass* Class)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	if (!Class)
	{
		return Data;
	}

	Data->SetStringField(TEXT("name"), Class->GetName());
	Data->SetStringField(TEXT("authored_name"), Class->GetAuthoredName());
	Data->SetStringField(TEXT("cpp_name"), FString(Class->GetPrefixCPP()) + Class->GetAuthoredName());
	Data->SetStringField(TEXT("path"), Class->GetPathName());
	Data->SetStringField(TEXT("package"), Class->GetOutermost() ? Class->GetOutermost()->GetName() : TEXT(""));
	Data->SetStringField(TEXT("super"), Class->GetSuperClass() ? Class->GetSuperClass()->GetPathName() : TEXT(""));
	Data->SetBoolField(TEXT("abstract"), Class->HasAnyClassFlags(CLASS_Abstract));
	Data->SetBoolField(TEXT("native"), Class->HasAnyClassFlags(CLASS_Native));
	Data->SetBoolField(TEXT("interface"), Class->HasAnyClassFlags(CLASS_Interface));
	if (UObject* GeneratedBy = Class->ClassGeneratedBy.Get())
	{
		Data->SetStringField(TEXT("generated_by"), GeneratedBy->GetPathName());
	}
	return Data;
}

TArray<TSharedPtr<FJsonValue>> GameplayTagContainerToJsonNames(const FGameplayTagContainer& Container, const FString& ExcludeTag = FString())
{
	TArray<FGameplayTag> Tags;
	Container.GetGameplayTagArray(Tags);
	Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FGameplayTag& Tag : Tags)
	{
		const FString TagName = Tag.ToString();
		if (!ExcludeTag.IsEmpty() && TagName == ExcludeTag)
		{
			continue;
		}
		Values.Add(MakeShared<FJsonValueString>(TagName));
	}
	return Values;
}

TSharedPtr<FJsonObject> BuildGameplayTagJson(const FGameplayTag& Tag, bool bIncludeMetadata, bool bIncludeParents, bool bIncludeChildren, bool bOnlyDictionaryTags)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	const FString TagName = Tag.ToString();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), TagName);
	Data->SetStringField(TEXT("leaf"), TagName.Contains(TEXT(".")) ? FPaths::GetExtension(TagName, false) : TagName);

	if (bIncludeMetadata)
	{
		FString Comment;
		TArray<FName> Sources;
		bool bIsExplicit = false;
		bool bIsRestricted = false;
		bool bAllowNonRestrictedChildren = false;
		if (Manager.GetTagEditorData(Tag.GetTagName(), Comment, Sources, bIsExplicit, bIsRestricted, bAllowNonRestrictedChildren))
		{
			TArray<TSharedPtr<FJsonValue>> SourceValues;
			for (const FName& Source : Sources)
			{
				SourceValues.Add(MakeShared<FJsonValueString>(Source.ToString()));
			}
			Data->SetStringField(TEXT("comment"), Comment);
			Data->SetArrayField(TEXT("sources"), SourceValues);
			Data->SetBoolField(TEXT("explicit"), bIsExplicit);
			Data->SetBoolField(TEXT("restricted"), bIsRestricted);
			Data->SetBoolField(TEXT("allow_non_restricted_children"), bAllowNonRestrictedChildren);
		}
	}

	if (bIncludeParents)
	{
		Data->SetArrayField(TEXT("parents"), GameplayTagContainerToJsonNames(Manager.RequestGameplayTagParents(Tag), TagName));
	}

	if (bIncludeChildren)
	{
		const FGameplayTagContainer Children = bOnlyDictionaryTags ? Manager.RequestGameplayTagChildrenInDictionary(Tag) : Manager.RequestGameplayTagChildren(Tag);
		Data->SetArrayField(TEXT("children"), GameplayTagContainerToJsonNames(Children, TagName));
	}

	return Data;
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
		UObject* Asset = UMCTDataAssetBuilder::LoadAssetObject(AssetPath);
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
		const FScopedTransaction Transaction(NSLOCTEXT("MCPToolkit", "ObjectSetProperty", "AI Set Object Property"));
		Object->Modify();
		const bool bSet = UMCTDataAssetBuilder::SetProperty(Object, PropertyPath, ImportText);

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
		void* ParamData = nullptr;
		if (Function->ParmsSize > 0)
		{
			ParamBuffer.SetNumZeroed(Function->ParmsSize);
			ParamData = ParamBuffer.GetData();
			Function->InitializeStruct(ParamData);
		}
		ON_SCOPE_EXIT
		{
			if (ParamData)
			{
				Function->DestroyStruct(ParamData);
			}
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
			if (!ParamData)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Function %s has no parameter storage for argument %s"), *FunctionName, *Param->GetName())));
				return;
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
			if (!ParamData)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Function %s has no parameter storage for output %s"), *FunctionName, *Param->GetName())));
				return;
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

FString HandleReflectClass(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise]()
	{
		FString ClassQuery;
		if (Params.IsValid())
		{
			Params->TryGetStringField(TEXT("class_path"), ClassQuery);
			if (ClassQuery.IsEmpty())
			{
				Params->TryGetStringField(TEXT("class_name"), ClassQuery);
			}
		}
		if (ClassQuery.IsEmpty())
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Missing 'class_path' or 'class_name' parameter")));
			return;
		}

		UClass* Class = ResolveClassByNameOrPath(ClassQuery);
		if (!Class)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Class not found: %s"), *ClassQuery)));
			return;
		}

		Promise->SetValue(CreateSuccessResponse(BuildClassReflection(Class, Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Class reflection timed out"));
	return Future.Get();
}

FString HandleReflectStruct(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise]()
	{
		FString StructQuery;
		if (Params.IsValid())
		{
			Params->TryGetStringField(TEXT("struct_path"), StructQuery);
			if (StructQuery.IsEmpty())
			{
				Params->TryGetStringField(TEXT("struct_name"), StructQuery);
			}
		}
		if (StructQuery.IsEmpty())
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Missing 'struct_path' or 'struct_name' parameter")));
			return;
		}

		UScriptStruct* Struct = ResolveStructByNameOrPath(StructQuery);
		if (!Struct)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Struct not found: %s"), *StructQuery)));
			return;
		}

		Promise->SetValue(CreateSuccessResponse(BuildStructReflection(Struct, Params, TEXT("struct"))));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Struct reflection timed out"));
	return Future.Get();
}

FString HandleReflectEnum(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise]()
	{
		FString EnumQuery;
		if (Params.IsValid())
		{
			Params->TryGetStringField(TEXT("enum_path"), EnumQuery);
			if (EnumQuery.IsEmpty())
			{
				Params->TryGetStringField(TEXT("enum_name"), EnumQuery);
			}
		}
		if (EnumQuery.IsEmpty())
		{
			Promise->SetValue(CreateErrorResponse(TEXT("Missing 'enum_path' or 'enum_name' parameter")));
			return;
		}

		UEnum* Enum = ResolveEnumByNameOrPath(EnumQuery);
		if (!Enum)
		{
			Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Enum not found: %s"), *EnumQuery)));
			return;
		}

		Promise->SetValue(CreateSuccessResponse(BuildEnumReflection(Enum, Params)));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("Enum reflection timed out"));
	return Future.Get();
}

FString HandleListClasses(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise]()
	{
		FString Query;
		FString ParentClassQuery;
		FString PackagePrefix;
		bool bIncludeAbstract = true;
		bool bIncludeDeprecated = false;
		bool bIncludeInterfaces = true;
		bool bIncludeBlueprintGenerated = true;
		if (Params.IsValid())
		{
			Params->TryGetStringField(TEXT("query"), Query);
			Params->TryGetStringField(TEXT("parent_class"), ParentClassQuery);
			Params->TryGetStringField(TEXT("package_prefix"), PackagePrefix);
			Params->TryGetBoolField(TEXT("include_abstract"), bIncludeAbstract);
			Params->TryGetBoolField(TEXT("include_deprecated"), bIncludeDeprecated);
			Params->TryGetBoolField(TEXT("include_interfaces"), bIncludeInterfaces);
			Params->TryGetBoolField(TEXT("include_blueprint_generated"), bIncludeBlueprintGenerated);
		}
		const int32 Limit = ReadLimit(Params, 200, 20000);

		UClass* ParentClass = nullptr;
		if (!ParentClassQuery.IsEmpty())
		{
			ParentClass = ResolveClassByNameOrPath(ParentClassQuery);
			if (!ParentClass)
			{
				Promise->SetValue(CreateErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *ParentClassQuery)));
				return;
			}
		}

		TArray<UClass*> Classes;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class)
			{
				continue;
			}
			if (!bIncludeAbstract && Class->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			if (!bIncludeDeprecated && Class->HasAnyClassFlags(CLASS_Deprecated))
			{
				continue;
			}
			if (!bIncludeInterfaces && Class->HasAnyClassFlags(CLASS_Interface))
			{
				continue;
			}
			if (!bIncludeBlueprintGenerated && Class->ClassGeneratedBy.Get())
			{
				continue;
			}
			if (ParentClass && !Class->IsChildOf(ParentClass))
			{
				continue;
			}

			const FString Path = Class->GetPathName();
			if (!PackagePrefix.IsEmpty() && !Path.StartsWith(PackagePrefix))
			{
				continue;
			}
			if (!MatchesFilter(Class->GetName(), Path, Query))
			{
				continue;
			}

			Classes.Add(Class);
		}

		Classes.Sort([](const UClass& Left, const UClass& Right)
		{
			return Left.GetPathName() < Right.GetPathName();
		});

		TArray<TSharedPtr<FJsonValue>> ClassItems;
		for (int32 Index = 0; Index < Classes.Num() && Index < Limit; ++Index)
		{
			ClassItems.Add(MakeShared<FJsonValueObject>(BuildClassListItem(Classes[Index])));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("query"), Query);
		Data->SetStringField(TEXT("parent_class"), ParentClass ? ParentClass->GetPathName() : TEXT(""));
		Data->SetStringField(TEXT("package_prefix"), PackagePrefix);
		Data->SetNumberField(TEXT("class_count"), Classes.Num());
		Data->SetArrayField(TEXT("classes"), ClassItems);
		Data->SetBoolField(TEXT("classes_truncated"), Classes.Num() > ClassItems.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List classes timed out"));
	return Future.Get();
}

FString HandleListGameplayTags(TSharedPtr<FJsonObject> Params)
{
	TSharedPtr<TPromise<FString>> Promise = MakeShared<TPromise<FString>>();
	TFuture<FString> Future = Promise->GetFuture();

	AsyncTask(ENamedThreads::GameThread, [Params, Promise]()
	{
		FString RootTag;
		FString Query;
		bool bOnlyDictionaryTags = true;
		bool bIncludeMetadata = true;
		bool bIncludeParents = false;
		bool bIncludeChildren = false;
		if (Params.IsValid())
		{
			Params->TryGetStringField(TEXT("root_tag"), RootTag);
			Params->TryGetStringField(TEXT("query"), Query);
			Params->TryGetBoolField(TEXT("only_dictionary_tags"), bOnlyDictionaryTags);
			Params->TryGetBoolField(TEXT("include_metadata"), bIncludeMetadata);
			Params->TryGetBoolField(TEXT("include_parents"), bIncludeParents);
			Params->TryGetBoolField(TEXT("include_children"), bIncludeChildren);
		}
		const int32 Limit = ReadLimit(Params, 500, 50000);

		FGameplayTagContainer AllTags;
		UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, bOnlyDictionaryTags);

		TArray<FGameplayTag> Tags;
		AllTags.GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		TArray<FGameplayTag> FilteredTags;
		for (const FGameplayTag& Tag : Tags)
		{
			const FString TagName = Tag.ToString();
			if (!RootTag.IsEmpty() && TagName != RootTag && !TagName.StartsWith(RootTag + TEXT(".")))
			{
				continue;
			}
			if (!Query.IsEmpty() && !TagName.Contains(Query, ESearchCase::IgnoreCase))
			{
				continue;
			}
			FilteredTags.Add(Tag);
		}

		TArray<TSharedPtr<FJsonValue>> TagItems;
		for (int32 Index = 0; Index < FilteredTags.Num() && Index < Limit; ++Index)
		{
			TagItems.Add(MakeShared<FJsonValueObject>(BuildGameplayTagJson(FilteredTags[Index], bIncludeMetadata, bIncludeParents, bIncludeChildren, bOnlyDictionaryTags)));
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("root_tag"), RootTag);
		Data->SetStringField(TEXT("query"), Query);
		Data->SetBoolField(TEXT("only_dictionary_tags"), bOnlyDictionaryTags);
		Data->SetBoolField(TEXT("include_metadata"), bIncludeMetadata);
		Data->SetBoolField(TEXT("include_parents"), bIncludeParents);
		Data->SetBoolField(TEXT("include_children"), bIncludeChildren);
		Data->SetNumberField(TEXT("total_tag_count"), Tags.Num());
		Data->SetNumberField(TEXT("matched_tag_count"), FilteredTags.Num());
		Data->SetArrayField(TEXT("tags"), TagItems);
		Data->SetBoolField(TEXT("tags_truncated"), FilteredTags.Num() > TagItems.Num());
		Promise->SetValue(CreateSuccessResponse(Data));
	});

	Future.WaitFor(FTimespan::FromSeconds(60.0));
	if (!Future.IsReady()) return CreateErrorResponse(TEXT("List gameplay tags timed out"));
	return Future.Get();
}
}
