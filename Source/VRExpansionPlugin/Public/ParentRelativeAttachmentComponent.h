// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "Components/ShapeComponent.h"
#include "ParentRelativeAttachmentComponent.generated.h"


UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UParentRelativeAttachmentComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bLockPitch;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bLockYaw;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bLockRoll;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary", meta = (ClampMin = "0", UIMin = "0"))
	float PitchTolerance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary", meta = (ClampMin = "0", UIMin = "0"))
	float YawTolerance;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary", meta = (ClampMin = "0", UIMin = "0"))
	float RollTolerance;


	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
};

