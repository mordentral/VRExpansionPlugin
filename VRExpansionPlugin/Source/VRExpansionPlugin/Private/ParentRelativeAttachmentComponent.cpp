// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "ParentRelativeAttachmentComponent.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"
//#include "VRSimpleCharacter.h"
//#include "VRCharacter.h"


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
	
	bLerpTransition = true;
	LerpSpeed = 100.0f;
	LastLerpVal = 0.0f;
	LerpTarget = 0.0f;
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (OptionalWaistTrackingParent.IsValid())
	{
		SetRelativeTransform(IVRTrackedParentInterface::Default_GetWaistOrientationAndPosition(OptionalWaistTrackingParent));
	}
	else if (IsLocallyControlled() && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed() && GEngine->XRSystem->HasValidTrackingPosition())
	{
		FQuat curRot;
		FVector curCameraLoc;
		GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, curRot, curCameraLoc);

		FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curRot.Rotator());
		
		// This is never true with the default value of 0.0f
		if ((FPlatformMath::Abs(InverseRot.Yaw - LastRot.Yaw)) < YawTolerance)
		{
			if (bLerpTransition)
			{
				LastLerpVal = FMath::FixedTurn(LastLerpVal, LerpTarget, LerpSpeed * DeltaTime);
				//LastLerpVal = FMath::FInterpConstantTo(LastLerpVal, LerpTarget, DeltaTime, LerpSpeed);
				SetRelativeRotation(FRotator(0, LastLerpVal, 0).Quaternion());
			}
			else
			{
				SetRelativeRotation(FRotator(0, LastRot.Yaw, 0).Quaternion());
			}
		}
		else
		{
			// If we are using a snap threshold
			if (!FMath::IsNearlyZero(YawTolerance))
			{
				LerpTarget = InverseRot.Yaw;
				LastLerpVal = LastRot.Yaw;
			}
			else // If we aren't then just directly set it to the correct rotation
			{
				SetRelativeRotation(FRotator(0, InverseRot.Yaw, 0).Quaternion());
			}

			LastRot = InverseRot;
		}

		if (bOffsetByHMD)
		{
			curCameraLoc.X = 0;
			curCameraLoc.Y = 0;
		}
		
		if(bUseFeetLocation)
		{		
			SetRelativeLocation(FVector(curCameraLoc.X, curCameraLoc.Y, 0.0f)); // Set the Z to the bottom of the capsule
		}
		else
		{
			SetRelativeLocation(curCameraLoc); // Use the HMD height instead
		}
	}
	else if (this->GetOwner())
	{
		if (UCameraComponent * CameraOwner = this->GetOwner()->FindComponentByClass<UCameraComponent>())
		{
			FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw(CameraOwner->RelativeRotation);

			// This is never true with the default value of 0.0f
			if ((FPlatformMath::Abs(InverseRot.Yaw - LastRot.Yaw)) < YawTolerance)
			{
				if (bLerpTransition)
				{
					LastLerpVal = FMath::FixedTurn(LastLerpVal, LerpTarget, LerpSpeed * DeltaTime);
					//LastLerpVal = FMath::FInterpConstantTo(LastLerpVal, LerpTarget, DeltaTime, LerpSpeed);
					SetRelativeRotation(FRotator(0, LastLerpVal, 0).Quaternion());
				}
				else
				{
					SetRelativeRotation(FRotator(0, LastRot.Yaw, 0).Quaternion());
				}
			}
			else
			{
				// If we are using a snap threshold
				if (!FMath::IsNearlyZero(YawTolerance))
				{
					LerpTarget = InverseRot.Yaw;
					LastLerpVal = LastRot.Yaw;
				}
				else // If we aren't then just directly set it to the correct rotation
				{
					SetRelativeRotation(FRotator(0, InverseRot.Yaw, 0).Quaternion());
				}

				LastRot = InverseRot;
			}

			if(bUseFeetLocation)
			{			
				SetRelativeLocation(FVector(CameraOwner->RelativeLocation.X, CameraOwner->RelativeLocation.Y, 0.0f)); // Set the Z to the bottom of the capsule
			}
			else
			{
				SetRelativeLocation(CameraOwner->RelativeLocation); // Use the HMD height instead
			}
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}