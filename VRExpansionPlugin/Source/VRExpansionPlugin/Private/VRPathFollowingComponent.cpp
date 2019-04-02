// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRPathFollowingComponent.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"

//#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 13
#include "Navigation/MetaNavMeshPath.h"
#include "NavLinkCustomInterface.h"
//#endif

// Force to use new movement comp

DEFINE_LOG_CATEGORY(LogPathFollowingVR);

void UVRPathFollowingComponent::SetMovementComponent(UNavMovementComponent* MoveComp)
{
	Super::SetMovementComponent(MoveComp);

	VRMovementComp = Cast<UVRBaseCharacterMovementComponent>(MovementComp);

	if (VRMovementComp)
	{
		OnRequestFinished.AddUObject(VRMovementComp, &UVRBaseCharacterMovementComponent::OnMoveCompleted);
	}
}

bool UVRPathFollowingComponent::HasReached(const FVector& TestPoint, EPathFollowingReachMode ReachMode, float InAcceptanceRadius) const
{
	// simple test for stationary agent, used as early finish condition
	const FVector CurrentLocation = MovementComp ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation()) : FVector::ZeroVector;
	const float GoalRadius = 0.0f;
	const float GoalHalfHeight = 0.0f;
	if (InAcceptanceRadius == UPathFollowingComponent::DefaultAcceptanceRadius)
	{
		InAcceptanceRadius = MyDefaultAcceptanceRadius;
	}

	const float AgentRadiusMod = (ReachMode == EPathFollowingReachMode::ExactLocation) || (ReachMode == EPathFollowingReachMode::OverlapGoal) ? 0.0f : MinAgentRadiusPct;
	return HasReachedInternal(TestPoint, GoalRadius, GoalHalfHeight, CurrentLocation, InAcceptanceRadius, AgentRadiusMod);
}

bool UVRPathFollowingComponent::HasReached(const AActor& TestGoal, EPathFollowingReachMode ReachMode, float InAcceptanceRadius, bool bUseNavAgentGoalLocation) const
{
	// simple test for stationary agent, used as early finish condition
	float GoalRadius = 0.0f;
	float GoalHalfHeight = 0.0f;
	FVector GoalOffset = FVector::ZeroVector;
	FVector TestPoint = TestGoal.GetActorLocation();
	if (InAcceptanceRadius == UPathFollowingComponent::DefaultAcceptanceRadius)
	{
		InAcceptanceRadius = MyDefaultAcceptanceRadius;
	}

	if (bUseNavAgentGoalLocation)
	{
		const INavAgentInterface* NavAgent = Cast<const INavAgentInterface>(&TestGoal);
		if (NavAgent)
		{
			const AActor* OwnerActor = GetOwner();
			const FVector GoalMoveOffset = NavAgent->GetMoveGoalOffset(OwnerActor);
			NavAgent->GetMoveGoalReachTest(OwnerActor, GoalMoveOffset, GoalOffset, GoalRadius, GoalHalfHeight);
			TestPoint = FQuatRotationTranslationMatrix(TestGoal.GetActorQuat(), NavAgent->GetNavAgentLocation()).TransformPosition(GoalOffset);

			if ((ReachMode == EPathFollowingReachMode::ExactLocation) || (ReachMode == EPathFollowingReachMode::OverlapAgent))
			{
				GoalRadius = 0.0f;
			}
		}
	}

	const FVector CurrentLocation = MovementComp ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation()) : FVector::ZeroVector;
	const float AgentRadiusMod = (ReachMode == EPathFollowingReachMode::ExactLocation) || (ReachMode == EPathFollowingReachMode::OverlapGoal) ? 0.0f : MinAgentRadiusPct;
	return HasReachedInternal(TestPoint, GoalRadius, GoalHalfHeight, CurrentLocation, InAcceptanceRadius, AgentRadiusMod);
}

