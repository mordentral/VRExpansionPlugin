// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
//#include "GripMotionControllerComponent.h"
//#include "MotionControllerComponent.h"
//#include "VRGripInterface.h"
//#include "GameplayTagContainer.h"
//#include "GameplayTagAssetInterface.h"
//#include "VRInteractibleFunctionLibrary.h"
//#include "PhysicsEngine/ConstraintInstance.h"


#include "VRGripScriptBase.generated.h"


UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRGripScriptBase : public UObject
{
	GENERATED_BODY()
public:
	UVRGripScriptBase(const FObjectInitializer& ObjectInitializer);
	// Need to add TICK and BeginPlay implementations

	// Implement VRGripInterface so that we can add functionality with it

	// Useful functions to override in c++ for functionality
	//virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction);

	// Not all scripts will require this function, specific ones that use things like Lever logic however will. Best to call it.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript|Init")
		void BeginPlay();
		virtual void BeginPlay_Implementation();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript|Steps")
		void GetWorldTransform_PreStep(FTransform & WorldTransform);
		virtual void GetWorldTransform_PreStep_Implementation(FTransform & WorldTransform);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript|Steps")
		void GetWorldTransform_Override(FTransform & WorldTransform);
		virtual void GetWorldTransform_Override_Implementation(FTransform & WorldTransform);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript|Steps")
		void GetWorldTransform_PostStep(FTransform & WorldTransform);
		virtual void GetWorldTransform_PostStep_Implementation(FTransform & WorldTransform);

};
