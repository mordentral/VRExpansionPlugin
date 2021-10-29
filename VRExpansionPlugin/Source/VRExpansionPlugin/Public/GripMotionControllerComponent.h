// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "IMotionController.h"
#include "SceneViewExtension.h"
#include "VRBPDatatypes.h"
#include "MotionControllerComponent.h"
#include "LateUpdateManager.h"
#include "IIdentifiableXRDevice.h" // for FXRDeviceId
#include "IXRTrackingSystem.h"
#include "VRGlobalSettings.h"
#include "Math/DualQuat.h"
#include "XRMotionControllerBase.h"
#include "GripMotionControllerComponent.generated.h"

class AVRBaseCharacter;

/**
 * 
 */



UCLASS()
class VREXPANSIONPLUGIN_API UGripMotionControllerComponent : public UMotionControllerComponent
{
	GENERATED_BODY()
	
};
