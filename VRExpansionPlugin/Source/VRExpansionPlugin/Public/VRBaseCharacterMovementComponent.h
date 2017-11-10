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
UENUM(Blueprintable)
enum class EVRMoveAction : uint8
{
	VRMOVEACTION_None = 0x00,
	VRMOVEACTION_SnapTurn = 0x01,
	VRMOVEACTION_Teleport = 0x02,
	VRMOVEACTION_Reserved1 = 0x03,
	VRMOVEACTION_Reserved2 = 0x04,
	VRMOVEACTION_CUSTOM1 = 0x05,
	VRMOVEACTION_CUSTOM2 = 0x06,
	VRMOVEACTION_CUSTOM3 = 0x07,
	VRMOVEACTION_CUSTOM4 = 0x08,
	VRMOVEACTION_CUSTOM5 = 0x09,
	VRMOVEACTION_CUSTOM6 = 0x0A,
	VRMOVEACTION_CUSTOM7 = 0x0B,
	VRMOVEACTION_CUSTOM8 = 0x0C,
	VRMOVEACTION_CUSTOM9 = 0x0D,
	VRMOVEACTION_CUSTOM10 = 0x0E,
};

//DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAIMoveCompletedSignature, FAIRequestID, RequestID, EPathFollowingResult::Type, Result);
USTRUCT()
struct VREXPANSIONPLUGIN_API FVRConditionalMoveRep
{
	GENERATED_USTRUCT_BODY()
public:

	UPROPERTY(Transient)
		FVector CustomVRInputVector;
	UPROPERTY(Transient)
		FVector RequestedVelocity;
	UPROPERTY(Transient)
		FVector MoveActionLoc;
	UPROPERTY(Transient)
		FRotator MoveActionRot;
	UPROPERTY(Transient)
		EVRMoveAction MoveAction;

	FVRConditionalMoveRep()
	{
		CustomVRInputVector = FVector::ZeroVector;
		RequestedVelocity = FVector::ZeroVector;
		MoveActionLoc = FVector::ZeroVector;
		MoveActionRot = FRotator::ZeroRotator;
		MoveAction = EVRMoveAction::VRMOVEACTION_None;
	}

	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		bool bHasVRinput = !CustomVRInputVector.IsZero();
		bool bHasRequestedVelocity = !RequestedVelocity.IsZero();
		bool bHasMoveAction = MoveAction != EVRMoveAction::VRMOVEACTION_None;

		// Defines the level of Quantization

		bool bHasAnyProperties = bHasVRinput || bHasRequestedVelocity || bHasMoveAction;
		Ar.SerializeBits(&bHasAnyProperties, 1);

		if (bHasAnyProperties)
		{
			Ar.SerializeBits(&bHasVRinput, 1);
			Ar.SerializeBits(&bHasRequestedVelocity, 1);
			Ar.SerializeBits(&bHasMoveAction, 1);

			if (bHasVRinput)
				bOutSuccess &= SerializePackedVector<100, 30>(CustomVRInputVector, Ar);

			if (bHasRequestedVelocity)
				bOutSuccess &= SerializePackedVector<100, 30>(RequestedVelocity, Ar);

			if (bHasMoveAction)
			{
				Ar.SerializeBits(&MoveAction, 4); // 16 elements, only allowing 1 per frame, they aren't flags

				switch (MoveAction)
				{
				case EVRMoveAction::VRMOVEACTION_None: break;
				case EVRMoveAction::VRMOVEACTION_SnapTurn:
				case EVRMoveAction::VRMOVEACTION_Teleport: // Not replicating rot as Control rot does that already
				{
					bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);
				}break;
				case EVRMoveAction::VRMOVEACTION_Reserved1:
				case EVRMoveAction::VRMOVEACTION_Reserved2:
				{}break;
				default: // Everything else
				{
					// Customs could use both rot and loc, so rep both
					bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);
					MoveActionRot.SerializeCompressedShort(Ar);
				}break;
				}
			}
		}

		return bOutSuccess;
	}

};

template<>
struct TStructOpsTypeTraits< FVRConditionalMoveRep > : public TStructOpsTypeTraitsBase2<FVRConditionalMoveRep>
{
	enum
	{
		WithNetSerializer = true
	};
};

class VREXPANSIONPLUGIN_API FSavedMove_VRBaseCharacter : public FSavedMove_Character
{

public:

	// Bit masks used by GetCompressedFlags() to encode movement information.
	/*enum CustomVRCompressedFlags
	{
		//FLAG_JumpPressed = 0x01,	// Jump pressed
		//FLAG_WantsToCrouch = 0x02,	// Wants to crouch
		//FLAG_Reserved_1 = 0x04,	// Reserved for future use
		//FLAG_Reserved_2 = 0x08,	// Reserved for future use
		// Remaining bit masks are available for custom flags.
		//FLAG_Custom_0 = 0x10,
		//FLAG_Custom_1 = 0x20,
		//FLAG_MoveAction = 0x40,//FLAG_Custom_2 = 0x40,
		//FLAG_SnapTurnRight = 0x80,
		//FLAG_Custom_3 = 0x80,
	};*/

	EVRConjoinedMovementModes VRReplicatedMovementMode;

	FVector VRCapsuleLocation;
	FVector LFDiff;
	FVector SnapTurnOffset;
	FRotator VRCapsuleRotation;
	FVRConditionalMoveRep ConditionalValues;

