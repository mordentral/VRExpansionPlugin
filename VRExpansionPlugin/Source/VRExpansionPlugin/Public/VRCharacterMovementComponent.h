// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "AI/Navigation/NavigationAvoidanceTypes.h"
#include "AI/RVOAvoidanceInterface.h"
#include "AITypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/Navigation/NavigationSystem.h"
#include "Animation/AnimationAsset.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Interfaces/NetworkPredictionInterface.h"
#include "WorldCollision.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRCharacterMovementComponent.generated.h"

class FDebugDisplayInfo;
class ACharacter;
class UVRCharacterMovementComponent;
class AVRCharacter;

/** Shared pointer for easy memory management of FSavedMove_Character, for accumulating and replaying network moves. */
//typedef TSharedPtr<class FSavedMove_Character> FSavedMovePtr;


//=============================================================================
/**
 * VRCharacterMovementComponent handles movement logic for the associated Character owner.
 * It supports various movement modes including: walking, falling, swimming, flying, custom.
 *
 * Movement is affected primarily by current Velocity and Acceleration. Acceleration is updated each frame
 * based on the input vector accumulated thus far (see UPawnMovementComponent::GetPendingInputVector()).
 *
 * Networking is fully implemented, with server-client correction and prediction included.
 *
 * @see ACharacter, UPawnMovementComponent
 * @see https://docs.unrealengine.com/latest/INT/Gameplay/Framework/Pawn/Character/
 */

//DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAIMoveCompletedSignature, FAIRequestID, RequestID, EPathFollowingResult::Type, Result);


UCLASS()
class VREXPANSIONPLUGIN_API UVRCharacterMovementComponent : public UVRBaseCharacterMovementComponent
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadOnly, Transient, Category = VRMovement)
	UVRRootComponent * VRRootCapsule;

	//UPROPERTY(BlueprintReadOnly, Transient, Category = VRMovement)
	//UCapsuleComponent * VRCameraCollider;

	// Removing, it makes less sense now with optional secondary collision settings
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRCharacterMovementComponent")
	//bool bAllowWalkingThroughWalls;

	virtual bool VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult = nullptr) override;

	// Allow merging movement replication (may cause issues when >10 players due to capsule location
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRCharacterMovementComponent")
	bool bAllowMovementMerging;

	// Higher values will cause more slide but better step up
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRCharacterMovementComponent", meta = (ClampMin = "0.01", UIMin = "0", ClampMax = "1.0", UIMax = "1"))
	float WallRepulsionMultiplier;

	/*FORCEINLINE bool HasRequestedVelocity()
	{
		return bHasRequestedVelocity;
	}*/

	/*UFUNCTION(BlueprintCallable, Category = "SimpleVRCharacterMovementComponent|VRLocations")
		void AddCustomReplicatedMovement(FVector Movement);

	FVector CustomVRInputVector;
	
	// Whether the custom replicated movement should be swept or not
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRCharacterMovementComponent")
		bool bSweepCustomReplicatedMovement;
	
	virtual void PerformMovement(float DeltaSeconds) override;
	*/

	///////////////////////////
	// Navigation Functions
	///////////////////////////

	/** Blueprint notification that we've completed the current movement request */
	//UPROPERTY(BlueprintAssignable, meta = (DisplayName = "MoveCompleted"))
	//FAIMoveCompletedSignature ReceiveMoveCompleted;

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);

	/**
	* Checks to see if the current location is not encroaching blocking geometry so the character can leave NavWalking.
	* Restores collision settings and adjusts character location to avoid getting stuck in geometry.
	* If it's not possible, MovementMode change will be delayed until character reach collision free spot.
	* @return True if movement mode was successfully changed
	*/
	virtual bool TryToLeaveNavWalking() override;
	
	
	virtual void PhysNavWalking(float deltaTime, int32 Iterations) override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;

	FORCEINLINE FVector GetActorFeetLocation() const { return VRRootCapsule ? (VRRootCapsule->OffsetComponentToWorld.GetLocation() - FVector(0, 0, UpdatedComponent->Bounds.BoxExtent.Z)) : UpdatedComponent ? (UpdatedComponent->GetComponentLocation() - FVector(0, 0, UpdatedComponent->Bounds.BoxExtent.Z)) : FNavigationSystem::InvalidLocation; }
	virtual FBasedPosition GetActorFeetLocationBased() const override
	{
		return FBasedPosition(NULL, GetActorFeetLocation());
	}

	///////////////////////////
	// End Navigation Functions
	///////////////////////////
	

	///////////////////////////
	// Replication Functions
	///////////////////////////
	void CallServerMoveVR(const class FSavedMove_VRCharacter* NewMove, const class FSavedMove_VRCharacter* OldMove);

	/** Replicated function sent by client to server - contains client movement and view info. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVR(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveVR_Implementation(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveVR_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDual(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, FVector_NetQuantize100 OldCustVRInputVector, uint8 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveVRDual_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, FVector_NetQuantize100 OldCustVRInputVector, uint8 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, FVector_NetQuantize100 OldCustVRInputVector, uint8 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. First move is non root motion, second is root motion. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDualHybridRootMotion(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc,FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, FVector_NetQuantize100 OldCustVRInputVector, uint8 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveVRDualHybridRootMotion_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, FVector_NetQuantize100 OldCustVRInputVector, uint8 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, FVector_NetQuantize100 OldCustVRInputVector, uint8 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, FVector_NetQuantize100 CustVRInputVector, uint8 CapsuleYaw, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);


	FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	FNetworkPredictionData_Server* GetPredictionData_Server() const override;

	///////////////////////////
	// End Replication Functions
	///////////////////////////

	/**
	 * Default UObject constructor.
	 */
	UVRCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	float ImmersionDepth() const override;
	bool CanCrouch();

	// Don't really need to override this at all, it doesn't work that well even when fixed in VR
	//void VisualizeMovement() const override;


	/*
	bool HasRootMotion() const
	{
		return RootMotionParams.bHasRootMotion;
	}*/

	// Had to modify this, since every frame can start in penetration (capsule component moves into wall before movement tick)
	// It was throwing out the initial hit and not calling "step up", now I am only checking for penetration after adjustment but keeping the initial hit for step up.
	// this makes for FAR more responsive step ups.
	// I WILL need to override these for flying / swimming as well
	bool SafeMoveUpdatedComponent(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport = ETeleportType::None);
	bool SafeMoveUpdatedComponent(const FVector& Delta, const FRotator& NewRotation, bool bSweep, FHitResult& OutHit, ETeleportType Teleport = ETeleportType::None);
	
	// This is here to force it to call the correct SafeMoveUpdatedComponent functions for floor movement
	virtual void MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds, FStepDownResult* OutStepDownResult) override;

	// Modify for correct location
	virtual void ApplyRepulsionForce(float DeltaSeconds) override;

	// Update BaseOffset to be zero
	virtual void UpdateBasedMovement(float DeltaSeconds) override;

	// Stop subtracting the capsules half height
	virtual FVector GetImpartedMovementBaseVelocity() const override;

	// Cheating at the relative collision detection
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction);

	// Need to fill our capsule component variable here and override the default tick ordering
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	// Correct an offset sweep test
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	// Always called with the capsulecomponent location, no idea why it doesn't just get it inside it already
	// Had to force it within the function to use VRLocation instead.
	virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bZeroDelta, const FHitResult* DownwardSweepResult) const override;

	// Need to use actual capsule location for step up
	bool StepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult);

	// Skip physics channels when looking for floor
	virtual bool FloorSweepTest(
		FHitResult& OutHit,
		const FVector& Start,
		const FVector& End,
		ECollisionChannel TraceChannel,
		const struct FCollisionShape& CollisionShape,
		const struct FCollisionQueryParams& Params,
		const struct FCollisionResponseParams& ResponseParam
		) const override;

	// Multiple changes to support relative motion and ledge sweeps
	virtual void PhysWalking(float deltaTime, int32 Iterations) override;
	

	// Supporting the direct move injection
	virtual void PhysFlying(float deltaTime, int32 Iterations) override;
	
	// Need to use VR location, was defaulting to actor
	virtual bool ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const override;

	// Overriding the physfalling because valid landing spots were computed incorrectly.
	virtual void PhysFalling(float deltaTime, int32 Iterations) override;

	// Just calls find floor
	/** Verify that the supplied hit result is a valid landing spot when falling. */
	//virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;

	// Making sure that impulses are correct
	virtual void CapsuleTouched(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult) override;
};


