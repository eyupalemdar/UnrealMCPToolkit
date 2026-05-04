// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/MCTDataAssetBuilder.h"
#include "Builders/MCTWidgetBlueprintBuilder.h"

#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MCTDataAssetBuilder"

DEFINE_LOG_CATEGORY_STATIC(LogMCTDataAssetBuilder, Log, All);

// =============================================================================
// ASSET LIFECYCLE
// =============================================================================

UObject* UMCTDataAssetBuilder::LoadAssetObject(const FString& AssetPath)
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
		UE_LOG(LogMCTDataAssetBuilder, Error, TEXT("LoadAssetObject: Not found at '%s'"), *AssetPath);
	}
	return Asset;
}

bool UMCTDataAssetBuilder::SaveAsset(UObject* Asset)
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
		UE_LOG(LogMCTDataAssetBuilder, Log, TEXT("SaveAsset: Saved '%s'"), *Asset->GetName());
	}
	else
	{
		UE_LOG(LogMCTDataAssetBuilder, Error, TEXT("SaveAsset: Failed to save '%s'"), *Asset->GetName());
	}
	return bResult;
}

// =============================================================================
// NESTED PATH RESOLUTION
// =============================================================================

/**
 * Parse a segment like "Actions[0]" into property name "Actions" and index 0.
 * If no bracket notation, index is set to -1.
 * Returns false if the segment is empty or malformed (empty prop name, bad index).
 */
static bool ParseSegment(const FString& Segment, FString& OutPropName, int32& OutIndex)
{
	if (Segment.IsEmpty())
	{
		OutPropName.Reset();
		OutIndex = -1;
		return false;
	}

	int32 BracketIdx = INDEX_NONE;
	if (Segment.FindChar(TEXT('['), BracketIdx))
	{
		OutPropName = Segment.Left(BracketIdx);
		FString IndexStr = Segment.Mid(BracketIdx + 1);
		IndexStr.RemoveFromEnd(TEXT("]"));
		if (OutPropName.IsEmpty() || IndexStr.IsEmpty() || !IndexStr.IsNumeric())
		{
			return false;
		}
		OutIndex = FCString::Atoi(*IndexStr);
		return OutIndex >= 0;
	}

	OutPropName = Segment;
	OutIndex = -1;
	return true;
}

/**
 * Walk a property path and set the leaf value using reflection.
 *
 * Supports struct / object traversal, struct-array element navigation, and
 * object-array element navigation at any depth. Example paths:
 *
 *   "Actions[0].Layout[0].LayoutClass"
 *   "Widgets[2].WidgetClass"
 *   "Foo.Bar.Baz"
 *
 * The previous implementation bounced back into SetProperty for paths rooted
 * at a struct-array element, which caused infinite recursion and eventually
 * a corrupted FName construction. This walker fully consumes the path
 * without relying on UObject re-entry.
 */
