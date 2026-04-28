// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Builders/AIAssetFactory.h"
#include "Builders/AIDataAssetBuilder.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AIAssetFactory)

// Enhanced Input
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "InputModifiers.h"

// Audio Foundation
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundAttenuation.h"

// Audio Modulation
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulationPatch.h"

// Physics
#include "PhysicalMaterials/PhysicalMaterial.h"

// Asset management
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"

// JSON
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Undo/Redo
#include "ScopedTransaction.h"

// For FKey
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "AIAssetFactory"

DEFINE_LOG_CATEGORY_STATIC(LogAIAssetFactory, Log, All);

// =============================================================================
// UTILITY
// =============================================================================

UClass* UAIAssetFactory::ResolveAssetClass(const FString& AssetType)
{
	if (AssetType.Equals(TEXT("InputAction"), ESearchCase::IgnoreCase))
		return UInputAction::StaticClass();
	if (AssetType.Equals(TEXT("InputMappingContext"), ESearchCase::IgnoreCase))
		return UInputMappingContext::StaticClass();
	if (AssetType.Equals(TEXT("SoundClass"), ESearchCase::IgnoreCase))
		return USoundClass::StaticClass();
	if (AssetType.Equals(TEXT("SoundSubmix"), ESearchCase::IgnoreCase))
		return USoundSubmix::StaticClass();
	if (AssetType.Equals(TEXT("SoundConcurrency"), ESearchCase::IgnoreCase))
		return USoundConcurrency::StaticClass();
	if (AssetType.Equals(TEXT("SoundAttenuation"), ESearchCase::IgnoreCase))
		return USoundAttenuation::StaticClass();
	if (AssetType.Equals(TEXT("SoundControlBus"), ESearchCase::IgnoreCase))
		return USoundControlBus::StaticClass();
	if (AssetType.Equals(TEXT("SoundControlBusMix"), ESearchCase::IgnoreCase))
		return USoundControlBusMix::StaticClass();
	if (AssetType.Equals(TEXT("SoundModulationPatch"), ESearchCase::IgnoreCase))
		return USoundModulationPatch::StaticClass();
	if (AssetType.Equals(TEXT("PhysicalMaterial"), ESearchCase::IgnoreCase))
		return UPhysicalMaterial::StaticClass();

	// Generic fallback — resolve any loaded concrete UObject class by short or
	// fully-qualified name. Enables DataAsset subclasses (UOkeySkinDefinition
	// etc.) without extending the hardcoded switch for every project type.
	if (!AssetType.IsEmpty())
	{
		// 1) Try direct object lookup by path (e.g. "/Script/OkeyGame.OkeySkinDefinition")
		if (UClass* ByPath = FindObject<UClass>(nullptr, *AssetType))
		{
			if (!ByPath->HasAnyClassFlags(CLASS_Abstract))
			{
				UE_LOG(LogAIAssetFactory, Log,
					TEXT("ResolveAssetClass: Resolved '%s' by path -> %s"),
					*AssetType, *ByPath->GetName());
				return ByPath;
			}
		}

		// 2) Try short name iteration across loaded classes. Accept concrete
		//    descendants of UObject (covers UDataAsset, UPrimaryDataAsset, etc.).
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			if (It->GetName().Equals(AssetType, ESearchCase::IgnoreCase)
			 || It->GetPathName().Equals(AssetType, ESearchCase::IgnoreCase))
			{
				UE_LOG(LogAIAssetFactory, Log,
					TEXT("ResolveAssetClass: Resolved '%s' via reflection -> %s"),
					*AssetType, *It->GetPathName());
				return *It;
			}
		}
	}

	UE_LOG(LogAIAssetFactory, Warning, TEXT("ResolveAssetClass: Unsupported type '%s'"), *AssetType);
	return nullptr;
}

TArray<FString> UAIAssetFactory::GetSupportedTypes()
{
	return {
		TEXT("InputAction"),
		TEXT("InputMappingContext"),
		TEXT("SoundClass"),
		TEXT("SoundSubmix"),
		TEXT("SoundConcurrency"),
		TEXT("SoundAttenuation"),
		TEXT("SoundControlBus"),
		TEXT("SoundControlBusMix"),
		TEXT("SoundModulationPatch"),
		TEXT("PhysicalMaterial")
	};
}

bool UAIAssetFactory::SaveAsset(UObject* Asset)
{
	return UAIDataAssetBuilder::SaveAsset(Asset);
}

// =============================================================================
// GENERIC ASSET CREATION
// =============================================================================

