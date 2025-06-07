// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Movement.cpp: Character movement implementation

=============================================================================*/

#include "VRCharacterMovementComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VRCharacterMovementComponent)

#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/GameNetworkManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameState.h"
#include "GameFramework/WorldSettings.h"
#include "Components/PrimitiveComponent.h"
#include "Animation/AnimMontage.h"
#include "DrawDebugHelpers.h"
//#include "PhysicsEngine/DestructibleActor.h"
#include "VRCharacter.h"
#include "VRExpansionFunctionLibrary.h"

// @todo this is here only due to circular dependency to AIModule. To be removed
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/AvoidanceManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/BrushComponent.h"
//#include "Components/DestructibleComponent.h"

#include "Engine/DemoNetDriver.h"
#include "Engine/NetworkObjectList.h"
#include "UObject/Package.h"

#include "VRRootComponent.h"
#include "WorldCollision.h"
#include "Runtime/Launch/Resources/Version.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Interfaces/NetworkPredictionInterface.h"

//#include "PerfCountersHelpers.h"

DEFINE_LOG_CATEGORY(LogVRCharacterMovement);

/**
 * Character stats
 */
DECLARE_CYCLE_STAT(TEXT("Char StepUp"), STAT_CharStepUp, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char FindFloor"), STAT_CharFindFloor, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char ReplicateMoveToServer"), STAT_CharacterMovementReplicateMoveToServer, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char CallServerMove"), STAT_CharacterMovementCallServerMove, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char CombineNetMove"), STAT_CharacterMovementCombineNetMove, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysWalking"), STAT_CharPhysWalking, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysFalling"), STAT_CharPhysFalling, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char PhysNavWalking"), STAT_CharPhysNavWalking, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char NavProjectPoint"), STAT_CharNavProjectPoint, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char NavProjectLocation"), STAT_CharNavProjectLocation, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char AdjustFloorHeight"), STAT_CharAdjustFloorHeight, STATGROUP_Character);
DECLARE_CYCLE_STAT(TEXT("Char ProcessLanded"), STAT_CharProcessLanded, STATGROUP_Character);

namespace CharacterMovementConstants
{
	// MAGIC NUMBERS
	const float MAX_STEP_SIDE_ZVR = 0.08f;	// maximum z value for the normal on the vertical side of steps
	const float SWIMBOBSPEEDVR = -80.f;
	const float VERTICAL_SLOPE_NORMAL_ZVR = 0.001f; // Slope is vertical if Abs(Normal.Z) <= this threshold. Accounts for precision problems that sometimes angle normals slightly off horizontal for vertical surface.
}

// Defines for build configs
#if DO_CHECK && !UE_BUILD_SHIPPING // Disable even if checks in shipping are enabled.
#define devCodeVR( Code )		checkCode( Code )
#else
#define devCodeVR(...)
#endif

// Statics
namespace CharacterMovementComponentStatics
{
	static const FName CrouchTraceName = FName(TEXT("CrouchTrace"));
	static const FName ImmersionDepthName = FName(TEXT("MovementComp_Character_ImmersionDepth"));

	static float fRotationCorrectionThreshold = 0.02f;
	FAutoConsoleVariableRef CVarRotationCorrectionThreshold(
		TEXT("vre.RotationCorrectionThreshold"),
		fRotationCorrectionThreshold,
		TEXT("Error threshold value before correcting a clients rotation.\n")
		TEXT("Rotation is replicated at 2 decimal precision, so values less than 0.01 won't matter."),
		ECVF_Default);

	static float fRotationChangingCorrectionThreshold = 0.3f;
	FAutoConsoleVariableRef CVarRotationChangingCorrectionThreshold(
		TEXT("vre.RotationChangingCorrectionThreshold"),
		fRotationChangingCorrectionThreshold,
		TEXT("Error threshold value before correcting a clients rotation when actively changing rotation.\n")
		TEXT("Rotation is replicated at 2 decimal precision, so values less than 0.01 won't matter."),
		ECVF_Default);
}

void UVRCharacterMovementComponent::StoreSetTrackingPaused(bool bNewTrackingPaused)
{
	FVRMoveActionContainer MoveAction;
	MoveAction.MoveAction = EVRMoveAction::VRMOVEACTION_PauseTracking;
	MoveAction.MoveActionFlags = bNewTrackingPaused;
	MoveAction.MoveActionLoc = VRRootCapsule->curCameraLoc;
	MoveAction.MoveActionRot = VRRootCapsule->StoredCameraRotOffset;
	MoveActionArray.MoveActions.Add(MoveAction);
	CheckServerAuthedMoveAction();
}

void UVRCharacterMovementComponent::Crouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}

	if (!bClientSimulation && !CanCrouchInCurrentState())
	{
		return;
	}

	// See if collision is already at desired size.
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == GetCrouchedHalfHeight())
	{
		if (!bClientSimulation)
		{
			CharacterOwner->SetIsCrouched(true);
		}
		CharacterOwner->OnStartCrouch(0.f, 0.f);
		return;
	}

	if (bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// restore collision size before crouching
		ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
		if (VRRootCapsule)
			VRRootCapsule->SetCapsuleSizeVR(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
		else
			CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());

		bShrinkProxyCapsule = true;
	}

	// Change collision size to crouching dimensions
	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float OldUnscaledRadius = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	// Height is not allowed to be smaller than radius.
	const float ClampedCrouchedHalfHeight = FMath::Max3(0.f, OldUnscaledRadius, GetCrouchedHalfHeight());

	if (VRRootCapsule)
		VRRootCapsule->SetCapsuleSizeVR(OldUnscaledRadius, ClampedCrouchedHalfHeight);
	else
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, ClampedCrouchedHalfHeight);

	float HalfHeightAdjust = (OldUnscaledHalfHeight - ClampedCrouchedHalfHeight);
	float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	if (!bClientSimulation)
	{
		// Crouching to a larger height? (this is rare)
		if (ClampedCrouchedHalfHeight > OldUnscaledHalfHeight)
		{
			FCollisionQueryParams CapsuleParams(CharacterMovementComponentStatics::CrouchTraceName, false, CharacterOwner);
			FCollisionResponseParams ResponseParam;
			InitCollisionParams(CapsuleParams, ResponseParam);

			FVector capLocation;
			if (VRRootCapsule)
			{
				capLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();
			}
			else
				capLocation = UpdatedComponent->GetComponentLocation();

			const bool bEncroached = GetWorld()->OverlapBlockingTestByChannel(capLocation - (ScaledHalfHeightAdjust * GetGravityDirection()), FQuat::Identity,
				UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CapsuleParams, ResponseParam);

			// If encroached, cancel
			if (bEncroached)
			{
				if (VRRootCapsule)
					VRRootCapsule->SetCapsuleSizeVR(OldUnscaledRadius, OldUnscaledHalfHeight);
				else
					CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(OldUnscaledRadius, OldUnscaledHalfHeight);
				return;
			}
		}

		// Skipping this move down as the VR character's base root doesn't behave the same
		// Retain roomscale off also covers it in the capsule size rescale
		if (bCrouchMaintainsBaseLocation)
		{
			// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
			//UpdatedComponent->MoveComponent(ScaledHalfHeightAdjust * GetGravityDirection(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
		}

		CharacterOwner->SetIsCrouched(true);
	}

	bForceNextFloorCheck = true;

	// OnStartCrouch takes the change from the Default size, not the current one (though they are usually the same).
	const float MeshAdjust = ScaledHalfHeightAdjust;
	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();
	HalfHeightAdjust = (DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - ClampedCrouchedHalfHeight);
	ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	AdjustProxyCapsuleSize();
	CharacterOwner->OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	// Set CapsuleSize VR already handles this
	/*if (!BaseVRCharacterOwner->bRetainRoomscale)
	{
		if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
		{
			FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();

			if (ClientData)
			{
				ClientData->MeshTranslationOffset -= MeshAdjust * -GetGravityDirection();
				ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
			}
		}
	}*/
}

void UVRCharacterMovementComponent::UnCrouch(bool bClientSimulation)
{
	if (!HasValidData())
	{
		return;
	}

	ACharacter* DefaultCharacter = CharacterOwner->GetClass()->GetDefaultObject<ACharacter>();

	// See if collision is already at desired size.
	if (CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() == DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight())
	{
		if (!bClientSimulation)
		{
			CharacterOwner->SetIsCrouched(false);
		}
		CharacterOwner->OnEndCrouch(0.f, 0.f);
		return;
	}

	const float CurrentCrouchedHalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	const float ComponentScale = CharacterOwner->GetCapsuleComponent()->GetShapeScale();
	const float OldUnscaledHalfHeight = CharacterOwner->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const float HalfHeightAdjust = DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() - OldUnscaledHalfHeight;
	const float ScaledHalfHeightAdjust = HalfHeightAdjust * ComponentScale;

	/*const*/ FVector PawnLocation = UpdatedComponent->GetComponentLocation();

	if (VRRootCapsule)
	{
		PawnLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();
	}

	// Grow to uncrouched size.
	check(CharacterOwner->GetCapsuleComponent());

	if (!bClientSimulation)
	{
		// Try to stay in place and see if the larger capsule fits. We use a slightly taller capsule to avoid penetration.
		const UWorld* MyWorld = GetWorld();
		const float SweepInflation = UE_KINDA_SMALL_NUMBER * 10.f;
		FCollisionQueryParams CapsuleParams(CharacterMovementComponentStatics::CrouchTraceName, false, CharacterOwner);
		FCollisionResponseParams ResponseParam;
		InitCollisionParams(CapsuleParams, ResponseParam);

		// Compensate for the difference between current capsule size and standing size
		const FCollisionShape StandingCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, -SweepInflation - ScaledHalfHeightAdjust); // Shrink by negative amount, so actually grow it.
		const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
		bool bEncroached = true;

		if (!bCrouchMaintainsBaseLocation)
		{
			// Expand in place
			bEncroached = MyWorld->OverlapBlockingTestByChannel(PawnLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				// Try adjusting capsule position to see if we can avoid encroachment.
				if (ScaledHalfHeightAdjust > 0.f)
				{
					// Shrink to a short capsule, sweep down to base to find where that would hit something, and then try to stand up from there.
					float PawnRadius, PawnHalfHeight;
					CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);
					const float ShrinkHalfHeight = PawnHalfHeight - PawnRadius;
					const float TraceDist = PawnHalfHeight - ShrinkHalfHeight;
					const FVector Down = TraceDist * GetGravityDirection();

					FHitResult Hit(1.f);
					const FCollisionShape ShortCapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_HeightCustom, ShrinkHalfHeight);
					const bool bBlockingHit = MyWorld->SweepSingleByChannel(Hit, PawnLocation, PawnLocation + Down, GetWorldToGravityTransform(), CollisionChannel, ShortCapsuleShape, CapsuleParams);
					if (Hit.bStartPenetrating)
					{
						bEncroached = true;
					}
					else
					{
						// Compute where the base of the sweep ended up, and see if we can stand there
						const float DistanceToBase = (Hit.Time * TraceDist) + ShortCapsuleShape.Capsule.HalfHeight;
						const FVector Adjustment = (-DistanceToBase + StandingCapsuleShape.Capsule.HalfHeight + SweepInflation + MIN_FLOOR_DIST / 2.f) * -GetGravityDirection();
						const FVector NewLoc = PawnLocation + Adjustment;
						bEncroached = MyWorld->OverlapBlockingTestByChannel(NewLoc, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
						if (!bEncroached)
						{
							// Intentionally not using MoveUpdatedComponent, where a horizontal plane constraint would prevent the base of the capsule from staying at the same spot.
							UpdatedComponent->MoveComponent(NewLoc - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						}
					}
				}
			}
		}
		else
		{
			// Expand while keeping base location the same.
			FVector StandingLocation = PawnLocation + (StandingCapsuleShape.GetCapsuleHalfHeight() - CurrentCrouchedHalfHeight) * -GetGravityDirection();
			bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);

			if (bEncroached)
			{
				if (IsMovingOnGround())
				{
					// Something might be just barely overhead, try moving down closer to the floor to avoid it.
					const float MinFloorDist = UE_KINDA_SMALL_NUMBER * 10.f;
					if (CurrentFloor.bBlockingHit && CurrentFloor.FloorDist > MinFloorDist)
					{
						StandingLocation -= (CurrentFloor.FloorDist - MinFloorDist) * -GetGravityDirection();
						bEncroached = MyWorld->OverlapBlockingTestByChannel(StandingLocation, GetWorldToGravityTransform(), CollisionChannel, StandingCapsuleShape, CapsuleParams, ResponseParam);
					}
				}
			}

			// Canceling this move because our VR capsule isn't actor based like it expects it to be
			if (!bEncroached)
			{
				// Commit the change in location.
				if (!BaseVRCharacterOwner || !BaseVRCharacterOwner->bRetainRoomscale)
				{
					// we actually move this when not using retained roomscale
					//UpdatedComponent->MoveComponent(StandingLocation - PawnLocation, UpdatedComponent->GetComponentQuat(), false, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
				}
				bForceNextFloorCheck = true;
			}
		}

		// If still encroached then abort.
		if (bEncroached)
		{
			return;
		}

		CharacterOwner->SetIsCrouched(false);
	}
	else
	{
		bShrinkProxyCapsule = true;
	}

	// Now call SetCapsuleSize() to cause touch/untouch events and actually grow the capsule
	if (VRRootCapsule)
		VRRootCapsule->SetCapsuleSizeVR(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), true);
	else
		CharacterOwner->GetCapsuleComponent()->SetCapsuleSize(DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), DefaultCharacter->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), true);

	const float MeshAdjust = ScaledHalfHeightAdjust;
	AdjustProxyCapsuleSize();
	CharacterOwner->OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// Don't smooth this change in mesh position
	/*if ((bClientSimulation && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) || (IsNetMode(NM_ListenServer) && CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy))
	{
		FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();

		if (ClientData)
		{
			ClientData->MeshTranslationOffset += MeshAdjust * -GetGravityDirection();
			ClientData->OriginalMeshTranslationOffset = ClientData->MeshTranslationOffset;
		}
	}*/
}

FNetworkPredictionData_Client* UVRCharacterMovementComponent::GetPredictionData_Client() const
{
	// Should only be called on client or listen server (for remote clients) in network games
	check(CharacterOwner != NULL);
	checkSlow(CharacterOwner->GetLocalRole() < ROLE_Authority || (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy && GetNetMode() == NM_ListenServer));
	checkSlow(GetNetMode() == NM_Client || GetNetMode() == NM_ListenServer);

	if (!ClientPredictionData)
	{
		UVRCharacterMovementComponent* MutableThis = const_cast<UVRCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_VRCharacter(*this);
	}

	return ClientPredictionData;
}

FNetworkPredictionData_Server* UVRCharacterMovementComponent::GetPredictionData_Server() const
{
	// Should only be called on server in network games
	check(CharacterOwner != NULL);
	check(CharacterOwner->GetLocalRole() == ROLE_Authority);
	checkSlow(GetNetMode() < NM_Client);

	if (!ServerPredictionData)
	{
		UVRCharacterMovementComponent* MutableThis = const_cast<UVRCharacterMovementComponent*>(this);
		MutableThis->ServerPredictionData = new FNetworkPredictionData_Server_VRCharacter(*this);
	}

	return ServerPredictionData;
}

void FSavedMove_VRCharacter::SetInitialPosition(ACharacter* C)
{

	// See if we can get the VR capsule location
	if (AVRCharacter * VRC = Cast<AVRCharacter>(C))
	{
		UVRCharacterMovementComponent * CharMove = Cast<UVRCharacterMovementComponent>(VRC->GetCharacterMovement());
		if (VRC->VRRootReference)
		{
			VRCapsuleLocation = VRC->VRRootReference->curCameraLoc;
			VRCapsuleRotation = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(VRC->VRRootReference->curCameraRot);

			// Don't reset this unless its zero, after that its from a movement merge
			if (LFDiff.IsZero())
			{
				LFDiff = VRC->VRRootReference->DifferenceFromLastFrame;
			}
		}
		else
		{
			VRCapsuleLocation = FVector::ZeroVector;
			VRCapsuleRotation = FRotator::ZeroRotator;
			LFDiff = FVector::ZeroVector;
		}
	}

	FSavedMove_VRBaseCharacter::SetInitialPosition(C);
}

void FSavedMove_VRCharacter::PrepMoveFor(ACharacter* Character)
{
	UVRCharacterMovementComponent * CharMove = Cast<UVRCharacterMovementComponent>(Character->GetCharacterMovement());

	// Set capsule location prior to testing movement
	// I am overriding the replicated value here when movement is made on purpose
	if (CharMove && CharMove->VRRootCapsule)
	{
		CharMove->VRRootCapsule->curCameraLoc = this->VRCapsuleLocation;
		CharMove->VRRootCapsule->curCameraRot = this->VRCapsuleRotation;//FRotator(0.0f, FRotator::DecompressAxisFromByte(CapsuleYaw), 0.0f);
		CharMove->VRRootCapsule->DifferenceFromLastFrame = LFDiff;// FVector(LFDiff.X, LFDiff.Y, 0.0f);
		CharMove->AdditionalVRInputVector = CharMove->VRRootCapsule->DifferenceFromLastFrame;

		if (AVRBaseCharacter * BaseChar = Cast<AVRBaseCharacter>(CharMove->GetCharacterOwner()))
		{
			if (BaseChar->GetVRReplicateCapsuleHeight() && this->CapsuleHeight > 0.0f && !FMath::IsNearlyEqual(this->CapsuleHeight, CharMove->VRRootCapsule->GetUnscaledCapsuleHalfHeight()))
			{
				BaseChar->SetCharacterHalfHeightVR(CapsuleHeight, false);
				//CharMove->VRRootCapsule->SetCapsuleHalfHeight(this->LFDiff.Z, false);
			}
		}

		CharMove->VRRootCapsule->StoredCameraRotOffset = CharMove->VRRootCapsule->curCameraRot;
		CharMove->VRRootCapsule->GenerateOffsetToWorld(false, false);
	}

	FSavedMove_VRBaseCharacter::PrepMoveFor(Character);
}


void UVRCharacterMovementComponent::RegenerateOffset()
{
	if(VRRootCapsule)
		VRRootCapsule->GenerateOffsetToWorld();
}

void UVRCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	QUICK_SCOPE_CYCLE_COUNTER(VRCharacterMovementServerMove_PerformMovement);
	//SCOPE_CYCLE_COUNTER(STAT_VRCharacterMovementServerMove);
	//CSV_SCOPED_TIMING_STAT(CharacterMovement, CharacterMovementServerMove);

	if (!HasValidData() || !IsActive())
	{
		return;
	}

	bool bAutoAcceptPacket = false;

	FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	check(ServerData);

	// Convert to our stored move data array
	const FVRCharacterNetworkMoveData* MoveDataVR = (const FVRCharacterNetworkMoveData*)&MoveData;

	if (MovementMode == MOVE_Custom && CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Seated)
	{
		return;
	}
	else if (bJustUnseated && MoveDataVR->ReplicatedMovementMode != EVRConjoinedMovementModes::C_VRMOVE_Seated)
	{	
		// If the client isn't still registered as seated then we accept this first change of movement and go
		ServerData->CurrentClientTimeStamp = MoveData.TimeStamp;
		bAutoAcceptPacket = true;
		bJustUnseated = false;
	}

	const float ClientTimeStamp = MoveData.TimeStamp;
	FVector ClientAccel = MoveData.Acceleration;

	static const auto CVarNetUseBaseRelativeAcceleration = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetUseBaseRelativeAcceleration"));
	// Convert the move's acceleration to worldspace if necessary
	if (CVarNetUseBaseRelativeAcceleration->GetInt() && MovementBaseUtility::IsDynamicBase(MoveData.MovementBase))
	{
		MovementBaseUtility::TransformDirectionToWorld(MoveData.MovementBase, MoveData.MovementBaseBoneName, MoveData.Acceleration, ClientAccel);
	}

	const uint8 ClientMoveFlags = MoveData.CompressedMoveFlags;
	const FRotator ClientControlRotation = MoveData.ControlRotation;

	if (!bAutoAcceptPacket && !VerifyClientTimeStamp(ClientTimeStamp, *ServerData))
	{
		const float ServerTimeStamp = ServerData->CurrentClientTimeStamp;
		// This is more severe if the timestamp has a large discrepancy and hasn't been recently reset.
		static const auto CVarNetServerMoveTimestampExpiredWarningThreshold = IConsoleManager::Get().FindConsoleVariable(TEXT("net.NetServerMoveTimestampExpiredWarningThreshold"));
		if (ServerTimeStamp > 1.0f && FMath::Abs(ServerTimeStamp - ClientTimeStamp) > CVarNetServerMoveTimestampExpiredWarningThreshold->GetFloat())
		{
			UE_LOG(LogNetPlayerMovement, Warning, TEXT("ServerMove: TimeStamp expired: %f, CurrentTimeStamp: %f, Character: %s"), ClientTimeStamp, ServerTimeStamp, *GetNameSafe(CharacterOwner));
		}
		else
		{
			UE_LOG(LogNetPlayerMovement, Log, TEXT("ServerMove: TimeStamp expired: %f, CurrentTimeStamp: %f, Character: %s"), ClientTimeStamp, ServerTimeStamp, *GetNameSafe(CharacterOwner));
		}
		return;
	}

	// Scope these, they nest with Outer references so it should work fine, this keeps the update rotation and move autonomous from double updating the char
	FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

	bool bServerReadyForClient = true;
	APlayerController* PC = Cast<APlayerController>(CharacterOwner->GetController());
	if (PC)
	{
		bServerReadyForClient = PC->NotifyServerReceivedClientData(CharacterOwner, ClientTimeStamp);
		if (!bServerReadyForClient)
		{
			ClientAccel = FVector::ZeroVector;
		}
	}

	const UWorld* MyWorld = GetWorld();
	/*const*/ float DeltaTime = 0.0f;
	
	// If we are auto accepting this packet, then accept it and use the maximum delta time as we don't know how long it has actually been since
	// The last valid update
	if (bAutoAcceptPacket)
	{
		DeltaTime = ServerData->MaxMoveDeltaTime * CharacterOwner->GetActorTimeDilation(*MyWorld);
	}
	else
	{
		DeltaTime = ServerData->GetServerMoveDeltaTime(ClientTimeStamp, CharacterOwner->GetActorTimeDilation(*MyWorld));
	}

	// They added a scope here, im not using it as im doing the full capsule above
	// Scope these, they nest with Outer references so it should work fine, this keeps the update rotation and move autonomous from double updating the char
	//const FScopedPreventAttachedComponentMove PreventMeshMove(BaseVRCharacterOwner ? BaseVRCharacterOwner->NetSmoother : nullptr);

	if (DeltaTime > 0.f)
	{
		ServerData->CurrentClientTimeStamp = ClientTimeStamp;
		ServerData->ServerAccumulatedClientTimeStamp += DeltaTime;
		ServerData->ServerTimeStamp = MyWorld->GetTimeSeconds();
		ServerData->ServerTimeStampLastServerMove = ServerData->ServerTimeStamp;

		if (bUseClientControlRotation)
		{
			if (AController* CharacterController = Cast<AController>(CharacterOwner->GetController()))
			{
				CharacterController->SetControlRotation(ClientControlRotation);
			}
		}

		if (!bServerReadyForClient)
		{
			return;
		}

		// Perform actual movement
		if ((MyWorld->GetWorldSettings()->GetPauserPlayerState() == NULL))
		{
			if (PC)
			{
				PC->UpdateRotation(DeltaTime);
			}

			if (!MoveDataVR->ConditionalMoveReps.RequestedVelocity.IsZero())
			{
				RequestedVelocity = MoveDataVR->ConditionalMoveReps.RequestedVelocity;
				bHasRequestedVelocity = true;
			}

			CustomVRInputVector = MoveDataVR->ConditionalMoveReps.CustomVRInputVector;
			MoveActionArray = MoveDataVR->ConditionalMoveReps.MoveActionArray;
			VRReplicatedMovementMode = MoveDataVR->ReplicatedMovementMode;

			// Set capsule location prior to testing movement
			// I am overriding the replicated value here when movement is made on purpose
			if (VRRootCapsule)
			{
				VRRootCapsule->curCameraLoc = MoveDataVR->VRCapsuleLocation;
				VRRootCapsule->curCameraRot = FRotator(0.0f, FRotator::DecompressAxisFromShort(MoveDataVR->VRCapsuleRotation), 0.0f);
				VRRootCapsule->DifferenceFromLastFrame = MoveDataVR->LFDiff;//FVector(MoveDataVR->LFDiff.X, MoveDataVR->LFDiff.Y, 0.0f);
				AdditionalVRInputVector = VRRootCapsule->DifferenceFromLastFrame;

				if (BaseVRCharacterOwner)
				{
					if (BaseVRCharacterOwner->GetVRReplicateCapsuleHeight() && MoveDataVR->CapsuleHeight > 0.0f && !FMath::IsNearlyEqual(MoveDataVR->CapsuleHeight, VRRootCapsule->GetUnscaledCapsuleHalfHeight()))
					{
						BaseVRCharacterOwner->SetCharacterHalfHeightVR(MoveDataVR->CapsuleHeight, false);
						//	BaseChar->ReplicatedCapsuleHeight.CapsuleHeight = LFDiff.Z;
							//VRRootCapsule->SetCapsuleHalfHeight(LFDiff.Z, false);
					}
				}

				VRRootCapsule->StoredCameraRotOffset = VRRootCapsule->curCameraRot;
				VRRootCapsule->GenerateOffsetToWorld(false, false);
			}

			MoveAutonomous(ClientTimeStamp, DeltaTime, ClientMoveFlags, ClientAccel);
			bHasRequestedVelocity = false;
		}


		UE_CLOG(CharacterOwner && UpdatedComponent, LogNetPlayerMovement, VeryVerbose, TEXT("ServerMove Time %f Acceleration %s Velocity %s Position %s Rotation %s GravityDirection %s DeltaTime %f Mode %s MovementBase %s.%s (Dynamic:%d)"),
			ClientTimeStamp, *ClientAccel.ToString(), *Velocity.ToString(), *UpdatedComponent->GetComponentLocation().ToString(), *UpdatedComponent->GetComponentRotation().ToCompactString(), *GetGravityDirection().ToCompactString(), DeltaTime, *GetMovementName(),
			*GetNameSafe(GetMovementBase()), *CharacterOwner->GetBasedMovement().BoneName.ToString(), MovementBaseUtility::IsDynamicBase(GetMovementBase()) ? 1 : 0);
	}

	// #TODO: Handle this better at some point? Client also denies it later on during correction (ApplyNetworkMovementMode in base movement)
	// Pre handling the errors, lets avoid rolling back to/from custom movement modes, they tend to be scripted and this can screw things up
	const uint8 CurrentPackedMovementMode = PackNetworkMovementMode();
	if (CurrentPackedMovementMode != MoveData.MovementMode)
	{
		TEnumAsByte<EMovementMode> NetMovementMode(MOVE_None);
		TEnumAsByte<EMovementMode> NetGroundMode(MOVE_None);
		uint8 NetCustomMode(0);
		UnpackNetworkMovementMode(MoveData.MovementMode, NetMovementMode, NetCustomMode, NetGroundMode);

		// Custom movement modes aren't going to be rolled back as they are client authed for our pawns
		if (NetMovementMode == EMovementMode::MOVE_Custom || MovementMode == EMovementMode::MOVE_Custom)
		{
			if (NetCustomMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing || CustomMovementMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing)
				SetMovementMode(NetMovementMode, NetCustomMode);
		}
	}

	// Validate move only after old and first dual portion, after all moves are completed.
	if (MoveData.NetworkMoveType == FCharacterNetworkMoveData::ENetworkMoveType::NewMove)
	{
		ServerMoveHandleClientErrorVR(ClientTimeStamp, DeltaTime, ClientAccel, MoveData.Location, ClientControlRotation, MoveData.MovementBase, MoveData.MovementBaseBoneName, MoveData.MovementMode);
		//ServerMoveHandleClientErrorVR(ClientTimeStamp, DeltaTime, ClientAccel, MoveData.Location, ClientControlRotation.Yaw, MoveData.MovementBase, MoveData.MovementBaseBoneName, MoveData.MovementMode);
		//ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, ClientAccel, MoveData.Location, MoveData.MovementBase, MoveData.MovementBaseBoneName, MoveData.MovementMode);
	}
}

/*void UVRCharacterMovementComponent::CallServerMove
(
	const class FSavedMove_Character* NewCMove,
	const class FSavedMove_Character* OldCMove
	)
{
	// This is technically "safe", I know for sure that I am using my own FSavedMove
	// I would have like to not override any of this, but I need a lot more specific information about the pawn
	// So just setting flags in the FSaved Move doesn't cut it
	// I could see a problem if someone overrides this override though
	const FSavedMove_VRCharacter * NewMove = (const FSavedMove_VRCharacter *)NewCMove;
	const FSavedMove_VRCharacter * OldMove = (const FSavedMove_VRCharacter *)OldCMove;

	check(NewMove != nullptr);
	//uint32 ClientYawPitchINT = 0;
	//uint8 ClientRollBYTE = 0;
	//NewMove->GetPackedAngles(ClientYawPitchINT, ClientRollBYTE);
	const uint16 CapsuleYawShort = FRotator::CompressAxisToShort(NewMove->VRCapsuleRotation.Yaw);
	const uint16 ClientYawShort = FRotator::CompressAxisToShort(NewMove->SavedControlRotation.Yaw);

	// Determine if we send absolute or relative location
	UPrimitiveComponent* ClientMovementBase = NewMove->EndBase.Get();
	const FName ClientBaseBone = NewMove->EndBoneName;
	const FVector SendLocation = MovementBaseUtility::UseRelativeLocation(ClientMovementBase) ? NewMove->SavedRelativeLocation : NewMove->SavedLocation;

	// send old move if it exists
	if (OldMove)
	{
		//const uint16 CapsuleYawShort = FRotator::CompressAxisToShort(OldMove->VRCapsuleRotation.Yaw);
		ServerMoveVROld(OldMove->TimeStamp, OldMove->Acceleration, OldMove->GetCompressedFlags(), OldMove->ConditionalValues);
	}

	// Pass these in here, don't pass in to old move, it will receive the new move values in dual operations
	// Will automatically not replicate them if movement base is nullptr (1 bit cost to check this)
	FVRConditionalMoveRep2 NewMoveConds;
	NewMoveConds.ClientMovementBase = ClientMovementBase;
	NewMoveConds.ClientBaseBoneName = ClientBaseBone;

	if (CharacterOwner && (CharacterOwner->bUseControllerRotationRoll || CharacterOwner->bUseControllerRotationPitch))
	{
		NewMoveConds.ClientPitch = FRotator::CompressAxisToShort(NewMove->SavedControlRotation.Pitch);
		NewMoveConds.ClientRoll = FRotator::CompressAxisToByte(NewMove->SavedControlRotation.Roll);
	}

	NewMoveConds.ClientYaw = FRotator::CompressAxisToShort(NewMove->SavedControlRotation.Yaw);

	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	if (const FSavedMove_Character* const PendingMove = ClientData->PendingMove.Get())
	{
		// This should send same as the uint16 because it uses a packedINT send by default for shorts
		//uint32 OldClientYawPitchINT = 0;
		//uint8 OldClientRollBYTE = 0;
		//ClientData->PendingMove->GetPackedAngles(OldClientYawPitchINT, OldClientRollBYTE);

		uint32 cPitch = 0;
		if (CharacterOwner && (CharacterOwner->bUseControllerRotationPitch))
			cPitch = FRotator::CompressAxisToShort(ClientData->PendingMove->SavedControlRotation.Pitch);
		
		uint32 cYaw = FRotator::CompressAxisToShort(ClientData->PendingMove->SavedControlRotation.Yaw);

		// Switch the order of pitch and yaw to place Yaw in smallest value, this will cut down on rep cost since normally pitch is zero'd out in VR
		uint32 OldClientYawPitchINT = (cPitch << 16) | (cYaw);

		FSavedMove_VRCharacter* oldMove = (FSavedMove_VRCharacter*)ClientData->PendingMove.Get();
		const uint16 OldCapsuleYawShort = FRotator::CompressAxisToShort(oldMove->VRCapsuleRotation.Yaw);
		//const uint16 OldClientYawShort = FRotator::CompressAxisToShort(ClientData->PendingMove->SavedControlRotation.Yaw);


		// If we delayed a move without root motion, and our new move has root motion, send these through a special function, so the server knows how to process them.
		if ((PendingMove->RootMotionMontage == NULL) && (NewMove->RootMotionMontage != NULL))
		{
		// send two moves simultaneously
			ServerMoveVRDualHybridRootMotion
			(
				PendingMove->TimeStamp,
				PendingMove->Acceleration,
				PendingMove->GetCompressedFlags(),
				OldClientYawPitchINT,
				oldMove->VRCapsuleLocation,
				oldMove->ConditionalValues,
				oldMove->LFDiff,
				OldCapsuleYawShort,
				NewMove->TimeStamp,
				NewMove->Acceleration,
				SendLocation,
				NewMove->VRCapsuleLocation,
				NewMove->ConditionalValues,
				NewMove->LFDiff,
				CapsuleYawShort,
				NewMove->GetCompressedFlags(),
				NewMoveConds,
				NewMove->EndPackedMovementMode
			);
		}
		else // Not Hybrid root motion rpc
		{
			// send two moves simultaneously
			if (oldMove->Acceleration.IsZero() && NewMove->Acceleration.IsZero())
			{
				ServerMoveVRDualExLight
				(
					PendingMove->TimeStamp,
					PendingMove->GetCompressedFlags(),
					OldClientYawPitchINT,
					oldMove->VRCapsuleLocation,
					oldMove->ConditionalValues,
					oldMove->LFDiff,
					OldCapsuleYawShort,
					NewMove->TimeStamp,
					SendLocation,
					NewMove->VRCapsuleLocation,
					NewMove->ConditionalValues,
					NewMove->LFDiff,
					CapsuleYawShort,
					NewMove->GetCompressedFlags(),
					NewMoveConds,
					NewMove->EndPackedMovementMode
				);
			}
			else
			{
				ServerMoveVRDual
				(
					PendingMove->TimeStamp,
					PendingMove->Acceleration,
					PendingMove->GetCompressedFlags(),
					OldClientYawPitchINT,
					oldMove->VRCapsuleLocation,
					oldMove->ConditionalValues,
					oldMove->LFDiff,
					OldCapsuleYawShort,
					NewMove->TimeStamp,
					NewMove->Acceleration,
					SendLocation,
					NewMove->VRCapsuleLocation,
					NewMove->ConditionalValues,
					NewMove->LFDiff,
					CapsuleYawShort,
					NewMove->GetCompressedFlags(),
					NewMoveConds,
					NewMove->EndPackedMovementMode
				);
			}
		}
	}
	else
	{

		if (NewMove->Acceleration.IsZero())
		{
			ServerMoveVRExLight
			(
				NewMove->TimeStamp,
				SendLocation,
				NewMove->VRCapsuleLocation,
				NewMove->ConditionalValues,
				NewMove->LFDiff,
				CapsuleYawShort,
				NewMove->GetCompressedFlags(),
				NewMoveConds,
				NewMove->EndPackedMovementMode
			);
		}
		else
		{
			ServerMoveVR
			(
				NewMove->TimeStamp,
				NewMove->Acceleration,
				SendLocation,
				NewMove->VRCapsuleLocation,
				NewMove->ConditionalValues,
				NewMove->LFDiff,
				CapsuleYawShort,
				NewMove->GetCompressedFlags(),
				NewMoveConds,
				NewMove->EndPackedMovementMode
			);
		}
	}

	MarkForClientCameraUpdate();
}*/

bool UVRCharacterMovementComponent::ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const
{
	// See if we hit an edge of a surface on the lower portion of the capsule.
	// In this case the normal will not equal the impact normal, and a downward sweep may find a walkable surface on top of the edge.
	if (GetGravitySpaceZ(Hit.Normal) > UE_KINDA_SMALL_NUMBER && !Hit.Normal.Equals(Hit.ImpactNormal))
	{
		FVector PawnLocation = UpdatedComponent->GetComponentLocation();
		if (VRRootCapsule)
			PawnLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();

		if (IsWithinEdgeTolerance(PawnLocation, Hit.ImpactPoint, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius()))
		{
			return true;
		}
	}

	return false;
}

void UVRCharacterMovementComponent::PhysWalking(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysWalking);

	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->GetController() && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
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

	devCodeVR(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN before Iteration (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;

	const EMovementMode StartingMovementMode = MovementMode;
	const uint8 StartingCustomMovementMode = CustomMovementMode;

	// Rewind the players position by the new capsule location
	RewindVRRelativeMovement();

	// Perform the move
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->GetController() || bRunPhysicsWithNoController || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)))
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
		FVector OldCapsuleLocation = VRRootCapsule ? VRRootCapsule->OffsetComponentToWorld.GetLocation() : OldLocation;

		const FFindFloorResult OldFloor = CurrentFloor;

		RestorePreAdditiveRootMotionVelocity();
		//RestorePreAdditiveVRMotionVelocity();

		// Ensure velocity is horizontal.
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;
		Acceleration = FVector::VectorPlaneProject(Acceleration, -GetGravityDirection());


		static const auto CVarLedgeMovementApplyDirectMove = IConsoleManager::Get().FindConsoleVariable(TEXT("p.LedgeMovement.ApplyDirectMove"));
		
		// Apply acceleration
		const bool bSkipForLedgeMove = bTriedLedgeMove && CVarLedgeMovementApplyDirectMove->GetBool();
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bSkipForLedgeMove)
		{
			CalcVelocity(timeTick, GroundFriction, false, GetMaxBrakingDeceleration());
			devCodeVR(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after CalcVelocity (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));
		}

		ApplyRootMotionToVelocity(timeTick);
		ApplyVRMotionToVelocity(deltaTime);//timeTick);

		devCodeVR(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysWalking: Velocity contains NaN after Root Motion application (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

		if (MovementMode != StartingMovementMode || CustomMovementMode != StartingCustomMovementMode)
		{
			// Root motion could have taken us out of our current mode
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
			// TODO: Bugged currently
			/*if (VRRootCapsule && VRRootCapsule->bUseWalkingCollisionOverride)
			{

				FHitResult HitRes;
				FCollisionQueryParams Params("RelativeMovementSweep", false, GetOwner());
				FCollisionResponseParams ResponseParam;

				VRRootCapsule->InitSweepCollisionParams(Params, ResponseParam);
				Params.bFindInitialOverlaps = true;
				bool bWasBlockingHit = false;

				bWasBlockingHit = GetWorld()->SweepSingleByChannel(HitRes, VRRootCapsule->OffsetComponentToWorld.GetLocation(), VRRootCapsule->OffsetComponentToWorld.GetLocation() + VRRootCapsule->DifferenceFromLastFrame, FQuat(0.0f, 0.0f, 0.0f, 1.0f), VRRootCapsule->GetCollisionObjectType(), VRRootCapsule->GetCollisionShape(), Params, ResponseParam);
				
				const FVector GravDir(0.f, 0.f, -1.f);
				if (CanStepUp(HitRes) || (CharacterOwner->GetMovementBase() != NULL && CharacterOwner->GetMovementBase()->GetOwner() == HitRes.GetActor()))
					StepUp(GravDir,VRRootCapsule->DifferenceFromLastFrame.GetSafeNormal2D(), HitRes, &StepDownResult);
			}*/
		}
		else
		{
			// try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsSwimming()) //just entered water
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
			else if (MovementMode != StartingMovementMode || CustomMovementMode != StartingCustomMovementMode)
			{
				// pawn ended up in a different mode, probably due to the step-up-and-over flow
				// let's refund the estimated unused time (if any) and keep moving in the new mode
				const float DesiredDist = Delta.Size();
				if (DesiredDist > UE_KINDA_SMALL_NUMBER)
				{
					const float ActualDist = ProjectToGravityFloor(UpdatedComponent->GetComponentLocation() - OldLocation).Size();
					remainingTime += timeTick * (1.f - FMath::Min(1.f, ActualDist / DesiredDist));
				}
				RestorePreAdditiveVRMotionVelocity();
				StartNewPhysics(remainingTime, Iterations);
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
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, OldFloor);
			// REMOVED 5.5 and replaced with above
			//const FVector GravDir = GetGravityDirection();
			//const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldCapsuleLocation, Delta, GravDir);
			if (!NewDelta.IsZero())
			{
				// first revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta / timeTick;
				remainingTime += timeTick;
				Iterations--;
				RestorePreAdditiveVRMotionVelocity();
				continue;
			}
			else
			{
				// see if it is OK to jump
				// @todo collision : only thing that can be problem is that oldbase has world collision on
				/*bool bMustJump = bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
 				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					RestorePreAdditiveVRMotionVelocity();
					return;
				}*/
				bCheckedFall = true;

				// revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.f;
				RestorePreAdditiveVRMotionVelocity();
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
					RestorePreAdditiveVRMotionVelocity();
					HandleWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsMovingOnGround())
					{
						// If still walking, then fall. If not, assume the user set a different mode they want to keep.
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}
					return;
				}

				AdjustFloorHeight();
				SetBaseFromFloor(CurrentFloor);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + MAX_FLOOR_DIST * -GetGravityDirection();
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			// check if just entered water
			if (IsSwimming())
			{
				RestorePreAdditiveVRMotionVelocity();
				StartSwimmingVR(OldCapsuleLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling.
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump))
				{
					RestorePreAdditiveVRMotionVelocity();
					return;
				}
				bCheckedFall = true;
			}

			if (bAutoOrientToFloorNormal && CurrentFloor.IsWalkableFloor())
			{
				// Auto Align to the new floor normal
				// Set gravity direction to the new floor normal
				AutoTraceAndSetCharacterToNewGravity(CurrentFloor.HitResult, timeTick);
			}
		}


		// Allow overlap events and such to change physics state and velocity
		if (IsMovingOnGround())
		{
			// Make velocity reflect actual move
			
			if (!bJustTeleported && timeTick >= MIN_TICK_TIME)
			{
				if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
				{
					// TODO-RootMotionSource: Allow this to happen during partial override Velocity, but only set allowed axes?
					Velocity = ((UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick);
					MaintainHorizontalGroundVelocity();
				}

				RestorePreAdditiveVRMotionVelocity();
			}
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			RestorePreAdditiveVRMotionVelocity();
			remainingTime = 0.f;
			break;
		}
	}

	if (IsMovingOnGround())
	{
		MaintainHorizontalGroundVelocity();
	}
}

