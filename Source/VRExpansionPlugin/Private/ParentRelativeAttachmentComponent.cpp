// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"
#include "KismetMathLibrary.h"
#include "ParentRelativeAttachmentComponent.h"


UParentRelativeAttachmentComponent::UParentRelativeAttachmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);
	YawTolerance = 0.0f;
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (this->GetAttachParent())
	{
		FRotator InverseRot = GetAttachParent()->GetComponentRotation();

		FRotator Inversey = InverseRot.GetInverse();

		InverseRot = UKismetMathLibrary::ComposeRotators(InverseRot, FRotator(Inversey.Pitch,0,0));
		InverseRot = UKismetMathLibrary::ComposeRotators(InverseRot, FRotator(0, 0, Inversey.Roll));

		if ((FPlatformMath::Abs(InverseRot.Yaw - LastRot.Yaw)) < YawTolerance)
		{
			SetWorldRotation(FRotator(0, LastRot.Yaw, 0).Quaternion(), false);
			return;
		}

		LastRot = InverseRot;
		SetWorldRotation(FRotator(0, InverseRot.Yaw, 0).Quaternion(), false);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}