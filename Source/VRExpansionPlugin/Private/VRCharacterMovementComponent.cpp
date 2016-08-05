// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Movement.cpp: Character movement implementation

=============================================================================*/

#include "VRExpansionPluginPrivatePCH.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/GameNetworkManager.h"
#include "GameFramework/Character.h"
#include "VRCharacterMovementComponent.h"
#include "GameFramework/GameState.h"
#include "Components/PrimitiveComponent.h"
#include "Animation/AnimMontage.h"
#include "PhysicsEngine/DestructibleActor.h"

// @todo this is here only due to circular dependency to AIModule. To be removed
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/AvoidanceManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/BrushComponent.h"
#include "Components/DestructibleComponent.h"

#include "Engine/DemoNetDriver.h"
#include "Engine/NetworkObjectList.h"

#include "PerfCountersHelpers.h"


DEFINE_LOG_CATEGORY_STATIC(LogCharacterMovement, Log, All);

/**
 * Character stats
 */
DECLARE_CYCLE_STAT(TEXT("Char StepUp"), STAT_CharStepUp, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char FindFloor"), STAT_CharFindFloor, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char ReplicateMoveToServer"), STAT_CharacterMovementReplicateMoveToServer, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char CallServerMove"), STAT_CharacterMovementCallServerMove, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char CombineNetMove"), STAT_CharacterMovementCombineNetMove, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysWalking"), STAT_CharPhysWalking, STATGROUP_Character);

static const auto CVarNetEnableMoveCombining = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("p.NetEnableMoveCombining"));

// MAGIC NUMBERS
const float MAX_STEP_SIDE_Z = 0.08f;	// maximum z value for the normal on the vertical side of steps

// Statics
namespace CharacterMovementComponentStatics
{
	static const FName ImmersionDepthName = FName(TEXT("MovementComp_Character_ImmersionDepth"));
}




/*
*
*
*  BEGIN TEST AREA
*
*
*/



//
// CROUCH functions need an overhaul
// NAVIGATION functions need an overhaul or to be removed entirely
// void UCharacterMovementComponent::ApplyRepulsionForce(float DeltaSeconds) ???? check for capsule hit location being correct


void UVRCharacterMovementComponent::PhysWalking(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysWalking);


	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->Role != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	if (!UpdatedComponent->IsQueryCollisionEnabled())
	{
		SetMovementMode(MOVE_Walking);
		return;
	}

	checkCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN before Iteration (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;

	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() || (CharacterOwner->Role == ROLE_SimulatedProxy)))
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Save current values
		UPrimitiveComponent * const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != NULL) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();

		// Used for ledge check
		FVector OldCapsuleLocation = OldLocation;
		if(VRRootCapsule)
			OldCapsuleLocation = VRRootCapsule->GetVRLocation();

		const FFindFloorResult OldFloor = CurrentFloor;

		RestorePreAdditiveRootMotionVelocity();

		// Ensure velocity is horizontal.
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;
		Acceleration.Z = 0.f;

		// Apply acceleration
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			CalcVelocity(timeTick, GroundFriction, false, BrakingDecelerationWalking);
			checkCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after CalcVelocity (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));
		}

		ApplyRootMotionToVelocity(timeTick);
		checkCode(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after Root Motion application (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

		if (IsFalling())
		{
			// Root motion could have put us into Falling.
			// No movement has taken place this movement tick so we pass on full time/past iteration count
			StartNewPhysics(remainingTime + timeTick, Iterations - 1);
			return;
		}

		// Compute move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;

		if (bZeroDelta)
		{
			remainingTime = 0.f;
		}
		else
		{
			// try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsFalling())
			{
				// pawn decided to jump up
				const float DesiredDist = Delta.Size();
				if (DesiredDist > KINDA_SMALL_NUMBER)
				{
					const float ActualDist = (UpdatedComponent->GetComponentLocation() - OldLocation).Size2D();
					remainingTime += timeTick * (1.f - FMath::Min(1.f, ActualDist / DesiredDist));
				}
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
			else if (IsSwimming()) //just entered water
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
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

		// check for ledges here
		const bool bCheckLedges = !CanWalkOffLedges();
		if (bCheckLedges && !CurrentFloor.IsWalkableFloor())
		{
			// calculate possible alternate movement
			const FVector GravDir = FVector(0.f, 0.f, -1.f);
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldCapsuleLocation, Delta, GravDir);
			if (!NewDelta.IsZero())
			{
				// first revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta / timeTick;
				remainingTime += timeTick;
				continue;
			}
			else
			{
				// see if it is OK to jump
				// @todo collision : only thing that can be problem is that oldbase has world collision on
				bool bMustJump = bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;

				// revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.f;
				break;
			}
		}
		else
		{
			// Validate the floor check
			if (CurrentFloor.IsWalkableFloor())
			{
				if (ShouldCatchAir(OldFloor, CurrentFloor))
				{
					CharacterOwner->OnWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsMovingOnGround())
					{
						// If still walking, then fall. If not, assume the user set a different mode they want to keep.
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}
					return;
				}

				AdjustFloorHeight();
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + FVector(0.f, 0.f, MAX_FLOOR_DIST);
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
			}

			// check if just entered water
			if (IsSwimming())
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling.
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					return;
				}
				bCheckedFall = true;
			}
		}


		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround())
		{
			// Make velocity reflect actual move
			if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME)
			{
				// TODO-RootMotionSource: Allow this to happen during partial override Velocity, but only set allowed axes?
				Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;
			}
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
	}

	if (IsMovingOnGround())
	{
		MaintainHorizontalGroundVelocity();
	}
}