void UVRCharacterMovementComponent::CapsuleTouched(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bEnablePhysicsInteraction)
	{
		return;
	}

	if (OtherComp != NULL && OtherComp->IsAnySimulatingPhysics())
	{
		/*const*/FVector OtherLoc = OtherComp->GetComponentLocation();
		if (UVRRootComponent * rCap = Cast<UVRRootComponent>(OtherComp))
		{
			OtherLoc = rCap->OffsetComponentToWorld.GetLocation();
		}
		
		const FVector Loc = VRRootCapsule->OffsetComponentToWorld.GetLocation();//UpdatedComponent->GetComponentLocation();

		FVector ImpulseDir = OtherLoc - Loc;
		SetGravitySpaceZ(ImpulseDir, 0.25f);
		ImpulseDir = (ImpulseDir.GetSafeNormal() + ProjectToGravityFloor(Velocity).GetSafeNormal()) * 0.5f;
		ImpulseDir.Normalize();

		FName BoneName = NAME_None;
		if (OtherBodyIndex != INDEX_NONE)
		{
			BoneName = ((USkinnedMeshComponent*)OtherComp)->GetBoneName(OtherBodyIndex);
		}

		float TouchForceFactorModified = TouchForceFactor;

		if (bTouchForceScaledToMass)
		{
			FBodyInstance* BI = OtherComp->GetBodyInstance(BoneName);
			TouchForceFactorModified *= BI ? BI->GetBodyMass() : 1.0f;
		}

		float ImpulseStrength = FMath::Clamp<FVector::FReal>(ProjectToGravityFloor(Velocity).Size() * TouchForceFactorModified,
			MinTouchForce > 0.0f ? MinTouchForce : -FLT_MAX,
			MaxTouchForce > 0.0f ? MaxTouchForce : FLT_MAX);

		FVector Impulse = ImpulseDir * ImpulseStrength;

		OtherComp->AddImpulse(Impulse, BoneName);
	}
}

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
	FSavedMovePtr NewMovePtr = ClientData->CreateSavedMove();
	FSavedMove_Character* const NewMove = NewMovePtr.Get();
	if (NewMove == nullptr)
	{
		return;
	}

	NewMove->SetMoveFor(CharacterOwner, DeltaTime, NewAcceleration, *ClientData);
	const UWorld* MyWorld = GetWorld();
	// Causing really bad crash when using vr offset location, rather remove for now than have it merge move improperly.

	// see if the two moves could be combined
	// do not combine moves which have different TimeStamps (before and after reset).
	if (const FSavedMove_Character* PendingMove = ClientData->PendingMove.Get())
	{
		if (bAllowMovementMerging && PendingMove->CanCombineWith(NewMovePtr, CharacterOwner, ClientData->MaxMoveDeltaTime * CharacterOwner->GetActorTimeDilation(*MyWorld)))
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_VRCharacterMovementComponent_CombineNetMove);
			//SCOPE_CYCLE_COUNTER(STAT_CharacterMovementCombineNetMove);

			// Only combine and move back to the start location if we don't move back in to a spot that would make us collide with something new.
			/*const */FVector OldStartLocation = PendingMove->GetRevertedLocation();

			// Modifying the location to account for capsule loc
			FVector OverlapLocation = OldStartLocation;
			if (VRRootCapsule)
				OverlapLocation += VRRootCapsule->OffsetComponentToWorld.GetLocation() - VRRootCapsule->GetComponentLocation();

			const bool bAttachedToObject = (NewMovePtr->StartAttachParent != nullptr);
			if (bAttachedToObject || !OverlapTest(OverlapLocation, PendingMove->StartRotation.Quaternion(), UpdatedComponent->GetCollisionObjectType(), GetPawnCapsuleCollisionShape(SHRINK_None), CharacterOwner))
			{
				// Avoid updating Mesh bones to physics during the teleport back, since PerformMovement() will update it right away anyway below.
				// Note: this must be before the FScopedMovementUpdate below, since that scope is what actually moves the character and mesh.
				//AVRBaseCharacter * BaseCharacter = Cast<AVRBaseCharacter>(CharacterOwner);		

				FScopedMeshBoneUpdateOverrideVR ScopedNoMeshBoneUpdate(CharacterOwner->GetMesh(), EKinematicBonesUpdateToPhysics::SkipAllBones);

				// Accumulate multiple transform updates until scope ends.
				FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, EScopedUpdate::DeferredUpdates);
				UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("CombineMove: add delta %f + %f and revert from %f %f to %f %f"), DeltaTime, ClientData->PendingMove->DeltaTime, UpdatedComponent->GetComponentLocation().X, UpdatedComponent->GetComponentLocation().Y, /*OldStartLocation.X*/OverlapLocation.X, /*OldStartLocation.Y*/OverlapLocation.Y);

				NewMove->CombineWith(PendingMove, CharacterOwner, PC, OldStartLocation);

				// Update the input vector to the new values!! We were always forgetting to do this
				FSavedMove_VRBaseCharacter * BaseCharMove = ((FSavedMove_VRBaseCharacter*)NewMove);
				AdditionalVRInputVector = BaseCharMove->LFDiff;// FVector(BaseCharMove->LFDiff.X, BaseCharMove->LFDiff.Y, 0.0f);

				/************************/
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
					ClientData->SavedMoves.Pop(EAllowShrinking::No);
				}
				ClientData->FreeMove(ClientData->PendingMove);
				ClientData->PendingMove = nullptr;
				PendingMove = nullptr; // Avoid dangling reference, it's deleted above.
			}
			else
			{
				UE_LOG(LogVRCharacterMovement, Verbose, TEXT("Not combining move [would collide at start location]"));
			}
		}
		/*else
		{
			UE_LOG(LogVRCharacterMovement, Verbose, TEXT("Not combining move [not allowed by CanCombineWith()]"));
		}*/
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
	if (CharacterOwner->IsReplicatingMovement())
	{
		check(NewMove == NewMovePtr.Get());
		ClientData->SavedMoves.Push(NewMovePtr);

		static const auto CVarNetEnableMoveCombining = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetEnableMoveCombining"));
		const bool bCanDelayMove = (CVarNetEnableMoveCombining->GetInt() != 0) && CanDelaySendingMove(NewMovePtr);

		if (bCanDelayMove && ClientData->PendingMove.IsValid() == false)
		{
			// Decide whether to hold off on move	
			const float NetMoveDeltaSeconds = FMath::Clamp(GetClientNetSendDeltaTime(PC, ClientData, NewMovePtr), 1.f / 120.f, 1.f / 5.f);
			const float SecondsSinceLastMoveSent = MyWorld->GetRealTimeSeconds() - ClientData->ClientUpdateRealTime;

			if (SecondsSinceLastMoveSent < NetMoveDeltaSeconds)
			{
				// Delay sending this move.
				ClientData->PendingMove = NewMovePtr;
				return;
			}
		}
		
		ClientData->ClientUpdateRealTime = MyWorld->GetRealTimeSeconds();

		UE_CLOG(CharacterOwner&& UpdatedComponent, LogVRCharacterMovement, VeryVerbose, TEXT("ClientMove Time %f Acceleration %s Velocity %s Position %s Rotation %s DeltaTime %f Mode %s MovementBase %s.%s (Dynamic:%d) DualMove? %d"),
			NewMove->TimeStamp, *NewMove->Acceleration.ToString(), *Velocity.ToString(), *UpdatedComponent->GetComponentLocation().ToString(), *UpdatedComponent->GetComponentRotation().ToCompactString(), NewMove->DeltaTime, *GetMovementName(),
			*GetNameSafe(NewMove->EndBase.Get()), *NewMove->EndBoneName.ToString(), MovementBaseUtility::IsDynamicBase(NewMove->EndBase.Get()) ? 1 : 0, ClientData->PendingMove.IsValid() ? 1 : 0);

		bool bSendServerMove = true;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

		// Testing options: Simulated packet loss to server
		const float TimeSinceLossStart = (MyWorld->RealTimeSeconds - ClientData->DebugForcedPacketLossTimerStart);

		static const auto CVarNetForceClientServerMoveLossDuration = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetForceClientServerMoveLossDuration"));
		static const auto CVarNetForceClientServerMoveLossPercent = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetForceClientServerMoveLossPercent"));
		if (ClientData->DebugForcedPacketLossTimerStart > 0.f && (TimeSinceLossStart < CVarNetForceClientServerMoveLossDuration->GetFloat()))
		{
			bSendServerMove = false;
			UE_LOG(LogVRCharacterMovement, Log, TEXT("Drop ServerMove, %.2f time remains"), CVarNetForceClientServerMoveLossDuration->GetFloat() - TimeSinceLossStart);
		}
		else if (CVarNetForceClientServerMoveLossPercent->GetFloat() != 0.f && (RandomStream.FRand() < CVarNetForceClientServerMoveLossPercent->GetFloat()))
		{
			bSendServerMove = false;
			ClientData->DebugForcedPacketLossTimerStart = (CVarNetForceClientServerMoveLossDuration->GetFloat() > 0) ? MyWorld->RealTimeSeconds : 0.0f;
			UE_LOG(LogVRCharacterMovement, Log, TEXT("Drop ServerMove, %.2f time remains"), CVarNetForceClientServerMoveLossDuration->GetFloat());
		}
		else
		{
			ClientData->DebugForcedPacketLossTimerStart = 0.f;
		}
#endif

		// Send move to server if this character is replicating movement
		if (bSendServerMove)
		{
			SCOPE_CYCLE_COUNTER(STAT_CharacterMovementCallServerMove);
			if (ShouldUsePackedMovementRPCs())
			{
				CallServerMovePacked(NewMove, ClientData->PendingMove.Get(), OldMove.Get());
			}
			/*else
			{
				CallServerMove(NewMove, OldMove.Get());
			}*/
		}
	}

	ClientData->PendingMove = NULL;
}

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
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	VRRootCapsule = NULL;
	//VRCameraCollider = NULL;
	// 0.1f is low slide and still impacts surfaces well
	// This variable is a bit of a hack, it reduces the movement of the pawn in the direction of relative movement
	//WallRepulsionMultiplier = 0.01f;
	bUseClientControlRotation = false;
	bAllowMovementMerging = true;
	bRunClientCorrectionToHMD = false;
	bRequestedMoveUseAcceleration = false;
}

void UVRCharacterMovementComponent::OnRegister()
{
	Super::OnRegister();

	//#TODO: double check that this works, they enforce linear usually
	// Force exponential smoothing for replays.
	const UWorld* MyWorld = GetWorld();
	const bool bIsReplay = (MyWorld && MyWorld->IsPlayingReplay());
	
	if (bIsReplay)
	{
		//NetworkSmoothingMode = ENetworkSmoothingMode::Linear;
		NetworkSmoothingMode = ENetworkSmoothingMode::Exponential;
	}
}


void UVRCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (!HasValidData())
	{
		return;
	}

	if (CharacterOwner && CharacterOwner->IsLocallyControlled())
	{
		// Root capsule is now throwing out the difference itself, I use the difference for multiplayer sends
		if (VRRootCapsule)
		{
			AdditionalVRInputVector = VRRootCapsule->DifferenceFromLastFrame;
		}
		else
		{
			AdditionalVRInputVector = FVector::ZeroVector;
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

// No support for crouching code yet
bool UVRCharacterMovementComponent::CanCrouch()
{
	return false;
}


void UVRCharacterMovementComponent::ApplyRepulsionForce(float DeltaSeconds)
{
	if (UpdatedPrimitive && RepulsionForce > 0.0f && CharacterOwner != nullptr)
	{
		const TArray<FOverlapInfo>& Overlaps = UpdatedPrimitive->GetOverlapInfos();
		if (Overlaps.Num() > 0)
		{
			FCollisionQueryParams QueryParams;
			QueryParams.bReturnFaceIndex = false;
			QueryParams.bReturnPhysicalMaterial = false;

			float CapsuleRadius = 0.f;
			float CapsuleHalfHeight = 0.f;
			CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
			const float RepulsionForceRadius = CapsuleRadius * 1.2f;
			const float StopBodyDistance = 2.5f;
			FVector MyLocation;
			
			if (VRRootCapsule)
				MyLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();
			else
				MyLocation = UpdatedPrimitive->GetComponentLocation();

			for (int32 i = 0; i < Overlaps.Num(); i++)
			{
				const FOverlapInfo& Overlap = Overlaps[i];

				UPrimitiveComponent* OverlapComp = Overlap.OverlapInfo.Component.Get();
				if (!OverlapComp || OverlapComp->Mobility < EComponentMobility::Movable)
				{
					continue;
				}

				// Use the body instead of the component for cases where we have multi-body overlaps enabled
				FBodyInstance* OverlapBody = nullptr;
				const int32 OverlapBodyIndex = Overlap.GetBodyIndex();
				const USkeletalMeshComponent* SkelMeshForBody = (OverlapBodyIndex != INDEX_NONE) ? Cast<USkeletalMeshComponent>(OverlapComp) : nullptr;
				if (SkelMeshForBody != nullptr)
				{
					OverlapBody = SkelMeshForBody->Bodies.IsValidIndex(OverlapBodyIndex) ? SkelMeshForBody->Bodies[OverlapBodyIndex] : nullptr;
				}
				else
				{
					OverlapBody = OverlapComp->GetBodyInstance();
				}

				if (!OverlapBody)
				{
					UE_LOG(LogVRCharacterMovement, Warning, TEXT("%s could not find overlap body for body index %d"), *GetName(), OverlapBodyIndex);
					continue;
				}

				if (!OverlapBody->IsInstanceSimulatingPhysics())
				{
					continue;
				}

				FTransform BodyTransform = OverlapBody->GetUnrealWorldTransform();

				FVector BodyVelocity = OverlapBody->GetUnrealWorldVelocity();
				FVector BodyLocation = BodyTransform.GetLocation();

				// Trace to get the hit location on the capsule
				FHitResult Hit;
				bool bHasHit = UpdatedPrimitive->LineTraceComponent(Hit, BodyLocation,
					ProjectToGravityFloor(MyLocation) + GetGravitySpaceComponentZ(BodyLocation),
					QueryParams);

				FVector HitLoc = Hit.ImpactPoint;
				bool bIsPenetrating = Hit.bStartPenetrating || Hit.PenetrationDepth > StopBodyDistance;

				// If we didn't hit the capsule, we're inside the capsule
				if (!bHasHit)
				{
					HitLoc = BodyLocation;
					bIsPenetrating = true;
				}

				const float DistanceNow = ProjectToGravityFloor(HitLoc - BodyLocation).SizeSquared();
				const float DistanceLater = ProjectToGravityFloor(HitLoc - (BodyLocation + BodyVelocity * DeltaSeconds)).SizeSquared();

				if (bHasHit && DistanceNow < StopBodyDistance && !bIsPenetrating)
				{
					OverlapBody->SetLinearVelocity(FVector::ZeroVector, false);
				}
				else if (DistanceLater <= DistanceNow || bIsPenetrating)
				{
					FVector ForceCenter = MyLocation;

					if (bHasHit)
					{
						SetGravitySpaceZ(ForceCenter, GetGravitySpaceZ(HitLoc));
					}
					else
					{
						const FVector::FReal MyLocationZ = GetGravitySpaceZ(MyLocation);
						SetGravitySpaceZ(ForceCenter, FMath::Clamp(GetGravitySpaceZ(BodyLocation), MyLocationZ - CapsuleHalfHeight, MyLocationZ + CapsuleHalfHeight));
					}

					OverlapBody->AddRadialForceToBody(ForceCenter, RepulsionForceRadius, RepulsionForce * Mass, ERadialImpulseFalloff::RIF_Constant);
				}
			}
		}
	}
}


void UVRCharacterMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	Super::SetUpdatedComponent(NewUpdatedComponent);

	if (UpdatedComponent)
	{	
		// Fill the VRRootCapsule if we can
		VRRootCapsule = Cast<UVRRootComponent>(UpdatedComponent);

		// Fill in the camera collider if we can
		/*if (AVRCharacter * vrOwner = Cast<AVRCharacter>(this->GetOwner()))
		{
			VRCameraCollider = vrOwner->VRCameraCollider;
		}*/

		// Stop the tick forcing
		UpdatedComponent->PrimaryComponentTick.RemovePrerequisite(this, PrimaryComponentTick);

		// Start forcing the root to tick before this, the actor tick will still tick after the movement component
		// We want the root component to tick first because it is setting its offset location based off of tick
		this->PrimaryComponentTick.AddPrerequisite(UpdatedComponent, UpdatedComponent->PrimaryComponentTick);
	}
}

FORCEINLINE_DEBUGGABLE bool UVRCharacterMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport)
{
	return SafeMoveUpdatedComponent(Delta, NewRotation.Quaternion(), bSweep, OutHit, Teleport);
}

bool UVRCharacterMovementComponent::SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport)
{
	if (UpdatedComponent == NULL)
	{
		OutHit.Reset(1.f);
		return false;
	}

	bool bMoveResult = MoveUpdatedComponent(Delta, NewRotation, bSweep, &OutHit, Teleport);

	// Handle initial penetrations
	if (OutHit.bStartPenetrating && UpdatedComponent)
	{
		const FVector RequestedAdjustment = GetPenetrationAdjustment(OutHit);
		if (ResolvePenetration(RequestedAdjustment, OutHit, NewRotation))
		{
			FHitResult TempHit;
			// Retry original move
			bMoveResult = MoveUpdatedComponent(Delta, NewRotation, bSweep, &TempHit, Teleport);

			// Remove start penetrating if this is a clean move, otherwise use the last moves hit as the result so step up actually attempts to work.
			if (TempHit.bStartPenetrating)
				OutHit = TempHit;
			else
				OutHit.bStartPenetrating = TempHit.bStartPenetrating;
		}
	}

	return bMoveResult;
}

