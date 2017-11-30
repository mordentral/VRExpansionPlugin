// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRBaseCharacter.h"
#include "VRPathFollowingComponent.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"

FName AVRBaseCharacter::LeftMotionControllerComponentName(TEXT("Left Grip Motion Controller"));
FName AVRBaseCharacter::RightMotionControllerComponentName(TEXT("Right Grip Motion Controller"));
FName AVRBaseCharacter::ReplicatedCameraComponentName(TEXT("VR Replicated Camera"));
FName AVRBaseCharacter::ParentRelativeAttachmentComponentName(TEXT("Parent Relative Attachment"));
FName AVRBaseCharacter::SmoothingSceneParentComponentName(TEXT("NetSmoother"));

AVRBaseCharacter::AVRBaseCharacter(const FObjectInitializer& ObjectInitializer)
 : Super(ObjectInitializer.DoNotCreateDefaultSubobject(ACharacter::MeshComponentName).SetDefaultSubobjectClass<UVRBaseCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))

{

	// Remove the movement jitter with slow speeds
	this->ReplicatedMovement.LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;

	if (UCapsuleComponent * cap = GetCapsuleComponent())
	{
		cap->SetCapsuleSize(16.0f, 96.0f);
		cap->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
		cap->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic, ECollisionResponse::ECR_Block);
	}

	NetSmoother = CreateDefaultSubobject<USceneComponent>(AVRBaseCharacter::SmoothingSceneParentComponentName);
	if (NetSmoother)
	{
		NetSmoother->SetupAttachment(RootComponent);
	}

	VRReplicatedCamera = CreateDefaultSubobject<UReplicatedVRCameraComponent>(AVRBaseCharacter::ReplicatedCameraComponentName);
	if (VRReplicatedCamera)
	{
		VRReplicatedCamera->bOffsetByHMD = false;
		VRReplicatedCamera->SetupAttachment(NetSmoother);
		VRReplicatedCamera->OverrideSendTransform = &AVRBaseCharacter::Server_SendTransformCamera;
	}

	VRMovementReference = NULL;
	if (GetMovementComponent())
	{
		VRMovementReference = Cast<UVRBaseCharacterMovementComponent>(GetMovementComponent());
		//AddTickPrerequisiteComponent(this->GetCharacterMovement());
	}

	ParentRelativeAttachment = CreateDefaultSubobject<UParentRelativeAttachmentComponent>(AVRBaseCharacter::ParentRelativeAttachmentComponentName);
	if (ParentRelativeAttachment && VRReplicatedCamera)
	{
		// Moved this to be root relative as the camera late updates were killing how it worked
		ParentRelativeAttachment->SetupAttachment(NetSmoother);
		ParentRelativeAttachment->bOffsetByHMD = false;
	}

	LeftMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(AVRBaseCharacter::LeftMotionControllerComponentName);
	if (LeftMotionController)
	{
		LeftMotionController->SetupAttachment(NetSmoother);
		LeftMotionController->Hand = EControllerHand::Left;
		LeftMotionController->bOffsetByHMD = false;
		// Keep the controllers ticking after movement
		LeftMotionController->AddTickPrerequisiteComponent(GetCharacterMovement());
		LeftMotionController->OverrideSendTransform = &AVRBaseCharacter::Server_SendTransformLeftController;
	}

	RightMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(AVRBaseCharacter::RightMotionControllerComponentName);
	if (RightMotionController)
	{
		RightMotionController->SetupAttachment(NetSmoother);
		RightMotionController->Hand = EControllerHand::Right;
		RightMotionController->bOffsetByHMD = false;
		// Keep the controllers ticking after movement
		RightMotionController->AddTickPrerequisiteComponent(GetCharacterMovement());
		RightMotionController->OverrideSendTransform = &AVRBaseCharacter::Server_SendTransformRightController;
	}

	OffsetComponentToWorld = FTransform(FQuat(0.0f, 0.0f, 0.0f, 1.0f), FVector::ZeroVector, FVector(1.0f));


	// Setting a minimum of every frame for replication consideration (UT uses this value for characters and projectiles).
	// Otherwise we will get some massive slow downs if the replication is allowed to hit the 2 per second minimum default
	MinNetUpdateFrequency = 100.0f;
}

//=============================================================================
void AVRBaseCharacter::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AVRBaseCharacter, SeatInformation, COND_OwnerOnly);
}

