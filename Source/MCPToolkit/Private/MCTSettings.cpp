// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

#include "MCTSettings.h"
#include "Misc/Paths.h"

UMCTSettings::UMCTSettings()
{
	OutputDirectory.Path = TEXT("Dev/AIExports");
}

FString UMCTSettings::GetOutputDirectoryAbsolute() const
{
	if (OutputDirectory.Path.IsEmpty())
	{
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("Dev"), TEXT("AIExports"));
	}

	if (FPaths::IsRelative(OutputDirectory.Path))
	{
		return FPaths::Combine(FPaths::ProjectDir(), OutputDirectory.Path);
	}

	return OutputDirectory.Path;
}
