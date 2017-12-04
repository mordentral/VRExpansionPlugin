UE4 Forums Thread
https://forums.unrealengine.com/development-discussion/vr-ar-development/89050-vr-openvr-expansion-plugin

Example Template Project
https://bitbucket.org/mordentral/vrexppluginexample

[Consider supporting me on patreon](https://www.patreon.com/mordentral)

***

### Use Of This Plugin ###

This Plugin is intended to add additional functionality to Open/SteamVR/(All VR now) in UE4. 

### How do I install it? ###

##Wiki Pages for Installation##
[Basic Installation Step-By-Step](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/How%20to%20add%20the%20plugin%20to%20a%20blueprint%20only%20project%20-%20Step%20by%20Step)

[Specific Commit Download Tutorial](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Retrieving%20specific%20commit%20version%20of%20the%20plugin%20or%20template)

[Pre-built binary useage](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Getting%20pre-built%20binary%20packages)

**Guides for migrating between different engine versions of the plugin:**

* [**Migration Guides**](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Migration%20Guides%20(between%20engine%20versions))

**Option 1:**

Go to forum thread and downloaded the pre-built binary version for the engine version you are using (not updated with every daily change, only weekly or with large patches).

Install it into your Engine/Plugins directoy or ProjectName/Plugins Directory.

**Option 2 (More up to date - preferred if possible):**

* Clone Or Download Zip and extract this repository to a folder named "VRExpansionPlugin" in your "ProjectName/Plugins" directory, create this directory if it is missing.

* Add the VRExpansionPlugin to your projects PublicDependencyModuleNames in the projects build.cs if you have c++ code included.

* IF you do not have c++ code, use the Add New button in the editor and add a blank c++ class to your project.

* Open up the generated project .SLN file and build the project from the build menu.

You need to have visual studio installed and follow the UE4 setup guide for it: https://docs.unrealengine.com/latest/INT/Programming/Development/VisualStudioSetup/

### How do I use it? ###

**KantanDocGen Automatic Documentation ([KantanDocGen](http://kantandev.com/free/kantan-doc-gen))** 

**[VRExpansionPlugin](https://mordentral.bitbucket.io/VRExpansionPlugin/VR)**

**[OpenVRExpansionPlugin](https://mordentral.bitbucket.io/OpenVRExpansionPlugin/VR)**

**[ExampleTemplate](https://mordentral.bitbucket.io/ExampleTemplate/VR)**  

[Tips & Tricks](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Plugin%20Tips%20&%20Information)

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

[VR Grippable Objects](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Grippable%20Objects)

[Replicated VR Camera Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Replicated%20VR%20Camera)


[VR Root Component Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VRRootComponent)


[Parent Relative Attachment Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/Parent%20Relative%20Attachment%20Component)


[VR Function Expansion Library Information Page](https://bitbucket.org/mordentral/vrexpansionplugin/wiki/VRExpansionFunctionLibrary)



### How do I VR? ###

The template project contains use examples of most of the features of the plugin as well as locomotion modes, interaction methods, and basic multiplayer.