void UVRCharacterMovementComponent::MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult)
{
	if (!CurrentFloor.IsWalkableFloor())
	{
		return;
	}

	// Move along the current floor
	const FVector Delta = ProjectToGravityFloor(InVelocity) * DeltaSeconds;
	FHitResult Hit(1.f);
	FVector RampVector = ComputeGroundMovementDelta(Delta, CurrentFloor.HitResult, CurrentFloor.bLineTrace);
	SafeMoveUpdatedComponent(RampVector, UpdatedComponent->GetComponentQuat(), true, Hit);
	float LastMoveTimeSlice = DeltaSeconds;

	if (Hit.bStartPenetrating)
	{
		// Allow this hit to be used as an impact we can deflect off, otherwise we do nothing the rest of the update and appear to hitch.
		HandleImpact(Hit);
		SlideAlongSurface(Delta, 1.f, Hit.Normal, Hit, true);

		if (Hit.bStartPenetrating)
		{
			OnCharacterStuckInGeometry(&Hit);
		}
	}
	else if (Hit.IsValidBlockingHit())
	{
		// We impacted something (most likely another ramp, but possibly a barrier).
		float PercentTimeApplied = Hit.Time;
		if ((Hit.Time > 0.f) && (GetGravitySpaceZ(Hit.Normal) > UE_KINDA_SMALL_NUMBER) && IsWalkable(Hit))
		{
			// Another walkable ramp.
			const float InitialPercentRemaining = 1.f - PercentTimeApplied;
			RampVector = ComputeGroundMovementDelta(Delta * InitialPercentRemaining, Hit, false);
			LastMoveTimeSlice = InitialPercentRemaining * LastMoveTimeSlice;
			SafeMoveUpdatedComponent(RampVector, UpdatedComponent->GetComponentQuat(), true, Hit);

			const float SecondHitPercent = Hit.Time * InitialPercentRemaining;
			PercentTimeApplied = FMath::Clamp(PercentTimeApplied + SecondHitPercent, 0.f, 1.f);
		}

		if (Hit.IsValidBlockingHit())
		{
			if (CanStepUp(Hit) || (CharacterOwner->GetMovementBase() != nullptr && Hit.HitObjectHandle == CharacterOwner->GetMovementBase()->GetOwner()))
			{
				// hit a barrier, try to step up
				const FVector PreStepUpLocation = UpdatedComponent->GetComponentLocation();
				const FVector GravDir = GetGravityDirection();

				if (!StepUp(GetGravityDirection(), Delta * (1.f - PercentTimeApplied), Hit, OutStepDownResult))
				{
					UE_LOG(LogVRCharacterMovement, Verbose, TEXT("- StepUp (ImpactNormal %s, Normal %s"), *Hit.ImpactNormal.ToString(), *Hit.Normal.ToString());
					HandleImpact(Hit, LastMoveTimeSlice, RampVector);
					SlideAlongSurface(Delta, 1.f - PercentTimeApplied, Hit.Normal, Hit, true);

				}
				else
				{
					UE_LOG(LogVRCharacterMovement, Verbose, TEXT("+ StepUp (ImpactNormal %s, Normal %s"), *Hit.ImpactNormal.ToString(), *Hit.Normal.ToString());
					if (!bMaintainHorizontalGroundVelocity)
					{
						// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments. Only consider horizontal movement.
						bJustTeleported = true;
						const float StepUpTimeSlice = (1.f - PercentTimeApplied) * DeltaSeconds;
						if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && StepUpTimeSlice >= UE_KINDA_SMALL_NUMBER)
						{
							Velocity = (UpdatedComponent->GetComponentLocation() - PreStepUpLocation) / StepUpTimeSlice;
							Velocity = ProjectToGravityFloor(Velocity);
						}
					}
				}
			}
			else if (Hit.Component.IsValid() && !Hit.Component.Get()->CanCharacterStepUp(CharacterOwner))
			{
				HandleImpact(Hit, LastMoveTimeSlice, RampVector);
				SlideAlongSurface(Delta, 1.f - PercentTimeApplied, Hit.Normal, Hit, true);

			}
		}
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
		OldLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();
	else
		OldLocation = UpdatedComponent->GetComponentLocation();

	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	// Don't bother stepping up if top of capsule is hitting something.
	const float InitialImpactZ = InHit.ImpactPoint | -GravDir;
	const float OldLocationZ = OldLocation | -GravDir;
	if (InitialImpactZ > OldLocationZ + (PawnHalfHeight - PawnRadius))
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
	const float StepSideZ = -1.f * FVector::DotProduct(InHit.ImpactNormal, GravDir);//const float StepSideZ = -1.f * (InHit.ImpactNormal | GravDir);
	float PawnInitialFloorBaseZ = OldLocationZ - PawnHalfHeight;
	float PawnFloorPointZ = PawnInitialFloorBaseZ;

	if (IsMovingOnGround() && CurrentFloor.IsWalkableFloor())
	{
		// Since we float a variable amount off the floor, we need to enforce max step height off the actual point of impact with the floor.
		const float FloorDist = FMath::Max(0.f, CurrentFloor.GetDistanceToFloor());
		PawnInitialFloorBaseZ -= FloorDist;
		StepTravelUpHeight = FMath::Max(StepTravelUpHeight - FloorDist, 0.f);
		StepTravelDownHeight = (MaxStepHeight + MAX_FLOOR_DIST*2.f);

		const bool bHitVerticalFace = !IsWithinEdgeTolerance(InHit.Location, InHit.ImpactPoint, PawnRadius);
		if (!CurrentFloor.bLineTrace && !bHitVerticalFace)
		{
			PawnFloorPointZ = CurrentFloor.HitResult.ImpactPoint | -GravDir;
		}
		else
		{
			// Base floor point is the base of the capsule moved down by how far we are hovering over the surface we are hitting.
			PawnFloorPointZ -= CurrentFloor.FloorDist;
		}
	}

	// Don't step up if the impact is below us, accounting for distance from floor.
	if (InitialImpactZ <= PawnInitialFloorBaseZ)
	{
		return false;
	}

	// Scope our movement updates, and do not apply them until all intermediate moves are completed.
	/*FScopedMovementUpdate*/ FVRCharacterScopedMovementUpdate ScopedStepUpMovement(UpdatedComponent, EScopedUpdate::DeferredUpdates);

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
		const float DeltaZ = (Hit.ImpactPoint | -GravDir) - PawnFloorPointZ;
		if (DeltaZ > MaxStepHeight)
		{
			//UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (too high Height %.3f) up from floor base %f"), DeltaZ, PawnInitialFloorBaseZ);
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
				//UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s opposed to movement)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}

			// Also reject if we would end up being higher than our starting location by stepping down.
			// It's fine to step down onto an unwalkable normal below us, we will just slide off. Rejecting those moves would prevent us from being able to walk off the edge.
			if ((Hit.Location | -GravDir) > OldLocationZ)
			{
				//UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s above old position)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}
		}

		// Reject moves where the downward sweep hit something very close to the edge of the capsule. This maintains consistency with FindFloor as well.
		if (!IsWithinEdgeTolerance(Hit.Location, Hit.ImpactPoint, PawnRadius))
		{
			//UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (outside edge tolerance)"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Don't step up onto invalid surfaces if traveling higher.
		if (DeltaZ > 0.f && !CanStepUp(Hit))
		{
			//UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (up onto surface with !CanStepUp())"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// See if we can validate the floor as a result of this step down. In almost all cases this should succeed, and we can avoid computing the floor outside this method.
		if (OutStepDownResult != NULL)
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), StepDownResult.FloorResult, false, &Hit);

			// Reject unwalkable normals if we end up higher than our initial height.
			// It's fine to walk down onto an unwalkable surface, don't reject those moves.
			if ((Hit.Location | -GravDir) > OldLocationZ)
			{
				// We should reject the floor result if we are trying to step up an actual step where we are not able to perch (this is rare).
				// In those cases we should instead abort the step up and try to slide along the stair.
				if (!StepDownResult.FloorResult.bBlockingHit && StepSideZ < CharacterMovementConstants::MAX_STEP_SIDE_ZVR)
				{
					ScopedStepUpMovement.RevertMove();
					return false;
				}
			}

			StepDownResult.bComputedFloor = true;

			if (bAutoOrientToFloorNormal && StepDownResult.FloorResult.IsWalkableFloor())
			{
				// Auto Align to the new floor normal
				// Set gravity direction to the new floor normal, don't blend the change, snap to it on a step up
				AutoTraceAndSetCharacterToNewGravity(StepDownResult.FloorResult.HitResult);
			}
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

bool UVRCharacterMovementComponent::IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const
{
	const FVector GravityRelativeToTestImpactPoint = RotateWorldToGravity(TestImpactPoint - CapsuleLocation);
	const float DistFromCenterSq = GravityRelativeToTestImpactPoint.SizeSquared2D();
	const float ReducedRadiusSq = FMath::Square(FMath::Max(VREdgeRejectDistance + UE_KINDA_SMALL_NUMBER, CapsuleRadius - VREdgeRejectDistance));
	return DistFromCenterSq < ReducedRadiusSq;
}

bool UVRCharacterMovementComponent::IsWithinClimbingEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const
{
	const float DistFromCenterSq = (TestImpactPoint - CapsuleLocation).SizeSquared2D();
	const float ReducedRadiusSq = FMath::Square(FMath::Max(VRClimbingEdgeRejectDistance + UE_KINDA_SMALL_NUMBER, CapsuleRadius - VRClimbingEdgeRejectDistance));
	return DistFromCenterSq < ReducedRadiusSq;
}

bool UVRCharacterMovementComponent::VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult)
{
	SCOPE_CYCLE_COUNTER(STAT_CharStepUp);

	if (!CanStepUp(InHit) || MaxStepHeight <= 0.f)
	{
		return false;
	}

	FVector OldLocation;

	if (VRRootCapsule)
		OldLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();
	else
		OldLocation = UpdatedComponent->GetComponentLocation();

	float PawnRadius, PawnHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	// Don't bother stepping up if top of capsule is hitting something.
	const float InitialImpactZ = InHit.ImpactPoint | -GravDir;
	const float OldLocationZ = OldLocation | -GravDir;
	if (InitialImpactZ > OldLocationZ + (PawnHalfHeight - PawnRadius))
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
	float PawnInitialFloorBaseZ = OldLocationZ - PawnHalfHeight;
	float PawnFloorPointZ = PawnInitialFloorBaseZ;

	// Scope our movement updates, and do not apply them until all intermediate moves are completed.
	FVRCharacterScopedMovementUpdate ScopedStepUpMovement(UpdatedComponent, EScopedUpdate::DeferredUpdates);

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


	// Adding in the directional difference of the last HMD movement to promote stepping up
	// Probably entirely wrong as Delta is divided by movement ticks but I want the effect to be stronger anyway
	// This won't effect control based movement unless stepping forward at the same time, but gives RW movement
	// the extra boost to get up over a lip
	// #TODO test this more, currently appears to be needed for walking, but is harmful for other modes
//	if (VRRootCapsule)
//		MoveUpdatedComponent(Delta + VRRootCapsule->DifferenceFromLastFrame.GetSafeNormal2D(), PawnRotation, true, &Hit);
//	else
		MoveUpdatedComponent(Delta, PawnRotation, true, &Hit);

	//MoveUpdatedComponent(Delta, PawnRotation, true, &Hit);

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

		//Don't adjust for VR, it doesn't work correctly
		ScopedStepUpMovement.RevertMove();
		return false;

		// adjust and try again
		//const float ForwardHitTime = Hit.Time;
		//const float ForwardSlideAmount = SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);

		//if (IsFalling())
		//{
		//	ScopedStepUpMovement.RevertMove();
		//	return false;
		//}

		// If both the forward hit and the deflection got us nowhere, there is no point in this step up.
		//if (ForwardHitTime == 0.f && ForwardSlideAmount == 0.f)
		//{
		//	ScopedStepUpMovement.RevertMove();
		//	return false;
		//}
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
		const float DeltaZ = (Hit.ImpactPoint | -GravDir) - PawnFloorPointZ;
		if (DeltaZ > MaxStepHeight)
		{
			UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (too high Height %.3f) up from floor base %f"), DeltaZ, PawnInitialFloorBaseZ);
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
				//UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s opposed to movement)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}

			// Also reject if we would end up being higher than our starting location by stepping down.
			// It's fine to step down onto an unwalkable normal below us, we will just slide off. Rejecting those moves would prevent us from being able to walk off the edge.
			if ((Hit.Location | -GravDir) > OldLocationZ)
			{
				UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (unwalkable normal %s above old position)"), *Hit.ImpactNormal.ToString());
				ScopedStepUpMovement.RevertMove();
				return false;
			}
		}

		// Reject moves where the downward sweep hit something very close to the edge of the capsule. This maintains consistency with FindFloor as well.
		if (!IsWithinClimbingEdgeTolerance(Hit.Location, Hit.ImpactPoint, PawnRadius))
		{
			UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (outside edge tolerance)"));
			ScopedStepUpMovement.RevertMove();
			return false;
		}

		// Don't step up onto invalid surfaces if traveling higher.
		if (DeltaZ > 0.f && !CanStepUp(Hit))
		{
			UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("- Reject StepUp (up onto surface with !CanStepUp())"));
			ScopedStepUpMovement.RevertMove();
			return false;		
		}

		// See if we can validate the floor as a result of this step down. In almost all cases this should succeed, and we can avoid computing the floor outside this method.
		if (OutStepDownResult != NULL)
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), StepDownResult.FloorResult, false, &Hit);

			// Reject unwalkable normals if we end up higher than our initial height.
			// It's fine to walk down onto an unwalkable surface, don't reject those moves.
			if ((Hit.Location | -GravDir) > OldLocationZ)
			{
				// We should reject the floor result if we are trying to step up an actual step where we are not able to perch (this is rare).
				// In those cases we should instead abort the step up and try to slide along the stair.
				if (!StepDownResult.FloorResult.bBlockingHit && StepSideZ < CharacterMovementConstants::MAX_STEP_SIDE_ZVR)
				{
					ScopedStepUpMovement.RevertMove();
					return false;
				}
			}

			StepDownResult.bComputedFloor = true;

			if (bAutoOrientToFloorNormal && StepDownResult.FloorResult.IsWalkableFloor())
			{
				// Auto Align to the new floor normal
				// Set gravity direction to the new floor normal, don't blend the change, snap to it on a step down
				AutoTraceAndSetCharacterToNewGravity(StepDownResult.FloorResult.HitResult);
			}
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

FVector UVRCharacterMovementComponent::GetActorFeetLocation() const
{
	// Call into the VR version of it instead
	return GetActorFeetLocationVR();
}


void UVRCharacterMovementComponent::UpdateBasedMovement(float DeltaSeconds)
{
	if (!HasValidData())
	{
		return;
	}

	const UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
	if (!MovementBaseUtility::UseRelativeLocation(MovementBase))
	{
		return;
	}

	if (!IsValid(MovementBase) || !IsValid(MovementBase->GetOwner()))
	{
		SetBase(NULL);
		return;
	}

	// Ignore collision with bases during these movements.
	TGuardValue<EMoveComponentFlags> ScopedFlagRestore(MoveComponentFlags, MoveComponentFlags | MOVECOMP_IgnoreBases);

	FQuat DeltaQuat = FQuat::Identity;
	FVector DeltaPosition = FVector::ZeroVector;

	FQuat NewBaseQuat;
	FVector NewBaseLocation;
	if (!MovementBaseUtility::GetMovementBaseTransform(MovementBase, CharacterOwner->GetBasedMovement().BoneName, NewBaseLocation, NewBaseQuat))
	{
		return;
	}

	// Find change in rotation
	const bool bRotationChanged = !OldBaseQuat.Equals(NewBaseQuat, 1e-8f);
	if (bRotationChanged)
	{
		DeltaQuat = NewBaseQuat * OldBaseQuat.Inverse();
	}

	// only if base moved
	if (bRotationChanged || (OldBaseLocation != NewBaseLocation))
	{
		// Calculate new transform matrix of base actor (ignoring scale).
		const FQuatRotationTranslationMatrix OldLocalToWorld(OldBaseQuat, OldBaseLocation);
		const FQuatRotationTranslationMatrix NewLocalToWorld(NewBaseQuat, NewBaseLocation);

		FQuat FinalQuat = UpdatedComponent->GetComponentQuat();

		if (bRotationChanged && !bIgnoreBaseRotation)
		{
			// Apply change in rotation and pipe through FaceRotation to maintain axis restrictions
			const FQuat PawnOldQuat = UpdatedComponent->GetComponentQuat();
			const FQuat TargetQuat = DeltaQuat * FinalQuat;
			FRotator TargetRotator(TargetQuat);
			CharacterOwner->FaceRotation(TargetRotator, 0.f);
			FinalQuat = UpdatedComponent->GetComponentQuat();

			if (PawnOldQuat.Equals(FinalQuat, 1e-6f))
			{
				// Nothing changed. This means we probably are using another rotation mechanism (bOrientToMovement etc). We should still follow the base object.
				// @todo: This assumes only Yaw is used, currently a valid assumption. This is the only reason FaceRotation() is used above really, aside from being a virtual hook.
				if (bOrientRotationToMovement || (bUseControllerDesiredRotation && CharacterOwner->GetController()))
				{
					// Custom gravity automatically aligns the character to the gravity direction, so we shouldn't zero out pitch and roll.
					if (!HasCustomGravity())
					{
						TargetRotator.Pitch = 0.f;
						TargetRotator.Roll = 0.f;
					}
					MoveUpdatedComponent(FVector::ZeroVector, TargetRotator, false);
					FinalQuat = UpdatedComponent->GetComponentQuat();
				}
			}

			// Pipe through ControlRotation, to affect camera.
			if (CharacterOwner->GetController())
			{
				const FQuat PawnDeltaRotation = FinalQuat * PawnOldQuat.Inverse();
				FRotator FinalRotation = FinalQuat.Rotator();
				UpdateBasedRotation(FinalRotation, PawnDeltaRotation.Rotator());
				FinalQuat = UpdatedComponent->GetComponentQuat();
			}
		}

		FVector NewWorldPos;
		if (HasCustomGravity())
		{
			const FVector RotationRadius = UpdatedComponent->GetComponentLocation() - NewBaseLocation;
			const FVector RotationDelta = DeltaQuat.RotateVector(RotationRadius) - RotationRadius;
			const FVector LinearDelta = NewBaseLocation - OldBaseLocation;
			NewWorldPos = ConstrainLocationToPlane(UpdatedComponent->GetComponentLocation() + RotationDelta + LinearDelta);
		}
		else
		{
			// We need to offset the base of the character here, not its origin, so offset by half height
			float HalfHeight, Radius;
			CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(Radius, HalfHeight);

			if (!BaseVRCharacterOwner || BaseVRCharacterOwner->bRetainRoomscale)
			{
				HalfHeight = 0;
			}

			FVector const BaseOffset(0.0f, 0.0f, HalfHeight);

			FVector const LocalBasePos = OldLocalToWorld.InverseTransformPosition(UpdatedComponent->GetComponentLocation() - BaseOffset);
			NewWorldPos = ConstrainLocationToPlane(NewLocalToWorld.TransformPosition(LocalBasePos) + BaseOffset);
		}
			
		DeltaPosition = ConstrainDirectionToPlane(NewWorldPos - UpdatedComponent->GetComponentLocation());

		// move attached actor
		if (bFastAttachedMove)
		{
			// we're trusting no other obstacle can prevent the move here
			UpdatedComponent->SetWorldLocationAndRotation(NewWorldPos, FinalQuat, false);
		}
		else
		{
			// Set MovementBase's root actor as ignored when moving the character primitive component, 
			// only perform if bDeferUpdateBasedMovement is true since this means the MovementBase is simulating physics
			const bool bIgnoreBaseActor = bDeferUpdateBasedMovement && bBasedMovementIgnorePhysicsBase;
			AActor* MovementBaseRootActor = nullptr;
			if (bIgnoreBaseActor)
			{
				MovementBaseRootActor = MovementBase->GetAttachmentRootActor();
				UpdatedPrimitive->IgnoreActorWhenMoving(MovementBaseRootActor, true);
				MoveComponentFlags |= MOVECOMP_CheckBlockingRootActorInIgnoreList; // Hit actors during MoveUpdatedComponent will have their root actor compared with the ignored actors array 
			}

			// hack - transforms between local and world space introducing slight error FIXMESTEVE - discuss with engine team: just skip the transforms if no rotation?
			FVector BaseMoveDelta = NewBaseLocation - OldBaseLocation;
			if (!bRotationChanged && (BaseMoveDelta.X == 0.f) && (BaseMoveDelta.Y == 0.f))
			{
				DeltaPosition.X = 0.f;
				DeltaPosition.Y = 0.f;
			}

			FHitResult MoveOnBaseHit(1.f);
			const FVector OldLocation = UpdatedComponent->GetComponentLocation();
			MoveUpdatedComponent(DeltaPosition, FinalQuat, true, &MoveOnBaseHit);

			if ((UpdatedComponent->GetComponentLocation() - (OldLocation + DeltaPosition)).IsNearlyZero() == false)
			{
				OnUnableToFollowBaseMove(DeltaPosition, OldLocation, MoveOnBaseHit);
			}

			// Reset base actor ignore state
			if (bIgnoreBaseActor)
			{
				MoveComponentFlags &= ~MOVECOMP_CheckBlockingRootActorInIgnoreList;
				UpdatedPrimitive->IgnoreActorWhenMoving(MovementBaseRootActor, false);
			}
		}

		if (MovementBase->IsSimulatingPhysics() && CharacterOwner->GetMesh())
		{
			CharacterOwner->GetMesh()->ApplyDeltaToAllPhysicsTransforms(DeltaPosition, DeltaQuat);
		}
	}


	// Check if falling above current base
	if (IsFalling() && bStayBasedInAir)
	{
		FVector PawnLocation = UpdatedComponent->GetComponentLocation();
		if (VRRootCapsule)
			PawnLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();

		FFindFloorResult OutFloorResult;
		ComputeFloorDist(PawnLocation, StayBasedInAirHeight, StayBasedInAirHeight, OutFloorResult, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius(), NULL);

		UPrimitiveComponent* HitComponent = OutFloorResult.HitResult.Component.Get();
		if (!HitComponent || HitComponent->GetAttachmentRoot() != MovementBase->GetAttachmentRoot())
		{
			// New or no base under the character
			ApplyImpartedMovementBaseVelocity();
			SetBase(NULL);
			return;
		}
	}
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
				// Base position should be the bottom of the actor since I offset the capsule now
				float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
				if (BaseVRCharacterOwner && BaseVRCharacterOwner->bRetainRoomscale)
				{
					HalfHeight = 0.0f;
				}
				
				const FVector CharacterBasePosition = (UpdatedComponent->GetComponentLocation() + HalfHeight * GetGravityDirection());
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



void UVRCharacterMovementComponent::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult) const
{
	SCOPE_CYCLE_COUNTER(STAT_CharFindFloor);
	//UE_LOG(LogVRCharacterMovement, Warning, TEXT("Find Floor"));
	// No collision, no floor...
	if (!HasValidData() || !UpdatedComponent->IsQueryCollisionEnabled())
	{
		OutFloorResult.Clear();
		return;
	}

	//UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("[Role:%d] FindFloor: %s at location %s"), (int32)CharacterOwner->Role, *GetNameSafe(CharacterOwner), *CapsuleLocation.ToString());
	check(CharacterOwner->GetCapsuleComponent());

	FVector UseCapsuleLocation = CapsuleLocation;
	if (VRRootCapsule)
		UseCapsuleLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();

	// Increase height check slightly if walking, to prevent floor height adjustment from later invalidating the floor result.
	const float HeightCheckAdjust = ((IsMovingOnGround() || IsClimbing()) ? MAX_FLOOR_DIST + UE_KINDA_SMALL_NUMBER : -MAX_FLOOR_DIST);

	float FloorSweepTraceDist = FMath::Max(MAX_FLOOR_DIST, MaxStepHeight + HeightCheckAdjust);
	float FloorLineTraceDist = FloorSweepTraceDist;
	bool bNeedToValidateFloor = true;

	// For reverting
	FFindFloorResult LastFloor = CurrentFloor;

	// Sweep floor
	if (FloorLineTraceDist > 0.f || FloorSweepTraceDist > 0.f)
	{
		UCharacterMovementComponent* MutableThis = const_cast<UCharacterMovementComponent*>((UCharacterMovementComponent*)this);

		if (bAlwaysCheckFloor || !bCanUseCachedLocation || bForceNextFloorCheck || bJustTeleported)
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

			const bool IsActorBasePendingKill = !IsValid(BaseActor);

			if (!bForceNextFloorCheck && !IsActorBasePendingKill && MovementBase)
			{
				//UE_LOG(LogVRCharacterMovement, Log, TEXT("%s SKIP check for floor"), *CharacterOwner->GetName());
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

	// #TODO: Modify the floor compute floor distance instead? Or just run movement logic differently for the bWalkingCollisionOverride setup.
	// #VR Specific - ignore floor traces that are negative, this can be caused by a failed floor check while starting in penetration
	if (VRRootCapsule && VRRootCapsule->bUseWalkingCollisionOverride && OutFloorResult.bBlockingHit && OutFloorResult.FloorDist <= 0.0f)
	{ 

		if (OutFloorResult.FloorDist <= -FMath::Max(MAX_FLOOR_DIST, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleRadius()))
		{
			// This was a negative trace result, the game wants us to pull out of penetration
			// But with walking collision override we don't want to do that, so check for the correct channel and throw away
			// the new floor if it matches
			ECollisionResponse FloorResponse;
			if (OutFloorResult.HitResult.Component.IsValid())
			{
				FloorResponse = OutFloorResult.HitResult.Component->GetCollisionResponseToChannel(VRRootCapsule->WalkingCollisionOverride);
				if (FloorResponse == ECR_Ignore || FloorResponse == ECR_Overlap)
					OutFloorResult = LastFloor;	// Move back to the last floor value, we are in penetration with a walking override
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
			if (IsMovingOnGround() || IsClimbing())
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
					OutFloorResult.SetFromLineTrace(PerchFloorResult.HitResult, OutFloorResult.FloorDist, FMath::Max(OutFloorResult.FloorDist, MIN_FLOOR_DIST), true);
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
				FVector TraceStart;
				FVector TraceEnd;

				if (VRRootCapsule)
				{
					TraceStart = VRRootCapsule->OffsetComponentToWorld.GetLocation() + CollisionHalfHeight * -GetGravityDirection();
					TraceEnd = VRRootCapsule->OffsetComponentToWorld.GetLocation() - CollisionHalfHeight * -GetGravityDirection();
				}
				else
				{
					TraceStart = UpdatedComponent->GetComponentLocation() + CollisionHalfHeight * -GetGravityDirection();
					TraceEnd = UpdatedComponent->GetComponentLocation() - CollisionHalfHeight * -GetGravityDirection();
				}

				FCollisionQueryParams NewTraceParams(CharacterMovementComponentStatics::ImmersionDepthName, true);
				VolumeBrushComp->LineTraceComponent(Hit, TraceStart, TraceEnd, NewTraceParams);
			}

			depth = (Hit.Time == 1.f) ? 1.f : (1.f - Hit.Time);
		}
	}
	return depth;
}

///////////////////////////
// Navigation Functions
///////////////////////////

bool UVRCharacterMovementComponent::TryToLeaveNavWalking()
{
	SetNavWalkingPhysics(false);

	bool bCanTeleport = true;
	if (CharacterOwner)
	{
		FVector CollisionFreeLocation;
		if (VRRootCapsule)
			CollisionFreeLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();
		else
			CollisionFreeLocation =	UpdatedComponent->GetComponentLocation();
		
		// Think I need to create a custom "FindTeleportSpot" function, it is using ComponentToWorld location
		bCanTeleport = GetWorld()->FindTeleportSpot(CharacterOwner, CollisionFreeLocation, UpdatedComponent->GetComponentRotation());
		if (bCanTeleport)
		{
			
			if (VRRootCapsule)
			{
				// Technically the same actor but i am keepign the usage convention for clarity.
				// Subtracting actor location from capsule to get difference in worldspace, then removing from collision free location
				// So that it uses the correct location.
				CharacterOwner->SetActorLocation(CollisionFreeLocation - (VRRootCapsule->OffsetComponentToWorld.GetLocation() - UpdatedComponent->GetComponentLocation()));
			}
			else
				CharacterOwner->SetActorLocation(CollisionFreeLocation);
		}
		else
		{
			SetNavWalkingPhysics(true);
		}
	}

	bWantsToLeaveNavWalking = !bCanTeleport;
	return bCanTeleport;
}

void UVRCharacterMovementComponent::PhysFlying(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Rewind the players position by the new capsule location
	RewindVRRelativeMovement();

	RestorePreAdditiveRootMotionVelocity();
	//RestorePreAdditiveVRMotionVelocity();

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		if (bCheatFlying && Acceleration.IsZero())
		{
			Velocity = FVector::ZeroVector;
		}
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction;
		CalcVelocity(deltaTime, Friction, true, GetMaxBrakingDeceleration());
	}

	ApplyRootMotionToVelocity(deltaTime);
	//ApplyVRMotionToVelocity(deltaTime);

	// Manually handle the velocity setup
	LastPreAdditiveVRVelocity = (AdditionalVRInputVector) / deltaTime;
	bool bExtremeInput = false;
	if (LastPreAdditiveVRVelocity.SizeSquared() > FMath::Square(TrackingLossThreshold))
	{
		// Default to always holding position during flight to avoid too much velocity injection
		AdditionalVRInputVector = FVector::ZeroVector;
		LastPreAdditiveVRVelocity = FVector::ZeroVector;
	}

	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted + AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (Hit.Time < 1.f)
	{
		const FVector VelDir = Velocity.GetSafeNormal();
		const float UpDown = VelDir | GetGravityDirection();


		bool bSteppedUp = false;
		if ((FMath::Abs(GetGravitySpaceZ(Hit.ImpactNormal)) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit))
		{
			const FVector::FReal StepZ = GetGravitySpaceZ(UpdatedComponent->GetComponentLocation());
			bSteppedUp = StepUp(GetGravityDirection(), (Adjusted + AdditionalVRInputVector) * (1.f - Hit.Time), Hit);
			if (bSteppedUp)
			{
				const FVector::FReal LocationZ = GetGravitySpaceZ(UpdatedComponent->GetComponentLocation()) + (GetGravitySpaceZ(OldLocation) - StepZ);
				SetGravitySpaceZ(OldLocation, LocationZ);
			}
		}

		if (!bSteppedUp)
		{
			//adjust and try again
			HandleImpact(Hit, deltaTime, Adjusted);
			SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
		}
	}

	if (!bJustTeleported)
	{
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			//Velocity = ((UpdatedComponent->GetComponentLocation() - OldLocation) - AdditionalVRInputVector) / deltaTime;
			Velocity = ((UpdatedComponent->GetComponentLocation() - OldLocation)) / deltaTime;
		}

		RestorePreAdditiveVRMotionVelocity();
	}
}

