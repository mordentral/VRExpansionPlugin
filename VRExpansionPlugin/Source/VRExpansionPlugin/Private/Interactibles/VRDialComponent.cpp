// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Interactibles/VRDialComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "Net/UnrealNetwork.h"

  //=============================================================================
UVRDialComponent::UVRDialComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->SetGenerateOverlapEvents(true);
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	bRepGameplayTags = false;

	// Defaulting these true so that they work by default in networked environments
	bReplicateMovement = true;

	DialRotationAxis = EVRInteractibleAxis::Axis_Z;
	InteractorRotationAxis = EVRInteractibleAxis::Axis_X;

	bDialUsesAngleSnap = false;
	bDialUseSnapAngleList = false;
	SnapAngleThreshold = 45.0f;
	SnapAngleIncrement = 45.0f;
	LastSnapAngle = 0.0f;
	RotationScaler = 1.0f;

	ClockwiseMaximumDialAngle = 180.0f;
	CClockwiseMaximumDialAngle = 180.0f;
	bDenyGripping = false;

	PrimarySlotRange = 100.f;
	SecondarySlotRange = 100.f;
	GripPriority = 1;

	MovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	BreakDistance = 100.0f;

	bLerpBackOnRelease = false;
	bSendDialEventsDuringLerp = false;
	DialReturnSpeed = 90.0f;
	bIsLerping = false;

	bDialUseDirectHandRotation = false;
	LastGripRot = 0.0f;
	InitialGripRot = 0.f;
	InitialRotBackEnd = 0.f;
	bUseRollover = false;
}

//=============================================================================
UVRDialComponent::~UVRDialComponent()
{
}


void UVRDialComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UVRDialComponent, InitialRelativeTransform);
	//DOREPLIFETIME_CONDITION(UVRDialComponent, bIsLerping, COND_InitialOnly);

	DOREPLIFETIME(UVRDialComponent, bRepGameplayTags);
	DOREPLIFETIME(UVRDialComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UVRDialComponent, GameplayTags, COND_Custom);
}

void UVRDialComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UVRDialComponent, GameplayTags, bRepGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeScale3D, bReplicateMovement);
}

void UVRDialComponent::OnRegister()
{
	Super::OnRegister();
	ResetInitialDialLocation(); // Load the original dial location
}

void UVRDialComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();
	CalculateDialProgress();

	bOriginalReplicatesMovement = bReplicateMovement;
}

void UVRDialComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (bIsLerping)
	{
		if (bUseRollover)
		{
			this->SetDialAngle(FMath::FInterpConstantTo(CurRotBackEnd, 0.f, DeltaTime, DialReturnSpeed), bSendDialEventsDuringLerp);
		}
		else
		{
			// Flip lerp direction if we are on the other side
			if (CurrentDialAngle > ClockwiseMaximumDialAngle)
				this->SetDialAngle(FMath::FInterpConstantTo(CurRotBackEnd, 360.f, DeltaTime, DialReturnSpeed), bSendDialEventsDuringLerp);
			else
				this->SetDialAngle(FMath::FInterpConstantTo(CurRotBackEnd, 0.f, DeltaTime, DialReturnSpeed), bSendDialEventsDuringLerp);
		}

		if (CurRotBackEnd == 0.f)
		{
			this->SetComponentTickEnabled(false);
			bIsLerping = false;
			OnDialFinishedLerping.Broadcast();
			ReceiveDialFinishedLerping();
		}
	}
	else
	{
		this->SetComponentTickEnabled(false); 
	}
}

void UVRDialComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) 
{

	// #TODO: Should this use a pivot rotation? it wouldn't make that much sense to me?
	float DeltaRot = 0.0f;

	if (!bDialUseDirectHandRotation)
	{
		FTransform CurrentRelativeTransform = InitialRelativeTransform * UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
		FVector CurInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(GrippingController->GetPivotLocation());
		
		float NewRot = FRotator::ClampAxis(UVRInteractibleFunctionLibrary::GetAtan2Angle(DialRotationAxis, CurInteractorLocation));
		//DeltaRot = RotationScaler * ( NewRot - LastGripRot);

		DeltaRot = RotationScaler * FMath::FindDeltaAngleDegrees(LastGripRot, NewRot);

		float LimitTest = FRotator::ClampAxis(((NewRot - InitialGripRot) + InitialRotBackEnd));
		float MaxCheckValue = bUseRollover ? -CClockwiseMaximumDialAngle : 360.0f - CClockwiseMaximumDialAngle;

		if (FMath::IsNearlyZero(CClockwiseMaximumDialAngle))
		{
			if (LimitTest > ClockwiseMaximumDialAngle && (CurRotBackEnd == ClockwiseMaximumDialAngle || CurRotBackEnd == 0.f))
			{
				DeltaRot = 0.f;
			}
		}
		else if (FMath::IsNearlyZero(ClockwiseMaximumDialAngle))
		{
			if (LimitTest < MaxCheckValue && (CurRotBackEnd == MaxCheckValue || CurRotBackEnd == 0.f))
			{
				DeltaRot = 0.f;
			}
		}
		else if (LimitTest > ClockwiseMaximumDialAngle && LimitTest < MaxCheckValue && (CurRotBackEnd == ClockwiseMaximumDialAngle || CurRotBackEnd == MaxCheckValue))
		{
			DeltaRot = 0.f;
		}

		LastGripRot = NewRot;
	}
	else
	{
		FRotator curRotation = GrippingController->GetComponentRotation();
		DeltaRot = RotationScaler * UVRInteractibleFunctionLibrary::GetAxisValue(InteractorRotationAxis, (curRotation - LastRotation).GetNormalized());
		LastRotation = curRotation;
	}

	AddDialAngle(DeltaRot, true);

	// Handle the auto drop
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
		return;
	}
}

void UVRDialComponent::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	FTransform CurrentRelativeTransform = InitialRelativeTransform * UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);

	// This lets me use the correct original location over the network without changes
	FTransform ReversedRelativeTransform = FTransform(GripInformation.RelativeTransform.ToInverseMatrixWithScale());
	FTransform CurrentTransform = this->GetComponentTransform();
	FTransform RelativeToGripTransform = ReversedRelativeTransform * CurrentTransform;

	//FTransform InitialTrans = RelativeToGripTransform.GetRelativeTransform(CurrentRelativeTransform);

	InitialInteractorLocation = CurrentRelativeTransform.InverseTransformPosition(RelativeToGripTransform.GetTranslation());
	InitialDropLocation = ReversedRelativeTransform.GetTranslation();

	if (!bDialUseDirectHandRotation)
	{
		LastGripRot = FRotator::ClampAxis(UVRInteractibleFunctionLibrary::GetAtan2Angle(DialRotationAxis, InitialInteractorLocation));
		InitialGripRot = LastGripRot;
		InitialRotBackEnd = CurRotBackEnd;
	}
	else
	{
		LastRotation = RelativeToGripTransform.GetRotation().Rotator(); // Forcing into world space now so that initial can be correct over the network
	}

	bIsLerping = false;

	//OnGripped.Broadcast(GrippingController, GripInformation);
}

