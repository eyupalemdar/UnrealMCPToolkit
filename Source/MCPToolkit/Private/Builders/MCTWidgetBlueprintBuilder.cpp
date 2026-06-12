// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/MCTWidgetBlueprintBuilder.h"

#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/UserWidget.h"

#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "KismetCompilerModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Logging/TokenizedMessage.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "MCTWidgetBlueprintBuilder"

DEFINE_LOG_CATEGORY_STATIC(LogAIWidgetBuilder, Log, All);

namespace
{
bool IsConcreteWidgetClass(UClass* Class)
{
	if (!Class || !Class->IsChildOf(UWidget::StaticClass()))
	{
		return false;
	}

	return !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		&& !Class->GetName().StartsWith(TEXT("SKEL_"))
		&& !Class->GetName().StartsWith(TEXT("REINST_"));
}

void CacheWidgetClassAliases(
	TMap<FString, UClass*>& ClassMap,
	UClass* Class,
	const TArray<FString>& Aliases = TArray<FString>())
{
	if (!IsConcreteWidgetClass(Class))
	{
		return;
	}

	const FString ClassName = Class->GetName();
	ClassMap.Add(ClassName, Class);

	if (ClassName.EndsWith(TEXT("_C")))
	{
		ClassMap.Add(ClassName.LeftChop(2), Class);
	}

	for (const FString& Alias : Aliases)
	{
		if (!Alias.IsEmpty())
		{
			ClassMap.Add(Alias, Class);
		}
	}
}

FString StripObjectPathDecorators(const FString& Input)
{
	FString Result = Input;
	Result.TrimStartAndEndInline();

	int32 FirstQuote = INDEX_NONE;
	int32 LastQuote = INDEX_NONE;
	if (Result.FindChar(TEXT('\''), FirstQuote)
		&& Result.FindLastChar(TEXT('\''), LastQuote)
		&& LastQuote > FirstQuote)
	{
		Result = Result.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		Result.TrimStartAndEndInline();
	}

	if (Result.Len() >= 2
		&& ((Result.StartsWith(TEXT("\"")) && Result.EndsWith(TEXT("\"")))
			|| (Result.StartsWith(TEXT("'")) && Result.EndsWith(TEXT("'")))))
	{
		Result = Result.Mid(1, Result.Len() - 2);
		Result.TrimStartAndEndInline();
	}

	return Result;
}

FProperty* FindInheritedPropertyByName(UWidgetBlueprint* WidgetBP, const FName PropertyName, UClass** OutOwnerClass = nullptr)
{
	if (OutOwnerClass)
	{
		*OutOwnerClass = nullptr;
	}
	if (!WidgetBP || PropertyName.IsNone())
	{
		return nullptr;
	}

	for (UClass* Class = WidgetBP->ParentClass; Class; Class = Class->GetSuperClass())
	{
		if (FProperty* Property = Class->FindPropertyByName(PropertyName))
		{
			if (OutOwnerClass)
			{
				*OutOwnerClass = Class;
			}
			return Property;
		}
	}

	return nullptr;
}

void AppendCompilerMessages(
	const FCompilerResultsLog& Results,
	TArray<FString>* OutWarnings,
	TArray<FString>* OutErrors)
{
	TSet<FString> SeenWarnings;
	TSet<FString> SeenErrors;

	auto AddUnique = [](TArray<FString>* OutMessages, TSet<FString>& Seen, const FString& Message)
	{
		if (OutMessages && !Message.IsEmpty() && !Seen.Contains(Message))
		{
			Seen.Add(Message);
			OutMessages->Add(Message);
		}
	};

	for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
	{
		const FString Text = Message->ToText().ToString();
		const EMessageSeverity::Type Severity = Message->GetSeverity();
		if (Severity <= EMessageSeverity::Error)
		{
			AddUnique(OutErrors, SeenErrors, Text);
		}
		else if (Severity == EMessageSeverity::PerformanceWarning || Severity == EMessageSeverity::Warning)
		{
			AddUnique(OutWarnings, SeenWarnings, Text);
		}
	}

	if (OutErrors && Results.NumErrors > 0 && OutErrors->Num() == 0)
	{
		OutErrors->Add(FString::Printf(TEXT("Blueprint compiler reported %d error(s) without detailed messages"), Results.NumErrors));
	}
	if (OutWarnings && Results.NumWarnings > 0 && OutWarnings->Num() == 0)
	{
		OutWarnings->Add(FString::Printf(TEXT("Blueprint compiler reported %d warning(s) without detailed messages"), Results.NumWarnings));
	}
}

void SetBuilderError(FString* OutError, const FString& Error)
{
	if (OutError)
	{
		*OutError = Error;
	}
}

bool CanSaveWidgetBlueprint(
	UWidgetBlueprint* WidgetBP,
	TArray<FString>* OutErrors)
{
	if (!IsValid(WidgetBP))
	{
		if (OutErrors)
		{
			OutErrors->Add(TEXT("Widget Blueprint is invalid or pending destruction; it may have been deleted by another command before save."));
		}
		return false;
	}

	constexpr EObjectFlags RequiredSaveFlags = RF_Public | RF_Standalone;
	if (!WidgetBP->HasAnyFlags(RequiredSaveFlags))
	{
		if (OutErrors)
		{
			OutErrors->Add(FString::Printf(
				TEXT("Widget Blueprint '%s' is missing RF_Public/RF_Standalone and will not be saved. The asset may have been deleted or mutated concurrently."),
				*WidgetBP->GetPathName()));
		}
		return false;
	}

	UPackage* Package = WidgetBP->GetOutermost();
	if (!Package)
	{
		if (OutErrors)
		{
			OutErrors->Add(TEXT("Widget Blueprint has no outer package; save skipped."));
		}
		return false;
	}

	return true;
}

bool IsWidgetDescendantOf(const UWidget* Candidate, const UWidget* PotentialAncestor)
{
	if (!Candidate || !PotentialAncestor)
	{
		return false;
	}
	if (Candidate == PotentialAncestor)
	{
		return true;
	}

	const UPanelWidget* Panel = Cast<UPanelWidget>(PotentialAncestor);
	if (!Panel)
	{
		return false;
	}

	for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
	{
		if (IsWidgetDescendantOf(Candidate, Panel->GetChildAt(ChildIndex)))
		{
			return true;
		}
	}
	return false;
}

void AddUniqueCandidate(TArray<FString>& Candidates, const FString& Candidate)
{
	if (!Candidate.IsEmpty())
	{
		Candidates.AddUnique(Candidate);
	}
}

FString GetObjectNameFromPath(const FString& Path)
{
	FString Left;
	FString Right;
	if (Path.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		return Right;
	}

	int32 SlashIndex = INDEX_NONE;
	if (Path.FindLastChar(TEXT('/'), SlashIndex))
	{
		return Path.Mid(SlashIndex + 1);
	}

	return Path;
}
}

// =============================================================================
// BLUEPRINT LIFECYCLE
// =============================================================================

