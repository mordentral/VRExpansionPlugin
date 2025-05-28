// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Movement.cpp: Character movement implementation

=============================================================================*/

#include "VRBaseCharacterMovementComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VRBaseCharacterMovementComponent)

#include "VRBPDatatypes.h"
#include "ParentRelativeAttachmentComponent.h"
#include "VRBaseCharacter.h"
#include "VRCharacter.h"
#include "VRRootComponent.h"
#include "AITypes.h"
#include "GripMotionControllerComponent.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "VRPlayerController.h"
#include "GameFramework/PhysicsVolume.h"
#include "Animation/AnimInstance.h"


DEFINE_LOG_CATEGORY(LogVRBaseCharacterMovement);

UVRBaseCharacterMovementComponent::UVRBaseCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.TickGroup = TG_PrePhysics;


	//#TODO: Might not be ready to make this change globally yet....

	// Set Acceleration and braking deceleration walking to high values to avoid ramp up on speed
	// Realized that I wasn't doing this here for people to default to no acceleration.
	/*this->bRequestedMoveUseAcceleration = false;
	this->MaxAcceleration = 200048.0f;
	this->BrakingDecelerationWalking = 200048.0f;
	*/

	AdditionalVRInputVector = FVector::ZeroVector;	
	CustomVRInputVector = FVector::ZeroVector;
	TrackingLossThreshold = 6000.f;
	bApplyAdditionalVRInputVectorAsNegative = true;
	bHadExtremeInput = false;
	bHoldPositionOnTrackingLossThresholdHit = false;

	VRClimbingStepHeight = 96.0f;
	VRClimbingEdgeRejectDistance = 5.0f;
	VRClimbingStepUpMultiplier = 1.0f;
	bClampClimbingStepUp = false;
	VRClimbingStepUpMaxSize = 20.0f;

	VRClimbingMaxReleaseVelocitySize = 800.0f;
	SetDefaultPostClimbMovementOnStepUp = true;
	DefaultPostClimbMovement = EVRConjoinedMovementModes::C_MOVE_Falling;

	bIgnoreSimulatingComponentsInFloorCheck = true;

	VRWallSlideScaler = 1.0f;
	VRLowGravWallFrictionScaler = 1.0f;
	VRLowGravIgnoresDefaultFluidFriction = true;

	VREdgeRejectDistance = 0.01f; // Rounded minimum of root movement

	VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;

	NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;

	bWasInPushBack = false;
	bIsInPushBack = false;

	bRunControlRotationInMovementComponent = true;

	// Allow merging dual movements, generally this is wanted for the perf increase
	bEnableServerDualMoveScopedMovementUpdates = true;

	bNotifyTeleported = false;

	bJustUnseated = false;

	bUseClientControlRotation = true;
	bDisableSimulatedTickWhenSmoothingMovement = true;
	bCapHMDMovementToMaxMovementSpeed = false;

	SetNetworkMoveDataContainer(VRNetworkMoveDataContainer);
	SetMoveResponseDataContainer(VRMoveResponseDataContainer);
}

// Rewind the relative movement that we had with the HMD
void UVRBaseCharacterMovementComponent::RewindVRRelativeMovement()
{
	if (bApplyAdditionalVRInputVectorAsNegative && (BaseVRCharacterOwner && BaseVRCharacterOwner->bRetainRoomscale))
	{
		//FHitResult AHit;
		MoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false);
		//SafeMoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false, AHit);
	}
}

void UVRBaseCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (!HasValidData())
	{
		return;
	}

	// Clear out the old custom input vector, it will pollute the pool now that all modes allow it.
	CustomVRInputVector = FVector::ZeroVector;

	if (PreviousMovementMode == EMovementMode::MOVE_Custom && PreviousCustomMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated)
	{
		if (MovementMode != EMovementMode::MOVE_Custom || CustomMovementMode != (uint8)EVRCustomMovementMode::VRMOVE_Seated)
		{
			if (AVRBaseCharacter * BaseOwner = Cast<AVRBaseCharacter>(CharacterOwner))
			{
				BaseOwner->InitSeatedModeTransition();
			}
		}
	}
	
	if (MovementMode == EMovementMode::MOVE_Custom)
	{
		if (CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing || CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated)
		{
			// Kill velocity and clear queued up events
			StopMovementKeepPathing();
			CharacterOwner->ResetJumpState();
			ClearAccumulatedForces();

			if (CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated)
			{
				if (AVRBaseCharacter * BaseOwner = Cast<AVRBaseCharacter>(CharacterOwner))
				{
					BaseOwner->InitSeatedModeTransition();
				}
			}
		}
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

bool UVRBaseCharacterMovementComponent::ForcePositionUpdate(float DeltaTime)
{
	// Skip force updating position if we are seated.
	if ((MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated))
	{
		return false;
	}

	return Super::ForcePositionUpdate(DeltaTime);
}

bool UVRBaseCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementClientUpdatePositionAfterServerUpdate);
	if (!HasValidData())
	{
		return false;
	}

	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	check(ClientData);

	if (!ClientData->bUpdatePosition)
	{
		return false;
	}

	ClientData->bUpdatePosition = false;

	// Don't do any network position updates on things running PHYS_RigidBody
	if (CharacterOwner->GetRootComponent() && CharacterOwner->GetRootComponent()->IsSimulatingPhysics())
	{
		return false;
	}

	if (ClientData->SavedMoves.Num() == 0)
	{
		UE_LOG(LogNetPlayerMovement, Verbose, TEXT("ClientUpdatePositionAfterServerUpdate No saved moves to replay"), ClientData->SavedMoves.Num());

		// With no saved moves to resimulate, the move the server updated us with is the last move we've done, no resimulation needed.
		CharacterOwner->bClientResimulateRootMotion = false;
		if (CharacterOwner->bClientResimulateRootMotionSources)
		{
			// With no resimulation, we just update our current root motion to what the server sent us
			UE_LOG(LogRootMotion, VeryVerbose, TEXT("CurrentRootMotion getting updated to ServerUpdate state: %s"), *CharacterOwner->GetName());
			CurrentRootMotion.UpdateStateFrom(CharacterOwner->SavedRootMotion);
			CharacterOwner->bClientResimulateRootMotionSources = false;
		}
		CharacterOwner->SavedRootMotion.Clear();

		return false;
	}

	// Save important values that might get affected by the replay.
	const float SavedAnalogInputModifier = AnalogInputModifier;
	const FRootMotionMovementParams BackupRootMotionParams = RootMotionParams; // For animation root motion
	const FRootMotionSourceGroup BackupRootMotion = CurrentRootMotion;
	const bool bRealPressedJump = CharacterOwner->bPressedJump;
	const float RealJumpMaxHoldTime = CharacterOwner->JumpMaxHoldTime;
	const int32 RealJumpMaxCount = CharacterOwner->JumpMaxCount;
	const bool bRealCrouch = bWantsToCrouch;
	const bool bRealForceMaxAccel = bForceMaxAccel;
	CharacterOwner->bClientWasFalling = (MovementMode == MOVE_Falling);
	CharacterOwner->bClientUpdating = true;
	bForceNextFloorCheck = true;

	// Store out our custom properties to restore after replaying
	const FVRMoveActionArray Orig_MoveActions = MoveActionArray;
	const FVector Orig_CustomInput = CustomVRInputVector;
	const EVRConjoinedMovementModes Orig_VRReplicatedMovementMode = VRReplicatedMovementMode;
	const FVector Orig_RequestedVelocity = RequestedVelocity;
	const bool Orig_HasRequestedVelocity = HasRequestedVelocity();

	// Replay moves that have not yet been acked.
	UE_LOG(LogNetPlayerMovement, Verbose, TEXT("ClientUpdatePositionAfterServerUpdate Replaying %d Moves, starting at Timestamp %f"), ClientData->SavedMoves.Num(), ClientData->SavedMoves[0]->TimeStamp);
	for (int32 i = 0; i < ClientData->SavedMoves.Num(); i++)
	{
		FSavedMove_Character* const CurrentMove = ClientData->SavedMoves[i].Get();
		checkSlow(CurrentMove != nullptr);

		// Make current SavedMove accessible to any functions that might need it.
		SetCurrentReplayedSavedMove(CurrentMove);

		CurrentMove->PrepMoveFor(CharacterOwner);

		if (ShouldUsePackedMovementRPCs())
		{
			// Make current move data accessible to MoveAutonomous or any other functions that might need it.
			if (FCharacterNetworkMoveData* NewMove = GetNetworkMoveDataContainer().GetNewMoveData())
			{
				SetCurrentNetworkMoveData(NewMove);
				NewMove->ClientFillNetworkMoveData(*CurrentMove, FCharacterNetworkMoveData::ENetworkMoveType::NewMove);
			}
		}

		MoveAutonomous(CurrentMove->TimeStamp, CurrentMove->DeltaTime, CurrentMove->GetCompressedFlags(), CurrentMove->Acceleration);

		CurrentMove->PostUpdate(CharacterOwner, FSavedMove_Character::PostUpdate_Replay);
		SetCurrentNetworkMoveData(nullptr);
		SetCurrentReplayedSavedMove(nullptr);
	}
	const bool bPostReplayPressedJump = CharacterOwner->bPressedJump;

	if (FSavedMove_Character* const PendingMove = ClientData->PendingMove.Get())
	{
		PendingMove->bForceNoCombine = true;
	}

	// Restore saved values.
	AnalogInputModifier = SavedAnalogInputModifier;
	RootMotionParams = BackupRootMotionParams;
	CurrentRootMotion = BackupRootMotion;
	if (CharacterOwner->bClientResimulateRootMotionSources)
	{
		// If we were resimulating root motion sources, it's because we had mismatched state
		// with the server - we just resimulated our SavedMoves and now need to restore
		// CurrentRootMotion with the latest "good state"
		UE_LOG(LogRootMotion, VeryVerbose, TEXT("CurrentRootMotion getting updated after ServerUpdate replays: %s"), *CharacterOwner->GetName());
		CurrentRootMotion.UpdateStateFrom(CharacterOwner->SavedRootMotion);
		CharacterOwner->bClientResimulateRootMotionSources = false;
	}
	CharacterOwner->SavedRootMotion.Clear();
	CharacterOwner->bClientResimulateRootMotion = false;
	CharacterOwner->bClientUpdating = false;
	CharacterOwner->bPressedJump = bRealPressedJump || bPostReplayPressedJump;
	CharacterOwner->JumpMaxHoldTime = RealJumpMaxHoldTime;
	CharacterOwner->JumpMaxCount = RealJumpMaxCount;
	bWantsToCrouch = bRealCrouch;
	bForceMaxAccel = bRealForceMaxAccel;
	bForceNextFloorCheck = true;

	// Restore our move actions
	MoveActionArray = Orig_MoveActions;
	CustomVRInputVector = Orig_CustomInput;
	VRReplicatedMovementMode = Orig_VRReplicatedMovementMode;
	RequestedVelocity = Orig_RequestedVelocity;
	SetHasRequestedVelocity(Orig_HasRequestedVelocity);

	return (ClientData->SavedMoves.Num() > 0);
}

void UVRBaseCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{

	// Skip calling into BP if we aren't locally controlled
	if (CharacterOwner->IsLocallyControlled() && GetReplicatedMovementMode() == EVRConjoinedMovementModes::C_VRMOVE_Climbing)
	{
		// Allow the player to run updates on the climb logic for CustomVRInputVector
		if (BaseVRCharacterOwner)
		{
			BaseVRCharacterOwner->UpdateClimbingMovement(DeltaTime);
		}
	}

	// Scope all of the movements, including PRC
	{
		UParentRelativeAttachmentComponent* OuterScopePRC = nullptr;
		if (BaseVRCharacterOwner && BaseVRCharacterOwner->ParentRelativeAttachment && BaseVRCharacterOwner->ParentRelativeAttachment->bUpdateInCharacterMovement)
		{
			OuterScopePRC = BaseVRCharacterOwner->ParentRelativeAttachment;
		}

		FScopedMovementUpdate ScopedPRCMovementUpdate(OuterScopePRC, EScopedUpdate::DeferredUpdates);
		
		{
			UReplicatedVRCameraComponent* OuterScopeCamera = nullptr;
			if (BaseVRCharacterOwner && BaseVRCharacterOwner->VRReplicatedCamera)
			{
				OuterScopeCamera = BaseVRCharacterOwner->VRReplicatedCamera;
			}

			FScopedMovementUpdate ScopedCameraMovementUpdate(OuterScopeCamera, EScopedUpdate::DeferredUpdates);

			// Scope in the character movements first
			{
				// Scope these, they nest with Outer references so it should work fine
				FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

				if (MovementMode == MOVE_Custom && CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated)
				{

					//#TODO 5.0: Handle this?
					/*FVector InputVector = FVector::ZeroVector;
					bool bUsingAsyncTick = (CharacterMovementCVars::AsyncCharacterMovement == 1) && IsAsyncCallbackRegistered();
					if (!bUsingAsyncTick)
					{
						// Do not consume input if simulating asynchronously, we will consume input when filling out async inputs.
						InputVector = ConsumeInputVector();
					}*/


					const FVector InputVector = ConsumeInputVector();
					if (!HasValidData() || ShouldSkipUpdate(DeltaTime))
					{
						return;
					}

					// Skip the perform movement logic, run the re-seat logic instead - running base movement component tick instead
					Super::Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

					// See if we fell out of the world.
					const bool bIsSimulatingPhysics = UpdatedComponent->IsSimulatingPhysics();
					if (CharacterOwner->GetLocalRole() == ROLE_Authority && (!bCheatFlying || bIsSimulatingPhysics) && !CharacterOwner->CheckStillInWorld())
					{
						return;
					}

					// If we are the owning client or the server then run the re-basing
					if (CharacterOwner->GetLocalRole() > ROLE_SimulatedProxy)
					{
						// Run offset logic here, the server will update simulated proxies with the movement replication
						if (AVRBaseCharacter* BaseChar = Cast<AVRBaseCharacter>(CharacterOwner))
						{
							BaseChar->TickSeatInformation(DeltaTime);
						}

						// Handle move actions here - Should be scoped
						CheckForMoveAction();
						MoveActionArray.Clear();

						if (CharacterOwner && !CharacterOwner->IsLocallyControlled() && DeltaTime > 0.0f)
						{
							// If not playing root motion, tick animations after physics. We do this here to keep events, notifies, states and transitions in sync with client updates.
							if (!CharacterOwner->bClientUpdating && !CharacterOwner->IsPlayingRootMotion() && CharacterOwner->GetMesh())
							{
								TickCharacterPose(DeltaTime);
								// TODO: SaveBaseLocation() in case tick moves us?

								// Trigger Events right away, as we could be receiving multiple ServerMoves per frame.
								CharacterOwner->GetMesh()->ConditionallyDispatchQueuedAnimEvents();
							}
						}

					}
					else
					{
						if (bNetworkUpdateReceived)
						{
							if (bNetworkMovementModeChanged)
							{
								ApplyNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode());
								bNetworkMovementModeChanged = false;
							}
						}
					}
				}
				else
					Super::TickComponent(DeltaTime, TickType, ThisTickFunction);


				// This should be valid for both Simulated and owning clients as well as the server
				// Better here than in perform movement
				if (UVRRootComponent* VRRoot = Cast<UVRRootComponent>(CharacterOwner->GetCapsuleComponent()))
				{
					// If we didn't move the capsule, have it update itself here so the visual and physics representation is correct
					// We do this specifically to avoid double calling into the render / physics threads.
					if (!VRRoot->bCalledUpdateTransform)
						VRRoot->OnUpdateTransform_Public(EUpdateTransformFlags::None, ETeleportType::None);
				}

				// Make sure these are cleaned out for the next frame
				AdditionalVRInputVector = FVector::ZeroVector;
				CustomVRInputVector = FVector::ZeroVector;
			}

			if (bRunControlRotationInMovementComponent && CharacterOwner->IsLocallyControlled())
			{
				if (BaseVRCharacterOwner)
				{
					if (BaseVRCharacterOwner->VRReplicatedCamera && BaseVRCharacterOwner->VRReplicatedCamera->bUsePawnControlRotation)
					{
						const AController* OwningController = BaseVRCharacterOwner->GetController();
						if (OwningController)
						{
							const FRotator PawnViewRotation = BaseVRCharacterOwner->GetViewRotation();
							if (!PawnViewRotation.Equals(BaseVRCharacterOwner->VRReplicatedCamera->GetComponentRotation()))
							{
								BaseVRCharacterOwner->VRReplicatedCamera->SetWorldRotation(PawnViewRotation);
							}
						}
					}
				}
			}

			// If some of our important components run inside the cmc updates then lets update them now
			if (OuterScopeCamera)
			{
				OuterScopeCamera->UpdateTracking(DeltaTime);
			}

			if (OuterScopePRC)
			{
				OuterScopePRC->UpdateTracking(DeltaTime);
			}
		}
	}

	if (bNotifyTeleported)
	{
		if (BaseVRCharacterOwner)
		{
			BaseVRCharacterOwner->OnCharacterTeleported_Bind.Broadcast();
			bNotifyTeleported = false;
		}
	}
}

bool UVRBaseCharacterMovementComponent::VerifyClientTimeStamp(float TimeStamp, FNetworkPredictionData_Server_Character & ServerData)
{
	// Server is auth on seated mode and we want to ignore incoming pending movements after we have decided to set the client to seated mode
	if (MovementMode == MOVE_Custom && CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated)
		return false;

	return Super::VerifyClientTimeStamp(TimeStamp, ServerData);
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

FVector UVRBaseCharacterMovementComponent::GetActorFeetLocationVR() const
{

	const UCapsuleComponent* const CapsuleComponent = CharacterOwner ? CharacterOwner->GetCapsuleComponent() : Cast<UCapsuleComponent>(UpdatedComponent);
	if (CapsuleComponent)
	{
		const float HalfHeight = CapsuleComponent->GetScaledCapsuleHalfHeight();
		if (AVRBaseCharacter* BaseCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
		{
			return BaseCharacter->OffsetComponentToWorld.GetLocation() + HalfHeight * GetGravityDirection();
		}
		else
		{
			return UpdatedComponent->GetComponentLocation() + HalfHeight * GetGravityDirection();
		}
	}

	return Super::GetActorFeetLocation();


	/*if (AVRBaseCharacter* BaseCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		return UpdatedComponent ? (BaseCharacter->OffsetComponentToWorld.GetLocation() - FVector(0, 0, UpdatedComponent->Bounds.BoxExtent.Z)) : FNavigationSystem::InvalidLocation;
	}
	else
	{
		return UpdatedComponent ? (UpdatedComponent->GetComponentLocation() - FVector(0, 0, UpdatedComponent->Bounds.BoxExtent.Z)) : FNavigationSystem::InvalidLocation;
	}*/
}

void UVRBaseCharacterMovementComponent::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	if (AVRBaseCharacter* vrOwner = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		vrOwner->NavigationMoveCompleted(RequestID, Result);
	}
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

	// #TODO: Report to epic that their FloorSweepTest was not using the gravity rotation so it didn't work when the capsule was out of range

	if (!bUseFlatBaseForFloorChecks)
	{
		bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, GetWorldToGravityTransform(), TraceChannel, CollisionShape, Params, ResponseParam);
	}
	else
	{
		// Test with a box that is enclosed by the capsule.
		const float CapsuleRadius = CollisionShape.GetCapsuleRadius();
		const float CapsuleHeight = CollisionShape.GetCapsuleHalfHeight();
		const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(CapsuleRadius * 0.707f, CapsuleRadius * 0.707f, CapsuleHeight));

		// First test with the box rotated so the corners are along the major axes (ie rotated 45 degrees).
		//bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat(RotateGravityToWorld(FVector(0.f, 0.f, -1.f)), UE_PI * 0.25f), TraceChannel, BoxShape, Params, ResponseParam);
		bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat(GetGravityDirection(), UE_PI * 0.25f), TraceChannel, BoxShape, Params, ResponseParam);

		if (!bBlockingHit)
		{
			// Test again with the same box, not rotated.
			OutHit.Reset(1.f, false);
			bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, GetWorldToGravityTransform(), TraceChannel, BoxShape, Params, ResponseParam);
		}
	}

	return bBlockingHit;
}

