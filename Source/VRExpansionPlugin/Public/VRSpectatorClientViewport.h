// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRBPDatatypes.h"
#include "VRBaseCharacter.h"
#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "VRSimpleCharacterMovementComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "ReplicatedVRCameraComponent.h"
//#include "VRSimpleRootComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRSpectatorClientViewport.generated.h"


UCLASS()
class VREXPANSIONPLUGIN_API UVRSpectatorClientViewport : public UGameViewportClient
{
	GENERATED_UCLASS_BODY()

public:

	virtual void UVRSpectatorClientViewport::UpdateActiveSplitscreenType() override;
};