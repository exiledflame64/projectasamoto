// Copyright Asamoto. Realm primary game module.

using UnrealBuildTool;

public class Realm : ModuleRules
{
	public Realm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Allow folder-rooted includes like "Sim/SimWorld.h" and "Core/SimSubsystem.h"
		// (this module uses a flat Sim/Render/Save/Core layout, not Public/Private).
		PublicIncludePaths.Add(ModuleDirectory);

		// Lightweight engine deps only. The Sim layer stays free of render/UObject
		// dependencies; these modules are for the Core subsystem + Render proxies.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
