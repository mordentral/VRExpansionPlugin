// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"
#include "VRPathFollowingComponent.h"

// Force to use new movement comp

//#define (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()) VRMovementComp != nullptr ? VR(VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()) : (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation());
DEFINE_LOG_CATEGORY(LogPathFollowingVR);

void UVRPathFollowingComponent::SetMovementComponent(UNavMovementComponent* MoveComp)
{
	Super::SetMovementComponent(MoveComp);

	VRMovementComp = Cast<UVRCharacterMovementComponent>(MovementComp);
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

// Do last, not really that needed
/*
void LogBlockHelper(AActor* LogOwner, UNavMovementComponent* MoveComp, float RadiusPct, float HeightPct, const FVector& SegmentStart, const FVector& SegmentEnd)
{
#if ENABLE_VISUAL_LOG
	if (MoveComp && LogOwner)
	{
		const FVector AgentLocation = MoveComp->GetActorFeetLocation();
		const FVector ToTarget = (SegmentEnd - AgentLocation);
		const float SegmentDot = FVector::DotProduct(ToTarget.GetSafeNormal(), (SegmentEnd - SegmentStart).GetSafeNormal());
		//UE_Vlog(LogOwner, LogPathFollowingVR, Verbose, TEXT("[agent to segment end] dot [segment dir]: %f"), SegmentDot);

		float AgentRadius = 0.0f;
		float AgentHalfHeight = 0.0f;
		AActor* MovingAgent = MoveComp->GetOwner();
		MovingAgent->GetSimpleCollisionCylinder(AgentRadius, AgentHalfHeight);

		const float Dist2D = ToTarget.Size2D();
		//UE_Vlog(LogOwner, LogPathFollowingVR, Verbose, TEXT("dist 2d: %f (agent radius: %f [%f])"), Dist2D, AgentRadius, AgentRadius * (1 + RadiusPct));

		const float ZDiff = FMath::Abs(ToTarget.Z);
		//UE_Vlog(LogOwner, LogPathFollowingVR, Verbose, TEXT("Z diff: %f (agent halfZ: %f [%f])"), ZDiff, AgentHalfHeight, AgentHalfHeight * (1 + HeightPct));
	}
#endif // ENABLE_VISUAL_LOG
}*/


/*
FAIRequestID UVRPathFollowingComponent::RequestMove(FNavPathSharedPtr InPath, FRequestCompletedSignature OnComplete,
	const AActor* InDestinationActor, float InAcceptanceRadius, bool bInStopOnOverlap, FCustomMoveSharedPtr InGameData)
{
	//UE_Vlog(GetOwner(), LogPathFollowingVR, Log, TEXT("RequestMove: Path(%s), AcceptRadius(%.1f%s), DestinationActor(%s), GameData(%s)"),
		*GetPathDescHelper(InPath),
		InAcceptanceRadius, bInStopOnOverlap ? TEXT(" + agent") : TEXT(""),
		*GetNameSafe(InDestinationActor),
		!InGameData.IsValid() ? TEXT("missing") : TEXT("valid"));

	LogPathHelper(GetOwner(), InPath, InDestinationActor);

	if (InAcceptanceRadius == UPathFollowingComponent::DefaultAcceptanceRadius)
	{
		InAcceptanceRadius = MyDefaultAcceptanceRadius;
	}

	if (ResourceLock.IsLocked())
	{
		//UE_Vlog(GetOwner(), LogPathFollowingVR, Log, TEXT("Rejecting move request due to resource lock by %s"), *ResourceLock.GetLockPriorityName());
		return FAIRequestID::InvalidRequest;
	}

	if (!InPath.IsValid() || InAcceptanceRadius < 0.0f)
	{
		//UE_Vlog(GetOwner(), LogPathFollowingVR, Log, TEXT("RequestMove: invalid request"));
		return FAIRequestID::InvalidRequest;
	}

	// try to grab movement component
	if (!UpdateMovementComponent())
	{
		//UE_Vlog(GetOwner(), LogPathFollowingVR, Warning, TEXT("RequestMove: missing movement component"));
		return FAIRequestID::InvalidRequest;
	}

	// update ID first, so any observer notified by AbortMove() could detect new request
	const uint32 PrevMoveId = CurrentRequestId;

	// abort previous movement
	if (Status == EPathFollowingStatus::Paused && Path.IsValid() && InPath.Get() == Path.Get() && DestinationActor == InDestinationActor)
	{
		ResumeMove();
	}
	else
	{
		if (Status == EPathFollowingStatus::Moving)
		{
			const bool bResetVelocity = false;
			const bool bFinishAsSkipped = true;
			AbortMove(TEXT("new request"), PrevMoveId, bResetVelocity, bFinishAsSkipped, EPathFollowingMessage::OtherRequest);
		}

		Reset();

		StoreRequestId();

		// store new data
		Path = InPath;
		Path->AddObserver(FNavigationPath::FPathObserverDelegate::FDelegate::CreateUObject(this, &UPathFollowingComponent::OnPathEvent));
		if (MovementComp && MovementComp->GetOwner())
		{
			Path->SetSourceActor(*(MovementComp->GetOwner()));
		}

		PathTimeWhenPaused = 0.0f;
		OnPathUpdated();

		AcceptanceRadius = InAcceptanceRadius;
		GameData = InGameData;
		OnRequestFinished = OnComplete;
		bStopOnOverlap = bInStopOnOverlap;
		SetDestinationActor(InDestinationActor);

#if ENABLE_VISUAL_LOG
		const FVector CurrentLocation = MovementComp ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()) : FVector::ZeroVector;
		const FVector DestLocation = InPath->GetDestinationLocation();
		const FVector ToDest = DestLocation - CurrentLocation;
		//UE_Vlog(GetOwner(), LogPathFollowingVR, Log, TEXT("RequestMove: accepted, ID(%u) dist2D(%.0f) distZ(%.0f)"),
			CurrentRequestId.GetID(), ToDest.Size2D(), FMath::Abs(ToDest.Z));
#endif // ENABLE_VISUAL_LOG

		// with async pathfinding paths can be incomplete, movement will start after receiving UpdateMove 
		if (Path->IsValid())
		{
			Status = EPathFollowingStatus::Moving;

			// determine with path segment should be followed
			const uint32 CurrentSegment = DetermineStartingPathPoint(InPath.Get());
			SetMoveSegment(CurrentSegment);
		}
		else
		{
			Status = EPathFollowingStatus::Waiting;
			GetWorld()->GetTimerManager().SetTimer(WaitingForPathTimer, this, &UPathFollowingComponent::OnWaitingPathTimeout, WaitingTimeout);
		}
	}

	return CurrentRequestId;
}*/


void UVRPathFollowingComponent::PauseMove(FAIRequestID RequestID, bool bResetVelocity)
{
	//UE_Vlog(GetOwner(), LogPathFollowingVR, Log, TEXT("PauseMove: RequestID(%u)"), RequestID);
	if (Status == EPathFollowingStatus::Paused)
	{
		return;
	}

	if (RequestID.IsEquivalent(GetCurrentRequestId()))
	{
		if (bResetVelocity && MovementComp && MovementComp->CanStopPathFollowing())
		{
			MovementComp->StopMovementKeepPathing();
		}

		LocationWhenPaused = MovementComp ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()) : FVector::ZeroVector;
		PathTimeWhenPaused = Path.IsValid() ? Path->GetTimeStamp() : 0.0f;
		Status = EPathFollowingStatus::Paused;

		UpdateMoveFocus();
	}

	// TODO: pause path updates with goal movement
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
				const FVector CurrentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation());
				const FVector PathPt0 = *ConsideredPath->GetPathPointLocation(0);
				const FVector PathPt1 = *ConsideredPath->GetPathPointLocation(1);
				// making this test in 2d to avoid situation where agent's Z location not being in "navmesh plane"
				// would influence the result
				const float SqDistToFirstPoint = (CurrentLocation - PathPt0).SizeSquared2D();
				const float SqDistToSecondPoint = (CurrentLocation - PathPt1).SizeSquared2D();
				PickedPathPoint = (SqDistToFirstPoint < SqDistToSecondPoint) ? 0 : 1;
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