UWidgetBlueprint* UMCTWidgetBlueprintBuilder::CreateWidgetBlueprint(
	const FString& PackagePath,
	const FString& AssetName,
	UClass* ParentClass)
{
	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CreateWidgetBlueprint: PackagePath and AssetName are required"));
		return nullptr;
	}

	// Default parent class
	if (!ParentClass)
	{
		ParentClass = UUserWidget::StaticClass();
	}

	// Validate parent class
	if (!ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CreateWidgetBlueprint: ParentClass '%s' must be a UUserWidget subclass"),
			*ParentClass->GetName());
		return nullptr;
	}

	// Build full package path
	FString FullPath = PackagePath / AssetName;

	// Check if already exists
	if (UWidgetBlueprint* Existing = LoadObject<UWidgetBlueprint>(nullptr, *FullPath))
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("CreateWidgetBlueprint: Asset already exists at '%s', returning existing"),
			*FullPath);
		return Existing;
	}

	// Create package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CreateWidgetBlueprint: Failed to create package at '%s'"), *FullPath);
		return nullptr;
	}

	// Create the Widget Blueprint using FKismetEditorUtilities
	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass(),
		NAME_None);

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(NewBP);
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CreateWidgetBlueprint: FKismetEditorUtilities::CreateBlueprint failed"));
		return nullptr;
	}
	WidgetBP->SetFlags(RF_Public | RF_Standalone);

	// Set root widget to CanvasPanel by default if none exists
	if (WidgetBP->WidgetTree && !WidgetBP->WidgetTree->RootWidget)
	{
		UWidget* RootCanvas = WidgetBP->WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), FName(TEXT("RootCanvas")));
		RootCanvas->bIsVariable = true;
		WidgetBP->WidgetTree->RootWidget = RootCanvas;
		WidgetBP->OnVariableAdded(RootCanvas->GetFName());
	}

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(WidgetBP);
	WidgetBP->MarkPackageDirty();

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("CreateWidgetBlueprint: Created '%s' with parent '%s'"),
		*FullPath, *ParentClass->GetName());

	return WidgetBP;
}

bool UMCTWidgetBlueprintBuilder::CompileAndSave(
	UWidgetBlueprint* WidgetBP,
	TArray<FString>* OutWarnings,
	TArray<FString>* OutErrors,
	bool* OutSaved)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CompileAndSave: WidgetBP is null"));
		if (OutSaved)
		{
			*OutSaved = false;
		}
		return false;
	}

	if (OutSaved)
	{
		*OutSaved = false;
	}

	// Pre-compile: Sync GUID map with ALL source widgets AND animations to prevent Ensure failures.
	// UE compiler (ValidateAndFixUpVariableGuids) expects EVERY widget AND animation
	// to have a GUID entry in WidgetVariableNameToGuidMap.
	{
		TSet<FName> CurrentWidgetNames;
		WidgetBP->ForEachSourceWidget([&CurrentWidgetNames](UWidget* Widget)
		{
			CurrentWidgetNames.Add(Widget->GetFName());
		});

		// Also include animations — compiler checks these at line 805
		for (UWidgetAnimation* Animation : WidgetBP->Animations)
		{
			if (Animation)
			{
				CurrentWidgetNames.Add(Animation->GetFName());
			}
		}

		// Remove stale GUID entries (widget no longer exists in WidgetTree outer)
		TArray<FName> StaleNames;
		for (const auto& Pair : WidgetBP->WidgetVariableNameToGuidMap)
		{
			if (!CurrentWidgetNames.Contains(Pair.Key))
			{
				StaleNames.Add(Pair.Key);
			}
		}
		for (const FName& Name : StaleNames)
		{
			UE_LOG(LogAIWidgetBuilder, Log, TEXT("CompileAndSave: Removing stale GUID for '%s'"), *Name.ToString());
			WidgetBP->WidgetVariableNameToGuidMap.Remove(Name);
		}

		// Add missing GUID entries (widget exists in outer but has no GUID)
		bool bAddedAny = false;
		for (const FName& Name : CurrentWidgetNames)
		{
			if (!WidgetBP->WidgetVariableNameToGuidMap.Contains(Name))
			{
				UE_LOG(LogAIWidgetBuilder, Log, TEXT("CompileAndSave: Adding missing GUID for '%s'"), *Name.ToString());
				WidgetBP->WidgetVariableNameToGuidMap.Add(Name, FGuid::NewGuid());
				bAddedAny = true;
			}
		}

		if (StaleNames.Num() > 0 || bAddedAny)
		{
			WidgetBP->Modify();
		}
	}

	// Compile
	FCompilerResultsLog CompileResults;
	FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::None, &CompileResults);
	AppendCompilerMessages(CompileResults, OutWarnings, OutErrors);

	// Check for errors
	bool bHasErrors = WidgetBP->Status == BS_Error || CompileResults.NumErrors > 0;
	if (bHasErrors)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CompileAndSave: Compilation failed for '%s'"),
			*WidgetBP->GetName());
		if (OutErrors && OutErrors->Num() == 0)
		{
			OutErrors->Add(FString::Printf(TEXT("Compilation failed for '%s'"), *WidgetBP->GetName()));
		}
	}

	// Collect warnings
	if (OutWarnings && WidgetBP->Status == BS_UpToDateWithWarnings && OutWarnings->Num() == 0)
	{
		OutWarnings->Add(TEXT("Blueprint compiled with warnings"));
	}

	// Save
	bool bSaved = false;
	if (!bHasErrors && CanSaveWidgetBlueprint(WidgetBP, OutErrors))
	{
		UPackage* Package = WidgetBP->GetOutermost();
		FString PackageFilename;
		if (FPackageName::DoesPackageExist(Package->GetName(), &PackageFilename))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			bSaved = UPackage::SavePackage(Package, WidgetBP, *PackageFilename, SaveArgs);
		}
		else
		{
			// New package - save to the appropriate location
			FString PackagePath = FPackageName::LongPackageNameToFilename(
				Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			bSaved = UPackage::SavePackage(Package, WidgetBP, *PackagePath, SaveArgs);
		}
	}
	if (OutSaved)
	{
		*OutSaved = bSaved;
	}

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("CompileAndSave: '%s' %s (saved=%d, errors=%d, warnings=%d)"),
		*WidgetBP->GetName(),
		bHasErrors ? TEXT("FAILED") : TEXT("succeeded"),
		bSaved ? 1 : 0,
		CompileResults.NumErrors,
		CompileResults.NumWarnings);

	return !bHasErrors;
}

// =============================================================================
// BLUEPRINT REPARENTING
// =============================================================================

bool UMCTWidgetBlueprintBuilder::ReparentBlueprint(
	UWidgetBlueprint* WidgetBP,
	UClass* NewParentClass)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReparentBlueprint: WidgetBP is null"));
		return false;
	}

	if (!NewParentClass)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReparentBlueprint: NewParentClass is null"));
		return false;
	}

	if (!NewParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReparentBlueprint: '%s' is not a UUserWidget subclass"),
			*NewParentClass->GetName());
		return false;
	}

	// Check if already the same parent
	if (WidgetBP->ParentClass == NewParentClass)
	{
		UE_LOG(LogAIWidgetBuilder, Log, TEXT("ReparentBlueprint: '%s' already has parent '%s'"),
			*WidgetBP->GetName(), *NewParentClass->GetName());
		return true;
	}

	FScopedTransaction Transaction(LOCTEXT("AIReparentBlueprint", "AI: Reparent Blueprint"));

	FString OldParentName = WidgetBP->ParentClass ? WidgetBP->ParentClass->GetName() : TEXT("None");

	// Perform reparenting (mirrors UBlueprintEditorLibrary::ReparentBlueprint logic)
	WidgetBP->ParentClass = NewParentClass;

	FBlueprintEditorUtils::RefreshAllNodes(WidgetBP);
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);

	// Compile with delta serialization for safe reinstancing
	EBlueprintCompileOptions CompileOptions =
		EBlueprintCompileOptions::SkipSave
		| EBlueprintCompileOptions::UseDeltaSerializationDuringReinstancing
		| EBlueprintCompileOptions::SkipNewVariableDefaultsDetection;

	FKismetEditorUtilities::CompileBlueprint(WidgetBP, CompileOptions);

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("ReparentBlueprint: '%s' reparented from '%s' to '%s'"),
		*WidgetBP->GetName(), *OldParentName, *NewParentClass->GetName());

	return true;
}

// =============================================================================
// WIDGET TREE MANIPULATION
// =============================================================================