// Adjust for component location
/*
bool UCharacterMovementComponent::CheckWaterJump(FVector CheckPoint, FVector& WallNormal)
{
if (!HasValidData())
{
return false;
}
// check if there is a wall directly in front of the swimming pawn
CheckPoint.Z = 0.f;
FVector CheckNorm = CheckPoint.GetSafeNormal();
float PawnCapsuleRadius, PawnCapsuleHalfHeight;
CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnCapsuleRadius, PawnCapsuleHalfHeight);
CheckPoint = UpdatedComponent->GetComponentLocation() + 1.2f * PawnCapsuleRadius * CheckNorm;
FVector Extent(PawnCapsuleRadius, PawnCapsuleRadius, PawnCapsuleHalfHeight);
FHitResult HitInfo(1.f);
FCollisionQueryParams CapsuleParams(CharacterMovementComponentStatics::CheckWaterJumpName, false, CharacterOwner);
FCollisionResponseParams ResponseParam;
InitCollisionParams(CapsuleParams, ResponseParam);
FCollisionShape CapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_None);
const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
bool bHit = GetWorld()->SweepSingleByChannel( HitInfo, UpdatedComponent->GetComponentLocation(), CheckPoint, FQuat::Identity, CollisionChannel, CapsuleShape, CapsuleParams, ResponseParam);

if ( bHit && !Cast<APawn>(HitInfo.GetActor()) )
{
// hit a wall - check if it is low enough
WallNormal = -1.f * HitInfo.ImpactNormal;
FVector Start = UpdatedComponent->GetComponentLocation();
Start.Z += MaxOutOfWaterStepHeight;
CheckPoint = Start + 3.2f * PawnCapsuleRadius * WallNormal;
FCollisionQueryParams LineParams(CharacterMovementComponentStatics::CheckWaterJumpName, true, CharacterOwner);
FCollisionResponseParams LineResponseParam;
InitCollisionParams(LineParams, LineResponseParam);
bHit = GetWorld()->LineTraceSingleByChannel( HitInfo, Start, CheckPoint, CollisionChannel, LineParams, LineResponseParam );
// if no high obstruction, or it's a valid floor, then pawn can jump out of water
return !bHit || IsWalkable(HitInfo);
}
return false;
}


*/

// Adjust for component location
/*
FVector UCharacterMovementComponent::FindWaterLine(FVector InWater, FVector OutofWater)
{
FVector Result = OutofWater;

TArray<FHitResult> Hits;
GetWorld()->LineTraceMultiByChannel(Hits, OutofWater, InWater, UpdatedComponent->GetCollisionObjectType(), FCollisionQueryParams(CharacterMovementComponentStatics::FindWaterLineName, true, CharacterOwner));

for( int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++ )
{
const FHitResult& Check = Hits[HitIdx];
if ( !CharacterOwner->IsOwnedBy(Check.GetActor()) && !Check.Component.Get()->IsWorldGeometry() )
{
APhysicsVolume *W = Cast<APhysicsVolume>(Check.GetActor());
if ( W && W->bWaterVolume )
{
FVector Dir = (InWater - OutofWater).GetSafeNormal();
Result = Check.Location;
if ( W == GetPhysicsVolume() )
Result += 0.1f * Dir;
else
Result -= 0.1f * Dir;
break;
}
}
}

return Result;
}

*/

// Adjust for new location / capsule
/*
float UCharacterMovementComponent::ImmersionDepth() const
{
float depth = 0.f;

if ( CharacterOwner && GetPhysicsVolume()->bWaterVolume )
{
const float CollisionHalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();

if ( (CollisionHalfHeight == 0.f) || (Buoyancy == 0.f) )
{
depth = 1.f;
}
else
{
UBrushComponent* VolumeBrushComp = GetPhysicsVolume()->GetBrushComponent();
FHitResult Hit(1.f);
if ( VolumeBrushComp )
{
const FVector TraceStart = UpdatedComponent->GetComponentLocation() + FVector(0.f,0.f,CollisionHalfHeight);
const FVector TraceEnd = UpdatedComponent->GetComponentLocation() - FVector(0.f,0.f,CollisionHalfHeight);

FCollisionQueryParams NewTraceParams(CharacterMovementComponentStatics::ImmersionDepthName, true);
VolumeBrushComp->LineTraceComponent( Hit, TraceStart, TraceEnd, NewTraceParams );
}

depth = (Hit.Time == 1.f) ? 1.f : (1.f - Hit.Time);
}
}
return depth;
}
*/

