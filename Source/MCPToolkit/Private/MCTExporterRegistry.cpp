// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTExporterRegistry.h"
#include "Exporters/MCTExporterBase.h"

// All exporter headers
#include "Exporters/MCTBlueprintExporter.h"
#include "Exporters/MCTAnimBlueprintExporter.h"
#include "Exporters/MCTWidgetBlueprintExporter.h"
#include "Exporters/MCTDataAssetExporter.h"
#include "Exporters/MCTDataTableExporter.h"
#include "Exporters/MCTInputExporter.h"
#include "Exporters/MCTAudioExporter.h"
#include "Exporters/MCTWorldExporter.h"
#include "Exporters/MCTTextureExporter.h"
#include "Exporters/MCTMaterialExporter.h"
#include "Exporters/MCTPhysicalMaterialExporter.h"

UMCTExporterRegistry* UMCTExporterRegistry::Instance = nullptr;

UMCTExporterRegistry* UMCTExporterRegistry::Get()
{
	if (!Instance)
	{
		// Create the singleton instance
		Instance = NewObject<UMCTExporterRegistry>();
		Instance->AddToRoot(); // Prevent garbage collection

		// Register default exporters
		Instance->RegisterDefaultExporters();
	}

	return Instance;
}

void UMCTExporterRegistry::RegisterExporter(TSubclassOf<UMCTExporterBase> ExporterClass)
{
	if (!ExporterClass)
	{
		return;
	}

	// Check if already registered
	for (UMCTExporterBase* Existing : RegisteredExporters)
	{
		if (Existing && Existing->GetClass() == ExporterClass)
		{
			return; // Already registered
		}
	}

	// Create instance
	UMCTExporterBase* Exporter = NewObject<UMCTExporterBase>(this, ExporterClass);
	if (Exporter)
	{
		RegisteredExporters.Add(Exporter);
		SortExportersByPriority();

		UE_LOG(LogTemp, Log, TEXT("MCTExporterRegistry: Registered exporter %s (Priority: %d)"),
			*Exporter->GetExporterDisplayName(), Exporter->GetPriority());
	}
}

void UMCTExporterRegistry::RegisterExporter(UMCTExporterBase* Exporter)
{
	if (!Exporter)
	{
		return;
	}

	// Check if already registered
	if (RegisteredExporters.Contains(Exporter))
	{
		return;
	}

	// Check for duplicate class
	for (UMCTExporterBase* Existing : RegisteredExporters)
	{
		if (Existing && Existing->GetClass() == Exporter->GetClass())
		{
			return; // Already have an exporter of this type
		}
	}

	RegisteredExporters.Add(Exporter);
	SortExportersByPriority();

	UE_LOG(LogTemp, Log, TEXT("MCTExporterRegistry: Registered exporter instance %s (Priority: %d)"),
		*Exporter->GetExporterDisplayName(), Exporter->GetPriority());
}

UMCTExporterBase* UMCTExporterRegistry::FindExporterForAsset(UObject* Asset) const
{
	if (!Asset)
	{
		return nullptr;
	}

	// Iterate through sorted exporters (highest priority first)
	for (UMCTExporterBase* Exporter : RegisteredExporters)
	{
		if (Exporter && Exporter->CanExport(Asset))
		{
			return Exporter;
		}
	}

	return nullptr;
}

bool UMCTExporterRegistry::IsAssetSupported(UObject* Asset) const
{
	return FindExporterForAsset(Asset) != nullptr;
}

TArray<UClass*> UMCTExporterRegistry::GetAllSupportedClasses() const
{
	TArray<UClass*> AllClasses;

	for (UMCTExporterBase* Exporter : RegisteredExporters)
	{
		if (Exporter)
		{
			TArray<UClass*> ExporterClasses = Exporter->GetSupportedClasses();
			for (UClass* SupportedClass : ExporterClasses)
			{
				AllClasses.AddUnique(SupportedClass);
			}
		}
	}

	return AllClasses;
}

void UMCTExporterRegistry::ClearExporters()
{
	RegisteredExporters.Empty();
	bDefaultExportersRegistered = false;
}

void UMCTExporterRegistry::RegisterDefaultExporters()
{
	if (bDefaultExportersRegistered)
	{
		return;
	}

	bDefaultExportersRegistered = true;

	UE_LOG(LogTemp, Log, TEXT("MCTExporterRegistry: Registering default exporters..."));

	// Register exporters in any order - they will be sorted by priority
	// Higher priority exporters are checked first

	// Widget Blueprint (priority 100 - most specific, checked before Blueprint)
	RegisterExporter(UMCTWidgetBlueprintExporter::StaticClass());

	// AnimBlueprint (priority 90 - between Widget:100 and Blueprint:50)
	RegisterExporter(UMCTAnimBlueprintExporter::StaticClass());

	// Blueprint (priority 50 - base blueprint)
	RegisterExporter(UMCTBlueprintExporter::StaticClass());

	// World/Map (priority 50) - NEW!
	RegisterExporter(UMCTWorldExporter::StaticClass());

	// Data Asset (priority 40)
	RegisterExporter(UMCTDataAssetExporter::StaticClass());

	// DataTable (priority 50)
	RegisterExporter(UMCTDataTableExporter::StaticClass());

	// Input (priority 50)
	RegisterExporter(UMCTInputExporter::StaticClass());

	// Audio (priority 50)
	RegisterExporter(UMCTAudioExporter::StaticClass());

	// Texture (priority 50)
	RegisterExporter(UMCTTextureExporter::StaticClass());

	// Physical Material (priority 46 - above Material:45, checked before Material)
	RegisterExporter(UMCTPhysicalMaterialExporter::StaticClass());

	// Material (priority 45 - between DataAsset:40 and standard:50)
	RegisterExporter(UMCTMaterialExporter::StaticClass());

	UE_LOG(LogTemp, Log, TEXT("MCTExporterRegistry: Registered %d default exporters"), RegisteredExporters.Num());
}

void UMCTExporterRegistry::SortExportersByPriority()
{
	// Sort by priority, highest first
	RegisteredExporters.Sort([](const UMCTExporterBase& A, const UMCTExporterBase& B)
	{
		return A.GetPriority() > B.GetPriority();
	});
}
