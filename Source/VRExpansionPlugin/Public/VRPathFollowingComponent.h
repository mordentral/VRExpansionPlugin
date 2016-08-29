// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRBPDatatypes.h"
#include "VRRootComponent.h"
#include "VRCharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Ai/Navigation/AbstractNavData.h"
#include "VRPathFollowingComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPathFollowingVR, Warning, All);

UCLASS()
class VREXPANSIONPLUGIN_API UVRPathFollowingComponent : public UPathFollowingComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(transient)
	UVRCharacterMovementComponent* VRMovementComp;

	// Add link to VRMovementComp
	void SetMovementComponent(UNavMovementComponent* MoveComp) override;

	// Have to override this to call the correct HasReachCurrentTarget
	void UpdatePathSegment() override;
	bool HasReachedCurrentTarget(const FVector& CurrentLocation) const;

	// Had to override this to get the correct DebugReachTest
	virtual void GetDebugStringTokens(TArray<FString>& Tokens, TArray<EPathFollowingDebugTokens::Type>& Flags) const override;
	void DebugReachTest(float& CurrentDot, float& CurrentDistance, float& CurrentHeight, uint8& bDotFailed, uint8& bDistanceFailed, uint8& bHeightFailed) const;

	//bool HasReached(const FVector& TestPoint, float AcceptanceRadius = UPathFollowingComponent::DefaultAcceptanceRadius, bool bExactSpot = false) const;
	//bool HasReached(const AActor& TestGoal, float AcceptanceRadius = UPathFollowingComponent::DefaultAcceptanceRadius, bool bExactSpot = false, bool bUseNavAgentGoalLocation = true) const;

	void FollowPathSegment(float DeltaTime) override;
	int32 DetermineStartingPathPoint(const FNavigationPath* ConsideredPath) const override;
		
	bool ShouldCheckPathOnResume() const override;
	void PauseMove(FAIRequestID RequestID, bool bResetVelocity) override;

	// Just a debug reference
	//virtual FAIRequestID RequestMove(FNavPathSharedPtr Path, FRequestCompletedSignature OnComplete, const AActor* DestinationActor = NULL, float AcceptanceRadius = UPathFollowingComponent::DefaultAcceptanceRadius, bool bStopOnOverlap = true, FCustomMoveSharedPtr GameData = NULL) override;
	
	// Just a debug reference
	//void LogBlockHelper(AActor* LogOwner, UNavMovementComponent* MoveComp, float RadiusPct, float HeightPct, const FVector& SegmentStart, const FVector& SegmentEnd)





};