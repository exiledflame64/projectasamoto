// Copyright Asamoto. Game target.

using UnrealBuildTool;
using System.Collections.Generic;

public class AsamotoTarget : TargetRules
{
	public AsamotoTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Realm");
	}
}
