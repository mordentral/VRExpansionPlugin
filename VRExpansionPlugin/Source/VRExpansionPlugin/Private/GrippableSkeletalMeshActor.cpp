// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "GrippableSkeletalMeshActor.h"

  //=============================================================================
AGrippableSkeletalMeshActor::AGrippableSkeletalMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VRGripInterfaceSettings.bDenyGripping = false;
	VRGripInterfaceSettings.OnTeleportBehavior = EGripInterfaceTeleportBehavior::TeleportAllComponents;
	VRGripInterfaceSettings.bSimulateOnDrop = true;
	VRGripInterfaceSettings.SlotDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	VRGripInterfaceSettings.FreeDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	//VRGripInterfaceSettings.bCanHaveDoubleGrip = false;
	VRGripInterfaceSettings.SecondaryGripType = ESecondaryGripType::SG_None;
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

	// Default replication on for multiplayer
	this->bNetLoadOnClient = false;
	this->bReplicateMovement = true;
	this->bReplicates = true;

	bRepGripSettingsAndGameplayTags = true;

	// Setting a minimum of every 3rd frame (VR 90fps) for replication consideration
	// Otherwise we will get some massive slow downs if the replication is allowed to hit the 2 per second minimum default
	MinNetUpdateFrequency = 30.0f;
}

void AGrippableSkeletalMeshActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrippableSkeletalMeshActor, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_CONDITION(AGrippableSkeletalMeshActor, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(AGrippableSkeletalMeshActor, GameplayTags, COND_Custom);
}

void AGrippableSkeletalMeshActor::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableSkeletalMeshActor, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableSkeletalMeshActor, GameplayTags, bRepGripSettingsAndGameplayTags);
}


/*void AGrippableSkeletalMeshActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	DOREPLIFETIME(AGrippableSkeletalMeshActor, VRGripInterfaceSettings);
}*/

//=============================================================================
AGrippableSkeletalMeshActor::~AGrippableSkeletalMeshActor()
{
}

void AGrippableSkeletalMeshActor::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) {}
void AGrippableSkeletalMeshActor::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void AGrippableSkeletalMeshActor::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void AGrippableSkeletalMeshActor::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void AGrippableSkeletalMeshActor::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void AGrippableSkeletalMeshActor::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void AGrippableSkeletalMeshActor::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void AGrippableSkeletalMeshActor::OnUsed_Implementation() {}
void AGrippableSkeletalMeshActor::OnEndUsed_Implementation() {}
void AGrippableSkeletalMeshActor::OnSecondaryUsed_Implementation() {}
void AGrippableSkeletalMeshActor::OnEndSecondaryUsed_Implementation() {}

bool AGrippableSkeletalMeshActor::DenyGripping_Implementation()
{
	return VRGripInterfaceSettings.bDenyGripping;
}


EGripInterfaceTeleportBehavior AGrippableSkeletalMeshActor::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool AGrippableSkeletalMeshActor::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripCollisionType AGrippableSkeletalMeshActor::SlotGripType_Implementation()
{
	return VRGripInterfaceSettings.SlotDefaultGripType;
}

EGripCollisionType AGrippableSkeletalMeshActor::FreeGripType_Implementation()
{
	return VRGripInterfaceSettings.FreeDefaultGripType;
}

/*bool AGrippableSkeletalMeshActor::CanHaveDoubleGrip_Implementation()
{
	return VRGripInterfaceSettings.bCanHaveDoubleGrip;
}*/

ESecondaryGripType AGrippableSkeletalMeshActor::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}


/*EGripTargetType AGrippableSkeletalMeshActor::GripTargetType_Implementation()
{
	return VRGripInterfaceSettings.GripTarget;
}*/

EGripMovementReplicationSettings AGrippableSkeletalMeshActor::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings AGrippableSkeletalMeshActor::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

float AGrippableSkeletalMeshActor::GripStiffness_Implementation()
{
	return VRGripInterfaceSettings.ConstraintStiffness;
}

float AGrippableSkeletalMeshActor::GripDamping_Implementation()
{
	return VRGripInterfaceSettings.ConstraintDamping;
}

FBPAdvGripPhysicsSettings AGrippableSkeletalMeshActor::AdvancedPhysicsSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedPhysicsSettings;
}

float AGrippableSkeletalMeshActor::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void AGrippableSkeletalMeshActor::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		OverridePrefix = "VRGripS";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(OverridePrefix, this, WorldLocation, VRGripInterfaceSettings.SecondarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

void AGrippableSkeletalMeshActor::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(OverridePrefix, this, WorldLocation, VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

bool AGrippableSkeletalMeshActor::IsInteractible_Implementation()
{
	return VRGripInterfaceSettings.bIsInteractible;
}

void AGrippableSkeletalMeshActor::IsHeld_Implementation(UGripMotionControllerComponent *& HoldingController, bool & bIsHeld)
{
	HoldingController = VRGripInterfaceSettings.HoldingController;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

void AGrippableSkeletalMeshActor::SetHeld_Implementation(UGripMotionControllerComponent * HoldingController, bool bIsHeld)
{
	if (bIsHeld)
		VRGripInterfaceSettings.HoldingController = HoldingController;
	else
		VRGripInterfaceSettings.HoldingController = nullptr;

	VRGripInterfaceSettings.bIsHeld = bIsHeld;
}


FBPInteractionSettings AGrippableSkeletalMeshActor::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}
