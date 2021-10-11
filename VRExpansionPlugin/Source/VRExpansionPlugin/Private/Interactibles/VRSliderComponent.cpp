// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Interactibles/VRSliderComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "Net/UnrealNetwork.h"

  //=============================================================================
UVRSliderComponent::UVRSliderComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->SetGenerateOverlapEvents(true);
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	bRepGameplayTags = false;

	// Defaulting these true so that they work by default in networked environments
	bReplicateMovement = true;

	MovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	BreakDistance = 100.0f;

	InitialRelativeTransform = FTransform::Identity;
	bDenyGripping = false;

	bUpdateInTick = false;
	bPassThrough = false;

	MinSlideDistance = FVector::ZeroVector;
	MaxSlideDistance = FVector(10.0f, 0.f, 0.f);
	SliderRestitution = 0.0f;
	CurrentSliderProgress = 0.0f;
	LastSliderProgress = FVector::ZeroVector;//0.0f;
	SplineLastSliderProgress = 0.0f;
	
	MomentumAtDrop = FVector::ZeroVector;// 0.0f;
	SplineMomentumAtDrop = 0.0f;
	SliderMomentumFriction = 3.0f;
	MaxSliderMomentum = 1.0f;
	FramesToAverage = 3;

	InitialInteractorLocation = FVector::ZeroVector;
	InitialGripLoc = FVector::ZeroVector;

	bSlideDistanceIsInParentSpace = true;
	bUseLegacyLogic = false;
	bIsLocked = false;
	bAutoDropWhenLocked = true;

	SplineComponentToFollow = nullptr;

	bFollowSplineRotationAndScale = false;
	SplineLerpType = EVRInteractibleSliderLerpType::Lerp_None;
	SplineLerpValue = 8.f;

	PrimarySlotRange = 100.f;
	SecondarySlotRange = 100.f;
	GripPriority = 1;
	LastSliderProgressState = -1.0f;
	LastInputKey = 0.0f;

	bSliderUsesSnapPoints = false;
	SnapIncrement = 0.1f;
	SnapThreshold = 0.1f;
	bIncrementProgressBetweenSnapPoints = false;
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

	DOREPLIFETIME(UVRSliderComponent, InitialRelativeTransform);
	DOREPLIFETIME(UVRSliderComponent, SplineComponentToFollow);
	//DOREPLIFETIME_CONDITION(UVRSliderComponent, bIsLerping, COND_InitialOnly);

	DOREPLIFETIME(UVRSliderComponent, bRepGameplayTags);
	DOREPLIFETIME(UVRSliderComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UVRSliderComponent, GameplayTags, COND_Custom);
}

void UVRSliderComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Replicate the levers initial transform if we are replicating movement
	//DOREPLIFETIME_ACTIVE_OVERRIDE(UVRSliderComponent, InitialRelativeTransform, bReplicateMovement);
	//DOREPLIFETIME_ACTIVE_OVERRIDE(UVRSliderComponent, SplineComponentToFollow, bReplicateMovement);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRSliderComponent, GameplayTags, bRepGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeScale3D, bReplicateMovement);
}

void UVRSliderComponent::OnRegister()
{
	Super::OnRegister();

	// Init the slider settings
	if (USplineComponent * ParentSpline = Cast<USplineComponent>(GetAttachParent()))
	{
		SetSplineComponentToFollow(ParentSpline);
	}
	else
	{
		ResetInitialSliderLocation();
	}
}

void UVRSliderComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	CalculateSliderProgress();

	bOriginalReplicatesMovement = bReplicateMovement;
}

void UVRSliderComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Call supers tick (though I don't think any of the base classes to this actually implement it)
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsHeld && bUpdateInTick && HoldingGrip.HoldingController)
	{
		FBPActorGripInformation GripInfo;
		EBPVRResultSwitch Result;
		HoldingGrip.HoldingController->GetGripByID(GripInfo, HoldingGrip.GripID, Result);

		if (Result == EBPVRResultSwitch::OnSucceeded)
		{
			bPassThrough = true;
			TickGrip_Implementation(HoldingGrip.HoldingController, GripInfo, DeltaTime);
			bPassThrough = false;
		}
		return;
	}

	// If we are locked then end the lerp, no point
	if (bIsLocked)
	{
		// Notify the end user
		OnSliderFinishedLerping.Broadcast(CurrentSliderProgress);
		ReceiveSliderFinishedLerping(CurrentSliderProgress);

		this->SetComponentTickEnabled(false);
		bReplicateMovement = bOriginalReplicatesMovement;

		return;
	}

	if (bIsLerping)
	{
		if ((SplineComponentToFollow && FMath::IsNearlyZero((SplineMomentumAtDrop * DeltaTime), 0.00001f)) || (!SplineComponentToFollow && (MomentumAtDrop * DeltaTime).IsNearlyZero(0.00001f)))
		{
			bIsLerping = false;
		}
		else
		{
			if (this->SplineComponentToFollow)
			{
				SplineMomentumAtDrop = FMath::FInterpTo(SplineMomentumAtDrop, 0.0f, DeltaTime, SliderMomentumFriction);
				float newProgress = CurrentSliderProgress + (SplineMomentumAtDrop * DeltaTime);

				if (newProgress < 0.0f || FMath::IsNearlyEqual(newProgress, 0.0f, 0.00001f))
				{
					if (SliderRestitution > 0.0f)
					{
						// Reverse the momentum
						SplineMomentumAtDrop = -(SplineMomentumAtDrop * SliderRestitution);
						this->SetSliderProgress(0.0f);
					}
					else
					{
						bIsLerping = false;
						this->SetSliderProgress(0.0f);
					}
				}
				else if (newProgress > 1.0f || FMath::IsNearlyEqual(newProgress, 1.0f, 0.00001f))
				{
					if (SliderRestitution > 0.0f)
					{
						// Reverse the momentum
						SplineMomentumAtDrop = -(SplineMomentumAtDrop * SliderRestitution);
						this->SetSliderProgress(1.0f);
					}
					else
					{
						bIsLerping = false;
						this->SetSliderProgress(1.0f);
					}
				}
				else
				{
					this->SetSliderProgress(newProgress);
				}
			}
			else
			{
				MomentumAtDrop = FMath::VInterpTo(MomentumAtDrop, FVector::ZeroVector, DeltaTime, SliderMomentumFriction);

				FVector ClampedLocation = ClampSlideVector(InitialRelativeTransform.InverseTransformPosition(this->GetRelativeLocation()) + (MomentumAtDrop * DeltaTime));
				this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));
				CurrentSliderProgress = GetCurrentSliderProgress(bSlideDistanceIsInParentSpace ? ClampedLocation * InitialRelativeTransform.GetScale3D() : ClampedLocation);
				float newProgress = CurrentSliderProgress;
				if (SliderRestitution > 0.0f)
				{
					// Implement bounce
					FVector CurLoc = ClampedLocation;
					
					if (
						(FMath::Abs(MinSlideDistance.X) > 0.0f && CurLoc.X <= -FMath::Abs(this->MinSlideDistance.X)) ||
						(FMath::Abs(MinSlideDistance.Y) > 0.0f && CurLoc.Y <= -FMath::Abs(this->MinSlideDistance.Y)) ||
						(FMath::Abs(MinSlideDistance.Z) > 0.0f && CurLoc.Z <= -FMath::Abs(this->MinSlideDistance.Z)) ||
						(FMath::Abs(MaxSlideDistance.X) > 0.0f && CurLoc.X >= FMath::Abs(this->MaxSlideDistance.X)) ||
						(FMath::Abs(MaxSlideDistance.Y) > 0.0f && CurLoc.Y >= FMath::Abs(this->MaxSlideDistance.Y)) ||
						(FMath::Abs(MaxSlideDistance.Z) > 0.0f && CurLoc.Z >= FMath::Abs(this->MaxSlideDistance.Z))
						)
					{
						MomentumAtDrop = (-MomentumAtDrop * SliderRestitution);
					}
				}
				else
				{
					if (newProgress < 0.0f || FMath::IsNearlyEqual(newProgress, 0.0f, 0.00001f))
					{
						bIsLerping = false;
						this->SetSliderProgress(0.0f);
					}
					else if (newProgress > 1.0f || FMath::IsNearlyEqual(newProgress, 1.0f, 0.00001f))
					{
						bIsLerping = false;
						this->SetSliderProgress(1.0f);
					}
				}			
			}
		}

		if (!bIsLerping)
		{
			// Notify the end user
			OnSliderFinishedLerping.Broadcast(CurrentSliderProgress);
			ReceiveSliderFinishedLerping(CurrentSliderProgress);

			this->SetComponentTickEnabled(false);
			bReplicateMovement = bOriginalReplicatesMovement;
		}
		
		// Check for the hit point always
		CheckSliderProgress();
	}
}

void UVRSliderComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) 
{

	// Skip this tick if its not triggered from the pass through
	if (bUpdateInTick && !bPassThrough)
		return;

	// If the sliders progress is locked then just exit early
	if (bIsLocked)
	{
		if (bAutoDropWhenLocked)
		{
			// Check if we should auto drop
			CheckAutoDrop(GrippingController, GripInformation);
		}

		return;
	}

	// Handle manual tracking here
	FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
	FTransform CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
	FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetPivotLocation());

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

	if (SliderBehaviorWhenReleased == EVRInteractibleSliderDropBehavior::RetainMomentum)
	{
		if (SplineComponentToFollow)
		{
			// Rolling average across num samples
			SplineMomentumAtDrop -= SplineMomentumAtDrop / FramesToAverage;
			SplineMomentumAtDrop += ((CurrentSliderProgress - SplineLastSliderProgress) / DeltaTime) / FramesToAverage;

			float momentumSign = FMath::Sign(SplineMomentumAtDrop);
			SplineMomentumAtDrop = momentumSign * FMath::Min(MaxSliderMomentum, FMath::Abs(SplineMomentumAtDrop));

			SplineLastSliderProgress = CurrentSliderProgress;
		}
		else
		{
			// Rolling average across num samples
			MomentumAtDrop -= MomentumAtDrop / FramesToAverage;
			FVector CurProgress = InitialRelativeTransform.InverseTransformPosition(this->GetRelativeLocation());
			if (bSlideDistanceIsInParentSpace)
				CurProgress *= FVector(1.0f) / InitialRelativeTransform.GetScale3D();

			MomentumAtDrop += ((/*CurrentSliderProgress*/CurProgress - LastSliderProgress) / DeltaTime) / FramesToAverage;

			//MomentumAtDrop = FMath::Min(MaxSliderMomentum, MomentumAtDrop);

			LastSliderProgress = CurProgress;//CurrentSliderProgress;
		}
	}

	CheckSliderProgress();

	// Check if we should auto drop
	CheckAutoDrop(GrippingController, GripInformation);
}

bool UVRSliderComponent::CheckAutoDrop(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation)
{
	// Converted to a relative value now so it should be correct
	if (BreakDistance > 0.f && GrippingController->HasGripAuthority(GripInformation) && FVector::DistSquared(InitialDropLocation, this->GetComponentTransform().InverseTransformPosition(GrippingController->GetPivotLocation())) >= FMath::Square(BreakDistance))
	{
		if (GrippingController->OnGripOutOfRange.IsBound())
		{
			uint8 GripID = GripInformation.GripID;
			GrippingController->OnGripOutOfRange.Broadcast(GripInformation, GripInformation.GripDistance);
		}
		else
		{
			GrippingController->DropObjectByInterface(this, HoldingGrip.GripID);
		}
		return true;
	}

	return false;
}

