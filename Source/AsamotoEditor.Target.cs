// Copyright Asamoto. Editor target.

using UnrealBuildTool;
using System.Collections.Generic;

public class AsamotoEditorTarget : TargetRules
{
	public AsamotoEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Realm");
	}
}
