// Fill out your copyright notice in the Description page of Project Settings.


#include "VRCameraManager.h"

//cameramanager class with Fade set to black remember to fade back in on begin play of your character class or controller
AVRCameraManager::AVRCameraManager(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetManualCameraFade(1, FColor(0), false);
}