/*
void UVRCharacterMovementComponent::ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration)
{
	SCOPE_CYCLE_COUNTER(STAT_CharacterMovementReplicateMoveToServer);
	check(CharacterOwner != NULL);

	// Can only start sending moves if our controllers are synced up over the network, otherwise we flood the reliable buffer.
	APlayerController* PC = Cast<APlayerController>(CharacterOwner->GetController());
	if (PC && PC->AcknowledgedPawn != CharacterOwner)
	{
		return;
	}

	// Bail out if our character's controller doesn't have a Player. This may be the case when the local player
	// has switched to another controller, such as a debug camera controller.
	if (PC && PC->Player == nullptr)
	{
		return;
	}

	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	if (!ClientData)
	{
		return;
	}

	// Update our delta time for physics simulation.
	DeltaTime = ClientData->UpdateTimeStampAndDeltaTime(DeltaTime, *CharacterOwner, *this);

	// Find the oldest (unacknowledged) important move (OldMove).
	// Don't include the last move because it may be combined with the next new move.
	// A saved move is interesting if it differs significantly from the last acknowledged move
	FSavedMovePtr OldMove = NULL;
	if (ClientData->LastAckedMove.IsValid())
	{
		for (int32 i = 0; i < ClientData->SavedMoves.Num() - 1; i++)
		{
			const FSavedMovePtr& CurrentMove = ClientData->SavedMoves[i];
			if (CurrentMove->IsImportantMove(ClientData->LastAckedMove))
			{
				OldMove = CurrentMove;
				break;
			}
		}
	}

	// Get a SavedMove object to store the movement in.
	FSavedMovePtr NewMove = ClientData->CreateSavedMove();
	if (NewMove.IsValid() == false)
	{
		return;
	}

	NewMove->SetMoveFor(CharacterOwner, DeltaTime, NewAcceleration, *ClientData);

	// see if the two moves could be combined
	// do not combine moves which have different TimeStamps (before and after reset).
	if (ClientData->PendingMove.IsValid() && !ClientData->PendingMove->bOldTimeStampBeforeReset && ClientData->PendingMove->CanCombineWith(NewMove, CharacterOwner, ClientData->MaxMoveDeltaTime * CharacterOwner->GetActorTimeDilation()))
	{
		SCOPE_CYCLE_COUNTER(STAT_CharacterMovementCombineNetMove);

		// Only combine and move back to the start location if we don't move back in to a spot that would make us collide with something new.
		const FVector OldStartLocation = ClientData->PendingMove->GetRevertedLocation();
		
		// Correct for offset capsule location if needed
		FVector OldCapsuleStartLocation = OldStartLocation;
		if (VRRootCapsule)
			OldCapsuleStartLocation = VRRootCapsule->GetVROffsetFromLocationAndRotation(OldStartLocation, ClientData->PendingMove->StartRotation.Quaternion());

		if (!OverlapTest(OldCapsuleStartLocation, ClientData->PendingMove->StartRotation.Quaternion(), UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CharacterOwner))
		{
			FScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, EScopedUpdate::DeferredUpdates);
			UE_LOG(LogNetPlayerMovement, VeryVerbose, TEXT("CombineMove: add delta %f + %f and revert from %f %f to %f %f"), DeltaTime, ClientData->PendingMove->DeltaTime, UpdatedComponent->GetComponentLocation().X, UpdatedComponent->GetComponentLocation().Y, OldStartLocation.X, OldStartLocation.Y);

			// to combine move, first revert pawn position to PendingMove start position, before playing combined move on client
			const bool bNoCollisionCheck = true;
			UpdatedComponent->SetWorldLocationAndRotation(OldStartLocation, ClientData->PendingMove->StartRotation, false);
			Velocity = ClientData->PendingMove->StartVelocity;

			SetBase(ClientData->PendingMove->StartBase.Get(), ClientData->PendingMove->StartBoneName);
			CurrentFloor = ClientData->PendingMove->StartFloor;

			// Now that we have reverted to the old position, prepare a new move from that position,
			// using our current velocity, acceleration, and rotation, but applied over the combined time from the old and new move.

			NewMove->DeltaTime += ClientData->PendingMove->DeltaTime;

			if (PC)
			{
				// We reverted position to that at the start of the pending move (above), however some code paths expect rotation to be set correctly
				// before character movement occurs (via FaceRotation), so try that now. The bOrientRotationToMovement path happens later as part of PerformMovement() and PhysicsRotation().
				CharacterOwner->FaceRotation(PC->GetControlRotation(), NewMove->DeltaTime);
			}

			SaveBaseLocation();
			NewMove->SetInitialPosition(CharacterOwner);

			// Remove pending move from move list. It would have to be the last move on the list.
			if (ClientData->SavedMoves.Num() > 0 && ClientData->SavedMoves.Last() == ClientData->PendingMove)
			{
				ClientData->SavedMoves.Pop();
			}
			ClientData->FreeMove(ClientData->PendingMove);
			ClientData->PendingMove = NULL;
		}
		else
		{
			//UE_LOG(LogNet, Log, TEXT("Not combining move, would collide at start location"));
		}
	}

	// Acceleration should match what we send to the server, plus any other restrictions the server also enforces (see MoveAutonomous).
	Acceleration = NewMove->Acceleration.GetClampedToMaxSize(GetMaxAcceleration());
	AnalogInputModifier = ComputeAnalogInputModifier(); // recompute since acceleration may have changed.

														// Perform the move locally
	CharacterOwner->ClientRootMotionParams.Clear();
	CharacterOwner->SavedRootMotion.Clear();
	PerformMovement(NewMove->DeltaTime);

	NewMove->PostUpdate(CharacterOwner, FSavedMove_Character::PostUpdate_Record);

	// Add NewMove to the list
	if (CharacterOwner->bReplicateMovement)
	{
		ClientData->SavedMoves.Push(NewMove);

		const bool bCanDelayMove = (CVarNetEnableMoveCombining->GetValueOnGameThread() != 0) && CanDelaySendingMove(NewMove);

		if (bCanDelayMove && ClientData->PendingMove.IsValid() == false)
		{
			// Decide whether to hold off on move
			// send moves more frequently in small games where server isn't likely to be saturated
			float NetMoveDelta;
			UPlayer* Player = (PC ? PC->Player : NULL);

			if (Player && (Player->CurrentNetSpeed > 10000) && (GetWorld()->GameState != NULL) && (GetWorld()->GameState->PlayerArray.Num() <= 10))
			{
				NetMoveDelta = 0.011f;
			}
			else if (Player && CharacterOwner->GetWorldSettings()->GameNetworkManagerClass)
			{
				NetMoveDelta = FMath::Max(0.0222f, 2 * GetDefault<AGameNetworkManager>(CharacterOwner->GetWorldSettings()->GameNetworkManagerClass)->MoveRepSize / Player->CurrentNetSpeed);
			}
			else
			{
				NetMoveDelta = 0.011f;
			}

			if ((GetWorld()->TimeSeconds - ClientData->ClientUpdateTime) * CharacterOwner->GetWorldSettings()->GetEffectiveTimeDilation() < NetMoveDelta)
			{
				// Delay sending this move.
				ClientData->PendingMove = NewMove;
				return;
			}
		}

		ClientData->ClientUpdateTime = GetWorld()->TimeSeconds;

		UE_LOG(LogNetPlayerMovement, Verbose, TEXT("Client ReplicateMove Time %f Acceleration %s Position %s DeltaTime %f"),
			NewMove->TimeStamp, *NewMove->Acceleration.ToString(), *UpdatedComponent->GetComponentLocation().ToString(), DeltaTime);

		// Send move to server if this character is replicating movement
		{
			SCOPE_CYCLE_COUNTER(STAT_CharacterMovementCallServerMove);
			CallServerMove(NewMove.Get(), OldMove.Get());
		}
	}

	ClientData->PendingMove = NULL;
}*/

