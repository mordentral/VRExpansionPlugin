// Fill out your copyright notice in the Description page of Project Settings.

#include "VRGripScriptBase.h"
 
UVRGripScriptBase::UVRGripScriptBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
//	PrimaryComponentTick.bCanEverTick = false;
//	PrimaryComponentTick.bStartWithTickEnabled = false;
//	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
}


void UVRGripScriptBase::BeginPlay_Implementation() {}
void UVRGripScriptBase::GetWorldTransform_PreStep_Implementation(FTransform & WorldTransform) {}
void UVRGripScriptBase::GetWorldTransform_PostStep_Implementation(FTransform & WorldTransform) {}
void UVRGripScriptBase::GetWorldTransform_Override_Implementation(FTransform & WorldTransform) {}