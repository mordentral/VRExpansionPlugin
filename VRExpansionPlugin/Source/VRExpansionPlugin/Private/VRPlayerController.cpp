// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRPlayerController.h"
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

void AVRPlayerController::InitNavigationControl(UPathFollowingComponent*& PathFollowingComp)
{
	PathFollowingComp = FindComponentByClass<UPathFollowingComponent>();
	if (PathFollowingComp == NULL)
	{
		PathFollowingComp = NewObject<UVRPathFollowingComponent>(this);
		PathFollowingComp->RegisterComponentWithWorld(GetWorld());
		PathFollowingComp->Initialize();
	}
}