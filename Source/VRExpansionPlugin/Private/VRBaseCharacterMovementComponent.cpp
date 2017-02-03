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
	AdditionalVRInputVector = FVector::ZeroVector;	
	CustomVRInputVector = FVector::ZeroVector;
	VRClimbingStepHeight = 96.0f;
	VRClimbingStepUpMultiplier = 2.0f;
	VRClimbingSetFallOnStepUp = true;
	VRClimbingMaxReleaseVelocitySize = 800.0f;
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

void UVRBaseCharacterMovementComponent::PhysCustom_Climbing(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (false)//!CustomVRInputVector.IsNearlyZero())
	{
		// Trying using fly movement so that it supports sliding now instead and step up.
		// Need to test
		FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FVector Adjusted = CustomVRInputVector;// / DeltaSeconds;
		FHitResult Hit1(1.f);
		SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit1);

		if (Hit1.Time < 1.f)
		{
			const FVector GravDir = FVector(0.f, 0.f, -1.f);
			const FVector VelDir = Velocity.GetSafeNormal();
			const float UpDown = GravDir | VelDir;

			bool bSteppedUp = false;
			if ((FMath::Abs(Hit1.ImpactNormal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit1))
			{
				float stepZ = UpdatedComponent->GetComponentLocation().Z;
				bSteppedUp = StepUp(GravDir, Adjusted * (1.f - Hit1.Time), Hit1, nullptr);
				if (bSteppedUp)
				{
					OldLocation.Z = UpdatedComponent->GetComponentLocation().Z + (OldLocation.Z - stepZ);
				}
			}

			if (!bSteppedUp)
			{
				// Just block with direct movement
				//adjust and try again
				HandleImpact(Hit1, deltaTime, Adjusted);
				SlideAlongSurface(Adjusted, (1.f - Hit1.Time), Hit1.Normal, Hit1, true);
			}
		}

		CustomVRInputVector = FVector::ZeroVector;
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
	ApplyVRMotionToVelocity(deltaTime);

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
			bSteppedUp = StepUp(GravDir, (Adjusted * VRClimbingStepUpMultiplier) * (1.f - Hit.Time), Hit);

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

	CustomVRInputVector = FVector::ZeroVector;
}
