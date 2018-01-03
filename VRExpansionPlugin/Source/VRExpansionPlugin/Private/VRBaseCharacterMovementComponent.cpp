// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Movement.cpp: Character movement implementation

=============================================================================*/

#include "VRBaseCharacterMovementComponent.h"
#include "VRBPDatatypes.h"
#include "VRBaseCharacter.h"
#include "GameFramework/PhysicsVolume.h"


UVRBaseCharacterMovementComponent::UVRBaseCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	// Set Acceleration and braking deceleration walking to high values to avoid ramp up on speed
	// Realized that I wasn't doing this here for people to default to no acceleration.
	this->bRequestedMoveUseAcceleration = false;
	this->MaxAcceleration = 200048.0f;
	this->BrakingDecelerationWalking = 200048.0f;

	AdditionalVRInputVector = FVector::ZeroVector;	
	CustomVRInputVector = FVector::ZeroVector;
	bApplyAdditionalVRInputVectorAsNegative = true;
	VRClimbingStepHeight = 96.0f;
	VRClimbingEdgeRejectDistance = 5.0f;
	VRClimbingStepUpMultiplier = 1.0f;
	VRClimbingMaxReleaseVelocitySize = 800.0f;
	SetDefaultPostClimbMovementOnStepUp = true;
	DefaultPostClimbMovement = EVRConjoinedMovementModes::C_MOVE_Falling;

	bIgnoreSimulatingComponentsInFloorCheck = true;
	VRReplicateCapsuleHeight = false;

	VRWallSlideScaler = 1.0f;
	VRLowGravWallFrictionScaler = 1.0f;
	VRLowGravIgnoresDefaultFluidFriction = true;

	VREdgeRejectDistance = 0.01f; // Rounded minimum of root movement

	VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;

	NetworkSmoothingMode = ENetworkSmoothingMode::Disabled;
	NetworkSimulatedSmoothRotationTime = 0.0f; // Don't smooth rotation, its not good

	bWasInPushBack = false;
	bIsInPushBack = false;
}

void UVRBaseCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{

	if (MovementMode == MOVE_Custom && CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated)
	{
		const FVector InputVector = ConsumeInputVector();
		if (!HasValidData() || ShouldSkipUpdate(DeltaTime))
		{
			return;
		}

		// Skip the perform movement logic, run the re-seat logic instead - running base movement component tick instead
		Super::Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

		// See if we fell out of the world.
		//const bool bIsSimulatingPhysics = UpdatedComponent->IsSimulatingPhysics();
		//if (CharacterOwner->Role == ROLE_Authority && (!bCheatFlying || bIsSimulatingPhysics) && !CharacterOwner->CheckStillInWorld())
		//{
		//	return;
		//}

		// If we are the owning client or the server then run the re-basing
		if (CharacterOwner->Role > ROLE_SimulatedProxy)
		{
			// Run offset logic here, the server will update simulated proxies with the movement replication
			if (AVRBaseCharacter * BaseChar = Cast<AVRBaseCharacter>(CharacterOwner))
			{
				BaseChar->TickSeatInformation(DeltaTime);
			}

		}

	}
	else
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UVRBaseCharacterMovementComponent::StartPushBackNotification(FHitResult HitResult)
{
	bIsInPushBack = true;

	if (bWasInPushBack)
		return;

	bWasInPushBack = true;

	if (AVRBaseCharacter * OwningCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		OwningCharacter->OnBeginWallPushback(HitResult, !Acceleration.Equals(FVector::ZeroVector), AdditionalVRInputVector);
	}
}

void UVRBaseCharacterMovementComponent::EndPushBackNotification()
{
	if (bIsInPushBack || !bWasInPushBack)
		return;

	bIsInPushBack = false;
	bWasInPushBack = false;

	if (AVRBaseCharacter * OwningCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		OwningCharacter->OnEndWallPushback();
	}
}

// Rewind the players position by the new capsule location
void UVRBaseCharacterMovementComponent::RewindVRRelativeMovement()
{
	//FHitResult AHit;
	MoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false);
	//SafeMoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false, AHit);
}