UObject* UAIAssetFactory::CreateAsset(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& AssetType,
	const TMap<FString, FString>& InitialProperties)
{
	if (PackagePath.IsEmpty() || AssetName.IsEmpty() || AssetType.IsEmpty())
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("CreateAsset: PackagePath, AssetName and AssetType required"));
		return nullptr;
	}

	UClass* AssetClass = ResolveAssetClass(AssetType);
	if (!AssetClass)
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("CreateAsset: Unknown asset type '%s'"), *AssetType);
		return nullptr;
	}

	FString FullPath = PackagePath / AssetName;

	// Return existing if found
	if (UObject* Existing = StaticLoadObject(AssetClass, nullptr, *FullPath))
	{
		UE_LOG(LogAIAssetFactory, Warning, TEXT("CreateAsset: Already exists at '%s', returning existing"), *FullPath);
		return Existing;
	}

	FScopedTransaction Transaction(
		FText::Format(LOCTEXT("AICreateAsset", "AI: Create {0}"), FText::FromString(AssetType)));

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("CreateAsset: Failed to create package at '%s'"), *FullPath);
		return nullptr;
	}

	UObject* NewAsset = NewObject<UObject>(Package, AssetClass, FName(*AssetName),
		RF_Public | RF_Standalone);

	if (!NewAsset)
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("CreateAsset: Failed to create %s '%s'"), *AssetType, *AssetName);
		return nullptr;
	}

	// Apply initial properties
	for (const auto& Pair : InitialProperties)
	{
		UAIDataAssetBuilder::SetProperty(NewAsset, Pair.Key, Pair.Value);
	}

	FAssetRegistryModule::AssetCreated(NewAsset);
	NewAsset->MarkPackageDirty();

	UE_LOG(LogAIAssetFactory, Log, TEXT("CreateAsset: Created %s '%s' at '%s'"),
		*AssetType, *AssetName, *FullPath);
	return NewAsset;
}

// =============================================================================
// INPUT MAPPING CONTEXT — CONVENIENCE METHODS
// =============================================================================

static UClass* FindTriggerClass(const FString& ShortName)
{
	// Try common trigger class names
	static const TMap<FString, FString> TriggerAliases = {
		{TEXT("Pressed"), TEXT("InputTriggerPressed")},
		{TEXT("Released"), TEXT("InputTriggerReleased")},
		{TEXT("Down"), TEXT("InputTriggerDown")},
		{TEXT("Hold"), TEXT("InputTriggerHold")},
		{TEXT("HoldAndRelease"), TEXT("InputTriggerHoldAndRelease")},
		{TEXT("Tap"), TEXT("InputTriggerTap")},
		{TEXT("Pulse"), TEXT("InputTriggerPulse")},
		{TEXT("ChordAction"), TEXT("InputTriggerChordAction")},
		{TEXT("ChordBlocker"), TEXT("InputTriggerChordBlocker")},
		{TEXT("Combo"), TEXT("InputTriggerCombo")},
	};

	FString ClassName = ShortName;
	if (const FString* Found = TriggerAliases.Find(ShortName))
	{
		ClassName = *Found;
	}

	// Prefix with U if missing
	FString SearchName = ClassName;
	if (!SearchName.StartsWith(TEXT("U")))
	{
		SearchName = TEXT("U") + SearchName;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UInputTrigger::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName() == SearchName || It->GetName() == ClassName)
			{
				return *It;
			}
		}
	}

	UE_LOG(LogAIAssetFactory, Warning, TEXT("FindTriggerClass: Not found '%s'"), *ShortName);
	return nullptr;
}

static UClass* FindModifierClass(const FString& ShortName)
{
	static const TMap<FString, FString> ModifierAliases = {
		{TEXT("Negate"), TEXT("InputModifierNegate")},
		{TEXT("DeadZone"), TEXT("InputModifierDeadZone")},
		{TEXT("Scalar"), TEXT("InputModifierScalar")},
		{TEXT("ScaleByDeltaTime"), TEXT("InputModifierScaleByDeltaTime")},
		{TEXT("FOVScaling"), TEXT("InputModifierFOVScaling")},
		{TEXT("ResponseCurve"), TEXT("InputModifierResponseCurveExponential")},
		{TEXT("Smooth"), TEXT("InputModifierSmooth")},
		{TEXT("SwizzleAxis"), TEXT("InputModifierSwizzleAxis")},
		{TEXT("ToWorldSpace"), TEXT("InputModifierToWorldSpace")},
	};

	FString ClassName = ShortName;
	if (const FString* Found = ModifierAliases.Find(ShortName))
	{
		ClassName = *Found;
	}

	FString SearchName = ClassName;
	if (!SearchName.StartsWith(TEXT("U")))
	{
		SearchName = TEXT("U") + SearchName;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UInputModifier::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName() == SearchName || It->GetName() == ClassName)
			{
				return *It;
			}
		}
	}

	UE_LOG(LogAIAssetFactory, Warning, TEXT("FindModifierClass: Not found '%s'"), *ShortName);
	return nullptr;
}

