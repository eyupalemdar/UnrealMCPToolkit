// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTDataAssetExporter.h"
#include "Engine/DataAsset.h"

bool UMCTDataAssetExporter::CanExport(UObject* Asset) const
{
	return Asset && Asset->IsA<UDataAsset>();
}

TArray<UClass*> UMCTDataAssetExporter::GetSupportedClasses() const
{
	return { UDataAsset::StaticClass() };
}

FString UMCTDataAssetExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	UDataAsset* DataAsset = Cast<UDataAsset>(Asset);
	if (!DataAsset)
	{
		return TEXT("Error: Not a Data Asset\n");
	}

	return ExportDataAsset(DataAsset, bFilterDefaults);
}

FString UMCTDataAssetExporter::ExportDataAsset(UDataAsset* DataAsset, bool bFilterDefaults)
{
	FString Output;

	// Header
	Output += MakeSectionHeader(FString::Printf(TEXT("DATA ASSET: %s"), *DataAsset->GetName()));

	// Class info
	Output += FString::Printf(TEXT("Class: %s\n"), *DataAsset->GetClass()->GetName());

	// Get parent class chain for context
	UClass* Class = DataAsset->GetClass();
	if (Class && Class->GetSuperClass() && Class->GetSuperClass() != UDataAsset::StaticClass())
	{
		FString ParentChain;
		UClass* Parent = Class->GetSuperClass();
		while (Parent && Parent != UDataAsset::StaticClass())
		{
			if (!ParentChain.IsEmpty())
			{
				ParentChain += TEXT(" -> ");
			}
			ParentChain += Parent->GetName();
			Parent = Parent->GetSuperClass();
		}
		if (!ParentChain.IsEmpty())
		{
			Output += FString::Printf(TEXT("Inheritance: %s\n"), *ParentChain);
		}
	}

	Output += TEXT("\n");

	// Properties - use deep export to capture embedded subobjects (like GameFeatureActions)
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectPropertiesDeep(DataAsset, TEXT(""), 0, bFilterDefaults);

	return Output;
}