void UVRPathFollowingComponent::GetDebugStringTokens(TArray<FString>& Tokens, TArray<EPathFollowingDebugTokens::Type>& Flags) const
{
	Tokens.Add(GetStatusDesc());
	Flags.Add(EPathFollowingDebugTokens::Description);

	if (Status != EPathFollowingStatus::Moving)
	{
		return;
	}

	FString& StatusDesc = Tokens[0];
	if (Path.IsValid())
	{
		const int32 NumMoveSegments = (Path.IsValid() && Path->IsValid()) ? Path->GetPathPoints().Num() : -1;
		const bool bIsDirect = (Path->CastPath<FAbstractNavigationPath>() != NULL);
		const bool bIsCustomLink = CurrentCustomLinkOb.IsValid();

		if (!bIsDirect)
		{
			StatusDesc += FString::Printf(TEXT(" (%d..%d/%d)%s"), MoveSegmentStartIndex + 1, MoveSegmentEndIndex + 1, NumMoveSegments,
				bIsCustomLink ? TEXT(" (custom NavLink)") : TEXT(""));
		}
		else
		{
			StatusDesc += TEXT(" (direct)");
		}
	}
	else
	{
		StatusDesc += TEXT(" (invalid path)");
	}

	// add debug params
	float CurrentDot = 0.0f, CurrentDistance = 0.0f, CurrentHeight = 0.0f;
	uint8 bFailedDot = 0, bFailedDistance = 0, bFailedHeight = 0;
	DebugReachTest(CurrentDot, CurrentDistance, CurrentHeight, bFailedHeight, bFailedDistance, bFailedHeight);

	Tokens.Add(TEXT("dot"));
	Flags.Add(EPathFollowingDebugTokens::ParamName);
	Tokens.Add(FString::Printf(TEXT("%.2f"), CurrentDot));
	Flags.Add(bFailedDot ? EPathFollowingDebugTokens::FailedValue : EPathFollowingDebugTokens::PassedValue);

	Tokens.Add(TEXT("dist2D"));
	Flags.Add(EPathFollowingDebugTokens::ParamName);
	Tokens.Add(FString::Printf(TEXT("%.0f"), CurrentDistance));
	Flags.Add(bFailedDistance ? EPathFollowingDebugTokens::FailedValue : EPathFollowingDebugTokens::PassedValue);

	Tokens.Add(TEXT("distZ"));
	Flags.Add(EPathFollowingDebugTokens::ParamName);
	Tokens.Add(FString::Printf(TEXT("%.0f"), CurrentHeight));
	Flags.Add(bFailedHeight ? EPathFollowingDebugTokens::FailedValue : EPathFollowingDebugTokens::PassedValue);
}

void UVRPathFollowingComponent::PauseMove(FAIRequestID RequestID, EPathFollowingVelocityMode VelocityMode)
{
	//UE_VLOG(GetOwner(), LogPathFollowing, Log, TEXT("PauseMove: RequestID(%u)"), RequestID);
	if (Status == EPathFollowingStatus::Paused)
	{
		return;
	}

	if (RequestID.IsEquivalent(GetCurrentRequestId()))
	{
		if ((VelocityMode == EPathFollowingVelocityMode::Reset) && MovementComp && HasMovementAuthority())
		{
			MovementComp->StopMovementKeepPathing();
		}

		LocationWhenPaused = MovementComp ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation()) : FVector::ZeroVector;
		PathTimeWhenPaused = Path.IsValid() ? Path->GetTimeStamp() : 0.0f;
		Status = EPathFollowingStatus::Paused;

		UpdateMoveFocus();
	}
}



bool UVRPathFollowingComponent::ShouldCheckPathOnResume() const
{
	bool bCheckPath = true;
	if (MovementComp != NULL)
	{
		float AgentRadius = 0.0f, AgentHalfHeight = 0.0f;
		MovementComp->GetOwner()->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);

		const FVector CurrentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation());
		const float DeltaMove2DSq = (CurrentLocation - LocationWhenPaused).SizeSquared2D();
		const float DeltaZ = FMath::Abs(CurrentLocation.Z - LocationWhenPaused.Z);
		if (DeltaMove2DSq < FMath::Square(AgentRadius) && DeltaZ < (AgentHalfHeight * 0.5f))
		{
			bCheckPath = false;
		}
	}

	return bCheckPath;
}

