// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "ParentRelativeAttachmentComponent.h"
#include "VRBaseCharacter.h"
#include "VRCharacter.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"
//#include "VRSimpleCharacter.h"
//#include "VRCharacter.h"


UParentRelativeAttachmentComponent::UParentRelativeAttachmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// Let it sit in DuringPhysics like is the default
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	bWantsInitializeComponent = true;

	SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));
	SetRelativeLocation(FVector::ZeroVector);
	YawTolerance = 0.0f;
	bOffsetByHMD = false;
	
	bLerpTransition = true;
	LerpSpeed = 100.0f;
	LastLerpVal = 0.0f;
	LerpTarget = 0.0f;
	bWasSetOnce = false;

	LeftControllerTrans = FTransform::Identity;
	RightControllerTrans = FTransform::Identity;

	bIgnoreRotationFromParent = false;
	bUpdateInCharacterMovement = true;

	bUseFeetLocation = false;
	CustomOffset = FVector::ZeroVector;

	//YawRotationMethod = EVR_PRC_RotationMethod::PRC_ROT_HMD;
}

void UParentRelativeAttachmentComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Update our tracking
	if (!bUseFeetLocation && AttachChar.IsValid()) // New case to early out and with less calculations
	{
		SetRelativeTransform(AttachChar->VRReplicatedCamera->GetRelativeTransform());
	}

}

void UParentRelativeAttachmentComponent::OnAttachmentChanged()
{
	if (AVRCharacter* CharacterOwner = Cast<AVRCharacter>(this->GetOwner()))
	{
		AttachChar = CharacterOwner;
	}
	else
	{
		AttachChar.Reset();
	}

	if (AVRBaseCharacter * BaseCharacterOwner = Cast<AVRBaseCharacter>(this->GetOwner()))
	{
		AttachBaseChar = BaseCharacterOwner;
	}
	else
	{
		AttachBaseChar.Reset();
	}

	Super::OnAttachmentChanged();
}

void UParentRelativeAttachmentComponent::UpdateTracking(float DeltaTime)
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

		TrackedParentWaist.AddToTranslation(CustomOffset);
		SetRelativeTransform(TrackedParentWaist);

	}
	else if (AttachChar.IsValid()) // New case to early out and with less calculations
	{
		SetRelativeRotAndLoc(AttachChar->VRRootReference->curCameraLoc, AttachChar->VRRootReference->StoredCameraRotOffset, DeltaTime);
	}
	else if (IsLocallyControlled() && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed())
	{
		FQuat curRot;
		FVector curCameraLoc;
		if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, curRot, curCameraLoc))
		{
			if (bOffsetByHMD)
			{
				curCameraLoc.X = 0;
				curCameraLoc.Y = 0;
			}

			if (!bIgnoreRotationFromParent)
			{
				FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curRot.Rotator());
				SetRelativeRotAndLoc(curCameraLoc, InverseRot, DeltaTime);
			}
			else
				SetRelativeRotAndLoc(curCameraLoc, FRotator::ZeroRotator, DeltaTime);
		}
	}
	else if (AttachBaseChar.IsValid())
	{
		if (AttachBaseChar->VRReplicatedCamera)
		{
			if (!bIgnoreRotationFromParent)
			{
				FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw(AttachBaseChar->VRReplicatedCamera->GetRelativeRotation());
				SetRelativeRotAndLoc(AttachBaseChar->VRReplicatedCamera->GetRelativeLocation(), InverseRot, DeltaTime);
			}
			else
				SetRelativeRotAndLoc(AttachBaseChar->VRReplicatedCamera->GetRelativeLocation(), FRotator::ZeroRotator, DeltaTime);
		}
	}
	else if (AActor* owner = this->GetOwner())
	{
		if (UCameraComponent* CameraOwner = owner->FindComponentByClass<UCameraComponent>())
		{
			if (!bIgnoreRotationFromParent)
			{
				FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw(CameraOwner->GetRelativeRotation());
				SetRelativeRotAndLoc(CameraOwner->GetRelativeLocation(), InverseRot, DeltaTime);
			}
			else
				SetRelativeRotAndLoc(CameraOwner->GetRelativeLocation(), FRotator::ZeroRotator, DeltaTime);
		}
	}
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (!bUpdateInCharacterMovement || !AttachChar.IsValid())
	{
		UpdateTracking(DeltaTime);
	}
	else
	{
		UCharacterMovementComponent * CharMove = AttachChar->GetCharacterMovement();
		if (!CharMove || !CharMove->IsComponentTickEnabled() || !CharMove->IsActive())
		{	
			// Our character movement isn't handling our updates, lets do it ourself.
			UpdateTracking(DeltaTime);
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}