void UVRPathFollowingComponent::UpdatePathSegment()
{
	if (!Path.IsValid() || MovementComp == NULL)
	{
		AbortMove(TEXT("no path"), FAIRequestID::CurrentRequest, true, false, EPathFollowingMessage::NoPath);
		return;
	}

	if (!Path->IsValid())
	{
		if (!Path->IsWaitingForRepath())
		{
			AbortMove(TEXT("no path"), FAIRequestID::CurrentRequest, true, false, EPathFollowingMessage::NoPath);
		}
		return;
	}

	// if agent has control over its movement, check finish conditions
	const bool bCanReachTarget = MovementComp->CanStopPathFollowing();
	if (bCanReachTarget && Status == EPathFollowingStatus::Moving)
	{
		const FVector CurrentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation());
		const int32 LastSegmentEndIndex = Path->GetPathPoints().Num() - 1;
		const bool bFollowingLastSegment = (MoveSegmentEndIndex >= LastSegmentEndIndex);

		if (bCollidedWithGoal)
		{
			// check if collided with goal actor
			OnSegmentFinished();
			OnPathFinished(EPathFollowingResult::Success);
		}
		else if (HasReachedDestination(CurrentLocation))
		{
			// always check for destination, acceptance radius may cause it to pass before reaching last segment
			OnSegmentFinished();
			OnPathFinished(EPathFollowingResult::Success);
		}
		else if (bFollowingLastSegment)
		{
			// use goal actor for end of last path segment
			// UNLESS it's partial path (can't reach goal)
			if (DestinationActor.IsValid() && Path->IsPartial() == false)
			{
				const FVector AgentLocation = DestinationAgent ? DestinationAgent->GetNavAgentLocation() : DestinationActor->GetActorLocation();
				// note that the condition below requires GoalLocation to be in world space.
				const FVector GoalLocation = FQuatRotationTranslationMatrix(DestinationActor->GetActorQuat(), AgentLocation).TransformPosition(MoveOffset);
				FVector HitLocation;

				if (MyNavData == nullptr //|| MyNavData->DoesNodeContainLocation(Path->GetPathPoints().Last().NodeRef, GoalLocation))
					|| (FVector::DistSquared(GoalLocation, *CurrentDestination) > SMALL_NUMBER &&  MyNavData->Raycast(CurrentLocation, GoalLocation, HitLocation, nullptr) == false))
				{
					CurrentDestination.Set(NULL, GoalLocation);

					//UE_Vlog(this, LogPathFollowingVR, Log, TEXT("Moving directly to move goal rather than following last path segment"));
					//UE_Vlog_LOCATION(this, LogPathFollowingVR, VeryVerbose, GoalLocation, 30, FColor::Green, TEXT("Last-segment-to-actor"));
					//UE_Vlog_SEGMENT(this, LogPathFollowingVR, VeryVerbose, CurrentLocation, GoalLocation, FColor::Green, TEXT_EMPTY);
				}
			}

			UpdateMoveFocus();
		}
		// check if current move segment is finished
		else if (HasReachedCurrentTarget(CurrentLocation))
		{
			OnSegmentFinished();
			SetNextMoveSegment();
		}
	}

	// gather location samples to detect if moving agent is blocked
	if (bCanReachTarget && Status == EPathFollowingStatus::Moving)
	{
		const bool bHasNewSample = UpdateBlockDetection();
		if (bHasNewSample && IsBlocked())
		{
			if (Path->GetPathPoints().IsValidIndex(MoveSegmentEndIndex) && Path->GetPathPoints().IsValidIndex(MoveSegmentStartIndex))
			{
				//LogBlockHelper(GetOwner(), MovementComp, MinAgentRadiusPct, MinAgentHalfHeightPct,
				//	*Path->GetPathPointLocation(MoveSegmentStartIndex),
				//	*Path->GetPathPointLocation(MoveSegmentEndIndex));
			}
			else
			{
				if ((GetOwner() != NULL) && (MovementComp != NULL))
				{
					//UE_Vlog(GetOwner(), LogPathFollowingVR, Verbose, TEXT("Path blocked, but move segment indices are not valid: start %d, end %d of %d"), MoveSegmentStartIndex, MoveSegmentEndIndex, Path->GetPathPoints().Num());
				}
			}
			OnPathFinished(EPathFollowingResult::Blocked);
		}
	}
}

