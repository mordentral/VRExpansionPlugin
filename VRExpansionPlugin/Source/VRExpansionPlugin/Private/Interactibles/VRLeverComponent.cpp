// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRLeverComponent.h"
#include "Net/UnrealNetwork.h"

  //=============================================================================
UVRLeverComponent::UVRLeverComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->bGenerateOverlapEvents = true;
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	bRepGameplayTags = false;
	bReplicateMovement = false;

	MovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	BreakDistance = 100.0f;

	HandleData = nullptr;
	SceneIndex = 0;

	bIsPhysicsLever = false;
	ParentComponent = nullptr;
	LeverRotationAxis = EVRInteractibleLeverAxis::Axis_X;
	
	LeverLimitNegative = 0.0f;
	LeverLimitPositive = 90.0f;
	bLeverState = false;
	LeverTogglePercentage = 0.8f;
	lerpCounter = 0.0f;

	LastDeltaAngle = 0.0f;

	LeverReturnTypeWhenReleased = EVRInteractibleLeverReturnType::ReturnToZero;
	LeverReturnSpeed = 50.0f;
	bSendLeverEventsDuringLerp = false;

	InitialRelativeTransform = FTransform::Identity;
	InitialInteractorLocation = FVector::ZeroVector;
	InitialGripRot = 0.0f;
	bIsLerping = false;
	bUngripAtTargetRotation = false;
	bDenyGripping = false;


	// Set to only overlap with things so that its not ruined by touching over actors
	this->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Overlap);
}

//=============================================================================
UVRLeverComponent::~UVRLeverComponent()
{
}


void UVRLeverComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UVRLeverComponent, bRepGameplayTags);
	DOREPLIFETIME(UVRLeverComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UVRLeverComponent, GameplayTags, COND_Custom);
}

void UVRLeverComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRLeverComponent, GameplayTags, bRepGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, bReplicateMovement);
}

void UVRLeverComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	ResetInitialLeverLocation();
}

void UVRLeverComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Call supers tick (though I don't think any of the base classes to this actually implement it)
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FTransform CurrentRelativeTransform;
	if (ParentComponent.IsValid())
	{
		// during grip there is no parent so we do this, might as well do it anyway for lerping as well
		CurrentRelativeTransform = this->GetComponentTransform().GetRelativeTransform(ParentComponent->GetComponentTransform());
	}
	else
	{
		CurrentRelativeTransform = this->GetRelativeTransform();
	}

	FQuat RotTransform = FQuat::Identity;

	if (LeverRotationAxis == EVRInteractibleLeverAxis::Axis_X)
		RotTransform = FRotator(FRotator(0.0, -90.0, 0.0)).Quaternion(); // Correct for roll and DotProduct

	FQuat newInitRot = (InitialRelativeTransform.GetRotation() * RotTransform);

	FVector v1 = (CurrentRelativeTransform.GetRotation() * RotTransform).Vector();
	FVector v2 = (newInitRot).Vector();
	v1.Normalize();
	v2.Normalize();

	FVector CrossP = FVector::CrossProduct(v1, v2);

	float angle = FMath::RadiansToDegrees(FMath::Atan2(CrossP.Size(), FVector::DotProduct(v1, v2)));
	angle *= FMath::Sign(FVector::DotProduct(CrossP, newInitRot.GetRightVector()));
	
//	float angle = FMath::RadiansToDegrees(CurrentRelativeTransform.GetRotation().AngularDistance(InitialRelativeTransform.GetRotation())) * ((GetAxisValue(CurrentRelativeTransform.Rotator()) - GetAxisValue(InitialRelativeTransform.Rotator())) < 0 ? -1.0f : 1.0f);

	CurrentLeverAngle = FMath::RoundToFloat(angle);

	if (bIsLerping)
	{
		float TargetAngle = 0.0f;
		switch (LeverReturnTypeWhenReleased)
		{
		case EVRInteractibleLeverReturnType::LerpToMax:
		{
			if (CurrentLeverAngle >= 0)
				TargetAngle = FMath::RoundToFloat(LeverLimitPositive);
			else
				TargetAngle = -FMath::RoundToFloat(LeverLimitNegative);
		}break;
		case EVRInteractibleLeverReturnType::LerpToMaxIfOverThreshold:
		{
			if ((!FMath::IsNearlyZero(LeverLimitPositive) && CurrentLeverAngle >= (LeverLimitPositive * LeverTogglePercentage)))
				TargetAngle = FMath::RoundToFloat(LeverLimitPositive);
			else if ((!FMath::IsNearlyZero(LeverLimitNegative) && CurrentLeverAngle <= -(LeverLimitNegative * LeverTogglePercentage)))
				TargetAngle = -FMath::RoundToFloat(LeverLimitNegative);
			//else - Handled by the default value
			//TargetAngle = 0.0f;
		}break;
		case EVRInteractibleLeverReturnType::ReturnToZero:
		default:
		{}break;
		}

		float LerpedVal = FMath::FixedTurn(angle, TargetAngle, LeverReturnSpeed * DeltaTime);
		//float LerpedVal = FMath::FInterpConstantTo(angle, TargetAngle, DeltaTime, LeverReturnSpeed);
		if (FMath::IsNearlyEqual(LerpedVal, TargetAngle))
		{
			this->SetComponentTickEnabled(false);

			this->SetRelativeRotation((FTransform(SetAxisValue(TargetAngle, FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
			CurrentLeverAngle = TargetAngle;
		}
		else
		{
			this->SetRelativeRotation((FTransform(SetAxisValue(LerpedVal, FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
		}
	}

	bool bNewLeverState = (!FMath::IsNearlyZero(LeverLimitNegative) && CurrentLeverAngle <= -(LeverLimitNegative * LeverTogglePercentage)) || (!FMath::IsNearlyZero(LeverLimitPositive) && CurrentLeverAngle >= (LeverLimitPositive * LeverTogglePercentage));
	//if (FMath::Abs(CurrentLeverAngle) >= LeverLimit  )
	if (bNewLeverState != bLeverState)
	{
		bLeverState = bNewLeverState;

		if(bSendLeverEventsDuringLerp || !bIsLerping)
			OnLeverStateChanged.Broadcast(bLeverState, CurrentLeverAngle >= 0.0f ? EVRInteractibleLeverEventType::LeverPositive : EVRInteractibleLeverEventType::LeverNegative, CurrentLeverAngle);

		if (!bIsLerping && bUngripAtTargetRotation && bLeverState && HoldingController)
		{
			HoldingController->DropObjectByInterface(this);
		}
	}
}

void UVRLeverComponent::OnUnregister()
{
	DestroyConstraint();
	Super::OnUnregister();
}

void UVRLeverComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) 
{
	// Handle manual tracking here

	FTransform CurrentRelativeTransform;
	if (ParentComponent.IsValid())
	{
		// during grip there is no parent so we do this, might as well do it anyway for lerping as well
		CurrentRelativeTransform = InitialRelativeTransform * ParentComponent->GetComponentTransform();
	}
	else
	{
		CurrentRelativeTransform = InitialRelativeTransform;
	}

	FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetComponentLocation());

	if (GrippingController->HasGripAuthority(GripInformation) && (CurInteractorLocation - InitialInteractorLocation).Size() >= BreakDistance)
	{
		GrippingController->DropObjectByInterface(this);
		return;
	}

	float DeltaAngle;

	FVector RotVector;
	if (LeverRotationAxis == EVRInteractibleLeverAxis::Axis_X)
		DeltaAngle = FMath::RadiansToDegrees(FMath::Atan2(CurInteractorLocation.Y, CurInteractorLocation.Z)) - InitialGripRot;
	else
		DeltaAngle = FMath::RadiansToDegrees(FMath::Atan2(CurInteractorLocation.Z, CurInteractorLocation.X)) - InitialGripRot;


	float CheckAngle = FRotator::NormalizeAxis(RotAtGrab + DeltaAngle);

	// Ignore rotations that would flip the angle of the lever to the other side, with a 90 degree allowance
	if (!FMath::IsNearlyZero(LastDeltaAngle) && FMath::Sign(CheckAngle) != FMath::Sign(LastDeltaAngle) && FMath::Abs(LastDeltaAngle) > 90.0f)
	{
	}
	else
	{
		this->SetRelativeRotation((FTransform(SetAxisValue(FMath::ClampAngle(RotAtGrab + DeltaAngle, -LeverLimitNegative, LeverLimitPositive), FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
		LastDeltaAngle = CheckAngle;
	}
}

void UVRLeverComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	ParentComponent = this->GetAttachParent();

	if (bIsPhysicsLever)
	{
		SetupConstraint();
	}
	else
	{
		FTransform CurrentRelativeTransform;
		if (ParentComponent.IsValid())
		{
			// during grip there is no parent so we do this, might as well do it anyway for lerping as well
			CurrentRelativeTransform = InitialRelativeTransform * ParentComponent->GetComponentTransform();
		}
		else
		{
			CurrentRelativeTransform = InitialRelativeTransform;//this->GetRelativeTransform();
		}

		InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetComponentLocation());
	

		FVector RotVector;
		if (LeverRotationAxis == EVRInteractibleLeverAxis::Axis_X)
			InitialGripRot = FMath::RadiansToDegrees(FMath::Atan2(InitialInteractorLocation.Y, InitialInteractorLocation.Z));
		else
			InitialGripRot = FMath::RadiansToDegrees(FMath::Atan2(InitialInteractorLocation.Z, InitialInteractorLocation.X));

		RotAtGrab = GetAxisValue(this->GetComponentTransform().GetRelativeTransform(CurrentRelativeTransform).Rotator());// GetAxisValue(CurrentRelativeTransform.GetRelativeTransform(InitialRelativeTransform.Inverse()).Rotator());// GetAxisValue(this->RelativeRotation);
	}

	bIsLerping = false;
	this->SetComponentTickEnabled(true);

}

void UVRLeverComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) 
{
	if(bIsPhysicsLever)
	{
		DestroyConstraint();
		FAttachmentTransformRules AttachRules(EAttachmentRule::KeepWorld, true);
		this->AttachToComponent(ParentComponent.Get(), AttachRules);
	}
	
	if (LeverReturnTypeWhenReleased != EVRInteractibleLeverReturnType::Stay)
	{
		bIsLerping = true;
	}
	else
		this->SetComponentTickEnabled(false);
}

void UVRLeverComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnUsed_Implementation() {}
void UVRLeverComponent::OnEndUsed_Implementation() {}
void UVRLeverComponent::OnSecondaryUsed_Implementation() {}
void UVRLeverComponent::OnEndSecondaryUsed_Implementation() {}

bool UVRLeverComponent::DenyGripping_Implementation()
{
	return bDenyGripping;
}

EGripInterfaceTeleportBehavior UVRLeverComponent::TeleportBehavior_Implementation()
{
	return EGripInterfaceTeleportBehavior::DropOnTeleport;
}

bool UVRLeverComponent::SimulateOnDrop_Implementation()
{
	return false;
}

EGripCollisionType UVRLeverComponent::SlotGripType_Implementation()
{
	if (bIsPhysicsLever)
		return EGripCollisionType::ManipulationGrip;
	else
		return EGripCollisionType::CustomGrip;
}

EGripCollisionType UVRLeverComponent::FreeGripType_Implementation()
{
	if (bIsPhysicsLever)
		return EGripCollisionType::ManipulationGrip;
	else
		return EGripCollisionType::CustomGrip;
}

ESecondaryGripType UVRLeverComponent::SecondaryGripType_Implementation()
{
	return ESecondaryGripType::SG_None;
}


EGripMovementReplicationSettings UVRLeverComponent::GripMovementReplicationType_Implementation()
{
	return MovementReplicationSetting;
}

EGripLateUpdateSettings UVRLeverComponent::GripLateUpdateSetting_Implementation()
{
	return EGripLateUpdateSettings::LateUpdatesAlwaysOff;
}

float UVRLeverComponent::GripStiffness_Implementation()
{
	return 1500.0f;
}

float UVRLeverComponent::GripDamping_Implementation()
{
	return 200.0f;
}

FBPAdvGripPhysicsSettings UVRLeverComponent::AdvancedPhysicsSettings_Implementation()
{
	return FBPAdvGripPhysicsSettings();
}

float UVRLeverComponent::GripBreakDistance_Implementation()
{
	return BreakDistance;
}

void UVRLeverComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

void UVRLeverComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

bool UVRLeverComponent::IsInteractible_Implementation()
{
	return false;
}

void UVRLeverComponent::IsHeld_Implementation(UGripMotionControllerComponent *& CurHoldingController, bool & bCurIsHeld)
{
	CurHoldingController = HoldingController;
	bCurIsHeld = bIsHeld;
}

void UVRLeverComponent::SetHeld_Implementation(UGripMotionControllerComponent * NewHoldingController, bool bNewIsHeld)
{
	bIsHeld = bNewIsHeld;

	if (bIsHeld)
		HoldingController = NewHoldingController;
	else
		HoldingController = nullptr;
}

FBPInteractionSettings UVRLeverComponent::GetInteractionSettings_Implementation()
{
	return FBPInteractionSettings();
}