UWidget* UMCTWidgetBlueprintBuilder::AddWidget(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetClassName,
	const FString& WidgetName,
	const FString& ParentWidgetName,
	FString* OutError)
{
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Invalid WidgetBP or WidgetTree"));
		SetBuilderError(OutError, TEXT("Invalid Widget Blueprint or WidgetTree"));
		return nullptr;
	}

	if (FindWidgetByName(WidgetBP, WidgetName))
	{
		const FString Error = FString::Printf(TEXT("Widget '%s' already exists in this WidgetTree"), *WidgetName);
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		return nullptr;
	}

	UClass* OwnerClass = nullptr;
	if (FProperty* InheritedProperty = FindInheritedPropertyByName(WidgetBP, FName(*WidgetName), &OwnerClass))
	{
		const FString Error = FString::Printf(
			TEXT("Widget name '%s' shadows inherited property '%s' on parent class '%s'. Use a unique local widget name or composition instead of duplicating inherited designer variables."),
			*WidgetName,
			*InheritedProperty->GetName(),
			OwnerClass ? *OwnerClass->GetName() : TEXT("unknown"));
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		return nullptr;
	}

	// Resolve widget class
	UClass* WidgetClass = ResolveWidgetClass(WidgetClassName);
	if (!WidgetClass)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Could not resolve class '%s'"), *WidgetClassName);
		SetBuilderError(OutError, FString::Printf(TEXT("Could not resolve widget class '%s'"), *WidgetClassName));
		return nullptr;
	}

	UPanelWidget* ResolvedPanelParent = nullptr;
	if (!ParentWidgetName.IsEmpty())
	{
		UWidget* ParentWidget = FindWidgetByName(WidgetBP, ParentWidgetName);
		if (!ParentWidget)
		{
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Parent '%s' not found"), *ParentWidgetName);
			SetBuilderError(OutError, FString::Printf(TEXT("Parent widget '%s' not found"), *ParentWidgetName));
			return nullptr;
		}

		ResolvedPanelParent = Cast<UPanelWidget>(ParentWidget);
		if (!ResolvedPanelParent)
		{
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Parent '%s' is not a panel widget (class: %s)"),
				*ParentWidgetName, *ParentWidget->GetClass()->GetName());
			SetBuilderError(OutError, FString::Printf(TEXT("Parent '%s' is not a panel widget (class: %s)"),
				*ParentWidgetName, *ParentWidget->GetClass()->GetName()));
			return nullptr;
		}
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddWidget", "AI: Add Widget"));

	// Construct the widget
	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(
		TSubclassOf<UWidget>(WidgetClass),
		FName(*WidgetName));

	if (!NewWidget)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: ConstructWidget failed for class '%s'"), *WidgetClassName);
		SetBuilderError(OutError, FString::Printf(TEXT("ConstructWidget failed for class '%s'"), *WidgetClassName));
		return nullptr;
	}

	// Mark as variable so it's accessible in Blueprint graphs
	NewWidget->bIsVariable = true;

	// Register GUID for this variable to prevent Ensure failures in ValidateAndFixUpVariableGuids
	WidgetBP->OnVariableAdded(NewWidget->GetFName());

	// Add to parent or set as root
	if (ParentWidgetName.IsEmpty())
	{
		// Clean up existing root if being replaced
		UWidget* OldRoot = WidgetBP->WidgetTree->RootWidget;
		if (OldRoot && OldRoot != NewWidget)
		{
			// Recursively clean descendants of old root
			if (UPanelWidget* OldPanel = Cast<UPanelWidget>(OldRoot))
			{
				TArray<UWidget*> OldDescendants;
				CollectAllDescendants(OldPanel, OldDescendants);
				for (UWidget* Desc : OldDescendants)
				{
					WidgetBP->OnVariableRemoved(Desc->GetFName());
					Desc->Rename(nullptr, GetTransientPackage());
				}
			}
			WidgetBP->OnVariableRemoved(OldRoot->GetFName());
			OldRoot->Rename(nullptr, GetTransientPackage());
			UE_LOG(LogAIWidgetBuilder, Log, TEXT("AddWidget: Cleaned up old root '%s'"), *OldRoot->GetName());
		}

		// Set as root widget
		WidgetBP->WidgetTree->RootWidget = NewWidget;
		UE_LOG(LogAIWidgetBuilder, Log, TEXT("AddWidget: Set '%s' (%s) as root"),
			*NewWidget->GetName(), *WidgetClassName);
	}
	else
	{
		UPanelSlot* Slot = ResolvedPanelParent->AddChild(NewWidget);
		if (!Slot)
		{
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: AddChild failed for parent '%s'"), *ParentWidgetName);
			SetBuilderError(OutError, FString::Printf(TEXT("AddChild failed for parent '%s'"), *ParentWidgetName));
			WidgetBP->OnVariableRemoved(NewWidget->GetFName());
			NewWidget->Rename(nullptr, GetTransientPackage());
			return nullptr;
		}

		UE_LOG(LogAIWidgetBuilder, Log, TEXT("AddWidget: Added '%s' (%s) to parent '%s'"),
			*NewWidget->GetName(), *WidgetClassName, *ParentWidgetName);
	}

	MarkModified(WidgetBP);
	return NewWidget;
}

bool UMCTWidgetBlueprintBuilder::RemoveWidget(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetName)
{
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return false;
	}

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("RemoveWidget: Widget '%s' not found"), *WidgetName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIRemoveWidget", "AI: Remove Widget"));

	// Collect ALL descendant widgets before modifying the tree.
	// UWidgetTree::RemoveWidget only detaches from parent — child UObjects remain
	// in the WidgetTree outer and cause GUID Ensure failures if not cleaned up.
	TArray<UWidget*> AllDescendants;
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		CollectAllDescendants(Panel, AllDescendants);
	}

	// Clean GUIDs and rename descendants to transient FIRST (before tree modification)
	for (UWidget* Descendant : AllDescendants)
	{
		WidgetBP->OnVariableRemoved(Descendant->GetFName());
		Descendant->Rename(nullptr, GetTransientPackage());
	}

	// Remove from parent
	if (Widget == WidgetBP->WidgetTree->RootWidget)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
	}
	else if (Widget->Slot)
	{
		UPanelWidget* Parent = Widget->Slot->Parent;
		if (Parent)
		{
			Parent->RemoveChild(Widget);
		}
	}

	// Clean up THIS widget's GUID
	WidgetBP->OnVariableRemoved(Widget->GetFName());

	// Remove from widget tree internal tracking
	WidgetBP->WidgetTree->RemoveWidget(Widget);

	// Rename to transient to ensure it's no longer in WidgetTree outer
	Widget->Rename(nullptr, GetTransientPackage());

	MarkModified(WidgetBP);

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("RemoveWidget: Removed '%s'"), *WidgetName);
	return true;
}