/*
*
*
*  END TEST AREA
*
*
*/

UVRCharacterMovementComponent::UVRCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PostPhysicsTickFunction.bCanEverTick = true;
	PostPhysicsTickFunction.bStartWithTickEnabled = false;
	//PostPhysicsTickFunction.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	VRRootCapsule = NULL;

	// Keep this false
	this->bTickBeforeOwner = false;

	WallRepulsionMultiplier = 0.12f;

	bAllowWalkingThroughWalls = false;
}


void UVRCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (VRRootCapsule && VRRootCapsule->bHadRelativeMovement && CharacterOwner->Role > ROLE_SimulatedProxy)
	{
		bForceNextFloorCheck = true;

		if (CharacterOwner->IsLocallyControlled())
		{
			if (!bAllowWalkingThroughWalls)
			{
				// Fake movement was too sketchy, going to find a different solution.
				//AddInputVector(VRRootCapsule->DifferenceFromLastFrame * 0.0008f);
				FHitResult OutHit;
				FCollisionQueryParams Params("RelativeMovementSweep", false, VRRootCapsule->GetOwner());
				FCollisionResponseParams ResponseParam;
				VRRootCapsule->InitSweepCollisionParams(Params, ResponseParam);
				bool bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, VRRootCapsule->GetVRLocation(), VRRootCapsule->GetVRLocation() + VRRootCapsule->DifferenceFromLastFrame, FQuat(0.0f, 0.0f, 0.0f, 1.0f), ECollisionChannel::ECC_Visibility, VRRootCapsule->GetCollisionShape(), Params, ResponseParam);

				// If we had a valid blocking hit
				if (bBlockingHit && (!OutHit.Component.IsValid() || (!OutHit.Component->IsSimulatingPhysics()))) // Cancel for simulating physics on the component
				{
					// Add the relative movement into the move for this frame to back us out and lower the strength to prevent sliding
					AddInputVector(VRRootCapsule->DifferenceFromLastFrame * WallRepulsionMultiplier);
				}
			}
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

// No support for crouching code yet
bool UVRCharacterMovementComponent::CanCrouch()
{
	return false;
}

// REMOVE ME - After testing, shouldn't be needed anymore, but make sure
/*bool UVRCharacterMovementComponent::CanStepUp(const FHitResult& Hit) const
{
	if (!Hit.IsValidBlockingHit() || !HasValidData() || MovementMode == MOVE_Falling )
	{
		return false;
	}

	// No component for "fake" hits when we are on a known good base.
	const UPrimitiveComponent* HitComponent = Hit.Component.Get();
	if (!HitComponent)
	{
		return true;
	}

	if (HitComponent->IsSimulatingPhysics())
		return false;

	if (!HitComponent->CanCharacterStepUp(CharacterOwner))
	{
		return false;
	}

	// No actor for "fake" hits when we are on a known good base.
	const AActor* HitActor = Hit.GetActor();
	if (!HitActor)
	{
		return true;
	}

	if (!HitActor->CanBeBaseForCharacter(CharacterOwner))
	{
		return false;
	}

	return true;
}*/

void UVRCharacterMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	Super::SetUpdatedComponent(NewUpdatedComponent);

	if (UpdatedComponent)
	{
		VRRootCapsule = Cast<UVRRootComponent>(UpdatedComponent);

		// Stop the tick forcing
		UpdatedComponent->PrimaryComponentTick.RemovePrerequisite(this, PrimaryComponentTick);

		// Start forcing the root to tick before this, the actor tick will still tick after the movement component
		// We want the root component to tick first because it is setting its offset location based off of tick
		this->PrimaryComponentTick.AddPrerequisite(UpdatedComponent, UpdatedComponent->PrimaryComponentTick);
	}
}

