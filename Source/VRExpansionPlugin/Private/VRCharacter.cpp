// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"

#include "VRCharacter.h"


AVRCharacter::AVRCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVRRootComponent>(ACharacter::CapsuleComponentName))
{

	if (UVRRootComponent * Capsule = Cast<UVRRootComponent>(GetCapsuleComponent()))
	{
		Capsule->VRCapsuleOffset = FVector(0.0f, 0.0f, 0.0f);
		Capsule->SetCapsuleSize(20.0f, 96.0f);
	}

	VRReplicatedCamera = CreateDefaultSubobject<UReplicatedVRCameraComponent>(TEXT("VR Camera"));
	if (VRReplicatedCamera)
	{
	}
		
	VRRelativeComponent = CreateDefaultSubobject<UParentRelativeAttachmentComponent>(TEXT("Relative Attachment"));
	if (VRRelativeComponent)
	{
		VRRelativeComponent->SetupAttachment(VRReplicatedCamera);
		if (USkeletalMeshComponent * Mesh = GetMesh())
		{
			Mesh->SetupAttachment(VRRelativeComponent);
		}
	}


	LeftMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Left Motion Controller"));
	if (LeftMotionController)
	{
		LeftMotionController->Hand = EControllerHand::Left;
		LeftMotionController->SetupAttachment(RootComponent);
	}

	RightMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Right Motion Controller"));
	if (RightMotionController)
	{
		RightMotionController->Hand = EControllerHand::Right;
		RightMotionController->SetupAttachment(RootComponent);
	}
}