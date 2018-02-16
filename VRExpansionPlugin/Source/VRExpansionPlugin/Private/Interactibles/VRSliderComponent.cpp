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
	MaxSlideDistance = FVector(10.0f, 0.f, 0.f);
	CurrentSliderProgress = 0.0f;

	InitialInteractorLocation = FVector::ZeroVector;
	InitialGripLoc = FVector::ZeroVector;

	bSlideDistanceIsInParentSpace = true;

	SplineComponentToFollow = nullptr;

	bFollowSplineRotationAndScale = false;
	bLerpAlongSpline = true;

	GripPriority = 1;
	LastSliderProgressState = -1.0f;
	LastInputKey = 0.0f;

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

	/*FTransform CurrentRelativeTransform;
	FTransform ParentTransform = FTransform::Identity;
	if (ParentComponent.IsValid())
	{
		ParentTransform = ParentComponent->GetComponentTransform();
		// during grip there is no parent so we do this, might as well do it anyway for lerping as well
		CurrentRelativeTransform = InitialRelativeTransform * ParentTransform
	}
	else
	{
		CurrentRelativeTransform = InitialRelativeTransform;
	}*/

	FTransform ParentTransform = GetCurrentParentTransform();
	FTransform CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
	FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetComponentLocation());

	FVector CalculatedLocation = InitialGripLoc + (CurInteractorLocation - InitialInteractorLocation);

	FVector origloc = GrippingController->GetComponentLocation();
	FVector relloc = CurInteractorLocation;
	FVector finalloc = CurrentRelativeTransform.TransformPosition(relloc);


	if (SplineComponentToFollow != nullptr)
	{	
		FVector WorldCalculatedLocation = CurrentRelativeTransform.TransformPosition(CalculatedLocation);
		float ClosestKey = 0.0f;
		if (bEnforceSplineLinearity && (ClosestKey = (SplineComponentToFollow->FindInputKeyClosestToWorldLocation(WorldCalculatedLocation) - LastInputKey)) >= 1.9f)
		{
		}
		else
		{
			LastInputKey = FMath::TruncToFloat(ClosestKey);
			if (bFollowSplineRotationAndScale)
			{
				FTransform trans = SplineComponentToFollow->FindTransformClosestToWorldLocation(WorldCalculatedLocation, ESplineCoordinateSpace::World);
				trans = trans * ParentTransform.Inverse();
				trans.MultiplyScale3D(InitialRelativeTransform.GetScale3D());

				this->SetRelativeTransform(trans);
			}
			else
			{
				this->SetRelativeLocation(ParentTransform.InverseTransformPosition(SplineComponentToFollow->FindLocationClosestToWorldLocation(WorldCalculatedLocation, ESplineCoordinateSpace::World)));
			}

			CurrentSliderProgress = GetCurrentSliderProgress(WorldCalculatedLocation);
		}
	}
	else
	{
		FVector ClampedLocation = ClampSlideVector(CalculatedLocation);
		this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));
		CurrentSliderProgress = GetCurrentSliderProgress(ClampedLocation * InitialRelativeTransform.GetScale3D());
	}
	
	// Skip first check, this will skip an event throw on rounded
	if (LastSliderProgressState < 0.0f)
	{	
		// Skip first tick, this is our resting position
		LastSliderProgressState = CurrentSliderProgress;
	}
	else if(LastSliderProgressState != CurrentSliderProgress && (CurrentSliderProgress == 1.0f || CurrentSliderProgress == 0.0f))
	{
		// I am working with exacts here because of the clamping, it should actually work with no precision issues
		// I wanted to ABS(Last-Cur) == 1.0 but it would cause an initial miss on whatever one last was inited to. 

		LastSliderProgressState = FMath::RoundToFloat(CurrentSliderProgress); // Ensure it is rounded to 0 or 1
		ReceiveSliderHitEnd(LastSliderProgressState);
		OnSliderHitEnd.Broadcast(LastSliderProgressState);
	}

	// Converted to a relative value now so it should be correct
	if (FVector::DistSquared(InitialDropLocation, this->GetComponentTransform().InverseTransformPosition(GrippingController->GetComponentLocation())) >= FMath::Square(BreakDistance))
	{
		GrippingController->DropObjectByInterface(this);
		return;
	}
}

void UVRSliderComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{

	ParentComponent = this->GetAttachParent();
	FTransform CurrentRelativeTransform = InitialRelativeTransform * GetCurrentParentTransform();

	// This lets me use the correct original location over the network without changes
	FTransform RelativeToGripTransform = FTransform(GripInformation.RelativeTransform.ToInverseMatrixWithScale()) * this->GetComponentTransform();

	InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(RelativeToGripTransform.GetTranslation());
	InitialGripLoc = InitialRelativeTransform.InverseTransformPosition(this->RelativeLocation);
	InitialDropLocation = GripInformation.RelativeTransform.Inverse().GetTranslation();
	LastInputKey = 0.0f;
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
void UVRSliderComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}

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

/*EGripCollisionType UVRSliderComponent::SlotGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}

EGripCollisionType UVRSliderComponent::FreeGripType_Implementation()
{
	return EGripCollisionType::CustomGrip;
}*/

EGripCollisionType UVRSliderComponent::GetPrimaryGripType_Implementation(bool bIsSlot)
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

/*float UVRSliderComponent::GripStiffness_Implementation()
{
	return 0.0f;
}

float UVRSliderComponent::GripDamping_Implementation()
{
	return 0.0f;
}*/
void UVRSliderComponent::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = 0.0f;
	GripDampingOut = 0.0f;
}

FBPAdvGripSettings UVRSliderComponent::AdvancedGripSettings_Implementation()
{
	return FBPAdvGripSettings(GripPriority);
}

float UVRSliderComponent::GripBreakDistance_Implementation()
{
	return BreakDistance;
}

/*void UVRSliderComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

void UVRSliderComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}*/

void UVRSliderComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
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
