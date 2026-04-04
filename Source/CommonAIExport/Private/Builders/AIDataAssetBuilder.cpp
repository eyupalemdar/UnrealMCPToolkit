// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/AIDataAssetBuilder.h"
#include "Builders/AIWidgetBlueprintBuilder.h"

#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIDataAssetBuilder"

DEFINE_LOG_CATEGORY_STATIC(LogAIDataAssetBuilder, Log, All);

// =============================================================================
// ASSET LIFECYCLE
// =============================================================================

UObject* UAIDataAssetBuilder::LoadAssetObject(const FString& AssetPath)
{
	FString CleanPath = AssetPath;
	if (!CleanPath.Contains(TEXT(".")))
	{
		// Auto-append object name: "/Game/Data/DA_Foo" -> "/Game/Data/DA_Foo.DA_Foo"
		FString AssetName = FPaths::GetCleanFilename(CleanPath);
		CleanPath = CleanPath + TEXT(".") + AssetName;
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *CleanPath);
	if (!Asset)
	{
		// Fallback: try original path
		Asset = LoadObject<UObject>(nullptr, *AssetPath);
	}
	if (!Asset)
	{
		UE_LOG(LogAIDataAssetBuilder, Error, TEXT("LoadAssetObject: Not found at '%s'"), *AssetPath);
	}
	return Asset;
}