void UVRBaseCharacterMovementComponent::ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult) const
{
	UE_LOG(LogVRBaseCharacterMovement, VeryVerbose, TEXT("[Role:%d] ComputeFloorDist: %s at location %s"), (int32)CharacterOwner->GetLocalRole(), *GetNameSafe(CharacterOwner), *CapsuleLocation.ToString());
	OutFloorResult.Clear();

	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	bool bSkipSweep = false;
	if (DownwardSweepResult != NULL && DownwardSweepResult->IsValidBlockingHit())
	{
		// Only if the supplied sweep was vertical and downward.
		const bool bIsDownward = GetGravitySpaceZ(DownwardSweepResult->TraceStart - DownwardSweepResult->TraceEnd) > 0;
		const bool bIsVertical = ProjectToGravityFloor(DownwardSweepResult->TraceStart - DownwardSweepResult->TraceEnd).SizeSquared() <= UE_KINDA_SMALL_NUMBER;
		if (bIsDownward && bIsVertical)
		{
			// Reject hits that are barely on the cusp of the radius of the capsule
			if (IsWithinEdgeTolerance(DownwardSweepResult->Location, DownwardSweepResult->ImpactPoint, PawnRadius))
			{
				// Don't try a redundant sweep, regardless of whether this sweep is usable.
				bSkipSweep = true;

				const bool bIsWalkable = IsWalkable(*DownwardSweepResult);
				const float FloorDist = GetGravitySpaceZ(CapsuleLocation - DownwardSweepResult->Location);
				OutFloorResult.SetFromSweep(*DownwardSweepResult, FloorDist, bIsWalkable);

				if (bIsWalkable)
				{
					// Use the supplied downward sweep as the floor hit result.			
					return;
				}
			}
		}
	}

	// We require the sweep distance to be >= the line distance, otherwise the HitResult can't be interpreted as the sweep result.
	if (SweepDistance < LineDistance)
	{
		ensure(SweepDistance >= LineDistance);
		return;
	}

	bool bBlockingHit = false;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ComputeFloorDist), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(QueryParams, ResponseParam);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();

	// Skip physics bodies for floor check if we are skipping simulated objects
	if (bIgnoreSimulatingComponentsInFloorCheck)
		ResponseParam.CollisionResponse.PhysicsBody = ECollisionResponse::ECR_Ignore;

	// Sweep test
	if (!bSkipSweep && SweepDistance > 0.f && SweepRadius > 0.f)
	{
		// Use a shorter height to avoid sweeps giving weird results if we start on a surface.
		// This also allows us to adjust out of penetrations.
		const float ShrinkScale = 0.9f;
		const float ShrinkScaleOverlap = 0.1f;
		float ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.f - ShrinkScale);
		float TraceDist = SweepDistance + ShrinkHeight;
		FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(SweepRadius, PawnHalfHeight - ShrinkHeight);

		FHitResult Hit(1.f);
		bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + TraceDist * GetGravityDirection(), CollisionChannel, CapsuleShape, QueryParams, ResponseParam);

		if (bBlockingHit)
		{
			// Reject hits adjacent to us, we only care about hits on the bottom portion of our capsule.
			// Check 2D distance to impact point, reject if within a tolerance from radius.
			if (Hit.bStartPenetrating || !IsWithinEdgeTolerance(CapsuleLocation, Hit.ImpactPoint, CapsuleShape.Capsule.Radius))
			{
				// Use a capsule with a slightly smaller radius and shorter height to avoid the adjacent object.
				// Capsule must not be nearly zero or the trace will fall back to a line trace from the start point and have the wrong length.
				CapsuleShape.Capsule.Radius = FMath::Max(0.f, CapsuleShape.Capsule.Radius - SWEEP_EDGE_REJECT_DISTANCE - UE_KINDA_SMALL_NUMBER);
				if (!CapsuleShape.IsNearlyZero())
				{
					ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.f - ShrinkScaleOverlap);
					TraceDist = SweepDistance + ShrinkHeight;
					CapsuleShape.Capsule.HalfHeight = FMath::Max(PawnHalfHeight - ShrinkHeight, CapsuleShape.Capsule.Radius);
					Hit.Reset(1.f, false);

					bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + TraceDist * GetGravityDirection(), CollisionChannel, CapsuleShape, QueryParams, ResponseParam);
				}
			}

			// Reduce hit distance by ShrinkHeight because we shrank the capsule for the trace.
			// We allow negative distances here, because this allows us to pull out of penetrations.
			const float MaxPenetrationAdjust = FMath::Max(MAX_FLOOR_DIST, PawnRadius);
			const float SweepResult = FMath::Max(-MaxPenetrationAdjust, Hit.Time * TraceDist - ShrinkHeight);

			OutFloorResult.SetFromSweep(Hit, SweepResult, false);
			if (Hit.IsValidBlockingHit() && IsWalkable(Hit))
			{
				if (SweepResult <= SweepDistance)
				{
					// Hit within test distance.
					OutFloorResult.bWalkableFloor = true;
					return;
				}
			}
		}
	}

	// Since we require a longer sweep than line trace, we don't want to run the line trace if the sweep missed everything.
	// We do however want to try a line trace if the sweep was stuck in penetration.
	if (!OutFloorResult.bBlockingHit && !OutFloorResult.HitResult.bStartPenetrating)
	{
		OutFloorResult.FloorDist = SweepDistance;
		return;
	}

	// Line trace
	if (LineDistance > 0.f)
	{
		const float ShrinkHeight = PawnHalfHeight;
		const FVector LineTraceStart = CapsuleLocation;
		const float TraceDist = LineDistance + ShrinkHeight;
		const FVector Down = TraceDist * GetGravityDirection();
		QueryParams.TraceTag = SCENE_QUERY_STAT_NAME_ONLY(FloorLineTrace);

		FHitResult Hit(1.f);
		bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, LineTraceStart, LineTraceStart + Down, CollisionChannel, QueryParams, ResponseParam);

		if (bBlockingHit)
		{
			if (Hit.Time > 0.f)
			{
				// Reduce hit distance by ShrinkHeight because we started the trace higher than the base.
				// We allow negative distances here, because this allows us to pull out of penetrations.
				const float MaxPenetrationAdjust = FMath::Max(MAX_FLOOR_DIST, PawnRadius);
				const float LineResult = FMath::Max(-MaxPenetrationAdjust, Hit.Time * TraceDist - ShrinkHeight);

				OutFloorResult.bBlockingHit = true;
				if (LineResult <= LineDistance && IsWalkable(Hit))
				{
					OutFloorResult.SetFromLineTrace(Hit, OutFloorResult.FloorDist, LineResult, true);
					return;
				}
			}
		}
	}

	// No hits were acceptable.
	OutFloorResult.bWalkableFloor = false;
}


float UVRBaseCharacterMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& InNormal, FHitResult& Hit, bool bHandleImpact)
{
	// Am running the CharacterMovementComponents calculations manually here now prior to scaling down the delta

	if (!Hit.bBlockingHit)
	{
		return 0.f;
	}

	FVector Normal(InNormal);
	const FVector::FReal NormalZ = GetGravitySpaceZ(Normal);
	if (IsMovingOnGround())
	{
		// We don't want to be pushed up an unwalkable surface.
		if (NormalZ > 0.f)
		{
			if (!IsWalkable(Hit))
			{
				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
			}
		}
		else if (NormalZ < -UE_KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule.
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.f && (GetGravitySpaceZ(FloorNormal) < 1.f - UE_DELTA);

				if (bFloorOpposedToMovement)
				{
					Normal = FloorNormal;
				}

				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
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

/*void UVRBaseCharacterMovementComponent::SetCrouchedHalfHeight(float NewCrouchedHalfHeight)
{
	this->CrouchedHalfHeight = NewCrouchedHalfHeight;
}*/

void UVRBaseCharacterMovementComponent::AddCustomReplicatedMovement(FVector Movement)
{
	// if we are a client then lets round this to match what it will be after net Replication
	if (GetNetMode() == NM_Client)
		CustomVRInputVector += RoundDirectMovement(Movement);
	else
		CustomVRInputVector += Movement; // If not a client, don't bother to round this down.
}

void UVRBaseCharacterMovementComponent::ClearCustomReplicatedMovement()
{
	CustomVRInputVector = FVector::ZeroVector;
}

void UVRBaseCharacterMovementComponent::CheckServerAuthedMoveAction()
{
	// If we are calling this on the server on a non owned character, there is no reason to wait around, just do the action now
	// If we ARE locally controlled, keep the action inline with the movement component to maintain consistency
	if (GetNetMode() < NM_Client)
	{
		ACharacter* OwningChar = GetCharacterOwner();
		if (OwningChar && !OwningChar->IsLocallyControlled())
		{
			CheckForMoveAction();
			MoveActionArray.Clear();
		}
	}
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_SetTrackingPaused(bool bNewTrackingPaused)
{
	StoreSetTrackingPaused(bNewTrackingPaused);
}

void UVRBaseCharacterMovementComponent::StoreSetTrackingPaused(bool bNewTrackingPaused)
{
	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_PauseTracking;
	MoveAction.MoveActionFlags = bNewTrackingPaused;
	MoveActionArray.MoveActions.Add(MoveAction);
	CheckServerAuthedMoveAction();
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_SnapTurn(float DeltaYawAngle, EVRMoveActionVelocityRetention VelocityRetention, bool bFlagGripTeleport, bool bFlagCharacterTeleport, bool bRotateAroundCapsule )
{
	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_SnapTurn; 
	
	// Removed 2 decimal precision rounding in favor of matching the actual replicated short fidelity instead.
	// MoveAction.MoveActionRot = FRotator(0.0f, FMath::RoundToFloat(((FRotator(0.f,DeltaYawAngle, 0.f).Quaternion() * UpdatedComponent->GetComponentQuat()).Rotator().Yaw) * 100.f) / 100.f, 0.0f);

	// Setting to the exact same fidelity as the replicated value ends up being, losing some precision
	FRotator TargetRotation = (UpdatedComponent->GetComponentQuat() * FRotator(0.f, DeltaYawAngle, 0.f).Quaternion()).Rotator();
	TargetRotation.Yaw = FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(TargetRotation.Yaw));
	TargetRotation.Pitch = FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(TargetRotation.Pitch));
	TargetRotation.Roll = FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(TargetRotation.Roll));
	MoveAction.MoveActionRot = TargetRotation;
	//MoveAction.MoveActionRot = FRotator( 0.0f, FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(DeltaYawAngle)), 0.0f);
		//FRotator(0.0f, FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort((FRotator(0.f, DeltaYawAngle, 0.f).Quaternion() * UpdatedComponent->GetComponentQuat()).Rotator().Yaw)), 0.0f);

	if (bFlagCharacterTeleport)
		MoveAction.MoveActionFlags = 0x02;// .MoveActionRot.Roll = 2.0f;
	else if(bFlagGripTeleport)
		MoveAction.MoveActionFlags = 0x01;//MoveActionRot.Roll = bFlagGripTeleport ? 1.0f : 0.0f;

	if (bRotateAroundCapsule)
	{
		MoveAction.MoveActionFlags |= 0x08;
	}

	if (VelocityRetention == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
	{
		//MoveAction.MoveActionRot.Pitch = FMath::RoundToFloat(DeltaYawAngle * 100.f) / 100.f;
		//MoveAction.MoveActionRot.Pitch = DeltaYawAngle;
		MoveAction.MoveActionDeltaYaw = FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(DeltaYawAngle));
	}

	MoveAction.VelRetentionSetting = VelocityRetention;

	MoveActionArray.MoveActions.Add(MoveAction);
	CheckServerAuthedMoveAction();
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_SetRotation(float NewYaw, EVRMoveActionVelocityRetention VelocityRetention, bool bFlagGripTeleport, bool bFlagCharacterTeleport, bool bRotateAroundCapsule)
{
	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_SetRotation;
	MoveAction.MoveActionRot = FRotator(0.0f, FMath::RoundToFloat(NewYaw * 100.f) / 100.f, 0.0f);

	if (bFlagCharacterTeleport)
		MoveAction.MoveActionFlags = 0x02;// .MoveActionRot.Roll = 2.0f;
	else if (bFlagGripTeleport)
		MoveAction.MoveActionFlags = 0x01;//MoveActionRot.Roll = bFlagGripTeleport ? 1.0f : 0.0f;

	if (bRotateAroundCapsule)
	{
		MoveAction.MoveActionFlags |= 0x08;
	}

	if (VelocityRetention == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
	{
		float DeltaYawAngle = FMath::FindDeltaAngleDegrees(UpdatedComponent->GetComponentRotation().Yaw, NewYaw);
		//MoveAction.MoveActionRot.Pitch = FMath::RoundToFloat(DeltaYawAngle * 100.f) / 100.f;
		MoveAction.MoveActionRot.Pitch = DeltaYawAngle;
	}

	MoveAction.VelRetentionSetting = VelocityRetention;

	MoveActionArray.MoveActions.Add(MoveAction);
	CheckServerAuthedMoveAction();
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_Teleport(FVector TeleportLocation, FRotator TeleportRotation, EVRMoveActionVelocityRetention VelocityRetention,  bool bSkipEncroachmentCheck)
{
	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_Teleport;
	MoveAction.MoveActionLoc = RoundDirectMovement(TeleportLocation);
	MoveAction.MoveActionRot.Yaw = FMath::RoundToFloat(TeleportRotation.Yaw * 100.f) / 100.f;
	MoveAction.MoveActionFlags |= (uint8)bSkipEncroachmentCheck;//.MoveActionRot.Roll = bSkipEncroachmentCheck ? 1.0f : 0.0f;

	if (VelocityRetention == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
	{
		float DeltaYawAngle = FMath::FindDeltaAngleDegrees(UpdatedComponent->GetComponentRotation().Yaw, TeleportRotation.Yaw);
		//MoveAction.MoveActionRot.Pitch = FMath::RoundToFloat(DeltaYawAngle * 100.f) / 100.f;
		MoveAction.MoveActionRot.Pitch = DeltaYawAngle;
	}

	MoveAction.VelRetentionSetting = VelocityRetention;

	MoveActionArray.MoveActions.Add(MoveAction);
	CheckServerAuthedMoveAction();
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_StopAllMovement()
{
	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_StopAllMovement;
	MoveActionArray.MoveActions.Add(MoveAction);

	CheckServerAuthedMoveAction();
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_SetGravityDirection(FVector NewGravityDirection, bool bOrientToNewGravity)
{
	if (NewGravityDirection.IsNearlyZero())
	{
		return;
	}

	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_SetGravityDirection;
	MoveAction.MoveActionVel = NewGravityDirection.GetSafeNormal();
	MoveAction.MoveActionFlags |= (uint8)bOrientToNewGravity;
	MoveActionArray.MoveActions.Add(MoveAction);

	CheckServerAuthedMoveAction();
}

void UVRBaseCharacterMovementComponent::PerformMoveAction_Custom(EVRMoveAction MoveActionToPerform, EVRMoveActionDataReq DataRequirementsForMoveAction, FVector MoveActionVector, FRotator MoveActionRotator, uint8 MoveActionFlags)
{
	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = MoveActionToPerform;

	// Round the vector to 2 decimal precision
	MoveAction.MoveActionLoc = RoundDirectMovement(MoveActionVector);
	MoveAction.MoveActionRot = MoveActionRotator;
	MoveAction.MoveActionDataReq = DataRequirementsForMoveAction;
	MoveAction.MoveActionFlags = MoveActionFlags;
	MoveActionArray.MoveActions.Add(MoveAction);

	CheckServerAuthedMoveAction();
}

bool UVRBaseCharacterMovementComponent::CheckForMoveAction()
{
	if (!BaseVRCharacterOwner)
		return true;

	for (FVRMoveActionContainer& MoveAction : MoveActionArray.MoveActions)
	{
		switch (MoveAction.MoveAction)
		{
		case EVRMoveAction::VRMOVEACTION_SnapTurn:
		{
			/*return */DoMASnapTurn(MoveAction);
		}break;
		case EVRMoveAction::VRMOVEACTION_Teleport:
		{
			if (!BaseVRCharacterOwner->SeatInformation.bSitting)
			{
				/*return */DoMATeleport(MoveAction);
			}
		}break;
		case EVRMoveAction::VRMOVEACTION_StopAllMovement:
		{
			/*return */DoMAStopAllMovement(MoveAction);
		}break;
		case EVRMoveAction::VRMOVEACTION_SetGravityDirection:
		{
			if (!BaseVRCharacterOwner->SeatInformation.bSitting)
			{
				/*return */DoMASetGravityDirection(MoveAction);
			}
		}break;
		case EVRMoveAction::VRMOVEACTION_SetRotation:
		{
			if (!BaseVRCharacterOwner->SeatInformation.bSitting)
			{
				/*return */DoMASetRotation(MoveAction);
			}
		}break;
		case EVRMoveAction::VRMOVEACTION_PauseTracking:
		{
			/*return */DoMAPauseTracking(MoveAction);
		}break;
		case EVRMoveAction::VRMOVEACTION_None:
		{}break;
		default: // All other move actions (CUSTOM)
		{
			if (BaseVRCharacterOwner)
			{
				BaseVRCharacterOwner->OnCustomMoveActionPerformed(MoveAction.MoveAction, MoveAction.MoveActionLoc, MoveAction.MoveActionRot, MoveAction.MoveActionFlags);
			}
		}break;
		}
	}

	return true;
}

bool UVRBaseCharacterMovementComponent::DoMASnapTurn(FVRMoveActionContainer& MoveAction)
{
	if (BaseVRCharacterOwner)
	{	
		FRotator TargetRot = MoveAction.MoveActionRot;
		FQuat OrigRot = BaseVRCharacterOwner->GetActorQuat();

		if (BaseVRCharacterOwner->SeatInformation.bSitting)
		{
			FRotator DeltaRot(0.f, MoveAction.MoveActionDeltaYaw, 0.f);
			TargetRot = ( OrigRot * DeltaRot.Quaternion() ).Rotator();
		}

		FTransform OriginalRelativeTrans = BaseVRCharacterOwner->GetRootComponent()->GetRelativeTransform();

		bool bRotateAroundCapsule = MoveAction.MoveActionFlags & 0x08;

		// Clamp to 2 decimal precision
		/*TargetRot = TargetRot.Clamp();
		TargetRot.Pitch = (TargetRot.Pitch * 100.f) / 100.f;
		TargetRot.Yaw = (TargetRot.Yaw * 100.f) / 100.f;
		TargetRot.Roll = (TargetRot.Roll * 100.f) / 100.f;
		TargetRot.Normalize();*/
		bIsBlendingOrientation = true;

		if (this->BaseVRCharacterOwner && this->BaseVRCharacterOwner->IsLocallyControlled())
		{
			if (this->bUseClientControlRotation)
			{
				MoveAction.MoveActionLoc = BaseVRCharacterOwner->SetActorRotationVR(TargetRot, false, false, bRotateAroundCapsule);
				MoveAction.MoveActionFlags |= 0x04; // Flag that we are using loc only
			}
			else
			{
				BaseVRCharacterOwner->SetActorRotationVR(TargetRot, false, false, bRotateAroundCapsule);
			}
		}
		else
		{
			if (MoveAction.MoveActionFlags & 0x04)
			{
				BaseVRCharacterOwner->SetActorLocation(BaseVRCharacterOwner->GetActorLocation() + MoveAction.MoveActionLoc);
			}
			else
			{
				BaseVRCharacterOwner->SetActorRotationVR(TargetRot, false, false, bRotateAroundCapsule);
			}
		}

		switch (MoveAction.VelRetentionSetting)
		{
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None:
		{

		}break;
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Clear:
		{
			this->Velocity = FVector::ZeroVector;
		}break;
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn:
		{	
			if (BaseVRCharacterOwner->IsLocallyControlled())
			{
				MoveAction.MoveActionVel = RoundDirectMovement((TargetRot.Quaternion() * OrigRot.Inverse()).RotateVector(this->Velocity));
				this->Velocity = MoveAction.MoveActionVel;
			}
			else
			{
				this->Velocity = MoveAction.MoveActionVel;
			}
		}break;
		}

		// If we are flagged to teleport the grips
		if (MoveAction.MoveActionFlags & 0x01 || MoveAction.MoveActionFlags & 0x02)
		{
			BaseVRCharacterOwner->NotifyOfTeleport(MoveAction.MoveActionFlags & 0x02);
		}

		if (BaseVRCharacterOwner->SeatInformation.bSitting)
		{
			BaseVRCharacterOwner->SeatInformation.StoredTargetTransform = (OriginalRelativeTrans.Inverse() * BaseVRCharacterOwner->GetRootComponent()->GetRelativeTransform()) * BaseVRCharacterOwner->SeatInformation.StoredTargetTransform;
			if (BaseVRCharacterOwner->IsLocallyControlled() && GetNetMode() == ENetMode::NM_Client)
			{
				BaseVRCharacterOwner->Server_SeatedSnapTurn(MoveAction.MoveActionDeltaYaw);
			}
		}
	}

	return false;
}

bool UVRBaseCharacterMovementComponent::DoMASetRotation(FVRMoveActionContainer& MoveAction)
{
	bool bRotateAroundCapsule = MoveAction.MoveActionFlags & 0x08;

	if (BaseVRCharacterOwner)
	{
		FTransform OriginalRelativeTrans = BaseVRCharacterOwner->GetRootComponent()->GetRelativeTransform();

		FRotator TargetRot(0.f, MoveAction.MoveActionRot.Yaw, 0.f);

		// Clamp to 2 decimal precision
		/*TargetRot = TargetRot.Clamp();
		TargetRot.Pitch = (TargetRot.Pitch * 100.f) / 100.f;
		TargetRot.Yaw = (TargetRot.Yaw * 100.f) / 100.f;
		TargetRot.Roll = (TargetRot.Roll * 100.f) / 100.f;
		TargetRot.Normalize();*/
		bIsBlendingOrientation = true;

		if (this->BaseVRCharacterOwner && this->BaseVRCharacterOwner->IsLocallyControlled())
		{
			if (this->bUseClientControlRotation)
			{
				MoveAction.MoveActionLoc = BaseVRCharacterOwner->SetActorRotationVR(TargetRot, true);
				MoveAction.MoveActionFlags |= 0x04; // Flag that we are using loc only
			}
			else
			{
				BaseVRCharacterOwner->SetActorRotationVR(TargetRot, true, true, bRotateAroundCapsule);
			}
		}
		else
		{
			if (MoveAction.MoveActionFlags & 0x04)
			{
				BaseVRCharacterOwner->SetActorLocation(BaseVRCharacterOwner->GetActorLocation() + MoveAction.MoveActionLoc);
			}
			else
			{
				BaseVRCharacterOwner->SetActorRotationVR(TargetRot, true, true, bRotateAroundCapsule);
			}
		}

		switch (MoveAction.VelRetentionSetting)
		{
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None:
		{

		}break;
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Clear:
		{
			this->Velocity = FVector::ZeroVector;
		}break;
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn:
		{
			if (BaseVRCharacterOwner->IsLocallyControlled())
			{
				MoveAction.MoveActionVel = RoundDirectMovement(FRotator(0.f, MoveAction.MoveActionRot.Pitch, 0.f).RotateVector(this->Velocity));
				this->Velocity = MoveAction.MoveActionVel;
			}
			else
			{
				this->Velocity = MoveAction.MoveActionVel;
			}
		}break;
		}

		// If we are flagged to teleport the grips
		if (MoveAction.MoveActionFlags & 0x01 || MoveAction.MoveActionFlags & 0x02)
		{
			BaseVRCharacterOwner->NotifyOfTeleport(MoveAction.MoveActionFlags & 0x02);
		}
	}

	return false;
}

bool UVRBaseCharacterMovementComponent::DoMATeleport(FVRMoveActionContainer& MoveAction)
{
	if (BaseVRCharacterOwner)
	{
		AController* OwningController = BaseVRCharacterOwner->GetController();

		if (!OwningController)
		{
			MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_None;
			return false;
		}

		bool bSkipEncroachmentCheck = MoveAction.MoveActionFlags & 0x01; //MoveAction.MoveActionRot.Roll > 0.0f;
		FRotator TargetRot(0.f, MoveAction.MoveActionRot.Yaw, 0.f);
		BaseVRCharacterOwner->TeleportTo(MoveAction.MoveActionLoc, TargetRot, false, bSkipEncroachmentCheck);

		switch (MoveAction.VelRetentionSetting)
		{
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None:
		{

		}break;
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Clear:
		{
			this->Velocity = FVector::ZeroVector;
		}break;
		case EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn:
		{
			if (BaseVRCharacterOwner->IsLocallyControlled())
			{
				MoveAction.MoveActionVel = RoundDirectMovement(FRotator(0.f, MoveAction.MoveActionRot.Pitch, 0.f).RotateVector(this->Velocity));
				this->Velocity = MoveAction.MoveActionVel;
			}
			else
			{
				this->Velocity = MoveAction.MoveActionVel;
			}
		}break;
		}

		if (BaseVRCharacterOwner->bUseControllerRotationYaw)
			OwningController->SetControlRotation(TargetRot);

		return true;
	}

	return false;
}

bool UVRBaseCharacterMovementComponent::DoMAStopAllMovement(FVRMoveActionContainer& MoveAction)
{
	if (AVRBaseCharacter * OwningCharacter = Cast<AVRBaseCharacter>(GetCharacterOwner()))
	{
		this->StopMovementImmediately();
		return true;
	}

	return false;
}

bool UVRBaseCharacterMovementComponent::DoMASetGravityDirection(FVRMoveActionContainer& MoveAction)
{
	bool bOrientToNewGravity = MoveAction.MoveActionFlags > 0;
	return SetCharacterToNewGravity(MoveAction.MoveActionVel, bOrientToNewGravity);
}

bool UVRBaseCharacterMovementComponent::DoMAPauseTracking(FVRMoveActionContainer& MoveAction)
{
	if (BaseVRCharacterOwner)
	{
		BaseVRCharacterOwner->bTrackingPaused = MoveAction.MoveActionFlags > 0;
		BaseVRCharacterOwner->PausedTrackingLoc = MoveAction.MoveActionLoc;
		BaseVRCharacterOwner->PausedTrackingRot = MoveAction.MoveActionRot.Yaw;
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

	// Skip calling into BP if we aren't locally controlled - *EDIT* MOVED TO TICKCOMPONENT to avoid batched movement issues
	/*if (CharacterOwner->IsLocallyControlled())
	{
		// Allow the player to run updates on the climb logic for CustomVRInputVector
		if (AVRBaseCharacter * characterOwner = Cast<AVRBaseCharacter>(CharacterOwner))
		{
			characterOwner->UpdateClimbingMovement(deltaTime);
		}
	}*/


	// I am forcing this to 0 to avoid some legacy velocity coming out of other movement modes, climbing should only be direct movement anyway.
	Velocity = FVector::ZeroVector;

	// Rewind the players position by the new capsule location
	RewindVRRelativeMovement();

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
				// Scope our movement updates, and do not apply them until all intermediate moves are completed.
				FVRCharacterScopedMovementUpdate ScopedStepUpMovement(UpdatedComponent, EScopedUpdate::DeferredUpdates);

				float stepZ = UpdatedComponent->GetComponentLocation().Z;
				
				// Making it easier to step up here with the multiplier, helps avoid falling back off
					if(bClampClimbingStepUp)
						bSteppedUp = VRClimbStepUp(GravDir, ((Adjusted.GetClampedToMaxSize2D(VRClimbingStepUpMaxSize) * VRClimbingStepUpMultiplier) + AdditionalVRInputVector) * (1.f - Hit.Time), Hit, &StepDownResult);
					else
						bSteppedUp = VRClimbStepUp(GravDir, ((Adjusted * VRClimbingStepUpMultiplier) + AdditionalVRInputVector) * (1.f - Hit.Time), Hit, &StepDownResult);

				if (bSteppedUp && OnPerformClimbingStepUp.IsBound())
				{
					FVector finalLoc = UpdatedComponent->GetComponentLocation();

					// Rewind the step up, the end user wants to handle it instead
					ScopedStepUpMovement.RevertMove();

					// Revert to old max step height
					MaxStepHeight = OldMaxStepHeight;
					
					OnPerformClimbingStepUp.Broadcast(finalLoc);
					return;
				}

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

		// This was causing based movement to apply to climbing
		//SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
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

	if (bAutoOrientToFloorNormal && CurrentFloor.IsWalkableFloor())
	{
		// Auto Align to the new floor normal
		// Set gravity direction to the new floor normal
		AutoTraceAndSetCharacterToNewGravity(CurrentFloor.HitResult, deltaTime);
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

	// Skip calling into BP if we aren't locally controlled
	if (CharacterOwner->IsLocallyControlled())
	{
		// Allow the player to run updates on the push logic for CustomVRInputVector
		if (AVRBaseCharacter * characterOwner = Cast<AVRBaseCharacter>(CharacterOwner))
		{
			characterOwner->UpdateLowGravMovement(deltaTime);
		}
	}

	float Friction = 0.0f; 
	// Rewind the players position by the new capsule location
	RewindVRRelativeMovement();

	//RestorePreAdditiveVRMotionVelocity();

	// If we are not in the default physics volume then accept the custom fluid friction setting
	// I set this by default to be ignored as many will not alter the default fluid friction
	if(!VRLowGravIgnoresDefaultFluidFriction || GetWorld()->GetDefaultPhysicsVolume() != GetPhysicsVolume())
		Friction = 0.5f * GetPhysicsVolume()->FluidFriction;

	CalcVelocity(deltaTime, Friction, true, 0.0f);

	// Adding in custom VR input vector here, can be used for custom movement during it
	// AddImpulse is not multiplayer compatible client side
	//Velocity += CustomVRInputVector; 

	ApplyVRMotionToVelocity(deltaTime);

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = (Velocity * deltaTime);
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted/* + AdditionalVRInputVector*/, UpdatedComponent->GetComponentQuat(), true, Hit);

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
			Velocity = (((UpdatedComponent->GetComponentLocation() - OldLocation) /* - AdditionalVRInputVector*/) / deltaTime) * VRLowGravWallFrictionScaler;
		}
	}
	else
	{
		if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			Velocity = (((UpdatedComponent->GetComponentLocation() - OldLocation) /* - AdditionalVRInputVector*/) / deltaTime);
		}
	}

	RestorePreAdditiveVRMotionVelocity();
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

EVRConjoinedMovementModes UVRBaseCharacterMovementComponent::GetReplicatedMovementMode()
{
	if (MovementMode == EMovementMode::MOVE_Custom)
	{
		return (EVRConjoinedMovementModes)((int8)CustomMovementMode + (int8)EVRConjoinedMovementModes::C_VRMOVE_Climbing);
	}
	else
		return (EVRConjoinedMovementModes)MovementMode.GetValue();
}

void UVRBaseCharacterMovementComponent::ApplyNetworkMovementMode(const uint8 ReceivedMode)
{
	if (CharacterOwner->GetLocalRole() != ENetRole::ROLE_SimulatedProxy)
	{
		const uint8 CurrentPackedMovementMode = PackNetworkMovementMode();
		if (CurrentPackedMovementMode != ReceivedMode)
		{
			TEnumAsByte<EMovementMode> NetMovementMode(MOVE_None);
			TEnumAsByte<EMovementMode> NetGroundMode(MOVE_None);
			uint8 NetCustomMode(0);
			UnpackNetworkMovementMode(ReceivedMode, NetMovementMode, NetCustomMode, NetGroundMode);

			// Custom movement modes aren't going to be rolled back as they are client authed for our pawns
			if (NetMovementMode == EMovementMode::MOVE_Custom || MovementMode == EMovementMode::MOVE_Custom)
			{
				if (NetCustomMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing || CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing)
				return; // Don't rollback custom movement modes, we set the server to trust the client on them now so the server should get corrected
			}
		}
	}
	
	Super::ApplyNetworkMovementMode(ReceivedMode);
}

void  UVRBaseCharacterMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	Super::SetUpdatedComponent(NewUpdatedComponent);

	BaseVRCharacterOwner = Cast<AVRCharacter>(CharacterOwner);
}


void UVRBaseCharacterMovementComponent::PerformMovement(float DeltaSeconds)
{
	// Scope these, they nest with Outer references so it should work fine
	FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

	// This moves it into update scope
	if (bRunControlRotationInMovementComponent && CharacterOwner->IsLocallyControlled())
	{
		if (BaseVRCharacterOwner && BaseVRCharacterOwner->OwningVRPlayerController)
		{
			BaseVRCharacterOwner->OwningVRPlayerController->RotationInput = BaseVRCharacterOwner->OwningVRPlayerController->LastRotationInput;
			BaseVRCharacterOwner->OwningVRPlayerController->UpdateRotation(DeltaSeconds);
			BaseVRCharacterOwner->OwningVRPlayerController->LastRotationInput = FRotator::ZeroRotator;
			BaseVRCharacterOwner->OwningVRPlayerController->RotationInput = FRotator::ZeroRotator;
		}
	}

	// Apply any replicated movement modes that are pending
	ApplyReplicatedMovementMode(VRReplicatedMovementMode, true);

	// Handle move actions here - Should be scoped
	CheckForMoveAction();

	// Clear out this flag prior to movement so we can see if it gets changed
	bIsInPushBack = false;


	// Handle collision swapping if we aren't doing locomotion based movement
	if (BaseVRCharacterOwner->VRRootReference->bUseWalkingCollisionOverride && !BaseVRCharacterOwner->bRetainRoomscale)
	{
		bool bAllowWalkingCollision = false;
		if (MovementMode == EMovementMode::MOVE_Walking || MovementMode == EMovementMode::MOVE_NavWalking)
			bAllowWalkingCollision = true;

		BaseVRCharacterOwner->VRRootReference->SetCollisionOverride(bAllowWalkingCollision && GetCurrentAcceleration().IsNearlyZero());
	}

	Super::PerformMovement(DeltaSeconds);

	EndPushBackNotification(); // Check if we need to notify of ending pushback

	// Make sure these are cleaned out for the next frame
	//AdditionalVRInputVector = FVector::ZeroVector;
	//CustomVRInputVector = FVector::ZeroVector;

	// Only clear it here if we are the server, the client clears it later
	if (CharacterOwner->GetLocalRole() == ROLE_Authority)
	{
		MoveActionArray.Clear();
	}
}

void UVRBaseCharacterMovementComponent::OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode, FVector ServerGravityDirection)
{
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);


	// If we got corrected then lets teleport our grips, this means that we were out of sync with the server or the server moved us
	if (BaseVRCharacterOwner)
	{
		BaseVRCharacterOwner->OnCharacterNetworkCorrected_Bind.Broadcast();

		if (IsValid(BaseVRCharacterOwner->LeftMotionController))
		{
			BaseVRCharacterOwner->LeftMotionController->TeleportMoveGrips(false, false);
			BaseVRCharacterOwner->LeftMotionController->PostTeleportMoveGrippedObjects();
		}

		if (IsValid(BaseVRCharacterOwner->RightMotionController))
		{
			BaseVRCharacterOwner->RightMotionController->TeleportMoveGrips(false, false);
			BaseVRCharacterOwner->RightMotionController->PostTeleportMoveGrippedObjects();
		}
		//BaseVRCharacterOwner->NotifyOfTeleport(false);
	}
}

void UVRBaseCharacterMovementComponent::SimulatedTick(float DeltaSeconds)
{
	//return Super::SimulatedTick(DeltaSeconds);

	QUICK_SCOPE_CYCLE_COUNTER(STAT_Character_CharacterMovementSimulated);
	checkSlow(CharacterOwner != nullptr);

	// If we are playing a RootMotion AnimMontage.
	if (CharacterOwner->IsPlayingNetworkedRootMotionMontage())
	{
		bWasSimulatingRootMotion = true;
		UE_LOG(LogRootMotion, Verbose, TEXT("UCharacterMovementComponent::SimulatedTick"));

		// Tick animations before physics.
		if (CharacterOwner && CharacterOwner->GetMesh())
		{
			TickCharacterPose(DeltaSeconds);

			// Make sure animation didn't trigger an event that destroyed us
			if (!HasValidData())
			{
				return;
			}
		}

		const FQuat OldRotationQuat = UpdatedComponent->GetComponentQuat();
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();

		USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
		const FVector SavedMeshRelativeLocation = Mesh ? Mesh->GetRelativeLocation() : FVector::ZeroVector;


		if (RootMotionParams.bHasRootMotion)
		{
			SimulateRootMotion(DeltaSeconds, RootMotionParams.GetRootMotionTransform());

#if !(UE_BUILD_SHIPPING)
			// debug
			/*if (CharacterOwner && false)
			{
				const FRotator OldRotation = OldRotationQuat.Rotator();
				const FRotator NewRotation = UpdatedComponent->GetComponentRotation();
				const FVector NewLocation = UpdatedComponent->GetComponentLocation();
				DrawDebugCoordinateSystem(GetWorld(), CharacterOwner->GetMesh()->GetComponentLocation() + FVector(0, 0, 1), NewRotation, 50.f, false);
				DrawDebugLine(GetWorld(), OldLocation, NewLocation, FColor::Red, false, 10.f);

				UE_LOG(LogRootMotion, Log, TEXT("UCharacterMovementComponent::SimulatedTick DeltaMovement Translation: %s, Rotation: %s, MovementBase: %s"),
					*(NewLocation - OldLocation).ToCompactString(), *(NewRotation - OldRotation).GetNormalized().ToCompactString(), *GetNameSafe(CharacterOwner->GetMovementBase()));
			}*/
#endif // !(UE_BUILD_SHIPPING)
		}

		// then, once our position is up to date with our animation, 
		// handle position correction if we have any pending updates received from the server.
		if (CharacterOwner && (CharacterOwner->RootMotionRepMoves.Num() > 0))
		{
			CharacterOwner->SimulatedRootMotionPositionFixup(DeltaSeconds);
		}

		if (!bNetworkSmoothingComplete && (NetworkSmoothingMode == ENetworkSmoothingMode::Linear))
		{
			// Same mesh with different rotation?
			const FQuat NewCapsuleRotation = UpdatedComponent->GetComponentQuat();
			if (Mesh == CharacterOwner->GetMesh() && !NewCapsuleRotation.Equals(OldRotationQuat, 1e-6f) && ClientPredictionData)
			{
				// #TODO: The below is new in 5.6, i don't have saved capsule rotation, don't think i need this change for base char
				/* 
									// Add delta rotation to the target rotation and original offset. Otherwise this object will move back toward the old rotation.
					const FQuat RotationDelta = NewCapsuleRotation - SavedCapsuleRotation;
					ClientPredictionData->MeshRotationTarget += RotationDelta;
					ClientPredictionData->OriginalMeshRotationOffset += RotationDelta;

					// Update the MeshRotationOffset to match the capsule rotation.
					ClientPredictionData->MeshRotationOffset = NewCapsuleRotation;
				*/

				// Smoothing should lerp toward this new rotation target, otherwise it will just try to go back toward the old rotation.
				ClientPredictionData->MeshRotationTarget = NewCapsuleRotation;
				Mesh->SetRelativeLocationAndRotation(SavedMeshRelativeLocation, CharacterOwner->GetBaseRotationOffset());
			}
		}
	}
	else if (CurrentRootMotion.HasActiveRootMotionSources())
	{
		// We have root motion sources and possibly animated root motion
		bWasSimulatingRootMotion = true;
		UE_LOG(LogRootMotion, Verbose, TEXT("UCharacterMovementComponent::SimulatedTick"));

		// If we have RootMotionRepMoves, find the most recent important one and set position/rotation to it
		bool bCorrectedToServer = false;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FQuat OldRotation = UpdatedComponent->GetComponentQuat();
		if (CharacterOwner->RootMotionRepMoves.Num() > 0)
		{
			// Move Actor back to position of that buffered move. (server replicated position).
			FSimulatedRootMotionReplicatedMove& RootMotionRepMove = CharacterOwner->RootMotionRepMoves.Last();
			if (CharacterOwner->RestoreReplicatedMove(RootMotionRepMove))
			{
				bCorrectedToServer = true;
			}
			Acceleration = RootMotionRepMove.RootMotion.Acceleration;

			CharacterOwner->PostNetReceiveVelocity(RootMotionRepMove.RootMotion.LinearVelocity);
			LastUpdateVelocity = RootMotionRepMove.RootMotion.LinearVelocity;

			// Convert RootMotionSource Server IDs -> Local IDs in AuthoritativeRootMotion and cull invalid
			// so that when we use this root motion it has the correct IDs
			ConvertRootMotionServerIDsToLocalIDs(CurrentRootMotion, RootMotionRepMove.RootMotion.AuthoritativeRootMotion, RootMotionRepMove.Time);
			RootMotionRepMove.RootMotion.AuthoritativeRootMotion.CullInvalidSources();

			// Set root motion states to that of repped in state
			CurrentRootMotion.UpdateStateFrom(RootMotionRepMove.RootMotion.AuthoritativeRootMotion, true);

			// Clear out existing RootMotionRepMoves since we've consumed the most recent
			UE_LOG(LogRootMotion, Log, TEXT("\tClearing old moves in SimulatedTick (%d)"), CharacterOwner->RootMotionRepMoves.Num());
			CharacterOwner->RootMotionRepMoves.Reset();
		}

		// Update replicated gravity direction
		if (bNetworkGravityDirectionChanged)
		{
			SetGravityDirection(CharacterOwner->GetReplicatedGravityDirection());
			bNetworkGravityDirectionChanged = false;
		}

		// Update replicated movement mode.
		if (bNetworkMovementModeChanged)
		{
			ApplyNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode());
			bNetworkMovementModeChanged = false;
		}

		// Perform movement
		PerformMovement(DeltaSeconds);

		// After movement correction, smooth out error in position if any.
		if (bCorrectedToServer || CurrentRootMotion.NeedsSimulatedSmoothing())
		{
			SmoothCorrection(OldLocation, OldRotation, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat());
		}
	}
	// Not playing RootMotion AnimMontage
	else
	{
		// if we were simulating root motion, we've been ignoring regular ReplicatedMovement updates.
		// If we're not simulating root motion anymore, force us to sync our movement properties.
		// (Root Motion could leave Velocity out of sync w/ ReplicatedMovement)
		if (bWasSimulatingRootMotion)
		{
			CharacterOwner->RootMotionRepMoves.Empty();
			CharacterOwner->OnRep_ReplicatedMovement();
			CharacterOwner->OnRep_ReplicatedBasedMovement();
			SetGravityDirection(GetCharacterOwner()->GetReplicatedGravityDirection());
			ApplyNetworkMovementMode(GetCharacterOwner()->GetReplicatedMovementMode());
		}

		if (CharacterOwner->IsReplicatingMovement() && UpdatedComponent)
		{
			//USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh();
			//const FVector SavedMeshRelativeLocation = Mesh ? Mesh->GetRelativeLocation() : FVector::ZeroVector;
			//const FQuat SavedCapsuleRotation = UpdatedComponent->GetComponentQuat();
			const bool bPreventMeshMovement = !bNetworkSmoothingComplete;

			// Avoid moving the mesh during movement if SmoothClientPosition will take care of it.
			if(NetworkSmoothingMode != ENetworkSmoothingMode::Disabled)
			{
				const FScopedPreventAttachedComponentMove PreventMeshMove(bPreventMeshMovement ? BaseVRCharacterOwner->NetSmoother : nullptr);
				//const FScopedPreventAttachedComponentMove PreventMeshMovement(bPreventMeshMovement ? Mesh : nullptr);
				if (CharacterOwner->IsPlayingRootMotion())
				{
					// Update replicated gravity direction
					if (bNetworkGravityDirectionChanged)
					{
						SetGravityDirection(CharacterOwner->GetReplicatedGravityDirection());
						bNetworkGravityDirectionChanged = false;
					}

					// Update replicated movement mode.
					if (bNetworkMovementModeChanged)
					{
						ApplyNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode());
						bNetworkMovementModeChanged = false;
					}

					PerformMovement(DeltaSeconds);
				}
				else
				{
					// Moved this var into the VRChar to control smoothing
					//if(!bDisableSimulatedTickWhenSmoothingMovement)
						SimulateMovement(DeltaSeconds);
				}
			}
			else
			{
				if (CharacterOwner->IsPlayingRootMotion())
				{
					PerformMovement(DeltaSeconds);
				}
				else
				{
					SimulateMovement(DeltaSeconds);
				}
			}

			// With Linear smoothing we need to know if the rotation changes, since the mesh should follow along with that (if it was prevented above).
			// This should be rare that rotation changes during simulation, but it can happen when ShouldRemainVertical() changes, or standing on a moving base.
			/*const bool bValidateRotation = bPreventMeshMovement && (NetworkSmoothingMode == ENetworkSmoothingMode::Linear);
			if (bValidateRotation && UpdatedComponent)
			{
				// Same mesh with different rotation?
				const FQuat NewCapsuleRotation = UpdatedComponent->GetComponentQuat();
				if (Mesh == CharacterOwner->GetMesh() && !NewCapsuleRotation.Equals(SavedCapsuleRotation, 1e-6f) && ClientPredictionData)
				{
					// Smoothing should lerp toward this new rotation target, otherwise it will just try to go back toward the old rotation.
					ClientPredictionData->MeshRotationTarget = NewCapsuleRotation;
					Mesh->SetRelativeLocationAndRotation(SavedMeshRelativeLocation, CharacterOwner->GetBaseRotationOffset());
				}
			}*/
		}

		if (bWasSimulatingRootMotion)
		{
			bWasSimulatingRootMotion = false;
		}
	}

	// Smooth mesh location after moving the capsule above.
	if (!bNetworkSmoothingComplete)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Character_CharacterMovementSmoothClientPosition);
		SmoothClientPosition(DeltaSeconds);
	}
	else
	{
		UE_LOG(LogVRBaseCharacterMovement, Verbose, TEXT("Skipping network smoothing for %s."), *GetNameSafe(CharacterOwner));
	}
}

