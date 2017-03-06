// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "GrippableStaticMeshActor.h"

  //=============================================================================
AGrippableStaticMeshActor::AGrippableStaticMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VRGripInterfaceSettings.bDenyGripping = false;
	VRGripInterfaceSettings.OnTeleportBehavior = EGripInterfaceTeleportBehavior::TeleportAllComponents;
	VRGripInterfaceSettings.bSimulateOnDrop = true;
	VRGripInterfaceSettings.EnumObjectType = 0;
	VRGripInterfaceSettings.SlotDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	VRGripInterfaceSettings.FreeDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	VRGripInterfaceSettings.bCanHaveDoubleGrip = false;
	//VRGripInterfaceSettings.GripTarget = EGripTargetType::ActorGrip;
	VRGripInterfaceSettings.MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
	VRGripInterfaceSettings.LateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping;
	VRGripInterfaceSettings.ConstraintStiffness = 1500.0f;
	VRGripInterfaceSettings.ConstraintDamping = 200.0f;
	VRGripInterfaceSettings.ConstraintBreakDistance = 100.0f;
	VRGripInterfaceSettings.SecondarySlotRange = 20.0f;
	VRGripInterfaceSettings.PrimarySlotRange = 20.0f;
	VRGripInterfaceSettings.bIsInteractible = false;

	VRGripInterfaceSettings.bIsHeld = false;
	VRGripInterfaceSettings.HoldingController = nullptr;

	this->SetMobility(EComponentMobility::Movable);

	// Default replication on for multiplayer
	this->bNetLoadOnClient = false;
	this->bReplicateMovement = true;
	this->bReplicates = true;
}

//=============================================================================
AGrippableStaticMeshActor::~AGrippableStaticMeshActor()
{
}


bool AGrippableStaticMeshActor::DenyGripping_Implementation()
{
	return VRGripInterfaceSettings.bDenyGripping;
}


EGripInterfaceTeleportBehavior AGrippableStaticMeshActor::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool AGrippableStaticMeshActor::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

void AGrippableStaticMeshActor::ObjectType_Implementation(uint8 & ObjectType)
{
	ObjectType = VRGripInterfaceSettings.EnumObjectType;
}

EGripCollisionType AGrippableStaticMeshActor::SlotGripType_Implementation()
{
	return VRGripInterfaceSettings.SlotDefaultGripType;
}

EGripCollisionType AGrippableStaticMeshActor::FreeGripType_Implementation()
{
	return VRGripInterfaceSettings.FreeDefaultGripType;
}

bool AGrippableStaticMeshActor::CanHaveDoubleGrip_Implementation()
{
	return VRGripInterfaceSettings.bCanHaveDoubleGrip;
}

/*EGripTargetType AGrippableStaticMeshActor::GripTargetType_Implementation()
{
	return VRGripInterfaceSettings.GripTarget;
}*/

EGripMovementReplicationSettings AGrippableStaticMeshActor::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings AGrippableStaticMeshActor::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

float AGrippableStaticMeshActor::GripStiffness_Implementation()
{
	return VRGripInterfaceSettings.ConstraintStiffness;
}

float AGrippableStaticMeshActor::GripDamping_Implementation()
{
	return VRGripInterfaceSettings.ConstraintDamping;
}

float AGrippableStaticMeshActor::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void AGrippableStaticMeshActor::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName("VRGripS", this, WorldLocation, VRGripInterfaceSettings.SecondarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

void AGrippableStaticMeshActor::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName("VRGripP", this, WorldLocation, VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

bool AGrippableStaticMeshActor::IsInteractible_Implementation()
{
	return VRGripInterfaceSettings.bIsInteractible;
}

void AGrippableStaticMeshActor::IsHeld_Implementation(UGripMotionControllerComponent *& HoldingController, bool & bIsHeld)
{
	HoldingController = VRGripInterfaceSettings.HoldingController;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

void AGrippableStaticMeshActor::SetHeld_Implementation(UGripMotionControllerComponent * HoldingController, bool bIsHeld)
{
	if (bIsHeld)
		VRGripInterfaceSettings.HoldingController = HoldingController;
	else
		VRGripInterfaceSettings.HoldingController = nullptr;

	VRGripInterfaceSettings.bIsHeld = bIsHeld;
}

FBPInteractionSettings AGrippableStaticMeshActor::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}