bool UVRCharacterMovementComponent::StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult)
{
	SCOPE_CYCLE_COUNTER(STAT_CharStepUp);

	if (!CanStepUp(InHit) || MaxStepHeight <= 0.f)
	{
		return false;
	}

	FVector OldLocation;

	if (VRRootCapsule)
		OldLocation = VRRootCapsule->GetVRLocation();
	else
		OldLocation = UpdatedComponent->GetComponentLocation();

	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	// Don't bother stepping up if top of capsule is hitting something.
	const float InitialImpactZ = InHit.ImpactPoint.Z;
	if (InitialImpactZ > OldLocation.Z + (PawnHalfHeight/**2*/ - PawnRadius))
	{
		return false;
	}

	// Don't step up if the impact is below us
	if (InitialImpactZ <= OldLocation.Z - PawnHalfHeight)
	{
		return false;
	}

	if (GravDir.IsZero())
	{
		return false;
	}

	// Gravity should be a normalized direction
	ensure(GravDir.IsNormalized());

	float StepTravelUpHeight = MaxStepHeight;
	float StepTravelDownHeight = StepTravelUpHeight;
	const float StepSideZ = -1.f * (InHit.ImpactNormal | GravDir);
	float PawnInitialFloorBaseZ = OldLocation.Z -PawnHalfHeight;
	float PawnFloorPointZ = PawnInitialFloorBaseZ;

	if (IsMovingOnGround() && CurrentFloor.IsWalkableFloor())
	{
		// Since we float a variable amount off the floor, we need to enforce max step height off the actual point of impact with the floor.
		const float FloorDist = FMath::Max(0.f, CurrentFloor.FloorDist);
		PawnInitialFloorBaseZ -= FloorDist;
		StepTravelUpHeight = FMath::Max(StepTravelUpHeight - FloorDist, 0.f);
		StepTravelDownHeight = (MaxStepHeight + MAX_FLOOR_DIST*2.f);

		const bool bHitVerticalFace = !IsWithinEdgeTolerance(InHit.Location, InHit.ImpactPoint, PawnRadius);
		if (!CurrentFloor.bLineTrace && !bHitVerticalFace)
		{
			PawnFloorPointZ = CurrentFloor.HitResult.ImpactPoint.Z;
		}
		else
		{
			// Base floor point is the base of the capsule moved down by how far we are hovering over the surface we are hitting.
			PawnFloorPointZ -= CurrentFloor.FloorDist;
		}
	}

	// Scope our movement updates, and do not apply them until all intermediate moves are completed.
	FScopedMovementUpdate ScopedStepUpMovement(UpdatedComponent, EScopedUpdate::DeferredUpdates);

	// step up - treat as vertical wall
	FHitResult SweepUpHit(1.f);
	const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
	MoveUpdatedComponent(-GravDir * StepTravelUpHeight, PawnRotation, true, &SweepUpHit);

	if (SweepUpHit.bStartPenetrating)
	{
		// Undo movement
		ScopedStepUpMovement.RevertMove();
		return false;
	}

	// step fwd
	FHitResult Hit(1.f);
	MoveUpdatedComponent(Delta, PawnRotation, true, &Hit);

	// Check result of forward movement
	if (Hit.bBlockingHit)
	{
		if (Hit.bStartPenetrating)
		{
			// Undo movement
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// If we hit something above us and also something ahead of us, we should notify about the upward hit as well.
		// The forward hit will be handled later (in the bSteppedOver case below).
		// In the case of hitting something above but not forward, we are not blocked from moving so we don't need the notification.
		if (SweepUpHit.bBlockingHit && Hit.bBlockingHit)
		{
			HandleImpact(SweepUpHit);
		}

		// pawn ran into a wall
		HandleImpact(Hit);
		if (IsFalling())
		{
			return true;
		}

		// adjust and try again
		const float ForwardHitTime = Hit.Time;
		const float ForwardSlideAmount = SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);

		if (IsFalling())
		{
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// If both the forward hit and the deflection got us nowhere, there is no point in this step up.
		if (ForwardHitTime == 0.f && ForwardSlideAmount == 0.f)
		{
			ScopedStepUpMovement.RevertMove();
			return false;
		}
	}

	// Step down
	MoveUpdatedComponent(GravDir * StepTravelDownHeight, UpdatedComponent->GetComponentQuat(), true, &Hit);

	// If step down was initially penetrating abort the step up
	if (Hit.bStartPenetrating)
	{
		ScopedStepUpMovement.RevertMove();
		return false;
	}

	FStepDownResult StepDownResult;
	if (Hit.IsValidBlockingHit())
	{
		// See if this step sequence would have allowed us to travel higher than our max step height allows.
		const float DeltaZ = Hit.ImpactPoint.Z - PawnFloorPointZ;
		if (DeltaZ > MaxStepHeight)
		{
			UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (too high Height %.3f) up from floor base %f"), DeltaZ, PawnInitialFloorBaseZ);
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Reject unwalkable surface normals here.
		if (!IsWalkable(Hit))
		{
			// Reject if normal opposes movement direction
			const bool bNormalTowardsMe = (Delta | Hit.ImpactNormal) < 0.f;
			if (bNormalTowardsMe)
			{
				//UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s opposed to movement)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}

			// Also reject if we would end up being higher than our starting location by stepping down.
			// It's fine to step down onto an unwalkable normal below us, we will just slide off. Rejecting those moves would prevent us from being able to walk off the edge.
			if (Hit.Location.Z > OldLocation.Z)
			{
				UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s above old position)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}
		}

		// Reject moves where the downward sweep hit something very close to the edge of the capsule. This maintains consistency with FindFloor as well.
		if (!IsWithinEdgeTolerance(Hit.Location, Hit.ImpactPoint, PawnRadius))
		{
			UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (outside edge tolerance)"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Don't step up onto invalid surfaces if traveling higher.
		if (DeltaZ > 0.f && !CanStepUp(Hit))
		{
			UE_LOG(LogCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (up onto surface with !CanStepUp())"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// See if we can validate the floor as a result of this step down. In almost all cases this should succeed, and we can avoid computing the floor outside this method.
		if (OutStepDownResult != NULL)
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), StepDownResult.FloorResult, false, &Hit);

			// Reject unwalkable normals if we end up higher than our initial height.
			// It's fine to walk down onto an unwalkable surface, don't reject those moves.
			if (Hit.Location.Z > OldLocation.Z)
			{
				// We should reject the floor result if we are trying to step up an actual step where we are not able to perch (this is rare).
				// In those cases we should instead abort the step up and try to slide along the stair.
				if (!StepDownResult.FloorResult.bBlockingHit && StepSideZ < MAX_STEP_SIDE_Z)
				{
					ScopedStepUpMovement.RevertMove();
					return false;
				}
			}

			StepDownResult.bComputedFloor = true;
		}
	}

	// Copy step down result.
	if (OutStepDownResult != NULL)
	{
		*OutStepDownResult = StepDownResult;
	}

	// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments.
	bJustTeleported |= !bMaintainHorizontalGroundVelocity;

	return true;
}


void UVRCharacterMovementComponent::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bZeroDelta, const FHitResult* DownwardSweepResult) const
{
	SCOPE_CYCLE_COUNTER(STAT_CharFindFloor);

	// No collision, no floor...
	if (!HasValidData() || !UpdatedComponent->IsQueryCollisionEnabled())
	{
		OutFloorResult.Clear();
		return;
	}

	FVector UseCapsuleLocation = CapsuleLocation;

	if (VRRootCapsule)
		UseCapsuleLocation = VRRootCapsule->GetVRLocation();

	check(CharacterOwner->GetCapsuleComponent());

	// Increase height check slightly if walking, to prevent floor height adjustment from later invalidating the floor result.
	const float HeightCheckAdjust = (IsMovingOnGround() ? MAX_FLOOR_DIST + KINDA_SMALL_NUMBER : -MAX_FLOOR_DIST);

	float FloorSweepTraceDist = FMath::Max(MAX_FLOOR_DIST, MaxStepHeight + HeightCheckAdjust);
	float FloorLineTraceDist = FloorSweepTraceDist;
	bool bNeedToValidateFloor = true;

	// Sweep floor
	if (FloorLineTraceDist > 0.f || FloorSweepTraceDist > 0.f)
	{
		UCharacterMovementComponent* MutableThis = const_cast<UCharacterMovementComponent*>((UCharacterMovementComponent*)this);

		if (bAlwaysCheckFloor || !bZeroDelta || bForceNextFloorCheck || bJustTeleported)
		{
			MutableThis->bForceNextFloorCheck = false;
			ComputeFloorDist(UseCapsuleLocation, FloorLineTraceDist, FloorSweepTraceDist, OutFloorResult, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius(), DownwardSweepResult);
		}
		else
		{
			// Force floor check if base has collision disabled or if it does not block us.
			UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
			const AActor* BaseActor = MovementBase ? MovementBase->GetOwner() : NULL;
			const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();

			if (MovementBase != NULL)
			{
				MutableThis->bForceNextFloorCheck = !MovementBase->IsQueryCollisionEnabled()
					|| MovementBase->GetCollisionResponseToChannel(CollisionChannel) != ECR_Block
					|| MovementBaseUtility::IsDynamicBase(MovementBase);
			}

			const bool IsActorBasePendingKill = BaseActor && BaseActor->IsPendingKill();

			if (!bForceNextFloorCheck && !IsActorBasePendingKill && MovementBase)
			{
				//UE_LOG(LogCharacterMovement, Log, TEXT("%s SKIP check for floor"), *CharacterOwner->GetName());
				OutFloorResult = CurrentFloor;
				bNeedToValidateFloor = false;
			}
			else
			{
				MutableThis->bForceNextFloorCheck = false;
				ComputeFloorDist(UseCapsuleLocation, FloorLineTraceDist, FloorSweepTraceDist, OutFloorResult, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius(), DownwardSweepResult);
			}
		}
	}

	// OutFloorResult.HitResult is now the result of the vertical floor check.
	// See if we should try to "perch" at this location.
	if (bNeedToValidateFloor && OutFloorResult.bBlockingHit && !OutFloorResult.bLineTrace)
	{
		const bool bCheckRadius = true;
		if (ShouldComputePerchResult(OutFloorResult.HitResult, bCheckRadius))
		{
			float MaxPerchFloorDist = FMath::Max(MAX_FLOOR_DIST, MaxStepHeight + HeightCheckAdjust);
			if (IsMovingOnGround())
			{
				MaxPerchFloorDist += FMath::Max(0.f, PerchAdditionalHeight);
			}

			FFindFloorResult PerchFloorResult;
			if (ComputePerchResult(GetValidPerchRadius(), OutFloorResult.HitResult, MaxPerchFloorDist, PerchFloorResult))
			{
				// Don't allow the floor distance adjustment to push us up too high, or we will move beyond the perch distance and fall next time.
				const float AvgFloorDist = (MIN_FLOOR_DIST + MAX_FLOOR_DIST) * 0.5f;
				const float MoveUpDist = (AvgFloorDist - OutFloorResult.FloorDist);
				if (MoveUpDist + PerchFloorResult.FloorDist >= MaxPerchFloorDist)
				{
					OutFloorResult.FloorDist = AvgFloorDist;
				}

				// If the regular capsule is on an unwalkable surface but the perched one would allow us to stand, override the normal to be one that is walkable.
				if (!OutFloorResult.bWalkableFloor)
				{
					OutFloorResult.SetFromLineTrace(PerchFloorResult.HitResult, OutFloorResult.FloorDist, FMath::Min(PerchFloorResult.FloorDist, PerchFloorResult.LineDist), true);
				}
			}
			else
			{
				// We had no floor (or an invalid one because it was unwalkable), and couldn't perch here, so invalidate floor (which will cause us to start falling).
				OutFloorResult.bWalkableFloor = false;
			}
		}
	}
}

bool UVRCharacterMovementComponent::FloorSweepTest(
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

	if (!bUseFlatBaseForFloorChecks)
	{
		TArray<FHitResult> OutHits;
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

		//bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity, TraceChannel, CollisionShape, Params, ResponseParam);
	}
	else
	{
		// Test with a box that is enclosed by the capsule.
		const float CapsuleRadius = CollisionShape.GetCapsuleRadius();
		const float CapsuleHeight = CollisionShape.GetCapsuleHalfHeight();
		const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(CapsuleRadius * 0.707f, CapsuleRadius * 0.707f, CapsuleHeight));

		// First test with the box rotated so the corners are along the major axes (ie rotated 45 degrees).
		TArray<FHitResult> OutHits;
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
		
		//bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat(FVector(0.f, 0.f, -1.f), PI * 0.25f), TraceChannel, BoxShape, Params, ResponseParam);

		if (!bBlockingHit)
		{
			// Test again with the same box, not rotated.
			OutHit.Reset(1.f, false);

			OutHits.Reset();
			//TArray<FHitResult> OutHits;
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
			//bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity, TraceChannel, BoxShape, Params, ResponseParam);
		}
	}

	return bBlockingHit;
}

