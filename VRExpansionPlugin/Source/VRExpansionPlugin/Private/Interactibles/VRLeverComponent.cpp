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
	bReplicateMovement = true;

	MovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	BreakDistance = 100.0f;
	Stiffness = 1500.0f;
	Damping = 200.0f;

	HandleData = nullptr;
	SceneIndex = 0;

	bIsPhysicsLever = false;
	ParentComponent = nullptr;
	LeverRotationAxis = EVRInteractibleLeverAxis::Axis_X;
	
	LeverLimitNegative = 0.0f;
	LeverLimitPositive = 90.0f;
	bLeverState = false;
	LeverTogglePercentage = 0.8f;

	LastDeltaAngle = 0.0f;
	FullCurrentAngle = 0.0f;

	LeverReturnTypeWhenReleased = EVRInteractibleLeverReturnType::ReturnToZero;
	LeverReturnSpeed = 50.0f;

	MomentumAtDrop = 0.0f;
	LeverMomentumFriction = 5.0f;
	MaxLeverMomentum = 180.0f;
	FramesToAverage = 3;
	LastLeverAngle = 0.0f;

	bSendLeverEventsDuringLerp = false;

	InitialRelativeTransform = FTransform::Identity;
	InitialInteractorLocation = FVector::ZeroVector;
	InitialGripRot = 0.0f;
	qRotAtGrab = FQuat::Identity;
	bIsLerping = false;
	bUngripAtTargetRotation = false;
	bDenyGripping = false;

	GripPriority = 1;

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

	DOREPLIFETIME_CONDITION(UVRLeverComponent, InitialRelativeTransform, COND_Custom);

	DOREPLIFETIME(UVRLeverComponent, bRepGameplayTags);
	DOREPLIFETIME(UVRLeverComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UVRLeverComponent, GameplayTags, COND_Custom);
}

void UVRLeverComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Replicate the levers initial transform if we are replicating movement
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRLeverComponent, InitialRelativeTransform, bReplicateMovement);

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

	// If we are the server, or this component doesn't replicate then get the initial lever location
	if (!bReplicates || GetNetMode() < ENetMode::NM_Client)
	{
		ResetInitialLeverLocation();
	}
}

void UVRLeverComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Call supers tick (though I don't think any of the base classes to this actually implement it)
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bIsLerping)
	{
		FTransform CurRelativeTransform = this->GetComponentTransform().GetRelativeTransform(GetCurrentParentTransform());

		switch (LeverRotationAxis)
		{
		case EVRInteractibleLeverAxis::Axis_X:
		case EVRInteractibleLeverAxis::Axis_Y:
		case EVRInteractibleLeverAxis::Axis_Z:
		{
			LerpAxis(FullCurrentAngle, DeltaTime);
		}break;
		case EVRInteractibleLeverAxis::Axis_XY:
		case EVRInteractibleLeverAxis::Axis_XZ:
		{
			// Only supporting LerpToZero with this mode currently
			FRotator curRot = CurRelativeTransform.GetRelativeTransform(InitialRelativeTransform).Rotator();
			FRotator LerpedRot = FMath::RInterpConstantTo(curRot, FRotator::ZeroRotator, DeltaTime, LeverReturnSpeed);

			if (LerpedRot.Equals(FRotator::ZeroRotator))
			{
				this->SetComponentTickEnabled(false);
				bReplicateMovement = true;
				this->SetRelativeRotation((FTransform::Identity * InitialRelativeTransform).Rotator());
			}
			else
				this->SetRelativeRotation((FTransform(LerpedRot.Quaternion()) * InitialRelativeTransform).Rotator());
		}break;
		default:break;
		}
	}

	FTransform CurrentRelativeTransform = this->GetComponentTransform().GetRelativeTransform(GetCurrentParentTransform());

	CalculateCurrentAngle(CurrentRelativeTransform);

	if (!bIsLerping && LeverReturnTypeWhenReleased == EVRInteractibleLeverReturnType::RetainMomentum)
	{
		// Rolling average across num samples
		MomentumAtDrop -= MomentumAtDrop / FramesToAverage;
		MomentumAtDrop += ((FullCurrentAngle - LastLeverAngle) / DeltaTime) / FramesToAverage;

		LastLeverAngle = FullCurrentAngle;
	}

	bool bNewLeverState = (!FMath::IsNearlyZero(LeverLimitNegative) && FullCurrentAngle <= -(LeverLimitNegative * LeverTogglePercentage)) || (!FMath::IsNearlyZero(LeverLimitPositive) && FullCurrentAngle >= (LeverLimitPositive * LeverTogglePercentage));
	//if (FMath::Abs(CurrentLeverAngle) >= LeverLimit  )
	if (bNewLeverState != bLeverState)
	{
		bLeverState = bNewLeverState;

		if (bSendLeverEventsDuringLerp || !bIsLerping)
		{
			ReceiveLeverStateChanged(bLeverState, FullCurrentAngle >= 0.0f ? EVRInteractibleLeverEventType::LeverPositive : EVRInteractibleLeverEventType::LeverNegative, FullCurrentAngle);
			OnLeverStateChanged.Broadcast(bLeverState, FullCurrentAngle >= 0.0f ? EVRInteractibleLeverEventType::LeverPositive : EVRInteractibleLeverEventType::LeverNegative, FullCurrentAngle);
		}

		if (!bIsLerping && bUngripAtTargetRotation && bLeverState && HoldingController)
		{
			FBPActorGripInformation GripInformation;
			EBPVRResultSwitch result;
			HoldingController->GetGripByObject(GripInformation, this, result);
			if (result == EBPVRResultSwitch::OnSucceeded && HoldingController->HasGripAuthority(GripInformation))
			{
				HoldingController->DropObjectByInterface(this);
			}
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

	FTransform CurrentRelativeTransform = InitialRelativeTransform * GetCurrentParentTransform();
	FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetComponentLocation());

	switch (LeverRotationAxis)
	{
	case EVRInteractibleLeverAxis::Axis_XY:
	{
		FRotator Rot;

		FVector nAxis;
		float nAngle = 0.0f;

		FQuat::FindBetweenVectors(qRotAtGrab.UnrotateVector(InitialInteractorLocation), CurInteractorLocation).ToAxisAndAngle(nAxis, nAngle);

		nAngle = FMath::Clamp(nAngle, 0.0f, FMath::DegreesToRadians(LeverLimitPositive));
		Rot = FQuat(nAxis, nAngle).Rotator();

		this->SetRelativeRotation((FTransform(Rot) * InitialRelativeTransform).Rotator());
	}
	break;
	case EVRInteractibleLeverAxis::Axis_XZ:
	{
		//Yaw Axis
		FVector nAxis;
		float nAngle = 0.0f;
		FQuat BetweenTemp;

		BetweenTemp = FQuat::FindBetweenVectors(qRotAtGrab.UnrotateVector(FVector(InitialInteractorLocation.X, InitialInteractorLocation.Y, 0.0f)), FVector(CurInteractorLocation.X, CurInteractorLocation.Y, 0));
		//BetweenTemp.ToAxisAndAngle(nAxis, nAngle);

		float yaw = BetweenTemp.Rotator().Yaw;// FQuat(nAxis, nAngle).Rotator().Yaw;

		//Pitch axis. CurInteractionLocation has to be rotated by yaw to not interfere with yaw axis. Also allows separated axis limitation if necessary.	
		FVector CurInteractionLocationLimitedPitch = FRotator(0, yaw, 0).UnrotateVector(CurInteractorLocation);

		BetweenTemp = FQuat::FindBetweenVectors(qRotAtGrab.UnrotateVector(FVector(0.0f, InitialInteractorLocation.Y, InitialInteractorLocation.Z)), FVector(0, CurInteractionLocationLimitedPitch.Y, CurInteractionLocationLimitedPitch.Z));
		//BetweenTemp.ToAxisAndAngle(nAxis, nAngle);
		//nAngle = FMath::Clamp(nAnglePitch, 0.0f, FMath::DegreesToRadians(LeverLimitPositive));

		float pitch = BetweenTemp.Rotator().Roll;

		//Final Rotation
		FRotator ShortestDistanceRot = FRotator(0, yaw, pitch);
		this->SetRelativeRotation((FTransform(ShortestDistanceRot) * InitialRelativeTransform).Rotator());	
	}break;
	case EVRInteractibleLeverAxis::Axis_X:
	case EVRInteractibleLeverAxis::Axis_Y:
	case EVRInteractibleLeverAxis::Axis_Z:
	{
		float DeltaAngle = CalcAngle(LeverRotationAxis, CurInteractorLocation);

		this->SetRelativeRotation((FTransform(SetAxisValue(DeltaAngle, FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
		LastDeltaAngle = DeltaAngle;
	}break;
	default:break;
	}

	// Also set it to after rotation
	if (GrippingController->HasGripAuthority(GripInformation) && FVector::DistSquared(InitialInteractorDropLocation, this->GetComponentTransform().InverseTransformPosition(GrippingController->GetComponentLocation())) >= FMath::Square(BreakDistance))
	{
		GrippingController->DropObjectByInterface(this);
		return;
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
		FTransform CurrentRelativeTransform = InitialRelativeTransform * GetCurrentParentTransform();
			
		// This lets me use the correct original location over the network without changes
		FTransform ReversedRelativeTransform = FTransform(GripInformation.RelativeTransform.ToInverseMatrixWithScale());
		FTransform RelativeToGripTransform = ReversedRelativeTransform * this->GetComponentTransform();

		InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(RelativeToGripTransform.GetTranslation());
		InitialInteractorDropLocation = ReversedRelativeTransform.GetTranslation();


		switch (LeverRotationAxis)
		{
		case EVRInteractibleLeverAxis::Axis_XY:
		case EVRInteractibleLeverAxis::Axis_XZ:
		{
			qRotAtGrab = this->GetComponentTransform().GetRelativeTransform(CurrentRelativeTransform).GetRotation();
		}break;
		case EVRInteractibleLeverAxis::Axis_X:
		{
			InitialGripRot = FMath::RadiansToDegrees(FMath::Atan2(InitialInteractorLocation.Y, InitialInteractorLocation.Z));
		}break;
		case EVRInteractibleLeverAxis::Axis_Y:
		{
			InitialGripRot = FMath::RadiansToDegrees(FMath::Atan2(InitialInteractorLocation.Z, InitialInteractorLocation.X));
		}break;
		case EVRInteractibleLeverAxis::Axis_Z:
		{
			InitialGripRot = FMath::RadiansToDegrees(FMath::Atan2(InitialInteractorLocation.Y, InitialInteractorLocation.X));
		}break;
		default:break;
		}

		RotAtGrab = GetAxisValue(this->GetComponentTransform().GetRelativeTransform(CurrentRelativeTransform).Rotator());
	}

	LastLeverAngle = CurrentLeverAngle;
	bIsLerping = false;
	bIsInFirstTick = true;

	if (GripInformation.GripMovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
	{
		bReplicateMovement = false;
	}

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
	{
		this->SetComponentTickEnabled(false);
		bReplicateMovement = true;
	}
}

void UVRLeverComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnSecondaryGrip_Implementation(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnSecondaryGripRelease_Implementation(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRLeverComponent::OnUsed_Implementation() {}
void UVRLeverComponent::OnEndUsed_Implementation() {}
void UVRLeverComponent::OnSecondaryUsed_Implementation() {}
void UVRLeverComponent::OnEndSecondaryUsed_Implementation() {}
void UVRLeverComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}

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

/*EGripCollisionType UVRLeverComponent::SlotGripType_Implementation()
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
}*/

EGripCollisionType UVRLeverComponent::GetPrimaryGripType_Implementation(bool bIsSlot)
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

/*float UVRLeverComponent::GripStiffness_Implementation()
{
	return Stiffness;
}

float UVRLeverComponent::GripDamping_Implementation()
{
	return Damping;
}*/
void UVRLeverComponent::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = Stiffness;
	GripDampingOut = Damping;
}

FBPAdvGripSettings UVRLeverComponent::AdvancedGripSettings_Implementation()
{
	return FBPAdvGripSettings(GripPriority);
}

float UVRLeverComponent::GripBreakDistance_Implementation()
{
	return BreakDistance;
}

/*void UVRLeverComponent::ClosestSecondarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}

void UVRLeverComponent::ClosestPrimarySlotInRange_Implementation(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	bHadSlotInRange = false;
}*/

void UVRLeverComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
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
