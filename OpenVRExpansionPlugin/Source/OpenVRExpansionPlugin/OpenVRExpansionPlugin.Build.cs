// Some copyright should be here...
using System.IO;
using UnrealBuildTool;

public class OpenVRExpansionPlugin : ModuleRules
{
    private string PluginsPath
    {
        get { return Path.GetFullPath(BuildConfiguration.RelativeEnginePath) + "Plugins/Runtime/"; }
    }

    public OpenVRExpansionPlugin(TargetInfo Target)
	{
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        //bEnforceIWYU = true;

        Definitions.Add("WITH_OPEN_VR_EXPANSION=1");

        PublicIncludePaths.AddRange(
			new string[] {
				"OpenVRExpansionPlugin/Public",
                "HeadMountedDisplay/Public",
                "Runtime/Engine/Classes/Kismet",
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
                "ShaderCore",
                "ProceduralMeshComponent",
                "VRExpansionPlugin"
               // "EngineSettings"
            });

       /* PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				//"CoreUObject",
				//"Engine",

				// ... add private dependencies that you statically link with here ...	
			}
			);
            */
        // Locking steamVR out of non windows builds
        if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
        {

            PublicIncludePaths.AddRange(
            new string[] {
                        "../Plugins/Runtime/Steam/SteamVR/Source/SteamVR/Private", // This is dumb but it isn't very open

                // ... add public include paths required here ...
            }
            );

            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                "SteamVR",
                "OpenVR",
                "SteamVRController"
                });
                
            // AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenVR");
            //  PrivateDependencyModuleNames.AddRange(new string[] { "D3D11RHI" });     //@todo steamvr: multiplatform
        }

    }
}
