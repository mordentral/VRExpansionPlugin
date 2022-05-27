// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "VRBPDatatypes.h"
#include "Curves/CurveFloat.h"
#include "GS_LerpToHand.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRLerpToHandFinishedSignature);


// A grip script that causes new grips to lerp to the hand (from their current position to where they are supposed to sit).
// It turns off when the lerp is complete.
UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin), hideCategories = TickSettings)
class VREXPANSIONPLUGIN_API UGS_LerpToHand : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	UGS_LerpToHand(const FObjectInitializer& ObjectInitializer);

	float CurrentLerpTime;
	float LerpSpeed;

	// If the initial grip distance is closer than this value then the lerping will not be performed.
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings")
		float MinDistanceForLerp;

	// How many seconds the lerp should take
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings")
		float LerpDuration;

	// The minimum speed (in UU per second) that that the lerp should have across the initial grip distance
	// Will speed the LerpSpeed up to try and maintain this initial speed if required
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings")
		float MinSpeedForLerp;

	// The maximum speed (in UU per second) that the lerp should have across the initial grip distance
	// Will slow the LerpSpeed down to try and maintain this initial speed if required
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings")
		float MaxSpeedForLerp;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpSettings")
	EVRLerpInterpolationMode LerpInterpolationMode;
	
	UPROPERTY(BlueprintAssignable, Category = "LerpEvents")
		FVRLerpToHandFinishedSignature OnLerpToHandBegin;

	UPROPERTY(BlueprintAssignable, Category = "LerpEvents")
		FVRLerpToHandFinishedSignature OnLerpToHandFinished;

	// Whether to use a curve map to map the lerp to
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LerpCurve")
		bool bUseCurve;

	// The curve to follow when using a curve map, only uses from 0.0 - 1.0 of the curve timeline and maps it across the entire duration
	UPROPERTY(Category = "LerpCurve", EditAnywhere, meta = (editcondition = "bUseCurve"))
		FRuntimeFloatCurve OptionalCurveToFollow;

	FTransform OnGripTransform;
	uint8 TargetGrip;

	//virtual void BeginPlay_Implementation() override;
	virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) override;
	virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) override;
};
