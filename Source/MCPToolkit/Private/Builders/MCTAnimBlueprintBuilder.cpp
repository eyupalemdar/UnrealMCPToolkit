// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/MCTAnimBlueprintBuilder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MCTAnimBlueprintBuilder)

// Animation
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"

// Factory
#include "Factories/AnimBlueprintFactory.h"

// Blueprint compile
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Asset management
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Undo/Redo
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MCTAnimBlueprintBuilder"

DEFINE_LOG_CATEGORY_STATIC(LogMCTAnimBlueprintBuilder, Log, All);

// =============================================================================
// UTILITY
// =============================================================================

UAnimBlueprint* UMCTAnimBlueprintBuilder::LoadAnimBlueprint(const FString& AssetPath)
{
	FString CleanPath = AssetPath;
	if (!CleanPath.Contains(TEXT(".")))
	{
		FString Name = FPaths::GetCleanFilename(CleanPath);
		CleanPath = CleanPath + TEXT(".") + Name;
	}

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *CleanPath);
	if (!AnimBP)
	{
		AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
	}
	if (!AnimBP)
	{
		UE_LOG(LogMCTAnimBlueprintBuilder, Error, TEXT("LoadAnimBlueprint: Not found at '%s'"), *AssetPath);
	}
	return AnimBP;
}

// =============================================================================
// LIFECYCLE
// =============================================================================

static UClass* ResolveAnimParentClass(const FString& ParentClass)
{
	// Default
	if (ParentClass.IsEmpty() || ParentClass.Equals(TEXT("AnimInstance"), ESearchCase::IgnoreCase))
	{
		return UAnimInstance::StaticClass();
	}

	// Try full path first
	UClass* Found = LoadClass<UAnimInstance>(nullptr, *ParentClass);
	if (Found)
	{
		return Found;
	}

	// Try with /Script/Engine. prefix
	FString ScriptPath = FString::Printf(TEXT("/Script/Engine.%s"), *ParentClass);
	Found = LoadClass<UAnimInstance>(nullptr, *ScriptPath);
	if (Found)
	{
		return Found;
	}

	UE_LOG(LogMCTAnimBlueprintBuilder, Warning,
		TEXT("ResolveAnimParentClass: '%s' not found, using UAnimInstance"), *ParentClass);
	return UAnimInstance::StaticClass();
}

UAnimBlueprint* UMCTAnimBlueprintBuilder::CreateAnimBlueprint(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& SkeletonPath,
	const FString& ParentClass)
{
	if (PackagePath.IsEmpty() || AssetName.IsEmpty() || SkeletonPath.IsEmpty())
	{
		UE_LOG(LogMCTAnimBlueprintBuilder, Error,
			TEXT("CreateAnimBlueprint: PackagePath, AssetName and SkeletonPath required"));
		return nullptr;
	}

	FString FullPath = PackagePath / AssetName;

	// Return existing if found
	if (UAnimBlueprint* Existing = LoadObject<UAnimBlueprint>(nullptr, *FullPath))
	{
		UE_LOG(LogMCTAnimBlueprintBuilder, Warning,
			TEXT("CreateAnimBlueprint: Already exists at '%s', returning existing"), *FullPath);
		return Existing;
	}

	// Load skeleton
	FString CleanSkeletonPath = SkeletonPath;
	if (!CleanSkeletonPath.Contains(TEXT(".")))
	{
		FString Name = FPaths::GetCleanFilename(CleanSkeletonPath);
		CleanSkeletonPath = CleanSkeletonPath + TEXT(".") + Name;
	}
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *CleanSkeletonPath);
	if (!Skeleton)
	{
		UE_LOG(LogMCTAnimBlueprintBuilder, Error,
			TEXT("CreateAnimBlueprint: Skeleton not found at '%s'"), *SkeletonPath);
		return nullptr;
	}

	// Resolve parent class
	UClass* AnimParentClass = ResolveAnimParentClass(ParentClass);

	FScopedTransaction Transaction(LOCTEXT("AICreateAnimBlueprint", "AI: Create AnimBlueprint"));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogMCTAnimBlueprintBuilder, Error,
			TEXT("CreateAnimBlueprint: Failed to create package at '%s'"), *FullPath);
		return nullptr;
	}

	// Use UAnimBlueprintFactory
	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->ParentClass = AnimParentClass;
	Factory->TargetSkeleton = Skeleton;

	UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(
		Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, FName(*AssetName),
			RF_Public | RF_Standalone, nullptr, GWarn));

	if (!NewAnimBP)
	{
		UE_LOG(LogMCTAnimBlueprintBuilder, Error,
			TEXT("CreateAnimBlueprint: Factory failed for '%s'"), *AssetName);
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(NewAnimBP);
	NewAnimBP->MarkPackageDirty();

	UE_LOG(LogMCTAnimBlueprintBuilder, Log,
		TEXT("CreateAnimBlueprint: Created '%s' (Skeleton=%s, Parent=%s)"),
		*FullPath, *Skeleton->GetName(), *AnimParentClass->GetName());
	return NewAnimBP;
}