void UVRBaseCharacterMovementComponent::MoveAutonomous(
	float ClientTimeStamp,
	float DeltaTime,
	uint8 CompressedFlags,
	const FVector& NewAccel
)
{
	if (!HasValidData())
	{
		return;
	}

	UpdateFromCompressedFlags(CompressedFlags);
	CharacterOwner->CheckJumpInput(DeltaTime);

	Acceleration = ConstrainInputAcceleration(NewAccel);
	Acceleration = Acceleration.GetClampedToMaxSize(GetMaxAcceleration());
	AnalogInputModifier = ComputeAnalogInputModifier();

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	FQuat OldRotation = UpdatedComponent->GetComponentQuat();

	if (BaseVRCharacterOwner && NetworkSmoothingMode == ENetworkSmoothingMode::Exponential)
	{
		OldLocation = BaseVRCharacterOwner->OffsetComponentToWorld.GetTranslation();
		OldRotation = BaseVRCharacterOwner->OffsetComponentToWorld.GetRotation();
	}

	const bool bWasPlayingRootMotion = CharacterOwner->IsPlayingRootMotion();

	// Scope these, they nest with Outer references so it should work fine, this keeps the update rotation and move autonomous from double updating the char
	//const FScopedPreventAttachedComponentMove PreventMeshMove(BaseVRCharacterOwner ? BaseVRCharacterOwner->NetSmoother : nullptr);

	PerformMovement(DeltaTime);

	// Check if data is valid as PerformMovement can mark character for pending kill
	if (!HasValidData())
	{
		return;
	}

	// If not playing root motion, tick animations after physics. We do this here to keep events, notifies, states and transitions in sync with client updates.
	if (CharacterOwner && !CharacterOwner->bClientUpdating && !CharacterOwner->IsPlayingRootMotion() && CharacterOwner->GetMesh())
	{
		if (!bWasPlayingRootMotion) // If we were playing root motion before PerformMovement but aren't anymore, we're on the last frame of anim root motion and have already ticked character
		{
			TickCharacterPose(DeltaTime);
		}
		// TODO: SaveBaseLocation() in case tick moves us?


		USkeletalMeshComponent* OwnerMesh = CharacterOwner->GetMesh();
		check(OwnerMesh != nullptr)

		static const auto CVarEnableQueuedAnimEventsOnServer = IConsoleManager::Get().FindConsoleVariable(TEXT("a.EnableQueuedAnimEventsOnServer"));
		if (CVarEnableQueuedAnimEventsOnServer->GetInt())
		{
			if (UAnimInstance* AnimInstance = OwnerMesh->GetAnimInstance())
			{
				if (OwnerMesh->VisibilityBasedAnimTickOption <= EVisibilityBasedAnimTickOption::AlwaysTickPose && AnimInstance->NeedsUpdate())
				{
					// If we are doing a full graph update on the server but its doing a parallel update,
					// trigger events right away since these are notifies queued from the montage update, and we could be receiving multiple ServerMoves per frame.
					OwnerMesh->ConditionallyDispatchQueuedAnimEvents();

					// We need to manually clear the anim notify queue (since normally its only is cleared in PreUpdateAnimation()) otherwise if animation ticks, the notifies queued from the ServerMove would fire twice.
					AnimInstance->ClearQueuedAnimEvents(false);

					// When animation ticks, we want its queued events to be triggered.
					OwnerMesh->AllowQueuedAnimEventsNextDispatch();
				}
			}
		}
		else
		{
			// Revert back to old behavior if wanted/needed.
			if (OwnerMesh->ShouldOnlyTickMontages(DeltaTime) || OwnerMesh->ShouldOnlyTickMontagesAndRefreshBones(DeltaTime))
			{
				OwnerMesh->ConditionallyDispatchQueuedAnimEvents();
			}
		}
	}

	if (CharacterOwner && UpdatedComponent)
	{
		// Smooth local view of remote clients on listen servers
		static const auto CVarNetEnableListenServerSmoothing = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetEnableListenServerSmoothing"));
		if (CVarNetEnableListenServerSmoothing->GetInt() &&
			CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy &&
			IsNetMode(NM_ListenServer))
		{
			SmoothCorrection(OldLocation, OldRotation, UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat());
		}
	}
}

void UVRBaseCharacterMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation)
{

	//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementSmoothCorrection);
	if (!HasValidData())
	{
		return;
	}

	if (!BaseVRCharacterOwner)
		Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);

	// We shouldn't be running this on a server that is not a listen server.
	checkSlow(GetNetMode() != NM_DedicatedServer);

	// Only client proxies or remote clients on a listen server should run this code.
	const bool bIsSimulatedProxy = (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy);
	const bool bIsRemoteAutoProxy = (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy);
	ensure(bIsSimulatedProxy || bIsRemoteAutoProxy);

	// Getting a correction means new data, so smoothing needs to run.
	bNetworkSmoothingComplete = false;

	// Handle selected smoothing mode.
	if (NetworkSmoothingMode == ENetworkSmoothingMode::Disabled || GetNetMode() == NM_Standalone)
	{
		UpdatedComponent->SetWorldLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::TeleportPhysics);
		bNetworkSmoothingComplete = true;
	}
	else if (FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character())
	{
		const UWorld* MyWorld = GetWorld();
		if (!ensure(MyWorld != nullptr))
		{
			return;
		}

		// Handle my custom VR Offset
		FVector OldWorldLocation = OldLocation;
		FQuat OldWorldRotation = OldRotation;
		FVector NewWorldLocation = NewLocation;
		FQuat NewWorldRotation = NewRotation;

		if (BaseVRCharacterOwner && NetworkSmoothingMode == ENetworkSmoothingMode::Exponential)
		{
			if (GetNetMode() < ENetMode::NM_Client)
			{
				NewWorldLocation = BaseVRCharacterOwner->OffsetComponentToWorld.GetTranslation();
				NewWorldRotation = BaseVRCharacterOwner->OffsetComponentToWorld.GetRotation();
			}
			else
			{
				FTransform NewWorldTransform(NewRotation, NewLocation, UpdatedComponent->GetRelativeScale3D());
				FTransform CurrentRelative = BaseVRCharacterOwner->OffsetComponentToWorld.GetRelativeTransform(UpdatedComponent->GetComponentTransform());
				FTransform NewWorld = CurrentRelative * NewWorldTransform;
				OldWorldLocation = BaseVRCharacterOwner->OffsetComponentToWorld.GetLocation();
				OldWorldRotation = BaseVRCharacterOwner->OffsetComponentToWorld.GetRotation();
				NewWorldLocation = NewWorld.GetLocation();
				NewWorldRotation = NewWorld.GetRotation();
			}
		}

		// The mesh doesn't move, but the capsule does so we have a new offset.
		FVector NewToOldVector = (OldWorldLocation - NewWorldLocation);
		if (bIsNavWalkingOnServer && FMath::Abs(GetGravitySpaceZ(NewToOldVector)) < NavWalkingFloorDistTolerance)
		{
			// ignore smoothing on Z axis
			// don't modify new location (local simulation result), since it's probably more accurate than server data
			// and shouldn't matter as long as difference is relatively small
			NewToOldVector = ProjectToGravityFloor(NewToOldVector);
		}

		const float DistSq = NewToOldVector.SizeSquared();
		if (DistSq > FMath::Square(ClientData->MaxSmoothNetUpdateDist))
		{
			ClientData->MeshTranslationOffset = (DistSq > FMath::Square(ClientData->NoSmoothNetUpdateDist))
				? FVector::ZeroVector
				: ClientData->MeshTranslationOffset + ClientData->MaxSmoothNetUpdateDist * NewToOldVector.GetSafeNormal();
		}
		else
		{
			ClientData->MeshTranslationOffset = ClientData->MeshTranslationOffset + NewToOldVector;
		}

		UE_LOG(LogVRBaseCharacterMovement, Verbose, TEXT("Proxy %s SmoothCorrection(%.2f)"), *GetNameSafe(CharacterOwner), FMath::Sqrt(DistSq));
		if (NetworkSmoothingMode == ENetworkSmoothingMode::Linear)
		{
			// #TODO: Get this working in the future?
			// I am currently skipping smoothing on rotation operations
			if ((!OldRotation.Equals(NewRotation, 1e-5f)))// || Velocity.IsNearlyZero()))
			{
				if (BaseVRCharacterOwner->NetSmoother)
				{
					BaseVRCharacterOwner->NetSmoother->SetRelativeLocation(BaseVRCharacterOwner->bRetainRoomscale ? FVector::ZeroVector : BaseVRCharacterOwner->VRRootReference->GetTargetHeightOffset());			
					//BaseVRCharacterOwner->NetSmoother->SetRelativeLocation(FVector::ZeroVector);
				}
				UpdatedComponent->SetWorldLocationAndRotation(NewLocation, NewRotation, false, nullptr, GetTeleportType());
				ClientData->MeshTranslationOffset = FVector::ZeroVector;
				ClientData->MeshRotationOffset = ClientData->MeshRotationTarget;
				bNetworkSmoothingComplete = true;
			}
			else
			{
				ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;

				// Remember the current and target rotation, we're going to lerp between them
				ClientData->OriginalMeshRotationOffset = OldRotation;
				ClientData->MeshRotationOffset = OldRotation;
				ClientData->MeshRotationTarget = NewRotation;

				// Move the capsule, but not the mesh.
				// Note: we don't change rotation, we lerp towards it in SmoothClientPosition.
				if (NewLocation != OldLocation)
				{
					const FScopedPreventAttachedComponentMove PreventMeshMove(BaseVRCharacterOwner->NetSmoother);
					UpdatedComponent->SetWorldLocation(NewLocation, false, nullptr, GetTeleportType());
				}
			}
		}
		else
		{
			// #TODO: Get this working in the future?
			// I am currently skipping smoothing on rotation operations
			/*if ((!OldRotation.Equals(NewRotation, 1e-5f)))// || Velocity.IsNearlyZero()))
			{
				BaseVRCharacterOwner->NetSmoother->SetRelativeLocation(BaseVRCharacterOwner->bRetainRoomscale ? FVector::ZeroVector : BaseVRCharacterOwner->VRRootReference->GetTargetHeightOffset());
				UpdatedComponent->SetWorldLocationAndRotation(NewLocation, NewRotation, false, nullptr, GetTeleportType());
				ClientData->MeshTranslationOffset = FVector::ZeroVector;
				ClientData->MeshRotationOffset = ClientData->MeshRotationTarget;
				bNetworkSmoothingComplete = true;
			}
			else*/
			{			
				// Calc rotation needed to keep current world rotation after UpdatedComponent moves.
				// Take difference between where we were rotated before, and where we're going
				ClientData->MeshRotationOffset = FQuat::Identity;// (NewRotation.Inverse() * OldRotation) * ClientData->MeshRotationOffset;
				ClientData->MeshRotationTarget = FQuat::Identity;

				const FScopedPreventAttachedComponentMove PreventMeshMove(BaseVRCharacterOwner->NetSmoother);
				UpdatedComponent->SetWorldLocationAndRotation(NewLocation, NewRotation, false, nullptr, GetTeleportType());
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// Update smoothing timestamps

		// If running ahead, pull back slightly. This will cause the next delta to seem slightly longer, and cause us to lerp to it slightly slower.
		if (ClientData->SmoothingClientTimeStamp > ClientData->SmoothingServerTimeStamp)
		{
			const double OldClientTimeStamp = ClientData->SmoothingClientTimeStamp;
			ClientData->SmoothingClientTimeStamp = FMath::LerpStable(ClientData->SmoothingServerTimeStamp, OldClientTimeStamp, 0.5);

			UE_LOG(LogVRBaseCharacterMovement, VeryVerbose, TEXT("SmoothCorrection: Pull back client from ClientTimeStamp: %.6f to %.6f, ServerTimeStamp: %.6f for %s"),
				OldClientTimeStamp, ClientData->SmoothingClientTimeStamp, ClientData->SmoothingServerTimeStamp, *GetNameSafe(CharacterOwner));
		}

		// Using server timestamp lets us know how much time actually elapsed, regardless of packet lag variance.
		double OldServerTimeStamp = ClientData->SmoothingServerTimeStamp;
			
		if (bIsSimulatedProxy)
		{
			// This value is normally only updated on the server, however some code paths might try to read it instead of the replicated value so copy it for proxies as well.
			ServerLastTransformUpdateTimeStamp = CharacterOwner->GetReplicatedServerLastTransformUpdateTimeStamp();
		}
		ClientData->SmoothingServerTimeStamp = ServerLastTransformUpdateTimeStamp;

		// Initial update has no delta.
		if (ClientData->LastCorrectionTime == 0)
		{
			ClientData->SmoothingClientTimeStamp = ClientData->SmoothingServerTimeStamp;
			OldServerTimeStamp = ClientData->SmoothingServerTimeStamp;
		}

		// Don't let the client fall too far behind or run ahead of new server time.
		const double ServerDeltaTime = ClientData->SmoothingServerTimeStamp - OldServerTimeStamp;
		const double MaxOffset = ClientData->MaxClientSmoothingDeltaTime;
		const double MinOffset = FMath::Min(double(ClientData->SmoothNetUpdateTime), MaxOffset);

		// MaxDelta is the farthest behind we're allowed to be after receiving a new server time.
		const double MaxDelta = FMath::Clamp(ServerDeltaTime * 1.25, MinOffset, MaxOffset);
		ClientData->SmoothingClientTimeStamp = FMath::Clamp(ClientData->SmoothingClientTimeStamp, ClientData->SmoothingServerTimeStamp - MaxDelta, ClientData->SmoothingServerTimeStamp);

		// Compute actual delta between new server timestamp and client simulation.
		ClientData->LastCorrectionDelta = ClientData->SmoothingServerTimeStamp - ClientData->SmoothingClientTimeStamp;
		ClientData->LastCorrectionTime = MyWorld->GetTimeSeconds();

		UE_LOG(LogVRBaseCharacterMovement, VeryVerbose, TEXT("SmoothCorrection: WorldTime: %.6f, ServerTimeStamp: %.6f, ClientTimeStamp: %.6f, Delta: %.6f for %s"),
		MyWorld->GetTimeSeconds(), ClientData->SmoothingServerTimeStamp, ClientData->SmoothingClientTimeStamp, ClientData->LastCorrectionDelta, *GetNameSafe(CharacterOwner));
		/*
		Visualize network smoothing was here, removed it
		*/
	}
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
	const bool bIsSimulatedProxy = (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy);
	const bool bIsRemoteAutoProxy = (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy);
	if (!ensure(bIsSimulatedProxy || bIsRemoteAutoProxy))
	{
		return;
	}

	SmoothClientPosition_Interpolate(DeltaSeconds);

	//SmoothClientPosition_UpdateVisuals(); No mesh, don't bother to run this
	SmoothClientPosition_UpdateVRVisuals();
}

void UVRBaseCharacterMovementComponent::SmoothClientPosition_UpdateVRVisuals()
{
	//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementSmoothClientPosition_Visual);
	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();

	if (!BaseVRCharacterOwner || !ClientData)
		return;

	if (ClientData)
	{
		if (NetworkSmoothingMode == ENetworkSmoothingMode::Linear && BaseVRCharacterOwner->NetSmoother)
		{
			// Erased most of the code here, check back in later
			const USceneComponent* MeshParent = BaseVRCharacterOwner->NetSmoother->GetAttachParent();
			FVector MeshParentScale = MeshParent != nullptr ? MeshParent->GetComponentScale() : FVector(1.0f, 1.0f, 1.0f);

			MeshParentScale.X = FMath::IsNearlyZero(MeshParentScale.X) ? 1.0f : MeshParentScale.X;
			MeshParentScale.Y = FMath::IsNearlyZero(MeshParentScale.Y) ? 1.0f : MeshParentScale.Y;
			MeshParentScale.Z = FMath::IsNearlyZero(MeshParentScale.Z) ? 1.0f : MeshParentScale.Z;

			const FVector NewRelLocation = ClientData->MeshRotationOffset.UnrotateVector(ClientData->MeshTranslationOffset);// + CharacterOwner->GetBaseTranslationOffset();
			
			FVector HeightOffset = (BaseVRCharacterOwner->bRetainRoomscale ? FVector::ZeroVector : BaseVRCharacterOwner->VRRootReference->GetTargetHeightOffset());
			BaseVRCharacterOwner->NetSmoother->SetRelativeLocation(NewRelLocation + HeightOffset);
		}
		else if (NetworkSmoothingMode == ENetworkSmoothingMode::Exponential && BaseVRCharacterOwner->NetSmoother)
		{
			const USceneComponent* MeshParent = BaseVRCharacterOwner->NetSmoother->GetAttachParent();
			FVector MeshParentScale = MeshParent != nullptr ? MeshParent->GetComponentScale() : FVector(1.0f, 1.0f, 1.0f);

			MeshParentScale.X = FMath::IsNearlyZero(MeshParentScale.X) ? 1.0f : MeshParentScale.X;
			MeshParentScale.Y = FMath::IsNearlyZero(MeshParentScale.Y) ? 1.0f : MeshParentScale.Y;
			MeshParentScale.Z = FMath::IsNearlyZero(MeshParentScale.Z) ? 1.0f : MeshParentScale.Z;

			// Adjust mesh location and rotation
			const FVector NewRelTranslation = (UpdatedComponent->GetComponentToWorld().InverseTransformVectorNoScale(ClientData->MeshTranslationOffset) / MeshParentScale);// +CharacterOwner->GetBaseTranslationOffset();
			const FQuat NewRelRotation = ClientData->MeshRotationOffset;// *CharacterOwner->GetBaseRotationOffset();
			//Basechar->NetSmoother->SetRelativeLocation(NewRelTranslation);

			FVector HeightOffset = BaseVRCharacterOwner->bRetainRoomscale ? FVector::ZeroVector : BaseVRCharacterOwner->VRRootReference->GetTargetHeightOffset();
			BaseVRCharacterOwner->NetSmoother->SetRelativeLocationAndRotation(NewRelTranslation + HeightOffset, NewRelRotation);
		}
		else
		{
			// Unhandled mode
		}
	}
}

void UVRBaseCharacterMovementComponent::SetHasRequestedVelocity(bool bNewHasRequestedVelocity)
{
	bHasRequestedVelocity = bNewHasRequestedVelocity;
}

bool UVRBaseCharacterMovementComponent::IsClimbing() const
{
	return ((MovementMode == MOVE_Custom) && (CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing)) && UpdatedComponent;
}

FVector UVRBaseCharacterMovementComponent::RewindVRMovement()
{
	RewindVRRelativeMovement();
	return AdditionalVRInputVector;
}

FVector UVRBaseCharacterMovementComponent::GetCustomInputVector()
{
	return CustomVRInputVector;
}

void UVRBaseCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	// If is a custom or VR custom movement mode
	//int32 MovementFlags = (Flags >> 2) & 15;
	//VRReplicatedMovementMode = (EVRConjoinedMovementModes)MovementFlags;

	//bWantsToSnapTurn = ((Flags & FSavedMove_VRBaseCharacter::FLAG_SnapTurn) != 0);

	Super::UpdateFromCompressedFlags(Flags);
}

