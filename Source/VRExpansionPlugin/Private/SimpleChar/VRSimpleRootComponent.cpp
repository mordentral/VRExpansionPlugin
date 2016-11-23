// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"

#include "PhysicsPublic.h"

#if WITH_PHYSX
#include "PhysXSupport.h"
#endif // WITH_PHYSX

#include "Components/PrimitiveComponent.h"

#include "VRSimpleRootComponent.h"

#define LOCTEXT_NAMESPACE "VRSimpleRootComponent"


UVRSimpleRootComponent::UVRSimpleRootComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);

	VRCapsuleOffset = FVector(0.0f, 0.0f, 0.0f);
	
	ShapeColor = FColor(223, 149, 157, 255);

	CapsuleRadius = 20.0f;
	CapsuleHalfHeight = 96.0f;
	bUseEditorCompositing = true;
	
	// Fixes a problem where headset stays at 0,0,0
	lastCameraLoc = FVector::ZeroVector;
	lastCameraRot = FRotator::ZeroRotator;
	curCameraRot = FRotator::ZeroRotator;
	curCameraLoc = FVector::ZeroVector;
	TargetPrimitiveComponent = NULL;
	//VRCameraCollider = NULL;

	bUseWalkingCollisionOverride = false;
	WalkingCollisionOverride = ECollisionChannel::ECC_Pawn;

	CanCharacterStepUpOn = ECB_No;
	bShouldUpdatePhysicsVolume = true;
	bCheckAsyncSceneOnMove = false;
	SetCanEverAffectNavigation(false);
	bDynamicObstacle = true;
}

void UVRSimpleRootComponent::BeginPlay()
{
	Super::BeginPlay();

	TargetPrimitiveComponent = NULL;
	MovementComponent = NULL;

	if(AVRSimpleCharacter * vrOwner = Cast<AVRSimpleCharacter>(this->GetOwner()))
	{ 
		TargetPrimitiveComponent = vrOwner->VRReplicatedCamera;
		MovementComponent = vrOwner->VRMovementReference;

		return;
	}
	else
	{
		TArray<USceneComponent*> children = this->GetAttachChildren();

		for (int i = 0; i < children.Num(); i++)
		{
			if (children[i]->IsA(UCameraComponent::StaticClass()))
			{
				TargetPrimitiveComponent = children[i];
				//return;
			}
			else if(children[i]->IsA(UVRSimpleCharacterMovementComponent::StaticClass()))
			{
				MovementComponent = Cast<UVRSimpleCharacterMovementComponent>(children[i]);
			}
		}
	}
}

void UVRSimpleRootComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{		
	if (IsLocallyControlled())
	{
		bool bWasDirectHead = false;

		if (IsLocallyControlled() && GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed())
		{
			bWasDirectHead = true;
			FQuat curRot;
			GEngine->HMDDevice->GetCurrentOrientationAndPosition(curRot, curCameraLoc);
			curCameraRot = curRot.Rotator();
		}
		else if (TargetPrimitiveComponent)
		{
			curCameraRot = TargetPrimitiveComponent->RelativeRotation;
			curCameraLoc = TargetPrimitiveComponent->RelativeLocation;
		}
		else
		{
			curCameraRot = FRotator::ZeroRotator;
			curCameraLoc = FVector::ZeroVector;
		}

		// Can adjust the relative tolerances to remove jitter and some update processing
		if (!(curCameraLoc - lastCameraLoc).IsNearlyZero(0.001f) || !(curCameraRot - lastCameraRot).IsNearlyZero(0.001f))
		{

			DifferenceFromLastFrame = (curCameraLoc - lastCameraLoc);
			DifferenceFromLastFrame.Z = 0.0f;
		

			if (!bWasDirectHead)
			{
				TargetPrimitiveComponent->SetRelativeLocation(FVector(0,0,TargetPrimitiveComponent->RelativeLocation.Z));
			}
			else
			{
				this->SetRelativeRotation(curCameraRot);
			}

			if (MovementComponent)
			{
				//MovementComponent->SetRequestedVelocity(this->GetComponentRotation().RotateVector(DifferenceFromLastFrame));
				MovementComponent->AdditionalVRInputVector = this->GetComponentRotation().RotateVector(DifferenceFromLastFrame); // Apply over a second
			}
		}
		else
		{
			MovementComponent->AdditionalVRInputVector = FVector::ZeroVector;
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


#if WITH_EDITOR
void UVRSimpleRootComponent::PreEditChange(UProperty* PropertyThatWillChange)
{
	// This is technically not correct at all to do...however when overloading a root component the preedit gets called twice for some reason.
	// Calling it twice attempts to double register it in the list and causes an assert to be thrown.
	if (this->GetOwner()->IsA(AVRSimpleCharacter::StaticClass()))
		return;	
	else
		Super::PreEditChange(PropertyThatWillChange);
}

void UVRSimpleRootComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// We only want to modify the property that was changed at this point
	// things like propagation from CDO to instances don't work correctly if changing one property causes a different property to change
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UVRSimpleRootComponent, CapsuleHalfHeight))
	{
		CapsuleHalfHeight = FMath::Max3(0.f, CapsuleHalfHeight, CapsuleRadius);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UVRSimpleRootComponent, CapsuleRadius))
	{
		CapsuleRadius = FMath::Clamp(CapsuleRadius, 0.f, CapsuleHalfHeight);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UVRSimpleRootComponent, VRCapsuleOffset))
	{
	}

	if (!IsTemplate())
	{
		//UpdateBodySetup(); // do this before reregistering components so that new values are used for collision
	}

	return;

	// Overrode the defaults for this, don't call the parent
	//Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif	// WITH_EDITOR


