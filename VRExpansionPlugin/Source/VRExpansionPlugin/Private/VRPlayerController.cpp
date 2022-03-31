// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRPlayerController.h"
#include "AI/NavigationSystemBase.h"
#include "VRBaseCharacterMovementComponent.h"
#include "Engine/Player.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"


AVRPlayerController::AVRPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bDisableServerUpdateCamera = true;
}

void AVRPlayerController::SpawnPlayerCameraManager()
{
	Super::SpawnPlayerCameraManager();
	
	// Turn off the default FOV and position replication of the camera manager, most functions should be sending values anyway and I am replicating
	// the actual camera position myself so this is just wasted bandwidth
	if(PlayerCameraManager != NULL && bDisableServerUpdateCamera)
		PlayerCameraManager->bUseClientSideCameraUpdates = false;
}

// #TODO 4.20: This was removed
/*void AVRPlayerController::InitNavigationControl(UPathFollowingComponent*& PathFollowingComp)
{
	PathFollowingComp = FindComponentByClass<UPathFollowingComponent>();
	if (PathFollowingComp == NULL)
	{
		PathFollowingComp = NewObject<UVRPathFollowingComponent>(this);
		PathFollowingComp->RegisterComponentWithWorld(GetWorld());
		PathFollowingComp->Initialize();
	}
}*/

/*IPathFollowingAgentInterface* AVRPlayerController::GetPathFollowingAgent() const
{
	// Moved spawning the path following component into the path finding logic instead
	return FNavigationSystem::FindPathFollowingAgentForActor(*this);
}*/

void AVRPlayerController::PlayerTick(float DeltaTime)
{

	// #TODO: Should I be only doing this if ticking CMC and CMC is active?
	if (AVRBaseCharacter * VRChar = Cast<AVRBaseCharacter>(GetPawn()))
	{
		// Keep from calling multiple times
		UVRBaseCharacterMovementComponent * BaseCMC = Cast<UVRBaseCharacterMovementComponent>(VRChar->GetMovementComponent());

		if (!BaseCMC || !BaseCMC->bRunControlRotationInMovementComponent)
			return Super::PlayerTick(DeltaTime);

		if (!bShortConnectTimeOut)
		{
			bShortConnectTimeOut = true;
			ServerShortTimeout();
		}

		TickPlayerInput(DeltaTime, DeltaTime == 0.f);
		LastRotationInput = RotationInput;

		if ((Player != NULL) && (Player->PlayerController == this))
		{
			// Validate current state
			bool bUpdateRotation = false;
			if (IsInState(NAME_Playing))
			{
				if (GetPawn() == NULL)
				{
					ChangeState(NAME_Inactive);
				}
				else if (Player && GetPawn() == AcknowledgedPawn && (!BaseCMC || (BaseCMC && !BaseCMC->IsActive())))
				{
					bUpdateRotation = true;
				}
			}

			if (IsInState(NAME_Inactive))
			{
				if (GetLocalRole() < ROLE_Authority)
				{
					SafeServerCheckClientPossession();
				}

				//bUpdateRotation = !IsFrozen();
			}
			else if (IsInState(NAME_Spectating))
			{
				if (GetLocalRole() < ROLE_Authority)
				{
					SafeServerUpdateSpectatorState();
				}

				// Keep it when spectating
				bUpdateRotation = true;
			}

			// Update rotation
			if (bUpdateRotation)
			{
				UpdateRotation(DeltaTime);
			}
		}
	}
	else
	{
		// Not our character, forget it
		Super::PlayerTick(DeltaTime);
	}
}

UVRLocalPlayer::UVRLocalPlayer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}