FVector UVRBaseCharacterMovementComponent::RoundDirectMovement(FVector InMovement) const
{
	// Match FVector_NetQuantize100 (2 decimal place of precision).
	UE::Net::QuantizeVector(100, InMovement);
	//InMovement.X = FMath::RoundToFloat(InMovement.X * 100.f) / 100.f;
	//InMovement.Y = FMath::RoundToFloat(InMovement.Y * 100.f) / 100.f;
	//InMovement.Z = FMath::RoundToFloat(InMovement.Z * 100.f) / 100.f;
	return InMovement;
}

void UVRBaseCharacterMovementComponent::SetAutoOrientToFloorNormal(bool bAutoOrient, bool bRevertGravityWhenDisabled)
{
	bAutoOrientToFloorNormal = bAutoOrient;

	if (!bAutoOrientToFloorNormal && bRevertGravityWhenDisabled)
	{
		//DefaultGravityDirection, could also get world gravity
		SetCharacterToNewGravity(FVector(0.0f, 0.0f, -1.0f), true);
	}
}

void UVRBaseCharacterMovementComponent::AutoTraceAndSetCharacterToNewGravity(FHitResult & TargetFloor, float DeltaTime)
{
	if (TargetFloor.Component.IsValid())
	{
		// Should we really be tracing complex? (true should maybe be false?)
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AutoTraceFloorNormal), /*true*/false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(QueryParams, ResponseParam);
		const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();

		FVector TraceStart = BaseVRCharacterOwner->GetVRLocation_Inline();
		FVector Offset = (-UpdatedComponent->GetComponentQuat().GetUpVector()) * (BaseVRCharacterOwner->VRRootReference->GetScaledCapsuleHalfHeight() + 10.0f);

		FHitResult OutHit;
		//const bool bDidHit = TargetFloor.Component->LineTraceComponent(OutHit, TraceStart, TraceStart + Offset, QueryParams);
		bool bDidHit = GetWorld()->LineTraceSingleByChannel(OutHit, TraceStart, TraceStart + Offset, CollisionChannel, QueryParams, ResponseParam);

		if (!bDidHit)
		{
			GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Yellow, TEXT("Didn't hit!"));
		}
		else
		{
			if (!IsWalkable(OutHit))
			{
				bDidHit = false;
			}
		}

		//DrawDebugLine(GetWorld(), TraceStart, TraceStart + Offset, FColor::Red, true);
		//DrawDebugCapsule(GetWorld(), BaseVRCharacterOwner->GetVRLocation(), BaseVRCharacterOwner->VRRootReference->GetScaledCapsuleHalfHeight(), 5.0f, BaseVRCharacterOwner->VRRootReference->GetComponentQuat(), FColor::Green, true);
		if (bDidHit)
		{
			FVector NewGravityDir = -OutHit.Normal;

			// Don't run gravity changes within our walkable slope
			float AngleOfChange = FMath::Abs(FMath::RadiansToDegrees(acosf(FVector::DotProduct(-OutHit.Normal, GetGravityDirection()))));
			if (AngleOfChange > GetWalkableFloorAngle() || AngleOfChange < 0.01f)
			{
				return;
			}
			else
			{
				if (bBlendGravityFloorChanges)
				{	
					// Blend the angle over time
					//const float Alpha = FMath::Clamp(DeltaTime * FloorOrientationChangeBlendRate, 0.f, 1.f);
					//NewGravityDir = FMath::Lerp(GetGravityDirection(), NewGravityDir, Alpha);
					//bIsBlendingOrientation = true;
				}
			}

			SetCharacterToNewGravity(NewGravityDir, true, DeltaTime);
		}
	}
}