bool AVRBaseCharacter::Server_SetSeatedMode_Validate(USceneComponent * SeatParent, bool bSetSeatedMode, FVector_NetQuantize100 TargetLoc, float TargetYaw, float AllowedRadius, float AllowedRadiusThreshold, bool bZeroToHead)
{
	return true;
}

void AVRBaseCharacter::Server_SetSeatedMode_Implementation(USceneComponent * SeatParent, bool bSetSeatedMode, FVector_NetQuantize100 TargetLoc, float TargetYaw, float AllowedRadius, float AllowedRadiusThreshold, bool bZeroToHead)
{
	SetSeatedMode(SeatParent, bSetSeatedMode, TargetLoc, TargetYaw, AllowedRadius, AllowedRadiusThreshold, bZeroToHead);
}

void AVRBaseCharacter::Server_ReZeroSeating_Implementation(FVector_NetQuantize100 NewRelativeHeadLoc, float NewRelativeHeadYaw, bool bZeroToHead = true)
{
	if (FMath::IsNearlyEqual(SeatInformation.StoredYaw, NewRelativeHeadYaw) && SeatInformation.StoredLocation.Equals(NewRelativeHeadLoc))
		return;

	SeatInformation.StoredYaw = NewRelativeHeadYaw;
	SeatInformation.StoredLocation = NewRelativeHeadLoc;

	// Null out Z so we keep feet location if not zeroing to head
	if (!bZeroToHead)
		SeatInformation.StoredLocation.Z = 0.0f;

	OnRep_SeatedCharInfo();
}

bool AVRBaseCharacter::Server_ReZeroSeating_Validate(FVector_NetQuantize100 NewLoc, float NewYaw, bool bZeroToHead = true)
{
	return true;
}

void AVRBaseCharacter::OnCustomMoveActionPerformed_Implementation(EVRMoveAction MoveActionType, FVector MoveActionVector, FRotator MoveActionRotator)
{

}

void AVRBaseCharacter::OnBeginWallPushback_Implementation(FHitResult HitResultOfImpact, bool bHadLocomotionInput, FVector HmdInput)
{

}

void AVRBaseCharacter::OnEndWallPushback_Implementation()
{

}

void AVRBaseCharacter::Server_SendTransformCamera_Implementation(FBPVRComponentPosRep NewTransform)
{
	if(VRReplicatedCamera)
		VRReplicatedCamera->Server_SendCameraTransform_Implementation(NewTransform);
}

bool AVRBaseCharacter::Server_SendTransformCamera_Validate(FBPVRComponentPosRep NewTransform)
{
	return true;
	// Optionally check to make sure that player is inside of their bounds and deny it if they aren't?
}

void AVRBaseCharacter::Server_SendTransformLeftController_Implementation(FBPVRComponentPosRep NewTransform)
{
	if (LeftMotionController)
		LeftMotionController->Server_SendControllerTransform_Implementation(NewTransform);
}

bool AVRBaseCharacter::Server_SendTransformLeftController_Validate(FBPVRComponentPosRep NewTransform)
{
	return true;
	// Optionally check to make sure that player is inside of their bounds and deny it if they aren't?
}

void AVRBaseCharacter::Server_SendTransformRightController_Implementation(FBPVRComponentPosRep NewTransform)
{
	if(RightMotionController)
		RightMotionController->Server_SendControllerTransform_Implementation(NewTransform);
}

bool AVRBaseCharacter::Server_SendTransformRightController_Validate(FBPVRComponentPosRep NewTransform)
{
	return true;
	// Optionally check to make sure that player is inside of their bounds and deny it if they aren't?
}
FVector AVRBaseCharacter::GetTeleportLocation(FVector OriginalLocation)
{	
	return OriginalLocation;
}


void AVRBaseCharacter::NotifyOfTeleport_Implementation()
{
	// Regenerate the capsule offset location - Should be done anyway in the move_impl function, but playing it safe
	//if (VRRootReference)
	//	VRRootReference->GenerateOffsetToWorld();

	if (LeftMotionController)
		LeftMotionController->PostTeleportMoveGrippedActors();

	if (RightMotionController)
		RightMotionController->PostTeleportMoveGrippedActors();
}

