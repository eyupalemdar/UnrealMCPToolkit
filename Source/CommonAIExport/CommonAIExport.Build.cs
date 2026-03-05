// Copyright (c) 2025 Alemdar Labs Ltd. All Rights Reserved.

using UnrealBuildTool;

public class CommonAIExport : ModuleRules
{
	public CommonAIExport(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Sockets",
			"Networking",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Slate",
			"SlateCore",
			"UMG",
			"UMGEditor",
			"Kismet",
			"BlueprintGraph",
			"EnhancedInput",
			"InputCore",  // For FKey
			"GameplayAbilities",
			"DeveloperSettings",
			"Projects",
			"ToolMenus",
			"ContentBrowser",
			"AssetRegistry",
			"ApplicationCore",
			"AnimGraph",
			"AnimGraphRuntime",
			"AudioModulation",  // For USoundControlBus, USoundControlBusMix, USoundModulationPatch
			"CommonUI",  // For UCommonTextBlock
			"ImageWrapper",  // For PNG export (IImageWrapper)
			"MovieScene",  // For UWidgetAnimation export
			"MovieSceneTracks",  // For section types (FloatSection, ColorSection, etc.)
			"PhysicsCore",  // For UPhysicalMaterial
			"KismetCompiler",  // For FKismetEditorUtilities::CompileBlueprint
			"MaterialEditor"  // For UMaterialEditingLibrary (Material Builder)
		});
	}
}
