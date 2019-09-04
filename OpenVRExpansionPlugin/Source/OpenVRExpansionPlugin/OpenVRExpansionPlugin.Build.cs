// Some copyright should be here...
using System.IO;
using UnrealBuildTool;

public class OpenVRExpansionPlugin : ModuleRules
{
    private string PluginsPath
    {
        get { return Path.GetFullPath(Target.RelativeEnginePath) + "Plugins/Runtime/"; }
    }

    public OpenVRExpansionPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        //bEnforceIWYU = true;

       PublicDefinitions.Add("WITH_OPEN_VR_EXPANSION=1");

		
        PublicIncludePaths.AddRange(
			new string[] {
				//"OpenVRExpansionPlugin/Public",
                //"HeadMountedDisplay/Public",
                "Runtime/Engine/Private/PhysicsEngine"
				
				// ... add public include paths required here ...
			}
			);
	

        PublicDependencyModuleNames.AddRange(
            new string[] 
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "HeadMountedDisplay",
                "RHI",
                "RenderCore",
               // "ShaderCore",
                "ProceduralMeshComponent",
               // "VRExpansionPlugin"
               // "EngineSettings"
            });


        if (
            Target.Platform == UnrealTargetPlatform.Win32 ||
            Target.Platform == UnrealTargetPlatform.Win64 ||
            Target.Platform == UnrealTargetPlatform.Mac ||
            (Target.Platform == UnrealTargetPlatform.Linux && Target.Architecture.StartsWith("x86_64"))
            )
            {
                PublicDependencyModuleNames.AddRange(
                new string[]
                {
			    "OpenVR"
                });
            }

    }
}
