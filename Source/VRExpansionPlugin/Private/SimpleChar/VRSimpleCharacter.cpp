// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"

#include "VRSimpleCharacter.h"

AVRSimpleCharacter::AVRSimpleCharacter(const FObjectInitializer& ObjectInitializer)
 : Super(ObjectInitializer.DoNotCreateDefaultSubobject(ACharacter::MeshComponentName).SetDefaultSubobjectClass<UVRSimpleRootComponent>(ACharacter::CapsuleComponentName).SetDefaultSubobjectClass<UVRSimpleCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))

{
	//VRRootReference = NULL;
	if (UCapsuleComponent * cap = GetCapsuleComponent())
	{
		cap->SetCapsuleSize(16.0f, 96.0f);
		//VRRootReference->VRCapsuleOffset = FVector(-8.0f, 0.0f, 0.0f);
		cap->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
		cap->SetCollisionResponseToChannel(ECollisionChannel::ECC_WorldStatic, ECollisionResponse::ECR_Block);
	}

	VRMovementReference = Cast<UVRSimpleCharacterMovementComponent>(GetMovementComponent());

	VRSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("VR Scene Component"));

	if (VRSceneComponent)
	{
		VRSceneComponent->SetupAttachment(RootComponent);
		VRSceneComponent->SetRelativeLocation(FVector(0, 0, -96));
	}

	VRReplicatedCamera = CreateDefaultSubobject<UReplicatedVRSimpleCameraComponent>(TEXT("VR Replicated Camera"));
	if (VRReplicatedCamera && VRSceneComponent)
	{
		VRReplicatedCamera->SetupAttachment(VRSceneComponent);
		VRReplicatedCamera->AddTickPrerequisiteComponent(VRMovementReference);
		//VRReplicatedCamera->SetupAttachment(RootComponent);
	}

	ParentRelativeAttachment = CreateDefaultSubobject<UParentRelativeAttachmentComponent>(TEXT("Parent Relative Attachment"));
	if (ParentRelativeAttachment && VRReplicatedCamera && VRSceneComponent)
	{
		// Moved this to be root relative as the camera late updates were killing how it worked
		//ParentRelativeAttachment->SetupAttachment(RootComponent);
		ParentRelativeAttachment->SetupAttachment(VRSceneComponent);
	}

	LeftMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Left Grip Motion Controller"));
	if (LeftMotionController)
	{
		if (VRSceneComponent)
		{
			LeftMotionController->SetupAttachment(VRSceneComponent);
		}
		//LeftMotionController->SetupAttachment(RootComponent);
		LeftMotionController->Hand = EControllerHand::Left;
		LeftMotionController->bOffsetByHMD = true;
		// Keep the controllers ticking after movement
		if (VRReplicatedCamera)
		{
			LeftMotionController->AddTickPrerequisiteComponent(VRMovementReference);
		}


	}

	RightMotionController = CreateDefaultSubobject<UGripMotionControllerComponent>(TEXT("Right Grip Motion Controller"));
	if (RightMotionController)
	{
		if (VRSceneComponent)
		{
			RightMotionController->SetupAttachment(VRSceneComponent);
		}
		//RightMotionController->SetupAttachment(RootComponent);
		RightMotionController->Hand = EControllerHand::Right;
		RightMotionController->bOffsetByHMD = true;
		// Keep the controllers ticking after movement
		if (VRReplicatedCamera)
		{
			RightMotionController->AddTickPrerequisiteComponent(VRMovementReference);
		}
	}
}


FVector AVRSimpleCharacter::GetTeleportLocation(FVector OriginalLocation)
{
	//FVector modifier = VRRootReference->GetVRLocation() - this->GetActorLocation();
	//modifier.Z = 0.0f; // Null out Z
	//return OriginalLocation - modifier;

	return OriginalLocation;
}

bool AVRSimpleCharacter::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest, bool bNoCheck)
{
	bool bTeleportSucceeded = Super::TeleportTo(DestLocation + GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), DestRotation, bIsATest, bNoCheck);

	if (bTeleportSucceeded)
	{
		NotifyOfTeleport();
	}

	return bTeleportSucceeded;
}

void AVRSimpleCharacter::NotifyOfTeleport_Implementation()
{
	// Regenerate the capsule offset location - Should be done anyway in the move_impl function, but playing it safe
	//if (VRRootReference)
	//	VRRootReference->GenerateOffsetToWorld();

	if (LeftMotionController)
		LeftMotionController->PostTeleportMoveGrippedActors();

	if (RightMotionController)
		RightMotionController->PostTeleportMoveGrippedActors();
}

void AVRSimpleCharacter::ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius, bool bStopOnOverlap, bool bUsePathfinding, bool bProjectDestinationToNavigation, bool bCanStrafe, TSubclassOf<UNavigationQueryFilter> FilterClass, bool bAllowPartialPaths)
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