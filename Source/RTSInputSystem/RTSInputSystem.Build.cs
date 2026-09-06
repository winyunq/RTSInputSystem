// Copyright 2024 Jesus Bracho All Rights Reserved.

using UnrealBuildTool;

public class RTSInputSystem : ModuleRules
{
	public RTSInputSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);

		PrivateIncludePaths.AddRange(
			new string[]
			{
			}
		);

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"AIModule",
				"EnhancedInput",
				"GameplayTags",
				"InputCore",
				"MassAPI",
				"MassBattle",
				"MassEntity",
				"Niagara",
				"SlateCore",
				"UMG"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"FlowFieldCanvas",
				"Slate",
				"ProceduralMeshComponent",
				"Projects",
				"RenderCore",
				"RHI"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
