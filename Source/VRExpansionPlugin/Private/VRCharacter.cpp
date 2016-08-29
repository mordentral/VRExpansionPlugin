// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"

#include "VRCharacter.h"


AVRCharacter::AVRCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.DoNotCreateDefaultSubobject(ACharacter::MeshComponentName).SetDefaultSubobjectClass<UVRRootComponent>(ACharacter::CapsuleComponentName).SetDefaultSubobjectClass<UVRCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	VRRootReference = NULL;
	if (GetCapsuleComponent())
	{
		VRRootReference = Cast<UVRRootComponent>(GetCapsuleComponent());
		VRRootReference->SetCapsuleSize(20.0f, 96.0f);
	}

	VRMovementReference = NULL;
	if (GetMovementComponent())
	{
		VRMovementReference = Cast<UVRCharacterMovementComponent>(GetMovementComponent());
	}

	VRReplicatedCamera = CreateDefaultSubobject<UReplicatedVRCameraComponent>(TEXT("VR Replicated Camera"));
	if (VRReplicatedCamera)
	{
		VRReplicatedCamera->SetupAttachment(RootComponent);
		// By default this will tick after the root, root will be one tick behind on position. Doubt it matters much
	}

	ParentRelativeAttachment = CreateDefaultSubobject<UParentRelativeAttachmentComponent>(TEXT("Parent Relative Attachment"));
	if (ParentRelativeAttachment && VRReplicatedCamera)
	{
		ParentRelativeAttachment->SetupAttachment(VRReplicatedCamera);

		/*if (GetMesh())
		{
			GetMesh()->SetupAttachment(ParentRelativeAttachment);
		}*/
	}

	LeftMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Left Grip Motion Controller"));
	if (LeftMotionController)
	{
		LeftMotionController->SetupAttachment(RootComponent);
		LeftMotionController->Hand = EControllerHand::Left;
		
		// Keep the controllers ticking after movement
		if (this->GetCharacterMovement())
		{
			LeftMotionController->AddTickPrerequisiteComponent(this->GetCharacterMovement());
		}
	}

	RightMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Right Grip Motion Controller"));
	if (RightMotionController)
	{
		RightMotionController->SetupAttachment(RootComponent);
		RightMotionController->Hand = EControllerHand::Right;

		// Keep the controllers ticking after movement
		if (this->GetCharacterMovement())
		{
			RightMotionController->AddTickPrerequisiteComponent(this->GetCharacterMovement());
		}
	}

}

FVector AVRCharacter::GetTeleportLocation(FVector OriginalLocation)
{
	FVector modifier = VRRootReference->GetVRLocation() - this->GetActorLocation();
	modifier.Z = 0.0f; // Null out Z
	return OriginalLocation - modifier;
}

bool AVRCharacter::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck)
{
	bool bTeleportSucceeded = Super::TeleportTo(DestLocation, DestRotation, bIsATest, bNoCheck);

	if (bTeleportSucceeded)
	{
		NotifyOfTeleport();
	}

	return bTeleportSucceeded;
}

void AVRCharacter::NotifyOfTeleport_Implementation()
{
	if (LeftMotionController)
		LeftMotionController->PostTeleportMoveGrippedActors();

	if (RightMotionController)
		RightMotionController->PostTeleportMoveGrippedActors();

	// Regenerate the capsule offset location
	if (VRRootReference)
		VRRootReference->GenerateOffsetToWorld();
}

FVector AVRCharacter::GetNavAgentLocation() const
{
	FVector AgentLocation = FNavigationSystem::InvalidLocation;

	if (GetCharacterMovement() != nullptr)
	{
		if (UVRCharacterMovementComponent * VRMove = Cast<UVRCharacterMovementComponent>(GetCharacterMovement()))
		{
			AgentLocation = VRMove->GetActorFeetLocation();
		}
		else
			AgentLocation = GetCharacterMovement()->GetActorFeetLocation();
	}

	if (FNavigationSystem::IsValidLocation(AgentLocation) == false && GetCapsuleComponent() != nullptr)
	{
		if (VRRootReference)
		{
			AgentLocation = VRRootReference->GetVRLocation() - FVector(0, 0, VRRootReference->GetScaledCapsuleHalfHeight());
		}
		else
			AgentLocation = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}

	return AgentLocation;
}