int32 UVRPathFollowingComponent::DetermineStartingPathPoint(const FNavigationPath* ConsideredPath) const
{
	int32 PickedPathPoint = INDEX_NONE;

	if (ConsideredPath && ConsideredPath->IsValid())
	{
		// if we already have some info on where we were on previous path
		// we can find out if there's a segment on new path we're currently at
		if (MoveSegmentStartRef != INVALID_NAVNODEREF &&
			MoveSegmentEndRef != INVALID_NAVNODEREF &&
			ConsideredPath->GetNavigationDataUsed() != NULL)
		{
			// iterate every new path node and see if segment match
			for (int32 PathPoint = 0; PathPoint < ConsideredPath->GetPathPoints().Num() - 1; ++PathPoint)
			{
				if (ConsideredPath->GetPathPoints()[PathPoint].NodeRef == MoveSegmentStartRef &&
					ConsideredPath->GetPathPoints()[PathPoint + 1].NodeRef == MoveSegmentEndRef)
				{
					PickedPathPoint = PathPoint;
					break;
				}
			}
		}

		if (MovementComp && PickedPathPoint == INDEX_NONE)
		{
			if (ConsideredPath->GetPathPoints().Num() > 2)
			{
				// check if is closer to first or second path point (don't assume AI's standing)
				const FVector CurrentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation());
				const FVector PathPt0 = *ConsideredPath->GetPathPointLocation(0);
				const FVector PathPt1 = *ConsideredPath->GetPathPointLocation(1);
				// making this test in 2d to avoid situation where agent's Z location not being in "navmesh plane"
				// would influence the result
				const float SqDistToFirstPoint = (CurrentLocation - PathPt0).SizeSquared2D();
				const float SqDistToSecondPoint = (CurrentLocation - PathPt1).SizeSquared2D();
				PickedPathPoint = FMath::IsNearlyEqual(SqDistToFirstPoint, SqDistToSecondPoint) ?
					((FMath::Abs(CurrentLocation.Z - PathPt0.Z) < FMath::Abs(CurrentLocation.Z - PathPt1.Z)) ? 0 : 1) :
					((SqDistToFirstPoint < SqDistToSecondPoint) ? 0 : 1);
			}
			else
			{
				// If there are only two point we probably should start from the beginning
				PickedPathPoint = 0;
			}
		}
	}

	return PickedPathPoint;
}

bool UVRPathFollowingComponent::UpdateBlockDetection()
{
	const float GameTime = GetWorld()->GetTimeSeconds();
	if (bUseBlockDetection &&
		MovementComp &&
		GameTime > (LastSampleTime + BlockDetectionInterval) &&
		BlockDetectionSampleCount > 0)
	{
		LastSampleTime = GameTime;

		if (LocationSamples.Num() == NextSampleIdx)
		{
			LocationSamples.AddZeroed(1);
		}

		LocationSamples[NextSampleIdx] = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationBased() : MovementComp->GetActorFeetLocationBased());
		NextSampleIdx = (NextSampleIdx + 1) % BlockDetectionSampleCount;
		return true;
	}

	return false;
}

