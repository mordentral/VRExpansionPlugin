// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
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

	FORCEINLINE bool HasRequestedVelocity()
	{
		return bHasRequestedVelocity;
	}


	UFUNCTION(BlueprintCallable, Category = "SimpleVRCharacterMovementComponent|VRLocations")
	void AddCustomReplicatedMovement(FVector Movement);

	FVector CustomVRInputVector;
	FVector AdditionalVRInputVector;

	// Injecting custom movement in here, bypasses floor detection
	//virtual void PerformMovement(float DeltaSeconds) override;

	FORCEINLINE void ApplyVRMotionToVelocity(float deltaTime);
	FORCEINLINE void RestorePreAdditiveVRMotionVelocity();
	FVector LastPreAdditiveVRVelocity;

	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void PhysCustom_Climbing(float deltaTime, int32 Iterations);

	// Height to auto step up
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepHeight;

	// Higher values make it easier to trigger a step up onto a platform and moves you farther in to the base
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepUpMultiplier;

	// If true will automatically set falling when a stepup occurs during climbing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		bool VRClimbingSetFallOnStepUp;

	// Max velocity on releasing a climbing grip
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingMaxReleaseVelocitySize;

};
