// Some copyright should be here...
using System.IO;
using UnrealBuildTool;

public class VRExpansionPlugin : ModuleRules
{
    private string PluginsPath
    {
        get { return Path.GetFullPath(BuildConfiguration.RelativeEnginePath) + "Plugins/Runtime/"; }
    }

    public VRExpansionPlugin(TargetInfo Target)
	{

       /* if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
        {
            AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenVR");
            //  PrivateDependencyModuleNames.AddRange(new string[] { "D3D11RHI" });     //@todo steamvr: multiplatform
        }*/

        PublicIncludePaths.AddRange(
			new string[] {
				"VRExpansionPlugin/Public",
                "HeadMountedDisplay/Public",
                "Runtime/Engine/Classes/Kismet",
                "Runtime/Engine/Private/PhysicsEngine",
                "../Plugins/Runtime/Steam/SteamVR/Source/SteamVR/Private" // This is dumb but it isn't very open
				
				// ... add public include paths required here ...
			}
			);

        PrivateIncludePaths.AddRange(
			new string[] {
				"VRExpansionPlugin/Private"

				// ... add other private include paths required here ...
			}
			);

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "PhysX", "APEX","HeadMountedDisplay", "SteamVR","OpenVR", "SteamVRController", "ProceduralMeshComponent" });

        PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore"

				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

    }
}
