// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PatternBreaker : ModuleRules
{
	public PatternBreaker(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "HTTP", "Json" });

		PublicIncludePaths.AddRange(new string[] {
			"PatternBreaker",
			"PatternBreaker/Variant_Platforming",
			"PatternBreaker/Variant_Platforming/Animation",
			"PatternBreaker/Variant_Combat",
			"PatternBreaker/Variant_Combat/AI",
			"PatternBreaker/Variant_Combat/Animation",
			"PatternBreaker/Variant_Combat/Gameplay",
			"PatternBreaker/Variant_Combat/Interfaces",
			"PatternBreaker/Variant_Combat/UI",
			"PatternBreaker/Variant_SideScrolling",
			"PatternBreaker/Variant_SideScrolling/AI",
			"PatternBreaker/Variant_SideScrolling/Gameplay",
			"PatternBreaker/Variant_SideScrolling/Interfaces",
			"PatternBreaker/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