void UVRPathFollowingComponent::UpdatePathSegment()
{
#if !UE_BUILD_SHIPPING
	DEBUG_bMovingDirectlyToGoal = false;
#endif // !UE_BUILD_SHIPPING

	if ((Path.IsValid() == false) || (MovementComp == nullptr))
	{
		//UE_CVLOG(Path.IsValid() == false, this, LogPathFollowing, Log, TEXT("Aborting move due to not having a valid path object"));
		OnPathFinished(EPathFollowingResult::Aborted, FPathFollowingResultFlags::InvalidPath);
		return;
	}

	if (!Path->IsValid())
	{
		if (!Path->IsWaitingForRepath())
		{
			//UE_VLOG(this, LogPathFollowing, Log, TEXT("Aborting move due to path being invelid and not waiting for repath"));
			OnPathFinished(EPathFollowingResult::Aborted, FPathFollowingResultFlags::InvalidPath);
		}

		return;
	}

	FMetaNavMeshPath* MetaNavPath = bIsUsingMetaPath ? Path->CastPath<FMetaNavMeshPath>() : nullptr;

	// if agent has control over its movement, check finish conditions
	const FVector CurrentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation());
	const bool bCanUpdateState = HasMovementAuthority();
	if (bCanUpdateState && Status == EPathFollowingStatus::Moving)
	{
		const int32 LastSegmentEndIndex = Path->GetPathPoints().Num() - 1;
		const bool bFollowingLastSegment = (MoveSegmentEndIndex >= LastSegmentEndIndex);
		const bool bLastPathChunk = (MetaNavPath == nullptr || MetaNavPath->IsLastSection());

		if (bCollidedWithGoal)
		{
			// check if collided with goal actor
			OnSegmentFinished();
			OnPathFinished(EPathFollowingResult::Success, FPathFollowingResultFlags::None);
		}
		else if (HasReachedDestination(CurrentLocation))
		{
			// always check for destination, acceptance radius may cause it to pass before reaching last segment
			OnSegmentFinished();
			OnPathFinished(EPathFollowingResult::Success, FPathFollowingResultFlags::None);
		}
		else if (bFollowingLastSegment && bMoveToGoalOnLastSegment && bLastPathChunk)
		{
			// use goal actor for end of last path segment
			// UNLESS it's partial path (can't reach goal)
			if (DestinationActor.IsValid() && Path->IsPartial() == false)
			{
				const FVector AgentLocation = DestinationAgent ? DestinationAgent->GetNavAgentLocation() : DestinationActor->GetActorLocation();
				// note that the condition below requires GoalLocation to be in world space.
				const FVector GoalLocation = FQuatRotationTranslationMatrix(DestinationActor->GetActorQuat(), AgentLocation).TransformPosition(MoveOffset);

				CurrentDestination.Set(NULL, GoalLocation);

				//UE_VLOG(this, LogPathFollowing, Log, TEXT("Moving directly to move goal rather than following last path segment"));
				//UE_VLOG_LOCATION(this, LogPathFollowing, VeryVerbose, GoalLocation, 30, FColor::Green, TEXT("Last-segment-to-actor"));
				//UE_VLOG_SEGMENT(this, LogPathFollowing, VeryVerbose, CurrentLocation, GoalLocation, FColor::Green, TEXT_EMPTY);
			}

			UpdateMoveFocus();

#if !UE_BUILD_SHIPPING
			DEBUG_bMovingDirectlyToGoal = true;
#endif // !UE_BUILD_SHIPPING
		}
		// check if current move segment is finished
		else if (HasReachedCurrentTarget(CurrentLocation))
		{
			OnSegmentFinished();
			SetNextMoveSegment();
		}
	}

	if (bCanUpdateState && Status == EPathFollowingStatus::Moving)
	{
		// check waypoint switch condition in meta paths
		if (MetaNavPath && Status == EPathFollowingStatus::Moving)
		{
			MetaNavPath->ConditionalMoveToNextSection(CurrentLocation, EMetaPathUpdateReason::MoveTick);
		}

		// gather location samples to detect if moving agent is blocked
		const bool bHasNewSample = UpdateBlockDetection();
		if (bHasNewSample && IsBlocked())
		{
			if (Path->GetPathPoints().IsValidIndex(MoveSegmentEndIndex) && Path->GetPathPoints().IsValidIndex(MoveSegmentStartIndex))
			{
				//LogBlockHelper(GetOwner(), MovementComp, MinAgentRadiusPct, MinAgentHalfHeightPct,
					//*Path->GetPathPointLocation(MoveSegmentStartIndex),
					//*Path->GetPathPointLocation(MoveSegmentEndIndex));
			}
			else
			{
				if ((GetOwner() != NULL) && (MovementComp != NULL))
				{
				//	UE_VLOG(GetOwner(), LogPathFollowing, Verbose, TEXT("Path blocked, but move segment indices are not valid: start %d, end %d of %d"), MoveSegmentStartIndex, MoveSegmentEndIndex, Path->GetPathPoints().Num());
				}
			}
			OnPathFinished(EPathFollowingResult::Blocked, FPathFollowingResultFlags::None);
		}
	}
}

void UVRPathFollowingComponent::FollowPathSegment(float DeltaTime)
{
	if (!Path.IsValid() || MovementComp == nullptr)
	{
		return;
	}

	const FVector CurrentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation());
	const FVector CurrentTarget = GetCurrentTargetLocation();

	// set to false by default, we will set set this back to true if appropriate
	bIsDecelerating = false;

	const bool bAccelerationBased = MovementComp->UseAccelerationForPathFollowing();
	if (bAccelerationBased)
	{
		CurrentMoveInput = (CurrentTarget - CurrentLocation).GetSafeNormal();

		if (MoveSegmentStartIndex >= DecelerationSegmentIndex)
		{
			const FVector PathEnd = Path->GetEndLocation();
			const float DistToEndSq = FVector::DistSquared(CurrentLocation, PathEnd);
			const bool bShouldDecelerate = DistToEndSq < FMath::Square(CachedBrakingDistance);
			if (bShouldDecelerate)
			{
				bIsDecelerating = true;

				const float SpeedPct = FMath::Clamp(FMath::Sqrt(DistToEndSq) / CachedBrakingDistance, 0.0f, 1.0f);
				CurrentMoveInput *= SpeedPct;
			}
		}

		PostProcessMove.ExecuteIfBound(this, CurrentMoveInput);
		MovementComp->RequestPathMove(CurrentMoveInput);
	}
	else
	{
		FVector MoveVelocity = (CurrentTarget - CurrentLocation) / DeltaTime;

		const int32 LastSegmentStartIndex = Path->GetPathPoints().Num() - 2;
		const bool bNotFollowingLastSegment = (MoveSegmentStartIndex < LastSegmentStartIndex);

		PostProcessMove.ExecuteIfBound(this, MoveVelocity);
		MovementComp->RequestDirectMove(MoveVelocity, bNotFollowingLastSegment);
	}
}

