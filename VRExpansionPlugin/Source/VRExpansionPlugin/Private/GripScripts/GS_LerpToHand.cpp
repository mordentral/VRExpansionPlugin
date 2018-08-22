// Fill out your copyright notice in the Description page of Project Settings.


#include "GripScripts/GS_LerpToHand.h"
#include "Math/DualQuat.h"

UGS_LerpToHand::UGS_LerpToHand(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer)
{
	bIsActive = false;
	bDenyAutoDrop = true; // Always deny auto dropping while this script is active
	WorldTransformOverrideType = EGSTransformOverrideType::ModifiesWorldTransform;

	LerpInterpolationMode = EVRLerpInterpolationMode::QuatInterp;
	InterpSpeed = 10.0f;
}

//void UGS_InteractibleSettings::BeginPlay_Implementation() {}
void UGS_LerpToHand::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) 
{
	bIsActive = true;
}
void UGS_LerpToHand::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) 
{
	bIsActive = false;
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
	bool bActorHasInterface
) 
{
	if (!root)
		return false;

	if (InterpSpeed <= 0.f)
	{
		bIsActive = false;
	}

	const float Alpha = FMath::Clamp(DeltaTime * InterpSpeed, 0.f, 1.f);

	FTransform NA = root->GetComponentTransform();
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
	if (WorldTransform.Equals(NB, 0.1f))
	{
		bIsActive = false;
	}

	return true;
}