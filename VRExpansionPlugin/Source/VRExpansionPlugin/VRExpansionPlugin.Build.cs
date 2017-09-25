// Some copyright should be here...
using System.IO;
using UnrealBuildTool;

public class VRExpansionPlugin : ModuleRules
{
    private string PluginsPath
    {
        get { return Path.GetFullPath(Target.RelativeEnginePath) + "Plugins/Runtime/"; }
    }

    public VRExpansionPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        //bEnforceIWYU = true;

        Definitions.Add("WITH_VR_EXPANSION=1");

        // To detect VR Preview, not built out in packaged builds
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "UnrealEd"
                }
            );
        }


        PublicIncludePaths.AddRange(
			new string[] {
				"VRExpansionPlugin/Public",
                "VRExpansionPlugin/Public/SimpleChar",
                "HeadMountedDisplay/Public",
                "Runtime/Engine/Private/PhysicsEngine"
				
				// ... add public include paths required here ...
			}
			);

        PrivateIncludePaths.AddRange(
			new string[] {
				"VRExpansionPlugin/Private",
                "VRExpansionPlugin/Private/SimpleChar",
				// ... add other private include paths required here ...
			}
			);

        PublicDependencyModuleNames.AddRange(
        new string[]
        {
                    "Core",
                    "CoreUObject",
                    "Engine",
                   // "InputCore",
                    "PhysX",
                    //"FLEX", remove comment if building in the NVIDIA flex branch - NOTE when put in place FLEX only listed win32 and win64 at compatible platforms
                    "HeadMountedDisplay",
                   // "RHI",
                    //"RenderCore",
                    //"ShaderCore",
                    //"NetworkReplayStreaming",
                    //"AIModule",
                    "UMG",

                    //"Renderer",
                    //"UtilityShaders"
        });


        PrivateDependencyModuleNames.AddRange(
            new string[] 
            {
               // "Core",
               // "CoreUObject",
                //"Engine",
                "InputCore",
               // "PhysX",
                //"FLEX", remove comment if building in the NVIDIA flex branch - NOTE when put in place FLEX only listed win32 and win64 at compatible platforms
                //"HeadMountedDisplay",
                "RHI",
                "RenderCore",
                "ShaderCore",
                "NetworkReplayStreaming",
                "AIModule",
                "UMG",
                "GameplayTags",
                "PhysXVehicles"

                //"Renderer",
               // "UtilityShaders"
            });



        PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				//"CoreUObject",
				//"Engine",
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
    }
}
