// Fill out your copyright notice in the Description page of Project Settings.


#include "GripScripts/GS_LerpToHand.h"
#include "GripMotionControllerComponent.h"
#include "Math/DualQuat.h"

UGS_LerpToHand::UGS_LerpToHand(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer)
{
	bIsActive = false;
	bDenyAutoDrop = true; // Always deny auto dropping while this script is active
	WorldTransformOverrideType = EGSTransformOverrideType::ModifiesWorldTransform;

	LerpInterpolationMode = EVRLerpInterpolationMode::QuatInterp;
	LerpDuration = 1.f;
	LerpSpeed = 0.0f;
	CurrentLerpTime = 0.0f;
	OnGripTransform = FTransform::Identity;
	bUseCurve = false;
	MinDistanceForLerp = 0.0f;
	MinSpeedForLerp = 0.f;
	MaxSpeedForLerp = 0.f;
	TargetGrip = INVALID_VRGRIP_ID;
}

//void UGS_InteractibleSettings::BeginPlay_Implementation() {}
void UGS_LerpToHand::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	// Removed this, let per object scripts overide
	// Dont run if the global lerping is enabled
	/*if (VRSettings.bUseGlobalLerpToHand)
	{
		bIsActive = false;
		return;
	}*/

	TargetGrip = GripInformation.GripID;

	OnGripTransform = GetParentTransform(true, GripInformation.GrippedBoneName);
	UObject* ParentObj = this->GetParent();

	FTransform TargetTransform = GripInformation.RelativeTransform * GrippingController->GetPivotTransform();
	float Distance = FVector::Dist(OnGripTransform.GetLocation(), TargetTransform.GetLocation());
	if (MinDistanceForLerp > 0.0f && Distance < MinDistanceForLerp)
	{
		// Don't init
		OnLerpToHandFinished.Broadcast();
		return;
	}
	else
	{
		float LerpScaler = 1.0f;
		float DistanceToSpeed = Distance / LerpDuration;
		if (DistanceToSpeed < MinSpeedForLerp)
		{
			LerpScaler = MinSpeedForLerp / DistanceToSpeed;
		}
		else if (MaxSpeedForLerp > 0.f && DistanceToSpeed > MaxSpeedForLerp)
		{
			LerpScaler = MaxSpeedForLerp / DistanceToSpeed;
		}
		else
		{
			LerpScaler = 1.0f;
		}

		// Get the modified lerp speed
		LerpSpeed = ((1.f / LerpDuration) * LerpScaler);

		OnLerpToHandBegin.Broadcast();

		if (FBPActorGripInformation* GripInfo = GrippingController->GetGripPtrByID(GripInformation.GripID))
		{
			GripInfo->bIsLerping = true;
		}
	}



	bIsActive = true;
	CurrentLerpTime = 0.0f;
}

void UGS_LerpToHand::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) 
{
	if(GripInformation.GripID == TargetGrip)
	{ 
		TargetGrip = INVALID_VRGRIP_ID;
		bIsActive = false;
	}
}

bool UGS_LerpToHand::GetWorldTransform_Implementation
(
	UGripMotionControllerComponent* GrippingController, 
	float DeltaTime, FTransform & WorldTransform, 
	const FTransform &ParentTransform, 
	FBPActorGripInformation &Grip, 
	AActor * actor, 
	UPrimitiveComponent * root, 
	bool bRootHasInterface, 
	bool bActorHasInterface, 
	bool bIsForTeleport
) 
{
	if (!root)
		return false;

	if (LerpDuration <= 0.f || !Grip.bIsLerping)
	{
		Grip.bIsLerping = false;
		GrippingController->OnLerpToHandFinished.Broadcast(Grip);
		bIsActive = false;
	}

	FTransform NA = OnGripTransform;//root->GetComponentTransform();

	float Alpha = 0.0f; 

	CurrentLerpTime += DeltaTime * LerpSpeed;
	float OrigAlpha = FMath::Clamp(CurrentLerpTime, 0.f, 1.0f);
	Alpha = OrigAlpha;

	if (bUseCurve)
	{
		if (FRichCurve * richCurve = OptionalCurveToFollow.GetRichCurve())
		{
			/*if (CurrentLerpTime > richCurve->GetLastKey().Time)
			{
				// Stop lerping
				OnLerpToHandFinished.Broadcast();
				CurrentLerpTime = 0.0f;
				bIsActive = false;
				return true;
			}
			else*/
			{
				Alpha = FMath::Clamp(richCurve->Eval(Alpha), 0.f, 1.f);
				//CurrentLerpTime += DeltaTime;
			}
		}
	}

	FTransform NB = WorldTransform;
	NA.NormalizeRotation();
	NB.NormalizeRotation();

	// Quaternion interpolation
	if (LerpInterpolationMode == EVRLerpInterpolationMode::QuatInterp)
	{
		WorldTransform.Blend(NA, NB, Alpha);
	}

	// Euler Angle interpolation
	else if (LerpInterpolationMode == EVRLerpInterpolationMode::EulerInterp)
	{
		WorldTransform.SetTranslation(FMath::Lerp(NA.GetTranslation(), NB.GetTranslation(), Alpha));
		WorldTransform.SetScale3D(FMath::Lerp(NA.GetScale3D(), NB.GetScale3D(), Alpha));

		FRotator A = NA.Rotator();
		FRotator B = NB.Rotator();
		WorldTransform.SetRotation(FQuat(A + (Alpha * (B - A))));
	}
	// Dual quaternion interpolation
	else
	{
		if ((NB.GetRotation() | NA.GetRotation()) < 0.0f)
		{
			NB.SetRotation(NB.GetRotation()*-1.0f);
		}
		WorldTransform = (FDualQuat(NA)*(1 - Alpha) + FDualQuat(NB)*Alpha).Normalized().AsFTransform(FMath::Lerp(NA.GetScale3D(), NB.GetScale3D(), Alpha));
	}

	// Turn it off if we need to
	if (OrigAlpha == 1.0f)
	{
		OnLerpToHandFinished.Broadcast();
		Grip.bIsLerping = false;
		GrippingController->OnLerpToHandFinished.Broadcast(Grip);
		CurrentLerpTime = 0.0f;
		bIsActive = false;
	}

	return true;
}