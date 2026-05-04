// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "Exporters/MCTAudioExporter.h"

// Audio Foundation
#include "Sound/SoundClass.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundAttenuation.h"

// Audio Modulation
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "SoundModulationPatch.h"

bool UMCTAudioExporter::CanExport(UObject* Asset) const
{
	if (!Asset)
	{
		return false;
	}

	return Asset->IsA<USoundClass>() ||
	       Asset->IsA<USoundSubmix>() ||
	       Asset->IsA<USoundConcurrency>() ||
	       Asset->IsA<USoundAttenuation>() ||
	       Asset->IsA<USoundControlBus>() ||
	       Asset->IsA<USoundControlBusMix>() ||
	       Asset->IsA<USoundModulationPatch>();
}

TArray<UClass*> UMCTAudioExporter::GetSupportedClasses() const
{
	return {
		USoundClass::StaticClass(),
		USoundSubmix::StaticClass(),
		USoundConcurrency::StaticClass(),
		USoundAttenuation::StaticClass(),
		USoundControlBus::StaticClass(),
		USoundControlBusMix::StaticClass(),
		USoundModulationPatch::StaticClass()
	};
}

FString UMCTAudioExporter::Export(UObject* Asset, bool bFilterDefaults)
{
	// Audio Foundation
	if (USoundClass* SoundClass = Cast<USoundClass>(Asset))
	{
		return ExportSoundClass(SoundClass, bFilterDefaults);
	}
	else if (USoundSubmix* Submix = Cast<USoundSubmix>(Asset))
	{
		return ExportSoundSubmix(Submix, bFilterDefaults);
	}
	else if (USoundConcurrency* Concurrency = Cast<USoundConcurrency>(Asset))
	{
		return ExportSoundConcurrency(Concurrency, bFilterDefaults);
	}
	else if (USoundAttenuation* Attenuation = Cast<USoundAttenuation>(Asset))
	{
		return ExportSoundAttenuation(Attenuation, bFilterDefaults);
	}
	// Audio Modulation
	else if (USoundControlBus* Bus = Cast<USoundControlBus>(Asset))
	{
		return ExportSoundControlBus(Bus, bFilterDefaults);
	}
	else if (USoundControlBusMix* Mix = Cast<USoundControlBusMix>(Asset))
	{
		return ExportSoundControlBusMix(Mix, bFilterDefaults);
	}
	else if (USoundModulationPatch* Patch = Cast<USoundModulationPatch>(Asset))
	{
		return ExportSoundModulationPatch(Patch, bFilterDefaults);
	}

	return TEXT("Error: Unsupported audio asset type\n");
}

//////////////////////////////////////////////////////////////////////////
// Audio Foundation

FString UMCTAudioExporter::ExportSoundClass(USoundClass* SoundClass, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("SOUND CLASS: %s"), *SoundClass->GetName()));

	// Hierarchy - Parent
	Output += FString::Printf(TEXT("ParentClass: %s\n"),
		SoundClass->ParentClass ? *SoundClass->ParentClass->GetPathName() : TEXT("None"));

	// Child Classes
	Output += FString::Printf(TEXT("ChildClassCount: %d\n"), SoundClass->ChildClasses.Num());
	for (const TObjectPtr<USoundClass>& Child : SoundClass->ChildClasses)
	{
		if (Child)
		{
			Output += FString::Printf(TEXT("  Child: %s\n"), *Child->GetName());
		}
	}

	// Key properties
	Output += TEXT("\n");
	Output += MakeSubsectionHeader(TEXT("Key Settings"));
	Output += FString::Printf(TEXT("Volume: %.2f\n"), SoundClass->Properties.Volume);
	Output += FString::Printf(TEXT("Pitch: %.2f\n"), SoundClass->Properties.Pitch);
	Output += FString::Printf(TEXT("bAlwaysPlay: %s\n"), SoundClass->Properties.bAlwaysPlay ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bReverb: %s\n"), SoundClass->Properties.bReverb ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bApplyAmbientVolumes: %s\n"), SoundClass->Properties.bApplyAmbientVolumes ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("DefaultSubmix: %s\n"),
		SoundClass->Properties.DefaultSubmix ? *SoundClass->Properties.DefaultSubmix->GetPathName() : TEXT("None"));

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(SoundClass, 0, bFilterDefaults);

	return Output;
}

FString UMCTAudioExporter::ExportSoundSubmix(USoundSubmix* Submix, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("SOUND SUBMIX: %s"), *Submix->GetName()));

	// Hierarchy
	Output += FString::Printf(TEXT("ParentSubmix: %s\n"),
		Submix->ParentSubmix ? *Submix->ParentSubmix->GetPathName() : TEXT("None"));

	Output += FString::Printf(TEXT("ChildSubmixCount: %d\n"), Submix->ChildSubmixes.Num());
	for (const TObjectPtr<USoundSubmixBase>& Child : Submix->ChildSubmixes)
	{
		if (Child)
		{
			Output += FString::Printf(TEXT("  Child: %s\n"), *Child->GetName());
		}
	}

	// Operational settings
	Output += TEXT("\n");
	Output += MakeSubsectionHeader(TEXT("Operational Settings"));
	Output += FString::Printf(TEXT("bMuteWhenBackgrounded: %s\n"), Submix->bMuteWhenBackgrounded ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("bAutoDisable: %s\n"), Submix->bAutoDisable ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("AutoDisableTime: %.3f\n"), Submix->AutoDisableTime);
	Output += FString::Printf(TEXT("bSendToAudioLink: %s\n"), Submix->bSendToAudioLink ? TEXT("True") : TEXT("False"));

	// Volume modulation
	Output += FString::Printf(TEXT("OutputVolumeModulation.Value: %.2f dB\n"), Submix->OutputVolumeModulation.Value);
	Output += FString::Printf(TEXT("WetLevelModulation.Value: %.2f dB\n"), Submix->WetLevelModulation.Value);
	Output += FString::Printf(TEXT("DryLevelModulation.Value: %.2f dB\n"), Submix->DryLevelModulation.Value);

	// Envelope follower
	Output += FString::Printf(TEXT("EnvelopeFollowerAttackTime: %d ms\n"), Submix->EnvelopeFollowerAttackTime);
	Output += FString::Printf(TEXT("EnvelopeFollowerReleaseTime: %d ms\n"), Submix->EnvelopeFollowerReleaseTime);

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(Submix, 0, bFilterDefaults);

	return Output;
}

