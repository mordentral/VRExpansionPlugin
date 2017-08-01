// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRDialComponent.h"

  //=============================================================================
UVRDialComponent::UVRDialComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->bGenerateOverlapEvents = true;
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	bRepGameplayTags = false;

	MovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	BreakDistance = 100.0f;
}

//=============================================================================
UVRDialComponent::~UVRDialComponent()
{
}


void UVRDialComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UVRDialComponent, bRepGameplayTags);
	DOREPLIFETIME_CONDITION(UVRDialComponent, GameplayTags, COND_Custom);
}

void UVRDialComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRDialComponent, GameplayTags, bRepGameplayTags);
}

void UVRDialComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	ResetInitialDialLocation();
}

void UVRDialComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
}

void UVRDialComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) 
{
	// Handle manual tracking here

	// #TODO: Implement auto drop
	/*if (GrippingController->HasGripAuthority(GripInformation) && (CurInteractorLocation - InitialInteractorLocation).Size() >= BreakDistance)
	{
		GrippingController->DropObjectByInterface(this);
		return;
	}*/


	FRotator DeltaRot = (GrippingController->RelativeRotation - LastRotation).GetNormalized();

	CurRotBackEnd += GetAxisValue(DeltaRot);

	if (bDialUsesAngleSnap)
	{
		this->SetRelativeRotation(SetAxisValue(FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement), this->RelativeRotation));
	}
	else
	{
		this->SetRelativeRotation(SetAxisValue(CurRotBackEnd, this->RelativeRotation));
	}

	LastRotation = GrippingController->RelativeRotation;

	// #TODO: should this be relative to original? or leave it as is and initial rotation offsets angle?
	CurrentDialAngle = FRotator::ClampAxis(GetAxisValue(this->RelativeRotation));
}

void UVRDialComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	LastRotation = GrippingController->RelativeRotation;
	CurRotBackEnd = GetAxisValue(this->RelativeRotation);
}

void UVRDialComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) 
{
	if (bDialUsesAngleSnap)
	{
		this->SetRelativeRotation(SetAxisValue(FMath::GridSnap(GetAxisValue(this->RelativeRotation), SnapAngleIncrement), this->RelativeRotation));
		CurrentDialAngle = FRotator::ClampAxis(GetAxisValue(this->RelativeRotation));
	}
}

void UVRDialComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnUsed_Implementation() {}
void UVRDialComponent::OnEndUsed_Implementation() {}
void UVRDialComponent::OnSecondaryUsed_Implementation() {}
void UVRDialComponent::OnEndSecondaryUsed_Implementation() {}

bool UVRDialComponent::DenyGripping_Implementation()
{
	return false;//VRGripInterfaceSettings.bDenyGripping;
}

EGripInterfaceTeleportBehavior UVRDialComponent::TeleportBehavior_Implementation()
{
	return EGripInterfaceTeleportBehavior::DropOnTeleport;
}

bool UVRDialComponent::SimulateOnDrop_Implementation()
{
	return false;
}

EGripCollisionType UVRDialComponent::SlotGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}

EGripCollisionType UVRDialComponent::FreeGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}

ESecondaryGripType UVRDialComponent::SecondaryGripType_Implementation()
{
	return ESecondaryGripType::SG_None;
}


EGripMovementReplicationSettings UVRDialComponent::GripMovementReplicationType_Implementation()
{
	return MovementReplicationSetting;
}

EGripLateUpdateSettings UVRDialComponent::GripLateUpdateSetting_Implementation()
{
	return EGripLateUpdateSettings::LateUpdatesAlwaysOff;
}

float UVRDialComponent::GripStiffness_Implementation()
{
	return 1500.0f;
}

float UVRDialComponent::GripDamping_Implementation()
{
	return 200.0f;
}

FBPAdvGripPhysicsSettings UVRDialComponent::AdvancedPhysicsSettings_Implementation()
{
	return FBPAdvGripPhysicsSettings();
}

float UVRDialComponent::GripBreakDistance_Implementation()
{
	return BreakDistance;
}

void UVRDialComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

void UVRDialComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

bool UVRDialComponent::IsInteractible_Implementation()
{
	return false;
}

void UVRDialComponent::IsHeld_Implementation(UGripMotionControllerComponent *& CurHoldingController, bool & bCurIsHeld)
{
	CurHoldingController = HoldingController;
	bCurIsHeld = bIsHeld;
}

void UVRDialComponent::SetHeld_Implementation(UGripMotionControllerComponent * NewHoldingController, bool bNewIsHeld)
{
	bIsHeld = bNewIsHeld;

	if (bIsHeld)
		HoldingController = NewHoldingController;
	else
		HoldingController = nullptr;
}

FBPInteractionSettings UVRDialComponent::GetInteractionSettings_Implementation()
{
	return FBPInteractionSettings();
}
