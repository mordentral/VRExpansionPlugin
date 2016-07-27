// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"
#include "ParentRelativeAttachmentComponent.h"


UParentRelativeAttachmentComponent::UParentRelativeAttachmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);

	bLockPitch = true;
	bLockYaw = false;
	bLockRoll = true;

	PitchTolerance = 0.0f;
	YawTolerance = 0.0f;
	RollTolerance = 0.0f;
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (this->GetAttachParent())
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
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}