void UVRDialComponent::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) 
{
	if (bDialUsesAngleSnap && bDialUseSnapAngleList)
	{
		float closestAngle = 0.f;
		float closestVal = FMath::Abs(closestAngle - CurRotBackEnd);
		float closestValt = 0.f;
		for (float val : DialSnapAngleList)
		{
			closestValt = FMath::Abs(val - CurRotBackEnd);
			if (closestValt < closestVal)
			{
				closestAngle = val;
				closestVal = closestValt;
			}
		}

		if (closestAngle != LastSnapAngle)
		{
			this->SetRelativeRotation((FTransform(UVRInteractibleFunctionLibrary::SetAxisValueRot(DialRotationAxis, FMath::UnwindDegrees(closestAngle), FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
			CurrentDialAngle = FMath::RoundToFloat(closestAngle);
			CurRotBackEnd = CurrentDialAngle;

			if (!FMath::IsNearlyEqual(LastSnapAngle, CurrentDialAngle))
			{
				ReceiveDialHitSnapAngle(CurrentDialAngle);
				OnDialHitSnapAngle.Broadcast(CurrentDialAngle);
				LastSnapAngle = CurrentDialAngle;
			}
		}
	}
	else if (bDialUsesAngleSnap && SnapAngleIncrement > 0.f && FMath::Abs(FMath::Fmod(CurRotBackEnd, SnapAngleIncrement)) <= FMath::Min(SnapAngleIncrement, SnapAngleThreshold))
	{
		this->SetRelativeRotation((FTransform(UVRInteractibleFunctionLibrary::SetAxisValueRot(DialRotationAxis, FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement), FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());		
		CurRotBackEnd = FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement);
		CurrentDialAngle = FRotator::ClampAxis(FMath::RoundToFloat(CurRotBackEnd));
		
		if (!FMath::IsNearlyEqual(LastSnapAngle, CurrentDialAngle))
		{
			ReceiveDialHitSnapAngle(CurrentDialAngle);
			OnDialHitSnapAngle.Broadcast(CurrentDialAngle);
			LastSnapAngle = CurrentDialAngle;
		}
	}

	if (bLerpBackOnRelease)
	{
		bIsLerping = true;
		this->SetComponentTickEnabled(true);
	}
	else
		this->SetComponentTickEnabled(false);

	//OnDropped.Broadcast(ReleasingController, GripInformation, bWasSocketed);
}

void UVRDialComponent::SetGripPriority(int NewGripPriority)
{
	GripPriority = NewGripPriority;
}

void UVRDialComponent::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void UVRDialComponent::OnSecondaryGrip_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRDialComponent::OnUsed_Implementation() {}
void UVRDialComponent::OnEndUsed_Implementation() {}
void UVRDialComponent::OnSecondaryUsed_Implementation() {}
void UVRDialComponent::OnEndSecondaryUsed_Implementation() {}
void UVRDialComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
bool UVRDialComponent::RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) { return false; }

bool UVRDialComponent::DenyGripping_Implementation(UGripMotionControllerComponent * GripInitiator)
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
	return FBPAdvGripSettings(GripPriority);
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

void UVRDialComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, bSecondarySlot ? SecondarySlotRange : PrimarySlotRange, bHadSlotInRange, SlotWorldTransform, SlotName, CallingController);
}

bool UVRDialComponent::AllowsMultipleGrips_Implementation()
{
	return false;
}

void UVRDialComponent::IsHeld_Implementation(TArray<FBPGripPair> & CurHoldingControllers, bool & bCurIsHeld)
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

void UVRDialComponent::Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
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

void UVRDialComponent::SetHeld_Implementation(UGripMotionControllerComponent * NewHoldingController, uint8 GripID, bool bNewIsHeld)
{
	if (bNewIsHeld)
	{
		HoldingGrip = FBPGripPair(NewHoldingController, GripID);
		if (MovementReplicationSetting != EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			if(!bIsHeld)
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

/*FBPInteractionSettings UVRDialComponent::GetInteractionSettings_Implementation()
{
	return FBPInteractionSettings();
}*/

bool UVRDialComponent::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	return false;
}

void UVRDialComponent::SetDialAngle(float DialAngle, bool bCallEvents)
{
	CurRotBackEnd = DialAngle;
	AddDialAngle(0.0f);
}

void UVRDialComponent::AddDialAngle(float DialAngleDelta, bool bCallEvents, bool bSkipSettingRot)
{
	//FindDeltaAngleDegrees
	/** Utility to ensure angle is between +/- 180 degrees by unwinding. */
//static float UnwindDegrees(float A)
	float MaxCheckValue = bUseRollover ? -CClockwiseMaximumDialAngle : 360.0f - CClockwiseMaximumDialAngle;

	float DeltaRot = DialAngleDelta;
	float tempCheck = bUseRollover ? CurRotBackEnd + DeltaRot : FRotator::ClampAxis(CurRotBackEnd + DeltaRot);

	// Clamp it to the boundaries
	if (FMath::IsNearlyZero(CClockwiseMaximumDialAngle))
	{
		CurRotBackEnd = FMath::Clamp(CurRotBackEnd + DeltaRot, 0.0f, ClockwiseMaximumDialAngle);
	}
	else if (FMath::IsNearlyZero(ClockwiseMaximumDialAngle))
	{
		if (bUseRollover)
		{
			CurRotBackEnd = FMath::Clamp(CurRotBackEnd + DeltaRot, -CClockwiseMaximumDialAngle, 0.0f);
		}
		else
		{
			if (CurRotBackEnd < MaxCheckValue)
				CurRotBackEnd = FMath::Clamp(360.0f + DeltaRot, MaxCheckValue, 360.0f);
			else
				CurRotBackEnd = FMath::Clamp(CurRotBackEnd + DeltaRot, MaxCheckValue, 360.0f);
		}
	}
	else if(!bUseRollover && tempCheck > ClockwiseMaximumDialAngle && tempCheck < MaxCheckValue)
	{
		if (CurRotBackEnd < MaxCheckValue)
		{
			CurRotBackEnd = ClockwiseMaximumDialAngle;
		}
		else
		{
			CurRotBackEnd = MaxCheckValue;
		}
	}
	else if (bUseRollover)
	{
		if (tempCheck > ClockwiseMaximumDialAngle)
		{
			CurRotBackEnd = ClockwiseMaximumDialAngle;
		}
		else if (tempCheck < MaxCheckValue)
		{
			CurRotBackEnd = MaxCheckValue;
		}
		else
		{
			CurRotBackEnd = tempCheck;
		}
	}
	else
	{
		CurRotBackEnd = tempCheck;
	}

	if (bDialUsesAngleSnap && bDialUseSnapAngleList)
	{
		float closestAngle = 0.f;
		// Always default 0.0f to the list
		float closestVal = FMath::Abs(closestAngle - CurRotBackEnd); 
		float closestValt = 0.f;
		for (float val : DialSnapAngleList)
		{
			closestValt = FMath::Abs(val - CurRotBackEnd);
			if (closestValt < closestVal)
			{
				closestAngle = val;
				closestVal = closestValt;
			}
		}

		if (closestAngle != LastSnapAngle)
		{
			if (!bSkipSettingRot)
				this->SetRelativeRotation((FTransform(UVRInteractibleFunctionLibrary::SetAxisValueRot(DialRotationAxis, FMath::UnwindDegrees(closestAngle), FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
			CurrentDialAngle = FMath::RoundToFloat(closestAngle);

			if (bCallEvents && !FMath::IsNearlyEqual(LastSnapAngle, CurrentDialAngle))
			{
				ReceiveDialHitSnapAngle(CurrentDialAngle);
				OnDialHitSnapAngle.Broadcast(CurrentDialAngle);
			}

			LastSnapAngle = CurrentDialAngle;
		}
	}
	else if (bDialUsesAngleSnap && SnapAngleIncrement > 0.f && FMath::Abs(FMath::Fmod(CurRotBackEnd, SnapAngleIncrement)) <= FMath::Min(SnapAngleIncrement, SnapAngleThreshold))
	{
		if (!bSkipSettingRot)
			this->SetRelativeRotation((FTransform(UVRInteractibleFunctionLibrary::SetAxisValueRot(DialRotationAxis, FMath::UnwindDegrees(FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement)), FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
		CurrentDialAngle = FMath::RoundToFloat(FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement));

		if (bCallEvents && !FMath::IsNearlyEqual(LastSnapAngle, CurrentDialAngle))
		{
			ReceiveDialHitSnapAngle(CurrentDialAngle);
			OnDialHitSnapAngle.Broadcast(CurrentDialAngle);
		}

		LastSnapAngle = CurrentDialAngle;
	}
	else
	{
		if (!bSkipSettingRot)
			this->SetRelativeRotation((FTransform(UVRInteractibleFunctionLibrary::SetAxisValueRot(DialRotationAxis, FMath::UnwindDegrees(CurRotBackEnd), FRotator::ZeroRotator)) * InitialRelativeTransform).Rotator());
		CurrentDialAngle = FMath::RoundToFloat(CurRotBackEnd);
	}

}

void UVRDialComponent::ResetInitialDialLocation()
{
	// Get our initial relative transform to our parent (or not if un-parented).
	InitialRelativeTransform = this->GetRelativeTransform();
	CurRotBackEnd = 0.0f;
	CalculateDialProgress();
}

void UVRDialComponent::CalculateDialProgress()
{
	FTransform CurRelativeTransform = this->GetComponentTransform().GetRelativeTransform(UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this));
	LastGripRot = UVRInteractibleFunctionLibrary::GetDeltaAngleFromTransforms(DialRotationAxis, InitialRelativeTransform, CurRelativeTransform);
	CurRotBackEnd = LastGripRot;
	AddDialAngle(0.0f, false, true);
}