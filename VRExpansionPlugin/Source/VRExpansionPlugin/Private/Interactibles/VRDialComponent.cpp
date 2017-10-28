// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRDialComponent.h"
#include "Net/UnrealNetwork.h"

  //=============================================================================
UVRDialComponent::UVRDialComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->bGenerateOverlapEvents = true;
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	bRepGameplayTags = false;
	bReplicateMovement = false;

	DialRotationAxis = EVRInteractibleAxis::Axis_Z;
	InteractorRotationAxis = EVRInteractibleAxis::Axis_X;

	bDialUsesAngleSnap = false;
	SnapAngleThreshold = 0.0f;
	SnapAngleIncrement = 45.0f;
	LastSnapAngle = 0.0f;
	RotationScaler = 1.0f;

	ClockwiseMaximumDialAngle = 180.0f;
	CClockwiseMaximumDialAngle = 180.0f;
	bDenyGripping = false;

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
	DOREPLIFETIME(UVRDialComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UVRDialComponent, GameplayTags, COND_Custom);
}

void UVRDialComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRDialComponent, GameplayTags, bRepGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
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

	// Handle the auto drop
	if (GrippingController->HasGripAuthority(GripInformation))
	{
		FVector CurInteractorLocation = GrippingController->GetComponentTransform().GetRelativeTransform(this->GetComponentTransform()).GetTranslation();
		if (FVector::DistSquared(InitialInteractorLocation, CurInteractorLocation) >= FMath::Square(BreakDistance))
		{
			GrippingController->DropObjectByInterface(this);
			return;
		}
	}

	float DeltaRot = RotationScaler * GetAxisValue((GrippingController->RelativeRotation - LastRotation).GetNormalized(), InteractorRotationAxis);
	AddDialAngle(DeltaRot, true);

	LastRotation = GrippingController->RelativeRotation;
}

void UVRDialComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	InitialInteractorLocation = GrippingController->GetComponentTransform().GetRelativeTransform(this->GetComponentTransform()).GetTranslation();
	
	// Need to rotate this by original hand to dial facing eventually
	LastRotation = GrippingController->RelativeRotation;
	this->SetComponentTickEnabled(true);
}

void UVRDialComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) 
{
	if (bDialUsesAngleSnap && FMath::Abs(FMath::Fmod(CurRotBackEnd, SnapAngleIncrement)) <= FMath::Min(SnapAngleIncrement, SnapAngleThreshold))
	{
		this->SetRelativeRotation((FTransform(SetAxisValue(FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement), FRotator::ZeroRotator, DialRotationAxis)) * InitialRelativeTransform).Rotator());		
		CurRotBackEnd = FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement);
		CurrentDialAngle = FRotator::ClampAxis(FMath::RoundToFloat(CurRotBackEnd));
	}

	this->SetComponentTickEnabled(false);
}

void UVRDialComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnUsed_Implementation() {}
void UVRDialComponent::OnEndUsed_Implementation() {}
void UVRDialComponent::OnSecondaryUsed_Implementation() {}
void UVRDialComponent::OnEndSecondaryUsed_Implementation() {}
void UVRDialComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}

bool UVRDialComponent::DenyGripping_Implementation()
{
	return bDenyGripping;
}

EGripInterfaceTeleportBehavior UVRDialComponent::TeleportBehavior_Implementation()
{
	return EGripInterfaceTeleportBehavior::DropOnTeleport;
}

bool UVRDialComponent::SimulateOnDrop_Implementation()
{
	return false;
}

/*EGripCollisionType UVRDialComponent::SlotGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}

EGripCollisionType UVRDialComponent::FreeGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}*/

EGripCollisionType UVRDialComponent::GetPrimaryGripType_Implementation(bool bIsSlot)
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

/*float UVRDialComponent::GripStiffness_Implementation()
{
	return 1500.0f;
}

float UVRDialComponent::GripDamping_Implementation()
{
	return 200.0f;
}*/

void UVRDialComponent::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = 0.0f;
	GripDampingOut = 0.0f;
}

FBPAdvGripSettings UVRDialComponent::AdvancedGripSettings_Implementation()
{
	return FBPAdvGripSettings();
}

float UVRDialComponent::GripBreakDistance_Implementation()
{
	return BreakDistance;
}

/*void UVRDialComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

void UVRDialComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}*/

void UVRDialComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
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