bool UMCTWidgetBlueprintBuilder::MoveWidget(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetName,
	const FString& NewParentName,
	int32 NewIndex)
{
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return false;
	}

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("MoveWidget: Widget '%s' not found"), *WidgetName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIMoveWidget", "AI: Move Widget"));

	UPanelWidget* CurrentParent = Widget->GetParent();
	const int32 CurrentIndex = CurrentParent ? CurrentParent->GetChildIndex(Widget) : INDEX_NONE;
	const bool bIsRoot = Widget == WidgetBP->WidgetTree->RootWidget;

	if (NewParentName.IsEmpty())
	{
		if (!bIsRoot && CurrentParent)
		{
			CurrentParent->RemoveChild(Widget);
		}

		WidgetBP->WidgetTree->RootWidget = Widget;
		MarkModified(WidgetBP);
		return true;
	}

	UWidget* NewParent = FindWidgetByName(WidgetBP, NewParentName);
	UPanelWidget* PanelParent = Cast<UPanelWidget>(NewParent);
	if (!PanelParent)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("MoveWidget: New parent '%s' is not a panel"),
			*NewParentName);
		return false;
	}

	if (IsWidgetDescendantOf(PanelParent, Widget))
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("MoveWidget: Cannot move widget '%s' under itself or descendant '%s'"),
			*WidgetName, *NewParentName);
		return false;
	}

	if (CurrentParent == PanelParent)
	{
		const int32 TargetIndex = NewIndex >= 0 ? NewIndex : PanelParent->GetChildrenCount() - 1;
		PanelParent->ShiftChild(TargetIndex, Widget);
		MarkModified(WidgetBP);
		return true;
	}

	UPanelSlot* SlotTemplate = Widget->Slot;

	if (bIsRoot)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
	}
	else if (CurrentParent)
	{
		CurrentParent->RemoveChild(Widget);
	}

	UPanelSlot* NewSlot = NewIndex >= 0
		? PanelParent->InsertChildAt(NewIndex, Widget, SlotTemplate)
		: PanelParent->AddChild(Widget, SlotTemplate);

	if (!NewSlot)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("MoveWidget: Failed to add widget '%s' to parent '%s'"),
			*WidgetName, *NewParentName);
		if (bIsRoot)
		{
			WidgetBP->WidgetTree->RootWidget = Widget;
		}
		else if (CurrentParent)
		{
			const int32 RestoreIndex = CurrentIndex != INDEX_NONE ? CurrentIndex : CurrentParent->GetChildrenCount();
			CurrentParent->InsertChildAt(RestoreIndex, Widget, SlotTemplate);
		}
		return false;
	}

	MarkModified(WidgetBP);
	return true;
}

UWidget* UMCTWidgetBlueprintBuilder::ReplaceWidget(
	UWidgetBlueprint* WidgetBP,
	const FString& TargetWidgetName,
	const FString& NewWidgetClassName,
	const FString& NewWidgetName,
	bool bPreserveSlot,
	FString* OutError)
{
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		SetBuilderError(OutError, TEXT("Invalid Widget Blueprint or WidgetTree"));
		return nullptr;
	}

	if (TargetWidgetName.IsEmpty())
	{
		SetBuilderError(OutError, TEXT("Target widget name is required"));
		return nullptr;
	}

	UWidget* OldWidget = FindWidgetByName(WidgetBP, TargetWidgetName);
	if (!OldWidget)
	{
		const FString Error = FString::Printf(TEXT("Widget '%s' not found"), *TargetWidgetName);
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		return nullptr;
	}

	FString FinalWidgetName = NewWidgetName.IsEmpty() ? TargetWidgetName : NewWidgetName;
	FinalWidgetName.TrimStartAndEndInline();
	if (FinalWidgetName.IsEmpty())
	{
		SetBuilderError(OutError, TEXT("Replacement widget name cannot be empty"));
		return nullptr;
	}

	const bool bKeepName = FinalWidgetName.Equals(TargetWidgetName, ESearchCase::CaseSensitive);
	if (!bKeepName && FindWidgetByName(WidgetBP, FinalWidgetName))
	{
		const FString Error = FString::Printf(TEXT("Widget '%s' already exists in this WidgetTree"), *FinalWidgetName);
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		return nullptr;
	}

	UClass* WidgetClass = ResolveWidgetClass(NewWidgetClassName);
	if (!WidgetClass)
	{
		const FString Error = FString::Printf(TEXT("Could not resolve widget class '%s'"), *NewWidgetClassName);
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		return nullptr;
	}

	if (!bKeepName)
	{
		UClass* OwnerClass = nullptr;
		if (FProperty* InheritedProperty = FindInheritedPropertyByName(WidgetBP, FName(*FinalWidgetName), &OwnerClass))
		{
			const FString Error = FString::Printf(
				TEXT("Widget name '%s' shadows inherited property '%s' on parent class '%s'. Use a unique local widget name or composition instead of duplicating inherited designer variables."),
				*FinalWidgetName,
				*InheritedProperty->GetName(),
				OwnerClass ? *OwnerClass->GetName() : TEXT("unknown"));
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
			SetBuilderError(OutError, Error);
			return nullptr;
		}
	}

	UPanelWidget* Parent = OldWidget->GetParent();
	const int32 OldIndex = Parent ? Parent->GetChildIndex(OldWidget) : INDEX_NONE;
	const bool bWasRoot = OldWidget == WidgetBP->WidgetTree->RootWidget;
	UPanelSlot* SlotTemplate = bPreserveSlot ? OldWidget->Slot : nullptr;

	if (!bWasRoot && !Parent)
	{
		const FString Error = FString::Printf(TEXT("Widget '%s' has no parent and is not the root widget"), *TargetWidgetName);
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		return nullptr;
	}

	const FName TempWidgetName = MakeUniqueObjectName(
		WidgetBP->WidgetTree,
		WidgetClass,
		FName(*(FinalWidgetName + TEXT("_ReplacementTmp"))));

	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(
		TSubclassOf<UWidget>(WidgetClass),
		TempWidgetName);
	if (!NewWidget)
	{
		const FString Error = FString::Printf(TEXT("ConstructWidget failed for class '%s'"), *NewWidgetClassName);
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AIReplaceWidget", "AI: Replace Widget"));

	TArray<UWidget*> OldDescendants;
	if (UPanelWidget* OldPanel = Cast<UPanelWidget>(OldWidget))
	{
		CollectAllDescendants(OldPanel, OldDescendants);
	}

	for (UWidget* Descendant : OldDescendants)
	{
		WidgetBP->OnVariableRemoved(Descendant->GetFName());
		Descendant->Rename(nullptr, GetTransientPackage());
	}

	if (bWasRoot)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
	}
	else if (Parent)
	{
		Parent->RemoveChild(OldWidget);
	}

	WidgetBP->OnVariableRemoved(OldWidget->GetFName());
	WidgetBP->WidgetTree->RemoveWidget(OldWidget);
	OldWidget->Rename(nullptr, GetTransientPackage());

	if (!NewWidget->Rename(*FinalWidgetName, WidgetBP->WidgetTree))
	{
		const FString Error = FString::Printf(TEXT("Failed to rename replacement widget to '%s'"), *FinalWidgetName);
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
		SetBuilderError(OutError, Error);
		NewWidget->Rename(nullptr, GetTransientPackage());
		return nullptr;
	}

	NewWidget->bIsVariable = true;

	UPanelSlot* NewSlot = nullptr;
	if (bWasRoot)
	{
		WidgetBP->WidgetTree->RootWidget = NewWidget;
	}
	else if (Parent)
	{
		NewSlot = OldIndex >= 0
			? Parent->InsertChildAt(OldIndex, NewWidget, SlotTemplate)
			: Parent->AddChild(NewWidget, SlotTemplate);
		if (!NewSlot)
		{
			const FString Error = FString::Printf(TEXT("Failed to add replacement widget '%s' to parent '%s'"),
				*FinalWidgetName, *Parent->GetName());
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("ReplaceWidget: %s"), *Error);
			SetBuilderError(OutError, Error);
			WidgetBP->WidgetTree->RemoveWidget(NewWidget);
			NewWidget->Rename(nullptr, GetTransientPackage());
			return nullptr;
		}
	}

	WidgetBP->OnVariableAdded(NewWidget->GetFName());
	MarkModified(WidgetBP);

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("ReplaceWidget: Replaced '%s' with '%s' (%s)"),
		*TargetWidgetName, *FinalWidgetName, *NewWidgetClassName);
	return NewWidget;
}

// =============================================================================
// PROPERTY SETTING
// =============================================================================

