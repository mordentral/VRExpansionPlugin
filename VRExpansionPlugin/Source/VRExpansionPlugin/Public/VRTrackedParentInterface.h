// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "UObject/Interface.h"

#include "VRTrackedParentInterface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UVRTrackedParentInterface: public UInterface
{
	GENERATED_UINTERFACE_BODY()
};


class VREXPANSIONPLUGIN_API IVRTrackedParentInterface
{
	GENERATED_IINTERFACE_BODY()
 
public:

	// Set a tracked parent
	UFUNCTION(BlueprintCallable, Category = "VRTrackedParentInterface")
	virtual void SetTrackedParent(UPrimitiveComponent * NewParentComponent, float WaistRadius, EBPVRWaistTrackingMode WaistTrackingMode)
	{}

	static void Default_SetTrackedParent_Impl(UPrimitiveComponent * NewParentComponent, float WaistRadius, EBPVRWaistTrackingMode WaistTrackingMode, FBPVRWaistTracking_Info & OptionalWaistTrackingParent, USceneComponent * Self);

	// Returns local transform of the parent relative attachment
	static FTransform Default_GetWaistOrientationAndPosition(FBPVRWaistTracking_Info & WaistTrackingInfo);
};