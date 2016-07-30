// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"

#include "VRCharacter.h"


AVRCharacter::AVRCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVRRootComponent>(ACharacter::CapsuleComponentName).SetDefaultSubobjectClass<UVRCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	if (GetCapsuleComponent())
	{
		UVRRootComponent * rootComp = Cast<UVRRootComponent>(GetCapsuleComponent());
		rootComp->SetCapsuleSize(20.0f, 96.0f);
	}

	VRReplicatedCamera = CreateDefaultSubobject<UReplicatedVRCameraComponent>(TEXT("VR Replicated Camera"));
	if (VRReplicatedCamera)
	{
		VRReplicatedCamera->SetupAttachment(RootComponent);
	}

	ParentRelativeAttachment = CreateDefaultSubobject<UParentRelativeAttachmentComponent>(TEXT("Parent Relative Attachment"));
	if (ParentRelativeAttachment && VRReplicatedCamera)
	{
		ParentRelativeAttachment->SetupAttachment(VRReplicatedCamera);

		if (GetMesh())
		{
			GetMesh()->SetupAttachment(ParentRelativeAttachment);
		}
	}

	LeftMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Left Grip Motion Controller"));
	if (LeftMotionController)
	{
		LeftMotionController->SetupAttachment(RootComponent);
		LeftMotionController->Hand = EControllerHand::Left;
	}

	RightMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Right Grip Motion Controller"));
	if (RightMotionController)
	{
		RightMotionController->SetupAttachment(RootComponent);
		RightMotionController->Hand = EControllerHand::Right;
	}

}