bool UMCTWidgetBlueprintBuilder::SetWidgetProperty(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetName,
	const FString& PropertyName,
	const FString& Value)
{
	if (!WidgetBP)
	{
		return false;
	}

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetWidgetProperty: Widget '%s' not found"), *WidgetName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetWidgetProperty", "AI: Set Widget Property"));

	bool bSuccess = SetPropertyByPath(Widget, PropertyName, Value);
	if (bSuccess)
	{
		// Slate bridge: reflection-based ImportText updates the UObject property,
		// but SBorder/SImage/etc. only receive the change via PostEditChangeProperty
		// (triggers each widget's SynchronizeProperties override). Required for
		// brush/color changes to render in capture_widget_preview.
		FString RootPropertyName = PropertyName;
		int32 DotIdx;
		if (PropertyName.FindChar(TEXT('.'), DotIdx))
		{
			RootPropertyName = PropertyName.Left(DotIdx);
		}
		if (FProperty* RootProp = Widget->GetClass()->FindPropertyByName(*RootPropertyName))
		{
			FPropertyChangedEvent PropertyEvent(RootProp, EPropertyChangeType::ValueSet);
			Widget->PostEditChangeProperty(PropertyEvent);
		}
		// Safety net when PostEditChangeProperty is suppressed by a property-specific guard.
		Widget->SynchronizeProperties();

		MarkModified(WidgetBP);
	}
	else
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetWidgetProperty: Failed to set '%s'='%s' on '%s'"),
			*PropertyName, *Value, *WidgetName);
	}

	return bSuccess;
}

bool UMCTWidgetBlueprintBuilder::SetSlotProperty(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetName,
	const FString& PropertyName,
	const FString& Value)
{
	if (!WidgetBP)
	{
		return false;
	}

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetSlotProperty: Widget '%s' not found"), *WidgetName);
		return false;
	}

	if (!Widget->Slot)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetSlotProperty: Widget '%s' has no slot (is it the root?)"),
			*WidgetName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetSlotProperty", "AI: Set Slot Property"));

	bool bSuccess = SetPropertyByPath(Widget->Slot, PropertyName, Value);
	if (bSuccess)
	{
		MarkModified(WidgetBP);
	}
	else
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetSlotProperty: Failed to set '%s'='%s' on slot of '%s'"),
			*PropertyName, *Value, *WidgetName);
	}

	return bSuccess;
}

int32 UMCTWidgetBlueprintBuilder::SetWidgetProperties(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetName,
	const TMap<FString, FString>& Properties,
	TArray<FString>* OutFailed)
{
	if (!WidgetBP)
	{
		return 0;
	}

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetWidgetProperties: Widget '%s' not found"), *WidgetName);
		return 0;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetWidgetProperties", "AI: Set Widget Properties"));

	int32 SuccessCount = 0;
	for (const auto& Pair : Properties)
	{
		if (SetPropertyByPath(Widget, Pair.Key, Pair.Value))
		{
			++SuccessCount;
		}
		else if (OutFailed)
		{
			OutFailed->Add(Pair.Key);
		}
	}

	if (SuccessCount > 0)
	{
		// Single Slate sync pass for bulk set — cheaper than per-property
		// PostEditChangeProperty. Critical for UBorder.Background and similar
		// brush/color properties to reach the Slate layer.
		Widget->SynchronizeProperties();

		MarkModified(WidgetBP);
	}

	return SuccessCount;
}

bool UMCTWidgetBlueprintBuilder::SetCanvasSlotLayout(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetName,
	float PositionX, float PositionY,
	float SizeX, float SizeY,
	float AnchorMinX, float AnchorMinY,
	float AnchorMaxX, float AnchorMaxY,
	float AlignmentX, float AlignmentY)
{
	if (!WidgetBP)
	{
		return false;
	}

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget || !Widget->Slot)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetCanvasSlotLayout: Widget '%s' not found or has no slot"),
			*WidgetName);
		return false;
	}

	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
	if (!CanvasSlot)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetCanvasSlotLayout: Widget '%s' is not in a CanvasPanel (slot type: %s)"),
			*WidgetName, *Widget->Slot->GetClass()->GetName());
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetCanvasSlotLayout", "AI: Set Canvas Slot Layout"));

	// Set anchors
	FAnchors Anchors;
	Anchors.Minimum = FVector2D(AnchorMinX, AnchorMinY);
	Anchors.Maximum = FVector2D(AnchorMaxX, AnchorMaxY);
	CanvasSlot->SetAnchors(Anchors);

	// Set offsets (position + size)
	CanvasSlot->SetOffsets(FMargin(PositionX, PositionY, SizeX, SizeY));

	// Set alignment
	CanvasSlot->SetAlignment(FVector2D(AlignmentX, AlignmentY));

	MarkModified(WidgetBP);
	return true;
}

// =============================================================================
// UTILITY
// =============================================================================

UClass* UMCTWidgetBlueprintBuilder::ResolveWidgetClass(const FString& ClassNameOrPath)
{
	const FString CleanPath = StripObjectPathDecorators(ClassNameOrPath);
	TMap<FString, UClass*>& Map = GetWidgetClassMap();

	// Try full generated-class or Widget Blueprint asset paths first. This is
	// required for freshly created WBP classes that are not present in the class
	// iterator cache yet, e.g. /Game/UI/W_Foo.W_Foo_C or /Game/UI/W_Foo.
	if (CleanPath.StartsWith(TEXT("/")))
	{
		TArray<FString> ClassCandidates;
		TArray<FString> BlueprintCandidates;

		AddUniqueCandidate(ClassCandidates, CleanPath);

		FString PackagePath;
		FString ObjectName;
		const bool bHasObjectName = CleanPath.Split(
			TEXT("."),
			&PackagePath,
			&ObjectName,
			ESearchCase::CaseSensitive,
			ESearchDir::FromEnd);

		if (bHasObjectName)
		{
			if (ObjectName.EndsWith(TEXT("_C")))
			{
				const FString BlueprintObjectName = ObjectName.LeftChop(2);
				AddUniqueCandidate(BlueprintCandidates, PackagePath);
				AddUniqueCandidate(BlueprintCandidates, PackagePath + TEXT(".") + BlueprintObjectName);
			}
			else
			{
				AddUniqueCandidate(BlueprintCandidates, CleanPath);
				AddUniqueCandidate(ClassCandidates, PackagePath + TEXT(".") + ObjectName + TEXT("_C"));
			}
		}
		else if (CleanPath.EndsWith(TEXT("_C")))
		{
			const FString BlueprintPath = CleanPath.LeftChop(2);
			const FString BlueprintName = GetObjectNameFromPath(BlueprintPath);
			AddUniqueCandidate(ClassCandidates, BlueprintPath + TEXT(".") + BlueprintName + TEXT("_C"));
			AddUniqueCandidate(BlueprintCandidates, BlueprintPath);
		}
		else
		{
			const FString BlueprintName = GetObjectNameFromPath(CleanPath);
			AddUniqueCandidate(ClassCandidates, CleanPath + TEXT(".") + BlueprintName + TEXT("_C"));
			AddUniqueCandidate(BlueprintCandidates, CleanPath);
			AddUniqueCandidate(BlueprintCandidates, CleanPath + TEXT(".") + BlueprintName);
		}

		for (const FString& Candidate : ClassCandidates)
		{
			UClass* Cls = LoadObject<UClass>(nullptr, *Candidate);
			if (IsConcreteWidgetClass(Cls))
			{
				CacheWidgetClassAliases(Map, Cls, { ClassNameOrPath, CleanPath, Candidate });
				return Cls;
			}
		}

		for (const FString& Candidate : BlueprintCandidates)
		{
			UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *Candidate);
			if (!WidgetBP)
			{
				continue;
			}

			UClass* GenClass = WidgetBP->GeneratedClass;
			if (!GenClass)
			{
				FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::SkipSave);
				GenClass = WidgetBP->GeneratedClass;
			}

			if (IsConcreteWidgetClass(GenClass))
			{
				CacheWidgetClassAliases(Map, GenClass, { ClassNameOrPath, CleanPath, Candidate, WidgetBP->GetName() });
				return GenClass;
			}
		}
	}

	// Try the cached map
	if (UClass** Found = Map.Find(ClassNameOrPath))
	{
		return *Found;
	}
	if (UClass** Found = Map.Find(CleanPath))
	{
		return *Found;
	}

	// Try case-insensitive search
	for (const auto& Pair : Map)
	{
		if (Pair.Key.Equals(ClassNameOrPath, ESearchCase::IgnoreCase)
			|| Pair.Key.Equals(CleanPath, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	UE_LOG(LogAIWidgetBuilder, Warning, TEXT("ResolveWidgetClass: Could not resolve '%s'"), *ClassNameOrPath);
	return nullptr;
}

UWidget* UMCTWidgetBlueprintBuilder::FindWidgetByName(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetName)
{
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return nullptr;
	}

	// Check root widget directly
	if (WidgetBP->WidgetTree->RootWidget &&
		WidgetBP->WidgetTree->RootWidget->GetName() == WidgetName)
	{
		return WidgetBP->WidgetTree->RootWidget;
	}

	// Use WidgetTree's FindWidget
	return WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
}

UWidgetBlueprint* UMCTWidgetBlueprintBuilder::LoadWidgetBlueprint(const FString& AssetPath)
{
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		// Try with _C suffix stripped
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("LoadWidgetBlueprint: Asset not found at '%s'"), *AssetPath);
		return nullptr;
	}

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Asset);
	if (!WBP)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("LoadWidgetBlueprint: '%s' is not a Widget Blueprint (class: %s)"),
			*AssetPath, *Asset->GetClass()->GetName());
		return nullptr;
	}

	return WBP;
}

