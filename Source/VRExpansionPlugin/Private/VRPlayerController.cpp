// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"

#include "VRPlayerController.h"


AVRPlayerController::AVRPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

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