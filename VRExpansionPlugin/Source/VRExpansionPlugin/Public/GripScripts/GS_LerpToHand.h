// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "Curves/CurveFloat.h"
#include "GS_LerpToHand.generated.h"

/** Different methods for interpolating rotation between transforms */
UENUM(BlueprintType)
enum class EVRLerpInterpolationMode : uint8
{
	/** Shortest Path or Quaternion interpolation for the rotation. */
	QuatInterp,

	/** Rotor or Euler Angle interpolation. */
	EulerInterp,

	/** Dual quaternion interpolation, follows helix or screw-motion path between keyframes.   */
	DualQuatInterp
};


// A grip script that causes new grips to lerp to the hand (from their current position to where they are supposed to sit).
// It turns off when the lerp is complete.
UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UGS_LerpToHand : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	UGS_LerpToHand(const FObjectInitializer& ObjectInitializer);

	float CurrentLerpTime;

	// Progress from 0.0 to 1.0 that it should take per second to finish lerping.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings")
	float InterpSpeed;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings")
	EVRLerpInterpolationMode LerpInterpolationMode;
	


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings|Curve")
		bool bUseCurve;
	UPROPERTY(Category = "LerpSettings|Curve", EditAnywhere, meta = (editcondition = "bUseCurve"))
		FRuntimeFloatCurve OptionalCurveToFollow;

	FTransform OnGripTransform;

	//virtual void BeginPlay_Implementation() override;
	virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) override;
	virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) override;
};