bool UVRBaseCharacterMovementComponent::FloorSweepTest(
	FHitResult& OutHit,
	const FVector& Start,
	const FVector& End,
	ECollisionChannel TraceChannel,
	const struct FCollisionShape& CollisionShape,
	const struct FCollisionQueryParams& Params,
	const struct FCollisionResponseParams& ResponseParam
) const
{
	bool bBlockingHit = false;
	TArray<FHitResult> OutHits;

	if (!bUseFlatBaseForFloorChecks)
	{
		if (bIgnoreSimulatingComponentsInFloorCheck)
		{
			// Testing all components in the way, skipping simulating components
			GetWorld()->SweepMultiByChannel(OutHits, Start, End, FQuat::Identity, TraceChannel, CollisionShape, Params, ResponseParam);

			for (int i = 0; i < OutHits.Num(); i++)
			{
				if (OutHits[i].bBlockingHit && (OutHits[i].Component.IsValid() && !OutHits[i].Component->IsSimulatingPhysics()))
				{
					OutHit = OutHits[i];
					bBlockingHit = true;
					break;
				}
			}
		}
		else
			bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity, TraceChannel, CollisionShape, Params, ResponseParam);
	}
	else
	{
		// Test with a box that is enclosed by the capsule.
		const float CapsuleRadius = CollisionShape.GetCapsuleRadius();
		const float CapsuleHeight = CollisionShape.GetCapsuleHalfHeight();
		const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(CapsuleRadius * 0.707f, CapsuleRadius * 0.707f, CapsuleHeight));

		// First test with the box rotated so the corners are along the major axes (ie rotated 45 degrees).
		//TArray<FHitResult> OutHits;
		OutHits.Reset();

		if (bIgnoreSimulatingComponentsInFloorCheck)
		{
			// Testing all components in the way, skipping simulating components
			GetWorld()->SweepMultiByChannel(OutHits, Start, End, FQuat(FVector(0.f, 0.f, -1.f), PI * 0.25f), TraceChannel, BoxShape, Params, ResponseParam);

			for (int i = 0; i < OutHits.Num(); i++)
			{
				if (OutHits[i].bBlockingHit && (OutHits[i].Component.IsValid() && !OutHits[i].Component->IsSimulatingPhysics()))
				{
					OutHit = OutHits[i];
					bBlockingHit = true;
					break;
				}
			}
		}
		else
			bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat(FVector(0.f, 0.f, -1.f), PI * 0.25f), TraceChannel, BoxShape, Params, ResponseParam);

		if (!bBlockingHit)
		{
			// Test again with the same box, not rotated.
			OutHit.Reset(1.f, false);

			if (bIgnoreSimulatingComponentsInFloorCheck)
			{
				OutHits.Reset();
				// Testing all components in the way, skipping simulating components
				GetWorld()->SweepMultiByChannel(OutHits, Start, End, FQuat::Identity, TraceChannel, BoxShape, Params, ResponseParam);

				for (int i = 0; i < OutHits.Num(); i++)
				{
					if (OutHits[i].bBlockingHit && (OutHits[i].Component.IsValid() && !OutHits[i].Component->IsSimulatingPhysics()))
					{
						OutHit = OutHits[i];
						bBlockingHit = true;
						break;
					}
				}
			}
			else
				bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity, TraceChannel, BoxShape, Params, ResponseParam);
		}
	}

	return bBlockingHit;
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

	StartPushBackNotification(Hit);

	// If the movement mode is one where sliding is an issue in VR, scale the delta by the custom scaler now
	// that we have already validated the floor normal.
	// Otherwise just pass in as normal, either way skip the parents implementation as we are doing it now.
	if (IsMovingOnGround() || (MovementMode == MOVE_Custom && CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing))
		return Super::Super::SlideAlongSurface(Delta * VRWallSlideScaler, Time, Normal, Hit, bHandleImpact);
	else
		return Super::Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);


}

void UVRBaseCharacterMovementComponent::SetCrouchedHalfHeight(float NewCrouchedHalfHeight)
{
	this->CrouchedHalfHeight = NewCrouchedHalfHeight;
}

