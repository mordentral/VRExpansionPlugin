// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "GrippableSkeletalMeshComponent.h"

  //=============================================================================
UGrippableSkeletalMeshComponent::UGrippableSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VRGripInterfaceSettings.bDenyGripping = false;
	VRGripInterfaceSettings.OnTeleportBehavior = EGripInterfaceTeleportBehavior::DropOnTeleport;
	VRGripInterfaceSettings.bSimulateOnDrop = true;
	VRGripInterfaceSettings.EnumObjectType = 0;
	VRGripInterfaceSettings.SlotDefaultGripType = EGripCollisionType::ManipulationGrip;
	VRGripInterfaceSettings.FreeDefaultGripType = EGripCollisionType::ManipulationGrip;
	VRGripInterfaceSettings.bCanHaveDoubleGrip = false;
	//VRGripInterfaceSettings.GripTarget = EGripTargetType::ComponentGrip;
	VRGripInterfaceSettings.MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
	VRGripInterfaceSettings.LateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOff;
	VRGripInterfaceSettings.ConstraintStiffness = 1500.0f;
	VRGripInterfaceSettings.ConstraintDamping = 200.0f;
	VRGripInterfaceSettings.ConstraintBreakDistance = 100.0f;
	VRGripInterfaceSettings.SecondarySlotRange = 20.0f;
	VRGripInterfaceSettings.PrimarySlotRange = 20.0f;
	VRGripInterfaceSettings.bIsInteractible = false;

	VRGripInterfaceSettings.bIsHeld = false;
	VRGripInterfaceSettings.HoldingController = nullptr;

	// Setting this for default with multiplayer
	this->bReplicates = true;
}

//=============================================================================
UGrippableSkeletalMeshComponent::~UGrippableSkeletalMeshComponent()
{
}


bool UGrippableSkeletalMeshComponent::DenyGripping_Implementation()
{
	return VRGripInterfaceSettings.bDenyGripping;
}

EGripInterfaceTeleportBehavior UGrippableSkeletalMeshComponent::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool UGrippableSkeletalMeshComponent::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

void UGrippableSkeletalMeshComponent::ObjectType_Implementation(uint8 & ObjectType)
{
	ObjectType = VRGripInterfaceSettings.EnumObjectType;
}

EGripCollisionType UGrippableSkeletalMeshComponent::SlotGripType_Implementation()
{
	return VRGripInterfaceSettings.SlotDefaultGripType;
}

EGripCollisionType UGrippableSkeletalMeshComponent::FreeGripType_Implementation()
{
	return VRGripInterfaceSettings.FreeDefaultGripType;
}

bool UGrippableSkeletalMeshComponent::CanHaveDoubleGrip_Implementation()
{
	return VRGripInterfaceSettings.bCanHaveDoubleGrip;
}

/*EGripTargetType UGrippableSkeletalMeshComponent::GripTargetType_Implementation()
{
	return VRGripInterfaceSettings.GripTarget;
}*/

EGripMovementReplicationSettings UGrippableSkeletalMeshComponent::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings UGrippableSkeletalMeshComponent::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

float UGrippableSkeletalMeshComponent::GripStiffness_Implementation()
{
	return VRGripInterfaceSettings.ConstraintStiffness;
}

float UGrippableSkeletalMeshComponent::GripDamping_Implementation()
{
	return VRGripInterfaceSettings.ConstraintDamping;
}

float UGrippableSkeletalMeshComponent::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void UGrippableSkeletalMeshComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component("VRGripS", this, WorldLocation, VRGripInterfaceSettings.SecondarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

void UGrippableSkeletalMeshComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component("VRGripP", this, WorldLocation, VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

bool UGrippableSkeletalMeshComponent::IsInteractible_Implementation()
{
	return VRGripInterfaceSettings.bIsInteractible;
}

void UGrippableSkeletalMeshComponent::IsHeld_Implementation(UGripMotionControllerComponent *& HoldingController, bool & bIsHeld)
{
	HoldingController = VRGripInterfaceSettings.HoldingController;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

void UGrippableSkeletalMeshComponent::SetHeld_Implementation(UGripMotionControllerComponent * HoldingController, bool bIsHeld)
{
	if (bIsHeld)
		VRGripInterfaceSettings.HoldingController = HoldingController;
	else
		VRGripInterfaceSettings.HoldingController = nullptr;

	VRGripInterfaceSettings.bIsHeld = bIsHeld;
}

FBPInteractionSettings UGrippableSkeletalMeshComponent::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}