FVector UVRCharacterMovementComponent::GetImpartedMovementBaseVelocity() const
{
	FVector Result = FVector::ZeroVector;
	if (CharacterOwner)
	{
		UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
		if (MovementBaseUtility::IsDynamicBase(MovementBase))
		{
			FVector BaseVelocity = MovementBaseUtility::GetMovementBaseVelocity(MovementBase, CharacterOwner->GetBasedMovement().BoneName);

			if (bImpartBaseAngularVelocity)
			{
				const FVector CharacterBasePosition = (UpdatedComponent->GetComponentLocation()/* - FVector(0.f, 0.f, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())*/);
				const FVector BaseTangentialVel = MovementBaseUtility::GetMovementBaseTangentialVelocity(MovementBase, CharacterOwner->GetBasedMovement().BoneName, CharacterBasePosition);
				BaseVelocity += BaseTangentialVel;
			}

			if (bImpartBaseVelocityX)
			{
				Result.X = BaseVelocity.X;
			}
			if (bImpartBaseVelocityY)
			{
				Result.Y = BaseVelocity.Y;
			}
			if (bImpartBaseVelocityZ)
			{
				Result.Z = BaseVelocity.Z;
			}
		}
	}

	return Result;
}

float UVRCharacterMovementComponent::ImmersionDepth() const
{
	float depth = 0.f;

	if (CharacterOwner && GetPhysicsVolume()->bWaterVolume)
	{
		const float CollisionHalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();

		if ((CollisionHalfHeight == 0.f) || (Buoyancy == 0.f))
		{
			depth = 1.f;
		}
		else
		{
			UBrushComponent* VolumeBrushComp = GetPhysicsVolume()->GetBrushComponent();
			FHitResult Hit(1.f);
			if (VolumeBrushComp)
			{
				const FVector TraceStart = UpdatedComponent->GetComponentLocation() + FVector(0.f, 0.f, CollisionHalfHeight*2);
				const FVector TraceEnd = UpdatedComponent->GetComponentLocation();// -FVector(0.f, 0.f, CollisionHalfHeight);

				FCollisionQueryParams NewTraceParams(CharacterMovementComponentStatics::ImmersionDepthName, true);
				VolumeBrushComp->LineTraceComponent(Hit, TraceStart, TraceEnd, NewTraceParams);
			}

			depth = (Hit.Time == 1.f) ? 1.f : (1.f - Hit.Time);
		}
	}
	return depth;
}