bool UVRBaseCharacterMovementComponent::SetCharacterToNewGravity(FVector NewGravityDirection, bool bOrientToNewGravity, float DeltaTime)
{
	// Ensure its normalized
	NewGravityDirection.Normalize();

	if (NewGravityDirection.Equals(GetGravityDirection()))
		return false;

	//SetGravityDirection(NewGravityDirection);

	if (bOrientToNewGravity && IsValid(BaseVRCharacterOwner))
	{
		FQuat CurrentRotQ = UpdatedComponent->GetComponentQuat();
		FQuat DeltaRot = FQuat::FindBetweenNormals(-CurrentRotQ.GetUpVector(), NewGravityDirection);

		if (bBlendGravityFloorChanges)
		{
			// Blend the angle over time
			const float Alpha = FMath::Clamp(DeltaTime * FloorOrientationChangeBlendRate, 0.f, 1.f);
			DeltaRot = FQuat::Slerp(FQuat::Identity, DeltaRot, Alpha);

			//NewGravityDir = FMath::Lerp(GetGravityDirection(), NewGravityDir, Alpha);
			bIsBlendingOrientation = true;
		}

		FQuat NewRot = (DeltaRot * CurrentRotQ);
		NewRot.Normalize();

		NewGravityDirection = -NewRot.GetUpVector();

		AController* OwningController = BaseVRCharacterOwner->GetController();

		float PivotZ = BaseVRCharacterOwner->bRetainRoomscale ? 0.0f : -BaseVRCharacterOwner->VRRootReference->GetUnscaledCapsuleHalfHeight();
		FQuat NewRotation = NewRot;

		// Clamp to 2 decimal precision
		/*NewRotation = NewRotation.Clamp();
		//NewRotation.Pitch = (NewRotation.Pitch * 100.f) / 100.f;
		//NewRotation.Yaw = (NewRotation.Yaw * 100.f) / 100.f;
		//NewRotation.Roll = (NewRotation.Roll * 100.f) / 100.f;*/
		//NewRotation.Normalize();

		FTransform BaseTransform = BaseVRCharacterOwner->VRRootReference->GetComponentTransform();
		FVector PivotPoint = BaseTransform.TransformPosition(FVector(0.0f, 0.0f, PivotZ));

		//DrawDebugSphere(GetWorld(), BaseVRCharacterOwner->GetVRLocation(), 10.0f, 12.0f, FColor::White, true);
		//DrawDebugSphere(GetWorld(), PivotPoint, 10.0f, 12.0f, FColor::Orange, true);

		FVector BasePoint = PivotPoint; // Get our pivot point
		const FTransform PivotToWorld = FTransform(FQuat::Identity, BasePoint);
		const FTransform WorldToPivot = FTransform(FQuat::Identity, -BasePoint);

		// Rebase the world transform to the pivot point, add the rotation, remove the pivot point rebase
		FTransform NewTransform = BaseTransform * WorldToPivot * FTransform(DeltaRot, FVector::ZeroVector, FVector(1.0f)) * PivotToWorld;

		//FVector NewLoc = BaseVRCharacterOwner->VRRootReference->OffsetComponentToWorld.InverseTransformPosition(PivotPoint);
		//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, FString::Printf(TEXT("NewLoc %s"), *NewLoc.ToString()));

		//DrawDebugLine(GetWorld(), OrigLocation, OrigLocation + ((-NewGravityDirection) * 40.0f), FColor::Red, true);

		// Also setting actor rot because the control rot transfers to it anyway eventually
		MoveUpdatedComponent(NewTransform.GetLocation()/*NewLocation*/ - BaseTransform.GetLocation(), NewRotation, /*bSweep*/ false);

		if (BaseVRCharacterOwner->bUseControllerRotationYaw && OwningController)
			OwningController->SetControlRotation(NewRotation.Rotator());
		
		SetGravityDirection(NewGravityDirection);
		return true;
	}
	else
	{
		SetGravityDirection(NewGravityDirection);
		return true;
	}

	//return false;
}