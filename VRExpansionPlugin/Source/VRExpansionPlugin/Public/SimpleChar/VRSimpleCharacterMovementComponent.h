// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "VRBaseCharacterMovementComponent.h"
#include "AI/Navigation/NavigationAvoidanceTypes.h"
#include "AI/RVOAvoidanceInterface.h"
#include "AITypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavigationSystem.h"
#include "Animation/AnimationAsset.h"
#include "Engine/EngineBaseTypes.h"
#include "Camera/CameraComponent.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Interfaces/NetworkPredictionInterface.h"
#include "WorldCollision.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRSimpleCharacterMovementComponent.generated.h"

class FDebugDisplayInfo;
class ACharacter;
class AVRSimpleCharacter;
//class UVRSimpleRootComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogSimpleCharacterMovement, Log, All);

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
class VREXPANSIONPLUGIN_API UVRSimpleCharacterMovementComponent : public UVRBaseCharacterMovementComponent
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

	// Skips checking for the HMD location on tick, for 2D pawns when a headset is connected
	UPROPERTY(BlueprintReadWrite, Category = VRMovement)
		bool bSkipHMDChecks;

	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	void PhysWalking(float deltaTime, int32 Iterations) override;
	void PhysFlying(float deltaTime, int32 Iterations) override;
	void PhysFalling(float deltaTime, int32 Iterations) override;
	void PhysNavWalking(float deltaTime, int32 Iterations) override;
	/**
	* Default UObject constructor.
	*/
	UVRSimpleCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult = nullptr) override;

	///////////////////////////
	// Replication Functions
	///////////////////////////
	//virtual void CallServerMove(const class FSavedMove_Character* NewMove, const class FSavedMove_Character* OldMove) override;

	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;

	/** Default client to server move RPC data container. Can be bypassed via SetNetworkMoveDataContainer(). */
	//FCharacterNetworkMoveDataContainer VRNetworkMoveDataContainer;
	//FCharacterMoveResponseDataContainer VRMoveResponseDataContainer;
	
	// Use ServerMoveVR instead
	virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	FNetworkPredictionData_Server* GetPredictionData_Server() const override;

	///////////////////////////
	// End Replication Functions
	///////////////////////////
};

class VREXPANSIONPLUGIN_API FSavedMove_VRSimpleCharacter : public FSavedMove_VRBaseCharacter
{

public:

	//FVector VRCapsuleLocation;
	//FVector LFDiff;
	//FVector CustomVRInputVector;
	//FRotator VRCapsuleRotation;
	//FVector RequestedVelocity;

	void Clear();
	virtual void SetInitialPosition(ACharacter* C);
	virtual void PrepMoveFor(ACharacter* Character) override;

	FSavedMove_VRSimpleCharacter() : FSavedMove_VRBaseCharacter()
	{
		//VRCapsuleLocation = FVector::ZeroVector;
		LFDiff = FVector::ZeroVector;
		//CustomVRInputVector = FVector::ZeroVector;
		//VRCapsuleRotation = FRotator::ZeroRotator;
		//RequestedVelocity = FVector::ZeroVector;
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