void UVRCharacterMovementComponent::PhysFalling(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysFalling);

	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	const FVector FallAcceleration = ProjectToGravityFloor(GetFallingLateralAcceleration(deltaTime));
	const bool bHasLimitedAirControl = ShouldLimitAirControl(deltaTime, FallAcceleration);

	// Rewind the players position by the new capsule location
	RewindVRRelativeMovement();

	float remainingTime = deltaTime;
	while ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations))
	{
		Iterations++;
		float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FVector OldCapsuleLocation = VRRootCapsule ? VRRootCapsule->OffsetComponentToWorld.GetLocation() : OldLocation;

		const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();
		bJustTeleported = false;

		const FVector OldVelocityWithRootMotion = Velocity;

		RestorePreAdditiveRootMotionVelocity();
	//	RestorePreAdditiveVRMotionVelocity();

		const FVector OldVelocity = Velocity;

		// Apply input
		const float MaxDecel = GetMaxBrakingDeceleration();
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			// Compute Velocity
			{
				// Acceleration = FallAcceleration for CalcVelocity(), but we restore it after using it.
				TGuardValue<FVector> RestoreAcceleration(Acceleration, FallAcceleration);
				if (HasCustomGravity())
				{
					Velocity = ProjectToGravityFloor(Velocity);
					const FVector GravityRelativeOffset = OldVelocity - Velocity;
					CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
					Velocity += GravityRelativeOffset;
				}
				else
				{
					Velocity.Z = 0.f;
					CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
					Velocity.Z = OldVelocity.Z;
				}
			}
		}

		//Velocity += CustomVRInputVector / deltaTime;

		// Compute current gravity
		const FVector Gravity = -GetGravityDirection() * GetGravityZ();

		float GravityTime = timeTick;

		// If jump is providing force, gravity may be affected.
		bool bEndingJumpForce = false;
		if (CharacterOwner->JumpForceTimeRemaining > 0.0f)
		{
			// Consume some of the force time. Only the remaining time (if any) is affected by gravity when bApplyGravityWhileJumping=false.
			const float JumpForceTime = FMath::Min(CharacterOwner->JumpForceTimeRemaining, timeTick);
			GravityTime = bApplyGravityWhileJumping ? timeTick : FMath::Max(0.0f, timeTick - JumpForceTime);

			// Update Character state
			CharacterOwner->JumpForceTimeRemaining -= JumpForceTime;
			if (CharacterOwner->JumpForceTimeRemaining <= 0.0f)
			{
				CharacterOwner->ResetJumpState();
				bEndingJumpForce = true;
			}
		}

		// Apply gravity
		Velocity = NewFallVelocity(Velocity, Gravity, GravityTime);

		//UE_LOG(LogCharacterMovement, Log, TEXT("dt=(%.6f) OldLocation=(%s) OldVelocity=(%s) OldVelocityWithRootMotion=(%s) NewVelocity=(%s)"), timeTick, *(UpdatedComponent->GetComponentLocation()).ToString(), *OldVelocity.ToString(), *OldVelocityWithRootMotion.ToString(), *Velocity.ToString());
		ApplyRootMotionToVelocity(timeTick);
		DecayFormerBaseVelocity(timeTick);

		// See if we need to sub-step to exactly reach the apex. This is important for avoiding "cutting off the top" of the trajectory as framerate varies.
		const FVector::FReal GravityRelativeOldVelocityWithRootMotionZ = GetGravitySpaceZ(OldVelocityWithRootMotion);
		static const auto CVarForceJumpPeakSubstep = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ForceJumpPeakSubstep"));
		if (CVarForceJumpPeakSubstep->GetInt() && GravityRelativeOldVelocityWithRootMotionZ > 0.f && GetGravitySpaceZ(Velocity) <= 0.f && NumJumpApexAttempts < MaxJumpApexAttemptsPerSimulation)
		{
			const FVector DerivedAccel = (Velocity - OldVelocityWithRootMotion) / timeTick;
			const FVector::FReal GravityRelativeDerivedAccelZ = GetGravitySpaceZ(DerivedAccel);
			if (!FMath::IsNearlyZero(GravityRelativeDerivedAccelZ))
			{
				const float TimeToApex = -GravityRelativeOldVelocityWithRootMotionZ / GravityRelativeDerivedAccelZ;

				// The time-to-apex calculation should be precise, and we want to avoid adding a substep when we are basically already at the apex from the previous iteration's work.
				const float ApexTimeMinimum = 0.0001f;
				if (TimeToApex >= ApexTimeMinimum && TimeToApex < timeTick)
				{
					const FVector ApexVelocity = OldVelocityWithRootMotion + (DerivedAccel * TimeToApex);
					if (HasCustomGravity())
					{
						Velocity = ProjectToGravityFloor(ApexVelocity); // Should be nearly zero anyway, but this makes apex notifications consistent.
					}
					else
					{
						Velocity = ApexVelocity;
						Velocity.Z = 0.f; // Should be nearly zero anyway, but this makes apex notifications consistent.

					}

					// We only want to move the amount of time it takes to reach the apex, and refund the unused time for next iteration.
					const float TimeToRefund = (timeTick - TimeToApex);

					remainingTime += TimeToRefund;
					timeTick = TimeToApex;
					Iterations--;
					NumJumpApexAttempts++;

					// Refund time to any active Root Motion Sources as well
					for (TSharedPtr<FRootMotionSource> RootMotionSource : CurrentRootMotion.RootMotionSources)
					{
						const float RewoundRMSTime = FMath::Max(0.0f, RootMotionSource->GetTime() - TimeToRefund);
						RootMotionSource->SetTime(RewoundRMSTime);
					}
				}
			}
		}

		//UE_LOG(LogCharacterMovement, Log, TEXT("dt=(%.6f) OldLocation=(%s) OldVelocity=(%s) NewVelocity=(%s)"), timeTick, *(UpdatedComponent->GetComponentLocation()).ToString(), *OldVelocity.ToString(), *Velocity.ToString());

		ApplyRootMotionToVelocity(timeTick);
		//ApplyVRMotionToVelocity(deltaTime);

		if (bNotifyApex && (GetGravitySpaceZ(Velocity) < 0.f))
		{
			// Just passed jump apex since now going down
			bNotifyApex = false;
			NotifyJumpApex();
		}

		// Compute change in position (using midpoint integration method).
		FVector Adjusted = (0.5f * (OldVelocityWithRootMotion + Velocity) * timeTick) + ((AdditionalVRInputVector / deltaTime) * timeTick);

		ApplyVRMotionToVelocity(deltaTime);

		// Special handling if ending the jump force where we didn't apply gravity during the jump.
		if (bEndingJumpForce && !bApplyGravityWhileJumping)
		{
			// We had a portion of the time at constant speed then a portion with acceleration due to gravity.
			// Account for that here with a more correct change in position.
			const float NonGravityTime = FMath::Max(0.f, timeTick - GravityTime);
			Adjusted = ((OldVelocityWithRootMotion * NonGravityTime) + (0.5f * (OldVelocityWithRootMotion + Velocity) * GravityTime)) /*+ ((AdditionalVRInputVector / deltaTime) * timeTick)*/;
		}

		// Move
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent(Adjusted, PawnRotation, true, Hit);

		if (!HasValidData())
		{
			RestorePreAdditiveVRMotionVelocity();
			return;
		}

		float LastMoveTimeSlice = timeTick;
		float subTimeTickRemaining = timeTick * (1.f - Hit.Time);

		if (IsSwimming()) //just entered water
		{
			RestorePreAdditiveVRMotionVelocity();
			remainingTime += subTimeTickRemaining;
			StartSwimmingVR(OldCapsuleLocation, OldVelocity, timeTick, remainingTime, Iterations);
			return;
		}
		else if (Hit.bBlockingHit)
		{
			if (IsValidLandingSpot(VRRootCapsule->OffsetComponentToWorld.GetLocation()/*UpdatedComponent->GetComponentLocation()*/, Hit))
			{
				RestorePreAdditiveVRMotionVelocity();
				remainingTime += subTimeTickRemaining;
				ProcessLanded(Hit, remainingTime, Iterations);
				return;
			}
			else
			{
				// Compute impact deflection based on final velocity, not integration step.
				// This allows us to compute a new velocity from the deflected vector, and ensures the full gravity effect is included in the slide result.
				Adjusted = Velocity * timeTick;

				// See if we can convert a normally invalid landing spot (based on the hit result) to a usable one.
				if (!Hit.bStartPenetrating && ShouldCheckForValidLandingSpot(timeTick, Adjusted, Hit))
				{
					/*const */FVector PawnLocation = UpdatedComponent->GetComponentLocation();
					if (VRRootCapsule)
						PawnLocation = VRRootCapsule->OffsetComponentToWorld.GetLocation();

					FFindFloorResult FloorResult;
					FindFloor(PawnLocation, FloorResult, false, NULL);

					// Note that we only care about capsule sweep floor results, since the line trace may detect a lower walkable surface that our falling capsule wouldn't actually reach yet.
					if (!FloorResult.bLineTrace && FloorResult.IsWalkableFloor() && IsValidLandingSpot(PawnLocation, FloorResult.HitResult))
					{
						//RestorePreAdditiveVRMotionVelocity();
						remainingTime += subTimeTickRemaining;
						ProcessLanded(FloorResult.HitResult, remainingTime, Iterations);

						if (bAutoOrientToFloorNormal && FloorResult.IsWalkableFloor())
						{
							// Auto Align to the new floor normal
							// Set gravity direction to the new floor normal
							AutoTraceAndSetCharacterToNewGravity(FloorResult.HitResult, timeTick);
						}
						return;
					}
				}

				HandleImpact(Hit, LastMoveTimeSlice, Adjusted);

				// If we've changed physics mode, abort.
				if (!HasValidData() || !IsFalling())
				{
					RestorePreAdditiveVRMotionVelocity();
					return;
				}

				// Limit air control based on what we hit.
				// We moved to the impact point using air control, but may want to deflect from there based on a limited air control acceleration.
				FVector VelocityNoAirControl = OldVelocity;
				FVector AirControlAccel = Acceleration;
				if (bHasLimitedAirControl)
				{
					// Compute VelocityNoAirControl
					{
						// Find velocity *without* acceleration.
						TGuardValue<FVector> RestoreAcceleration(Acceleration, FVector::ZeroVector);
						TGuardValue<FVector> RestoreVelocity(Velocity, OldVelocity);
						if (HasCustomGravity())
						{
							Velocity = ProjectToGravityFloor(Velocity);
							const FVector GravityRelativeOffset = OldVelocity - Velocity;
							CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
							VelocityNoAirControl = Velocity + GravityRelativeOffset;
						}
						else
						{
							Velocity.Z = 0.f;
							CalcVelocity(timeTick, FallingLateralFriction, false, MaxDecel);
							VelocityNoAirControl = FVector(Velocity.X, Velocity.Y, OldVelocity.Z);
						}
						VelocityNoAirControl = NewFallVelocity(VelocityNoAirControl, Gravity, GravityTime);
					}

					const bool bCheckLandingSpot = false; // we already checked above.
					AirControlAccel = (Velocity - VelocityNoAirControl) / timeTick;
					const FVector AirControlDeltaV = LimitAirControl(LastMoveTimeSlice, AirControlAccel, Hit, bCheckLandingSpot) * LastMoveTimeSlice;
					Adjusted = (VelocityNoAirControl + AirControlDeltaV) * LastMoveTimeSlice;
				}

				const FVector OldHitNormal = Hit.Normal;
				const FVector OldHitImpactNormal = Hit.ImpactNormal;
				FVector Delta = ComputeSlideVector(Adjusted, 1.f - Hit.Time, OldHitNormal, Hit);

				// Compute velocity after deflection (only gravity component for RootMotion)
				const UPrimitiveComponent* HitComponent = Hit.GetComponent();
				static const auto CVarUseTargetVelocityOnImpact = IConsoleManager::Get().FindConsoleVariable(TEXT("p.UseTargetVelocityOnImpact"));
				if (CVarUseTargetVelocityOnImpact->GetInt() && !Velocity.IsNearlyZero() && MovementBaseUtility::IsSimulatedBase(HitComponent))
				{
					const FVector ContactVelocity = MovementBaseUtility::GetMovementBaseVelocity(HitComponent, NAME_None) + MovementBaseUtility::GetMovementBaseTangentialVelocity(HitComponent, NAME_None, Hit.ImpactPoint);
					const FVector NewVelocity = Velocity - Hit.ImpactNormal * FVector::DotProduct(Velocity - ContactVelocity, Hit.ImpactNormal);
					Velocity = HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? ProjectToGravityFloor(Velocity) + GetGravitySpaceComponentZ(NewVelocity) : NewVelocity;
				}
				else if (subTimeTickRemaining > UE_KINDA_SMALL_NUMBER && !bJustTeleported)
				{
					const FVector NewVelocity = (Delta / subTimeTickRemaining);
					Velocity = HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? ProjectToGravityFloor(Velocity) + GetGravitySpaceComponentZ(NewVelocity) : NewVelocity;
				}

				if (subTimeTickRemaining > UE_KINDA_SMALL_NUMBER && (Delta | Adjusted) > 0.f)
				{
					// Move in deflected direction.
					SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);

					if (Hit.bBlockingHit)
					{
						// hit second wall
						LastMoveTimeSlice = subTimeTickRemaining;
						subTimeTickRemaining = subTimeTickRemaining * (1.f - Hit.Time);


						if (IsValidLandingSpot(VRRootCapsule->OffsetComponentToWorld.GetLocation()/*UpdatedComponent->GetComponentLocation()*/, Hit))
						{
							RestorePreAdditiveVRMotionVelocity();
							remainingTime += subTimeTickRemaining;
							ProcessLanded(Hit, remainingTime, Iterations);
							return;
						}

						HandleImpact(Hit, LastMoveTimeSlice, Delta);

						// If we've changed physics mode, abort.
						if (!HasValidData() || !IsFalling())
						{
							RestorePreAdditiveVRMotionVelocity();
							return;
						}

						// Act as if there was no air control on the last move when computing new deflection.
						if (bHasLimitedAirControl && GetGravitySpaceZ(Hit.Normal) > CharacterMovementConstants::VERTICAL_SLOPE_NORMAL_ZVR)
						{
							const FVector LastMoveNoAirControl = VelocityNoAirControl * LastMoveTimeSlice;
							Delta = ComputeSlideVector(LastMoveNoAirControl, 1.f, OldHitNormal, Hit);
						}

						FVector PreTwoWallDelta = Delta;
						TwoWallAdjust(Delta, Hit, OldHitNormal);

						// Limit air control, but allow a slide along the second wall.
						if (bHasLimitedAirControl)
						{
							const bool bCheckLandingSpot = false; // we already checked above.
							const FVector AirControlDeltaV = LimitAirControl(subTimeTickRemaining, AirControlAccel, Hit, bCheckLandingSpot) * subTimeTickRemaining;

							// Only allow if not back in to first wall
							if (FVector::DotProduct(AirControlDeltaV, OldHitNormal) > 0.f)
							{
								Delta += (AirControlDeltaV * subTimeTickRemaining);
							}
						}

						// Compute velocity after deflection (only gravity component for RootMotion)
						if (subTimeTickRemaining > UE_KINDA_SMALL_NUMBER && !bJustTeleported)
						{
							const FVector NewVelocity = (Delta / subTimeTickRemaining);
							Velocity = HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocityWithIgnoreZAccumulate() ? ProjectToGravityFloor(Velocity) + GetGravitySpaceComponentZ(NewVelocity) : NewVelocity;
						}

						// bDitch=true means that pawn is straddling two slopes, neither of which he can stand on
						bool bDitch = ((GetGravitySpaceZ(OldHitImpactNormal) > 0.f) && (GetGravitySpaceZ(Hit.ImpactNormal) > 0.f) && (FMath::Abs(GetGravitySpaceZ(Delta)) <= UE_KINDA_SMALL_NUMBER) && ((Hit.ImpactNormal | OldHitImpactNormal) < 0.f));
						SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
						if (Hit.Time == 0.f)
						{
							// if we are stuck then try to side step
							FVector SideDelta = ProjectToGravityFloor(OldHitNormal + Hit.ImpactNormal).GetSafeNormal();
							if (SideDelta.IsNearlyZero())
							{
								if (HasCustomGravity())
								{
									const FVector GravityRelativeHitNormal = RotateWorldToGravity(OldHitNormal);
									SideDelta = RotateGravityToWorld(FVector(GravityRelativeHitNormal.Y, -GravityRelativeHitNormal.X, 0.f)).GetSafeNormal();
								}
								else
								{
									SideDelta = FVector(OldHitNormal.Y, -OldHitNormal.X, 0).GetSafeNormal();
								}
							}
							SafeMoveUpdatedComponent(SideDelta, PawnRotation, true, Hit);
						}

						if (bDitch || IsValidLandingSpot(VRRootCapsule->OffsetComponentToWorld.GetLocation()/*UpdatedComponent->GetComponentLocation()*/, Hit) || Hit.Time == 0.f)
						{
							RestorePreAdditiveVRMotionVelocity();
							remainingTime = 0.f;
							ProcessLanded(Hit, remainingTime, Iterations);
							return;
						}
						else if (GetPerchRadiusThreshold() > 0.f && Hit.Time == 1.f && GetGravitySpaceZ(OldHitImpactNormal) >= GetWalkableFloorZ())
						{
							// We might be in a virtual 'ditch' within our perch radius. This is rare.
							const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
							const float ZMovedDist = FMath::Abs(GetGravitySpaceZ(PawnLocation - OldLocation));
							const float MovedDist2D = ProjectToGravityFloor(PawnLocation - OldLocation).Size();
							if (ZMovedDist <= 0.2f * timeTick && MovedDist2D <= 4.f * timeTick)
							{
								FVector GravityRelativeVelocity = RotateWorldToGravity(Velocity);
								GravityRelativeVelocity.X += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								GravityRelativeVelocity.Y += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								GravityRelativeVelocity.Z = FMath::Max<float>(JumpZVelocity * 0.25f, 1.f);
								Velocity = RotateGravityToWorld(GravityRelativeVelocity);
								Delta = Velocity * timeTick;
								SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
							}
						}
					}
				}
			}
		}
		else
		{
			// We are finding the floor and adjusting here now
			// This matches the final falling Z up to the PhysWalking floor offset to prevent the visible hitch when going
			// From falling to walking.
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false, NULL);

			if (CurrentFloor.IsWalkableFloor())
			{
				// If the current floor distance is within the physwalking required floor offset
				if (CurrentFloor.GetDistanceToFloor() < (MIN_FLOOR_DIST + MAX_FLOOR_DIST) / 2)
				{
					// Adjust to correct height
					AdjustFloorHeight();

					SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);

					// If this is a valid landing spot, stop falling now so that we land correctly
					if (IsValidLandingSpot(VRRootCapsule->OffsetComponentToWorld.GetLocation()/*UpdatedComponent->GetComponentLocation()*/, CurrentFloor.HitResult))
					{
						remainingTime += subTimeTickRemaining;
						ProcessLanded(CurrentFloor.HitResult, remainingTime, Iterations);
						return;
					}
				}
			}
			else if (CurrentFloor.HitResult.bStartPenetrating)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				FHitResult Hit2(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit2.TraceStart + RotateGravityToWorld(FVector(0.f, 0.f, MAX_FLOOR_DIST));
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit2);
				ResolvePenetration(RequestedAdjustment, Hit2, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			if (bAutoOrientToFloorNormal && CurrentFloor.IsWalkableFloor())
			{
				// Auto Align to the new floor normal
				// Set gravity direction to the new floor normal
				AutoTraceAndSetCharacterToNewGravity(CurrentFloor.HitResult, timeTick);
			}
		}

		const FVector GravityProjectedVelocity = ProjectToGravityFloor(Velocity);
		if (GravityProjectedVelocity.SizeSquared() <= UE_KINDA_SMALL_NUMBER * 10.f)
		{
			Velocity = GetGravitySpaceComponentZ(Velocity);
		}

		RestorePreAdditiveVRMotionVelocity();
	}
}