bool UVRPathFollowingComponent::HasReachedCurrentTarget(const FVector& CurrentLocation) const
{
	if (MovementComp == NULL)
	{
		return false;
	}

	const FVector CurrentTarget = GetCurrentTargetLocation();
	const FVector CurrentDirection = GetCurrentDirection();

	// check if moved too far
	const FVector ToTarget = (CurrentTarget - (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation()));
	const float SegmentDot = FVector::DotProduct(ToTarget, CurrentDirection);
	if (SegmentDot < 0.0)
	{
		return true;
	}

	// or standing at target position
	// don't use acceptance radius here, it has to be exact for moving near corners (2D test < 5% of agent radius)
	const float GoalRadius = 0.0f;
	const float GoalHalfHeight = 0.0f;

	return HasReachedInternal(CurrentTarget, GoalRadius, GoalHalfHeight, CurrentLocation, CurrentAcceptanceRadius, 0.05f);
}

void UVRPathFollowingComponent::DebugReachTest(float& CurrentDot, float& CurrentDistance, float& CurrentHeight, uint8& bDotFailed, uint8& bDistanceFailed, uint8& bHeightFailed) const
{
	if (!Path.IsValid() || MovementComp == NULL)
	{
		return;
	}

	const int32 LastSegmentEndIndex = Path->GetPathPoints().Num() - 1;
	const bool bFollowingLastSegment = (MoveSegmentEndIndex >= LastSegmentEndIndex);

	float GoalRadius = 0.0f;
	float GoalHalfHeight = 0.0f;
	float RadiusThreshold = 0.0f;
	float AgentRadiusPct = 0.05f;

	FVector AgentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocationVR() : MovementComp->GetActorFeetLocation());
	FVector GoalLocation = GetCurrentTargetLocation();
	RadiusThreshold = CurrentAcceptanceRadius;

	if (bFollowingLastSegment)
	{
		GoalLocation = *Path->GetPathPointLocation(Path->GetPathPoints().Num() - 1);
		AgentRadiusPct = MinAgentRadiusPct;

		// take goal's current location, unless path is partial or last segment doesn't reach goal actor (used by tethered AI)
		if (DestinationActor.IsValid() && !Path->IsPartial() && bMoveToGoalOnLastSegment)
		{
			if (DestinationAgent)
			{
				FVector GoalOffset;

				const AActor* OwnerActor = GetOwner();
				DestinationAgent->GetMoveGoalReachTest(OwnerActor, MoveOffset, GoalOffset, GoalRadius, GoalHalfHeight);
				GoalLocation = FQuatRotationTranslationMatrix(DestinationActor->GetActorQuat(), DestinationAgent->GetNavAgentLocation()).TransformPosition(GoalOffset);
			}
			else
			{
				GoalLocation = DestinationActor->GetActorLocation();
			}
		}
	}

	const FVector ToGoal = (GoalLocation - AgentLocation);
	const FVector CurrentDirection = GetCurrentDirection();
	CurrentDot = FVector::DotProduct(ToGoal.GetSafeNormal(), CurrentDirection);
	bDotFailed = (CurrentDot < 0.0f) ? 1 : 0;

	// get cylinder of moving agent
	float AgentRadius = 0.0f;
	float AgentHalfHeight = 0.0f;
	AActor* MovingAgent = MovementComp->GetOwner();
	MovingAgent->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);

	CurrentDistance = ToGoal.Size2D();
	const float UseRadius = FMath::Max(RadiusThreshold, GoalRadius + (AgentRadius * AgentRadiusPct));
	bDistanceFailed = (CurrentDistance > UseRadius) ? 1 : 0;

	CurrentHeight = FMath::Abs(ToGoal.Z);
	const float UseHeight = GoalHalfHeight + (AgentHalfHeight * MinAgentHalfHeightPct);
	bHeightFailed = (CurrentHeight > UseHeight) ? 1 : 0;
}
