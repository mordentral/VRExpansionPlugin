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

        PublicIncludePaths.AddRange(
			new string[] {
				"VRExpansionPlugin/Public",
                "HeadMountedDisplay/Public",
                "Runtime/Engine/Classes/Kismet",
                "Runtime/Engine/Private/PhysicsEngine"
				
				// ... add public include paths required here ...
			}
			);

        PrivateIncludePaths.AddRange(
			new string[] {
				"VRExpansionPlugin/Private",

				// ... add other private include paths required here ...
			}
			);

        PublicDependencyModuleNames.AddRange(
            new string[] 
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "PhysX",
                "HeadMountedDisplay",
                "RHI",
                "RenderCore",
                "ShaderCore",
                "NetworkReplayStreaming",
                "AIModule",
                "ProceduralMeshComponent"
            });

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



        // Don't load APEX on incompatible platforms
        if (
            Target.Platform != UnrealTargetPlatform.IOS &&
            Target.Platform != UnrealTargetPlatform.TVOS &&
            Target.Platform != UnrealTargetPlatform.Android &&
            Target.Platform != UnrealTargetPlatform.HTML5)
        {
            PublicDependencyModuleNames.AddRange(
            new string[]
            {                   
             "APEX"
            });
        }


        // Locking steamVR out of non windows builds
        if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
        {

            PublicIncludePaths.AddRange(
            new string[] {
                        "../Plugins/Runtime/Steam/SteamVR/Source/SteamVR/Private" // This is dumb but it isn't very open

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
