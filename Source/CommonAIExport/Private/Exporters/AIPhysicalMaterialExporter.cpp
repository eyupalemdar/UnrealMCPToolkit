// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/AIPhysicalMaterialExporter.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

bool UAIPhysicalMaterialExporter::CanExport(UObject* Asset) const
{
	return Asset && Asset->IsA<UPhysicalMaterial>();
}

TArray<UClass*> UAIPhysicalMaterialExporter::GetSupportedClasses() const
{
	return { UPhysicalMaterial::StaticClass() };
}

FString UAIPhysicalMaterialExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	UPhysicalMaterial* PhysMat = Cast<UPhysicalMaterial>(Asset);
	if (!PhysMat)
	{
		return TEXT("Error: Not a Physical Material\n");
	}
	return ExportPhysicalMaterial(PhysMat, bFilterDefaults);
}

FString UAIPhysicalMaterialExporter::ExportPhysicalMaterial(UPhysicalMaterial* PhysMat, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("PHYSICAL MATERIAL: %s"), *PhysMat->GetName()));
	Output += FString::Printf(TEXT("Class: %s\n"), *PhysMat->GetClass()->GetName());
	Output += TEXT("\n");

	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(PhysMat, 0, bFilterDefaults);

	return Output;
}
