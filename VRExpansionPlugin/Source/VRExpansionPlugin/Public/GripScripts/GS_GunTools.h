// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "GripScripts/GS_Default.h"
#include "GS_GunTools.generated.h"

UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UGS_GunTools : public UGS_Default
{
	GENERATED_BODY()
public:

	UGS_GunTools(const FObjectInitializer& ObjectInitializer);
	
	// Offset to apply to the pivot (good for centering pivot into the palm ect).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings")
		FVector PivotOffset;

	// Overrides the pivot location to be at this component instead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings")
		USceneComponent * OverridePivotComponent;

	// Causes the override pivot component to act like a shoulder mount location
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|ShoulderMount")
		bool bUseOverridePivotAsShoulderMount;

	// Relative transform on the gripped object to keep to the shoulder mount
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|ShoulderMount")
		FTransform_NetQuantize ShoulderMountRelativeTransform;

	// Overrides the relative transform and uses this socket location instead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|ShoulderMount")
		FName ShoulderMountSocketOverride;


	// If this gun has recoil
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|Recoil")
		bool bHasRecoil;

	// Recoil transform to apply per instance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|Recoil", meta = (editcondition = "bHasRecoil"))
		FTransform_NetQuantize InstanceTransform;

	// Maximum recoil addition
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|Recoil", meta = (editcondition = "bHasRecoil"))
		FTransform_NetQuantize MaxRecoil;

	// Recoil decay rate, how fast it decays back to baseline
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|Recoil", meta = (editcondition = "bHasRecoil"))
		float DecayRate;

	FTransform BackEndRecoilStorage;

	UFUNCTION(BlueprintCallable, Category = "GunTools|Recoil")
		void AddRecoilInstance(float RecoilInstanceStrength);

	UFUNCTION(BlueprintCallable, Category = "GunTools|Recoil")
		void ClearRecoil();

	virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface) override;
};
