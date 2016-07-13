// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Engine.h"
#include "VRBPDatatypes.generated.h"

USTRUCT()
struct FBPVRComponentPosRep
{
	GENERATED_BODY()
public:
	UPROPERTY()
		FVector_NetQuantize100 Position;
	UPROPERTY()
		FRotator Orientation;
};

UENUM(Blueprintable)
enum EGripAttachmentType
{
	// Did not like how this works
//	GripWithAttachTo,
	GripWithMoveTo
};

UENUM(Blueprintable)
enum EGripCollisionType
{
	InteractiveCollisionWithPhysics,
	SweepWithPhysics,
	PhysicsOnly
};

USTRUCT()
struct FBPActorGripInformation
{
	GENERATED_BODY()
public:
	UPROPERTY()
		AActor * Actor;
	UPROPERTY()
		TEnumAsByte<EGripCollisionType> GripCollisionType;
	UPROPERTY()
		TEnumAsByte<EGripAttachmentType> GripAttachmentType;
	UPROPERTY()
		bool bColliding;
	UPROPERTY()
		FTransform RelativeTransform;
	UPROPERTY()
		bool bOriginalReplicatesMovement;

	// For multi grip situations
	UPROPERTY()
		USceneComponent * SecondaryAttachment;
	//UPROPERTY()
	//	FTransform SecondaryRelativeTransform;
	UPROPERTY()
		bool bHasSecondaryAttachment;
	// Allow hand to not be primary positional attachment?
	// End multi grip

	FBPActorGripInformation()
	{
		Actor = nullptr;
		bColliding = false;
		GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics;
		GripAttachmentType = EGripAttachmentType::GripWithMoveTo;


		SecondaryAttachment = nullptr;
		bHasSecondaryAttachment = false;
		//bHandIsPrimaryReference = true;
	}
};