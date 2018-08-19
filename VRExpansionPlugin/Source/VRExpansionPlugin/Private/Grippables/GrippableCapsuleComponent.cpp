// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/GrippableCapsuleComponent.h"
#include "Net/UnrealNetwork.h"

  //=============================================================================
UGrippableCapsuleComponent::UGrippableCapsuleComponent(const FObjectInitializer& ObjectInitializer)
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
	//VRGripInterfaceSettings.bIsInteractible = false;

	VRGripInterfaceSettings.bIsHeld = false;
	VRGripInterfaceSettings.HoldingController = nullptr;

	bReplicateMovement = false;
	//this->bReplicates = true;

	bRepGripSettingsAndGameplayTags = true;
}

void UGrippableCapsuleComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME/*_CONDITION*/(UGrippableCapsuleComponent, GripLogicScripts);// , COND_Custom);
	DOREPLIFETIME(UGrippableCapsuleComponent, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME(UGrippableCapsuleComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UGrippableCapsuleComponent, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(UGrippableCapsuleComponent, GameplayTags, COND_Custom);
}

void UGrippableCapsuleComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UGrippableCapsuleComponent, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(UGrippableCapsuleComponent, GameplayTags, bRepGripSettingsAndGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
}

bool UGrippableCapsuleComponent::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script && !Script->IsPendingKill())
		{
			WroteSomething |= Channel->ReplicateSubobject(Script, *Bunch, *RepFlags);
		}
	}

	return WroteSomething;
}

//=============================================================================
UGrippableCapsuleComponent::~UGrippableCapsuleComponent()
{
}

void UGrippableCapsuleComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	// Call all grip scripts begin play events so they can perform any needed logic
	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script)
		{
			Script->OnBeginPlay(this);
		}
	}
}

void UGrippableCapsuleComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) {}
void UGrippableCapsuleComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableCapsuleComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void UGrippableCapsuleComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UGrippableCapsuleComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void UGrippableCapsuleComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UGrippableCapsuleComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UGrippableCapsuleComponent::OnUsed_Implementation() {}
void UGrippableCapsuleComponent::OnEndUsed_Implementation() {}
void UGrippableCapsuleComponent::OnSecondaryUsed_Implementation() {}
void UGrippableCapsuleComponent::OnEndSecondaryUsed_Implementation() {}
void UGrippableCapsuleComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
bool UGrippableCapsuleComponent::RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) { return false; }


bool UGrippableCapsuleComponent::DenyGripping_Implementation()
{
	return VRGripInterfaceSettings.bDenyGripping;
}

EGripInterfaceTeleportBehavior UGrippableCapsuleComponent::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool UGrippableCapsuleComponent::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripCollisionType UGrippableCapsuleComponent::GetPrimaryGripType_Implementation(bool bIsSlot)
{
	return bIsSlot ? VRGripInterfaceSettings.SlotDefaultGripType : VRGripInterfaceSettings.FreeDefaultGripType;
}

ESecondaryGripType UGrippableCapsuleComponent::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}

EGripMovementReplicationSettings UGrippableCapsuleComponent::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings UGrippableCapsuleComponent::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

void UGrippableCapsuleComponent::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = VRGripInterfaceSettings.ConstraintStiffness;
	GripDampingOut = VRGripInterfaceSettings.ConstraintDamping;
}

FBPAdvGripSettings UGrippableCapsuleComponent::AdvancedGripSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedGripSettings;
}

float UGrippableCapsuleComponent::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void UGrippableCapsuleComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, bSecondarySlot ? VRGripInterfaceSettings.SecondarySlotRange : VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform);
}

/*bool UGrippableCapsuleComponent::IsInteractible_Implementation()
{
	return VRGripInterfaceSettings.bIsInteractible;
}*/

void UGrippableCapsuleComponent::IsHeld_Implementation(UGripMotionControllerComponent *& HoldingController, bool & bIsHeld)
{
	HoldingController = VRGripInterfaceSettings.HoldingController;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

void UGrippableCapsuleComponent::SetHeld_Implementation(UGripMotionControllerComponent * HoldingController, bool bIsHeld)
{
	if (bIsHeld)
		VRGripInterfaceSettings.HoldingController = HoldingController;
	else
		VRGripInterfaceSettings.HoldingController = nullptr;

	VRGripInterfaceSettings.bIsHeld = bIsHeld;
}

/*FBPInteractionSettings UGrippableCapsuleComponent::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}*/

bool UGrippableCapsuleComponent::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	ArrayReference = GripLogicScripts;
	return GripLogicScripts.Num() > 0;
}