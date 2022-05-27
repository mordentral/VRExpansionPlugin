// Fill out your copyright notice in the Description page of Project Settings.

#include "VRGripInterface.h"
#include "UObject/ObjectMacros.h"
#include "GripScripts/VRGripScriptBase.h"
#include "UObject/Interface.h"
 
UVRGripInterface::UVRGripInterface(const class FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
 
}


void IVRGripInterface::Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
{

}