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
	GripWithAttachTo,
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
	//	UPROPERTY()
	//	bool bInteractiveCollision;
	//	UPROPERTY()
	//	bool bSweepCollision;
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


	FBPActorGripInformation()
	{
		bColliding = false;
		GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics;
		GripAttachmentType = EGripAttachmentType::GripWithMoveTo;
	}
};