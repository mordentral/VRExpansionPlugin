// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "WorldCollision.h"
#include "VRMovementComponent.generated.h"

//class FDebugDisplayInfo;
//class ACharacter;
//class UVRCharacterMovementComponent;

UCLASS()
class VREXPANSIONPLUGIN_API UVRMovementComponent : public UMovementComponent
{
	GENERATED_BODY()
public:

	/**
	 * Default UObject constructor.
	 */
	UVRMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};