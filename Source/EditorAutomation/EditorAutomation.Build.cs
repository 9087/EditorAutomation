// Copyright (c) 2026 Wu Zhiwei. All Rights Reserved.

using UnrealBuildTool;

public class EditorAutomation : ModuleRules
{
	public EditorAutomation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"InputCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetRegistry",
			"BlueprintGraph",
			"Kismet",
			"LevelEditor",
			"UMGEditor",
			"Json",
			"JsonUtilities",
			"Slate",
			"SlateCore",
			"InputCore",
			"ApplicationCore",
		});
	}
}