static bool WalkAndSet(
	UStruct* OwnerStruct,
	void* Container,
	const TArray<FString>& PathParts,
	int32 StartIdx,
	const FString& Value,
	UObject* AssetForImport)
{
	if (!OwnerStruct || !Container || !AssetForImport) { return false; }
	if (StartIdx >= PathParts.Num()) { return false; }

	FString PropName;
	int32 ArrayIndex = -1;
	if (!ParseSegment(PathParts[StartIdx], PropName, ArrayIndex))
	{
		UE_LOG(LogMCTDataAssetBuilder, Warning,
			TEXT("WalkAndSet: malformed segment '%s'"), *PathParts[StartIdx]);
		return false;
	}

	FProperty* Prop = OwnerStruct->FindPropertyByName(FName(*PropName));
	if (!Prop)
	{
		UE_LOG(LogMCTDataAssetBuilder, Warning,
			TEXT("WalkAndSet: property '%s' not found on %s"),
			*PropName, *OwnerStruct->GetName());
		return false;
	}

	const bool bIsLast = (StartIdx == PathParts.Num() - 1);

	if (ArrayIndex >= 0)
	{
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
		if (!ArrayProp)
		{
			UE_LOG(LogMCTDataAssetBuilder, Warning,
				TEXT("WalkAndSet: '%s' is not an array property"), *PropName);
			return false;
		}

		FScriptArrayHelper ArrayHelper(ArrayProp,
			ArrayProp->ContainerPtrToValuePtr<void>(Container));
		if (ArrayIndex >= ArrayHelper.Num())
		{
			UE_LOG(LogMCTDataAssetBuilder, Warning,
				TEXT("WalkAndSet: index %d out of range on '%s' (size=%d)"),
				ArrayIndex, *PropName, ArrayHelper.Num());
			return false;
		}

		void* ElemPtr = ArrayHelper.GetRawPtr(ArrayIndex);

		if (bIsLast)
		{
			// Replace the element in-place via inner property's ImportText.
			if (!ArrayProp->Inner->ImportText_Direct(*Value, ElemPtr, AssetForImport, PPF_None))
			{
				return false;
			}
			AssetForImport->MarkPackageDirty();
			return true;
		}

		// Continue walking into the element.
		if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
		{
			return WalkAndSet(InnerStruct->Struct, ElemPtr, PathParts, StartIdx + 1, Value, AssetForImport);
		}
		if (FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner))
		{
			UObject* InnerObj = InnerObjProp->GetObjectPropertyValue(ElemPtr);
			if (!InnerObj)
			{
				UE_LOG(LogMCTDataAssetBuilder, Warning,
					TEXT("WalkAndSet: null object at '%s[%d]'"), *PropName, ArrayIndex);
				return false;
			}
			return WalkAndSet(InnerObj->GetClass(), InnerObj, PathParts, StartIdx + 1, Value, InnerObj);
		}

		UE_LOG(LogMCTDataAssetBuilder, Warning,
			TEXT("WalkAndSet: array '%s' has unsupported inner type for traversal"), *PropName);
		return false;
	}

	// No array index — direct property on Container.
	if (bIsLast)
	{
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
		if (!Prop->ImportText_Direct(*Value, ValuePtr, AssetForImport, PPF_None))
		{
			return false;
		}
		AssetForImport->MarkPackageDirty();
		return true;
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Container);
		return WalkAndSet(StructProp->Struct, StructPtr, PathParts, StartIdx + 1, Value, AssetForImport);
	}
	if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
	{
		UObject* InnerObj = ObjProp->GetObjectPropertyValue(
			ObjProp->ContainerPtrToValuePtr<void>(Container));
		if (!InnerObj)
		{
			UE_LOG(LogMCTDataAssetBuilder, Warning,
				TEXT("WalkAndSet: null object at '%s'"), *PropName);
			return false;
		}
		return WalkAndSet(InnerObj->GetClass(), InnerObj, PathParts, StartIdx + 1, Value, InnerObj);
	}

	UE_LOG(LogMCTDataAssetBuilder, Warning,
		TEXT("WalkAndSet: '%s' is not navigable (not struct/object/array)"), *PropName);
	return false;
}

bool UMCTDataAssetBuilder::ResolveNestedPath(
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
			UE_LOG(LogMCTDataAssetBuilder, Warning,
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
				UE_LOG(LogMCTDataAssetBuilder, Warning,
					TEXT("ResolveNestedPath: '%s' is not an array property"),
					*PropName);
				return false;
			}

			FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CurrentObject));
			if (ArrayIndex >= ArrayHelper.Num())
			{
				UE_LOG(LogMCTDataAssetBuilder, Warning,
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
					UE_LOG(LogMCTDataAssetBuilder, Warning,
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
					UE_LOG(LogMCTDataAssetBuilder, Warning,
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

int32 UMCTDataAssetBuilder::AddArrayElement(
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
		UE_LOG(LogMCTDataAssetBuilder, Warning,
			TEXT("AddArrayElement: Failed to resolve path '%s'"), *ArrayPath);
		return -1;
	}

	return UMCTWidgetBlueprintBuilder::AddArrayElement(TargetObject, PropertyName, ElementValues, ClassName);
}

bool UMCTDataAssetBuilder::RemoveArrayElement(
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

	return UMCTWidgetBlueprintBuilder::RemoveArrayElement(TargetObject, PropertyName, Index);
}

int32 UMCTDataAssetBuilder::GetArrayLength(
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

	return UMCTWidgetBlueprintBuilder::GetArrayLength(TargetObject, PropertyName);
}

bool UMCTDataAssetBuilder::SetArrayElementProperty(
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

	return UMCTWidgetBlueprintBuilder::SetArrayElementProperty(
		TargetObject, PropertyName, Index, SubPropertyName, Value);
}

// =============================================================================
// NESTED PATH PROPERTY OPERATIONS
// =============================================================================

bool UMCTDataAssetBuilder::SetProperty(
	UObject* Asset,
	const FString& PropertyPath,
	const FString& Value)
{
	if (!Asset || PropertyPath.IsEmpty()) { return false; }

	FScopedTransaction Transaction(LOCTEXT("AISetProperty", "AI: Set Data Asset Property"));

	TArray<FString> PathParts;
	PropertyPath.ParseIntoArray(PathParts, TEXT("."));
	if (PathParts.Num() == 0) { return false; }

	return WalkAndSet(Asset->GetClass(), Asset, PathParts, 0, Value, Asset);
}

TSharedPtr<FJsonObject> UMCTDataAssetBuilder::GetPropertiesAsJson(UObject* Asset)
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