void UVRSliderComponent::CheckSliderProgress()
{
	// Skip first check, this will skip an event throw on rounded
	if (LastSliderProgressState < 0.0f)
	{
		// Skip first tick, this is our resting position
		if (!bSliderUsesSnapPoints)
			LastSliderProgressState = FMath::RoundToFloat(CurrentSliderProgress); // Ensure it is rounded to 0 or 1
		else
			LastSliderProgressState = CurrentSliderProgress;
	}
	else if ((LastSliderProgressState != CurrentSliderProgress) || bHitEventThreshold)
	{
		if ((!bSliderUsesSnapPoints && (CurrentSliderProgress == 1.0f || CurrentSliderProgress == 0.0f)) ||
			(bSliderUsesSnapPoints && SnapIncrement > 0.f && FMath::IsNearlyEqual(FMath::Fmod(CurrentSliderProgress, SnapIncrement), 0.0f, 0.001f))
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

	if (FMath::Abs(LastSliderProgressState - CurrentSliderProgress) >= (bSliderUsesSnapPoints ? FMath::Min(EventThrowThreshold, SnapIncrement / 2.0f) : EventThrowThreshold))
	{
		bHitEventThreshold = true;
	}
}

void UVRSliderComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	FTransform CurrentRelativeTransform = InitialRelativeTransform * UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);

	// This lets me use the correct original location over the network without changes
	FTransform ReversedRelativeTransform = FTransform(GripInformation.RelativeTransform.ToInverseMatrixWithScale());
	FTransform RelativeToGripTransform = ReversedRelativeTransform * this->GetComponentTransform();

	InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(RelativeToGripTransform.GetTranslation());
	InitialGripLoc = InitialRelativeTransform.InverseTransformPosition(this->GetRelativeLocation());
	InitialDropLocation = ReversedRelativeTransform.GetTranslation();
	LastInputKey = -1.0f;
	LerpedKey = 0.0f;
	bHitEventThreshold = false;
	//LastSliderProgressState = -1.0f;
	LastSliderProgress = InitialGripLoc;//CurrentSliderProgress;
	SplineLastSliderProgress = CurrentSliderProgress;

	bIsLerping = false;
	MomentumAtDrop = FVector::ZeroVector;//0.0f;
	SplineMomentumAtDrop = 0.0f;

	if (GripInformation.GripMovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
	{
		bReplicateMovement = false;
	}

	if (bUpdateInTick)
		SetComponentTickEnabled(true);

	//OnGripped.Broadcast(GrippingController, GripInformation);

}

void UVRSliderComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) 
{
	//this->SetComponentTickEnabled(false);
	// #TODO: Handle letting go and how lerping works, specifically with the snap points it may be an issue
	if (SliderBehaviorWhenReleased != EVRInteractibleSliderDropBehavior::Stay)
	{
		bIsLerping = true;
		this->SetComponentTickEnabled(true);

		FVector Len = (MinSlideDistance.GetAbs() + MaxSlideDistance.GetAbs());
		if(bSlideDistanceIsInParentSpace)
			Len *= (FVector(1.0f) / InitialRelativeTransform.GetScale3D());

		float TotalDistance = Len.Size();		

		if (!SplineComponentToFollow)
		{
			if (MaxSliderMomentum * TotalDistance < MomentumAtDrop.Size())
			{
				MomentumAtDrop = MomentumAtDrop.GetSafeNormal() * (TotalDistance * MaxSliderMomentum);
			}
		}

		if(MovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
			bReplicateMovement = false;
	}
	else
	{
		this->SetComponentTickEnabled(false);
		bReplicateMovement = bOriginalReplicatesMovement;
	}

	//OnDropped.Broadcast(ReleasingController, GripInformation, bWasSocketed);
}

void UVRSliderComponent::SetIsLocked(bool bNewLockedState)
{
	bIsLocked = bNewLockedState;
}

void UVRSliderComponent::SetGripPriority(int NewGripPriority)
{
	GripPriority = NewGripPriority;
}

void UVRSliderComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void UVRSliderComponent::OnSecondaryGrip_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRSliderComponent::OnUsed_Implementation() {}
void UVRSliderComponent::OnEndUsed_Implementation() {}
void UVRSliderComponent::OnSecondaryUsed_Implementation() {}
void UVRSliderComponent::OnEndSecondaryUsed_Implementation() {}
void UVRSliderComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
bool UVRSliderComponent::RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) { return false; }

