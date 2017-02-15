// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"

#include "VRSimpleCharacter.h"

AVRSimpleCharacter::AVRSimpleCharacter(const FObjectInitializer& ObjectInitializer)
 : Super(ObjectInitializer.SetDefaultSubobjectClass<UVRSimpleCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))

{

	// Remove the movement jitter with slow speeds
	this->ReplicatedMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;

	VRMovementReference = Cast<UVRBaseCharacterMovementComponent>(GetMovementComponent());

	VRSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("VR Scene Component"));

	if (VRSceneComponent)
	{
		VRSceneComponent->SetupAttachment(RootComponent);
		VRSceneComponent->SetRelativeLocation(FVector(0, 0, -96));
	}

	//VRReplicatedCamera = CreateDefaultSubobject<UReplicatedVRCameraComponent>(TEXT("VR Replicated Camera"));
	if (VRReplicatedCamera && VRSceneComponent)
	{
		VRReplicatedCamera->bOffsetByHMD = true;
		VRReplicatedCamera->SetupAttachment(VRSceneComponent);
	}

	/*VRHeadCollider = CreateDefaultSubobject<UCapsuleComponent>(TEXT("VR Head Collider"));
	if (VRHeadCollider && VRReplicatedCamera)
	{
		VRHeadCollider->SetCapsuleSize(20.0f, 25.0f);
		VRHeadCollider->SetupAttachment(VRReplicatedCamera);
	}*/

//	ParentRelativeAttachment = CreateDefaultSubobject<UParentRelativeAttachmentComponent>(TEXT("Parent Relative Attachment"));
	if (ParentRelativeAttachment && VRReplicatedCamera && VRSceneComponent)
	{
		ParentRelativeAttachment->bOffsetByHMD = true;
		ParentRelativeAttachment->SetupAttachment(VRSceneComponent);
	}

	if (LeftMotionController)
	{
		LeftMotionController->bOffsetByHMD = true;

		if (VRSceneComponent)
		{
			LeftMotionController->SetupAttachment(VRSceneComponent);
		}
	}

	//RightMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Right Grip Motion Controller"));
	if (RightMotionController)
	{
		RightMotionController->bOffsetByHMD = true;

		if (VRSceneComponent)
		{
			RightMotionController->SetupAttachment(VRSceneComponent);
		}
	}
}

void AVRSimpleCharacter::GenerateOffsetToWorld()
{
	FRotator CamRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(VRReplicatedCamera->GetComponentRotation());
	OffsetComponentToWorld = FTransform(CamRotOffset.Quaternion(), this->GetActorLocation(), this->GetActorScale3D());
}

FVector AVRSimpleCharacter::GetTeleportLocation(FVector OriginalLocation)
{
	//FVector modifier = VRRootReference->GetVRLocation() - this->GetActorLocation();
	//modifier.Z = 0.0f; // Null out Z
	//return OriginalLocation - modifier;
	
	return OriginalLocation;
}

bool AVRSimpleCharacter::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck)
{
	bool bTeleportSucceeded = Super::TeleportTo(DestLocation + FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), DestRotation, bIsATest, bNoCheck);

	if (bTeleportSucceeded)
	{
		NotifyOfTeleport();
	}

	return bTeleportSucceeded;
}