UE4 Forums Thread
https://forums.unrealengine.com/development-discussion/vr-ar-development/89050-vr-openvr-expansion-plugin

Example Template Project
https://github.com/mordentral/VRExpPluginExample

Website:
www.vreue4.com

***

### Use Of This Plugin ###

This Plugin is intended to add additional functionality to Open/SteamVR/(All VR now) in UE4.

### Plugin Website ###
[VREUE4.com](https://vreue4.com)

### How do I install it? ###

https://vreue4.com/documentation?section=installation

**Guides for migrating between different engine versions of the plugin:**

View the patch notes at www.vreue4.com for migration guides as well.

**Option 1:**

Go to www.vreue4.com and downloaded the pre-built binary version for the engine version you are using (not updated with every daily change, only weekly or with large patches).

Install it into your Engine/Plugins directoy or ProjectName/Plugins Directory.

**Option 2 (More up to date - preferred if possible):**

* Clone Or Download Zip and extract this repository to a folder named "VRExpansionPlugin" in your "ProjectName/Plugins" directory, create this directory if it is missing.

* Add the VRExpansionPlugin to your projects PublicDependencyModuleNames in the projects build.cs if you have c++ code included.

* IF you do not have c++ code, use the Add New button in the editor and add a blank c++ class to your project.

* Open up the generated project .SLN file and build the project from the build menu.

You need to have visual studio installed and follow the UE4 setup guide for it: https://docs.unrealengine.com/latest/INT/Programming/Development/VisualStudioSetup/

### How do I use it? ###
### How do I VR? ###

The template project contains use examples of most of the features of the plugin as well as locomotion modes, interaction methods, and basic multiplayer.