bool UVRSliderComponent::DenyGripping_Implementation(UGripMotionControllerComponent * GripInitiator)
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

void UVRSliderComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, bSecondarySlot ? SecondarySlotRange : PrimarySlotRange, bHadSlotInRange, SlotWorldTransform, SlotName, CallingController);
}

bool UVRSliderComponent::AllowsMultipleGrips_Implementation()
{
	return false;
}

void UVRSliderComponent::IsHeld_Implementation(TArray<FBPGripPair> & CurHoldingControllers, bool & bCurIsHeld)
{
	CurHoldingControllers.Empty();
	if (HoldingGrip.IsValid())
	{
		CurHoldingControllers.Add(HoldingGrip);
		bCurIsHeld = bIsHeld;
	}
	else
	{
		bCurIsHeld = false;
	}
}

void UVRSliderComponent::Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
{
	if (bGripped)
	{
		OnGripped.Broadcast(Controller, GripInformation);
	}
	else
	{
		OnDropped.Broadcast(Controller, GripInformation, bWasSocketed);
	}
}

void UVRSliderComponent::SetHeld_Implementation(UGripMotionControllerComponent * NewHoldingController, uint8 GripID, bool bNewIsHeld)
{
	if (bNewIsHeld)
	{
		HoldingGrip = FBPGripPair(NewHoldingController, GripID);
		if (MovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			if (!bIsHeld && !bIsLerping)
				bOriginalReplicatesMovement = bReplicateMovement;
			bReplicateMovement = false;
		}
	}
	else
	{
		HoldingGrip.Clear();
		if (MovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			bReplicateMovement = bOriginalReplicatesMovement;
		}
	}

	bIsHeld = bNewIsHeld;
}

/*FBPInteractionSettings UVRSliderComponent::GetInteractionSettings_Implementation()
{
	return FBPInteractionSettings();
}*/

bool UVRSliderComponent::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	return false;
}

FVector UVRSliderComponent::ClampSlideVector(FVector ValueToClamp)
{
	FVector fScaleFactor = FVector(1.0f);

	if (bSlideDistanceIsInParentSpace)
		fScaleFactor = fScaleFactor / InitialRelativeTransform.GetScale3D();

	FVector MinScale = (bUseLegacyLogic ? MinSlideDistance : MinSlideDistance.GetAbs()) * fScaleFactor;

	FVector Dist = (bUseLegacyLogic ? (MinSlideDistance + MaxSlideDistance) : (MinSlideDistance.GetAbs() + MaxSlideDistance.GetAbs())) * fScaleFactor;
	FVector Progress = (ValueToClamp - (-MinScale)) / Dist;

	if (bSliderUsesSnapPoints)
	{
		Progress.X = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.X, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
		Progress.Y = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.Y, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
		Progress.Z = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.Z, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
	}
	else
	{
		Progress.X = FMath::Clamp(Progress.X, 0.f, 1.f);
		Progress.Y = FMath::Clamp(Progress.Y, 0.f, 1.f);
		Progress.Z = FMath::Clamp(Progress.Z, 0.f, 1.f);
	}

	return (Progress * Dist) - (MinScale);
}