void UVRCharacterMovementComponent::PhysNavWalking(float deltaTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharPhysNavWalking);

	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Root motion not for VR
	if ((!CharacterOwner || !CharacterOwner->GetController()) && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	const EMovementMode StartingMovementMode = MovementMode;
	const uint8 StartingCustomMovementMode = CustomMovementMode;

	// Rewind the players position by the new capsule location
	RewindVRRelativeMovement();

	RestorePreAdditiveRootMotionVelocity();
	//RestorePreAdditiveVRMotionVelocity();

	// Ensure velocity is horizontal.
	MaintainHorizontalGroundVelocity();
	devCodeVR(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysNavWalking: Velocity contains NaN before CalcVelocity (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));

	//bound acceleration
	Acceleration = ProjectToGravityFloor(Acceleration);
	//if (!HasRootMotion())
	//{
		CalcVelocity(deltaTime, GroundFriction, false, BrakingDecelerationWalking);
		devCodeVR(ensureMsgf(!Velocity.ContainsNaN(), TEXT("PhysNavWalking: Velocity contains NaN after CalcVelocity (%s)\n%s"), *GetPathNameSafe(this), *Velocity.ToString()));
	//}

	ApplyRootMotionToVelocity(deltaTime);
	ApplyVRMotionToVelocity(deltaTime);

	if (MovementMode != StartingMovementMode || CustomMovementMode != StartingCustomMovementMode)
	{
		// Root motion could have taken us out of our current mode
		StartNewPhysics(deltaTime, Iterations);
		return;
	}

	Iterations++;

	const FVector DesiredMove = ProjectToGravityFloor(Velocity);

	//const FVector OldPlayerLocation = GetActorFeetLocation();
	const FVector OldLocation = GetActorFeetLocationVR();
	const FVector DeltaMove = DesiredMove * deltaTime;
	const bool bDeltaMoveNearlyZero = DeltaMove.IsNearlyZero();

	FVector AdjustedDest = OldLocation + DeltaMove;
	FNavLocation DestNavLocation;

	bool bSameNavLocation = false;
	if (CachedNavLocation.NodeRef != INVALID_NAVNODEREF)
	{
		if (bProjectNavMeshWalking)
		{
			const float DistSq2D = ProjectToGravityFloor(OldLocation - CachedNavLocation.Location).SizeSquared();
			const float DistZ = FMath::Abs(GetGravitySpaceZ(OldLocation - CachedNavLocation.Location));

			const float TotalCapsuleHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f;
			const float ProjectionScale = (GetGravitySpaceZ(OldLocation) > GetGravitySpaceZ(CachedNavLocation.Location)) ? NavMeshProjectionHeightScaleUp : NavMeshProjectionHeightScaleDown;
			const float DistZThr = TotalCapsuleHeight * FMath::Max(0.f, ProjectionScale);

			bSameNavLocation = (DistSq2D <= UE_KINDA_SMALL_NUMBER) && (DistZ < DistZThr);
		}
		else
		{
			bSameNavLocation = CachedNavLocation.Location.Equals(OldLocation);
		}

		if (bDeltaMoveNearlyZero && bSameNavLocation)
		{
			if (const INavigationDataInterface * NavData = GetNavData())
			{
				if (!NavData->IsNodeRefValid(CachedNavLocation.NodeRef))
				{
					CachedNavLocation.NodeRef = INVALID_NAVNODEREF;
					bSameNavLocation = false;
				}
			}
		}
	}

	if (bDeltaMoveNearlyZero && bSameNavLocation)
	{
		DestNavLocation = CachedNavLocation;
		UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("%s using cached navmesh location! (bProjectNavMeshWalking = %d)"), *GetNameSafe(CharacterOwner), bProjectNavMeshWalking);
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_CharNavProjectPoint);

		// Start the trace from the Z location of the last valid trace.
		// Otherwise if we are projecting our location to the underlying geometry and it's far above or below the navmesh,
		// we'll follow that geometry's plane out of range of valid navigation.
		if (bSameNavLocation && bProjectNavMeshWalking)
		{
			SetGravitySpaceZ(AdjustedDest, GetGravitySpaceZ(CachedNavLocation.Location));
		}

		// Find the point on the NavMesh
		const bool bHasNavigationData = FindNavFloor(AdjustedDest, DestNavLocation);
		if (!bHasNavigationData)
		{
			RestorePreAdditiveVRMotionVelocity();
			SetMovementMode(MOVE_Walking);
			return;
		}

		CachedNavLocation = DestNavLocation;
	}

	if (DestNavLocation.NodeRef != INVALID_NAVNODEREF)
	{
		FVector NewLocation = ProjectToGravityFloor(AdjustedDest) + GetGravitySpaceComponentZ(DestNavLocation.Location);
		if (bProjectNavMeshWalking)
		{
			SCOPE_CYCLE_COUNTER(STAT_CharNavProjectLocation);
			const float TotalCapsuleHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f;
			const float UpOffset = TotalCapsuleHeight * FMath::Max(0.f, NavMeshProjectionHeightScaleUp);
			const float DownOffset = TotalCapsuleHeight * FMath::Max(0.f, NavMeshProjectionHeightScaleDown);
			NewLocation = ProjectLocationFromNavMesh(deltaTime, OldLocation, NewLocation, UpOffset, DownOffset);
		}

		FVector AdjustedDelta = NewLocation - OldLocation;

		if (!AdjustedDelta.IsNearlyZero())
		{
			// 4.16 UNCOMMENT
			FHitResult HitResult;
			SafeMoveUpdatedComponent(AdjustedDelta, UpdatedComponent->GetComponentQuat(), bSweepWhileNavWalking, HitResult);
			
			/* 4.16 Delete*/
			//const bool bSweep = UpdatedPrimitive ? UpdatedPrimitive->bGenerateOverlapEvents : false;
			//FHitResult HitResult;
			//SafeMoveUpdatedComponent(AdjustedDelta, UpdatedComponent->GetComponentQuat(), bSweep, HitResult);
			// End 4.16 delete
		}

		// Update velocity to reflect actual move
		if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasVelocity())
		{
			Velocity = (GetActorFeetLocationVR() - OldLocation) / deltaTime;
			MaintainHorizontalGroundVelocity();
		}

		bJustTeleported = false;
	}
	else
	{
		StartFalling(Iterations, deltaTime, deltaTime, DeltaMove, OldLocation);
	}

	RestorePreAdditiveVRMotionVelocity();
}

void UVRCharacterMovementComponent::PhysSwimming(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// Rewind the players position by the new capsule location
	RewindVRRelativeMovement();

	RestorePreAdditiveRootMotionVelocity();
	//RestorePreAdditiveVRMotionVelocity();

	float NetFluidFriction = 0.f;
	float Depth = ImmersionDepth();
	float NetBuoyancy = Buoyancy * Depth;
	float OriginalAccelZ = GetGravitySpaceZ(Acceleration);
	bool bLimitedUpAccel = false;

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (GetGravitySpaceZ(Velocity) > 0.33f * MaxSwimSpeed) && (NetBuoyancy != 0.f))
	{
		//damp positive Z out of water
		SetGravitySpaceZ(Velocity, FMath::Max<FVector::FReal>(0.33f * MaxSwimSpeed, GetGravitySpaceZ(Velocity) * Depth * Depth));
	}
	else if (Depth < 0.65f)
	{
		bLimitedUpAccel = (OriginalAccelZ > 0.f);
		SetGravitySpaceZ(Acceleration, FMath::Min<FVector::FReal>(0.1f, OriginalAccelZ));
	}

	Iterations++;
	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	bJustTeleported = false;
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		const float Friction = 0.5f * GetPhysicsVolume()->FluidFriction * Depth;
		CalcVelocity(deltaTime, Friction, true, GetMaxBrakingDeceleration());
		Velocity += (GetGravityZ() * deltaTime * (1.f - NetBuoyancy)) * -GetGravityDirection();
	}

	ApplyRootMotionToVelocity(deltaTime);
	ApplyVRMotionToVelocity(deltaTime);

	FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);
	const float remainingTime = deltaTime * Swim(Adjusted, Hit);

	//may have left water - if so, script might have set new physics mode
	if (!IsSwimming())
	{
		RestorePreAdditiveVRMotionVelocity();
		StartNewPhysics(remainingTime, Iterations);
		return;
	}

	if (Hit.Time < 1.f && CharacterOwner)
	{
		HandleSwimmingWallHit(Hit, deltaTime);
		if (bLimitedUpAccel && (GetGravitySpaceZ(Velocity) >= 0.f))
		{
			// allow upward velocity at surface if against obstacle
			Velocity += OriginalAccelZ * deltaTime * -GetGravityDirection();
			Adjusted = Velocity * (1.f - Hit.Time)*deltaTime;
			SwimVR(Adjusted, Hit);
			if (!IsSwimming())
			{
				RestorePreAdditiveVRMotionVelocity();
				StartNewPhysics(remainingTime, Iterations);
				return;
			}
		}

		const FVector VelDir = Velocity.GetSafeNormal();
		const float UpDown = VelDir | GetGravityDirection();

		bool bSteppedUp = false;
		if ((FMath::Abs(GetGravitySpaceZ(Hit.ImpactNormal)) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) && CanStepUp(Hit))
		{
			const float StepZ = GetGravitySpaceZ(UpdatedComponent->GetComponentLocation());
			const FVector RealVelocity = Velocity;
			SetGravitySpaceZ(Velocity, 1.f);	// HACK: since will be moving up, in case pawn leaves the water
			bSteppedUp = StepUp(GetGravityDirection(), Adjusted * (1.f - Hit.Time), Hit);
			if (bSteppedUp)
			{
				//may have left water - if so, script might have set new physics mode
				if (!IsSwimming())
				{
					RestorePreAdditiveVRMotionVelocity();
					StartNewPhysics(remainingTime, Iterations);
					return;
				}
				SetGravitySpaceZ(OldLocation, GetGravitySpaceZ(UpdatedComponent->GetComponentLocation()) + (GetGravitySpaceZ(OldLocation) - StepZ));
			}
			Velocity = RealVelocity;
		}

		if (!bSteppedUp)
		{
			//adjust and try again
			HandleImpact(Hit, deltaTime, Adjusted);
			SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
		}
	}

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bJustTeleported && ((deltaTime - remainingTime) > UE_KINDA_SMALL_NUMBER) && CharacterOwner)
	{
		const bool bWaterJump = !GetPhysicsVolume()->bWaterVolume;
		const FVector::FReal VelZ = GetGravitySpaceZ(Velocity);
		Velocity = ((UpdatedComponent->GetComponentLocation() - OldLocation)/* - AdditionalVRInputVector*/) / (deltaTime - remainingTime);
		if (bWaterJump)
		{
			SetGravitySpaceZ(Velocity, VelZ);
		}
	}

	if (!GetPhysicsVolume()->bWaterVolume && IsSwimming())
	{
		SetMovementMode(MOVE_Falling); //in case script didn't change it (w/ zone change)
	}

	RestorePreAdditiveVRMotionVelocity();

	//may have left water - if so, script might have set new physics mode
	if (!IsSwimming())
	{
		StartNewPhysics(remainingTime, Iterations);
	}
}


void UVRCharacterMovementComponent::StartSwimmingVR(FVector OldLocation, FVector OldVelocity, float timeTick, float remainingTime, int32 Iterations)
{
	if (remainingTime < MIN_TICK_TIME || timeTick < MIN_TICK_TIME)
	{
		return;
	}

	FVector NewLocation = VRRootCapsule ? VRRootCapsule->OffsetComponentToWorld.GetLocation() : UpdatedComponent->GetComponentLocation();

	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bJustTeleported)
	{
		Velocity = (NewLocation - OldLocation) / timeTick; //actual average velocity
		Velocity = 2.f*Velocity - OldVelocity; //end velocity has 2* accel of avg
		Velocity = Velocity.GetClampedToMaxSize(GetPhysicsVolume()->TerminalVelocity);
	}
	const FVector End = FindWaterLine(NewLocation, OldLocation);
	float waterTime = 0.f;
	if (End != NewLocation)
	{
		const float ActualDist = (NewLocation - OldLocation).Size();
		if (ActualDist > UE_KINDA_SMALL_NUMBER)
		{
			waterTime = timeTick * (End - NewLocation).Size() / ActualDist;
			remainingTime += waterTime;
		}
		MoveUpdatedComponent(End - NewLocation, UpdatedComponent->GetComponentQuat(), true);
	}
	const FVector::FReal GravityRelativeVelocityZ = GetGravitySpaceZ(Velocity);
	if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (GravityRelativeVelocityZ > 2.f * CharacterMovementConstants::SWIMBOBSPEEDVR) && (GravityRelativeVelocityZ < 0.f)) //allow for falling out of water
	{
		SetGravitySpaceZ(Velocity, CharacterMovementConstants::SWIMBOBSPEEDVR - ProjectToGravityFloor(Velocity).Size() * 0.7f); //smooth bobbing
	}
	if ((remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations))
	{
		PhysSwimming(remainingTime, Iterations);
	}
}

float UVRCharacterMovementComponent::SwimVR(FVector Delta, FHitResult& Hit)
{
	FVector Start = VRRootCapsule ? VRRootCapsule->OffsetComponentToWorld.GetLocation() : UpdatedComponent->GetComponentLocation();
	
	float airTime = 0.f;
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

	if (!GetPhysicsVolume()->bWaterVolume) //then left water
	{
		FVector NewLoc = VRRootCapsule ? VRRootCapsule->OffsetComponentToWorld.GetLocation() : UpdatedComponent->GetComponentLocation();

		const FVector End = FindWaterLine(Start, NewLoc);
		const float DesiredDist = Delta.Size();
		if (End != NewLoc && DesiredDist > UE_KINDA_SMALL_NUMBER)
		{
			airTime = (End - NewLoc).Size() / DesiredDist;
			if (((NewLoc - Start) | (End - NewLoc)) > 0.f)
			{
				airTime = 0.f;
			}
			SafeMoveUpdatedComponent(End - NewLoc, UpdatedComponent->GetComponentQuat(), true, Hit);
		}
	}
	return airTime;
}

bool UVRCharacterMovementComponent::CheckWaterJump(FVector CheckPoint, FVector& WallNormal)
{
	if (!HasValidData())
	{
		return false;
	}
	FVector currentLoc = VRRootCapsule ? VRRootCapsule->OffsetComponentToWorld.GetLocation() : UpdatedComponent->GetComponentLocation();

	// check if there is a wall directly in front of the swimming pawn
	CheckPoint = ProjectToGravityFloor(CheckPoint);
	FVector CheckNorm = CheckPoint.GetSafeNormal();
	float PawnCapsuleRadius, PawnCapsuleHalfHeight;
	CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleSize(PawnCapsuleRadius, PawnCapsuleHalfHeight);
	CheckPoint = currentLoc + 1.2f * PawnCapsuleRadius * CheckNorm;
	FHitResult HitInfo(1.f);
	FCollisionQueryParams CapsuleParams(SCENE_QUERY_STAT(CheckWaterJump), false, CharacterOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(CapsuleParams, ResponseParam);
	FCollisionShape CapsuleShape = GetPawnCapsuleCollisionShape(SHRINK_None);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();
	bool bHit = GetWorld()->SweepSingleByChannel(HitInfo, UpdatedComponent->GetComponentLocation(), CheckPoint, GetWorldToGravityTransform(), CollisionChannel, CapsuleShape, CapsuleParams, ResponseParam);

	if (bHit && !HitInfo.HitObjectHandle.DoesRepresentClass(APawn::StaticClass()))
	{
		// hit a wall - check if it is low enough
		WallNormal = -1.f * HitInfo.ImpactNormal;
		FVector Start = currentLoc;//UpdatedComponent->GetComponentLocation();
		Start += MaxOutOfWaterStepHeight * -GetGravityDirection();
		CheckPoint = Start + 3.2f * PawnCapsuleRadius * WallNormal;
		FCollisionQueryParams LineParams(SCENE_QUERY_STAT(CheckWaterJump), true, CharacterOwner);
		FCollisionResponseParams LineResponseParam;
		InitCollisionParams(LineParams, LineResponseParam);
		bHit = GetWorld()->LineTraceSingleByChannel(HitInfo, Start, CheckPoint, CollisionChannel, LineParams, LineResponseParam);
		// if no high obstruction, or it's a valid floor, then pawn can jump out of water
		return !bHit || IsWalkable(HitInfo);
	}
	return false;
}

FBasedPosition UVRCharacterMovementComponent::GetActorFeetLocationBased() const
{
	return FBasedPosition(NULL, GetActorFeetLocationVR());
}

void UVRCharacterMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	SCOPE_CYCLE_COUNTER(STAT_CharProcessLanded);

	if (CharacterOwner && CharacterOwner->ShouldNotifyLanded(Hit))
	{
		CharacterOwner->Landed(Hit);
	}
	if (IsFalling())
	{
		
		if (GetGroundMovementMode() == MOVE_NavWalking)
		{
			// verify navmesh projection and current floor
			// otherwise movement will be stuck in infinite loop:
			// navwalking -> (no navmesh) -> falling -> (standing on something) -> navwalking -> ....

			const FVector TestLocation = GetActorFeetLocationVR();
			FNavLocation NavLocation;

			const bool bHasNavigationData = FindNavFloor(TestLocation, NavLocation);
			if (!bHasNavigationData || NavLocation.NodeRef == INVALID_NAVNODEREF)
			{
				SetGroundMovementMode(MOVE_Walking);
				//GroundMovementMode = MOVE_Walking;
				UE_LOG(LogVRCharacterMovement, Verbose, TEXT("ProcessLanded(): %s tried to go to NavWalking but couldn't find NavMesh! Using Walking instead."), *GetNameSafe(CharacterOwner));
			}
		}

		SetPostLandedPhysics(Hit);
	}

	IPathFollowingAgentInterface* PFAgent = GetPathFollowingAgent();
	if (PFAgent)
	{
		PFAgent->OnLanded();
	}

	StartNewPhysics(remainingTime, Iterations);
}

///////////////////////////
// End Navigation Functions
///////////////////////////

void UVRCharacterMovementComponent::PostPhysicsTickComponent(float DeltaTime, FCharacterMovementComponentPostPhysicsTickFunction& ThisTickFunction)
{
	if (bDeferUpdateBasedMovement)
	{
		FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);
		UpdateBasedMovement(DeltaTime);
		SaveBaseLocation();
		bDeferUpdateBasedMovement = false;
	}
}


