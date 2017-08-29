// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "GrippableSkeletalMeshComponent.h"

  //=============================================================================
UGrippableSkeletalMeshComponent::UGrippableSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VRGripInterfaceSettings.bDenyGripping = false;
	VRGripInterfaceSettings.OnTeleportBehavior = EGripInterfaceTeleportBehavior::DropOnTeleport;
	VRGripInterfaceSettings.bSimulateOnDrop = true;
	VRGripInterfaceSettings.SlotDefaultGripType = EGripCollisionType::ManipulationGrip;
	VRGripInterfaceSettings.FreeDefaultGripType = EGripCollisionType::ManipulationGrip;
	//VRGripInterfaceSettings.bCanHaveDoubleGrip = false;
	VRGripInterfaceSettings.SecondaryGripType = ESecondaryGripType::SG_None;
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

	bReplicateMovement = false;
	//this->bReplicates = true;

	bRepGripSettingsAndGameplayTags = true;
}

void UGrippableSkeletalMeshComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UGrippableSkeletalMeshComponent, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME(UGrippableSkeletalMeshComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UGrippableSkeletalMeshComponent, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(UGrippableSkeletalMeshComponent, GameplayTags, COND_Custom);
}

void UGrippableSkeletalMeshComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UGrippableSkeletalMeshComponent, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(UGrippableSkeletalMeshComponent, GameplayTags, bRepGripSettingsAndGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
}


//=============================================================================
UGrippableSkeletalMeshComponent::~UGrippableSkeletalMeshComponent()
{
}

void UGrippableSkeletalMeshComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) {}
void UGrippableSkeletalMeshComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSkeletalMeshComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSkeletalMeshComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSkeletalMeshComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSkeletalMeshComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UGrippableSkeletalMeshComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UGrippableSkeletalMeshComponent::OnUsed_Implementation() {}
void UGrippableSkeletalMeshComponent::OnEndUsed_Implementation() {}
void UGrippableSkeletalMeshComponent::OnSecondaryUsed_Implementation() {}
void UGrippableSkeletalMeshComponent::OnEndSecondaryUsed_Implementation() {}

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

EGripCollisionType UGrippableSkeletalMeshComponent::SlotGripType_Implementation()
{
	return VRGripInterfaceSettings.SlotDefaultGripType;
}

EGripCollisionType UGrippableSkeletalMeshComponent::FreeGripType_Implementation()
{
	return VRGripInterfaceSettings.FreeDefaultGripType;
}

/*bool UGrippableSkeletalMeshComponent::CanHaveDoubleGrip_Implementation()
{
	return VRGripInterfaceSettings.bCanHaveDoubleGrip;
}*/

ESecondaryGripType UGrippableSkeletalMeshComponent::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
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

FBPAdvGripPhysicsSettings UGrippableSkeletalMeshComponent::AdvancedPhysicsSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedPhysicsSettings;
}

float UGrippableSkeletalMeshComponent::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void UGrippableSkeletalMeshComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		OverridePrefix = "VRGripS";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, VRGripInterfaceSettings.SecondarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

void UGrippableSkeletalMeshComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
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
