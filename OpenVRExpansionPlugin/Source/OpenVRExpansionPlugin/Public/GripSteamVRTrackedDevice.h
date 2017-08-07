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

// This class is deprecated as of 4.17, please replace it with Motion or GripMotion Controllers as it will be deleted eventually.
UCLASS(Blueprintable,DEPRECATED(4.17, "No longer needed, motion controllers can do that now"), meta = (BlueprintSpawnableComponent), ClassGroup = MotionController, hidecategories = ("MotionController|Types"))
class OPENVREXPANSIONPLUGIN_API UDEPRECATED_GripSteamVRTrackedDevice : public UGripMotionControllerComponent
{

public:

	GENERATED_UCLASS_BODY()
	~UDEPRECATED_GripSteamVRTrackedDevice();


	/** Which hand this component should automatically follow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
	EBPVRDeviceIndex TrackedDeviceIndex;
	
	/*UGripMotionControllerComponent(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer)
	{
		TrackedDeviceIndex = EBPVRDeviceIndex::TrackedDevice1;
	}*/

	/** If true, the Position and Orientation args will contain the most recent controller state */
	virtual bool GripPollControllerState(FVector& Position, FRotator& Orientation, float WorldToMetersScale) override;
};