***NOTICE - Branched off of original motion controller plugin***

UE4 Forums Thread
https://forums.unrealengine.com/showthread.php?116782-VR-(OpenVR)-Expansion-Plugin

Example Template Project
https://bitbucket.org/mordentral/vrexppluginexample

### Use Of This Plugin ###

This Plugin is intended to add additional functionality to Open/SteamVR/(All VR now) in UE4. 

### How do I install it? ###

* Clone Or Download Zip and extract this repository to a folder named "VRExpansionPlugin" in your "ProjectName/Plugins" directory, create this directory if it is missing.

* Add the VRExpansionPlugin to your projects PublicDependencyModuleNames in the projects build.cs if you have c++ code included.

* IF you do not have c++ code, use the Add New button in the editor and add a blank c++ class to your project.

* Open up the generated project .SLN file and build the project from the build menu.

You need to have visual studio installed and follow the UE4 setup guide for it: https://docs.unrealengine.com/latest/INT/Programming/Development/VisualStudioSetup/

I will offer pre-compiled binaries for all builds as well eventually when the plugin is stable enough for it, but until then installing it in the project instead of the engine is better.

### How do I use it? ###

[VR Character Actor Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VR%20Character%20Actor)


[VR Simple Character Actor Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VR%20Simple%20Character%20Actor)


[VR Character Movement Component Information Page](
https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VR%20Character%20Movement%20Component)


[VR Path Following Component Information Page](
https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VR%20Path%20Following%20Component)

[VR Player Controller Information Page](
https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VR%20Player%20Controller)

[Grip Motion Controller Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VRGrippableMotionController)

[FBPActorGripInformation Struct Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/FBPActorGripInformation%20Struct)

[VR Grip Interface Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VRGripInterface)

[Replicated VR Camera Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Replicated%20VR%20Camera)


[VR Root Component Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VRRootComponent)


[Parent Relative Attachment Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Parent%20Relative%20Attachment%20Component)


[VR Function Expansion Library Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VRExpansionFunctionLibrary)



### How do I VR? ###

Here is a list of VR quick tips  that I will begin to assemble


For Temporal AA I found the following two console commands to greatly increase clarity and reduce blur.

 * r.temporalAAsamples 1
 * r.temporalAAsharpness 1