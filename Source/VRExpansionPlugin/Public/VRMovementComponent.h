// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
//#include "AI/Navigation/NavigationAvoidanceTypes.h"
//#include "AI/RVOAvoidanceInterface.h"
//#include "Animation/AnimationAsset.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
//#include "GameFramework/PawnMovementComponent.h"
//#include "Interfaces/NetworkPredictionInterface.h"
#include "WorldCollision.h"
//#include "GameFramework/RootMotionSource.h"
#include "VRCharacterMovementComponent.generated.h"

//class FDebugDisplayInfo;
//class ACharacter;
//class UVRCharacterMovementComponent;

UCLASS()
class VREXPANSIONPLUGIN_API UVRCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:

	/**
	 * Default UObject constructor.
	 */
	UVRCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};