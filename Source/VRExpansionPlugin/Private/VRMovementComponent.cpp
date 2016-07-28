// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Movement.cpp: Character movement implementation

=============================================================================*/

#include "VRExpansionPluginPrivatePCH.h"
//#include "GameFramework/PhysicsVolume.h"
//#include "GameFramework/GameNetworkManager.h"
//#include "GameFramework/Character.h"
#include "VRCharacterMovementComponent.h"
//#include "GameFramework/GameState.h"
//#include "Components/PrimitiveComponent.h"
//#include "Animation/AnimMontage.h"
//#include "PhysicsEngine/DestructibleActor.h"

// @todo this is here only due to circular dependency to AIModule. To be removed
//#include "Navigation/PathFollowingComponent.h"
//#include "AI/Navigation/AvoidanceManager.h"
//#include "Components/CapsuleComponent.h"
//#include "Components/BrushComponent.h"
//#include "Components/DestructibleComponent.h"

//#include "Engine/DemoNetDriver.h"
//#include "Engine/NetworkObjectList.h"

//#include "PerfCountersHelpers.h"


UVRCharacterMovementComponent::UVRCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PostPhysicsTickFunction.bCanEverTick = true;
	PostPhysicsTickFunction.bStartWithTickEnabled = false;
	PostPhysicsTickFunction.TickGroup = TG_PostPhysics;
}