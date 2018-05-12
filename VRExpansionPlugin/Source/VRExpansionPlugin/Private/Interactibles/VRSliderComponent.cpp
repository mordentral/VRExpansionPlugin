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
	SplineLerpType = EVRInteractibleSliderLerpType::Lerp_None;
	SplineLerpValue = 8.f;

	GripPriority = 1;
	LastSliderProgressState = -1.0f;
	LastInputKey = 0.0f;

	bSliderUsesSnapPoints = false;
	SnapIncrement = 0.1f;
	SnapThreshold = 0.1f;
	EventThrowThreshold = 1.0f;
	bHitEventThreshold = false;

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

	if (USplineComponent * ParentSpline = Cast<USplineComponent>(GetAttachParent()))
	{
		SetSplineComponentToFollow(ParentSpline);
	}
	else
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
	FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
	FTransform CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
	FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetComponentLocation());

	FVector CalculatedLocation = InitialGripLoc + (CurInteractorLocation - InitialInteractorLocation);
	
	float SplineProgress = CurrentSliderProgress;
	if (SplineComponentToFollow != nullptr)
	{
		FVector WorldCalculatedLocation = CurrentRelativeTransform.TransformPosition(CalculatedLocation);
		float ClosestKey = SplineComponentToFollow->FindInputKeyClosestToWorldLocation(WorldCalculatedLocation);

		if (bSliderUsesSnapPoints)
		{
			float SplineLength = SplineComponentToFollow->GetSplineLength();
			SplineProgress = GetCurrentSliderProgress(WorldCalculatedLocation, true, ClosestKey);

			SplineProgress = UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(SplineProgress, SnapIncrement, SnapThreshold);

			const int32 NumPoints = SplineComponentToFollow->SplineCurves.Position.Points.Num();

			if (SplineComponentToFollow->SplineCurves.Position.Points.Num() > 1)
			{
				ClosestKey = SplineComponentToFollow->SplineCurves.ReparamTable.Eval(SplineProgress * SplineLength, 0.0f);
			}

			WorldCalculatedLocation = SplineComponentToFollow->GetLocationAtSplineInputKey(ClosestKey, ESplineCoordinateSpace::World);
		}

		bool bLerpToNewKey = true;
		bool bChangedLocation = false;

		if (bEnforceSplineLinearity && LastInputKey >= 0.0f &&
			FMath::Abs((FMath::TruncToFloat(ClosestKey) - FMath::TruncToFloat(LastInputKey))) > 1.0f &&
			(!bSliderUsesSnapPoints || (SplineProgress - CurrentSliderProgress > SnapIncrement))
			)
		{
			bLerpToNewKey = false;
		}
		else
		{
			LerpedKey = ClosestKey;
		}

		if (bFollowSplineRotationAndScale)
		{
			FTransform trans;
			if (SplineLerpType != EVRInteractibleSliderLerpType::Lerp_None && LastInputKey >= 0.0f && !FMath::IsNearlyEqual(LerpedKey, LastInputKey))
			{
				GetLerpedKey(LerpedKey, DeltaTime);
				trans = SplineComponentToFollow->GetTransformAtSplineInputKey(LerpedKey, ESplineCoordinateSpace::World, true);
				bChangedLocation = true;
			}
			else if (bLerpToNewKey)
			{
				trans = SplineComponentToFollow->FindTransformClosestToWorldLocation(WorldCalculatedLocation, ESplineCoordinateSpace::World, true);
				bChangedLocation = true;
			}

			if (bChangedLocation)
			{
				trans.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
				trans = trans * ParentTransform.Inverse();
				this->SetRelativeTransform(trans);
			}
		}
		else
		{
			FVector WorldLocation;
			if (SplineLerpType != EVRInteractibleSliderLerpType::Lerp_None && LastInputKey >= 0.0f && !FMath::IsNearlyEqual(LerpedKey, LastInputKey))
			{
				GetLerpedKey(LerpedKey, DeltaTime);
				WorldLocation = SplineComponentToFollow->GetLocationAtSplineInputKey(LerpedKey, ESplineCoordinateSpace::World);
				bChangedLocation = true;
			}
			else if (bLerpToNewKey)
			{
				WorldLocation = SplineComponentToFollow->FindLocationClosestToWorldLocation(WorldCalculatedLocation, ESplineCoordinateSpace::World);
				bChangedLocation = true;
			}

			if (bChangedLocation)
				this->SetRelativeLocation(ParentTransform.InverseTransformPosition(WorldLocation));
		}

		CurrentSliderProgress = GetCurrentSliderProgress(WorldCalculatedLocation, true, bLerpToNewKey ? LerpedKey : ClosestKey);
		if (bLerpToNewKey)
		{
			LastInputKey = LerpedKey;
		}
	}
	else
	{
		FVector ClampedLocation = ClampSlideVector(CalculatedLocation);
		this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));
		CurrentSliderProgress = GetCurrentSliderProgress(bSlideDistanceIsInParentSpace ? ClampedLocation * InitialRelativeTransform.GetScale3D() : ClampedLocation);
	}

	// Skip first check, this will skip an event throw on rounded
	if (LastSliderProgressState < 0.0f)
	{
		// Skip first tick, this is our resting position
		LastSliderProgressState = CurrentSliderProgress;
	}
	else if ((LastSliderProgressState != CurrentSliderProgress) || bHitEventThreshold)
	{
		if ((!bSliderUsesSnapPoints && (CurrentSliderProgress == 1.0f || CurrentSliderProgress == 0.0f)) ||
			(bSliderUsesSnapPoints && FMath::IsNearlyEqual(FMath::Fmod(CurrentSliderProgress, SnapIncrement), 0.0f))
			)
		{
			// I am working with exacts here because of the clamping, it should actually work with no precision issues
			// I wanted to ABS(Last-Cur) == 1.0 but it would cause an initial miss on whatever one last was inited to. 

			if (!bSliderUsesSnapPoints)
				LastSliderProgressState = FMath::RoundToFloat(CurrentSliderProgress); // Ensure it is rounded to 0 or 1
			else
				LastSliderProgressState = CurrentSliderProgress;

			ReceiveSliderHitPoint(LastSliderProgressState);
			OnSliderHitPoint.Broadcast(LastSliderProgressState);
			bHitEventThreshold = false;
		}
	}

	if (FMath::Abs(LastSliderProgressState - CurrentSliderProgress) >= EventThrowThreshold)
	{
		bHitEventThreshold = true;
	}

	// Converted to a relative value now so it should be correct
	if (GrippingController->HasGripAuthority(GripInformation) && FVector::DistSquared(InitialDropLocation, this->GetComponentTransform().InverseTransformPosition(GrippingController->GetComponentLocation())) >= FMath::Square(BreakDistance))
	{
		GrippingController->DropObjectByInterface(this);
		return;
	}
}

void UVRSliderComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	FTransform CurrentRelativeTransform = InitialRelativeTransform * UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);

	// This lets me use the correct original location over the network without changes
	FTransform ReversedRelativeTransform = FTransform(GripInformation.RelativeTransform.ToInverseMatrixWithScale());
	FTransform RelativeToGripTransform = ReversedRelativeTransform * this->GetComponentTransform();

	InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(RelativeToGripTransform.GetTranslation());
	InitialGripLoc = InitialRelativeTransform.InverseTransformPosition(this->RelativeLocation);
	InitialDropLocation = ReversedRelativeTransform.GetTranslation();
	LastInputKey = -1.0f;
	LerpedKey = 0.0f;
	bHitEventThreshold = false;
	LastSliderProgressState = -1.0f;
}

void UVRSliderComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) 
{
	//this->SetComponentTickEnabled(false);
	// #TODO: Handle letting go and how lerping works, specifically with the snap points it may be an issue
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
