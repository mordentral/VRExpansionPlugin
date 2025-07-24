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
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        //bEnforceIWYU = true;

        PublicDefinitions.Add("WITH_VR_EXPANSION=1");
        SetupIrisSupport(Target);

        // To detect VR Preview, not built out in packaged builds
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "UnrealEd"
                }
            );
        }

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                        "Settings"
            }
        );

        PublicIncludePaths.AddRange(
			new string[] {
				//"VRExpansionPlugin/Public",
                //"VRExpansionPlugin/Public/SimpleChar",
                //"HeadMountedDisplay/Public",
                //"Runtime/Engine/Private/PhysicsEngine"
				
				// ... add public include paths required here ...
			}
			);

        PrivateIncludePaths.AddRange(
			new string[] {
				//"VRExpansionPlugin/Private",
                //"VRExpansionPlugin/Private/SimpleChar",
				// ... add other private include paths required here ...
			}
			);

        PublicDependencyModuleNames.AddRange(
        new string[]
        {
                    "Core",
                    "NetCore",
                    "CoreUObject",
                    "Engine",
                    "PhysicsCore",
                    "HeadMountedDisplay",
                    "UMG",
                    "NavigationSystem",
                    "AIModule",
                    "AnimGraphRuntime",
                    "XRBase",
                    "GameplayTags",
                    "Mover"
        });

        //if(Target.bUseChaos)
       // {
            PublicDependencyModuleNames.Add("Chaos");
            PublicDependencyModuleNames.Add("ChaosVehicles");
        //}

        PrivateDependencyModuleNames.AddRange(
            new string[] 
            {
               // "Core",
               // "CoreUObject",
                //"Engine",
                "InputCore",
                "ImageCore", // For new stereo layer stuff
                //"FLEX", remove comment if building in the NVIDIA flex branch - NOTE when put in place FLEX only listed win32 and win64 at compatible platforms
                //"HeadMountedDisplay",
                "RHI",
				"ApplicationCore",
                "RenderCore",
               // "ShaderCore",
                "NetworkReplayStreaming"
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

        // Allow gameplay debugger on editor builds
        if (Target.bBuildDeveloperTools || (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test))
        {
            PrivateDependencyModuleNames.Add("GameplayDebugger");
            PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1"); // Already in AI Module, but gameplay abilities and other modules also duplicate the definition
        }
        else
        {
            PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
        }
    }
}
