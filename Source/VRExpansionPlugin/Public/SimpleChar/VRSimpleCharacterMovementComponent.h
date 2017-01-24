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
#include "VRSimpleCharacterMovementComponent.generated.h"

class FDebugDisplayInfo;
class ACharacter;
class UVRSimpleCharacterMovementComponent;
class AVRSimpleCharacter;
//class UVRSimpleRootComponent;

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
class VREXPANSIONPLUGIN_API UVRSimpleCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:

	bool bIsFirstTick;
	FVector curCameraLoc;
	FRotator curCameraRot;

	FVector lastCameraLoc;
	FRotator lastCameraRot;

	UPROPERTY(BlueprintReadOnly, Transient, Category = VRMovement)
		UCapsuleComponent * VRRootCapsule;

	UPROPERTY(BlueprintReadOnly, Transient, Category = VRMovement)
		UCameraComponent * VRCameraComponent;

	bool IsLocallyControlled() const
	{
		// I like epics implementation better than my own
		const AActor* MyOwner = GetOwner();	
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
	}

	FORCEINLINE bool HasRequestedVelocity()
	{
		return bHasRequestedVelocity;
	}

	UFUNCTION(BlueprintCallable, Category = "SimpleVRCharacterMovementComponent|VRLocations")
	void AddCustomReplicatedMovement(FVector Movement);

	FVector CustomVRInputVector;
	FVector AdditionalVRInputVector;

	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	FORCEINLINE void ApplyVRMotionToVelocity(float deltaTime);
	FORCEINLINE void RestorePreAdditiveVRMotionVelocity();
	FVector LastPreAdditiveVRVelocity;

	void PhysWalking(float deltaTime, int32 Iterations) override;
	void PhysFlying(float deltaTime, int32 Iterations) override;
	void PhysFalling(float deltaTime, int32 Iterations) override;
	void PhysNavWalking(float deltaTime, int32 Iterations) override;
	/**
	* Default UObject constructor.
	*/
	UVRSimpleCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	///////////////////////////
	// Replication Functions
	///////////////////////////
	void CallServerMoveVR(const class FSavedMove_VRSimpleCharacter* NewMove, const class FSavedMove_VRSimpleCharacter* OldMove);
	
	// Use ServerMoveVR instead
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	/** Replicated function sent by client to server - contains client movement and view info. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVR(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveVR_Implementation(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveVR_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 CompressedMoveFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. */
	UFUNCTION(unreliable, server, WithValidation)
		virtual void ServerMoveVRDual(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveVRDual_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. First move is non root motion, second is root motion. */
	UFUNCTION(unreliable, server, WithValidation)
		virtual void ServerMoveVRDualHybridRootMotion(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual void ServerMoveVRDualHybridRootMotion_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 rOldRequestedVelocity, FVector_NetQuantize100 OldLFDiff, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 rRequestedVelocity, FVector_NetQuantize100 LFDiff, uint8 NewFlags, uint8 ClientRoll, uint32 View, UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode);


	FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	FNetworkPredictionData_Server* GetPredictionData_Server() const override;

	///////////////////////////
	// End Replication Functions
	///////////////////////////
};

class VREXPANSIONPLUGIN_API FSavedMove_VRSimpleCharacter : public FSavedMove_Character
{

public:

	//FVector VRCapsuleLocation;
	FVector LFDiff;
	//FRotator VRCapsuleRotation;
	FVector RequestedVelocity;

	void Clear();
	virtual void SetInitialPosition(ACharacter* C);

	FSavedMove_VRSimpleCharacter() : FSavedMove_Character()
	{
		//VRCapsuleLocation = FVector::ZeroVector;
		LFDiff = FVector::ZeroVector;
		//VRCapsuleRotation = FRotator::ZeroRotator;
		RequestedVelocity = FVector::ZeroVector;
	}

	bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		if (bForceNoCombine || NewMove->bForceNoCombine)
		{
			return false;
		}

		FSavedMove_VRSimpleCharacter * nMove = (FSavedMove_VRSimpleCharacter *)NewMove.Get();

		if (!nMove || (!LFDiff.IsNearlyZero() && !nMove->LFDiff.IsNearlyZero()) || (!RequestedVelocity.IsNearlyZero() && !nMove->RequestedVelocity.IsNearlyZero()))
			return false;

		// Cannot combine moves which contain root motion for now.
		// @fixme laurent - we should be able to combine most of them though, but current scheme of resetting pawn location and resimulating forward doesn't work.
		// as we don't want to tick montage twice (so we don't fire events twice). So we need to rearchitecture this so we tick only the second part of the move, and reuse the first part.
		if ((RootMotionMontage != NULL) || (NewMove->RootMotionMontage != NULL))
		{
			return false;
		}

		if (NewMove->Acceleration.IsZero())
		{
			if (!Acceleration.IsZero())
			{
				return false;
			}

			if (!StartVelocity.IsZero() || !NewMove->StartVelocity.IsZero())
			{
				return false;
			}
		}
		else
		{
			if (NewMove->DeltaTime + DeltaTime >= MaxDelta)
			{
				return false;
			}

			if (!FVector::Coincident(AccelNormal, NewMove->AccelNormal, AccelDotThresholdCombine))
			{
				return false;
			}
		}

		if (bPressedJump || NewMove->bPressedJump)
		{
			return false;
		}

		if (bWantsToCrouch != NewMove->bWantsToCrouch)
		{
			return false;
		}

		if (StartBase != NewMove->StartBase)
		{
			return false;
		}

		if (StartBoneName != NewMove->StartBoneName)
		{
			return false;
		}

		if (MovementMode != NewMove->MovementMode)
		{
			return false;
		}

		if (StartCapsuleRadius != NewMove->StartCapsuleRadius)
		{
			return false;
		}

		if (StartCapsuleHalfHeight != NewMove->StartCapsuleHalfHeight)
		{
			return false;
		}

		if (!StartBaseRotation.Equals(NewMove->StartBaseRotation)) // only if base hasn't rotated
		{
			return false;
		}

		if (CustomTimeDilation != NewMove->CustomTimeDilation)
		{
			return false;
		}

		return true;
	}

};

// Need this for capsule location replication
class VREXPANSIONPLUGIN_API FNetworkPredictionData_Client_VRSimpleCharacter : public FNetworkPredictionData_Client_Character
{
public:
	FNetworkPredictionData_Client_VRSimpleCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{

	}

	FSavedMovePtr AllocateNewMove()
	{
		return FSavedMovePtr(new FSavedMove_VRSimpleCharacter());
	}
};


// Need this for capsule location replication?????
class VREXPANSIONPLUGIN_API FNetworkPredictionData_Server_VRSimpleCharacter : public FNetworkPredictionData_Server_Character
{
public:
	FNetworkPredictionData_Server_VRSimpleCharacter(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Server_Character(ClientMovement)
	{

	}

	FSavedMovePtr AllocateNewMove()
	{
		return FSavedMovePtr(new FSavedMove_VRSimpleCharacter());
	}
};