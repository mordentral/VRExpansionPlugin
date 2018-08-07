// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
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

UCLASS(Blueprintable, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UGS_LerpToHand : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	UGS_LerpToHand(const FObjectInitializer& ObjectInitializer);

	virtual bool Wants_DenyAutoDrop_Implementation() override
	{
		return true;
	}

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	float InterpSpeed;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	EVRLerpInterpolationMode LerpInterpolationMode;

	//virtual void BeginPlay_Implementation() override;
	virtual void GetWorldTransform_Implementation(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface) override;
	virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) override;
};