void UVRPathFollowingComponent::FollowPathSegment(float DeltaTime)
{
	if (MovementComp == NULL || !Path.IsValid())
	{
		return;
	}

	//const FVector CurrentLocation = MovementComp->IsMovingOnGround() ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()) : MovementComp->GetActorLocation();
	const FVector CurrentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation());
	const FVector CurrentTarget = GetCurrentTargetLocation();
	FVector MoveVelocity = (CurrentTarget - CurrentLocation) / DeltaTime;

	const int32 LastSegmentStartIndex = Path->GetPathPoints().Num() - 2;
	const bool bNotFollowingLastSegment = (MoveSegmentStartIndex < LastSegmentStartIndex);

	PostProcessMove.ExecuteIfBound(this, MoveVelocity);
	
	MovementComp->RequestDirectMove(MoveVelocity, bNotFollowingLastSegment);
}

/*bool UVRPathFollowingComponent::HasReached(const FVector& TestPoint, float InAcceptanceRadius, bool bExactSpot) const
{
	// simple test for stationary agent, used as early finish condition
	const FVector CurrentLocation = MovementComp ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()) : FVector::ZeroVector;
	const float GoalRadius = 0.0f;
	const float GoalHalfHeight = 0.0f;
	if (InAcceptanceRadius == UPathFollowingComponent::DefaultAcceptanceRadius)
	{
		InAcceptanceRadius = MyDefaultAcceptanceRadius;
	}

	return HasReachedInternal(TestPoint, GoalRadius, GoalHalfHeight, CurrentLocation, InAcceptanceRadius, bExactSpot ? 0.0f : MinAgentRadiusPct);
}

bool UVRPathFollowingComponent::HasReached(const AActor& TestGoal, float InAcceptanceRadius, bool bExactSpot, bool bUseNavAgentGoalLocation) const
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
			const FVector GoalMoveOffset = NavAgent->GetMoveGoalOffset(GetOwner());
			NavAgent->GetMoveGoalReachTest(GetOwner(), GoalMoveOffset, GoalOffset, GoalRadius, GoalHalfHeight);
			TestPoint = FQuatRotationTranslationMatrix(TestGoal.GetActorQuat(), NavAgent->GetNavAgentLocation()).TransformPosition(GoalOffset);
		}
	}

	const FVector CurrentLocation = MovementComp ? (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()) : FVector::ZeroVector;
	return HasReachedInternal(TestPoint, GoalRadius, GoalHalfHeight, CurrentLocation, InAcceptanceRadius, bExactSpot ? 0.0f : MinAgentRadiusPct);
}
*/

bool UVRPathFollowingComponent::HasReachedCurrentTarget(const FVector& CurrentLocation) const
{
	if (MovementComp == NULL)
	{
		return false;
	}

	const FVector CurrentTarget = GetCurrentTargetLocation();
	const FVector CurrentDirection = GetCurrentDirection();

	// check if moved too far
	const FVector ToTarget = (CurrentTarget - (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation()));
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

	FVector AgentLocation = (VRMovementComp != nullptr ? VRMovementComp->GetActorFeetLocation() : MovementComp->GetActorFeetLocation());
	FVector GoalLocation = GetCurrentTargetLocation();
	RadiusThreshold = CurrentAcceptanceRadius;

	if (bFollowingLastSegment)
	{
		GoalLocation = *Path->GetPathPointLocation(Path->GetPathPoints().Num() - 1);
		AgentRadiusPct = MinAgentRadiusPct;

		// take goal's current location, unless path is partial
		if (DestinationActor.IsValid() && !Path->IsPartial())
		{
			if (DestinationAgent)
			{
				FVector GoalOffset;
				DestinationAgent->GetMoveGoalReachTest(GetOwner(), MoveOffset, GoalOffset, GoalRadius, GoalHalfHeight);

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