void AVRBaseCharacter::ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius, bool bStopOnOverlap, bool bUsePathfinding, bool bProjectDestinationToNavigation, bool bCanStrafe, TSubclassOf<UNavigationQueryFilter> FilterClass, bool bAllowPartialPaths)
{
	UNavigationSystem* NavSys = Controller ? UNavigationSystem::GetCurrent(Controller->GetWorld()) : nullptr;
	if (NavSys == nullptr || Controller == nullptr )
	{
		UE_LOG(LogTemp, Warning, TEXT("UVRSimpleCharacter::ExtendedSimpleMoveToLocation called for NavSys:%s Controller:%s (if any of these is None then there's your problem"),
			*GetNameSafe(NavSys), *GetNameSafe(Controller));
		return;
	}

	UPathFollowingComponent* PFollowComp = nullptr;
	Controller->InitNavigationControl(PFollowComp);

	if (PFollowComp == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExtendedSimpleMoveToLocation - No PathFollowingComponent Found"));
		return;
	}

	if (!PFollowComp->IsPathFollowingAllowed())
	{
		UE_LOG(LogTemp, Warning, TEXT("ExtendedSimpleMoveToLocation - Path Following Movement Is Not Set To Allowed"));
		return;
	}

	EPathFollowingReachMode ReachMode;
	if (bStopOnOverlap)
		ReachMode = EPathFollowingReachMode::OverlapAgent;
	else
		ReachMode = EPathFollowingReachMode::ExactLocation;

	bool bAlreadyAtGoal = false;

	if(UVRPathFollowingComponent * pathcomp = Cast<UVRPathFollowingComponent>(PFollowComp))
		bAlreadyAtGoal = pathcomp->HasReached(GoalLocation, /*EPathFollowingReachMode::OverlapAgent*/ReachMode);
	else
		bAlreadyAtGoal = PFollowComp->HasReached(GoalLocation, /*EPathFollowingReachMode::OverlapAgent*/ReachMode);

	// script source, keep only one move request at time
	if (PFollowComp->GetStatus() != EPathFollowingStatus::Idle)
	{
		if (GetNetMode() == ENetMode::NM_Client)
		{
			// Stop the movement here, not keeping the velocity because it bugs out for clients, might be able to fix.
			PFollowComp->AbortMove(*NavSys, FPathFollowingResultFlags::ForcedScript | FPathFollowingResultFlags::NewRequest
				, FAIRequestID::AnyRequest, /*bAlreadyAtGoal ? */EPathFollowingVelocityMode::Reset /*: EPathFollowingVelocityMode::Keep*/);
		}
		else
		{
			PFollowComp->AbortMove(*NavSys, FPathFollowingResultFlags::ForcedScript | FPathFollowingResultFlags::NewRequest
				, FAIRequestID::AnyRequest, bAlreadyAtGoal ? EPathFollowingVelocityMode::Reset : EPathFollowingVelocityMode::Keep);
		}
	}

	if (bAlreadyAtGoal)
	{
		PFollowComp->RequestMoveWithImmediateFinish(EPathFollowingResult::Success);
	}
	else
	{
		const ANavigationData* NavData = NavSys->GetNavDataForProps(Controller->GetNavAgentPropertiesRef());
		if (NavData)
		{
			FPathFindingQuery Query(Controller, *NavData, Controller->GetNavAgentLocation(), GoalLocation);
			FPathFindingResult Result = NavSys->FindPathSync(Query);
			if (Result.IsSuccessful())
			{
				FAIMoveRequest MoveReq(GoalLocation);
				MoveReq.SetUsePathfinding(bUsePathfinding);
				MoveReq.SetAllowPartialPath(bAllowPartialPaths);
				MoveReq.SetProjectGoalLocation(bProjectDestinationToNavigation);
				MoveReq.SetNavigationFilter(*FilterClass ? FilterClass : DefaultNavigationFilterClass);
				MoveReq.SetAcceptanceRadius(AcceptanceRadius);
				MoveReq.SetReachTestIncludesAgentRadius(bStopOnOverlap);
				MoveReq.SetCanStrafe(bCanStrafe);
				MoveReq.SetReachTestIncludesGoalRadius(true);

				PFollowComp->RequestMove(/*FAIMoveRequest(GoalLocation)*/MoveReq, Result.Path);
			}
			else if (PFollowComp->GetStatus() != EPathFollowingStatus::Idle)
			{
				PFollowComp->RequestMoveWithImmediateFinish(EPathFollowingResult::Invalid);
			}
		}
	}
}