bool UAIDataAssetBuilder::SaveAsset(UObject* Asset)
{
	if (!Asset) return false;

	UPackage* Package = Asset->GetOutermost();
	if (!Package) return false;

	FString PackageFilename;
	FString PackagePath;

	if (FPackageName::DoesPackageExist(Package->GetName(), &PackageFilename))
	{
		PackagePath = PackageFilename;
	}
	else
	{
		PackagePath = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	bool bResult = UPackage::SavePackage(Package, Asset, *PackagePath, SaveArgs);

	if (bResult)
	{
		UE_LOG(LogAIDataAssetBuilder, Log, TEXT("SaveAsset: Saved '%s'"), *Asset->GetName());
	}
	else
	{
		UE_LOG(LogAIDataAssetBuilder, Error, TEXT("SaveAsset: Failed to save '%s'"), *Asset->GetName());
	}
	return bResult;
}

// =============================================================================
// NESTED PATH RESOLUTION
// =============================================================================

/**
 * Parse a segment like "Actions[0]" into property name "Actions" and index 0.
 * If no bracket notation, index is set to -1.
 */
static bool ParseSegment(const FString& Segment, FString& OutPropName, int32& OutIndex)
{
	int32 BracketIdx = INDEX_NONE;
	if (Segment.FindChar(TEXT('['), BracketIdx))
	{
		OutPropName = Segment.Left(BracketIdx);
		FString IndexStr = Segment.Mid(BracketIdx + 1);
		IndexStr.RemoveFromEnd(TEXT("]"));
		OutIndex = FCString::Atoi(*IndexStr);
		return true;
	}
	else
	{
		OutPropName = Segment;
		OutIndex = -1;
		return true;
	}
}

bool UAIDataAssetBuilder::ResolveNestedPath(
	UObject* RootObject,
	const FString& FullPath,
	UObject*& OutTargetObject,
	FString& OutPropertyName)
{
	if (!RootObject || FullPath.IsEmpty())
	{
		return false;
	}

	// Split by "." but respect bracket notation
	TArray<FString> Segments;
	FullPath.ParseIntoArray(Segments, TEXT("."));

	if (Segments.Num() == 0)
	{
		return false;
	}

	// If only one segment, no navigation needed
	if (Segments.Num() == 1)
	{
		OutTargetObject = RootObject;

		// Check if single segment has array index: "Actions[0]" -> just property "Actions"
		// But for our use case, array commands handle the index themselves
		OutPropertyName = Segments[0];
		return true;
	}

	// Navigate all segments except the last one
	UObject* CurrentObject = RootObject;

	for (int32 SegIdx = 0; SegIdx < Segments.Num() - 1; ++SegIdx)
	{
		FString PropName;
		int32 ArrayIndex;
		ParseSegment(Segments[SegIdx], PropName, ArrayIndex);

		FProperty* Prop = CurrentObject->GetClass()->FindPropertyByName(FName(*PropName));
		if (!Prop)
		{
			UE_LOG(LogAIDataAssetBuilder, Warning,
				TEXT("ResolveNestedPath: Property '%s' not found on %s"),
				*PropName, *CurrentObject->GetClass()->GetName());
			return false;
		}

		if (ArrayIndex >= 0)
		{
			// Array access: navigate into array element
			FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
			if (!ArrayProp)
			{
				UE_LOG(LogAIDataAssetBuilder, Warning,
					TEXT("ResolveNestedPath: '%s' is not an array property"),
					*PropName);
				return false;
			}

			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CurrentObject));
			if (ArrayIndex >= ArrayHelper.Num())
			{
				UE_LOG(LogAIDataAssetBuilder, Warning,
					TEXT("ResolveNestedPath: Index %d out of range (array '%s' has %d elements)"),
					ArrayIndex, *PropName, ArrayHelper.Num());
				return false;
			}

			// Check if inner type is UObject*
			FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner);
			if (InnerObjProp)
			{
				UObject* InnerObj = InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(ArrayIndex));
				if (!InnerObj)
				{
					UE_LOG(LogAIDataAssetBuilder, Warning,
						TEXT("ResolveNestedPath: Null object at '%s[%d]'"),
						*PropName, ArrayIndex);
					return false;
				}
				CurrentObject = InnerObj;
			}
			else
			{
				// Inner is a struct — we cannot navigate further with UObject*
				// For struct arrays, the remaining path becomes a nested struct property path
				// Reconstruct the remaining path including this segment's array index
				FString RemainingPath;
				for (int32 i = SegIdx; i < Segments.Num(); ++i)
				{
					if (!RemainingPath.IsEmpty()) RemainingPath += TEXT(".");
					RemainingPath += Segments[i];
				}
				OutTargetObject = CurrentObject;
				OutPropertyName = RemainingPath;
				return true;
			}
		}
		else
		{
			// Non-array: navigate into object property
			FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
			if (ObjProp)
			{
				UObject* InnerObj = ObjProp->GetObjectPropertyValue(
					ObjProp->ContainerPtrToValuePtr<void>(CurrentObject));
				if (!InnerObj)
				{
					UE_LOG(LogAIDataAssetBuilder, Warning,
						TEXT("ResolveNestedPath: Null object at '%s'"),
						*PropName);
					return false;
				}
				CurrentObject = InnerObj;
			}
			else
			{
				// Struct or other type — cannot navigate with UObject*
				// Return remaining path as-is
				FString RemainingPath;
				for (int32 i = SegIdx; i < Segments.Num(); ++i)
				{
					if (!RemainingPath.IsEmpty()) RemainingPath += TEXT(".");
					RemainingPath += Segments[i];
				}
				OutTargetObject = CurrentObject;
				OutPropertyName = RemainingPath;
				return true;
			}
		}
	}

	// Last segment is the property name
	OutTargetObject = CurrentObject;
	OutPropertyName = Segments.Last();
	return true;
}

// =============================================================================
// NESTED PATH ARRAY OPERATIONS
// =============================================================================

int32 UAIDataAssetBuilder::AddArrayElement(
	UObject* Asset,
	const FString& ArrayPath,
	const TMap<FString, FString>& ElementValues,
	const FString& ClassName)
{
	if (!Asset) return -1;

	UObject* TargetObject = nullptr;
	FString PropertyName;

	if (!ResolveNestedPath(Asset, ArrayPath, TargetObject, PropertyName))
	{
		UE_LOG(LogAIDataAssetBuilder, Warning,
			TEXT("AddArrayElement: Failed to resolve path '%s'"), *ArrayPath);
		return -1;
	}

	return UAIWidgetBlueprintBuilder::AddArrayElement(TargetObject, PropertyName, ElementValues, ClassName);
}

