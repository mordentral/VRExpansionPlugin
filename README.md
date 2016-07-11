### Use Of This Plugin ###

This plugin is intended to add colliding pickup functionality to the VR Motion controllers in UE4. Due to how the engine works this is not possible in Blueprint (with late renderer updates) and requires some specific functions in c++.

### How do I install it? ###

* Clone Or Download Zip and extract this repository to a folder named "VRGripMotionControllerPlugin" in your "ProjectName/Plugins" directory, create this directory if it is missing.

* Add the VRGripMotionControllerPlugin to your projects PublicDependencyModuleNames in the projects build.cs if you have c++ code included.

I will push pre-compiled binaries for all builds as well but I highly suggest that you have an installed compiler as it will re-build the project files for other engine versions for you then.

### Notes ###

* Because of having to alter a class declared INSIDE of the main controllercomponent class as well as access the rendering thread I found it better to redefine the entire motion controller instead of inherit from the original and override functions, it is a stand alone class and you will NOT be able to cast to "UMotionControllerComponent" from it. I apologize for this as it also means it will have to be manually updated with changes to the UMotionControllerComponent class in engine but having it as a child was really messy and I had to re-write sections of almost all of the functions anyway.

* You will get console warnings in the editor when gripping an actor with a skeletal mesh as the root component. It will still behave as is intended, it is just warning you that the skeletal mesh might be animated. In shipping builds this warning is gone, you could also set a primitive collision as the root and child the skeletal mesh to it to remove the warning and have better collision overall anyway.

### How do I use it? ###

* Replace your current MotionControllerComponents with GripMotionControllerComponents

* When you wish to pick up an actor call the components "GripActor" function, it will return "true/false" if the grip was successful. (See below for breakdowns of functions)

* When you wish to drop an actor call the "DropActor" function, it will return true/false if the drop was successful, false means it could not find that actor as gripped. (See below for breakdowns of functions)

* All actors to be gripped currently MUST have a UPrimitiveComponent derived component as the ROOT component (otherwise it will return false as it doesn't know what to grip or simulate).

* When teleporting a character you have to call "PostTeleportMoveGrippedActors" on each GripMotionControllerComponent. This moves all gripped actors to the new location WITHOUT allowing for collision with obstacles in the way and without updating the physics velocity. If you do not do this then the gripped actors will follow WITH collision and may get stuck on obstacles on the way.

### GripControllerComponent Variables ###

!! bool bSweepAttachedActors - (Removed as the grip function can do this now)

bool bUseWithoutTracking - If set gripped actors will still follow the controller even if there is no tracking for the hand it is assigned to. This is mainly used for testing grip functionality without a headset on or VR equipment set up at the time. A first or third person pawn can use it with this set and it parented to the camera and still test gripping and dropping.

### Best Practices ###

* If only picking up one actor at a time per hand, store them in a BP variable, then when going to pick up a new actor you can cancel if it is already held by that hand or make the other hand drop the actor if it is holding it (calling DropActor on it just to be safe is valid, you do not need to check in blueprint necessarily, but for this case I would have bSimulate turned off).

* Use a sphere trace to check for an object from the controller to pickup, with the right radius you can match the diameter of the controller / hand and it has less problems than overlaps and won't go through holes in geometry like a line trace. Use Persistent drawing on the trace until it looks right and matches where you want the grip location to be.

* Either remove collision from subcomponents to the controller or set "IgnoreMoveActor" on the pawn for a short period when dropping/throwing an item that may collide with the hand on the way out, otherwise the item may be thrown off course. Generally this applies mostly if the momentum is inward towards the hand itself and not outward away from it.

* Only pass in mobile actors to the grip function, it has the ability to set mobility on them but it does NOT restore the original mobility after dropping so it is safer to handle it yourself to get the exact result that you want.

* Pre-filter in BP the actor types that are allowed to be picked up, the function does not care what you pass to it and will even attempt to pick up terrain actors.

* Set some sort of breaking distance where if a gripped actor is too far away that it will get dropped.


### Blueprint Function Explanations ###

##GripActor##

* ActorToGrip - The actor that you want picked up

* WorldOffset - The transform that you want the picked up actor to stick to, this is in worldspace and automatically creates the relative offset from the gripping component.

* WorldOffsetIsRelative - If true then the worldoffset will be treated as relative already and will not be converted from world space. Can be used for snapping functionality by passing in a Sockets RelativeTransform (actor or component space)
or to null out offsets at all by passing in an empty transform. (Transforms with positions of worldspace 0,0,0 would not be 0,0,0 relative to an object without this set to true).

* OptionalSnapToSocketName - An optional input that if set to a valid socket name will override the WorldOffset and instead snap the object to the controller using the relative offset of the named socket. Socket must be on the root component for this to work, it does not currently iterate child components to look for the socket.

* SweepCollision - Whether the gripped actor has collision, if false then it will not trigger hit events and will pass through objects.

* InteractiveCollision - If true, the held actor can be stopped by the enviroment, if false then it passes through the enviroment but still triggers hit events.

* AllowSetMobility - Whether to set the root component of the gripped actor to mobile, the grip function will fail otherwise if the root component is static mobility.

Returns true/false whether the actor was gripped or not.

The default use of this would be to pass in the GetActorTransform to the WorldOffset. The function would then get the relative position of the actor from the controller and lock the actor to it until dropped. 

If you wanted snap functionality you would:

* Create a mesh socket for the root component of the actor

* If the socket is on the root component then name it in the grip function

*  If the socket is not on the root component then pass its component or actor space transform in as WorldOffset and set WorldOffsetIsRelative to true.

You can of course mix the two versions by using snap if the grab is within a distance of a snap point.

##DropActor##

* ActorToDrop - The actor that you are attempting to drop, if it is not currently held it will just return false

* bSimulate - Whether to set the gripped actor to simulate after dropping, if false the actor will be frozen in place instead.

Returns true/false whether the actor was found and dropped.

##PostTeleportMoveGrippedActors##

Moves all actors gripped by this controller component to the new location without changing velocity or allowing interaction with the enviroment.

## TODO? ##

* Might need to handle held actor colliding with a non controller childed component. In that case would have to set the rootcomponent of the motion controllers parent to ignoremove the gripped actor. This shouldn't be a problem with VR pawns except when they have bodies but it could be a problem with non VR pawns.