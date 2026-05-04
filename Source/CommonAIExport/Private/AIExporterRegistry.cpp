// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "AIExporterRegistry.h"
#include "Exporters/AIExporterBase.h"

// All exporter headers
#include "Exporters/AIBlueprintExporter.h"
#include "Exporters/AIAnimBlueprintExporter.h"
#include "Exporters/AIWidgetBlueprintExporter.h"
#include "Exporters/AIDataAssetExporter.h"
#include "Exporters/AIDataTableExporter.h"
#include "Exporters/AIInputExporter.h"
#include "Exporters/AIAudioExporter.h"
#include "Exporters/AIWorldExporter.h"
#include "Exporters/AITextureExporter.h"
#include "Exporters/AIMaterialExporter.h"
#include "Exporters/AIPhysicalMaterialExporter.h"

UAIExporterRegistry* UAIExporterRegistry::Instance = nullptr;

UAIExporterRegistry* UAIExporterRegistry::Get()
{
	if (!Instance)
	{
		// Create the singleton instance
		Instance = NewObject<UAIExporterRegistry>();
		Instance->AddToRoot(); // Prevent garbage collection

		// Register default exporters
		Instance->RegisterDefaultExporters();
	}

	return Instance;
}

void UAIExporterRegistry::RegisterExporter(TSubclassOf<UAIExporterBase> ExporterClass)
{
	if (!ExporterClass)
	{
		return;
	}

	// Check if already registered
	for (UAIExporterBase* Existing : RegisteredExporters)
	{
		if (Existing && Existing->GetClass() == ExporterClass)
		{
			return; // Already registered
		}
	}

	// Create instance
	UAIExporterBase* Exporter = NewObject<UAIExporterBase>(this, ExporterClass);
	if (Exporter)
	{
		RegisteredExporters.Add(Exporter);
		SortExportersByPriority();

		UE_LOG(LogTemp, Log, TEXT("AIExporterRegistry: Registered exporter %s (Priority: %d)"),
			*Exporter->GetExporterDisplayName(), Exporter->GetPriority());
	}
}

void UAIExporterRegistry::RegisterExporter(UAIExporterBase* Exporter)
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
	for (UAIExporterBase* Existing : RegisteredExporters)
	{
		if (Existing && Existing->GetClass() == Exporter->GetClass())
		{
			return; // Already have an exporter of this type
		}
	}

	RegisteredExporters.Add(Exporter);
	SortExportersByPriority();

	UE_LOG(LogTemp, Log, TEXT("AIExporterRegistry: Registered exporter instance %s (Priority: %d)"),
		*Exporter->GetExporterDisplayName(), Exporter->GetPriority());
}

UAIExporterBase* UAIExporterRegistry::FindExporterForAsset(UObject* Asset) const
{
	if (!Asset)
	{
		return nullptr;
	}

	// Iterate through sorted exporters (highest priority first)
	for (UAIExporterBase* Exporter : RegisteredExporters)
	{
		if (Exporter && Exporter->CanExport(Asset))
		{
			return Exporter;
		}
	}

	return nullptr;
}

bool UAIExporterRegistry::IsAssetSupported(UObject* Asset) const
{
	return FindExporterForAsset(Asset) != nullptr;
}

TArray<UClass*> UAIExporterRegistry::GetAllSupportedClasses() const
{
	TArray<UClass*> AllClasses;

	for (UAIExporterBase* Exporter : RegisteredExporters)
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

void UAIExporterRegistry::ClearExporters()
{
	RegisteredExporters.Empty();
	bDefaultExportersRegistered = false;
}

void UAIExporterRegistry::RegisterDefaultExporters()
{
	if (bDefaultExportersRegistered)
	{
		return;
	}

	bDefaultExportersRegistered = true;

	UE_LOG(LogTemp, Log, TEXT("AIExporterRegistry: Registering default exporters..."));

	// Register exporters in any order - they will be sorted by priority
	// Higher priority exporters are checked first

	// Widget Blueprint (priority 100 - most specific, checked before Blueprint)
	RegisterExporter(UAIWidgetBlueprintExporter::StaticClass());

	// AnimBlueprint (priority 90 - between Widget:100 and Blueprint:50)
	RegisterExporter(UAIAnimBlueprintExporter::StaticClass());

	// Blueprint (priority 50 - base blueprint)
	RegisterExporter(UAIBlueprintExporter::StaticClass());

	// World/Map (priority 50) - NEW!
	RegisterExporter(UAIWorldExporter::StaticClass());

	// Data Asset (priority 40)
	RegisterExporter(UAIDataAssetExporter::StaticClass());

	// DataTable (priority 50)
	RegisterExporter(UAIDataTableExporter::StaticClass());

	// Input (priority 50)
	RegisterExporter(UAIInputExporter::StaticClass());

	// Audio (priority 50)
	RegisterExporter(UAIAudioExporter::StaticClass());

	// Texture (priority 50)
	RegisterExporter(UAITextureExporter::StaticClass());

	// Physical Material (priority 46 - above Material:45, checked before Material)
	RegisterExporter(UAIPhysicalMaterialExporter::StaticClass());

	// Material (priority 45 - between DataAsset:40 and standard:50)
	RegisterExporter(UAIMaterialExporter::StaticClass());

	UE_LOG(LogTemp, Log, TEXT("AIExporterRegistry: Registered %d default exporters"), RegisteredExporters.Num());
}

void UAIExporterRegistry::SortExportersByPriority()
{
	// Sort by priority, highest first
	RegisteredExporters.Sort([](const UAIExporterBase& A, const UAIExporterBase& B)
	{
		return A.GetPriority() > B.GetPriority();
	});
}
