// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTInputExporter.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"

bool UMCTInputExporter::CanExport(UObject* Asset) const
{
	if (!Asset)
	{
		return false;
	}

	return Asset->IsA<UInputAction>() || Asset->IsA<UInputMappingContext>();
}

TArray<UClass*> UMCTInputExporter::GetSupportedClasses() const
{
	return {
		UInputAction::StaticClass(),
		UInputMappingContext::StaticClass()
	};
}

FString UMCTInputExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	if (UInputAction* InputAction = Cast<UInputAction>(Asset))
	{
		return ExportInputAction(InputAction, bFilterDefaults);
	}
	else if (UInputMappingContext* MappingContext = Cast<UInputMappingContext>(Asset))
	{
		return ExportInputMappingContext(MappingContext, bFilterDefaults);
	}

	return TEXT("Error: Unsupported input asset type\n");
}

FString UMCTInputExporter::ExportInputAction(UInputAction* InputAction, bool bFilterDefaults)
{
	FString Output;

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("INPUT ACTION: %s"), *InputAction->GetName()));

	// Key properties
	Output += FString::Printf(TEXT("ValueType: %s\n"),
		*UEnum::GetValueAsString(InputAction->ValueType));

	// Triggers
	if (InputAction->Triggers.Num() > 0)
	{
		Output += TEXT("\n");
		Output += MakeSubsectionHeader(TEXT("Triggers"));
		for (int32 i = 0; i < InputAction->Triggers.Num(); ++i)
		{
			if (UInputTrigger* Trigger = InputAction->Triggers[i])
			{
				Output += FString::Printf(TEXT("[%d] %s\n"), i, *Trigger->GetClass()->GetName());
			}
		}
	}

	// Modifiers
	if (InputAction->Modifiers.Num() > 0)
	{
		Output += TEXT("\n");
		Output += MakeSubsectionHeader(TEXT("Modifiers"));
		for (int32 i = 0; i < InputAction->Modifiers.Num(); ++i)
		{
			if (UInputModifier* Modifier = InputAction->Modifiers[i])
			{
				Output += FString::Printf(TEXT("[%d] %s\n"), i, *Modifier->GetClass()->GetName());
			}
		}
	}

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(InputAction, 0, bFilterDefaults);

	return Output;
}

FString UMCTInputExporter::ExportInputMappingContext(UInputMappingContext* MappingContext, bool bFilterDefaults)
{
	FString Output;

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("INPUT MAPPING CONTEXT: %s"), *MappingContext->GetName()));

	// Mappings
	const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();
	Output += FString::Printf(TEXT("MappingCount: %d\n\n"), Mappings.Num());

	if (Mappings.Num() > 0)
	{
		Output += MakeSubsectionHeader(TEXT("Mappings"));

		for (int32 i = 0; i < Mappings.Num(); ++i)
		{
			const FEnhancedActionKeyMapping& Mapping = Mappings[i];

			Output += FString::Printf(TEXT("\n[%d] %s\n"),
				i,
				Mapping.Action ? *Mapping.Action->GetName() : TEXT("None"));

			Output += FString::Printf(TEXT("  Key: %s\n"), *Mapping.Key.ToString());

			// Triggers
			if (Mapping.Triggers.Num() > 0)
			{
				Output += TEXT("  Triggers:\n");
				for (UInputTrigger* Trigger : Mapping.Triggers)
				{
					if (Trigger)
					{
						Output += FString::Printf(TEXT("    - %s\n"), *Trigger->GetClass()->GetName());
					}
				}
			}

			// Modifiers
			if (Mapping.Modifiers.Num() > 0)
			{
				Output += TEXT("  Modifiers:\n");
				for (UInputModifier* Modifier : Mapping.Modifiers)
				{
					if (Modifier)
					{
						Output += FString::Printf(TEXT("    - %s\n"), *Modifier->GetClass()->GetName());
					}
				}
			}
		}
	}

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(MappingContext, 0, bFilterDefaults);

	return Output;
}