void UVRCharacterMovementComponent::SimulateMovement(float DeltaSeconds)
{
	if (!HasValidData() || UpdatedComponent->Mobility != EComponentMobility::Movable || UpdatedComponent->IsSimulatingPhysics())
	{
		return;
	}

	const bool bIsSimulatedProxy = (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy);
	const FRepMovement& ConstRepMovement = CharacterOwner->GetReplicatedMovement();

	// Workaround for replication not being updated initially
	if (bIsSimulatedProxy &&
		ConstRepMovement.Location.IsZero() &&
		ConstRepMovement.Rotation.IsZero() &&
		ConstRepMovement.LinearVelocity.IsZero())
	{
		return;
	}

	// If base is not resolved on the client, we should not try to simulate at all
	if (CharacterOwner->GetReplicatedBasedMovement().IsBaseUnresolved())
	{
		UE_LOG(LogVRCharacterMovement, Verbose, TEXT("Base for simulated character '%s' is not resolved on client, skipping SimulateMovement"), *CharacterOwner->GetName());
		return;
	}

	FVector OldVelocity;
	FVector OldLocation;

	// Scoped updates can improve performance of multiple MoveComponent calls.
	{
		FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

		bool bHandledNetUpdate = false;
		if (bIsSimulatedProxy)
		{
			// Handle network changes
			if (bNetworkUpdateReceived)
			{
				bNetworkUpdateReceived = false;
				bHandledNetUpdate = true;
				UE_LOG(LogVRCharacterMovement, Verbose, TEXT("Proxy %s received net update"), *GetNameSafe(CharacterOwner));
				if (bNetworkGravityDirectionChanged)
				{
					SetGravityDirection(CharacterOwner->GetReplicatedGravityDirection());
					bNetworkGravityDirectionChanged = false;
				}

				if (bNetworkMovementModeChanged)
				{
					ApplyNetworkMovementMode(CharacterOwner->GetReplicatedMovementMode());
					bNetworkMovementModeChanged = false;
				}
				else if (bJustTeleported || bForceNextFloorCheck)
				{
					// Make sure floor is current. We will continue using the replicated base, if there was one.
					bJustTeleported = false;
					UpdateFloorFromAdjustment();
				}
			}
			else if (bForceNextFloorCheck)
			{
				UpdateFloorFromAdjustment();
			}
		}

		UpdateCharacterStateBeforeMovement(DeltaSeconds);

		if (MovementMode != MOVE_None)
		{
			//TODO: Also ApplyAccumulatedForces()?
			HandlePendingLaunch();
		}
		ClearAccumulatedForces();

		if (MovementMode == MOVE_None)
		{
			return;
		}

		const bool bSimGravityDisabled = (bIsSimulatedProxy && CharacterOwner->bSimGravityDisabled);
		const bool bZeroReplicatedGroundVelocity = (bIsSimulatedProxy && IsMovingOnGround() && ConstRepMovement.LinearVelocity.IsZero());

		// bSimGravityDisabled means velocity was zero when replicated and we were stuck in something. Avoid external changes in velocity as well.
		// Being in ground movement with zero velocity, we cannot simulate proxy velocities safely because we might not get any further updates from the server.
		if (bSimGravityDisabled || bZeroReplicatedGroundVelocity)
		{
			Velocity = FVector::ZeroVector;
		}


		MaybeUpdateBasedMovement(DeltaSeconds);

		// simulated pawns predict location
		OldVelocity = Velocity;
		OldLocation = UpdatedComponent->GetComponentLocation();

		UpdateProxyAcceleration();

		static const auto CVarNetEnableSkipProxyPredictionOnNetUpdate = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetEnableSkipProxyPredictionOnNetUpdate"));
		// May only need to simulate forward on frames where we haven't just received a new position update.
		if (!bHandledNetUpdate || !bNetworkSkipProxyPredictionOnNetUpdate || !CVarNetEnableSkipProxyPredictionOnNetUpdate->GetInt())
		{
			UE_LOG(LogVRCharacterMovement, Verbose, TEXT("Proxy %s simulating movement"), *GetNameSafe(CharacterOwner));
			FStepDownResult StepDownResult;

			// Skip the estimated movement when movement simulation is off, but keep the floor find
			if(!bDisableSimulatedTickWhenSmoothingMovement)
			{ 
				MoveSmooth(Velocity, DeltaSeconds, &StepDownResult);
			}

			// find floor and check if falling
			if (IsMovingOnGround() || MovementMode == MOVE_Falling)
			{
				const bool bShouldFindFloor = GetGravitySpaceZ(Velocity) <= UE_KINDA_SMALL_NUMBER;

				if (StepDownResult.bComputedFloor)
				{
					CurrentFloor = StepDownResult.FloorResult;
				}
				else if (bDisableSimulatedTickWhenSmoothingMovement || bShouldFindFloor)
				{
					FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, Velocity.IsZero(), NULL);
				}
				else
				{
					CurrentFloor.Clear();
				}

				// Possible for dynamic movement bases, particularly those that align to slopes while the character does not, to encroach the character.
				// Check to see if we can resolve the penetration in those cases, and if so find the floor.
				if (CurrentFloor.HitResult.bStartPenetrating && MovementBaseUtility::IsDynamicBase(GetMovementBase()))
				{
					// Follows PhysWalking approach for encroachment on floor tests
					FHitResult Hit(CurrentFloor.HitResult);
					Hit.TraceEnd = Hit.TraceStart - GetGravityDirection() * MAX_FLOOR_DIST;


					const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
					const bool bResolved = ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
					bForceNextFloorCheck |= bResolved;

					if (bAutoOrientToFloorNormal && CurrentFloor.IsWalkableFloor())
					{
						// Auto Align to the new floor normal
						// Set gravity direction to the new floor normal
						AutoTraceAndSetCharacterToNewGravity(CurrentFloor.HitResult, DeltaSeconds);
					}
				}
				else if (!CurrentFloor.IsWalkableFloor())
				{
					if (!bSimGravityDisabled)
					{
						// No floor, must fall.
						if (GetGravitySpaceZ(Velocity) <= UE_KINDA_SMALL_NUMBER || bApplyGravityWhileJumping || !CharacterOwner->IsJumpProvidingForce())
						{
							Velocity = NewFallVelocity(Velocity, -GetGravityDirection() * GetGravityZ(), DeltaSeconds);
						}
					}

					SetMovementMode(MOVE_Falling);
				}
				else
				{
					// Walkable floor
					if (IsMovingOnGround())
					{
						AdjustFloorHeight();
						SetBaseFromFloor(CurrentFloor);
					}
					else if (MovementMode == MOVE_Falling)
					{
						if (CurrentFloor.FloorDist <= MIN_FLOOR_DIST || (bSimGravityDisabled && CurrentFloor.FloorDist <= MAX_FLOOR_DIST))
						{
							// Landed
							SetPostLandedPhysics(CurrentFloor.HitResult);
						}
						else
						{
							if (!bSimGravityDisabled)
							{
								// Continue falling.
								Velocity = NewFallVelocity(Velocity, -GetGravityDirection() * GetGravityZ(), DeltaSeconds);
							}
							CurrentFloor.Clear();
						}
					}

					if (bAutoOrientToFloorNormal && CurrentFloor.IsWalkableFloor())
					{
						// Auto Align to the new floor normal
						// Set gravity direction to the new floor normal
						AutoTraceAndSetCharacterToNewGravity(CurrentFloor.HitResult, DeltaSeconds);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogVRCharacterMovement, Verbose, TEXT("Proxy %s SKIPPING simulate movement"), *GetNameSafe(CharacterOwner));
		}

		UpdateCharacterStateAfterMovement(DeltaSeconds);

		// consume path following requested velocity
		LastUpdateRequestedVelocity = bHasRequestedVelocity ? RequestedVelocity : FVector::ZeroVector;
		bHasRequestedVelocity = false;

		OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	} // End scoped movement update

	  // Call custom post-movement events. These happen after the scoped movement completes in case the events want to use the current state of overlaps etc.
	CallMovementUpdateDelegate(DeltaSeconds, OldLocation, OldVelocity);

	static const auto CVarBasedMovementMode = IConsoleManager::Get().FindConsoleVariable(TEXT("p.BasedMovementMode"));
	if (CVarBasedMovementMode->GetInt() == 0)
	{
		SaveBaseLocation(); // behaviour before implementing this fix
	}
	else
	{
		MaybeSaveBaseLocation();
	}

	UpdateComponentVelocity();
	bJustTeleported = false;

	LastUpdateLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	LastUpdateRotation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;
	LastUpdateVelocity = Velocity;
}

void UVRCharacterMovementComponent::MoveSmooth(const FVector& InVelocity, const float DeltaSeconds, FStepDownResult* OutStepDownResult)
{
	if (!HasValidData())
	{
		return;
	}

	// Custom movement mode.
	// Custom movement may need an update even if there is zero velocity.
	if (MovementMode == MOVE_Custom)
	{
		FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);
		PhysCustom(DeltaSeconds, 0);
		return;
	}

	FVector Delta = InVelocity * DeltaSeconds;
	if (Delta.IsZero())
	{
		return;
	}

	FVRCharacterScopedMovementUpdate ScopedMovementUpdate(UpdatedComponent, bEnableScopedMovementUpdates ? EScopedUpdate::DeferredUpdates : EScopedUpdate::ImmediateUpdates);

	if (IsMovingOnGround())
	{
		MoveAlongFloor(InVelocity, DeltaSeconds, OutStepDownResult);
	}
	else
	{
		FHitResult Hit(1.f);
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);

		if (Hit.IsValidBlockingHit())
		{
			bool bSteppedUp = false;

			if (IsFlying())
			{
				if (CanStepUp(Hit))
				{
					OutStepDownResult = NULL; // No need for a floor when not walking.
					bool bShouldAttemptStepUp = false;
					bShouldAttemptStepUp = FMath::Abs(GetGravitySpaceZ(Hit.ImpactNormal)) < 0.2;
					if (bShouldAttemptStepUp)
					{
						const FVector GravDir = GetGravityDirection();
						const FVector DesiredDir = Delta.GetSafeNormal();
						const float UpDown = GravDir | DesiredDir;
						if ((UpDown < 0.5f) && (UpDown > -0.2f))
						{
							bSteppedUp = StepUp(GravDir, Delta * (1.f - Hit.Time), Hit, OutStepDownResult);
						}
					}
				}
			}

			// If StepUp failed, try sliding.
			if (!bSteppedUp)
			{
				SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, false);
			}
		}
	}
}

void UVRCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	if (MoveResponse.IsGoodMove())
	{
		ClientAckGoodMove_Implementation(MoveResponse.ClientAdjustment.TimeStamp);
	}
	else
	{
		// Wrappers to old RPC handlers, to maintain compatibility. If overrides need additional serialized data, they can access GetMoveResponseDataContainer()
		if (MoveResponse.bRootMotionSourceCorrection)
		{
			if (FRootMotionSourceGroup* RootMotionSourceGroup = MoveResponse.GetRootMotionSourceGroup(*this))
			{
				ClientAdjustRootMotionSourcePosition_Implementation(
					MoveResponse.ClientAdjustment.TimeStamp,
					*RootMotionSourceGroup,
					MoveResponse.bRootMotionMontageCorrection,
					MoveResponse.RootMotionTrackPosition,
					MoveResponse.ClientAdjustment.NewLoc,
					MoveResponse.RootMotionRotation,
					GetGravitySpaceZ(MoveResponse.ClientAdjustment.NewVel),
					MoveResponse.ClientAdjustment.NewBase,
					MoveResponse.ClientAdjustment.NewBaseBoneName,
					MoveResponse.bHasBase,
					MoveResponse.ClientAdjustment.bBaseRelativePosition,
					MoveResponse.ClientAdjustment.MovementMode);
			}
		}
		else if (MoveResponse.bRootMotionMontageCorrection)
		{
			ClientAdjustRootMotionPosition_Implementation(
				MoveResponse.ClientAdjustment.TimeStamp,
				MoveResponse.RootMotionTrackPosition,
				MoveResponse.ClientAdjustment.NewLoc,
				MoveResponse.RootMotionRotation,
				GetGravitySpaceZ(MoveResponse.ClientAdjustment.NewVel),
				MoveResponse.ClientAdjustment.NewBase,
				MoveResponse.ClientAdjustment.NewBaseBoneName,
				MoveResponse.bHasBase,
				MoveResponse.ClientAdjustment.bBaseRelativePosition,
				MoveResponse.ClientAdjustment.MovementMode);
		}
		else
		{
			ClientAdjustPositionVR_Implementation(
				MoveResponse.ClientAdjustment.TimeStamp,
				MoveResponse.ClientAdjustment.NewLoc,
				//FRotator::CompressAxisToShort(MoveResponse.ClientAdjustment.NewRot.Yaw),
				MoveResponse.ClientAdjustment.NewVel,
				MoveResponse.ClientAdjustment.NewBase,
				MoveResponse.ClientAdjustment.NewBaseBoneName,
				MoveResponse.bHasBase,
				MoveResponse.ClientAdjustment.bBaseRelativePosition,
				MoveResponse.ClientAdjustment.MovementMode,
				MoveResponse.bHasRotation ? MoveResponse.ClientAdjustment.NewRot : TOptional<FRotator>(),
				MoveResponse.ClientAdjustment.GravityDirection
			);

				// #TODO: Epic added rotation adjustment in 5.1
				//MoveResponse.bHasRotation ? MoveResponse.ClientAdjustment.NewRot : TOptional<FRotator>()
		}
	}
}

