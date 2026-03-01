// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/AIWidgetBlueprintBuilder.h"

#include "WidgetBlueprint.h"
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
#include "KismetCompilerModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogAIWidgetBuilder, Log, All);

// =============================================================================
// BLUEPRINT LIFECYCLE
// =============================================================================

UWidgetBlueprint* UAIWidgetBlueprintBuilder::CreateWidgetBlueprint(
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

	// Set root widget to CanvasPanel by default if none exists
	if (WidgetBP->WidgetTree && !WidgetBP->WidgetTree->RootWidget)
	{
		UWidget* RootCanvas = WidgetBP->WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), FName(TEXT("RootCanvas")));
		WidgetBP->WidgetTree->RootWidget = RootCanvas;
	}

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(WidgetBP);
	WidgetBP->MarkPackageDirty();

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("CreateWidgetBlueprint: Created '%s' with parent '%s'"),
		*FullPath, *ParentClass->GetName());

	return WidgetBP;
}

bool UAIWidgetBlueprintBuilder::CompileAndSave(
	UWidgetBlueprint* WidgetBP,
	TArray<FString>* OutWarnings)
{
	if (!WidgetBP)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CompileAndSave: WidgetBP is null"));
		return false;
	}

	// Compile
	FKismetEditorUtilities::CompileBlueprint(WidgetBP, EBlueprintCompileOptions::None);

	// Check for errors
	bool bHasErrors = WidgetBP->Status == BS_Error;
	if (bHasErrors)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("CompileAndSave: Compilation failed for '%s'"),
			*WidgetBP->GetName());
	}

	// Collect warnings
	if (OutWarnings)
	{
		// Blueprint compiler messages are in the message log, not easily accessible here.
		// We check the status instead.
		if (WidgetBP->Status == BS_UpToDateWithWarnings)
		{
			OutWarnings->Add(TEXT("Blueprint compiled with warnings"));
		}
	}

	// Save
	UPackage* Package = WidgetBP->GetOutermost();
	if (Package)
	{
		FString PackageFilename;
		if (FPackageName::DoesPackageExist(Package->GetName(), &PackageFilename))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, WidgetBP, *PackageFilename, SaveArgs);
		}
		else
		{
			// New package - save to the appropriate location
			FString PackagePath = FPackageName::LongPackageNameToFilename(
				Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			UPackage::SavePackage(Package, WidgetBP, *PackagePath, SaveArgs);
		}
	}

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("CompileAndSave: '%s' %s"),
		*WidgetBP->GetName(),
		bHasErrors ? TEXT("FAILED") : TEXT("succeeded"));

	return !bHasErrors;
}

// =============================================================================
// WIDGET TREE MANIPULATION
// =============================================================================

UWidget* UAIWidgetBlueprintBuilder::AddWidget(
	UWidgetBlueprint* WidgetBP,
	const FString& WidgetClassName,
	const FString& WidgetName,
	const FString& ParentWidgetName)
{
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Invalid WidgetBP or WidgetTree"));
		return nullptr;
	}

	// Resolve widget class
	UClass* WidgetClass = ResolveWidgetClass(WidgetClassName);
	if (!WidgetClass)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Could not resolve class '%s'"), *WidgetClassName);
		return nullptr;
	}

	// Construct the widget
	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(
		TSubclassOf<UWidget>(WidgetClass),
		FName(*WidgetName));

	if (!NewWidget)
	{
		UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: ConstructWidget failed for class '%s'"), *WidgetClassName);
		return nullptr;
	}

	// Mark as variable so it's accessible in Blueprint graphs
	NewWidget->bIsVariable = true;

	// Add to parent or set as root
	if (ParentWidgetName.IsEmpty())
	{
		// Set as root widget
		WidgetBP->WidgetTree->RootWidget = NewWidget;
		UE_LOG(LogAIWidgetBuilder, Log, TEXT("AddWidget: Set '%s' (%s) as root"),
			*NewWidget->GetName(), *WidgetClassName);
	}
	else
	{
		UWidget* ParentWidget = FindWidgetByName(WidgetBP, ParentWidgetName);
		if (!ParentWidget)
		{
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Parent '%s' not found"), *ParentWidgetName);
			return nullptr;
		}

		UPanelWidget* PanelParent = Cast<UPanelWidget>(ParentWidget);
		if (!PanelParent)
		{
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: Parent '%s' is not a panel widget (class: %s)"),
				*ParentWidgetName, *ParentWidget->GetClass()->GetName());
			return nullptr;
		}

		UPanelSlot* Slot = PanelParent->AddChild(NewWidget);
		if (!Slot)
		{
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("AddWidget: AddChild failed for parent '%s'"), *ParentWidgetName);
			return nullptr;
		}

		UE_LOG(LogAIWidgetBuilder, Log, TEXT("AddWidget: Added '%s' (%s) to parent '%s'"),
			*NewWidget->GetName(), *WidgetClassName, *ParentWidgetName);
	}

	MarkModified(WidgetBP);
	return NewWidget;
}

