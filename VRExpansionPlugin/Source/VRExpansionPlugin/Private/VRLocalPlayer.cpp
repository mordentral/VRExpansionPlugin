// Fill out your copyright notice in the Description page of Project Settings.


#include "VRLocalPlayer.h"
#include "VRPlayerController.h"

//override fix to flickering caused by networking replication of local controller
//Set this class to your project settings LocalPlayer class
UVRLocalPlayer::UVRLocalPlayer(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PendingLevelPlayerControllerClass = AVRPlayerController::StaticClass();
}