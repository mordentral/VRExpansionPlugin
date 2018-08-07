// Fill out your copyright notice in the Description page of Project Settings.

#include "GripScripts/VRGripScriptBase.h"
 
UVRGripScriptBase::UVRGripScriptBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
//	PrimaryComponentTick.bCanEverTick = false;
//	PrimaryComponentTick.bStartWithTickEnabled = false;
//	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;

	WorldTransformOverrideType = EGSTransformOverrideType::None;
}


//void UVRGripScriptBase::BeginPlay_Implementation() {}
void UVRGripScriptBase::GetWorldTransform_Implementation(UGripMotionControllerComponent* GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface) {}
void UVRGripScriptBase::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRGripScriptBase::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}

 
EGSTransformOverrideType UVRGripScriptBase::GetWorldTransformOverrideType_Implementation() { return WorldTransformOverrideType; }
bool UVRGripScriptBase::IsScriptActive_Implementation() { return bIsActive; }
bool UVRGripScriptBase::Wants_DenyAutoDrop_Implementation() { return false; }
//bool UVRGripScriptBase::Wants_DenyTeleport_Implementation() { return false; }