bool UAIDataAssetBuilder::RemoveArrayElement(
	UObject* Asset,
	const FString& ArrayPath,
	int32 Index)
{
	if (!Asset) return false;

	UObject* TargetObject = nullptr;
	FString PropertyName;

	if (!ResolveNestedPath(Asset, ArrayPath, TargetObject, PropertyName))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIRemoveArrayElement", "AI: Remove Array Element"));

	return UAIWidgetBlueprintBuilder::RemoveArrayElement(TargetObject, PropertyName, Index);
}

int32 UAIDataAssetBuilder::GetArrayLength(
	UObject* Asset,
	const FString& ArrayPath)
{
	if (!Asset) return -1;

	UObject* TargetObject = nullptr;
	FString PropertyName;

	if (!ResolveNestedPath(Asset, ArrayPath, TargetObject, PropertyName))
	{
		return -1;
	}

	return UAIWidgetBlueprintBuilder::GetArrayLength(TargetObject, PropertyName);
}

bool UAIDataAssetBuilder::SetArrayElementProperty(
	UObject* Asset,
	const FString& ArrayPath,
	int32 Index,
	const FString& SubPropertyName,
	const FString& Value)
{
	if (!Asset) return false;

	UObject* TargetObject = nullptr;
	FString PropertyName;

	if (!ResolveNestedPath(Asset, ArrayPath, TargetObject, PropertyName))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetArrayElementProperty", "AI: Set Array Element Property"));

	return UAIWidgetBlueprintBuilder::SetArrayElementProperty(
		TargetObject, PropertyName, Index, SubPropertyName, Value);
}

// =============================================================================
// NESTED PATH PROPERTY OPERATIONS
// =============================================================================

bool UAIDataAssetBuilder::SetProperty(
	UObject* Asset,
	const FString& PropertyPath,
	const FString& Value)
{
	if (!Asset) return false;

	FScopedTransaction Transaction(LOCTEXT("AISetProperty", "AI: Set Data Asset Property"));

	// For simple paths (no array brackets), directly set on the asset
	if (!PropertyPath.Contains(TEXT("[")))
	{
		// Use the same reflection pattern as WBP builder
		// Navigate dot-notation path and use ImportText on the leaf
		TArray<FString> PathParts;
		PropertyPath.ParseIntoArray(PathParts, TEXT("."));

		UStruct* CurrentStruct = Asset->GetClass();
		void* CurrentContainer = Asset;

		for (int32 i = 0; i < PathParts.Num(); ++i)
		{
			FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
			if (!Prop)
			{
				UE_LOG(LogAIDataAssetBuilder, Warning,
					TEXT("SetProperty: Property '%s' not found on %s"),
					*PathParts[i], *CurrentStruct->GetName());
				return false;
			}

			if (i == PathParts.Num() - 1)
			{
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentContainer);
				const TCHAR* ValueCStr = *Value;
				bool bResult = Prop->ImportText_Direct(ValueCStr, ValuePtr, Asset, PPF_None) != nullptr;
				if (bResult)
				{
					Asset->MarkPackageDirty();
				}
				return bResult;
			}
			else
			{
				FStructProperty* StructProp = CastField<FStructProperty>(Prop);
				if (!StructProp)
				{
					return false;
				}
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
			}
		}
		return false;
	}

	// For paths with array brackets, resolve nested path first
	UObject* TargetObject = nullptr;
	FString LeafPropertyName;

	if (!ResolveNestedPath(Asset, PropertyPath, TargetObject, LeafPropertyName))
	{
		return false;
	}

	// Recursively call SetProperty on the resolved target
	return SetProperty(TargetObject, LeafPropertyName, Value);
}

TSharedPtr<FJsonObject> UAIDataAssetBuilder::GetPropertiesAsJson(UObject* Asset)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Asset)
	{
		return Result;
	}

	// Get archetype for comparison (to filter defaults)
	UObject* Archetype = Asset->GetArchetype();

	for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient))
		{
			continue;
		}

		FString ValueStr;
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Asset);
		Prop->ExportText_Direct(ValueStr, ValuePtr, nullptr, Asset, PPF_None);

		Result->SetStringField(Prop->GetName(), ValueStr);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