bool UAIWidgetBlueprintBuilder::RemoveWidget(
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

	// If it's the root, clear root
	if (Widget == WidgetBP->WidgetTree->RootWidget)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
	}
	else if (Widget->Slot)
	{
		// Remove from parent panel
		UPanelWidget* Parent = Widget->Slot->Parent;
		if (Parent)
		{
			Parent->RemoveChild(Widget);
		}
	}

	WidgetBP->WidgetTree->RemoveWidget(Widget);
	MarkModified(WidgetBP);

	UE_LOG(LogAIWidgetBuilder, Log, TEXT("RemoveWidget: Removed '%s'"), *WidgetName);
	return true;
}

bool UAIWidgetBlueprintBuilder::MoveWidget(
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

	// Remove from current parent
	if (Widget == WidgetBP->WidgetTree->RootWidget)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
	}
	else if (Widget->Slot && Widget->Slot->Parent)
	{
		Widget->Slot->Parent->RemoveChild(Widget);
	}

	// Add to new parent
	if (NewParentName.IsEmpty())
	{
		WidgetBP->WidgetTree->RootWidget = Widget;
	}
	else
	{
		UWidget* NewParent = FindWidgetByName(WidgetBP, NewParentName);
		UPanelWidget* PanelParent = Cast<UPanelWidget>(NewParent);
		if (!PanelParent)
		{
			UE_LOG(LogAIWidgetBuilder, Error, TEXT("MoveWidget: New parent '%s' is not a panel"),
				*NewParentName);
			return false;
		}

		PanelParent->AddChild(Widget);
		// TODO: Handle NewIndex reordering if needed
	}

	MarkModified(WidgetBP);
	return true;
}

// =============================================================================
// PROPERTY SETTING
// =============================================================================

bool UAIWidgetBlueprintBuilder::SetWidgetProperty(
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

	bool bSuccess = SetPropertyByPath(Widget, PropertyName, Value);
	if (bSuccess)
	{
		MarkModified(WidgetBP);
	}
	else
	{
		UE_LOG(LogAIWidgetBuilder, Warning, TEXT("SetWidgetProperty: Failed to set '%s'='%s' on '%s'"),
			*PropertyName, *Value, *WidgetName);
	}

	return bSuccess;
}

bool UAIWidgetBlueprintBuilder::SetSlotProperty(
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

int32 UAIWidgetBlueprintBuilder::SetWidgetProperties(
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
		MarkModified(WidgetBP);
	}

	return SuccessCount;
}

bool UAIWidgetBlueprintBuilder::SetCanvasSlotLayout(
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

UClass* UAIWidgetBlueprintBuilder::ResolveWidgetClass(const FString& ClassNameOrPath)
{
	// Try full path first
	if (ClassNameOrPath.StartsWith(TEXT("/")))
	{
		UClass* Cls = LoadObject<UClass>(nullptr, *ClassNameOrPath);
		if (Cls && Cls->IsChildOf(UWidget::StaticClass()))
		{
			return Cls;
		}
	}

	// Try the cached map
	TMap<FString, UClass*>& Map = GetWidgetClassMap();
	if (UClass** Found = Map.Find(ClassNameOrPath))
	{
		return *Found;
	}

	// Try case-insensitive search
	for (const auto& Pair : Map)
	{
		if (Pair.Key.Equals(ClassNameOrPath, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	UE_LOG(LogAIWidgetBuilder, Warning, TEXT("ResolveWidgetClass: Could not resolve '%s'"), *ClassNameOrPath);
	return nullptr;
}

UWidget* UAIWidgetBlueprintBuilder::FindWidgetByName(
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

UWidgetBlueprint* UAIWidgetBlueprintBuilder::LoadWidgetBlueprint(const FString& AssetPath)
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

TSharedPtr<FJsonObject> UAIWidgetBlueprintBuilder::GetWidgetTreeAsJson(UWidgetBlueprint* WidgetBP)
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

TArray<TPair<FString, bool>> UAIWidgetBlueprintBuilder::GetAvailableWidgetClasses()
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

TMap<FString, UClass*>& UAIWidgetBlueprintBuilder::GetWidgetClassMap()
{
	static TMap<FString, UClass*> ClassMap;

	if (ClassMap.Num() == 0)
	{
		// Iterate all UWidget subclasses
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UWidget::StaticClass()))
			{
				continue;
			}
			if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			// Skip SKEL_ and REINST_ (hot-reload artifacts)
			FString ClassName = Class->GetName();
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
			{
				continue;
			}

			ClassMap.Add(ClassName, Class);
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
	}

	return ClassMap;
}

bool UAIWidgetBlueprintBuilder::SetPropertyByPath(
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

void UAIWidgetBlueprintBuilder::MarkModified(UWidgetBlueprint* WidgetBP)
{
	if (WidgetBP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	}
}

TSharedPtr<FJsonObject> UAIWidgetBlueprintBuilder::WidgetToJson(UWidget* Widget)
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
