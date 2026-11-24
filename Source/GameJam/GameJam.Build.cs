// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

// 游戏模块的构建依赖，UnrealBuildTool 据此生成编译和链接配置。
public class GameJam : ModuleRules
{
	public GameJam(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		// 公开头文件使用的基础类型、Actor、资源与输入系统。
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AudioExtensions" });

		// 菜单与 HUD 在 GridPuzzleScreen.cpp 中通过 Slate 构建。
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "AudioMixer" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

