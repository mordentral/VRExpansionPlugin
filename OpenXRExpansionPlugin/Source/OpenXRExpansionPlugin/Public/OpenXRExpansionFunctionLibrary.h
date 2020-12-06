// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "OpenXRCore.h"
#include "OpenXRHMD.h"
#include "OpenXRExpansionFunctionLibrary.Generated.h"

/*UENUM(Blueprintable)
enum class EBPOpenXRProviderPlatform : uint8
{
	PF_SteamVR,
	PF_Oculus,
	PF_Unknown
};*/

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
/*UENUM(Blueprintable)
enum class EBPOpenXRHMDDeviceType : uint8
{
	DT_SteamVR,
	DT_ValveIndex,
	DT_Vive,
	DT_ViveCosmos,
	DT_OculusQuestHMD,
	DT_OculusHMD,
	DT_WindowsMR,
	DT_Unknown
};*/

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum class EBPOpenXRControllerDeviceType : uint8
{
	DT_SimpleController,
	DT_ValveIndexController,
	DT_ViveController,
	DT_ViveProController,
	//DT_CosmosController,
	DT_DaydreamController,
	DT_OculusTouchController,
	DT_OculusGoController,
	DT_MicrosoftMotionController,
	DT_MicrosoftXboxController,
	DT_UnknownController
};

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class OPENXREXPANSIONPLUGIN_API UOpenXRExpansionFunctionLibrary : public UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_BODY()

public:
	UOpenXRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer);

	~UOpenXRExpansionFunctionLibrary();
public:

	static FOpenXRHMD* GetOpenXRHMD()
	{
		static FName SystemName(TEXT("OpenXR"));
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
		{
			return static_cast<FOpenXRHMD*>(GEngine->XRSystem.Get());
		}

		return nullptr;
	}


	// Get a list of all currently tracked devices and their types, index in the array is their device index
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
		static bool GetXRMotionControllerType(FString &TrackingSystemName, EBPOpenXRControllerDeviceType &DeviceType)
	{
		DeviceType = EBPOpenXRControllerDeviceType::DT_UnknownController;

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
						if (xrGetCurrentInteractionProfile(XRSesh, myPath, &interactionProfile) == XR_SUCCESS)
						{
							char myPathy[XR_MAX_SYSTEM_NAME_SIZE];
							uint32_t outputsize;
							xrPathToString(XRInstance, interactionProfile.interactionProfile, XR_MAX_SYSTEM_NAME_SIZE, &outputsize, myPathy);

							if (outputsize < 1)
								return false;

							FString InteractionName(ANSI_TO_TCHAR(myPathy));

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
								DeviceType = EBPOpenXRControllerDeviceType::DT_UnknownController;		
							}
						}

						TrackingSystemName = FString(ANSI_TO_TCHAR(systemProperties.systemName));// , XR_MAX_SYSTEM_NAME_SIZE);
						//VendorID = systemProperties.vendorId;
						return true;
					}
				}
			}
		}

		TrackingSystemName.Empty();
		return false;
	}

};	
