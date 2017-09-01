// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "VRBaseCharacterMovementComponent.generated.h"


/** Shared pointer for easy memory management of FSavedMove_Character, for accumulating and replaying network moves. */
//typedef TSharedPtr<class FSavedMove_Character> FSavedMovePtr;


//=============================================================================
/**
 * VRSimpleCharacterMovementComponent handles movement logic for the associated Character owner.
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
class VREXPANSIONPLUGIN_API UVRBaseCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:

	UVRBaseCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void PerformMovement(float DeltaSeconds) override;

	FORCEINLINE bool HasRequestedVelocity()
	{
		return bHasRequestedVelocity;
	}

	void SetHasRequestedVelocity(bool bNewHasRequestedVelocity)
	{
		bHasRequestedVelocity = bNewHasRequestedVelocity;
	}

	bool IsLocallyControlled() const
	{
		// I like epics implementation better than my own
		const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
	}

	// Sets the crouching half height since it isn't exposed during runtime to blueprints
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void SetCrouchedHalfHeight(float NewCrouchedHalfHeight);

	// Setting this higher will divide the wall slide effect by this value, to reduce collision sliding.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement", meta = (ClampMin = "0.0", UIMin = "0", ClampMax = "5.0", UIMax = "5"))
		float VRWallSlideScaler;

	/** Custom version of SlideAlongSurface that handles different movement modes separately; namely during walking physics we might not want to slide up slopes. */
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact) override;

	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacterMovementComponent|VRLocations")
		void AddCustomReplicatedMovement(FVector Movement);

	FVector CustomVRInputVector;
	FVector AdditionalVRInputVector;

	// Injecting custom movement in here, bypasses floor detection
	//virtual void PerformMovement(float DeltaSeconds) override;

	//inline void ApplyVRMotionToVelocity(float deltaTime);
	//inline  void RestorePreAdditiveVRMotionVelocity();
	FVector LastPreAdditiveVRVelocity;

	inline void UVRBaseCharacterMovementComponent::ApplyVRMotionToVelocity(float deltaTime)
	{
		if (AdditionalVRInputVector.IsNearlyZero())
		{
			LastPreAdditiveVRVelocity = FVector::ZeroVector;
			return;
		}

		LastPreAdditiveVRVelocity = (AdditionalVRInputVector) / deltaTime;// Velocity; // Save off pre-additive Velocity for restoration next tick	
		Velocity += LastPreAdditiveVRVelocity;

		// Switch to Falling if we have vertical velocity from root motion so we can lift off the ground
		if (!LastPreAdditiveVRVelocity.IsNearlyZero() && LastPreAdditiveVRVelocity.Z != 0.f && IsMovingOnGround())
		{
			float LiftoffBound;
			// Default bounds - the amount of force gravity is applying this tick
			LiftoffBound = FMath::Max(GetGravityZ() * deltaTime, SMALL_NUMBER);

			if (LastPreAdditiveVRVelocity.Z > LiftoffBound)
			{
				SetMovementMode(MOVE_Falling);
			}
		}
	}

	inline void UVRBaseCharacterMovementComponent::RestorePreAdditiveVRMotionVelocity()
	{
		Velocity -= LastPreAdditiveVRVelocity;
		LastPreAdditiveVRVelocity = FVector::ZeroVector;
	}

	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void PhysCustom_Climbing(float deltaTime, int32 Iterations);
	virtual void PhysCustom_LowGrav(float deltaTime, int32 Iterations);

	// Added in 4.16
	///* Allow custom handling when character hits a wall while swimming. */
	//virtual void HandleSwimmingWallHit(const FHitResult& Hit, float DeltaTime);

	// If true will never count a simulating component as the floor, to prevent jitter / physics problems.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool bIgnoreSimulatingComponentsInFloorCheck;

	// Option to Skip simulating components when looking for floor
	virtual bool FloorSweepTest(
		FHitResult& OutHit,
		const FVector& Start,
		const FVector& End,
		ECollisionChannel TraceChannel,
		const struct FCollisionShape& CollisionShape,
		const struct FCollisionQueryParams& Params,
		const struct FCollisionResponseParams& ResponseParam
	) const override;

	// Need to use actual capsule location for step up
	virtual bool VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult = nullptr);

	// Height to auto step up
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepHeight;

	/* Custom distance that is required before accepting a climbing stepup
	*  This is to help with cases where head wobble causes falling backwards
	*  Do NOT set to larger than capsule radius!
	*  #TODO: Port to SimpleCharacter as well
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingEdgeRejectDistance;

	// Higher values make it easier to trigger a step up onto a platform and moves you farther in to the base *DEFUNCT*
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepUpMultiplier;

	// If true will automatically set falling when a stepup occurs during climbing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		bool SetDefaultPostClimbMovementOnStepUp;

	// Max velocity on releasing a climbing grip
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingMaxReleaseVelocitySize;

	// If true will replicate the capsule height on to clients, allows for dynamic capsule height changes in multiplayer
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool VRReplicateCapsuleHeight;

	UFUNCTION(BlueprintCallable, Category = "VRMovement|Climbing")
		void SetClimbingMode(bool bIsClimbing);

	// Default movement mode to switch to post climb ended, only used if SetDefaultPostClimbMovementOnStepUp is true
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		EVRConjoinedMovementModes DefaultPostClimbMovement;

	/*
	* This is called client side to make a replicated movement mode change that hits the server in the saved move.
	*
	* Custom Movement Mode is currently limited to 0 - 8, the index's 0 and 1 are currently used up for the plugin movement modes.
	* So setting it to 0 or 1 would be Climbing, and LowGrav respectivly, this leaves 2-8 as open index's for use.
	* For a total of 6 Custom movement modes past the currently implemented plugin ones.
	*/
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void SetReplicatedMovementMode(EVRConjoinedMovementModes NewMovementMode);

	// We use 4 bits for this so a maximum of 16 elements
	EVRConjoinedMovementModes VRReplicatedMovementMode;

	void UpdateFromCompressedFlags(uint8 Flags) override
	{
		// If is a custom or VR custom movement mode
		int32 MovementFlags = (Flags >> 2) & 15;
		VRReplicatedMovementMode = (EVRConjoinedMovementModes)MovementFlags;

		Super::UpdateFromCompressedFlags(Flags);
	}

	FVector RoundDirectMovement(FVector InMovement) const
	{
		// Match FVector_NetQuantize100 (2 decimal place of precision).
		InMovement.X = FMath::RoundToFloat(InMovement.X * 100.f) / 100.f;
		InMovement.Y = FMath::RoundToFloat(InMovement.Y * 100.f) / 100.f;
		InMovement.Z = FMath::RoundToFloat(InMovement.Z * 100.f) / 100.f;
		return InMovement;
	}

	// Setting this below 1.0 will change how fast you de-accelerate when touching a wall
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|LowGrav", meta = (ClampMin = "0.0", UIMin = "0", ClampMax = "5.0", UIMax = "5"))
		float VRLowGravWallFrictionScaler;

	// If true then low grav will ignore the default physics volume fluid friction, useful if you have a mix of low grav and normal movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|LowGrav")
		bool VRLowGravIgnoresDefaultFluidFriction;

	/** Replicate position correction to client, associated with a timestamped servermove.  Client will replay subsequent moves after applying adjustment.  */
	UFUNCTION(unreliable, client)
		virtual void ClientAdjustPosition(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override
	{
		//this->CustomVRInputVector = FVector::ZeroVector;

		Super::ClientAdjustPosition_Implementation(TimeStamp, NewLoc, NewVel, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
	}

	/* Bandwidth saving version, when velocity is zeroed */
	UFUNCTION(unreliable, client)
	virtual void ClientVeryShortAdjustPosition(float TimeStamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void ClientVeryShortAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override
	{
		//this->CustomVRInputVector = FVector::ZeroVector;

		Super::ClientVeryShortAdjustPosition_Implementation(TimeStamp, NewLoc, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
	}
};


class VREXPANSIONPLUGIN_API FSavedMove_VRBaseCharacter : public FSavedMove_Character
{

public:

	EVRConjoinedMovementModes VRReplicatedMovementMode;

	FVector CustomVRInputVector;
	FVector VRCapsuleLocation;
	FVector LFDiff;
	FRotator VRCapsuleRotation;
	FVector RequestedVelocity;

	void Clear();
	virtual void SetInitialPosition(ACharacter* C);

	FSavedMove_VRBaseCharacter() : FSavedMove_Character()
	{
		CustomVRInputVector = FVector::ZeroVector;

		VRCapsuleLocation = FVector::ZeroVector;
		LFDiff = FVector::ZeroVector;
		VRCapsuleRotation = FRotator::ZeroRotator;
		RequestedVelocity = FVector::ZeroVector;
		VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;// _None;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		// Fills in 01 and 02 for Jump / Crouch
		uint8 Result = FSavedMove_Character::GetCompressedFlags();

		// Not supporting custom movement mode directly at this time by replicating custom index
		// We use 4 bits for this so a maximum of 16 elements
		Result |= (uint8)VRReplicatedMovementMode << 2;

		// Reserved_1, and Reserved_2, Flag_Custom_0 and Flag_Custom_1 are used up
		// By the VRReplicatedMovementMode packing
		// Only custom_2 and custom_3 are left

		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		FSavedMove_VRBaseCharacter * nMove = (FSavedMove_VRBaseCharacter *)NewMove.Get();


		if (!nMove || (VRReplicatedMovementMode != nMove->VRReplicatedMovementMode))
			return false;

		if (!CustomVRInputVector.IsZero() || !nMove->CustomVRInputVector.IsZero())
			return false;

		if (!RequestedVelocity.IsZero() || !nMove->RequestedVelocity.IsZero())
			return false;

		// Hate this but we really can't combine if I am sending a new capsule height
		if (!FMath::IsNearlyEqual(LFDiff.Z, nMove->LFDiff.Z))
			return false;
	
		if (!LFDiff.IsZero() && !nMove->LFDiff.IsZero() && !FVector::Coincident(LFDiff.GetSafeNormal2D(), nMove->LFDiff.GetSafeNormal2D(), AccelDotThresholdCombine))
			return false;

		return FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}


	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override
	{
		// Auto important if toggled climbing
		if (VRReplicatedMovementMode != EVRConjoinedMovementModes::C_MOVE_MAX)//_None)
			return true;

		if (!CustomVRInputVector.IsZero())
			return true;

		if (!RequestedVelocity.IsZero())
			return true;

		// #TODO: What to do here?
		// This is debatable, however it will ALWAYS be non zero realistically and only really effects step ups for the most part
		//if (!LFDiff.IsNearlyZero())
			//return true;

		// Else check parent class
		return FSavedMove_Character::IsImportantMove(LastAckedMove);
	}

	virtual void PrepMoveFor(ACharacter* Character) override;
};

