// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRBPDatatypes.h"
#include "AITypes.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "CharacterMovementCompTypes.h"
#include "VRBaseCharacterMovementComponent.generated.h"

class AVRBaseCharacter;

DECLARE_LOG_CATEGORY_EXTERN(LogVRBaseCharacterMovement, Log, All);

/** Delegate for notification when to handle a climbing step up, will override default step up logic if is bound to. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVROnPerformClimbingStepUp, FVector, FinalStepUpLocation);

/*
* The base class for our VR characters, contains common logic across them, not to be used directly
*/
UCLASS()
class VREXPANSIONPLUGIN_API UVRBaseCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:

	UVRBaseCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Default client to server move RPC data container. Can be bypassed via SetNetworkMoveDataContainer(). */
	FVRCharacterNetworkMoveDataContainer VRNetworkMoveDataContainer;
	FVRCharacterMoveResponseDataContainer VRMoveResponseDataContainer;

	bool bNotifyTeleported;

	/** BaseVR Character movement component belongs to */
	UPROPERTY(Transient, DuplicateTransient)
		AVRBaseCharacter* BaseVRCharacterOwner;

	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent);

	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual void PerformMovement(float DeltaSeconds) override;
	//virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	// Overriding this to run the seated logic
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	// Skip force updating position if we are seated.
	virtual bool ForcePositionUpdate(float DeltaTime) override;

	// When true will use the default engines behavior of setting rotation to match the clients instead of simulating rotations, this is really only here for FPS test pawns
	// And non VRCharacter classes (simple character will use this)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRBaseCharacterMovementComponent")
		bool bUseClientControlRotation;

	// When true remote proxies will no longer attempt to estimate player moves when motion smoothing is enabled.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRBaseCharacterMovementComponent|Smoothing")
		bool bDisableSimulatedTickWhenSmoothingMovement;

	// Adding seated transition
	void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	// Called when a valid climbing step up movement is found, if bound to the default auto step up is not performed to let custom step up logic happen instead.
	UPROPERTY(BlueprintAssignable, Category = "VRMovement")
		FVROnPerformClimbingStepUp OnPerformClimbingStepUp;

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);

	// Can't be inline anymore
	FVector GetActorFeetLocationVR() const;

	FORCEINLINE bool HasRequestedVelocity()
	{
		return bHasRequestedVelocity;
	}

	void SetHasRequestedVelocity(bool bNewHasRequestedVelocity);
	bool IsClimbing() const;

	// Sets the crouching half height since it isn't exposed during runtime to blueprints
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void SetCrouchedHalfHeight(float NewCrouchedHalfHeight);

	// Setting this higher will divide the wall slide effect by this value, to reduce collision sliding.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement", meta = (ClampMin = "0.0", UIMin = "0", ClampMax = "5.0", UIMax = "5"))
		float VRWallSlideScaler;

	/** Custom version of SlideAlongSurface that handles different movement modes separately; namely during walking physics we might not want to slide up slopes. */
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact) override;

	// Add in the custom replicated movement that climbing mode uses, this is a cutom vector that is applied to character movements
	// on the next tick as a movement input..
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacterMovementComponent|VRLocations")
		void AddCustomReplicatedMovement(FVector Movement);

	// Clears the custom replicated movement, can be used to cancel movements if the mode changes
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacterMovementComponent|VRLocations")
		void ClearCustomReplicatedMovement();

	// Called to check if the server is performing a move action on a non controlled character
	// If so then we just run the logic right away as it can't be inlined and won't be replicated
	void CheckServerAuthedMoveAction();

	// Set tracking paused for our root capsule and replicate the location to all connections
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_SetTrackingPaused(bool bNewTrackingPaused);
	virtual void StoreSetTrackingPaused(bool bNewTrackingPaused);

	// Perform a snap turn in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_SnapTurn(float SnapTurnDeltaYaw, EVRMoveActionVelocityRetention VelocityRetention = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None, bool bFlagGripTeleport = false, bool bFlagCharacterTeleport = false);

	// Perform a rotation set in line with the move actions system
	// This node specifically sets the FACING direction to a value, where your HMD is pointed
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_SetRotation(float NewYaw, EVRMoveActionVelocityRetention VelocityRetention = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None, bool bFlagGripTeleport = false, bool bFlagCharacterTeleport = false);

	// Perform a teleport in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_Teleport(FVector TeleportLocation, FRotator TeleportRotation, EVRMoveActionVelocityRetention VelocityRetention = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None, bool bSkipEncroachmentCheck = false);

	// Perform StopAllMovementImmediately in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_StopAllMovement();
	
	// Perform a custom moveaction that you define, will call the OnCustomMoveActionPerformed event in the character when processed so you can run your own logic
	// Be sure to set the minimum data replication requirements for your move action in order to save on replication.
	// Flags will always replicate if it is non zero
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_Custom(EVRMoveAction MoveActionToPerform, EVRMoveActionDataReq DataRequirementsForMoveAction, FVector MoveActionVector, FRotator MoveActionRotator, uint8 MoveActionFlags = 0);

	FVRMoveActionArray MoveActionArray;

	bool CheckForMoveAction();
	virtual bool DoMASnapTurn(FVRMoveActionContainer& MoveAction);
	virtual bool DoMASetRotation(FVRMoveActionContainer& MoveAction);
	virtual bool DoMATeleport(FVRMoveActionContainer& MoveAction);
	virtual bool DoMAStopAllMovement(FVRMoveActionContainer& MoveAction);
	virtual bool DoMAPauseTracking(FVRMoveActionContainer& MoveAction);

	FVector CustomVRInputVector;
	FVector AdditionalVRInputVector;
	FVector LastPreAdditiveVRVelocity;
	bool bHadExtremeInput;
	bool bApplyAdditionalVRInputVectorAsNegative;
	
	// Rewind the relative movement that we had with the HMD
	inline void RewindVRRelativeMovement()
	{
		if (bApplyAdditionalVRInputVectorAsNegative)
		{
			//FHitResult AHit;
			MoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false);
			//SafeMoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false, AHit);
		}
	}

	// Any movement above this value we will consider as have been a tracking jump and null out the movement in the character
	// Raise this value higher if players are noticing freezing when moving quickly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement", meta = (ClampMin = "0.0", UIMin = "0"))
		float TrackingLossThreshold;

	// If we hit the tracking loss threshold then rewind position instead of running to the new location
	// Will force the HMD to stay in its original spot prior to the tracking jump
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool bHoldPositionOnTrackingLossThresholdHit;

	// Rewind the relative movement that we had with the HMD, this is exposed to Blueprint so that custom movement modes can use it to rewind prior to movement actions.
	// Returns the Vector required to get back to the original position (for custom movement modes)
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		FVector RewindVRMovement();

	// Gets the current CustomInputVector for use in custom movement modes
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		FVector GetCustomInputVector();

	bool bWasInPushBack;
	bool bIsInPushBack;
	void StartPushBackNotification(FHitResult HitResult);
	void EndPushBackNotification();

	bool bJustUnseated;

	//virtual void SendClientAdjustment() override;

	virtual bool VerifyClientTimeStamp(float TimeStamp, FNetworkPredictionData_Server_Character & ServerData) override;

	inline void ApplyVRMotionToVelocity(float deltaTime)
	{
		bHadExtremeInput = false;

		if (AdditionalVRInputVector.IsNearlyZero() && CustomVRInputVector.IsNearlyZero())
		{
			LastPreAdditiveVRVelocity = FVector::ZeroVector;
			return;
		}

		LastPreAdditiveVRVelocity = (AdditionalVRInputVector / deltaTime); // Save off pre-additive Velocity for restoration next tick	

		if (LastPreAdditiveVRVelocity.SizeSquared() > FMath::Square(TrackingLossThreshold))
		{
			bHadExtremeInput = true;
			if (bHoldPositionOnTrackingLossThresholdHit)
			{
				LastPreAdditiveVRVelocity = FVector::ZeroVector;
			}
		}

		// Post the HMD velocity checks, add in our direct movement now
		LastPreAdditiveVRVelocity += (CustomVRInputVector / deltaTime);

		Velocity += LastPreAdditiveVRVelocity;
	}

	inline void RestorePreAdditiveVRMotionVelocity()
	{
		if (!LastPreAdditiveVRVelocity.IsNearlyZero())
		{
			if (bHadExtremeInput)
			{
				// Just zero out the velocity here
				Velocity = FVector::ZeroVector;
			}
			else
			{
				// This doesn't work with input in the opposing direction
				/*FVector ProjectedVelocity = Velocity.ProjectOnToNormal(LastPreAdditiveVRVelocity.GetSafeNormal());
				float VelSq = ProjectedVelocity.SizeSquared();
				float AddSq = LastPreAdditiveVRVelocity.SizeSquared();

				if (VelSq > AddSq || ProjectedVelocity.Equals(LastPreAdditiveVRVelocity, 0.1f))
				{
					// Subtract velocity if we still relatively retain it in the normalized direction
					Velocity -= LastPreAdditiveVRVelocity;
				}*/

				Velocity -= LastPreAdditiveVRVelocity;
			}
		}

		LastPreAdditiveVRVelocity = FVector::ZeroVector;
	}

	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void PhysCustom_Climbing(float deltaTime, int32 Iterations);
	virtual void PhysCustom_LowGrav(float deltaTime, int32 Iterations);

	// Teleport grips on correction to fixup issues
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;

	// Fix network smoothing with our default mesh back in
	virtual void SimulatedTick(float DeltaSeconds) override;

	// Skip updates with rotational differences
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;

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

	// If true will never count a physicsbody channel component as the floor, to prevent jitter / physics problems.
	// Make sure that you set simulating objects to the physics body channel if you want this to work correctly
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool bIgnoreSimulatingComponentsInFloorCheck;

	// If true will run the control rotation in the CMC instead of in the player controller
	// This puts the player rotation into the scoped movement (perf savings) and also ensures it is properly rotated prior to movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool bRunControlRotationInMovementComponent;

	// Moved into compute floor dist
	// Option to Skip simulating components when looking for floor
	/*virtual bool FloorSweepTest(
		const FVector& Start,
		FHitResult& OutHit,
		const FVector& End,
		ECollisionChannel TraceChannel,
		const struct FCollisionShape& CollisionShape,
		const struct FCollisionQueryParams& Params,
		const struct FCollisionResponseParams& ResponseParam
	) const override;*/

	virtual void ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult = NULL) const override;

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

	// If true will clamp the maximum movement on climbing step up to: VRClimbingStepUpMaxSize
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		bool bClampClimbingStepUp;

	// Maximum X/Y vector size to use when climbing stepping up (prevents very deep step ups from large movements).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepUpMaxSize;

	// If true will automatically set falling when a stepup occurs during climbing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		bool SetDefaultPostClimbMovementOnStepUp;

	// Max velocity on releasing a climbing grip
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingMaxReleaseVelocitySize;

	/* Custom distance that is required before accepting a walking stepup
	*  This is to help promote stepping up, engine default is 0.15f, generally you want it lower than that
	*  Do NOT set to larger than capsule radius!
	*  #TODO: Port to SimpleCharacter as well
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		float VREdgeRejectDistance;

	UFUNCTION(BlueprintCallable, Category = "VRMovement|Climbing")
		void SetClimbingMode(bool bIsClimbing);

	// Default movement mode to switch to post climb ended, only used if SetDefaultPostClimbMovementOnStepUp is true
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		EVRConjoinedMovementModes DefaultPostClimbMovement;

	// Overloading this to handle an edge case
	virtual void ApplyNetworkMovementMode(const uint8 ReceivedMode) override;

	/*
	* This is called client side to make a replicated movement mode change that hits the server in the saved move.
	*
	* Custom Movement Mode is currently limited to 0 - 8, the index's 0 and 1 are currently used up for the plugin movement modes.
	* So setting it to 0 or 1 would be Climbing, and LowGrav respectivly, this leaves 2-8 as open index's for use.
	* For a total of 6 Custom movement modes past the currently implemented plugin ones.
	*/
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void SetReplicatedMovementMode(EVRConjoinedMovementModes NewMovementMode);

	/*
	* Call this to convert the current movement mode to a Conjoined one for reference
	*
	* Custom Movement Mode is currently limited to 0 - 8, the index's 0 and 1 are currently used up for the plugin movement modes.
	* So setting it to 0 or 1 would be Climbing, and LowGrav respectivly, this leaves 2-8 as open index's for use.
	* For a total of 6 Custom movement modes past the currently implemented plugin ones.
	*/
	UFUNCTION(BlueprintPure, Category = "VRMovement")
		EVRConjoinedMovementModes GetReplicatedMovementMode();

	// We use 4 bits for this so a maximum of 16 elements
	EVRConjoinedMovementModes VRReplicatedMovementMode;

	FORCEINLINE void ApplyReplicatedMovementMode(EVRConjoinedMovementModes &NewMovementMode, bool bClearMovementMode = false)
	{
		if (NewMovementMode != EVRConjoinedMovementModes::C_MOVE_MAX)//None)
		{
			if (NewMovementMode <= EVRConjoinedMovementModes::C_MOVE_MAX)
			{
				// Is a default movement mode, just directly set it
				SetMovementMode((EMovementMode)NewMovementMode);
			}
			else // Is Custom
			{
				// Auto calculates the difference for our VR movements, index is from 0 so using climbing should get me correct index's as it is the first custom mode
				SetMovementMode(EMovementMode::MOVE_Custom, (((int8)NewMovementMode - (uint8)EVRConjoinedMovementModes::C_VRMOVE_Climbing)));
			}

			// Clearing it here instead now, as this way the code can inject it during PerformMovement
			// Specifically used by the Climbing Step up, so that server rollbacks are supported
			if(bClearMovementMode)
				NewMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;//None;
		}
	}

	void UpdateFromCompressedFlags(uint8 Flags) override;

	FVector RoundDirectMovement(FVector InMovement) const;

	// Setting this below 1.0 will change how fast you de-accelerate when touching a wall
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|LowGrav", meta = (ClampMin = "0.0", UIMin = "0", ClampMax = "5.0", UIMax = "5"))
		float VRLowGravWallFrictionScaler;

	// If true then low grav will ignore the default physics volume fluid friction, useful if you have a mix of low grav and normal movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|LowGrav")
		bool VRLowGravIgnoresDefaultFluidFriction;
};

