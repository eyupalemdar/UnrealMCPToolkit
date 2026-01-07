// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/AIExporterBase.h"
#include "AIAudioExporter.generated.h"

class USoundClass;
class USoundSubmix;
class USoundConcurrency;
class USoundAttenuation;
class USoundControlBus;
class USoundControlBusMix;
class USoundModulationPatch;

/**
 * Exporter for Audio Foundation and Modulation assets.
 *
 * Handles:
 * - USoundClass
 * - USoundSubmix
 * - USoundConcurrency
 * - USoundAttenuation
 * - USoundControlBus
 * - USoundControlBusMix
 * - USoundModulationPatch
 *
 * Priority: 50 (standard)
 */
UCLASS()
class COMMONAIEXPORT_API UAIAudioExporter : public UAIExporterBase
{
	GENERATED_BODY()

public:
	//~ Begin UAIExporterBase Interface
	virtual bool CanExport(UObject* Asset) const override;
	virtual TArray<UClass*> GetSupportedClasses() const override;
	virtual FString Export(UObject* Asset, bool bFilterDefaults = false) override;
	virtual int32 GetPriority() const override { return 50; }
	virtual FString GetExporterDisplayName() const override { return TEXT("AudioExporter"); }
	//~ End UAIExporterBase Interface

protected:
	// Audio Foundation
	FString ExportSoundClass(USoundClass* SoundClass, bool bFilterDefaults);
	FString ExportSoundSubmix(USoundSubmix* Submix, bool bFilterDefaults);
	FString ExportSoundConcurrency(USoundConcurrency* Concurrency, bool bFilterDefaults);
	FString ExportSoundAttenuation(USoundAttenuation* Attenuation, bool bFilterDefaults);

	// Audio Modulation
	FString ExportSoundControlBus(USoundControlBus* Bus, bool bFilterDefaults);
	FString ExportSoundControlBusMix(USoundControlBusMix* Mix, bool bFilterDefaults);
	FString ExportSoundModulationPatch(USoundModulationPatch* Patch, bool bFilterDefaults);
};
