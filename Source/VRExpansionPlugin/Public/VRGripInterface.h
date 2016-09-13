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
};