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

	virtual void PerformMovement(float DeltaSeconds) override;

	FORCEINLINE bool HasRequestedVelocity()
	{
		return bHasRequestedVelocity;
	}

	bool IsLocallyControlled() const
	{
		// I like epics implementation better than my own
		const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
	}

	// Setting this higher will divide the wall slide effect by this value, to reduce collision sliding.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement",meta = (ClampMin = "0.0", UIMin = "0", ClampMax = "5.0", UIMax = "5"))
		float VRWallSlideScaler;

	/** Custom version of SlideAlongSurface that handles different movement modes separately; namely during walking physics we might not want to slide up slopes. */
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact) override;

	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacterMovementComponent|VRLocations")
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

	// Need to use actual capsule location for step up
	virtual bool VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult = nullptr);

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
	
	UFUNCTION(BlueprintCallable, Category = "VRMovement|Climbing")
		void SetClimbingMode(bool bIsClimbing);

	
	bool bStartedClimbing;
	bool bEndedClimbing;

	void UVRBaseCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags) override
	{
		bStartedClimbing = ((Flags & FSavedMove_Character::FLAG_Custom_0) != 0);
		bEndedClimbing = ((Flags & FSavedMove_Character::FLAG_Custom_1) != 0);
		Super::UpdateFromCompressedFlags(Flags);
	}

};

class VREXPANSIONPLUGIN_API FSavedMove_VRBaseCharacter : public FSavedMove_Character
{

public:

	bool bStartedClimbing;
	bool bEndedClimbing;

	void Clear();
	virtual void SetInitialPosition(ACharacter* C);

	FSavedMove_VRBaseCharacter() : FSavedMove_Character()
	{
		bStartedClimbing = false;
		bEndedClimbing = false;
	}

	uint8 GetCompressedFlags() const
	{
		uint8 Result = FSavedMove_Character::GetCompressedFlags();

		if (bStartedClimbing)
		{
			Result |= FLAG_Custom_0;
		}

		if (bEndedClimbing)
		{
			Result |= FLAG_Custom_1;
		}

		return Result;
	}

	bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		FSavedMove_VRBaseCharacter * nMove = (FSavedMove_VRBaseCharacter *)NewMove.Get();

		if (!nMove || (bStartedClimbing != nMove->bStartedClimbing) || (bEndedClimbing != nMove->bEndedClimbing))
			return false;

		return FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}


	bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override
	{

		// Auto important if toggled climbing
		if (bStartedClimbing || bEndedClimbing)
			return true;

		// Else check parent class
		return FSavedMove_Character::IsImportantMove(LastAckedMove);
	}

};