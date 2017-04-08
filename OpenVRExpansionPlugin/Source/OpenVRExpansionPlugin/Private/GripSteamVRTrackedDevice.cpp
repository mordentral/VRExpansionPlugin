// Fill out your copyright notice in the Description page of Project Settings.

#include "OpenVRExpansionPluginPrivatePCH.h"
#include "GripSteamVRTrackedDevice.h"
#include "OpenVRExpansionFunctionLibrary.h"
#include "GripMotionControllerComponent.h"


//=============================================================================
UGripSteamVRTrackedDevice::UGripSteamVRTrackedDevice(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TrackedDeviceIndex = EBPVRDeviceIndex::TrackedDevice1;
}

//=============================================================================
UGripSteamVRTrackedDevice::~UGripSteamVRTrackedDevice()
{
}

bool UGripSteamVRTrackedDevice::PollControllerState(FVector& Position, FRotator& Orientation)
{
#if !STEAMVR_SUPPORTED_PLATFORM
	return false;
#else
	if ((PlayerIndex != INDEX_NONE) && bHasAuthority && TrackedDeviceIndex != EBPVRDeviceIndex::None)
	{

		// This is a silly workaround but the VRSystem commands don't actually detect if the unit is on, only if it is a valid ID.
		// So I would have to check against a valid tracking frame anyway and this already does it for me through a table
		// that is not at all accessible outside of the module it is in.....
		TArray<int32> ValidTrackedIDs;
		USteamVRFunctionLibrary::GetValidTrackedDeviceIds(ESteamVRTrackedDeviceType::Invalid, ValidTrackedIDs);

		if (ValidTrackedIDs.Find((int32)TrackedDeviceIndex) != INDEX_NONE)
		{
			CurrentTrackingStatus = ETrackingStatus::Tracked;
		}
		else
		{
			CurrentTrackingStatus = ETrackingStatus::NotTracked;
			return false;
		}

		// Wanted to access SteamVRHMD directly but the linkage was bad due to how the module exports
		if (!USteamVRFunctionLibrary::GetTrackedDevicePositionAndOrientation((int32)TrackedDeviceIndex, Position, Orientation))
		{
			return false;
		}

		if (bOffsetByHMD)
		{
			if (IsInGameThread())
			{
				if (GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed() && GEngine->HMDDevice->HasValidTrackingPosition())
				{
					FQuat curRot;
					FVector curLoc;
					GEngine->HMDDevice->GetCurrentOrientationAndPosition(curRot, curLoc);
					curLoc.Z = 0;

					LastLocationForLateUpdate = curLoc;
				}
				else
					LastLocationForLateUpdate = FVector::ZeroVector;
			}

			Position -= LastLocationForLateUpdate;
		}
		return true;
	}
	return false;
#endif
}