class VREXPANSIONPLUGIN_API FSavedMove_VRCharacter : public FSavedMove_VRBaseCharacter
{

public:

	FVector VRCapsuleLocation;
	FVector LFDiff;
	FRotator VRCapsuleRotation;
	FVector RequestedVelocity;
	FVector AdditionalInputVector;

	void Clear();
	virtual void SetInitialPosition(ACharacter* C);

	FSavedMove_VRCharacter() : FSavedMove_VRBaseCharacter()
	{
		VRCapsuleLocation = FVector::ZeroVector;
		LFDiff = FVector::ZeroVector;
		AdditionalInputVector = FVector::ZeroVector;
		VRCapsuleRotation = FRotator::ZeroRotator;
		RequestedVelocity = FVector::ZeroVector;
	}

	bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		FSavedMove_VRCharacter * nMove = (FSavedMove_VRCharacter *)NewMove.Get();

		if (!nMove || (!LFDiff.IsNearlyZero() && !nMove->LFDiff.IsNearlyZero()) || (!RequestedVelocity.IsNearlyZero() && !nMove->RequestedVelocity.IsNearlyZero()) || (!AdditionalInputVector.IsNearlyZero() && !nMove->AdditionalInputVector.IsNearlyZero()))
			return false;
		
		return FSavedMove_VRBaseCharacter::CanCombineWith(NewMove, Character, MaxDelta);
	}

};

// Need this for capsule location replication
class VREXPANSIONPLUGIN_API FNetworkPredictionData_Client_VRCharacter : public FNetworkPredictionData_Client_Character
{
public:
	FNetworkPredictionData_Client_VRCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{

	}

	FSavedMovePtr AllocateNewMove()
	{
		return FSavedMovePtr(new FSavedMove_VRCharacter());
	}
};


// Need this for capsule location replication?????
class VREXPANSIONPLUGIN_API FNetworkPredictionData_Server_VRCharacter : public FNetworkPredictionData_Server_Character
{
public:
	FNetworkPredictionData_Server_VRCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Server_Character(ClientMovement)
	{

	}

	FSavedMovePtr AllocateNewMove()
	{
		return FSavedMovePtr(new FSavedMove_VRCharacter());
	}
};