FString UMCTAudioExporter::ExportSoundConcurrency(USoundConcurrency* Concurrency, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("SOUND CONCURRENCY: %s"), *Concurrency->GetName()));

	const FSoundConcurrencySettings& Settings = Concurrency->Concurrency;
	Output += FString::Printf(TEXT("MaxCount: %d\n"), Settings.MaxCount);
	Output += FString::Printf(TEXT("bLimitToOwner: %s\n"), Settings.bLimitToOwner ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("ResolutionRule: %s\n"), *UEnum::GetValueAsString(Settings.ResolutionRule));

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(Concurrency, 0, bFilterDefaults);

	return Output;
}

FString UMCTAudioExporter::ExportSoundAttenuation(USoundAttenuation* Attenuation, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("SOUND ATTENUATION: %s"), *Attenuation->GetName()));

	const FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Output += FString::Printf(TEXT("bAttenuate: %s\n"), Settings.bAttenuate ? TEXT("True") : TEXT("False"));
	Output += FString::Printf(TEXT("DistanceAlgorithm: %s\n"), *UEnum::GetValueAsString(Settings.DistanceAlgorithm));
	Output += FString::Printf(TEXT("FalloffDistance: %.2f\n"), Settings.FalloffDistance);

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(Attenuation, 0, bFilterDefaults);

	return Output;
}

//////////////////////////////////////////////////////////////////////////
// Audio Modulation

FString UMCTAudioExporter::ExportSoundControlBus(USoundControlBus* Bus, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("SOUND CONTROL BUS: %s"), *Bus->GetName()));

	Output += FString::Printf(TEXT("Address: %s\n"), *Bus->Address);
	Output += FString::Printf(TEXT("bBypass: %s\n"), Bus->bBypass ? TEXT("True") : TEXT("False"));

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(Bus, 0, bFilterDefaults);

	return Output;
}

FString UMCTAudioExporter::ExportSoundControlBusMix(USoundControlBusMix* Mix, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("SOUND CONTROL BUS MIX: %s"), *Mix->GetName()));

	Output += FString::Printf(TEXT("StageCount: %d\n"), Mix->MixStages.Num());

	for (int32 i = 0; i < Mix->MixStages.Num(); ++i)
	{
		const FSoundControlBusMixStage& Stage = Mix->MixStages[i];
		Output += FString::Printf(TEXT("  Stage[%d].Bus: %s\n"), i,
			Stage.Bus ? *Stage.Bus->GetPathName() : TEXT("None"));
		Output += FString::Printf(TEXT("  Stage[%d].Value.TargetValue: %.2f\n"), i, Stage.Value.TargetValue);
	}

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(Mix, 0, bFilterDefaults);

	return Output;
}

FString UMCTAudioExporter::ExportSoundModulationPatch(USoundModulationPatch* Patch, bool bFilterDefaults)
{
	FString Output;

	Output += MakeSectionHeader(FString::Printf(TEXT("SOUND MODULATION PATCH: %s"), *Patch->GetName()));

	const FSoundControlModulationPatch& Settings = Patch->PatchSettings;
	Output += FString::Printf(TEXT("bBypass: %s\n"), Settings.bBypass ? TEXT("True") : TEXT("False"));

	// Output parameter
	if (USoundModulationParameter* OutParam = Settings.OutputParameter)
	{
		Output += FString::Printf(TEXT("OutputParameter: %s\n"), *OutParam->GetPathName());
	}
	else
	{
		Output += TEXT("OutputParameter: None\n");
	}

	// Input buses
	Output += FString::Printf(TEXT("InputCount: %d\n"), Settings.Inputs.Num());
	for (int32 i = 0; i < Settings.Inputs.Num(); ++i)
	{
		const FSoundControlModulationInput& Input = Settings.Inputs[i];
		Output += FString::Printf(TEXT("  Input[%d].Bus: %s\n"), i,
			Input.Bus ? *Input.Bus->GetPathName() : TEXT("None"));
		Output += FString::Printf(TEXT("  Input[%d].bSampleAndHold: %s\n"), i,
			Input.bSampleAndHold ? TEXT("True") : TEXT("False"));
		Output += FString::Printf(TEXT("  Input[%d].Transform.Scalar: %.2f\n"), i, Input.Transform.Scalar);
	}

	// All properties
	Output += TEXT("\n");
	Output += MakeSectionHeader(TEXT("PROPERTIES"));
	Output += ExportObjectProperties(Patch, 0, bFilterDefaults);

	return Output;
}
