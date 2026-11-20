// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

// 编辑器目标，编译后让 UE 编辑器加载 GameJam 类型、蓝图父类与开发测试。
public class GameJamEditorTarget : TargetRules
{
	public GameJamEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("GameJam");
	}
}
