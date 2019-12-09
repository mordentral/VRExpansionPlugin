// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"
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

	static void Default_SetTrackedParent_Impl(UPrimitiveComponent * NewParentComponent, float WaistRadius, EBPVRWaistTrackingMode WaistTrackingMode, FBPVRWaistTracking_Info & OptionalWaistTrackingParent, USceneComponent * Self)
	{
		// If had a different original tracked parent
		// Moved this to first thing so the pre-res is removed prior to erroring out and clearing this
		if (OptionalWaistTrackingParent.IsValid())
		{
			// Remove the tick Prerequisite
			Self->RemoveTickPrerequisiteComponent(OptionalWaistTrackingParent.TrackedDevice);
		}

		if (!NewParentComponent || !Self)
		{
			OptionalWaistTrackingParent.Clear();
			return;
		}

		// Make other component tick first if possible, waste of time if in wrong tick group
		if (NewParentComponent->PrimaryComponentTick.TickGroup == Self->PrimaryComponentTick.TickGroup)
		{
			// Make sure the other component isn't depending on this one
			NewParentComponent->RemoveTickPrerequisiteComponent(Self);

			// Add a tick pre-res for ourselves so that we tick after our tracked parent.
			Self->AddTickPrerequisiteComponent(NewParentComponent);
		}

		OptionalWaistTrackingParent.TrackedDevice = NewParentComponent;
		OptionalWaistTrackingParent.RestingRotation = NewParentComponent->GetRelativeRotation();
		OptionalWaistTrackingParent.RestingRotation.Yaw = 0.0f;
		
		OptionalWaistTrackingParent.TrackingMode = WaistTrackingMode;
		OptionalWaistTrackingParent.WaistRadius = WaistRadius;
	}

	// Returns local transform of the parent relative attachment
	static FTransform Default_GetWaistOrientationAndPosition(FBPVRWaistTracking_Info & WaistTrackingInfo)
	{
		if (!WaistTrackingInfo.IsValid())
			return FTransform::Identity;

		FTransform DeviceTransform = WaistTrackingInfo.TrackedDevice->GetRelativeTransform();

		// Rewind by the initial rotation when the new parent was set, this should be where the tracker rests on the person
		DeviceTransform.ConcatenateRotation(WaistTrackingInfo.RestingRotation.Quaternion().Inverse());
		DeviceTransform.SetScale3D(FVector(1, 1, 1));

		// Don't bother if not set
		if (WaistTrackingInfo.WaistRadius > 0.0f)
		{
			DeviceTransform.AddToTranslation(DeviceTransform.GetRotation().RotateVector(FVector(-WaistTrackingInfo.WaistRadius, 0, 0)));
		}


		// This changes the forward vector to be correct
		// I could pre do it by changed the yaw in resting mode to these values, but that had its own problems
		// If given an initial forward vector that it should align to I wouldn't have to do this and could auto calculate it.
		// But without that I am limited to this.

		// #TODO: add optional ForwardVector to initial setup function that auto calculates offset so that the user can pass in HMD forward or something for calibration X+
		// Also would be better overall because slightly offset from right angles in yaw wouldn't matter anymore, it would adjust for it.
		switch (WaistTrackingInfo.TrackingMode)
		{
		case EBPVRWaistTrackingMode::VRWaist_Tracked_Front: DeviceTransform.ConcatenateRotation(FRotator(0, 0, 0).Quaternion()); break;
		case EBPVRWaistTrackingMode::VRWaist_Tracked_Rear: DeviceTransform.ConcatenateRotation(FRotator(0, -180, 0).Quaternion()); break;
		case EBPVRWaistTrackingMode::VRWaist_Tracked_Left: DeviceTransform.ConcatenateRotation(FRotator(0, 90, 0).Quaternion()); break;
		case EBPVRWaistTrackingMode::VRWaist_Tracked_Right:	DeviceTransform.ConcatenateRotation(FRotator(0, -90, 0).Quaternion()); break;
		}
		
		return DeviceTransform;
	}
};