bool UMCTAnimBlueprintBuilder::CompileAndSave(UAnimBlueprint* AnimBP, TArray<FString>* OutWarnings)
{
	if (!AnimBP)
	{
		UE_LOG(LogMCTAnimBlueprintBuilder, Error, TEXT("CompileAndSave: Null AnimBlueprint"));
		return false;
	}

	// Compile
	FKismetEditorUtilities::CompileBlueprint(AnimBP);

	// Check for errors
	bool bHasErrors = false;
	if (AnimBP->Status == BS_Error)
	{
		bHasErrors = true;
		UE_LOG(LogMCTAnimBlueprintBuilder, Error, TEXT("CompileAndSave: Compilation failed for '%s'"),
			*AnimBP->GetName());
	}

	// Warnings are logged by the compiler; no explicit collection needed

	// Save regardless of warnings (but not errors)
	if (!bHasErrors)
	{
		UPackage* Package = AnimBP->GetOutermost();
		FString PackageFilename;
		if (FPackageName::DoesPackageExist(Package->GetName(), &PackageFilename))
		{
			// Use existing filename
		}
		else
		{
			PackageFilename = FPackageName::LongPackageNameToFilename(
				Package->GetName(), FPackageName::GetAssetPackageExtension());
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, AnimBP, *PackageFilename, SaveArgs);

		UE_LOG(LogMCTAnimBlueprintBuilder, Log, TEXT("CompileAndSave: %s '%s' (Saved=%d)"),
			bHasErrors ? TEXT("ERRORS") : TEXT("OK"), *AnimBP->GetName(), bSaved);
		return bSaved;
	}

	return false;
}

// =============================================================================
// JSON INFO
// =============================================================================

TSharedPtr<FJsonObject> UMCTAnimBlueprintBuilder::GetAnimBlueprintInfoAsJson(UAnimBlueprint* AnimBP)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!AnimBP)
	{
		return Result;
	}

	Result->SetStringField(TEXT("asset_name"), AnimBP->GetName());
	Result->SetStringField(TEXT("asset_path"), AnimBP->GetPathName());

	// Skeleton
	if (AnimBP->TargetSkeleton)
	{
		Result->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton->GetPathName());
	}

	// Parent class
	if (AnimBP->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), AnimBP->ParentClass->GetPathName());
	}

	// Compilation status
	FString StatusStr;
	switch (AnimBP->Status)
	{
	case BS_UpToDate: StatusStr = TEXT("UpToDate"); break;
	case BS_Dirty: StatusStr = TEXT("Dirty"); break;
	case BS_Error: StatusStr = TEXT("Error"); break;
	case BS_BeingCreated: StatusStr = TEXT("BeingCreated"); break;
	default: StatusStr = TEXT("Unknown"); break;
	}
	Result->SetStringField(TEXT("status"), StatusStr);

	// Graphs
	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	for (UEdGraph* Graph : AnimBP->UbergraphPages)
	{
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("type"), TEXT("EventGraph"));
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("type"), TEXT("Function"));
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}
	Result->SetArrayField(TEXT("graphs"), GraphsArray);

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& Var : AnimBP->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Result->SetArrayField(TEXT("variables"), VarsArray);

	return Result;
}

#undef LOCTEXT_NAMESPACE
