// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "GrippableStaticMeshComponent.h"

  //=============================================================================
UGrippableStaticMeshComponent::UGrippableStaticMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bDenyGripping = false;
	OnTeleportBehavior = EGripInterfaceTeleportBehavior::DropOnTeleport;
	bSimulateOnDrop = true;
	EnumObjectType = 0;
	SlotDefaultGripType = EGripCollisionType::ManipulationGrip;
	FreeDefaultGripType = EGripCollisionType::ManipulationGrip;
	bCanHaveDoubleGrip = false;
	GripTarget = EGripTargetType::ComponentGrip;
	MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
	ConstraintStiffness = 1500.0f;
	ConstraintDamping = 200.0f;
	SecondarySlotRange = 20.0f;
	PrimarySlotRange = 20.0f;
	bIsInteractible = false;
	//	FBPInteractionSettings sInteractionSettings;
}

//=============================================================================
UGrippableStaticMeshComponent::~UGrippableStaticMeshComponent()
{
}


bool UGrippableStaticMeshComponent::DenyGripping_Implementation()
{
	return bDenyGripping;
}


EGripInterfaceTeleportBehavior UGrippableStaticMeshComponent::TeleportBehavior_Implementation()
{
	return OnTeleportBehavior;
}

bool UGrippableStaticMeshComponent::SimulateOnDrop_Implementation()
{
	return bSimulateOnDrop;
}

void UGrippableStaticMeshComponent::ObjectType_Implementation(uint8 & ObjectType)
{
	ObjectType = EnumObjectType;
}

EGripCollisionType UGrippableStaticMeshComponent::SlotGripType_Implementation()
{
	return SlotDefaultGripType;
}

EGripCollisionType UGrippableStaticMeshComponent::FreeGripType_Implementation()
{
	return FreeDefaultGripType;
}

bool UGrippableStaticMeshComponent::CanHaveDoubleGrip_Implementation()
{
	return bCanHaveDoubleGrip;
}

EGripTargetType UGrippableStaticMeshComponent::GripTargetType_Implementation()
{
	return GripTarget;
}

EGripMovementReplicationSettings UGrippableStaticMeshComponent::GripMovementReplicationType_Implementation()
{
	return MovementReplicationType;
}

float UGrippableStaticMeshComponent::GripStiffness_Implementation()
{
	return ConstraintStiffness;
}

float UGrippableStaticMeshComponent::GripDamping_Implementation()
{
	return ConstraintDamping;
}

void UGrippableStaticMeshComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component("VRGripS", this, WorldLocation, SecondarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

void UGrippableStaticMeshComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component("VRGripP", this, WorldLocation, PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

bool UGrippableStaticMeshComponent::IsInteractible_Implementation()
{
	return bIsInteractible;
}

FBPInteractionSettings UGrippableStaticMeshComponent::GetInteractionSettings_Implementation()
{
	return InteractionSettings;
}

