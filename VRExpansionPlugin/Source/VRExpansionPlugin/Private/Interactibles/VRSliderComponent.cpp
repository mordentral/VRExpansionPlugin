// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRSliderComponent.h"
#include "Net/UnrealNetwork.h"

  //=============================================================================
UVRSliderComponent::UVRSliderComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->bGenerateOverlapEvents = true;
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	bRepGameplayTags = false;
	bReplicateMovement = false;

	MovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	BreakDistance = 100.0f;

	ParentComponent = nullptr;

	InitialRelativeTransform = FTransform::Identity;
	bDenyGripping = false;

	MinSlideDistance = FVector::ZeroVector;
	MaxSlideDistance = FVector::ZeroVector;
	CurrentSliderProgress = 0.0f;

	InitialInteractorLocation = FVector::ZeroVector;
	InitialGripLoc = FVector::ZeroVector;

	bSlideDistanceIsInParentSpace = true;

	SplineComponentToFollow = nullptr;

	bFollowSplineRotationAndScale = false;


	// Set to only overlap with things so that its not ruined by touching over actors
	this->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
}

//=============================================================================
UVRSliderComponent::~UVRSliderComponent()
{
}


void UVRSliderComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UVRSliderComponent, bRepGameplayTags);
	DOREPLIFETIME(UVRSliderComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UVRSliderComponent, GameplayTags, COND_Custom);
}

void UVRSliderComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRSliderComponent, GameplayTags, bRepGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
}

void UVRSliderComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	ResetInitialSliderLocation();
}

void UVRSliderComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Call supers tick (though I don't think any of the base classes to this actually implement it)
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UVRSliderComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) 
{
	// Handle manual tracking here

	FTransform CurrentRelativeTransform;
	FTransform ParentTransform = FTransform::Identity;
	if (ParentComponent.IsValid())
	{
		ParentTransform = ParentComponent->GetComponentTransform();
		// during grip there is no parent so we do this, might as well do it anyway for lerping as well
		CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
	}
	else
	{
		CurrentRelativeTransform = InitialRelativeTransform;
	}

	FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetComponentLocation());

	FVector CalculatedLocation = InitialGripLoc + (CurInteractorLocation - InitialInteractorLocation);

	if (SplineComponentToFollow != nullptr)
	{	
		if (bFollowSplineRotationAndScale)
		{
			FTransform trans = SplineComponentToFollow->FindTransformClosestToWorldLocation(CurrentRelativeTransform.TransformPosition(CalculatedLocation), ESplineCoordinateSpace::World);

			trans = trans * ParentTransform.Inverse();
			trans.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
			 
			this->SetRelativeTransform(trans);
		}
		else
		{
			this->SetRelativeLocation(ParentTransform.InverseTransformPosition(SplineComponentToFollow->FindLocationClosestToWorldLocation(CurrentRelativeTransform.TransformPosition(CalculatedLocation), ESplineCoordinateSpace::World)));
		}

		CurrentSliderProgress = GetCurrentSliderProgress(CurrentRelativeTransform.TransformPosition(CalculatedLocation));
	}
	else
	{
		FVector ClampedLocation = ClampSlideVector(CalculatedLocation);
		this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));

		CurrentSliderProgress = GetCurrentSliderProgress(ClampedLocation * InitialRelativeTransform.GetScale3D());
	}
	
	if ((InitialRelativeTransform.InverseTransformPosition(this->RelativeLocation) - CurInteractorLocation).Size() >= BreakDistance)
	{
		GrippingController->DropObjectByInterface(this);
		return;
	}
}

void UVRSliderComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	ParentComponent = this->GetAttachParent();

	FTransform CurrentRelativeTransform;
	if (ParentComponent.IsValid())
	{
		CurrentRelativeTransform = InitialRelativeTransform * ParentComponent->GetComponentTransform();
	}
	else
	{
		CurrentRelativeTransform = InitialRelativeTransform;
	}

	InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetComponentLocation());
	InitialGripLoc = InitialRelativeTransform.InverseTransformPosition(this->RelativeLocation);
}

void UVRSliderComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) 
{
	//this->SetComponentTickEnabled(false);
}

void UVRSliderComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnUsed_Implementation() {}
void UVRSliderComponent::OnEndUsed_Implementation() {}
void UVRSliderComponent::OnSecondaryUsed_Implementation() {}
void UVRSliderComponent::OnEndSecondaryUsed_Implementation() {}

bool UVRSliderComponent::DenyGripping_Implementation()
{
	return bDenyGripping;
}

EGripInterfaceTeleportBehavior UVRSliderComponent::TeleportBehavior_Implementation()
{
	return EGripInterfaceTeleportBehavior::DropOnTeleport;
}

bool UVRSliderComponent::SimulateOnDrop_Implementation()
{
	return false;
}

EGripCollisionType UVRSliderComponent::SlotGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}

EGripCollisionType UVRSliderComponent::FreeGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}

ESecondaryGripType UVRSliderComponent::SecondaryGripType_Implementation()
{
	return ESecondaryGripType::SG_None;
}


EGripMovementReplicationSettings UVRSliderComponent::GripMovementReplicationType_Implementation()
{
	return MovementReplicationSetting;
}

EGripLateUpdateSettings UVRSliderComponent::GripLateUpdateSetting_Implementation()
{
	return EGripLateUpdateSettings::LateUpdatesAlwaysOff;
}

float UVRSliderComponent::GripStiffness_Implementation()
{
	return 0.0f;
}

float UVRSliderComponent::GripDamping_Implementation()
{
	return 0.0f;
}

FBPAdvGripPhysicsSettings UVRSliderComponent::AdvancedPhysicsSettings_Implementation()
{
	return FBPAdvGripPhysicsSettings();
}

float UVRSliderComponent::GripBreakDistance_Implementation()
{
	return BreakDistance;
}

void UVRSliderComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

void UVRSliderComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

bool UVRSliderComponent::IsInteractible_Implementation()
{
	return false;
}

void UVRSliderComponent::IsHeld_Implementation(UGripMotionControllerComponent *& CurHoldingController, bool & bCurIsHeld)
{
	CurHoldingController = HoldingController;
	bCurIsHeld = bIsHeld;
}

void UVRSliderComponent::SetHeld_Implementation(UGripMotionControllerComponent * NewHoldingController, bool bNewIsHeld)
{
	bIsHeld = bNewIsHeld;

	if (bIsHeld)
		HoldingController = NewHoldingController;
	else
		HoldingController = nullptr;
}

FBPInteractionSettings UVRSliderComponent::GetInteractionSettings_Implementation()
{
	return FBPInteractionSettings();
}
