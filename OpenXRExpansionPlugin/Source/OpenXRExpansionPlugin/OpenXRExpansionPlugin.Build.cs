// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
    public class OpenXRExpansionPlugin: ModuleRules
    {
        public OpenXRExpansionPlugin(ReadOnlyTargetRules Target) 
				: base(Target)
        {
            DefaultBuildSettings = BuildSettingsVersion.Latest;
            IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

            SetupIrisSupport(Target);

            PublicDependencyModuleNames.AddRange(
			   new string[]
			   {
					//"InputDevice",
					//"LiveLink",
					//"LiveLinkInterface"
			   }
		   );

            var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
            PrivateIncludePaths.AddRange(
                new string[] {
                    EngineDir + "Plugins/Runtime/OpenXR/Source/OpenXRHMD/Private",
                    EngineDir + "/Source/ThirdParty/OpenXR/include",
					// ... add other private include paths required here ...
				}
                );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "NetCore",
                    "CoreUObject",
					//"ApplicationCore",
                    "Engine",
                    //"InputDevice",
                    "InputCore",
					"Slate",
					"HeadMountedDisplay",
                    //"AnimGraph",
                    "AnimGraphRuntime",
                    "SlateCore",
                    "XRBase"
					//"LiveLink",
					//"LiveLinkInterface",
                }
				);
                
            if (Target.Platform != UnrealTargetPlatform.Mac && Target.Platform != UnrealTargetPlatform.IOS)
            {
                PrivateDependencyModuleNames.AddRange(
                    new string[]
                    {
                        "OpenXRHMD"
                    }
                );
                PrivateDefinitions.AddRange(new string[] { "OPENXR_SUPPORTED" });
                AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");
            }

           // if (Target.bBuildEditor == true)
           // {
               // PrivateDependencyModuleNames.Add("UnrealEd");
           // }
        }
    }
}
