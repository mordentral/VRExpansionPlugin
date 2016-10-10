// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRGripInterface.generated.h"


UINTERFACE(BlueprintType)
class VREXPANSIONPLUGIN_API UVRGripInterface: public UInterface
{
	GENERATED_UINTERFACE_BODY()
};


class VREXPANSIONPLUGIN_API IVRGripInterface
{
	GENERATED_IINTERFACE_BODY()
 
public:

	// Set up as deny instead of allow so that default allows for gripping
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		bool DenyGripping();

	// How an interfaced object behaves when teleporting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		EGripInterfaceTeleportBehavior TeleportBehavior();

	// Should this object simulate on drop
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		bool SimulateOnDrop();

	// Type of object, fill in with your own enum
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		void ObjectType(uint8 & ObjectType);

	// Grip type to use when gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		EGripCollisionType SlotGripType();

	// Grip type to use when not gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		EGripCollisionType FreeGripType();

	// Can have secondary grip
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		bool CanHaveDoubleGrip();

	// Get closest secondary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		void ClosestSecondarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform);

	// Get closest primary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		void ClosestPrimarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform);


	// Events that can be called for interface inheriting actors

	// Event triggered on the interfaced object when gripped
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when grip is released
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is gripped
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnChildGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is released
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnChildGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary gripped
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnSecondaryGrip(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary grip is released
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnSecondaryGripRelease(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation);


	// Interaction Functions

	// Call to use an object
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnUsed();

	// Check if the object is an interactable
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		bool IsInteractible();

	// Get interactable settings
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		FBPInteractionSettings GetInteractionSettings();
};