	void Clear();
	virtual void SetInitialPosition(ACharacter* C);

	FSavedMove_VRBaseCharacter() : FSavedMove_Character()
	{
		VRCapsuleLocation = FVector::ZeroVector;
		LFDiff = FVector::ZeroVector;
		VRCapsuleRotation = FRotator::ZeroRotator;
		VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;// _None;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		// Fills in 01 and 02 for Jump / Crouch
		uint8 Result = FSavedMove_Character::GetCompressedFlags();

		// Not supporting custom movement mode directly at this time by replicating custom index
		// We use 4 bits for this so a maximum of 16 elements
		Result |= (uint8)VRReplicatedMovementMode << 2;

		// This takes up custom_2
		/*if (bWantsToSnapTurn)
		{
			Result |= FLAG_SnapTurn;
		}*/

		// Reserved_1, and Reserved_2, Flag_Custom_0 and Flag_Custom_1 are used up
		// By the VRReplicatedMovementMode packing


		// only custom_2 and custom_3 are left currently
		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		FSavedMove_VRBaseCharacter * nMove = (FSavedMove_VRBaseCharacter *)NewMove.Get();


		if (!nMove || (VRReplicatedMovementMode != nMove->VRReplicatedMovementMode))
			return false;

		if (ConditionalValues.MoveAction != EVRMoveAction::VRMOVEACTION_None || nMove->ConditionalValues.MoveAction != EVRMoveAction::VRMOVEACTION_None)
			return false;

		if (!ConditionalValues.CustomVRInputVector.IsZero() || !nMove->ConditionalValues.CustomVRInputVector.IsZero())
			return false;

		if (!ConditionalValues.RequestedVelocity.IsZero() || !nMove->ConditionalValues.RequestedVelocity.IsZero())
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

		if (!ConditionalValues.CustomVRInputVector.IsZero())
			return true;

		if (!ConditionalValues.RequestedVelocity.IsZero())
			return true;

		if (ConditionalValues.MoveAction != EVRMoveAction::VRMOVEACTION_None)
			return true;

		// #TODO: What to do here?
		// This is debatable, however it will ALWAYS be non zero realistically and only really effects step ups for the most part
		//if (!LFDiff.IsNearlyZero())
		//return true;

		// Else check parent class
		return FSavedMove_Character::IsImportantMove(LastAckedMove);
	}

	virtual void PrepMoveFor(ACharacter* Character) override;

	/** Set the properties describing the final position, etc. of the moved pawn. */
	virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode) override;
};


UCLASS()
class VREXPANSIONPLUGIN_API UVRBaseCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:

	UVRBaseCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void PerformMovement(float DeltaSeconds) override;
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

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

	// Perform a snap turn in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_SnapTurn(float SnapTurnDeltaYaw);

	// Perform a teleport in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_Teleport(FVector TeleportLocation, FRotator TeleportRotation);
	
	// Perform a teleport in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_Custom(EVRMoveAction MoveActionToPerform, FVector MoveActionVector, FRotator MoveActionRotator);

	EVRMoveAction MoveAction;
	FVector MoveActionLoc;
	FRotator MoveActionRot;

	bool bHadMoveActionThisFrame;
	bool CheckForMoveAction();
	bool DoMASnapTurn();
	bool DoMATeleport();

	FVector CustomVRInputVector;
	FVector AdditionalVRInputVector;
	FVector LastPreAdditiveVRVelocity;
	bool bApplyAdditionalVRInputVectorAsNegative;
	
	// Rewind the relative movement that we had with the HMD
	inline void RewindVRRelativeMovement();

	bool bWasInPushBack;
	bool bIsInPushBack;
	void StartPushBackNotification(FHitResult HitResult);
	void EndPushBackNotification();

	//virtual void SendClientAdjustment() override;

	inline void ApplyVRMotionToVelocity(float deltaTime)
	{
		if (AdditionalVRInputVector.IsNearlyZero())
		{
			LastPreAdditiveVRVelocity = FVector::ZeroVector;
			return;
		}
		
		LastPreAdditiveVRVelocity = (AdditionalVRInputVector) / deltaTime;// Velocity; // Save off pre-additive Velocity for restoration next tick	
		Velocity += LastPreAdditiveVRVelocity;
	}

	inline void RestorePreAdditiveVRMotionVelocity()
	{
		Velocity -= LastPreAdditiveVRVelocity;
		LastPreAdditiveVRVelocity = FVector::ZeroVector;
	}

	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void PhysCustom_Climbing(float deltaTime, int32 Iterations);
	virtual void PhysCustom_LowGrav(float deltaTime, int32 Iterations);

	/**
	* Smooth mesh location for network interpolation, based on values set up by SmoothCorrection.
	* Internally this simply calls SmoothClientPosition_Interpolate() then SmoothClientPosition_UpdateVisuals().
	* This function is not called when bNetworkSmoothingComplete is true.
	* @param DeltaSeconds Time since last update.
	*/
	virtual void SmoothClientPosition(float DeltaSeconds) override;

	/** Update mesh location based on interpolated values. */
	void SmoothClientPosition_UpdateVRVisuals();

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

		//bWantsToSnapTurn = ((Flags & FSavedMove_VRBaseCharacter::FLAG_SnapTurn) != 0);

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

