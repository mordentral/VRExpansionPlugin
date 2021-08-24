// Fill out your copyright notice in the Description page of Project Settings.
#include "OpenXRExpansionFunctionLibrary.h"
//#include "EngineMinimal.h"
#include "Engine/Engine.h"
#include <openxr/openxr.h>
#include "CoreMinimal.h"
#include "IXRTrackingSystem.h"

//General Log
DEFINE_LOG_CATEGORY(OpenXRExpansionFunctionLibraryLog);

UOpenXRExpansionFunctionLibrary::UOpenXRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//=============================================================================
UOpenXRExpansionFunctionLibrary::~UOpenXRExpansionFunctionLibrary()
{

}

void UOpenXRExpansionFunctionLibrary::GetXRMotionControllerType(FString& TrackingSystemName, EBPOpenXRControllerDeviceType& DeviceType, EBPXRResultSwitch& Result)
{
	DeviceType = EBPOpenXRControllerDeviceType::DT_UnknownController;
	Result = EBPXRResultSwitch::OnFailed;

	if (FOpenXRHMD* pOpenXRHMD = GetOpenXRHMD())
	{
		XrInstance XRInstance = pOpenXRHMD->GetInstance();
		XrSystemId XRSysID = pOpenXRHMD->GetSystem();

		if (XRSysID && XRInstance)
		{
			XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES };
			systemProperties.next = nullptr;

			if (xrGetSystemProperties(XRInstance, XRSysID, &systemProperties) == XR_SUCCESS)
			{
				XrSession XRSesh = pOpenXRHMD->GetSession();

				if (XRSesh)
				{
					XrPath myPath;
					XrResult PathResult = xrStringToPath(XRInstance, "/user/hand/left", &myPath);
					XrInteractionProfileState interactionProfile{ XR_TYPE_INTERACTION_PROFILE_STATE };
					interactionProfile.next = nullptr;

					XrResult QueryResult = xrGetCurrentInteractionProfile(XRSesh, myPath, &interactionProfile);
					if (QueryResult == XR_SUCCESS)
					{
						char myPathy[XR_MAX_SYSTEM_NAME_SIZE];
						uint32_t outputsize;
						xrPathToString(XRInstance, interactionProfile.interactionProfile, XR_MAX_SYSTEM_NAME_SIZE, &outputsize, myPathy);

						if (outputsize < 1)
							return;

						FString InteractionName(ANSI_TO_TCHAR(myPathy));
						if (InteractionName.Len() < 1)
							return;

						/*
						* Interaction profile paths [6.4]
						An interaction profile identifies a collection of buttons and
						other input sources, and is of the form:
						 /interaction_profiles/<vendor_name>/<type_name>
						Paths supported in the core 1.0 release
						 /interaction_profiles/khr/simple_control
												 /interaction_profiles/khr/simple_controller
						 /interaction_profiles/google/daydream_controller
						 /interaction_profiles/htc/vive_controller
						 /interaction_profiles/htc/vive_pro
						 /interaction_profiles/microsoft/motion_controller
						 /interaction_profiles/microsoft/xbox_controller
						 /interaction_profiles/oculus/go_controller
						 /interaction_profiles/oculus/touch_controller
						 /interaction_profiles/valve/index_controller
						*/

						// Not working currently?
						/*XrInputSourceLocalizedNameGetInfo InputSourceInfo{ XR_TYPE_INPUT_SOURCE_LOCALIZED_NAME_GET_INFO };
						InputSourceInfo.next = nullptr;
						InputSourceInfo.whichComponents = XR_INPUT_SOURCE_LOCALIZED_NAME_INTERACTION_PROFILE_BIT;
						InputSourceInfo.sourcePath = interactionProfile.interactionProfile;

						char buffer[XR_MAX_SYSTEM_NAME_SIZE];
						uint32_t usedBufferCount = 0;
						if (xrGetInputSourceLocalizedName(XRSesh, &InputSourceInfo, XR_MAX_SYSTEM_NAME_SIZE, &usedBufferCount, (char*)&buffer) == XR_SUCCESS)
						{
							int g = 0;
						}*/


						if (InteractionName.Find("touch_controller", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_OculusTouchController;
						}
						else if (InteractionName.Contains("index_controller", ESearchCase::IgnoreCase))
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_ValveIndexController;
						}
						else if (InteractionName.Find("vive_controller", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_ViveController;
						}
						else if (InteractionName.Find("vive_pro", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_ViveProController;
						}
						else if (InteractionName.Find("simple_controller", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_SimpleController;
						}
						else if (InteractionName.Find("daydream_controller", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_DaydreamController;
						}
						else if (InteractionName.Find("motion_controller", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_MicrosoftMotionController;
						}
						else if (InteractionName.Find("xbox_controller", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_MicrosoftXboxController;
						}
						else if (InteractionName.Find("go_controller", ESearchCase::IgnoreCase) != INDEX_NONE)
						{
							DeviceType = EBPOpenXRControllerDeviceType::DT_OculusGoController;
						}
						else
						{
							UE_LOG(OpenXRExpansionFunctionLibraryLog, Warning, TEXT("UNKNOWN OpenXR Interaction profile detected!!!: %s"), *InteractionName);
							DeviceType = EBPOpenXRControllerDeviceType::DT_UnknownController;
						}

						Result = EBPXRResultSwitch::OnSucceeded;
					}

					TrackingSystemName = FString(ANSI_TO_TCHAR(systemProperties.systemName));// , XR_MAX_SYSTEM_NAME_SIZE);
					//VendorID = systemProperties.vendorId;
					return;
				}
			}
		}
	}

	TrackingSystemName.Empty();
	return;
}