void UVRBaseCharacterMovementComponent::AddCustomReplicatedMovement(FVector Movement)
{
	// if we are a client then lets round this to match what it will be after net Replication
	if (GetNetMode() == NM_Client)
		CustomVRInputVector += RoundDirectMovement(Movement);
	else
		CustomVRInputVector += Movement; // If not a client, don't bother to round this down.
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_SnapTurn(float DeltaYawAngle)
{
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_SnapTurn;
	MoveAction.MoveActionRot = FRotator(0.0f, DeltaYawAngle, 0.0f);
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_Teleport(FVector TeleportLocation, FRotator TeleportRotation)
{
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_Teleport;
	MoveAction.MoveActionLoc = TeleportLocation;
	MoveAction.MoveActionRot = TeleportRotation;
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_StopAllMovement()
{
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_StopAllMovement;
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_Custom(EVRMoveAction MoveActionToPerform, EVRMoveActionDataReq DataRequirementsForMoveAction, FVector MoveActionVector, FRotator MoveActionRotator)
{
	MoveAction.MoveAction = MoveActionToPerform;
	MoveAction.MoveActionLoc = MoveActionVector;
	MoveAction.MoveActionRot = MoveActionRotator;
	MoveAction.MoveActionDataReq = DataRequirementsForMoveAction;
}

bool UVRBaseCharacterMovementComponent::CheckForMoveAction()
{
	switch (MoveAction.MoveAction)
	{
	case EVRMoveAction::VRMOVEACTION_SnapTurn: 
	{
		return DoMASnapTurn();
	}break;
	case EVRMoveAction::VRMOVEACTION_Teleport:
	{
		return DoMATeleport(); 
	}break;
	case EVRMoveAction::VRMOVEACTION_StopAllMovement:
	{
		return DoMAStopAllMovement();
	}break;
	case EVRMoveAction::VRMOVEACTION_Reserved1:
	case EVRMoveAction::VRMOVEACTION_None:
	{}break;
	default: // All other move actions (CUSTOM)
	{
		if (AVRBaseCharacter * OwningCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
		{
			OwningCharacter->OnCustomMoveActionPerformed(MoveAction.MoveAction, MoveAction.MoveActionLoc, MoveAction.MoveActionRot);
		}
	}break;
	}

	return false;
}

bool UVRBaseCharacterMovementComponent::DoMASnapTurn()
{
	if (AVRBaseCharacter * OwningCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		if (!IsLocallyControlled())
		{
			OwningCharacter->AddActorWorldOffset(MoveAction.MoveActionLoc);
			return true;
		}
		else
		{
			MoveAction.MoveActionLoc = OwningCharacter->AddActorWorldRotationVR(MoveAction.MoveActionRot, true);
			return true;
		}
	}

	return false;
}

bool UVRBaseCharacterMovementComponent::DoMATeleport()
{
	if (AVRBaseCharacter * OwningCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		if (!IsLocallyControlled())
		{
			OwningCharacter->TeleportTo(MoveAction.MoveActionLoc, OwningCharacter->GetActorRotation(), false, false);
			return true;
		}
		else
		{
			AController* OwningController = OwningCharacter->GetController();

			if (!OwningController)
			{
				MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_None;
				return false;
			}

			OwningCharacter->TeleportTo(MoveAction.MoveActionLoc, OwningCharacter->GetActorRotation(), false, false);

			if (OwningCharacter->bUseControllerRotationYaw)
				OwningController->SetControlRotation(MoveAction.MoveActionRot);

			return true;
		}
	}

	return false;
}

bool UVRBaseCharacterMovementComponent::DoMAStopAllMovement()
{
	if (AVRBaseCharacter * OwningCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		this->StopMovementImmediately();
		return true;
	}

	return false;
}

void UVRBaseCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	switch (static_cast<EVRCustomMovementMode>(CustomMovementMode))
	{
	case EVRCustomMovementMode::VRMOVE_Climbing:
		PhysCustom_Climbing(deltaTime, Iterations);
		break;
	case EVRCustomMovementMode::VRMOVE_LowGrav:
		PhysCustom_LowGrav(deltaTime, Iterations);
		break;
	case EVRCustomMovementMode::VRMOVE_Seated:
		break;
	default:
		Super::PhysCustom(deltaTime, Iterations);
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

	// I am forcing this to 0 to avoid some legacy velocity coming out of other movement modes, climbing should only be direct movement anyway.
	Velocity = FVector::ZeroVector;

	if (bApplyAdditionalVRInputVectorAsNegative)
	{
		// Rewind the players position by the new capsule location
		RewindVRRelativeMovement();
	}

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = /*(Velocity * deltaTime) + */CustomVRInputVector;
	FVector Delta = Adjusted + AdditionalVRInputVector;
	bool bZeroDelta = Delta.IsNearlyZero();

	FStepDownResult StepDownResult;

	// Instead of remaking the step up function, temp assign a custom step height and then fall back to the old one afterward
	// This isn't the "proper" way to do it, but it saves on re-making stepup() for both vr characters seperatly (due to different hmd injection)
	float OldMaxStepHeight = MaxStepHeight;
	MaxStepHeight = VRClimbingStepHeight;
	bool bSteppedUp = false;

	if (!bZeroDelta)
	{
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

		if (Hit.Time < 1.f)
		{
			const FVector GravDir = FVector(0.f, 0.f, -1.f);
			const FVector VelDir = (CustomVRInputVector).GetSafeNormal();//Velocity.GetSafeNormal();
			const float UpDown = GravDir | VelDir;

			//bool bSteppedUp = false;
			if ((FMath::Abs(Hit.ImpactNormal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit))
			{
				float stepZ = UpdatedComponent->GetComponentLocation().Z;

				// Making it easier to step up here with the multiplier, helps avoid falling back off
				bSteppedUp = VRClimbStepUp(GravDir, ((Adjusted * VRClimbingStepUpMultiplier) + AdditionalVRInputVector) * (1.f - Hit.Time), Hit, &StepDownResult);

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
	}

	// Revert to old max step height
	MaxStepHeight = OldMaxStepHeight;

	if (bSteppedUp)
	{
		if (AVRBaseCharacter * ownerCharacter = Cast<AVRBaseCharacter>(CharacterOwner))
		{
			if (SetDefaultPostClimbMovementOnStepUp)
			{
				// Takes effect next frame, this allows server rollback to correctly handle auto step up
				SetReplicatedMovementMode(DefaultPostClimbMovement);
				// Before doing this the server could rollback the client from a bad step up and leave it hanging in climb mode
				// This way the rollback replay correctly sets the movement mode from the step up request

				Velocity = FVector::ZeroVector;
			}

			// Notify the end user that they probably want to stop gripping now
			ownerCharacter->OnClimbingSteppedUp();
		}
	}

	// Update floor.
	// StepUp might have already done it for us.
	if (StepDownResult.bComputedFloor)
	{
		CurrentFloor = StepDownResult.FloorResult;
	}
	else
	{
		FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, NULL);
	}

	if (CurrentFloor.IsWalkableFloor())
	{
		if(CurrentFloor.GetDistanceToFloor() < (MIN_FLOOR_DIST + MAX_FLOOR_DIST) / 2)
			AdjustFloorHeight();

		SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
	}
	else if (CurrentFloor.HitResult.bStartPenetrating)
	{
		// The floor check failed because it started in penetration
		// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
		FHitResult Hit(CurrentFloor.HitResult);
		Hit.TraceEnd = Hit.TraceStart + FVector(0.f, 0.f, MAX_FLOOR_DIST);
		const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
		ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
		bForceNextFloorCheck = true;
	}

	if(!bSteppedUp || !SetDefaultPostClimbMovementOnStepUp)
	{
		if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			Velocity = (((UpdatedComponent->GetComponentLocation() - OldLocation) - AdditionalVRInputVector) / deltaTime).GetClampedToMaxSize(VRClimbingMaxReleaseVelocitySize);
		}
	}
}

void UVRBaseCharacterMovementComponent::PhysCustom_LowGrav(float deltaTime, int32 Iterations)
{

	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	float Friction = 0.0f; 

	// If we are not in the default physics volume then accept the custom fluid friction setting
	// I set this by default to be ignored as many will not alter the default fluid friction
	if(!VRLowGravIgnoresDefaultFluidFriction || GetWorld()->GetDefaultPhysicsVolume() != GetPhysicsVolume())
		Friction = 0.5f * GetPhysicsVolume()->FluidFriction;

	CalcVelocity(deltaTime, Friction, true, 0.0f);

	if (bApplyAdditionalVRInputVectorAsNegative)
	{
		// Rewind the players position by the new capsule location
		RewindVRRelativeMovement();
	}

	// Adding in custom VR input vector here, can be used for custom movement during it
	// AddImpulse is not multiplayer compatible client side
	Velocity += CustomVRInputVector; 

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = (Velocity * deltaTime);
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted + AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (Hit.Time < 1.f)
	{
		// Still running step up with grav dir
		const FVector GravDir = FVector(0.f, 0.f, -1.f);
		const FVector VelDir = Velocity.GetSafeNormal();
		const float UpDown = GravDir | VelDir;

		bool bSteppedUp = false;
		if ((FMath::Abs(Hit.ImpactNormal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit))
		{
			float stepZ = UpdatedComponent->GetComponentLocation().Z;
			bSteppedUp = StepUp(GravDir, Adjusted * (1.f - Hit.Time), Hit);
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

		if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			Velocity = (((UpdatedComponent->GetComponentLocation() - OldLocation) - AdditionalVRInputVector) / deltaTime) * VRLowGravWallFrictionScaler;
		}
	}
	else
	{
		if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			Velocity = (((UpdatedComponent->GetComponentLocation() - OldLocation) - AdditionalVRInputVector) / deltaTime);
		}
	}
}


void UVRBaseCharacterMovementComponent::SetClimbingMode(bool bIsClimbing)
{
	if (bIsClimbing)
		VRReplicatedMovementMode = EVRConjoinedMovementModes::C_VRMOVE_Climbing;
	else
		VRReplicatedMovementMode = DefaultPostClimbMovement;
}

void UVRBaseCharacterMovementComponent::SetReplicatedMovementMode(EVRConjoinedMovementModes NewMovementMode)
{
	// Only have up to 15 that it can go up to, the previous 7 index's are used up for std movement modes
	VRReplicatedMovementMode = NewMovementMode;
}


/*void UVRBaseCharacterMovementComponent::ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration)
{
	Super::ReplicateMoveToServer(DeltaTime, NewAcceleration);
}*/

/*void UVRBaseCharacterMovementComponent::SendClientAdjustment()
{
	if (!HasValidData())
	{
		return;
	}

	FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	check(ServerData);

	if (ServerData->PendingAdjustment.TimeStamp <= 0.f)
	{
		return;
	}

	if (ServerData->PendingAdjustment.bAckGoodMove == true)
	{
		// just notify client this move was received
		ClientAckGoodMove(ServerData->PendingAdjustment.TimeStamp);
	}
	else
	{
		const bool bIsPlayingNetworkedRootMotionMontage = CharacterOwner->IsPlayingNetworkedRootMotionMontage();
		if (HasRootMotionSources())
		{
			FRotator Rotation = ServerData->PendingAdjustment.NewRot.GetNormalized();
			FVector_NetQuantizeNormal CompressedRotation(Rotation.Pitch / 180.f, Rotation.Yaw / 180.f, Rotation.Roll / 180.f);
			ClientAdjustRootMotionSourcePosition
			(
				ServerData->PendingAdjustment.TimeStamp,
				CurrentRootMotion,
				bIsPlayingNetworkedRootMotionMontage,
				bIsPlayingNetworkedRootMotionMontage ? CharacterOwner->GetRootMotionAnimMontageInstance()->GetPosition() : -1.f,
				ServerData->PendingAdjustment.NewLoc,
				CompressedRotation,
				ServerData->PendingAdjustment.NewVel.Z,
				ServerData->PendingAdjustment.NewBase,
				ServerData->PendingAdjustment.NewBaseBoneName,
				ServerData->PendingAdjustment.NewBase != NULL,
				ServerData->PendingAdjustment.bBaseRelativePosition,
				PackNetworkMovementMode()
			);
		}
		else if (bIsPlayingNetworkedRootMotionMontage)
		{
			FRotator Rotation = ServerData->PendingAdjustment.NewRot.GetNormalized();
			FVector_NetQuantizeNormal CompressedRotation(Rotation.Pitch / 180.f, Rotation.Yaw / 180.f, Rotation.Roll / 180.f);
			ClientAdjustRootMotionPosition
			(
				ServerData->PendingAdjustment.TimeStamp,
				CharacterOwner->GetRootMotionAnimMontageInstance()->GetPosition(),
				ServerData->PendingAdjustment.NewLoc,
				CompressedRotation,
				ServerData->PendingAdjustment.NewVel.Z,
				ServerData->PendingAdjustment.NewBase,
				ServerData->PendingAdjustment.NewBaseBoneName,
				ServerData->PendingAdjustment.NewBase != NULL,
				ServerData->PendingAdjustment.bBaseRelativePosition,
				PackNetworkMovementMode()
			);
		}
		else if (ServerData->PendingAdjustment.NewVel.IsZero())
		{
			if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(GetOwner()))
			{
				FVector CusVec = VRC->GetVRLocation();
				GEngine->AddOnScreenDebugMessage(-1, 125.f, IsLocallyControlled() ? FColor::Red : FColor::Green, FString::Printf(TEXT("VrLoc: x: %f, y: %f, X: %f"), CusVec.X, CusVec.Y, CusVec.Z));
			}
			GEngine->AddOnScreenDebugMessage(-1, 125.f, FColor::Red, TEXT("Correcting Client Location!"));
			ClientVeryShortAdjustPosition
			(
				ServerData->PendingAdjustment.TimeStamp,
				ServerData->PendingAdjustment.NewLoc,
				ServerData->PendingAdjustment.NewBase,
				ServerData->PendingAdjustment.NewBaseBoneName,
				ServerData->PendingAdjustment.NewBase != NULL,
				ServerData->PendingAdjustment.bBaseRelativePosition,
				PackNetworkMovementMode()
			);
		}
		else
		{
			if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(GetOwner()))
			{
				FVector CusVec = VRC->GetVRLocation();
				GEngine->AddOnScreenDebugMessage(-1, 125.f, IsLocallyControlled() ? FColor::Red : FColor::Green, FString::Printf(TEXT("VrLoc: x: %f, y: %f, X: %f"), CusVec.X, CusVec.Y, CusVec.Z));
			}
			GEngine->AddOnScreenDebugMessage(-1, 125.f, FColor::Red, TEXT("Correcting Client Location!"));
			ClientAdjustPosition
			(
				ServerData->PendingAdjustment.TimeStamp,
				ServerData->PendingAdjustment.NewLoc,
				ServerData->PendingAdjustment.NewVel,
				ServerData->PendingAdjustment.NewBase,
				ServerData->PendingAdjustment.NewBaseBoneName,
				ServerData->PendingAdjustment.NewBase != NULL,
				ServerData->PendingAdjustment.bBaseRelativePosition,
				PackNetworkMovementMode()
			);
		}
	}

	ServerData->PendingAdjustment.TimeStamp = 0;
	ServerData->PendingAdjustment.bAckGoodMove = false;
	ServerData->bForceClientUpdate = false;
}
*/
void UVRBaseCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	if (VRReplicatedMovementMode != EVRConjoinedMovementModes::C_MOVE_MAX)//None)
	{
		if (VRReplicatedMovementMode <= EVRConjoinedMovementModes::C_MOVE_MAX)
		{
			// Is a default movement mode, just directly set it
			SetMovementMode((EMovementMode)VRReplicatedMovementMode);
		}
		else // Is Custom
		{
			// Auto calculates the difference for our VR movements, index is from 0 so using climbing should get me correct index's as it is the first custom mode
			SetMovementMode(EMovementMode::MOVE_Custom, (((int8)VRReplicatedMovementMode - (uint8)EVRConjoinedMovementModes::C_VRMOVE_Climbing)) );
		}

		// Clearing it here instead now, as this way the code can inject it during PerformMovement
		// Specifically used by the Climbing Step up, so that server rollbacks are supported
		VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;//None;
	}

	// Handle move actions here
	CheckForMoveAction();

	// Clear out this flag prior to movement so we can see if it gets changed
	bIsInPushBack = false;

	Super::PerformMovement(DeltaSeconds);

	EndPushBackNotification(); // Check if we need to notify of ending pushback

	// Make sure these are cleaned out for the next frame
	AdditionalVRInputVector = FVector::ZeroVector;
	CustomVRInputVector = FVector::ZeroVector;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_None;
}

void FSavedMove_VRBaseCharacter::SetInitialPosition(ACharacter* C)
{
	// See if we can get the VR capsule location
	if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(C))
	{
		if (UVRBaseCharacterMovementComponent * moveComp = Cast<UVRBaseCharacterMovementComponent>(VRC->GetMovementComponent()))
		{

			// Saving this out early because it will be wiped before the PostUpdate gets the values
			ConditionalValues.MoveAction.MoveAction = moveComp->MoveAction.MoveAction;

			VRReplicatedMovementMode = moveComp->VRReplicatedMovementMode;

			ConditionalValues.CustomVRInputVector = moveComp->CustomVRInputVector;

			if (moveComp->HasRequestedVelocity())
				ConditionalValues.RequestedVelocity = moveComp->RequestedVelocity;
			else
				ConditionalValues.RequestedVelocity = FVector::ZeroVector;
				
			// Throw out the Z value of the headset, its not used anyway for movement
			// Instead, re-purpose it to be the capsule half height
			if (moveComp->VRReplicateCapsuleHeight && C)
				LFDiff.Z = C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		}
		else
		{
			VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;//None;
			ConditionalValues.CustomVRInputVector = FVector::ZeroVector;
			ConditionalValues.RequestedVelocity = FVector::ZeroVector;
		}
	}
	else
	{
		VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;//None;
		ConditionalValues.CustomVRInputVector = FVector::ZeroVector;
	}

	FSavedMove_Character::SetInitialPosition(C);
}


void FSavedMove_VRBaseCharacter::PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode)
{
	FSavedMove_Character::PostUpdate(C, PostUpdateMode);

	if (ConditionalValues.MoveAction.MoveAction != EVRMoveAction::VRMOVEACTION_None)
	{
		// See if we can get the VR capsule location
		if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(C))
		{
			if (UVRBaseCharacterMovementComponent * moveComp = Cast<UVRBaseCharacterMovementComponent>(VRC->GetMovementComponent()))
			{
				// This is cleared out in perform movement so I need to save it before applying below
				EVRMoveAction tempAction = ConditionalValues.MoveAction.MoveAction;
				ConditionalValues.MoveAction = moveComp->MoveAction;
				ConditionalValues.MoveAction.MoveAction = tempAction;
			}
			else
			{
				ConditionalValues.MoveAction.Clear();
			}
		}
		else
		{
			ConditionalValues.MoveAction.Clear();
		}
	}
}

void FSavedMove_VRBaseCharacter::Clear()
{
	VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;// None;

	VRCapsuleLocation = FVector::ZeroVector;
	VRCapsuleRotation = FRotator::ZeroRotator;
	LFDiff = FVector::ZeroVector;

	ConditionalValues.CustomVRInputVector = FVector::ZeroVector;
	ConditionalValues.RequestedVelocity = FVector::ZeroVector;
	ConditionalValues.MoveAction.Clear();

	FSavedMove_Character::Clear();
}

void FSavedMove_VRBaseCharacter::PrepMoveFor(ACharacter* Character)
{
	UVRBaseCharacterMovementComponent * BaseCharMove = Cast<UVRBaseCharacterMovementComponent>(Character->GetCharacterMovement());

	if (BaseCharMove)
	{
		BaseCharMove->MoveAction = ConditionalValues.MoveAction; 
		BaseCharMove->CustomVRInputVector = ConditionalValues.CustomVRInputVector;//this->CustomVRInputVector;
		BaseCharMove->VRReplicatedMovementMode = this->VRReplicatedMovementMode;
	}
	
	if (!ConditionalValues.RequestedVelocity.IsZero())
	{
		BaseCharMove->RequestedVelocity = ConditionalValues.RequestedVelocity;
		BaseCharMove->SetHasRequestedVelocity(true);
	}
	else
	{
		BaseCharMove->SetHasRequestedVelocity(false);
	}

	FSavedMove_Character::PrepMoveFor(Character);
}

void UVRBaseCharacterMovementComponent::SmoothClientPosition(float DeltaSeconds)
{
	if (!HasValidData() || NetworkSmoothingMode == ENetworkSmoothingMode::Disabled)
	{
		return;
	}

	// We shouldn't be running this on a server that is not a listen server.
	checkSlow(GetNetMode() != NM_DedicatedServer);
	checkSlow(GetNetMode() != NM_Standalone);

	// Only client proxies or remote clients on a listen server should run this code.
	const bool bIsSimulatedProxy = (CharacterOwner->Role == ROLE_SimulatedProxy);
	const bool bIsRemoteAutoProxy = (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy);
	if (!ensure(bIsSimulatedProxy || bIsRemoteAutoProxy))
	{
		return;
	}

	// #TODO: To fix smoothing perfectly I would need to override SimulatedTick which isn't virtual so I also would need to
	// override TickComponent all the way back to base character movement comp. Then I would need to use VR loc instead of actor loc
	// for the smoothed location, and rotation likely wouldn't work period. Then there is the scoped prevent mesh move command which
	// would need to be moved to the new smoothing component...... I am on the fence about whether supporting epics smoothing is worth it
	// or if I should drop it and maybe run my own?

	/*if (bHadMoveActionThisFrame)
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		if (ClientData)
		{
			if (NetworkSmoothingMode == ENetworkSmoothingMode::Linear)
			{
				ClientData->MeshTranslationOffset = FVector::ZeroVector;
				ClientData->SmoothingClientTimeStamp = ClientData->SmoothingServerTimeStamp;
				bNetworkSmoothingComplete = true;
			}
			else if (NetworkSmoothingMode == ENetworkSmoothingMode::Exponential)
			{
				bNetworkSmoothingComplete = true;
				// Make sure to snap exactly to target values.
				ClientData->MeshTranslationOffset = FVector::ZeroVector;
				ClientData->MeshRotationOffset = ClientData->MeshRotationTarget;
			}
		}
	}
	else
	{
		SmoothClientPosition_Interpolate(DeltaSeconds);
		//SmoothClientPosition_UpdateVisuals(); No mesh, don't bother to run this
		SmoothClientPosition_UpdateVRVisuals();
	}*/

	SmoothClientPosition_Interpolate(DeltaSeconds);
	//SmoothClientPosition_UpdateVisuals(); No mesh, don't bother to run this
	SmoothClientPosition_UpdateVRVisuals();
}

void UVRBaseCharacterMovementComponent::SmoothClientPosition_UpdateVRVisuals()
{
	//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementSmoothClientPosition_Visual);
	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();

	AVRBaseCharacter * Basechar = Cast<AVRBaseCharacter>(CharacterOwner);

	if (!Basechar || !ClientData)
		return;

	if (ClientData)
	{
		if (NetworkSmoothingMode == ENetworkSmoothingMode::Linear)
		{
			// Adjust capsule rotation and mesh location. Optimized to trigger only one transform chain update.
			// If we know the rotation is changing that will update children, so it's sufficient to set RelativeLocation directly on the mesh.
			const FVector NewRelLocation = ClientData->MeshRotationOffset.UnrotateVector(ClientData->MeshTranslationOffset) + CharacterOwner->GetBaseTranslationOffset();

			if (!UpdatedComponent->GetComponentQuat().Equals(ClientData->MeshRotationOffset, SCENECOMPONENT_QUAT_TOLERANCE))
			{
				const FVector OldLocation = Basechar->NetSmoother->RelativeLocation;
				const FRotator OldRotation = UpdatedComponent->RelativeRotation;
				Basechar->NetSmoother->RelativeLocation = NewRelLocation;
				//Mesh->RelativeLocation = NewRelLocation;
				UpdatedComponent->SetWorldRotation(ClientData->MeshRotationOffset);

				// If we did not move from SetWorldRotation, we need to at least call SetRelativeLocation since we were relying on the UpdatedComponent to update the transform of the mesh
				if (UpdatedComponent->RelativeRotation == OldRotation)
				{
					Basechar->NetSmoother->RelativeLocation = OldLocation;
					Basechar->NetSmoother->SetRelativeLocation(NewRelLocation);
				}
			}
			else
			{
				Basechar->NetSmoother->SetRelativeLocation(NewRelLocation);
			}
		}
		else if (NetworkSmoothingMode == ENetworkSmoothingMode::Exponential)
		{
			// Adjust mesh location and rotation
			const FVector NewRelTranslation = UpdatedComponent->GetComponentToWorld().InverseTransformVectorNoScale(ClientData->MeshTranslationOffset) + CharacterOwner->GetBaseTranslationOffset();
			const FQuat NewRelRotation = ClientData->MeshRotationOffset * CharacterOwner->GetBaseRotationOffset();

			Basechar->NetSmoother->SetRelativeLocationAndRotation(NewRelTranslation, NewRelRotation);
		}
		else if (NetworkSmoothingMode == ENetworkSmoothingMode::Replay)
		{
			if (!UpdatedComponent->GetComponentQuat().Equals(ClientData->MeshRotationOffset, SCENECOMPONENT_QUAT_TOLERANCE) || !UpdatedComponent->GetComponentLocation().Equals(ClientData->MeshTranslationOffset, KINDA_SMALL_NUMBER))
			{
				UpdatedComponent->SetWorldLocationAndRotation(ClientData->MeshTranslationOffset, ClientData->MeshRotationOffset);
			}
		}
		else
		{
			// Unhandled mode
		}
	}
}