bool UAIAssetFactory::AddInputMapping(
	UInputMappingContext* IMC,
	const FString& InputActionPath,
	const FString& KeyName,
	const TArray<FString>& TriggerClasses,
	const TArray<FString>& ModifierClasses)
{
	if (!IMC)
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("AddInputMapping: Null IMC"));
		return false;
	}

	// Load the InputAction
	FString CleanPath = InputActionPath;
	if (!CleanPath.Contains(TEXT(".")))
	{
		FString Name = FPaths::GetCleanFilename(CleanPath);
		CleanPath = CleanPath + TEXT(".") + Name;
	}
	UInputAction* Action = LoadObject<UInputAction>(nullptr, *CleanPath);
	if (!Action)
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("AddInputMapping: InputAction not found at '%s'"), *InputActionPath);
		return false;
	}

	// Resolve key
	const FKey Key{FName(*KeyName)};
	if (!Key.IsValid())
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("AddInputMapping: Invalid key name '%s'"), *KeyName);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIAddInputMapping", "AI: Add Input Mapping"));
	IMC->Modify();

	// Create the mapping
	FEnhancedActionKeyMapping NewMapping;
	NewMapping.Action = Action;
	NewMapping.Key = Key;

	// Add triggers
	for (const FString& TriggerName : TriggerClasses)
	{
		UClass* TriggerClass = FindTriggerClass(TriggerName);
		if (TriggerClass)
		{
			UInputTrigger* Trigger = NewObject<UInputTrigger>(IMC, TriggerClass);
			NewMapping.Triggers.Add(Trigger);
		}
	}

	// Add modifiers
	for (const FString& ModifierName : ModifierClasses)
	{
		UClass* ModifierClass = FindModifierClass(ModifierName);
		if (ModifierClass)
		{
			UInputModifier* Modifier = NewObject<UInputModifier>(IMC, ModifierClass);
			NewMapping.Modifiers.Add(Modifier);
		}
	}

	// Add to the IMC's mappings array
	IMC->MapKey(Action, Key);

	// The last mapping should be the one we just added
	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());
	if (Mappings.Num() > 0)
	{
		FEnhancedActionKeyMapping& LastMapping = Mappings.Last();
		LastMapping.Triggers = NewMapping.Triggers;
		LastMapping.Modifiers = NewMapping.Modifiers;
	}

	IMC->MarkPackageDirty();

	UE_LOG(LogAIAssetFactory, Log, TEXT("AddInputMapping: Added %s → %s to %s"),
		*Action->GetName(), *KeyName, *IMC->GetName());
	return true;
}

bool UAIAssetFactory::RemoveInputMapping(UInputMappingContext* IMC, int32 MappingIndex)
{
	if (!IMC)
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("RemoveInputMapping: Null IMC"));
		return false;
	}

	TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(IMC->GetMappings());

	if (MappingIndex < 0 || MappingIndex >= Mappings.Num())
	{
		UE_LOG(LogAIAssetFactory, Error, TEXT("RemoveInputMapping: Index %d out of range (0-%d)"),
			MappingIndex, Mappings.Num() - 1);
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AIRemoveInputMapping", "AI: Remove Input Mapping"));
	IMC->Modify();

	// UnmapKey by action and key
	FEnhancedActionKeyMapping& Mapping = Mappings[MappingIndex];
	IMC->UnmapKey(Mapping.Action, Mapping.Key);

	IMC->MarkPackageDirty();

	UE_LOG(LogAIAssetFactory, Log, TEXT("RemoveInputMapping: Removed mapping at index %d from %s"),
		MappingIndex, *IMC->GetName());
	return true;
}

TSharedPtr<FJsonObject> UAIAssetFactory::GetInputMappingsAsJson(UInputMappingContext* IMC)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!IMC)
	{
		return Result;
	}

	Result->SetStringField(TEXT("asset_name"), IMC->GetName());
	Result->SetStringField(TEXT("asset_path"), IMC->GetPathName());

	TArray<TSharedPtr<FJsonValue>> MappingsArray;
	const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();

	for (int32 i = 0; i < Mappings.Num(); ++i)
	{
		const FEnhancedActionKeyMapping& Mapping = Mappings[i];

		TSharedPtr<FJsonObject> MappingObj = MakeShared<FJsonObject>();
		MappingObj->SetNumberField(TEXT("index"), i);
		MappingObj->SetStringField(TEXT("action"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT("None"));
		MappingObj->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());

		// Triggers
		TArray<TSharedPtr<FJsonValue>> TriggersArray;
		for (const UInputTrigger* Trigger : Mapping.Triggers)
		{
			if (Trigger)
			{
				TriggersArray.Add(MakeShared<FJsonValueString>(Trigger->GetClass()->GetName()));
			}
		}
		MappingObj->SetArrayField(TEXT("triggers"), TriggersArray);

		// Modifiers
		TArray<TSharedPtr<FJsonValue>> ModifiersArray;
		for (const UInputModifier* Modifier : Mapping.Modifiers)
		{
			if (Modifier)
			{
				ModifiersArray.Add(MakeShared<FJsonValueString>(Modifier->GetClass()->GetName()));
			}
		}
		MappingObj->SetArrayField(TEXT("modifiers"), ModifiersArray);

		MappingsArray.Add(MakeShared<FJsonValueObject>(MappingObj));
	}

	Result->SetArrayField(TEXT("mappings"), MappingsArray);
	return Result;
}

#undef LOCTEXT_NAMESPACE