float UVRSliderComponent::GetDistanceAlongSplineAtSplineInputKey(float InKey) const
{

	const int32 NumPoints = SplineComponentToFollow->SplineCurves.Position.Points.Num();
	const int32 NumSegments = SplineComponentToFollow->IsClosedLoop() ? NumPoints : NumPoints - 1;

	if ((InKey >= 0) && (InKey < NumSegments))
	{
		const int32 ReparamPrevIndex = static_cast<int32>(InKey * SplineComponentToFollow->ReparamStepsPerSegment);
		const int32 ReparamNextIndex = ReparamPrevIndex + 1;

		const float Alpha = (InKey * SplineComponentToFollow->ReparamStepsPerSegment) - static_cast<float>(ReparamPrevIndex);

		const float PrevDistance = SplineComponentToFollow->SplineCurves.ReparamTable.Points[ReparamPrevIndex].InVal;
		const float NextDistance = SplineComponentToFollow->SplineCurves.ReparamTable.Points[ReparamNextIndex].InVal;

		// ReparamTable assumes that distance and input keys have a linear relationship in-between entries.
		return FMath::Lerp(PrevDistance, NextDistance, Alpha);
	}
	else if (InKey >= NumSegments)
	{
		return SplineComponentToFollow->SplineCurves.GetSplineLength();
	}

	return 0.0f;
}

float UVRSliderComponent::GetCurrentSliderProgress(FVector CurLocation, bool bUseKeyInstead, float CurKey)
{
	if (SplineComponentToFollow != nullptr)
	{
		// In this case it is a world location
		float ClosestKey = CurKey;

		if (!bUseKeyInstead)
			ClosestKey = SplineComponentToFollow->FindInputKeyClosestToWorldLocation(CurLocation);

		/*int32 primaryKey = FMath::TruncToInt(ClosestKey);

		float distance1 = SplineComponentToFollow->GetDistanceAlongSplineAtSplinePoint(primaryKey);
		float distance2 = SplineComponentToFollow->GetDistanceAlongSplineAtSplinePoint(primaryKey + 1);

		float FinalDistance = ((distance2 - distance1) * (ClosestKey - (float)primaryKey)) + distance1;
		return FMath::Clamp(FinalDistance / SplineComponentToFollow->GetSplineLength(), 0.0f, 1.0f);*/
		float SplineLength = SplineComponentToFollow->GetSplineLength();
		return GetDistanceAlongSplineAtSplineInputKey(ClosestKey) / SplineLength;
	}

	// Should need the clamp normally, but if someone is manually setting locations it could go out of bounds
	float Progress = 0.f;
	
	if (bUseLegacyLogic)
	{
		Progress = FMath::Clamp(FVector::Dist(-MinSlideDistance, CurLocation) / FVector::Dist(-MinSlideDistance, MaxSlideDistance), 0.0f, 1.0f);
	}
	else
	{
		Progress = FMath::Clamp(FVector::Dist(-MinSlideDistance.GetAbs(), CurLocation) / FVector::Dist(-MinSlideDistance.GetAbs(), MaxSlideDistance.GetAbs()), 0.0f, 1.0f);
	}

	if (bSliderUsesSnapPoints && SnapThreshold < SnapIncrement)
	{
		if (FMath::Fmod(Progress, SnapIncrement) < SnapThreshold)
		{
			Progress = FMath::GridSnap(Progress, SnapIncrement);
		}
		else if(!bIncrementProgressBetweenSnapPoints)
		{
			Progress = CurrentSliderProgress;
		}
	}
	
	return Progress;
}

void UVRSliderComponent::GetLerpedKey(float &ClosestKey, float DeltaTime)
{
	switch (SplineLerpType)
	{
	case EVRInteractibleSliderLerpType::Lerp_Interp:
	{
		ClosestKey = FMath::FInterpTo(LastInputKey, ClosestKey, DeltaTime, SplineLerpValue);
	}break;
	case EVRInteractibleSliderLerpType::Lerp_InterpConstantTo:
	{
		ClosestKey = FMath::FInterpConstantTo(LastInputKey, ClosestKey, DeltaTime, SplineLerpValue);
	}break;

	default: break;
	}
}

