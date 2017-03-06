// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Movement.cpp: Character movement implementation

=============================================================================*/

#include "VRExpansionPluginPrivatePCH.h"
#include "VRBaseCharacterMovementComponent.h"
#include "VRBPDataTypes.h"


UVRBaseCharacterMovementComponent::UVRBaseCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	AdditionalVRInputVector = FVector::ZeroVector;	
	CustomVRInputVector = FVector::ZeroVector;
	VRClimbingStepHeight = 96.0f;
	VRClimbingStepUpMultiplier = 2.0f;
	VRClimbingSetFallOnStepUp = true;
	VRClimbingMaxReleaseVelocitySize = 800.0f;

	VRWallSlideScaler = 1.0f;
}


float UVRBaseCharacterMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& InNormal, FHitResult& Hit, bool bHandleImpact)
{
	// Am running the CharacterMovementComponents calculations manually here now prior to scaling down the delta

	if (!Hit.bBlockingHit)
	{
		return 0.f;
	}

	FVector Normal(InNormal);
	if (IsMovingOnGround())
	{
		// We don't want to be pushed up an unwalkable surface.
		if (Normal.Z > 0.f)
		{
			if (!IsWalkable(Hit))
			{
				Normal = Normal.GetSafeNormal2D();
			}
		}
		else if (Normal.Z < -KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule.
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.f && (FloorNormal.Z < 1.f - DELTA);
				if (bFloorOpposedToMovement)
				{
					Normal = FloorNormal;
				}

				Normal = Normal.GetSafeNormal2D();
			}
		}
	}


	/*if ((Delta | InNormal) <= -0.2)
	{

	}*/

	// If the movement mode is one where sliding is an issue in VR, scale the delta by the custom scaler now
	// that we have already validated the floor normal.
	// Otherwise just pass in as normal, either way skip the parents implementation as we are doing it now.
	if (IsMovingOnGround() || (MovementMode == MOVE_Custom && CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing))
		return Super::Super::SlideAlongSurface(Delta * VRWallSlideScaler, Time, Normal, Hit, bHandleImpact);
	else
		return Super::Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);

}

void UVRBaseCharacterMovementComponent::AddCustomReplicatedMovement(FVector Movement)
{
	CustomVRInputVector += Movement;
}


void UVRBaseCharacterMovementComponent::ApplyVRMotionToVelocity(float deltaTime)
{
	if (AdditionalVRInputVector.IsNearlyZero())
	{
		LastPreAdditiveVRVelocity = FVector::ZeroVector;
		return;
	}

	LastPreAdditiveVRVelocity = (AdditionalVRInputVector) / deltaTime;// Velocity; // Save off pre-additive Velocity for restoration next tick	
	Velocity += LastPreAdditiveVRVelocity;

	// Switch to Falling if we have vertical velocity from root motion so we can lift off the ground
	if( !LastPreAdditiveVRVelocity.IsNearlyZero() && LastPreAdditiveVRVelocity.Z != 0.f && IsMovingOnGround() )
	{
		float LiftoffBound;
		// Default bounds - the amount of force gravity is applying this tick
		LiftoffBound = FMath::Max(GetGravityZ() * deltaTime, SMALL_NUMBER);

		if(LastPreAdditiveVRVelocity.Z > LiftoffBound )
		{
			SetMovementMode(MOVE_Falling);
		}
	}
}

void UVRBaseCharacterMovementComponent::RestorePreAdditiveVRMotionVelocity()
{
	Velocity -= LastPreAdditiveVRVelocity;
	LastPreAdditiveVRVelocity = FVector::ZeroVector;
}


void UVRBaseCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	switch (CustomMovementMode)
	{
	case EVRCustomMovementMode::VRMOVE_Climbing:
		PhysCustom_Climbing(deltaTime, Iterations);
		break;
	default:
		break;
	}
}

bool UVRBaseCharacterMovementComponent::VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult)
{
	return StepUp(GravDir, Delta, InHit, OutStepDownResult);
}

