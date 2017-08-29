// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
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

	// Disable the ServerUpdateCamera function defaulted on in PlayerCameraManager
	// We are manually replicating the camera position and rotation ourselves anyway
	// Generally that function will just be additional replication overhead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRPlayerController")
		bool bDisableServerUpdateCamera;

	/** spawn cameras for servers and owning players */
	virtual void SpawnPlayerCameraManager() override;
};