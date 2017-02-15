// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"
#include "KismetMathLibrary.h"
#include "ParentRelativeAttachmentComponent.h"
#include "VRSimpleCharacter.h"


UParentRelativeAttachmentComponent::UParentRelativeAttachmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);
	YawTolerance = 0.0f;
	bOffsetByHMD = false;
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (IsLocallyControlled() && GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed() && GEngine->HMDDevice->HasValidTrackingPosition())
	{
		FQuat curRot;
		FVector curCameraLoc;
		GEngine->HMDDevice->GetCurrentOrientationAndPosition(curRot, curCameraLoc);

		FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curRot.Rotator());

		FTransform ParentTrans = FTransform::Identity;

		if (USceneComponent * Parent = GetAttachParent())
			ParentTrans = Parent->GetComponentToWorld();
		
		if ((FPlatformMath::Abs(InverseRot.Yaw - LastRot.Yaw)) < YawTolerance)
		{
			SetWorldRotation(FRotator(0, LastRot.Yaw, 0).Quaternion() * ParentTrans.GetRotation(), false);
			return;
		}

		LastRot = InverseRot;
		SetWorldRotation(FRotator(0, InverseRot.Yaw, 0).Quaternion() * ParentTrans.GetRotation(), false);

		if (bOffsetByHMD)
		{
			curCameraLoc.X = 0;
			curCameraLoc.Y = 0;
		}

		SetRelativeLocation(curCameraLoc);
	}
	else if (this->GetOwner())
	{
		if (UCameraComponent * CameraOwner = this->GetOwner()->FindComponentByClass<UCameraComponent>())
		{
			FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw(CameraOwner->GetComponentRotation());

			if ((FPlatformMath::Abs(InverseRot.Yaw - LastRot.Yaw)) < YawTolerance)
			{
				SetWorldRotation(FRotator(0, LastRot.Yaw, 0).Quaternion(), false);
				return;
			}

			LastRot = InverseRot;
			SetWorldRotation(FRotator(0, InverseRot.Yaw, 0).Quaternion(), false);
			SetWorldLocation(CameraOwner->GetComponentLocation());
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}