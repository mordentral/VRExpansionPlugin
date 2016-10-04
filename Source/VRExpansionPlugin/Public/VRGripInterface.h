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

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		void ObjectType(uint8 & ObjectType);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		EGripCollisionType SlotGripType();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		EGripCollisionType FreeGripType();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		bool CanHaveDoubleGrip();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		void ClosestSecondarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		void ClosestPrimarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform);


	// Events that can be called for interface inheriting actors
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnGrip(UGripMotionControllerComponent * GrippingController);

	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnGripRelease(UGripMotionControllerComponent * ReleasingController);

	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnSecondaryGrip(USceneComponent * SecondaryGripComponent);

	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnSecondaryGripRelease(USceneComponent * ReleasingSecondaryGripComponent);


	// Interaction Functions
	UFUNCTION(BlueprintImplementableEvent, Category = "VRGrip")
		void OnUsed();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		bool IsInteractible();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGrip")
		FBPInteractionSettings GetInteractionSettings();
};