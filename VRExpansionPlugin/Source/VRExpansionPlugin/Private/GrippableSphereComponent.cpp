// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "GrippableSphereComponent.h"

  //=============================================================================
UGrippableSphereComponent::UGrippableSphereComponent(const FObjectInitializer& ObjectInitializer)
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
	bRepGripSettingsAndGameplayTags = true;
}

void UGrippableSphereComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UGrippableSphereComponent, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_CONDITION(UGrippableSphereComponent, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(UGrippableSphereComponent, GameplayTags, COND_Custom);
}

void UGrippableSphereComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UGrippableSphereComponent, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(UGrippableSphereComponent, GameplayTags, bRepGripSettingsAndGameplayTags);
}


//=============================================================================
UGrippableSphereComponent::~UGrippableSphereComponent()
{
}

void UGrippableSphereComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, FVector MControllerLocDelta, float DeltaTime) {}
void UGrippableSphereComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSphereComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSphereComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSphereComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableSphereComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UGrippableSphereComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UGrippableSphereComponent::OnUsed_Implementation() {}
void UGrippableSphereComponent::OnEndUsed_Implementation() {}
void UGrippableSphereComponent::OnSecondaryUsed_Implementation() {}
void UGrippableSphereComponent::OnEndSecondaryUsed_Implementation() {}

bool UGrippableSphereComponent::DenyGripping_Implementation()
{
	return VRGripInterfaceSettings.bDenyGripping;
}

EGripInterfaceTeleportBehavior UGrippableSphereComponent::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool UGrippableSphereComponent::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripCollisionType UGrippableSphereComponent::SlotGripType_Implementation()
{
	return VRGripInterfaceSettings.SlotDefaultGripType;
}

EGripCollisionType UGrippableSphereComponent::FreeGripType_Implementation()
{
	return VRGripInterfaceSettings.FreeDefaultGripType;
}

/*bool UGrippableSphereComponent::CanHaveDoubleGrip_Implementation()
{
	return VRGripInterfaceSettings.bCanHaveDoubleGrip;
}*/

ESecondaryGripType UGrippableSphereComponent::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}


/*EGripTargetType UGrippableSphereComponent::GripTargetType_Implementation()
{
	return VRGripInterfaceSettings.GripTarget;
}*/

EGripMovementReplicationSettings UGrippableSphereComponent::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings UGrippableSphereComponent::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

float UGrippableSphereComponent::GripStiffness_Implementation()
{
	return VRGripInterfaceSettings.ConstraintStiffness;
}

float UGrippableSphereComponent::GripDamping_Implementation()
{
	return VRGripInterfaceSettings.ConstraintDamping;
}

FBPAdvGripPhysicsSettings UGrippableSphereComponent::AdvancedPhysicsSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedPhysicsSettings;
}

float UGrippableSphereComponent::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void UGrippableSphereComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		OverridePrefix = "VRGripS";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, VRGripInterfaceSettings.SecondarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

void UGrippableSphereComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

bool UGrippableSphereComponent::IsInteractible_Implementation()
{
	return VRGripInterfaceSettings.bIsInteractible;
}

void UGrippableSphereComponent::IsHeld_Implementation(UGripMotionControllerComponent *& HoldingController, bool & bIsHeld)
{
	HoldingController = VRGripInterfaceSettings.HoldingController;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

void UGrippableSphereComponent::SetHeld_Implementation(UGripMotionControllerComponent * HoldingController, bool bIsHeld)
{
	if (bIsHeld)
		VRGripInterfaceSettings.HoldingController = HoldingController;
	else
		VRGripInterfaceSettings.HoldingController = nullptr;

	VRGripInterfaceSettings.bIsHeld = bIsHeld;
}

FBPInteractionSettings UGrippableSphereComponent::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}