// #TODO: Epic added their own rotation correct in 5.1
//TOptional<FRotator> OptionalRotation /* = TOptional<FRotator>()*/
void UVRCharacterMovementComponent::ClientAdjustPositionVR_Implementation
(
	float TimeStamp,
	FVector NewLocation,
	//uint16 NewYaw,
	FVector NewVelocity,
	UPrimitiveComponent* NewBase,
	FName NewBaseBoneName,
	bool bHasBase,
	bool bBaseRelativePosition,
	uint8 ServerMovementMode,
	TOptional<FRotator> OptionalRotation,
	TOptional<FVector> OptionalGravityDirection
)
{
	if (!HasValidData() || !IsActive())
	{
		return;
	}


	FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	check(ClientData);

	// Make sure the base actor exists on this client.
	const bool bUnresolvedBase = bHasBase && (NewBase == NULL);
	if (bUnresolvedBase)
	{
		if (bBaseRelativePosition)
		{
			UE_LOG(LogNetPlayerMovement, Warning, TEXT("ClientAdjustPosition_Implementation could not resolve the new relative movement base actor, ignoring server correction! Client currently at world location %s on base %s"),
				*UpdatedComponent->GetComponentLocation().ToString(), *GetNameSafe(GetMovementBase()));
			return;
		}
		else
		{
			UE_LOG(LogNetPlayerMovement, Verbose, TEXT("ClientAdjustPosition_Implementation could not resolve the new absolute movement base actor, but WILL use the position!"));
		}
	}

	// Ack move if it has not expired.
	int32 MoveIndex = ClientData->GetSavedMoveIndex(TimeStamp);
	if (MoveIndex == INDEX_NONE)
	{
		if (ClientData->LastAckedMove.IsValid())
		{
			UE_LOG(LogNetPlayerMovement, Log, TEXT("ClientAdjustPosition_Implementation could not find Move for TimeStamp: %f, LastAckedTimeStamp: %f, CurrentTimeStamp: %f"), TimeStamp, ClientData->LastAckedMove->TimeStamp, ClientData->CurrentTimeStamp);
		}
		return;
	}

	// Trying to use epics rotation code now
	// Comment this when we port to it
	/*if (!bUseClientControlRotation)
	{
		float YawValue = FRotator::DecompressAxisFromShort(NewYaw);
		// Trust the server's control yaw
		if (ClientData->LastAckedMove.IsValid() && !FMath::IsNearlyEqual(ClientData->LastAckedMove->SavedControlRotation.Yaw, YawValue))
		{
			if (BaseVRCharacterOwner)
			{
				if (BaseVRCharacterOwner->bUseControllerRotationYaw)
				{
					AController * myController = BaseVRCharacterOwner->GetController();
					if (myController)
					{
						//FRotator newRot = myController->GetControlRotation();
						myController->SetControlRotation(FRotator(0.f, YawValue, 0.f));//(ClientData->LastAckedMove->SavedControlRotation);
					}
				}

				BaseVRCharacterOwner->SetActorRotation(FRotator(0.f, YawValue, 0.f));
			}
		}
	}*/

	ClientData->AckMove(MoveIndex, *this);

	FVector WorldShiftedNewLocation;
	//  Received Location is relative to dynamic base
	if (bBaseRelativePosition)
	{
		MovementBaseUtility::TransformLocationToWorld(NewBase, NewBaseBoneName, NewLocation, WorldShiftedNewLocation); // TODO: error handling if returns false	
	}
	else
	{
		WorldShiftedNewLocation = FRepMovement::RebaseOntoLocalOrigin(NewLocation, this);
	}

	// Server's world velocity may need to be converted to velocity relative to the movement base orientation, if the base orientations don't match.
	const FCharacterMoveResponseDataContainer& ResponseDataContainer = GetMoveResponseDataContainer();
	if (ResponseDataContainer.ClientAdjustment.bBaseRelativeVelocity)
	{
		// Convert Relative Velocity -> World Velocity
		const FVector CurrentVelocity = NewVelocity;
		MovementBaseUtility::TransformDirectionToWorld(NewBase, NewBaseBoneName, CurrentVelocity, NewVelocity);
	}


	// #TODO: Epics 5.1 rotation enforce, we use our own currently
	
	// Fall back to the last-known good rotation if the server didn't send one
	/*static const auto CVarUseLastGoodRotationDuringCorrection = IConsoleManager::Get().FindConsoleVariable(TEXT("p.UseLastGoodRotationDuringCorrection"));
	if (CVarUseLastGoodRotationDuringCorrection->GetInt()
		&& (bOrientRotationToMovement || bUseControllerDesiredRotation)
		&& (!OptionalRotation.IsSet() && ClientData->LastAckedMove.IsValid()))
	{
		OptionalRotation = ClientData->LastAckedMove->SavedRotation;
	}*/

	// #TODO: Currently epic isn't using the gravity direction in corrections, which should be incorrect
	// Trigger event
	FVector GravityCorrection = OptionalGravityDirection.IsSet() ? OptionalGravityDirection.GetValue() : DefaultGravityDirection;
	OnClientCorrectionReceived(*ClientData, TimeStamp, WorldShiftedNewLocation, NewVelocity, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, GravityCorrection);

	// Trust the server's positioning.
	if (UpdatedComponent)
	{
		// Orient to new gravity value
		SetCharacterToNewGravity(GravityCorrection, bAutoOrientToFloorNormal);

		// #TODO: Epics 5.1 rotation enforce, we use our own currently
		/*if (OptionalRotation.IsSet())
		{
			UpdatedComponent->SetWorldLocationAndRotation(WorldShiftedNewLocation, OptionalRotation.GetValue(), false, nullptr, ETeleportType::TeleportPhysics);
		}
		else
		{
			UpdatedComponent->SetWorldLocation(WorldShiftedNewLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}*/

		if (bRunClientCorrectionToHMD && BaseVRCharacterOwner)
		{
			if (OptionalRotation.IsSet())
			{
				BaseVRCharacterOwner->SetActorLocationAndRotationVR(WorldShiftedNewLocation, OptionalRotation.GetValue(), false, false, true, true);
				//BaseVRCharacterOwner->SetActorRotation(OptionalRotation.GetValue(), ETeleportType::TeleportPhysics);
			
						// Trust the server's control yaw
				if (ClientData->LastAckedMove.IsValid() && !ClientData->LastAckedMove->SavedControlRotation.Equals(OptionalRotation.GetValue()))
				{
					if (BaseVRCharacterOwner)
					{
						if (BaseVRCharacterOwner->bUseControllerRotationYaw)
						{
							AController* myController = BaseVRCharacterOwner->GetController();
							if (myController)
							{
								//FRotator newRot = myController->GetControlRotation();
								myController->SetControlRotation(OptionalRotation.GetValue());//(ClientData->LastAckedMove->SavedControlRotation);
							}
						}
					}
				}
			
			}
			else
			{
				BaseVRCharacterOwner->SetActorLocationVR(WorldShiftedNewLocation, true);
			}
		}
		else
		{
			if (OptionalRotation.IsSet())
			{
				UpdatedComponent->SetWorldLocationAndRotation(WorldShiftedNewLocation, OptionalRotation.GetValue(), false, nullptr, ETeleportType::TeleportPhysics);

				// Trust the server's control yaw
				if (ClientData->LastAckedMove.IsValid() && !ClientData->LastAckedMove->SavedControlRotation.Equals(OptionalRotation.GetValue()))
				{
					if (BaseVRCharacterOwner)
					{
						if (BaseVRCharacterOwner->bUseControllerRotationYaw)
						{
							AController* myController = BaseVRCharacterOwner->GetController();
							if (myController)
							{
								//FRotator newRot = myController->GetControlRotation();
								myController->SetControlRotation(OptionalRotation.GetValue());//(ClientData->LastAckedMove->SavedControlRotation);
							}
						}
					}
				}
			}
			else
			{
				UpdatedComponent->SetWorldLocation(WorldShiftedNewLocation, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}
	}

	Velocity = NewVelocity;

	// Trust the server's movement mode
	UPrimitiveComponent* PreviousBase = CharacterOwner->GetMovementBase();
	ApplyNetworkMovementMode(ServerMovementMode);

	// Set base component
	UPrimitiveComponent* FinalBase = NewBase;
	FName FinalBaseBoneName = NewBaseBoneName;
	if (bUnresolvedBase)
	{
		check(NewBase == NULL);
		check(!bBaseRelativePosition);

		// We had an unresolved base from the server
		// If walking, we'd like to continue walking if possible, to avoid falling for a frame, so try to find a base where we moved to.
		if (PreviousBase && UpdatedComponent)
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
			if (CurrentFloor.IsWalkableFloor())
			{
				FinalBase = CurrentFloor.HitResult.Component.Get();
				FinalBaseBoneName = CurrentFloor.HitResult.BoneName;
			}
			else
			{
				FinalBase = nullptr;
				FinalBaseBoneName = NAME_None;
			}

			/*if (bAutoOrientToFloorNormal && CurrentFloor.IsWalkableFloor())
			{
				// Auto Align to the new floor normal
				// Set gravity direction to the new floor normal, snap to new rotation on correction
				// #TODO: Might need to entirely remove this and let the correction set everything?
				AutoTraceAndSetCharacterToNewGravity(CurrentFloor.HitResult);
			}*/
		}
	}
	SetBase(FinalBase, FinalBaseBoneName);

	// Update floor at new location
	UpdateFloorFromAdjustment();
	bJustTeleported = true;

	// Even if base has not changed, we need to recompute the relative offsets (since we've moved).
	SaveBaseLocation();

	LastUpdateLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	LastUpdateRotation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;
	LastUpdateVelocity = Velocity;

	UpdateComponentVelocity();
	ClientData->bUpdatePosition = true;
}

bool UVRCharacterMovementComponent::ServerCheckClientErrorVR(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& ClientWorldLocation, FRotator ClientRot, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	// Check location difference against global setting
	if (!bIgnoreClientMovementErrorChecksAndCorrection)
	{
		//const FVector LocDiff = UpdatedComponent->GetComponentLocation() - ClientWorldLocation;


	// If we are rolling back client rotation
	//if (!bUseClientControlRotation && !FMath::IsNearlyEqual(FRotator::ClampAxis(ClientYaw), FRotator::ClampAxis(UpdatedComponent->GetComponentRotation().Yaw), CharacterMovementComponentStatics::fRotationCorrectionThreshold))

		if (!bUseClientControlRotation)
		{
			// If we are using default gravity direction just match to the yaw only for now (less precision errors)
			if (GetGravityDirection().Equals(DefaultGravityDirection))
			{
				if (!FMath::IsNearlyEqual(FRotator::ClampAxis(ClientRot.Yaw), FRotator::ClampAxis(UpdatedComponent->GetComponentRotation().Yaw), CharacterMovementComponentStatics::fRotationCorrectionThreshold))
				{
					return true;
				}
			}
			else
			{
				float CorrectionValue = bIsBlendingOrientation ? CharacterMovementComponentStatics::fRotationChangingCorrectionThreshold : CharacterMovementComponentStatics::fRotationCorrectionThreshold;
				bIsBlendingOrientation = false;

				if (FQuat::ErrorAutoNormalize(UpdatedComponent->GetComponentQuat(), ClientRot.Quaternion()) > CorrectionValue)
				{
					return true;
				}
			}
		}


#if ROOT_MOTION_DEBUG
		if (RootMotionSourceDebug::CVarDebugRootMotionSources.GetValueOnAnyThread() == 1)
		{
			const FVector LocDiff = UpdatedComponent->GetComponentLocation() - ClientWorldLocation;
			FString AdjustedDebugString = FString::Printf(TEXT("ServerCheckClientError LocDiff(%.1f) ExceedsAllowablePositionError(%d) TimeStamp(%f)"),
				LocDiff.Size(), GetDefault<AGameNetworkManager>()->ExceedsAllowablePositionError(LocDiff), ClientTimeStamp);
			RootMotionSourceDebug::PrintOnScreen(*CharacterOwner, AdjustedDebugString);
		}
#endif

		if (ServerExceedsAllowablePositionError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation, RelativeClientLocation, ClientMovementBase, ClientBaseBoneName, ClientMovementMode))
		{
			return true;
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		static const auto CVarNetForceClientAdjustmentPercent = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetForceClientAdjustmentPercent"));
		if (CVarNetForceClientAdjustmentPercent->GetFloat() > UE_SMALL_NUMBER)
		{
			if (RandomStream.FRand() < CVarNetForceClientAdjustmentPercent->GetFloat())
			{
				UE_LOG(LogVRCharacterMovement, VeryVerbose, TEXT("** ServerCheckClientError forced by p.NetForceClientAdjustmentPercent"));
				return true;
			}
		}
#endif
	}
	else
	{
#if !UE_BUILD_SHIPPING
		static const auto CVarNetShowCorrections = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetShowCorrections"));
		if (CVarNetShowCorrections->GetInt() != 0)
		{
			UE_LOG(LogVRCharacterMovement, Warning, TEXT("*** Server: %s is set to ignore error checks and corrections."), *GetNameSafe(CharacterOwner));
		}
#endif // !UE_BUILD_SHIPPING
	}

	return false;
}

void UVRCharacterMovementComponent::ServerMoveHandleClientErrorVR(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLoc, FRotator ClientRot, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode)
{
	if (!ShouldUsePackedMovementRPCs())
	{
		if (RelativeClientLoc == FVector(1.f, 2.f, 3.f)) // first part of double servermove
		{
			return;
		}
	}

	FNetworkPredictionData_Server_VRCharacter* ServerData = ((FNetworkPredictionData_Server_VRCharacter*)GetPredictionData_Server_Character());
	check(ServerData);

	// Don't prevent more recent updates from being sent if received this frame.
	// We're going to send out an update anyway, might as well be the most recent one.
	APlayerController* PC = Cast<APlayerController>(CharacterOwner->GetController());
	if ((ServerData->LastUpdateTime != GetWorld()->TimeSeconds))
	{
		const AGameNetworkManager* GameNetworkManager = (const AGameNetworkManager*)(AGameNetworkManager::StaticClass()->GetDefaultObject());
		if (GameNetworkManager->WithinUpdateDelayBounds(PC, ServerData->LastUpdateTime))
		{
			return;
		}
	}

	// Offset may be relative to base component
	FVector ClientLoc = RelativeClientLoc;
	if (MovementBaseUtility::UseRelativeLocation(ClientMovementBase))
	{
		MovementBaseUtility::TransformLocationToWorld(ClientMovementBase, ClientBaseBoneName, RelativeClientLoc, ClientLoc);
	}
	else
	{
		ClientLoc = FRepMovement::RebaseOntoLocalOrigin(ClientLoc, this);
	}

	FVector ServerLoc = UpdatedComponent->GetComponentLocation();

	// Client may send a null movement base when walking on bases with no relative location (to save bandwidth).
	// In this case don't check movement base in error conditions, use the server one (which avoids an error based on differing bases). Position will still be validated.
	if (ClientMovementBase == nullptr)
	{
		TEnumAsByte<EMovementMode> NetMovementMode(MOVE_None);
		TEnumAsByte<EMovementMode> NetGroundMode(MOVE_None);
		uint8 NetCustomMode(0);
		UnpackNetworkMovementMode(ClientMovementMode, NetMovementMode, NetCustomMode, NetGroundMode);
		if (NetMovementMode == MOVE_Walking)
		{
			ClientMovementBase = CharacterOwner->GetBasedMovement().MovementBase;
			ClientBaseBoneName = CharacterOwner->GetBasedMovement().BoneName;
		}
	}

	// If base location is out of sync on server and client, changing base can result in a jarring correction.
	// So in the case that the base has just changed on server or client, server trusts the client (within a threshold)
	UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
	FName MovementBaseBoneName = CharacterOwner->GetBasedMovement().BoneName;
	const bool bServerIsFalling = IsFalling();
	const bool bClientIsFalling = ClientMovementMode == MOVE_Falling;
	const bool bServerJustLanded = bLastServerIsFalling && !bServerIsFalling;
	const bool bClientJustLanded = bLastClientIsFalling && !bClientIsFalling;

	FVector RelativeLocation = ServerLoc;
	FVector RelativeVelocity = Velocity;
	bool bUseLastBase = false;
	bool bFallingWithinAcceptableError = false;

	// Potentially trust the client a little when landing
	static const auto CVarClientAuthorityThresholdOnBaseChange = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ClientAuthorityThresholdOnBaseChange"));
	static const auto CVarMaxFallingCorrectionLeash = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxFallingCorrectionLeash"));

	const float ClientAuthorityThreshold = CVarClientAuthorityThresholdOnBaseChange->GetFloat();
	const float MaxFallingCorrectionLeash = CVarMaxFallingCorrectionLeash->GetFloat();
	const bool bDeferServerCorrectionsWhenFalling = ClientAuthorityThreshold > 0.f || MaxFallingCorrectionLeash > 0.f;
	if (bDeferServerCorrectionsWhenFalling)
	{
		// Teleports and other movement modes mean we should just trust the server like we normally would
		if (bTeleportedSinceLastUpdate || (MovementMode != MOVE_Walking && MovementMode != MOVE_Falling))
		{
			MaxServerClientErrorWhileFalling = 0.f;
			bCanTrustClientOnLanding = false;
		}

		// MaxFallingCorrectionLeash indicates we'll use a variable correction size based on the error on take-off and the direction of movement.
		// ClientAuthorityThreshold is an static client-trusting correction upon landing.
		// If both are set, use the smaller of the two. If only one is set, use that. If neither are set, we wouldn't even be inside this block.
		float MaxLandingCorrection = 0.f;
		if (ClientAuthorityThreshold > 0.f && MaxFallingCorrectionLeash > 0.f)
		{
			MaxLandingCorrection = FMath::Min(ClientAuthorityThreshold, MaxServerClientErrorWhileFalling);
		}
		else
		{
			MaxLandingCorrection = FMath::Max(ClientAuthorityThreshold, MaxServerClientErrorWhileFalling);
		}

		if (bCanTrustClientOnLanding && MaxLandingCorrection > 0.f && (bClientJustLanded || bServerJustLanded))
		{
			// no longer falling; server should trust client up to a point to finish the landing as the client sees it
			const FVector LocDiff = ServerLoc - ClientLoc;

			if (!LocDiff.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
			{
				if (LocDiff.SizeSquared() < FMath::Square(MaxLandingCorrection))
				{
					ServerLoc = ClientLoc;
					UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
					bJustTeleported = true;
				}
				else
				{
					const FVector ClampedDiff = LocDiff.GetSafeNormal() * MaxLandingCorrection;
					ServerLoc -= ClampedDiff;
					UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
					bJustTeleported = true;
				}
			}

			MaxServerClientErrorWhileFalling = 0.f;
			bCanTrustClientOnLanding = false;
		}

		// They have it private....
		// Check for lift-off, going from walking to falling.
		// Note that if bStayBasedInAir is enabled we can't rely on the walking to falling transition, instead run logic on the first tick after clearing the MovementBase
		//const bool bCanLiftOffFromBase = bStayBasedInAir
		//	? !MovementBase && LastServerMovementBase.Get() // If we keep the base while in air, consider lift-off if base gets set to null and we had a base last tick
		//	: bLastServerIsWalking; // If walking last tick, we were can consider lift-off logic
		//if (bServerIsFalling && bCanLiftOffFromBase && !bTeleportedSinceLastUpdate)

		if (bServerIsFalling && bLastServerIsWalking && !bTeleportedSinceLastUpdate)
		{
			float ClientForwardFactor = 1.f;
			UPrimitiveComponent* LastServerMovementBaseVRPtr = LastServerMovementBaseVR.Get();
			if (IsValid(LastServerMovementBaseVRPtr) && MovementBaseUtility::IsDynamicBase(LastServerMovementBaseVRPtr) && MaxWalkSpeed > UE_KINDA_SMALL_NUMBER)
			{
				const FVector LastBaseVelocity = MovementBaseUtility::GetMovementBaseVelocity(LastServerMovementBaseVRPtr, LastServerMovementBaseBoneName);
				RelativeVelocity = Velocity - LastBaseVelocity;
				const FVector BaseDirection = ProjectToGravityFloor(LastBaseVelocity).GetSafeNormal();
				const FVector RelativeDirection = RelativeVelocity * (1.f / MaxWalkSpeed);

				ClientForwardFactor = FMath::Clamp(FVector::DotProduct(BaseDirection, RelativeDirection), 0.f, 1.f);

				// To improve position syncing, use old base for take-off
				if (MovementBaseUtility::UseRelativeLocation(LastServerMovementBaseVRPtr))
				{
					// Relative Location
					MovementBaseUtility::TransformLocationToLocal(LastServerMovementBaseVRPtr, LastServerMovementBaseBoneName, UpdatedComponent->GetComponentLocation(), RelativeLocation);
					bUseLastBase = true;
				}
			}

			if (ClientAuthorityThreshold > 0.f && ClientForwardFactor < 1.f)
			{
				const float AdjustedClientAuthorityThreshold = ClientAuthorityThreshold * (1.f - ClientForwardFactor);
				const FVector LocDiff = ServerLoc - ClientLoc;

				// Potentially trust the client a little when taking off in the opposite direction to the base (to help not get corrected back onto the base)
				if (!LocDiff.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
				{
					if (LocDiff.SizeSquared() < FMath::Square(AdjustedClientAuthorityThreshold))
					{
						ServerLoc = ClientLoc;
						UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						bJustTeleported = true;
					}
					else
					{
						const FVector ClampedDiff = LocDiff.GetSafeNormal() * AdjustedClientAuthorityThreshold;
						ServerLoc -= ClampedDiff;
						UpdatedComponent->MoveComponent(ServerLoc - UpdatedComponent->GetComponentLocation(), UpdatedComponent->GetComponentQuat(), true, nullptr, EMoveComponentFlags::MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
						bJustTeleported = true;
					}
				}
			}

			if (ClientForwardFactor < 1.f)
			{
				MaxServerClientErrorWhileFalling = FMath::Min((ServerLoc - ClientLoc).Size() * (1.f - ClientForwardFactor), MaxFallingCorrectionLeash);
				bCanTrustClientOnLanding = true;
			}
			else
			{
				MaxServerClientErrorWhileFalling = 0.f;
				bCanTrustClientOnLanding = false;
			}
		}
		else if (!bServerIsFalling && bCanTrustClientOnLanding)
		{
			MaxServerClientErrorWhileFalling = 0.f;
			bCanTrustClientOnLanding = false;
		}

		if (MaxServerClientErrorWhileFalling > 0.f && (bServerIsFalling || bClientIsFalling))
		{
			const FVector LocDiff = ServerLoc - ClientLoc;
			if (LocDiff.SizeSquared() <= FMath::Square(MaxServerClientErrorWhileFalling))
			{
				ServerLoc = ClientLoc;
				// Still want a velocity update when we first take off
				bFallingWithinAcceptableError = true;
			}
			else
			{
				// Change ServerLoc to be on the edge of the acceptable error rather than doing a full correction.
				// This is not actually changing the server position, but changing it as far as corrections are concerned.
				// This means we're just holding the client on a longer leash while we're falling.
				static const auto CVarMaxFallingCorrectionLeashBuffer = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxFallingCorrectionLeashBuffer"));
				ServerLoc = ServerLoc - LocDiff.GetSafeNormal() * FMath::Clamp(MaxServerClientErrorWhileFalling - CVarMaxFallingCorrectionLeashBuffer->GetFloat(), 0.f, MaxServerClientErrorWhileFalling);
			}
		}
	}

	bool bInClientAuthoritativeMovementMode = false;
	// Custom movement modes aren't going to be rolled back as they are client authed for our pawns

	TEnumAsByte<EMovementMode> NetMovementMode(MOVE_None);
	TEnumAsByte<EMovementMode> NetGroundMode(MOVE_None);
	uint8 NetCustomMode(0);
	UnpackNetworkMovementMode(ClientMovementMode, NetMovementMode, NetCustomMode, NetGroundMode);
	if (NetMovementMode == EMovementMode::MOVE_Custom)
	{
		if (NetCustomMode == (uint8)EVRCustomMovementMode::VRMOVE_Climbing)
			bInClientAuthoritativeMovementMode = true;
	}

	// Compute the client error from the server's position
	// If client has accumulated a noticeable positional error, correct them.
	bNetworkLargeClientCorrection = ServerData->bForceClientUpdate;
	if (!bInClientAuthoritativeMovementMode && (ServerData->bForceClientUpdate || (!bFallingWithinAcceptableError && ServerCheckClientErrorVR(ClientTimeStamp, DeltaTime, Accel, ClientLoc, ClientRot, RelativeClientLoc, ClientMovementBase, ClientBaseBoneName, ClientMovementMode))))
	{
		//UPrimitiveComponent* MovementBase = CharacterOwner->GetMovementBase();
		ServerData->PendingAdjustment.NewVel = Velocity;
		ServerData->PendingAdjustment.NewBase = MovementBase;
		ServerData->PendingAdjustment.NewBaseBoneName = MovementBaseBoneName;
		ServerData->PendingAdjustment.NewRot = UpdatedComponent->GetComponentRotation();
		ServerData->PendingAdjustment.GravityDirection = GetGravityDirection();

		if (bRunClientCorrectionToHMD && IsValid(BaseVRCharacterOwner))
		{
			FVector CapsuleLoc = BaseVRCharacterOwner->GetVRLocation();
			CapsuleLoc.Z = ServerLoc.Z;
			ServerData->PendingAdjustment.NewLoc = FRepMovement::RebaseOntoZeroOrigin(CapsuleLoc, this);
			//ServerData->PendingAdjustment.NewRot = BaseVRCharacterOwner->GetVRRotation();
		}
		else
		{
			ServerData->PendingAdjustment.NewLoc = FRepMovement::RebaseOntoZeroOrigin(ServerLoc, this);
			//ServerData->PendingAdjustment.NewRot = UpdatedComponent->GetComponentRotation();
		}

		ServerData->PendingAdjustment.bBaseRelativePosition = (bDeferServerCorrectionsWhenFalling && bUseLastBase) || MovementBaseUtility::UseRelativeLocation(MovementBase);
		ServerData->PendingAdjustment.bBaseRelativeVelocity = false;

		// Relative location?
		if (ServerData->PendingAdjustment.bBaseRelativePosition)
		{
			if (bDeferServerCorrectionsWhenFalling && bUseLastBase)
			{
				ServerData->PendingAdjustment.NewVel = RelativeVelocity;
				ServerData->PendingAdjustment.NewBase = LastServerMovementBaseVR.Get();
				ServerData->PendingAdjustment.NewBaseBoneName = LastServerMovementBaseBoneName;

				if (bRunClientCorrectionToHMD && IsValid(BaseVRCharacterOwner))
				{	
					FBasedMovementInfo BaseInfo = CharacterOwner->GetBasedMovement();
					FVector BaseLocation;
					FQuat BaseQuat;

					const bool bResult = MovementBaseUtility::GetMovementBaseTransform(BaseInfo.MovementBase, BaseInfo.BoneName, BaseLocation, BaseQuat);
					if (bResult)
					{
						ServerData->PendingAdjustment.NewLoc = FTransform(BaseQuat, BaseLocation).InverseTransformPositionNoScale(CharacterOwner->GetBasedMovement().Location);
					}
					else
					{
						ServerData->PendingAdjustment.NewLoc = RelativeLocation;
					}
				}
				else
				{
					ServerData->PendingAdjustment.NewLoc = RelativeLocation;
				}
			}
			else
			{
				if (bRunClientCorrectionToHMD && IsValid(BaseVRCharacterOwner))
				{
					ServerData->PendingAdjustment.NewLoc = CharacterOwner->GetMovementBase()->GetComponentTransform().InverseTransformPosition(ServerData->PendingAdjustment.NewLoc);
				}
				else
				{
					ServerData->PendingAdjustment.NewLoc = CharacterOwner->GetBasedMovement().Location;
				}

				static const auto CVarNetUseBaseRelativeVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetUseBaseRelativeVelocity"));
				if (CVarNetUseBaseRelativeVelocity->GetInt())
				{
					// Store world velocity converted to local space of movement base
					ServerData->PendingAdjustment.bBaseRelativeVelocity = true;
					const FVector CurrentVelocity = ServerData->PendingAdjustment.NewVel;
					MovementBaseUtility::TransformDirectionToLocal(MovementBase, MovementBaseBoneName, CurrentVelocity, ServerData->PendingAdjustment.NewVel);
				}
			}

			// TODO: this could be a relative rotation, but all client corrections ignore rotation right now except the root motion one, which would need to be updated.
			//ServerData->PendingAdjustment.NewRot = CharacterOwner->GetBasedMovement().Rotation;
		}

#if !UE_BUILD_SHIPPING
		static const auto CVarNetShowCorrections = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetShowCorrections"));
		static const auto CVarNetCorrectionLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetCorrectionLifetime"));
		if (CVarNetShowCorrections->GetInt() != 0)
		{
			const FVector LocDiff = UpdatedComponent->GetComponentLocation() - ClientLoc;
			const FString BaseString = MovementBase ? MovementBase->GetPathName(MovementBase->GetOutermost()) : TEXT("None");
			UE_LOG(LogVRCharacterMovement, Warning, TEXT("*** Server: Error for %s at Time=%.3f is %3.3f LocDiff(%s) ClientLoc(%s) ServerLoc(%s) Base: %s Bone: %s Accel(%s) Velocity(%s)"),
				*GetNameSafe(CharacterOwner), ClientTimeStamp, LocDiff.Size(), *LocDiff.ToString(), *ClientLoc.ToString(), *UpdatedComponent->GetComponentLocation().ToString(), *BaseString, *ServerData->PendingAdjustment.NewBaseBoneName.ToString(), *Accel.ToString(), *Velocity.ToString());
			const float DebugLifetime = CVarNetCorrectionLifetime->GetFloat();
			DrawDebugCapsule(GetWorld(), UpdatedComponent->GetComponentLocation(), CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), FQuat::Identity, FColor(100, 255, 100), false, DebugLifetime);
			DrawDebugCapsule(GetWorld(), ClientLoc, CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), FQuat::Identity, FColor(255, 100, 100), false, DebugLifetime);
		}
#endif

		ServerData->LastUpdateTime = GetWorld()->TimeSeconds;
		ServerData->PendingAdjustment.DeltaTime = DeltaTime;
		ServerData->PendingAdjustment.TimeStamp = ClientTimeStamp;
		ServerData->PendingAdjustment.bAckGoodMove = false;
		ServerData->PendingAdjustment.MovementMode = PackNetworkMovementMode();

		//PerfCountersIncrement(PerfCounter_NumServerMoveCorrections);
	}
	else
	{
		if (bInClientAuthoritativeMovementMode || ServerShouldUseAuthoritativePosition(ClientTimeStamp, DeltaTime, Accel, ClientLoc, RelativeClientLoc, ClientMovementBase, ClientBaseBoneName, ClientMovementMode))
		{
			const FVector LocDiff = UpdatedComponent->GetComponentLocation() - ClientLoc; //-V595
			if (!LocDiff.IsZero() || ClientMovementMode != PackNetworkMovementMode() || GetMovementBase() != ClientMovementBase || (CharacterOwner && CharacterOwner->GetBasedMovement().BoneName != ClientBaseBoneName))
			{
				// Just set the position. On subsequent moves we will resolve initially overlapping conditions.
				UpdatedComponent->SetWorldLocation(ClientLoc, false); //-V595

																	  // Trust the client's movement mode.
				ApplyNetworkMovementMode(ClientMovementMode);

				// Update base and floor at new location.
				SetBase(ClientMovementBase, ClientBaseBoneName);
				UpdateFloorFromAdjustment();

				// Even if base has not changed, we need to recompute the relative offsets (since we've moved).
				SaveBaseLocation();

				LastUpdateLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
				LastUpdateRotation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;
				LastUpdateVelocity = Velocity;
			}
		}

		// acknowledge receipt of this successful servermove()
		ServerData->PendingAdjustment.TimeStamp = ClientTimeStamp;
		ServerData->PendingAdjustment.bAckGoodMove = true;
	}

	//PerfCountersIncrement(PerfCounter_NumServerMoves);

	ServerData->bForceClientUpdate = false;

	LastServerMovementBaseVR = MovementBase;
	LastServerMovementBaseBoneName = MovementBaseBoneName;
	bLastClientIsFalling = bClientIsFalling;
	bLastServerIsFalling = bServerIsFalling;
	bLastServerIsWalking = MovementMode == MOVE_Walking;
}

bool UVRCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
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

	// Scope these, they nest with Outer references so it should work fine, this keeps the update rotation and move autonomous from double updating the char
	//const FScopedPreventAttachedComponentMove PreventMeshMove(BaseVRCharacterOwner ? BaseVRCharacterOwner->NetSmoother : nullptr);

	// Store out our custom properties to restore after replaying
	const FVRMoveActionArray Orig_MoveActions = MoveActionArray;
	const FVector Orig_CustomInput = CustomVRInputVector;
	const EVRConjoinedMovementModes Orig_VRReplicatedMovementMode = VRReplicatedMovementMode;
	const FVector Orig_RequestedVelocity = RequestedVelocity;
	const bool Orig_HasRequestedVelocity = HasRequestedVelocity();

	// Store our VRchar custom properties
	FVector Orig_CameraLoc = VRRootCapsule->curCameraLoc;
	FRotator Orig_curCameraRot = VRRootCapsule->curCameraRot;
	FVector Orig_DifferenceFromLastFrame = VRRootCapsule->DifferenceFromLastFrame;
	FVector Orig_AdditionalVRInputVector = AdditionalVRInputVector;
	FRotator Orig_CameraRotOffset = VRRootCapsule->StoredCameraRotOffset;

	// Replay moves that have not yet been acked.
	UE_LOG(LogNetPlayerMovement, Verbose, TEXT("ClientUpdatePositionAfterServerUpdate Replaying %d Moves, starting at Timestamp %f"), ClientData->SavedMoves.Num(), ClientData->SavedMoves[0]->TimeStamp);
	for (int32 i = 0; i < ClientData->SavedMoves.Num(); i++)
	{
		FSavedMove_Character* const CurrentMove = ClientData->SavedMoves[i].Get();
		checkSlow(CurrentMove != nullptr);

		// Make current SavedMove accessible to any functions that might need it.PrepMoveFor
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

	// Restore our custom properties
	MoveActionArray = Orig_MoveActions;
	CustomVRInputVector = Orig_CustomInput;
	VRReplicatedMovementMode = Orig_VRReplicatedMovementMode;
	RequestedVelocity = Orig_RequestedVelocity;
	SetHasRequestedVelocity(Orig_HasRequestedVelocity);

	// Restore our VRchar custom properties
	VRRootCapsule->curCameraLoc = Orig_CameraLoc;
	VRRootCapsule->curCameraRot = Orig_curCameraRot;
	VRRootCapsule->DifferenceFromLastFrame = Orig_DifferenceFromLastFrame;
	AdditionalVRInputVector = Orig_AdditionalVRInputVector;
	VRRootCapsule->StoredCameraRotOffset = Orig_CameraRotOffset;
	VRRootCapsule->GenerateOffsetToWorld(false, false);

	return (ClientData->SavedMoves.Num() > 0);
}

FVector UVRCharacterMovementComponent::GetPenetrationAdjustment(const FHitResult& Hit) const
{
	// This checks for a walking collision override on the penetrated object
	// If found then it stops penetration adjustments.
	if (MovementMode == EMovementMode::MOVE_Walking && VRRootCapsule && VRRootCapsule->bUseWalkingCollisionOverride && Hit.Component.IsValid())
	{
		ECollisionResponse WalkingResponse;
		WalkingResponse = Hit.Component->GetCollisionResponseToChannel(VRRootCapsule->WalkingCollisionOverride);

		if (WalkingResponse == ECR_Ignore || WalkingResponse == ECR_Overlap)
		{
			return FVector::ZeroVector;
		}
	}

	FVector Result = Super::GetPenetrationAdjustment(Hit);

	if (CharacterOwner)
	{
		const bool bIsProxy = (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy);
		float MaxDistance = bIsProxy ? MaxDepenetrationWithGeometryAsProxy : MaxDepenetrationWithGeometry;
		if (Hit.HitObjectHandle.DoesRepresentClass(APawn::StaticClass()))
		{
			MaxDistance = bIsProxy ? MaxDepenetrationWithPawnAsProxy : MaxDepenetrationWithPawn;
		}

		Result = Result.GetClampedToMaxSize(MaxDistance);
	}

	return Result;
}