TSharedPtr<FJsonObject> UMCTWidgetBlueprintBuilder::GetWidgetTreeAsJson(UWidgetBlueprint* WidgetBP)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		Result->SetStringField(TEXT("error"), TEXT("Invalid Widget Blueprint"));
		return Result;
	}

	Result->SetStringField(TEXT("name"), WidgetBP->GetName());
	Result->SetStringField(TEXT("parent_class"),
		WidgetBP->ParentClass ? WidgetBP->ParentClass->GetName() : TEXT("None"));

	if (WidgetBP->WidgetTree->RootWidget)
	{
		Result->SetObjectField(TEXT("root"), WidgetToJson(WidgetBP->WidgetTree->RootWidget));
	}

	return Result;
}

TArray<TPair<FString, bool>> UMCTWidgetBlueprintBuilder::GetAvailableWidgetClasses()
{
	TArray<TPair<FString, bool>> Result;
	const TMap<FString, UClass*>& Map = GetWidgetClassMap();

	TSet<UClass*> Seen;
	for (const auto& Pair : Map)
	{
		if (!Seen.Contains(Pair.Value))
		{
			Seen.Add(Pair.Value);
			bool bIsPanel = Pair.Value->IsChildOf(UPanelWidget::StaticClass());
			Result.Emplace(Pair.Value->GetName(), bIsPanel);
		}
	}

	Result.Sort([](const TPair<FString, bool>& A, const TPair<FString, bool>& B)
	{
		return A.Key < B.Key;
	});

	return Result;
}

// =============================================================================
// PRIVATE IMPLEMENTATION
// =============================================================================

TMap<FString, UClass*>& UMCTWidgetBlueprintBuilder::GetWidgetClassMap()
{
	static TMap<FString, UClass*> ClassMap;

	// Refresh from currently loaded classes on every call. New Widget Blueprint
	// generated classes can appear after the first MCP request.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (IsConcreteWidgetClass(Class))
		{
			CacheWidgetClassAliases(ClassMap, Class);
		}
	}

	// Common aliases
	auto AddAlias = [&](const FString& Alias, const FString& ClassName)
	{
		if (UClass** Found = ClassMap.Find(ClassName))
		{
			ClassMap.Add(Alias, *Found);
		}
	};

	AddAlias(TEXT("Text"), TEXT("TextBlock"));
	AddAlias(TEXT("HBox"), TEXT("HorizontalBox"));
	AddAlias(TEXT("VBox"), TEXT("VerticalBox"));
	AddAlias(TEXT("Canvas"), TEXT("CanvasPanel"));
	AddAlias(TEXT("HBoxSlot"), TEXT("HorizontalBoxSlot"));
	AddAlias(TEXT("VBoxSlot"), TEXT("VerticalBoxSlot"));

	return ClassMap;
}

bool UMCTWidgetBlueprintBuilder::SetPropertyByPath(
	UObject* Object,
	const FString& PropertyPath,
	const FString& Value)
{
	if (!Object || PropertyPath.IsEmpty())
	{
		return false;
	}

	// Split dot-notation path
	TArray<FString> PathParts;
	PropertyPath.ParseIntoArray(PathParts, TEXT("."));

	UStruct* CurrentStruct = Object->GetClass();
	void* CurrentContainer = Object;

	for (int32 i = 0; i < PathParts.Num(); ++i)
	{
		FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
		if (!Prop)
		{
			UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetPropertyByPath: Property '%s' not found on %s"),
				*PathParts[i], *CurrentStruct->GetName());
			return false;
		}

		if (i == PathParts.Num() - 1)
		{
			// Leaf property — set the value using ImportText
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentContainer);
			const TCHAR* ValueCStr = *Value;
			const TCHAR* Result = Prop->ImportText_Direct(ValueCStr, ValuePtr, Object, PPF_None);
			if (Result != nullptr)
			{
				return true;
			}

			// Fallback: handle enum properties with alternative name formats
			// MCP sends "EHorizontalAlignment::Center" but UE expects "HAlign_Center"
			FByteProperty* ByteProp = CastField<FByteProperty>(Prop);
			FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop);
			UEnum* Enum = nullptr;
			if (ByteProp && ByteProp->Enum)
			{
				Enum = ByteProp->Enum;
			}
			else if (EnumProp)
			{
				Enum = EnumProp->GetEnum();
			}

			if (Enum)
			{
				FString CleanValue = Value;

				// Strip enum class prefix: "EHorizontalAlignment::Center" -> "Center"
				int32 ColonIdx;
				if (CleanValue.FindLastChar(TEXT(':'), ColonIdx))
				{
					CleanValue = CleanValue.Mid(ColonIdx + 1);
				}

				// Try all enum values, match by short name (case-insensitive)
				for (int32 EnumIdx = 0; EnumIdx < Enum->NumEnums() - 1; ++EnumIdx)
				{
					FString EnumName = Enum->GetNameStringByIndex(EnumIdx);
					FString DisplayName = Enum->GetDisplayNameTextByIndex(EnumIdx).ToString();

					// Match against: full name "HAlign_Center", display name "Center", or short "Center"
					if (EnumName.Equals(CleanValue, ESearchCase::IgnoreCase) ||
						DisplayName.Equals(CleanValue, ESearchCase::IgnoreCase))
					{
						int64 EnumValue = Enum->GetValueByIndex(EnumIdx);
						if (ByteProp)
						{
							ByteProp->SetPropertyValue_InContainer(CurrentContainer, (uint8)EnumValue);
						}
						else if (EnumProp)
						{
							// Use the underlying numeric property
							FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
							if (UnderlyingProp)
							{
								UnderlyingProp->SetIntPropertyValue(
									Prop->ContainerPtrToValuePtr<void>(CurrentContainer), EnumValue);
							}
						}
						UE_LOG(LogAIWidgetBuilder, Verbose,
							TEXT("SetPropertyByPath: Enum fallback matched '%s' -> '%s' = %lld"),
							*Value, *EnumName, EnumValue);
						return true;
					}
				}

				UE_LOG(LogAIWidgetBuilder, Warning,
					TEXT("SetPropertyByPath: Enum '%s' has no value matching '%s'"),
					*Enum->GetName(), *Value);
			}

			return false;
		}
		else
		{
			// Navigate into struct property
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp)
			{
				UE_LOG(LogAIWidgetBuilder, Warning,
					TEXT("SetPropertyByPath: '%s' is not a struct, cannot navigate further"),
					*PathParts[i]);
				return false;
			}

			CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = StructProp->Struct;
		}
	}

	return false;
}