void UVRBaseCharacterMovementComponent::PhysCustom_Climbing(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	//RestorePreAdditiveRootMotionVelocity();
	//RestorePreAdditiveVRMotionVelocity();

	/*if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		if (bCheatFlying && Acceleration.IsZero())
		{
			Velocity = FVector::ZeroVector;
		}
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction;
		CalcVelocity(deltaTime, Friction, true, BrakingDecelerationFlying);
	}*/

	// I am forcing this to 0 to avoid some legacy velocity coming out of other movement modes, climbing should only be direct movement anyway.
	Velocity = FVector::ZeroVector;

	// Root motion should never happen in VR, lets just not even check for it
	//ApplyRootMotionToVelocity(deltaTime);
	//ApplyVRMotionToVelocity(deltaTime);

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = /*(Velocity * deltaTime) + */CustomVRInputVector + AdditionalVRInputVector;

	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);
	bool bSteppedUp = false;

	if (Hit.Time < 1.f)
	{
		const FVector GravDir = FVector(0.f, 0.f, -1.f);
		const FVector VelDir = Velocity.GetSafeNormal();
		const float UpDown = GravDir | VelDir;

		//bool bSteppedUp = false;
		if ((FMath::Abs(Hit.ImpactNormal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit))
		{
			float stepZ = UpdatedComponent->GetComponentLocation().Z;

			// Instead of remaking the step up function, temp assign a custom step height and then fall back to the old one afterward
			// This isn't the "proper" way to do it, but it saves on re-making stepup() for both vr characters seperatly (due to different hmd injection)
			float OldMaxStepHeight = MaxStepHeight;
			MaxStepHeight = VRClimbingStepHeight;

			// Making it easier to step up here with the multiplier, helps avoid falling back off
			bSteppedUp = VRClimbStepUp(GravDir, (Adjusted * VRClimbingStepUpMultiplier) * (1.f - Hit.Time), Hit);

			MaxStepHeight = OldMaxStepHeight;

			if (bSteppedUp)
			{
				OldLocation.Z = UpdatedComponent->GetComponentLocation().Z + (OldLocation.Z - stepZ);
			}
		}

		if (!bSteppedUp)
		{
			//adjust and try again
			HandleImpact(Hit, deltaTime, Adjusted);
			SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
		}
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = ((UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime).GetClampedToMaxSize(VRClimbingMaxReleaseVelocitySize);
	}

	if (bSteppedUp)
	{
		if (AVRBaseCharacter * ownerCharacter = Cast<AVRBaseCharacter>(CharacterOwner))
		{
			if (VRClimbingSetFallOnStepUp)
			{
				// Force falling movement, this prevents climbing interfering with stepup.
				SetMovementMode(MOVE_Falling); 
				StartNewPhysics(0.0f, Iterations);
			}

			// Notify the end user that they probably want to stop gripping now
			ownerCharacter->OnClimbingSteppedUp();
		}
	}
}

void UVRBaseCharacterMovementComponent::SetClimbingMode(bool bIsClimbing)
{
	bStartedClimbing = bIsClimbing;
	bEndedClimbing = !bIsClimbing;
}

void UVRBaseCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	// Run bToggledClimbingMode in here
	if (bStartedClimbing)
	{
		SetMovementMode(EMovementMode::MOVE_Custom, (uint8)EVRCustomMovementMode::VRMOVE_Climbing);
		bStartedClimbing = false;
	}
	else if (bEndedClimbing)
	{
		SetMovementMode(EMovementMode::MOVE_Falling);
		bEndedClimbing = false;
	}

	Super::PerformMovement(DeltaSeconds);

	// Make sure these are cleaned out for the next frame
	AdditionalVRInputVector = FVector::ZeroVector;
	CustomVRInputVector = FVector::ZeroVector;
}

void FSavedMove_VRBaseCharacter::SetInitialPosition(ACharacter* C)
{
	// See if we can get the VR capsule location
	if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(C))
	{
		if (UVRBaseCharacterMovementComponent * moveComp = Cast<UVRBaseCharacterMovementComponent>(VRC->GetMovementComponent()))
		{
			bStartedClimbing = moveComp->bStartedClimbing;
			bEndedClimbing = moveComp->bEndedClimbing;
		}
		else
		{
			bStartedClimbing = false;
			bEndedClimbing = false;
		}
	}
	else
	{
		bStartedClimbing = false;
		bEndedClimbing = false;
	}

	FSavedMove_Character::SetInitialPosition(C);
}

void FSavedMove_VRBaseCharacter::Clear()
{
	bStartedClimbing = false;
	bEndedClimbing = false;

	FSavedMove_Character::Clear();
}