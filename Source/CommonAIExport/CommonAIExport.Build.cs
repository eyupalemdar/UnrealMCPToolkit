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
			"JsonUtilities",
			"HTTPServer"
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
			"AIModule",
			"NavigationSystem",
			"GameplayAbilities",
			"GameplayTags",
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
			"CommonInput",  // For CommonUI runtime input data during widget preview capture
			"ImageWrapper",  // For PNG export (IImageWrapper)
			"RenderCore",  // For FlushRenderingCommands (widget preview capture)
			"Landscape",  // For landscape proxy/component diagnostics
			"Foliage",  // For foliage actor/type diagnostics
			"LevelSequence",  // For LevelSequence/Sequencer asset inspection
			"MovieScene",  // For UWidgetAnimation export
			"MovieSceneTracks",  // For section types (FloatSection, ColorSection, etc.)
			"PhysicsCore",  // For UPhysicalMaterial
			"KismetCompiler",  // For FKismetEditorUtilities::CompileBlueprint
			"MaterialEditor",  // For UMaterialEditingLibrary (Material Builder)
			"AssetTools"  // For IAssetTools::RenameAssets + FAssetRenameData (rename_asset)
		});
	}
}