void UVRSliderComponent::SetSplineComponentToFollow(USplineComponent * SplineToFollow)
{
	SplineComponentToFollow = SplineToFollow;
	
	if (SplineToFollow != nullptr)
		ResetToParentSplineLocation();
	else
		CalculateSliderProgress();
}

void UVRSliderComponent::ResetInitialSliderLocation()
{
	// Get our initial relative transform to our parent (or not if un-parented).
	InitialRelativeTransform = this->GetRelativeTransform();
	ResetToParentSplineLocation();

	if (SplineComponentToFollow == nullptr)
		CurrentSliderProgress = GetCurrentSliderProgress(FVector(0, 0, 0));
}

void UVRSliderComponent::ResetToParentSplineLocation()
{
	if (SplineComponentToFollow != nullptr)
	{
		FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
		FTransform WorldTransform = SplineComponentToFollow->FindTransformClosestToWorldLocation(this->GetComponentLocation(), ESplineCoordinateSpace::World, true);
		if (bFollowSplineRotationAndScale)
		{
			WorldTransform.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
			WorldTransform = WorldTransform * ParentTransform.Inverse();
			this->SetRelativeTransform(WorldTransform);
		}
		else
		{
			this->SetWorldLocation(WorldTransform.GetLocation());
		}

		CurrentSliderProgress = GetCurrentSliderProgress(WorldTransform.GetLocation());
	}
}

void UVRSliderComponent::SetSliderProgress(float NewSliderProgress)
{
	NewSliderProgress = FMath::Clamp(NewSliderProgress, 0.0f, 1.0f);

	if (SplineComponentToFollow != nullptr)
	{
		FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
		float splineProgress = SplineComponentToFollow->GetSplineLength() * NewSliderProgress;

		if (bFollowSplineRotationAndScale)
		{
			FTransform trans = SplineComponentToFollow->GetTransformAtDistanceAlongSpline(splineProgress, ESplineCoordinateSpace::World, true);
			trans.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
			trans = trans * ParentTransform.Inverse();
			this->SetRelativeTransform(trans);
		}
		else
		{
			this->SetRelativeLocation(ParentTransform.InverseTransformPosition(SplineComponentToFollow->GetLocationAtDistanceAlongSpline(splineProgress, ESplineCoordinateSpace::World)));
		}
	}
	else // Not a spline follow
	{
		// Doing it min+max because the clamp value subtracts the min value
		FVector CalculatedLocation = bUseLegacyLogic ? FMath::Lerp(-MinSlideDistance, MaxSlideDistance, NewSliderProgress) : FMath::Lerp(-MinSlideDistance.GetAbs(), MaxSlideDistance.GetAbs(), NewSliderProgress);

		if (bSlideDistanceIsInParentSpace)
			CalculatedLocation *= FVector(1.0f) / InitialRelativeTransform.GetScale3D();

		FVector ClampedLocation = ClampSlideVector(CalculatedLocation);

		//if (bSlideDistanceIsInParentSpace)
		//	this->SetRelativeLocation(InitialRelativeTransform.TransformPositionNoScale(ClampedLocation));
		//else
		this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));
	}

	CurrentSliderProgress = NewSliderProgress;
}

float UVRSliderComponent::CalculateSliderProgress()
{
	if (this->SplineComponentToFollow != nullptr)
	{
		CurrentSliderProgress = GetCurrentSliderProgress(this->GetComponentLocation());
	}
	else
	{
		FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
		FTransform CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
		FVector CalculatedLocation = CurrentRelativeTransform.InverseTransformPosition(this->GetComponentLocation());

		CurrentSliderProgress = GetCurrentSliderProgress(bSlideDistanceIsInParentSpace ? CalculatedLocation * InitialRelativeTransform.GetScale3D() : CalculatedLocation);
	}

	return CurrentSliderProgress;
}