// =============================================================================
// CDO (CLASS DEFAULT OBJECT) PROPERTIES
// =============================================================================

bool UMCTWidgetBlueprintBuilder::SetCDOProperty(
	UWidgetBlueprint* WidgetBP,
	const FString& PropertyName,
	const FString& Value)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetCDOProperty: WidgetBP is null"));
		return false;
	}

	// Ensure GeneratedClass exists
	UClass* GenClass = WidgetBP->GeneratedClass;
	if (!GenClass)
	{
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
		GenClass = WidgetBP->GeneratedClass;
	}
	if (!GenClass)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetCDOProperty: No GeneratedClass for %s"), *WidgetBP->GetName());
		return false;
	}

	UObject* CDO = GenClass->GetDefaultObject();
	if (!CDO)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("SetCDOProperty: No CDO for %s"), *GenClass->GetName());
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetCDOProperty", "AI: Set CDO Property"));

	bool bResult = SetPropertyByPath(CDO, PropertyName, Value);
	if (bResult)
	{
		CDO->MarkPackageDirty();
		WidgetBP->MarkPackageDirty();
		UE_LOG(LogAIWidgetBuilder, Log, TEXT("SetCDOProperty: Set '%s' = '%s' on %s"),
			*PropertyName, *Value, *WidgetBP->GetName());
	}
	else
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetCDOProperty: Failed to set '%s' on %s"),
			*PropertyName, *WidgetBP->GetName());
	}
	return bResult;
}

TSharedPtr<FJsonObject> UMCTWidgetBlueprintBuilder::GetCDOPropertiesAsJson(UWidgetBlueprint* WidgetBP)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!WidgetBP || !WidgetBP->GeneratedClass)
	{
		return Result;
	}

	UObject* CDO = WidgetBP->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		return Result;
	}

	// Get the parent CDO for comparison (to show only "own" values)
	UObject* ParentCDO = nullptr;
	UClass* ParentClass = WidgetBP->GeneratedClass->GetSuperClass();
	if (ParentClass)
	{
		ParentCDO = ParentClass->GetDefaultObject();
	}

	for (TFieldIterator<FProperty> PropIt(WidgetBP->GeneratedClass); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly))
		{
			continue;
		}

		FString ValueStr;
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
		Prop->ExportText_Direct(ValueStr, ValuePtr, nullptr, CDO, PPF_None);

		Result->SetStringField(Prop->GetName(), ValueStr);
	}

	return Result;
}

// =============================================================================
// ARRAY PROPERTY SUPPORT
// =============================================================================

int32 UMCTWidgetBlueprintBuilder::AddArrayElement(
	UObject* Object,
	const FString& ArrayPropertyName,
	const TMap<FString, FString>& ElementValues,
	const FString& ClassName)
{
	if (!Object)
	{
		return -1;
	}

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Object->GetClass()->FindPropertyByName(FName(*ArrayPropertyName)));
	if (!ArrayProp)
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("AddArrayElement: Array property '%s' not found on %s"),
			*ArrayPropertyName, *Object->GetClass()->GetName());
		return -1;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddArrayElement", "AI: Add Array Element"));

	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Object));
	int32 NewIndex = ArrayHelper.AddValue();

	// If inner is a struct, set sub-properties
	FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
	if (InnerStructProp && ElementValues.Num() > 0)
	{
		void* ElemPtr = ArrayHelper.GetRawPtr(NewIndex);
		UScriptStruct* Struct = InnerStructProp->Struct;

		for (const auto& Pair : ElementValues)
		{
			// Support dot-notation for nested struct properties
			TArray<FString> SubParts;
			Pair.Key.ParseIntoArray(SubParts, TEXT("."));

			UStruct* CurrentStruct = Struct;
			void* CurrentContainer = ElemPtr;

			for (int32 i = 0; i < SubParts.Num(); ++i)
			{
				FProperty* SubProp = CurrentStruct->FindPropertyByName(FName(*SubParts[i]));
				if (!SubProp)
				{
					UE_LOG(LogAIWidgetBuilder, Warning, TEXT("AddArrayElement: Sub-property '%s' not found in %s"),
						*SubParts[i], *CurrentStruct->GetName());
					break;
				}

				if (i == SubParts.Num() - 1)
				{
					// Leaf — set value
					void* SubPtr = SubProp->ContainerPtrToValuePtr<void>(CurrentContainer);
					SubProp->ImportText_Direct(*Pair.Value, SubPtr, Object, PPF_None);
				}
				else
				{
					// Navigate into sub-struct
					FStructProperty* SubStructProp = CastField<FStructProperty>(SubProp);
					if (SubStructProp)
					{
						CurrentContainer = SubStructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
						CurrentStruct = SubStructProp->Struct;
					}
					else
					{
						break;
					}
				}
			}
		}
	}
	else if (FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner))
	{
		// Instanced UObject array (e.g. TArray<TObjectPtr<UGameFeatureAction>> with UPROPERTY(Instanced))
		if (ClassName.IsEmpty())
		{
			UE_LOG(LogAIWidgetBuilder, Warning, TEXT("AddArrayElement: Object array '%s' requires class_name parameter"),
				*ArrayPropertyName);
			ArrayHelper.RemoveValues(NewIndex, 1);
			return -1;
		}

		UClass* ObjClass = FindObject<UClass>(nullptr, *ClassName);
		if (!ObjClass)
		{
			ObjClass = LoadObject<UClass>(nullptr, *ClassName);
		}
		if (!ObjClass)
		{
			UE_LOG(LogAIWidgetBuilder, Warning, TEXT("AddArrayElement: Could not find class '%s'"), *ClassName);
			ArrayHelper.RemoveValues(NewIndex, 1);
			return -1;
		}

		UObject* NewObj = NewObject<UObject>(Object, ObjClass, NAME_None, RF_Public | RF_Transactional);
		InnerObjProp->SetObjectPropertyValue(ArrayHelper.GetRawPtr(NewIndex), NewObj);

		// Set sub-properties on the newly created object
		for (const auto& Pair : ElementValues)
		{
			FProperty* SubProp = NewObj->GetClass()->FindPropertyByName(FName(*Pair.Key));
			if (SubProp)
			{
				void* SubPtr = SubProp->ContainerPtrToValuePtr<void>(NewObj);
				SubProp->ImportText_Direct(*Pair.Value, SubPtr, NewObj, PPF_None);
			}
			else
			{
				UE_LOG(LogAIWidgetBuilder, Warning, TEXT("AddArrayElement: Sub-property '%s' not found on %s"),
					*Pair.Key, *ObjClass->GetName());
			}
		}
	}
	else if (!InnerStructProp && ElementValues.Num() > 0)
	{
		// Simple type array — use the first value
		void* ElemPtr2 = ArrayHelper.GetRawPtr(NewIndex);
		for (const auto& Pair : ElementValues)
		{
			ArrayProp->Inner->ImportText_Direct(*Pair.Value, ElemPtr2, Object, PPF_None);
			break; // Only one value for simple types
		}
	}

	Object->MarkPackageDirty();
	UE_LOG(LogAIWidgetBuilder, Log, TEXT("AddArrayElement: Added element at index %d to '%s'"),
		NewIndex, *ArrayPropertyName);
	return NewIndex;
}

