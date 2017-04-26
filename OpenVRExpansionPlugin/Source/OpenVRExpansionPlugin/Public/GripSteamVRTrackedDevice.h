// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EngineMinimal.h"
#include "Engine/Engine.h"
#include "IMotionController.h"
#include "SceneViewExtension.h"
#include "VRBPDatatypes.h"
#include "VRGripInterface.h"
#include "OpenVRExpansionFunctionLibrary.h"
#include "GripMotionControllerComponent.h"

#include "GripSteamVRTrackedDevice.generated.h"

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = MotionController, hidecategories = ("MotionController|Types"))
class OPENVREXPANSIONPLUGIN_API UGripSteamVRTrackedDevice : public UGripMotionControllerComponent
{

public:

	GENERATED_UCLASS_BODY()
	~UGripSteamVRTrackedDevice();


	/** Which hand this component should automatically follow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
	EBPVRDeviceIndex TrackedDeviceIndex;
	
	/*UGripMotionControllerComponent(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		TrackedDeviceIndex = EBPVRDeviceIndex::TrackedDevice1;
	}*/

	/** If true, the Position and Orientation args will contain the most recent controller state */
	virtual bool PollControllerState(FVector& Position, FRotator& Orientation, float WorldToMetersScale) override;
};