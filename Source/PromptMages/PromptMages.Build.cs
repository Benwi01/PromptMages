// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PromptMages : ModuleRules
{
	public PromptMages(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate",
			"SlateCore",
			"PhysicsCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"PromptMages",
			"PromptMages/Spellbook",
			"PromptMages/Mage",
			"PromptMages/Variant_Horror",
			"PromptMages/Variant_Horror/UI",
			"PromptMages/Variant_Shooter",
			"PromptMages/Variant_Shooter/AI",
			"PromptMages/Variant_Shooter/UI",
			"PromptMages/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
