// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "GS_GunTools.generated.h"

UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UGS_GunTools : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	// Shoulder Attachment component
	// Shoulder attachment transform

	// Recoil max magnitude
	// Recoil falloff
	// Recoil addition transform

	// ?

	//virtual void BeginPlay_Implementation() override;
	//virtual void ModifyWorldTransform_Implementation(float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface) override;
	//virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;
	//virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false) override;
};
