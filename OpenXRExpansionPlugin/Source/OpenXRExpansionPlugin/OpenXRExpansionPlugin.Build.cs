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
					//"LiveLink",
					//"LiveLinkInterface",
					"OpenXRHMD"
                }
				);

            AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");

           // if (Target.bBuildEditor == true)
           // {
               // PrivateDependencyModuleNames.Add("UnrealEd");
           // }
        }
    }
}
