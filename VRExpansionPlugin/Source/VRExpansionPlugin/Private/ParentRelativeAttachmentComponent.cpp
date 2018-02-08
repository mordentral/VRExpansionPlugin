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
	bWasSetOnce = false;

	bIgnoreRotationFromParent = false;
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (OptionalWaistTrackingParent.IsValid())
	{
		//#TODO: bOffsetByHMD not supported with this currently, fix it, need to check for both camera and HMD
		FTransform TrackedParentWaist = IVRTrackedParentInterface::Default_GetWaistOrientationAndPosition(OptionalWaistTrackingParent);

		if (bUseFeetLocation)
		{
			TrackedParentWaist.SetTranslation(TrackedParentWaist.GetTranslation() * FVector(1.0f, 1.0f, 0.0f));

			if (!bIgnoreRotationFromParent)
			{
				FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(TrackedParentWaist.Rotator());

				TrackedParentWaist.SetRotation(GetCalculatedRotation(InverseRot, DeltaTime));
			}
		}

		SetRelativeTransform(TrackedParentWaist);

	}
	else if (IsLocallyControlled() && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed())
	{
		FQuat curRot;
		FVector curCameraLoc;
		if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, curRot, curCameraLoc))
		{
			if (!bIgnoreRotationFromParent)
			{
				FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curRot.Rotator());

				SetRelativeRotation(GetCalculatedRotation(InverseRot, DeltaTime));
			}

			if (bOffsetByHMD)
			{
				curCameraLoc.X = 0;
				curCameraLoc.Y = 0;
			}

			if (bUseFeetLocation)
			{
				SetRelativeLocation(FVector(curCameraLoc.X, curCameraLoc.Y, 0.0f)); // Set the Z to the bottom of the capsule
			}
			else
			{
				SetRelativeLocation(curCameraLoc); // Use the HMD height instead
			}
		}
	}
	else if (this->GetOwner())
	{
		if (UCameraComponent * CameraOwner = this->GetOwner()->FindComponentByClass<UCameraComponent>())
		{
			if (!bIgnoreRotationFromParent)
			{
				FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw(CameraOwner->RelativeRotation);

				SetRelativeRotation(GetCalculatedRotation(InverseRot, DeltaTime));
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