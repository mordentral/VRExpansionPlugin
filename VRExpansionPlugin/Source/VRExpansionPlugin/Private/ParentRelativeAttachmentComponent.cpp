// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "ParentRelativeAttachmentComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(ParentRelativeAttachmentComponent)

#include "Engine/Engine.h"
#include "VRBaseCharacter.h"
#include "VRCharacter.h"
#include "IXRTrackingSystem.h"
#include "VRRootComponent.h"
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
	//bOffsetByHMD = false;
	
	bLerpTransition = true;
	LerpSpeed = 100.0f;
	LastLerpVal = 0.0f;
	LerpTarget = 0.0f;
	bWasSetOnce = false;

	LeftControllerTrans = FTransform::Identity;
	RightControllerTrans = FTransform::Identity;

	bIgnoreRotationFromParent = false;
	bUpdateInCharacterMovement = true;
	bIsPaused = false;

	CustomOffset = FVector::ZeroVector;

	//YawRotationMethod = EVR_PRC_RotationMethod::PRC_ROT_HMD;
}

void UParentRelativeAttachmentComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Update our tracking
	if (!bUseFeetLocation && IsValid(AttachChar) && IsValid(AttachChar->VRReplicatedCamera)) // New case to early out and with less calculations
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
		AttachChar = nullptr;
	}

	if (AVRBaseCharacter * BaseCharacterOwner = Cast<AVRBaseCharacter>(this->GetOwner()))
	{
		AttachBaseChar = BaseCharacterOwner;
	}
	else
	{
		AttachBaseChar = nullptr;
	}

	Super::OnAttachmentChanged();
}

void UParentRelativeAttachmentComponent::SetPaused(bool bNewPaused, bool bZeroOutRotation, bool bZeroOutLocation)
{
	if (bNewPaused != bIsPaused)
	{
		bIsPaused = bNewPaused;

		if (bIsPaused && (bZeroOutLocation || bZeroOutRotation))
		{
			FVector NewLoc = this->GetRelativeLocation();
			FRotator NewRot = this->GetRelativeRotation();

			if (bZeroOutLocation)
			{
				NewLoc = FVector::ZeroVector;
			}

			if (bZeroOutRotation)
			{
				NewRot = FRotator::ZeroRotator;
			}

			SetRelativeLocationAndRotation(NewLoc, NewRot);
		}
	}
}

void UParentRelativeAttachmentComponent::SetRelativeRotAndLoc(FVector NewRelativeLocation, FRotator NewRelativeRotation, float DeltaTime)
{

	RunSampling(NewRelativeRotation, NewRelativeLocation);

	if (bUseFeetLocation)
	{
		FVector TotalOffset = CustomOffset;
		if (bUseCenterAsFeetLocation && AttachChar && AttachChar->VRRootReference)
		{
			TotalOffset.Z += AttachChar->VRRootReference->GetUnscaledCapsuleHalfHeight();
		}

		if (!bIgnoreRotationFromParent)
		{
			SetRelativeLocationAndRotation(
				FVector(NewRelativeLocation.X, NewRelativeLocation.Y, 0.0f) + TotalOffset,
				GetCalculatedRotation(NewRelativeRotation, DeltaTime)
			);
		}
		else
		{
			SetRelativeLocation(FVector(NewRelativeLocation.X, NewRelativeLocation.Y, 0.0f) + TotalOffset);
		}
	}
	else
	{
		if (!bIgnoreRotationFromParent)
		{
			SetRelativeLocationAndRotation(
				NewRelativeLocation + CustomOffset,
				GetCalculatedRotation(NewRelativeRotation, DeltaTime)
			); // Use the HMD height instead
		}
		else
		{
			SetRelativeLocation(NewRelativeLocation + CustomOffset); // Use the HMD height instead
		}
	}
}

void UParentRelativeAttachmentComponent::UpdateTracking(float DeltaTime)
{
	// We are paused, do not update tracking anymore
	if (bIsPaused)
		return;

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
	else if (IsValid(AttachChar)) // New case to early out and with less calculations
	{
		if (AttachChar->bRetainRoomscale)
		{
			SetRelativeRotAndLoc(AttachChar->VRRootReference->curCameraLoc, AttachChar->VRRootReference->StoredCameraRotOffset, DeltaTime);
		}
		else
		{
			FVector CameraLoc = FVector(0.0f, 0.0f, AttachChar->VRRootReference->curCameraLoc.Z);
			CameraLoc += AttachChar->VRRootReference->StoredCameraRotOffset.RotateVector(FVector(-AttachChar->VRRootReference->VRCapsuleOffset.X, -AttachChar->VRRootReference->VRCapsuleOffset.Y, 0.0f));
			SetRelativeRotAndLoc(CameraLoc, AttachChar->VRRootReference->StoredCameraRotOffset, DeltaTime);
		}
	}
	else if (IsLocallyControlled() && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed())
	{
		FQuat curRot;
		FVector curCameraLoc;
		if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, curRot, curCameraLoc))
		{
			/*if (bOffsetByHMD)
			{
				curCameraLoc.X = 0;
				curCameraLoc.Y = 0;
			}*/

			if (!bIgnoreRotationFromParent)
			{
				FRotator InverseRot = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curRot.Rotator());
				SetRelativeRotAndLoc(curCameraLoc, InverseRot, DeltaTime);
			}
			else
				SetRelativeRotAndLoc(curCameraLoc, FRotator::ZeroRotator, DeltaTime);
		}
	}
	else if (IsValid(AttachBaseChar))
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
	if (!bUpdateInCharacterMovement || !IsValid(AttachChar))
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