bool UMCTWidgetBlueprintBuilder::RemoveArrayElement(
	UObject* Object,
	const FString& ArrayPropertyName,
	int32 Index)
{
	if (!Object)
	{
		return false;
	}

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Object->GetClass()->FindPropertyByName(FName(*ArrayPropertyName)));
	if (!ArrayProp)
	{
		return false;
	}

	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Object));
	if (Index < 0 || Index >= ArrayHelper.Num())
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIRemoveArrayElement", "AI: Remove Array Element"));

	ArrayHelper.RemoveValues(Index, 1);
	Object->MarkPackageDirty();
	return true;
}

int32 UMCTWidgetBlueprintBuilder::GetArrayLength(
	UObject* Object,
	const FString& ArrayPropertyName)
{
	if (!Object)
	{
		return -1;
	}

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Object->GetClass()->FindPropertyByName(FName(*ArrayPropertyName)));
	if (!ArrayProp)
	{
		return -1;
	}

	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Object));
	return ArrayHelper.Num();
}

bool UMCTWidgetBlueprintBuilder::SetArrayElementProperty(
	UObject* Object,
	const FString& ArrayPropertyName,
	int32 Index,
	const FString& SubPropertyName,
	const FString& Value)
{
	if (!Object)
	{
		return false;
	}

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(
		Object->GetClass()->FindPropertyByName(FName(*ArrayPropertyName)));
	if (!ArrayProp)
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetArrayElementProperty: Array '%s' not found"), *ArrayPropertyName);
		return false;
	}

	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Object));
	if (Index < 0 || Index >= ArrayHelper.Num())
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetArrayElementProperty: Index %d out of range (array has %d elements)"),
			Index, ArrayHelper.Num());
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AISetArrayElementProperty", "AI: Set Array Element Property"));

	// Instanced UObject array element — navigate into the inner object
	if (FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner))
	{
		UObject* InnerObj = InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(Index));
		if (!InnerObj)
		{
			UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetArrayElementProperty: Element %d of '%s' is null"),
				Index, *ArrayPropertyName);
			return false;
		}

		FProperty* SubProp = InnerObj->GetClass()->FindPropertyByName(FName(*SubPropertyName));
		if (!SubProp)
		{
			UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetArrayElementProperty: Property '%s' not found on %s"),
				*SubPropertyName, *InnerObj->GetClass()->GetName());
			return false;
		}

		void* SubPtr = SubProp->ContainerPtrToValuePtr<void>(InnerObj);
		const TCHAR* ValueCStr = *Value;
		bool bResult = SubProp->ImportText_Direct(ValueCStr, SubPtr, InnerObj, PPF_None) != nullptr;
		if (bResult)
		{
			Object->MarkPackageDirty();
		}
		return bResult;
	}

	FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
	if (!InnerStructProp)
	{
		// Simple type — set directly
		void* ElemPtr = ArrayHelper.GetRawPtr(Index);
		const TCHAR* ValueCStr = *Value;
		return ArrayProp->Inner->ImportText_Direct(ValueCStr, ElemPtr, Object, PPF_None) != nullptr;
	}

	// Struct element — navigate with dot-notation
	void* ElemPtr = ArrayHelper.GetRawPtr(Index);
	TArray<FString> SubParts;
	SubPropertyName.ParseIntoArray(SubParts, TEXT("."));

	UStruct* CurrentStruct = InnerStructProp->Struct;
	void* CurrentContainer = ElemPtr;

	for (int32 i = 0; i < SubParts.Num(); ++i)
	{
		FProperty* SubProp = CurrentStruct->FindPropertyByName(FName(*SubParts[i]));
		if (!SubProp)
		{
			UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetArrayElementProperty: Property '%s' not found in %s"),
				*SubParts[i], *CurrentStruct->GetName());
			return false;
		}

		if (i == SubParts.Num() - 1)
		{
			void* SubPtr = SubProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			const TCHAR* ValueCStr = *Value;
			bool bResult = SubProp->ImportText_Direct(ValueCStr, SubPtr, Object, PPF_None) != nullptr;
			if (bResult)
			{
				Object->MarkPackageDirty();
			}
			return bResult;
		}
		else
		{
			FStructProperty* SubStructProp = CastField<FStructProperty>(SubProp);
			if (!SubStructProp)
			{
				return false;
			}
			CurrentContainer = SubStructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			CurrentStruct = SubStructProp->Struct;
		}
	}

	return false;
}

// =============================================================================
// PRIVATE UTILITIES
// =============================================================================

void UMCTWidgetBlueprintBuilder::CollectAllDescendants(UPanelWidget* Parent, TArray<UWidget*>& OutDescendants)
{
	if (!Parent)
	{
		return;
	}

	for (int32 i = 0; i < Parent->GetChildrenCount(); ++i)
	{
		UWidget* Child = Parent->GetChildAt(i);
		if (Child)
		{
			OutDescendants.Add(Child);
			if (UPanelWidget* ChildPanel = Cast<UPanelWidget>(Child))
			{
				CollectAllDescendants(ChildPanel, OutDescendants);
			}
		}
	}
}

void UMCTWidgetBlueprintBuilder::MarkModified(UWidgetBlueprint* WidgetBP)
{
	if (WidgetBP)
	{
		// Use non-structural mark to avoid triggering immediate recompilation.
		// Structural compile happens in CompileAndSave() after all edits are done.
		// MarkBlueprintAsStructurallyModified triggers full recompile which can crash
		// when BindWidget variables don't yet have matching widgets in the tree.
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	}
}

TSharedPtr<FJsonObject> UMCTWidgetBlueprintBuilder::WidgetToJson(UWidget* Widget)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	if (!Widget)
	{
		return Obj;
	}

	Obj->SetStringField(TEXT("name"), Widget->GetName());
	Obj->SetStringField(TEXT("type"), Widget->GetClass()->GetName());
	Obj->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);

	// Visibility
	if (Widget->GetVisibility() != ESlateVisibility::Visible)
	{
		Obj->SetStringField(TEXT("visibility"),
			UEnum::GetValueAsString(Widget->GetVisibility()));
	}

	// Slot info
	if (Widget->Slot)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("type"), Widget->Slot->GetClass()->GetName());

		UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
		if (CanvasSlot)
		{
			auto Anchors = CanvasSlot->GetAnchors();
			auto Offsets = CanvasSlot->GetOffsets();
			auto Alignment = CanvasSlot->GetAlignment();

			SlotObj->SetStringField(TEXT("offsets"),
				FString::Printf(TEXT("L=%.1f T=%.1f R=%.1f B=%.1f"),
					Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom));
			SlotObj->SetStringField(TEXT("anchors"),
				FString::Printf(TEXT("Min(%.2f,%.2f) Max(%.2f,%.2f)"),
					Anchors.Minimum.X, Anchors.Minimum.Y,
					Anchors.Maximum.X, Anchors.Maximum.Y));
			SlotObj->SetStringField(TEXT("alignment"),
				FString::Printf(TEXT("(%.2f,%.2f)"), Alignment.X, Alignment.Y));
		}

		Obj->SetObjectField(TEXT("slot"), SlotObj);
	}

	// Children (if panel)
	UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
	if (Panel && Panel->GetChildrenCount() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ChildrenArray;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			if (UWidget* Child = Panel->GetChildAt(i))
			{
				ChildrenArray.Add(MakeShared<FJsonValueObject>(WidgetToJson(Child)));
			}
		}
		Obj->SetArrayField(TEXT("children"), ChildrenArray);
	}

	return Obj;
}

#undef LOCTEXT_NAMESPACE