void UVRCharacterMovementComponent::VisualizeMovement() const
{
	if (CharacterOwner == nullptr)
	{
		return;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	const FVector TopOfCapsule = GetActorLocation() + FVector(0.f, 0.f, CharacterOwner->GetSimpleCollisionHalfHeight()*2);
	float HeightOffset = 0.f;

	// Position
	{
		const FColor DebugColor = FColor::White;
		const FVector DebugLocation = TopOfCapsule + FVector(0.f, 0.f, HeightOffset);
		FString DebugText = FString::Printf(TEXT("Position: %s"), *GetActorLocation().ToCompactString());
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.f, true);
	}

	// Velocity
	{
		const FColor DebugColor = FColor::Green;
		HeightOffset += 15.f;
		const FVector DebugLocation = TopOfCapsule + FVector(0.f, 0.f, HeightOffset);
		DrawDebugDirectionalArrow(GetWorld(), DebugLocation, DebugLocation + Velocity,
			100.f, DebugColor, false, -1.f, (uint8)'\000', 10.f);

		FString DebugText = FString::Printf(TEXT("Velocity: %s (Speed: %.2f)"), *Velocity.ToCompactString(), Velocity.Size());
		DrawDebugString(GetWorld(), DebugLocation + FVector(0.f, 0.f, 5.f), DebugText, nullptr, DebugColor, 0.f, true);
	}

	// Acceleration
	{
		const FColor DebugColor = FColor::Yellow;
		HeightOffset += 15.f;
		const float MaxAccelerationLineLength = 200.f;
		const float CurrentMaxAccel = GetMaxAcceleration();
		const float CurrentAccelAsPercentOfMaxAccel = CurrentMaxAccel > 0.f ? Acceleration.Size() / CurrentMaxAccel : 1.f;
		const FVector DebugLocation = TopOfCapsule + FVector(0.f, 0.f, HeightOffset);
		DrawDebugDirectionalArrow(GetWorld(), DebugLocation,
			DebugLocation + Acceleration.GetSafeNormal(SMALL_NUMBER) * CurrentAccelAsPercentOfMaxAccel * MaxAccelerationLineLength,
			25.f, DebugColor, false, -1.f, (uint8)'\000', 8.f);

		FString DebugText = FString::Printf(TEXT("Acceleration: %s"), *Acceleration.ToCompactString());
		DrawDebugString(GetWorld(), DebugLocation + FVector(0.f, 0.f, 5.f), DebugText, nullptr, DebugColor, 0.f, true);
	}

	// Movement Mode
	{
		const FColor DebugColor = FColor::Blue;
		HeightOffset += 20.f;
		const FVector DebugLocation = TopOfCapsule + FVector(0.f, 0.f, HeightOffset);
		FString DebugText = FString::Printf(TEXT("MovementMode: %s"), *GetMovementName());
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.f, true);
	}

	// Root motion (additive)
	if (CurrentRootMotion.HasAdditiveVelocity())
	{
		const FColor DebugColor = FColor::Cyan;
		HeightOffset += 15.f;
		const FVector DebugLocation = TopOfCapsule + FVector(0.f, 0.f, HeightOffset);

		FVector CurrentAdditiveVelocity(FVector::ZeroVector);
		CurrentRootMotion.AccumulateAdditiveRootMotionVelocity(0.f, *CharacterOwner, *this, CurrentAdditiveVelocity);

		DrawDebugDirectionalArrow(GetWorld(), DebugLocation, DebugLocation + CurrentAdditiveVelocity,
			100.f, DebugColor, false, -1.f, (uint8)'\000', 10.f);

		FString DebugText = FString::Printf(TEXT("RootMotionAdditiveVelocity: %s (Speed: %.2f)"),
			*CurrentAdditiveVelocity.ToCompactString(), CurrentAdditiveVelocity.Size());
		DrawDebugString(GetWorld(), DebugLocation + FVector(0.f, 0.f, 5.f), DebugText, nullptr, DebugColor, 0.f, true);
	}

	// Root motion (override)
	if (CurrentRootMotion.HasOverrideVelocity())
	{
		const FColor DebugColor = FColor::Green;
		HeightOffset += 15.f;
		const FVector DebugLocation = TopOfCapsule + FVector(0.f, 0.f, HeightOffset);
		FString DebugText = FString::Printf(TEXT("Has Override RootMotion"));
		DrawDebugString(GetWorld(), DebugLocation, DebugText, nullptr, DebugColor, 0.f, true);
	}
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
}
