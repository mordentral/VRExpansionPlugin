// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRBPDatatypes.h"
#include "VRPathFollowingComponent.h"

#include "VRPlayerController.generated.h"


UCLASS()
class VREXPANSIONPLUGIN_API AVRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVRPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	void InitNavigationControl(UPathFollowingComponent*& PathFollowingComp) override;
};