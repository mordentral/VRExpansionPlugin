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
class UVRSimpleRootComponent;

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

	UPROPERTY(BlueprintReadOnly, Transient, Category = VRMovement)
	UVRSimpleRootComponent * VRRootCapsule;

	//UPROPERTY(BlueprintReadOnly, Transient, Category = VRMovement)
	//UCapsuleComponent * VRCameraCollider;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSimpleCharacterMovementComponent")
	bool bAllowWalkingThroughWalls;

	// Allow merging movement replication (may cause issues when >10 players due to capsule location
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSimpleCharacterMovementComponent")
	bool bAllowMovementMerging;

	// Higher values will cause more slide but better step up
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSimpleCharacterMovementComponent", meta = (ClampMin = "0.01", UIMin = "0", ClampMax = "1.0", UIMax = "1"))
	float WallRepulsionMultiplier;


	void SetRequestedVelocity(FVector RequestedVel)
	{
		RequestedVelocity = RequestedVel;
		bHasRequestedVelocity = true;
	}

	bool CalcVRMove(float DeltaTime, float MaxAccel, float MaxSpeed, float Friction, float BrakingDeceleration, FVector& OutAcceleration, float& OutRequestedSpeed);

	FVector AdditionalVRInputVector;
	FVector LastAdditionalVRInputVector;

	FVector ScaleInputAcceleration(const FVector& InputAcceleration) const override;

	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;
	void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	/**
	* Default UObject constructor.
	*/
	UVRSimpleCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

