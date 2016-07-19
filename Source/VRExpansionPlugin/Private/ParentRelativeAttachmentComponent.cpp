// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "ParentRelativeAttachmentComponent.h"


UParentRelativeAttachmentComponent::UParentRelativeAttachmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->CapsuleRadius = 16.0f;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);

	bLockPitch = true;
	bLockYaw = false;
	bLockRoll = true;

	PitchTolerance = 1.0f;
	YawTolerance = 1.0f;
	RollTolerance = 1.0f;

	bAutoSizeCapsuleHeight = false;
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if(this->GetAttachParent())
	{
		FRotator InverseRot = GetAttachParent()->GetComponentRotation();
		FRotator CurRot = this->GetComponentRotation();

		float newYaw = CurRot.Yaw;
		float newRoll = CurRot.Roll;
		float newPitch = CurRot.Pitch;

		if (bLockYaw)
			newYaw = 0;
		else if (!bLockYaw && (FPlatformMath::Abs(InverseRot.Yaw - CurRot.Yaw)) > YawTolerance)
			newYaw = InverseRot.Yaw;
		else
			newYaw = CurRot.Yaw;

		if (bLockPitch)
			newPitch = 0;
		else if (!bLockPitch && (FPlatformMath::Abs(InverseRot.Pitch - CurRot.Pitch)) > PitchTolerance)
			newPitch = InverseRot.Pitch;

		if (bLockRoll)
			newRoll = 0;
		else if (!bLockRoll && (FPlatformMath::Abs(InverseRot.Roll - CurRot.Roll)) > RollTolerance)
			newRoll = InverseRot.Roll;

		SetWorldRotation(FRotator(newPitch, newYaw, newRoll), false);
		
		if (bAutoSizeCapsuleHeight)
		{
			SetCapsuleSize(this->CapsuleRadius, GetAttachParent()->RelativeLocation.Z / 2, false);
			this->SetRelativeLocation(FVector(0, 0, -this->CapsuleHalfHeight), false);
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}