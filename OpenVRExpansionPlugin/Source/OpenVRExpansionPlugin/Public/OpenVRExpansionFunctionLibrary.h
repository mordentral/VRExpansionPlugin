// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "Engine/Texture.h"
#include "Engine/EngineTypes.h"
#include "RHI.h"
//#include "EngineMinimal.h"
#include "IMotionController.h"
//#include "VRBPDatatypes.h"

//Re-defined here as I can't load ISteamVRPlugin on non windows platforms
// Make sure to update if it changes

// #TODO: 4.19 check this
#define STEAMVR_SUPPORTED_PLATFORM (PLATFORM_MAC || (PLATFORM_LINUX && PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS) || (PLATFORM_WINDOWS && WINVER > 0x0502))

// #TODO: Check this over time for when they make it global
// @TODO: hardcoded to match FSteamVRHMD::GetSystemName(), which we should turn into 
static FName SteamVRSystemName(TEXT("SteamVR"));


#if STEAMVR_SUPPORTED_PLATFORM
#include "openvr.h"
//#include "ISteamVRPlugin.h"
//#include "SteamVRFunctionLibrary.h"

#endif // STEAMVR_SUPPORTED_PLATFORM

#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
// Or procedural mesh component throws an error....
//#include "PhysicsEngine/ConvexElem.h" // Fixed in 4.13.1?

//#include "HeadMountedDisplay.h" 
//#include "HeadMountedDisplayFunctionLibrary.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IHeadMountedDisplay.h"

#include "OpenVRExpansionFunctionLibrary.generated.h"

//General Advanced Sessions Log
DECLARE_LOG_CATEGORY_EXTERN(OpenVRExpansionFunctionLibraryLog, Log, All);


UENUM()
enum class EBPOpenVRTrackedDeviceClass : uint8
{
	// #TODO: Keep up to date
	//enum ETrackedDeviceClass - copied from valves enum
TrackedDeviceClass_Invalid = 0,				// the ID was not valid.
TrackedDeviceClass_HMD = 1,					// Head-Mounted Displays
TrackedDeviceClass_Controller = 2,			// Tracked controllers
TrackedDeviceClass_GenericTracker = 3,		// Generic trackers, similar to controllers
TrackedDeviceClass_TrackingReference = 4,	// Camera and base stations that serve as tracking reference points
TrackedDeviceClass_DisplayRedirect = 5,		// Accessories that aren't necessarily tracked themselves, but may redirect video output from other tracked devices
};

// This makes a lot of the blueprint functions cleaner
/*UENUM()
enum class EBPVRDeviceIndex : uint8
{
	// On Success
	HMD = 0,
	FirstTracking_Reference = 1,
	SecondTracking_Reference = 2,
	FirstController = 3,
	SecondController = 4,
	TrackedDevice1 = 5,
	TrackedDevice2 = 6,
	TrackedDevice3 = 7,
	TrackedDevice4 = 8,
	TrackedDevice5 = 9,
	TrackedDevice6 = 10,
	TrackedDevice7 = 11,
	TrackedDevice8 = 12,
	TrackedDevice9 = 13,
	TrackedDevice10 = 14,
	TrackedDevice11 = 15,
	None = 255
};*/

// This makes a lot of the blueprint functions cleaner
UENUM()
enum class EBPOVRResultSwitch : uint8
{
	// On Success
	OnSucceeded,
	// On Failure
	OnFailed
};

//USTRUCT(BlueprintType, Category = "VRExpansionFunctions|SteamVR")
struct OPENVREXPANSIONPLUGIN_API FBPOpenVRKeyboardHandle
{
	//GENERATED_BODY()
public:

	uint64_t VRKeyboardHandle;

	FBPOpenVRKeyboardHandle()
	{
#if STEAMVR_SUPPORTED_PLATFORM
		//static const VROverlayHandle_t k_ulOverlayHandleInvalid = 0;	
		VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
#endif
	}
	const bool IsValid()
	{
#if STEAMVR_SUPPORTED_PLATFORM
		return VRKeyboardHandle != vr::k_ulOverlayHandleInvalid;
#else
		return false;
#endif
	}

	//This is here for the Find() and Remove() functions from TArray
	FORCEINLINE bool operator==(const FBPOpenVRKeyboardHandle &Other) const
	{
		if (VRKeyboardHandle == Other.VRKeyboardHandle)
			return true;

		return false;
	}
	//#define INVALID_TRACKED_CAMERA_HANDLE
};


USTRUCT(BlueprintType, Category = "VRExpansionFunctions|SteamVR|VRCamera")
struct OPENVREXPANSIONPLUGIN_API FBPOpenVRCameraHandle
{
	GENERATED_BODY()
public:

	uint64_t pCameraHandle;

	FBPOpenVRCameraHandle()
	{
#if STEAMVR_SUPPORTED_PLATFORM
		pCameraHandle = INVALID_TRACKED_CAMERA_HANDLE;
#endif
	}
	const bool IsValid()
	{
#if STEAMVR_SUPPORTED_PLATFORM
		return pCameraHandle != INVALID_TRACKED_CAMERA_HANDLE;
#else
		return false;
#endif
	}

	//This is here for the Find() and Remove() functions from TArray
	FORCEINLINE bool operator==(const FBPOpenVRCameraHandle &Other) const
	{
		if (pCameraHandle == Other.pCameraHandle)
			return true;

		return false;
	}
	//#define INVALID_TRACKED_CAMERA_HANDLE
};

UENUM(BlueprintType)
enum class EOpenVRCameraFrameType : uint8
{
	VRFrameType_Distorted = 0,
	VRFrameType_Undistorted,
	VRFrameType_MaximumUndistorted
};

// This will make using the load model as async easier to understand
UENUM()
enum class EAsyncBlueprintResultSwitch : uint8
{
	// On Success 
	OnSuccess,
	// On still loading async
	AsyncLoading,
	// On Failure
	OnFailure
};

// Redefined here so that non windows packages can compile
/** Defines the class of tracked devices in SteamVR*/
// #TODO: Update these
UENUM(BlueprintType)
enum class EBPSteamVRTrackedDeviceType : uint8
{
	/** Represents a Steam VR Controller */
	Controller,

	/** Represents a static tracking reference device, such as a Lighthouse or tracking camera */
	TrackingReference,

	/** Misc. device types, for future expansion */
	Other,

	/** DeviceId is invalid */
	Invalid
};

#if STEAMVR_SUPPORTED_PLATFORM
static vr::ETrackedDeviceProperty VREnumToString(const FString& enumName, uint8 value)
{
	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, *enumName, true);

	if (!EnumPtr)
		return vr::ETrackedDeviceProperty::Prop_Invalid;

	FString EnumName = EnumPtr->GetNameStringByIndex(value).Right(4);

	if (EnumName.IsEmpty() || EnumName.Len() < 4)
		return vr::ETrackedDeviceProperty::Prop_Invalid;

	return static_cast<vr::ETrackedDeviceProperty>(FCString::Atoi(*EnumName));
}
#endif


/*
// Not implementing currently

// Properties that are unique to TrackedDeviceClass_HMD
Prop_DisplayMCImageData_Binary				= 2041,

Prop_IconPathName_String = 5000, // DEPRECATED. Value not referenced. Now expected to be part of icon path properties.


// Not implemented because very little use, and names are huuggge.....
Prop_NamedIconPathControllerLeftDeviceOff_String_2051
Prop_NamedIconPAthControllerRightDeviceOff_String_2052
Prop_NamedIconPathTrackingReferenceDeviceOff_String_2053


// Properties that are used by helpers, but are opaque to applications
Prop_DisplayHiddenArea_Binary_Start				= 5100,
Prop_DisplayHiddenArea_Binary_End				= 5150,
Prop_ParentContainer							= 5151

// Vendors are free to expose private debug data in this reserved region
Prop_VendorSpecific_Reserved_Start = 10000,
Prop_VendorSpecific_Reserved_End = 10999,

Prop_ImuFactoryGyroBias_Vector3				= 2064,
Prop_ImuFactoryGyroScale_Vector3			= 2065,
Prop_ImuFactoryAccelerometerBias_Vector3	= 2066,
Prop_ImuFactoryAccelerometerScale_Vector3	= 2067,
// reserved 2068

// Driver requested mura correction properties
Prop_DriverRequestedMuraCorrectionMode_Int32		= 2200,
Prop_DriverRequestedMuraFeather_InnerLeft_Int32		= 2201,
Prop_DriverRequestedMuraFeather_InnerRight_Int32	= 2202,
Prop_DriverRequestedMuraFeather_InnerTop_Int32		= 2203,
Prop_DriverRequestedMuraFeather_InnerBottom_Int32	= 2204,
Prop_DriverRequestedMuraFeather_OuterLeft_Int32		= 2205,
Prop_DriverRequestedMuraFeather_OuterRight_Int32	= 2206,
Prop_DriverRequestedMuraFeather_OuterTop_Int32		= 2207,
Prop_DriverRequestedMuraFeather_OuterBottom_Int32	= 2208,

Prop_TrackedDeviceProperty_Max				= 1000000,


Prop_CameraWhiteBalance_Vector4_Array = 2071, // Prop_NumCameras_Int32-sized array of float[4] RGBG white balance calibration data (max size is vr::k_unMaxCameras)
Prop_CameraDistortionFunction_Int32_Array = 2072, // Prop_NumCameras_Int32-sized array of vr::EVRDistortionFunctionType values (max size is vr::k_unMaxCameras)
Prop_CameraDistortionCoefficients_Float_Array = 2073, // Prop_NumCameras_Int32-sized array of double[vr::k_unMaxDistortionFunctionParameters] (max size is vr::k_unMaxCameras)
Prop_DisplayAvailableFrameRates_Float_Array = 2080, // populated by compositor from actual EDID list when available from GPU driver
*/


// #TODO: Update these
UENUM(BlueprintType)
enum class EVRDeviceProperty_String : uint8
{

	// No prefix = 1000 series
	Prop_TrackingSystemName_String_1000				UMETA(DisplayName = "Prop_TrackingSystemName_String"),
	Prop_ModelNumber_String_1001					UMETA(DisplayName = "Prop_ModelNumber_String"),
	Prop_SerialNumber_String_1002					UMETA(DisplayName = "Prop_SerialNumber_String"),
	Prop_RenderModelName_String_1003				UMETA(DisplayName = "Prop_RenderModelName_String"),
	Prop_ManufacturerName_String_1005				UMETA(DisplayName = "Prop_ManufacturerName_String"),
	Prop_TrackingFirmwareVersion_String_1006		UMETA(DisplayName = "Prop_TrackingFirmwareVersion_String"),
	Prop_HardwareRevision_String_1007				UMETA(DisplayName = "Prop_HardwareRevision_String"),
	Prop_AllWirelessDongleDescriptions_String_1008	UMETA(DisplayName = "Prop_AllWirelessDongleDescriptions_String"),
	Prop_ConnectedWirelessDongle_String_1009		UMETA(DisplayName = "Prop_ConnectedWirelessDongle_String"),
	Prop_Firmware_ManualUpdateURL_String_1016		UMETA(DisplayName = "Prop_Firmware_ManualUpdateURL_String"),
	Prop_Firmware_ProgrammingTarget_String_1028		UMETA(DisplayName = "Prop_Firmware_ProgrammingTarget_String"),
	Prop_DriverVersion_String_1031					UMETA(DisplayName = "Prop_DriverVersion_String"),
	Prop_ResourceRoot_String_1035					UMETA(DisplayName = "Prop_ResourceRoot_String"),
	Prop_RegisteredDeviceType_String_1036			UMETA(DisplayName = "Prop_RegisteredDeviceType_String"),
	Prop_InputProfileName_String_1037				UMETA(DisplayName = "Prop_InputProfileName_String"),

	Prop_AdditionalDeviceSettingsPath_String_1042	UMETA(DisplayName = "Prop_AdditionalDeviceSettingsPath_String"),
	Prop_AdditionalSystemReportData_String_1045		UMETA(DisplayName = "Prop_AdditionalSystemReportData_String"),
	Prop_CompositeFirmwareVersion_String_1046		UMETA(DisplayName = "Prop_CompositeFirmwareVersion_String"),

	// 1 prefix = 2000 series
	HMDProp_DisplayMCImageLeft_String_2012			UMETA(DisplayName = "HMDProp_DisplayMCImageLeft_String"),
	HMDProp_DisplayMCImageRight_String_2013			UMETA(DisplayName = "HMDProp_DisplayMCImageRight_String"),
	HMDProp_DisplayGCImage_String_2021				UMETA(DisplayName = "HMDProp_DisplayGCImage_String"),
	HMDProp_CameraFirmwareDescription_String_2028	UMETA(DisplayName = "HMDProp_CameraFirmwareDescription_String"),
	HMDProp_DriverProvidedChaperonePath_String_2048 UMETA(DisplayName = "HMDProp_DriverProvidedChaperonePath_String"),

	HMDProp_ExpectedControllerType_String_2074		UMETA(DisplayName = "HMDProp_ExpectedControllerType_String"),
	HMDProp_DashboardLayoutPathName_String_2090		UMETA(DisplayName = "HMDProp_DashboardLayoutPathName_String"),

	// 2 prefix = 3000 series
	ControllerProp_AttachedDeviceId_String_3000		UMETA(DisplayName = "ControllerProp_AttachedDeviceId_String"),

	// 3 prefix = 4000 series
	TrackRefProp_ModeLabel_String_4006				UMETA(DisplayName = "TrackRefProp_ModeLabel_String"),

	// 4 prefix = 5000 series
	UIProp_NamedIconPathDeviceOff_String_5001				UMETA(DisplayName = "UIProp_NamedIconPathDeviceOff_String"),
	UIProp_NamedIconPathDeviceSearching_String_5002			UMETA(DisplayName = "UIProp_NamedIconPathDeviceSearching_String"),
	UIProp_NamedIconPathDeviceSearchingAlert_String_5003	UMETA(DisplayName = "UIProp_NamedIconPathDeviceSearchingAlert_String_"),
	UIProp_NamedIconPathDeviceReady_String_5004				UMETA(DisplayName = "UIProp_NamedIconPathDeviceReady_String"),
	UIProp_NamedIconPathDeviceReadyAlert_String_5005		UMETA(DisplayName = "UIProp_NamedIconPathDeviceReadyAlert_String"),
	UIProp_NamedIconPathDeviceNotReady_String_5006			UMETA(DisplayName = "UIProp_NamedIconPathDeviceNotReady_String"),
	UIProp_NamedIconPathDeviceStandby_String_5007			UMETA(DisplayName = "UIProp_NamedIconPathDeviceStandby_String"),
	UIProp_NamedIconPathDeviceAlertLow_String_5008			UMETA(DisplayName = "UIProp_NamedIconPathDeviceAlertLow_String"),

	// 5 prefix = 6000 series
	DriverProp_UserConfigPath_String_6000			UMETA(DisplayName = "DriverProp_UserConfigPath_String"),
	DriverProp_InstallPath_String_6001				UMETA(DisplayName = "DriverProp_InstallPath_String"),

	// Properties that are set internally based on other information provided by drivers
	DriverProp_ControllerType_String_7000			UMETA(DisplayName = "DriverProp_ControllerType_String"),
	//DriveerProp_LegacyInputProfile_String_7001		UMETA(DisplayName = "DriveerProp_LegacyInputProfile_String") // Deprecated

};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Bool : uint8
{	
	// No prefix = 1000 series
	Prop_WillDriftInYaw_Bool_1004					UMETA(DisplayName = "Prop_WillDriftInYaw_Bool"),
	Prop_DeviceIsWireless_Bool_1010					UMETA(DisplayName = "Prop_DeviceIsWireless_Bool"),
	Prop_DeviceIsCharging_Bool_1011					UMETA(DisplayName = "Prop_DeviceIsCharging_Bool"),
	Prop_Firmware_UpdateAvailable_Bool_1014			UMETA(DisplayName = "Prop_Firmware_UpdateAvailable_Bool"),
	Prop_Firmware_ManualUpdate_Bool_1015			UMETA(DisplayName = "Prop_Firmware_ManualUpdate_Bool"),
	Prop_BlockServerShutdown_Bool_1023				UMETA(DisplayName = "Prop_BlockServerShutdown_Bool"),
	Prop_CanUnifyCoordinateSystemWithHmd_Bool_1024	UMETA(DisplayName = "Prop_CanUnifyCoordinateSystemWithHmd_Bool"),
	Prop_ContainsProximitySensor_Bool_1025			UMETA(DisplayName = "Prop_ContainsProximitySensor_Bool"),
	Prop_DeviceProvidesBatteryStatus_Bool_1026		UMETA(DisplayName = "Prop_DeviceProvidesBatteryStatus_Bool"),
	Prop_DeviceCanPowerOff_Bool_1027				UMETA(DisplayName = "Prop_DeviceCanPowerOff_Bool"),
	Prop_HasCamera_Bool_1030						UMETA(DisplayName = "Prop_HasCamera_Bool"),
	Prop_Firmware_ForceUpdateRequired_Bool_1032		UMETA(DisplayName = "Prop_Firmware_ForceUpdateRequired_Bool"),
	Prop_ViveSystemButtonFixRequired_Bool_1033		UMETA(DisplayName = "Prop_ViveSystemButtonFixRequired_Bool"),
	Prop_NeverTracked_Bool_1038						UMETA(DisplayName = "Prop_NeverTracked_Bool"),
	Prop_Identifiable_Bool_1043						UMETA(DisplayName = "Prop_Identifiable_Bool"),
	Prop_Firmware_RemindUpdate_Bool_1047			UMETA(DisplayName = "Prop_Firmware_RemindUpdate_Bool"),

	// 1 prefix = 2000 series
	HMDProp_ReportsTimeSinceVSync_Bool_2000				UMETA(DisplayName = "HMDProp_ReportsTimeSinceVSync_Bool"),
	HMDProp_IsOnDesktop_Bool_2007						UMETA(DisplayName = "HMDProp_IsOnDesktop_Bool"),
	HMDProp_DisplaySuppressed_Bool_2036					UMETA(DisplayName = "HMDProp_DisplaySuppressed_Bool"),
	HMDProp_DisplayAllowNightMode_Bool_2037				UMETA(DisplayName = "HMDProp_DisplayAllowNightMode_Bool"),
	HMDProp_DriverDirectModeSendsVsyncEvents_Bool_2043	UMETA(DisplayName = "HMDProp_DriverDirectModeSendsVsyncEvents_Bool"),
	HMDProp_DisplayDebugMode_Bool_2044					UMETA(DisplayName = "HMDProp_DisplayDebugMode_Bool"),
	HMDProp_DoNotApplyPrediction_Bool_2054				UMETA(DisplayName = "HMDProp_DoNotApplyPrediction_Bool"),

	HMDProp_DriverIsDrawingControllers_Bool_2057				UMETA(DisplayName = "HMDProp_DriverIsDrawingControllers_Bool"),
	HMDProp_DriverRequestsApplicationPause_Bool_2058			UMETA(DisplayName = "HMDProp_DriverRequestsApplicationPause_Bool"),
	HMDProp_DriverRequestsReducedRendering_Bool_2059			UMETA(DisplayName = "HMDProp_DriverRequestsReducedRendering_Bool"),
	HMDProp_ConfigurationIncludesLighthouse20Features_Bool_2069	UMETA(DisplayName = "HMDProp_ConfigurationIncludesLighthouse20Features_Bool"),

	HMDProp_DriverProvidedChaperoneVisibility_Bool_2076			UMETA(DisplayName = "HMDProp_DriverProvidedChaperoneVisibility_Bool"),
	HMDProp_DisplaySupportsMultipleFramerates_Bool_2081			UMETA(DisplayName = "HMDProp_DisplaySupportsMultipleFramerates_Bool"),

	// Tracked devices
	TrackRefProp_CanWirelessIdentify_Bool_4007	UMETA(DisplayName = "TrackRefProp_CanWirelessIdentify_Bool"),


	// 5 prefix = 6000 series
	DriverProp_HasDisplayComponent_Bool_6002			UMETA(DisplayName = "DriverProp_HasDisplayComponent_Bool"),
	DriverProp_HasControllerComponent_Bool_6003			UMETA(DisplayName = "DriverProp_HasControllerComponent_Bool"),
	DriverProp_HasCameraComponent_Bool_6004				UMETA(DisplayName = "DriverProp_HasCameraComponent_Bool"),
	DriverProp_HasDriverDirectModeComponent_Bool_6005	UMETA(DisplayName = "DriverProp_HasDriverDirectModeComponent_Bool"),
	DriverProp_HasVirtualDisplayComponent_Bool_6006		UMETA(DisplayName = "DriverProp_HasVirtualDisplayComponent_Bool"),
	DriverProp_HasSpatialAnchorsSupport_Bool_6007		UMETA(DisplayName = "DriverProp_HasSpatialAnchorsSupport_Bool")

};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Float : uint8
{
	// No Prefix = 1000 series
	Prop_DeviceBatteryPercentage_Float_1012						UMETA(DisplayName = "Prop_DeviceBatteryPercentage_Float"),

	// 1 Prefix = 2000 series
	HMDProp_SecondsFromVsyncToPhotons_Float_2001				UMETA(DisplayName = "HMDProp_SecondsFromVsyncToPhotons_Float"),
	HMDProp_DisplayFrequency_Float_2002							UMETA(DisplayName = "HMDProp_DisplayFrequency_Float"),
	HMDProp_UserIpdMeters_Float_2003							UMETA(DisplayName = "HMDProp_UserIpdMeters_Float"),
	HMDProp_DisplayMCOffset_Float_2009							UMETA(DisplayName = "HMDProp_DisplayMCOffset_Float"),
	HMDProp_DisplayMCScale_Float_2010							UMETA(DisplayName = "HMDProp_DisplayMCScale_Float"),
	HMDProp_DisplayGCBlackClamp_Float_2014						UMETA(DisplayName = "HMDProp_DisplayGCBlackClamp_Float"),
	HMDProp_DisplayGCOffset_Float_2018							UMETA(DisplayName = "HMDProp_DisplayGCOffset_Float"),
	HMDProp_DisplayGCScale_Float_2019							UMETA(DisplayName = "HMDProp_DisplayGCScale_Float"),
	HMDProp_DisplayGCPrescale_Float_2020						UMETA(DisplayName = "HMDProp_DisplayGCPrescale_Float"),
	HMDProp_LensCenterLeftU_Float_2022							UMETA(DisplayName = "HMDProp_LensCenterLeftU_Float"),
	HMDProp_LensCenterLeftV_Float_2023							UMETA(DisplayName = "HMDProp_LensCenterLeftV_Float"),
	HMDProp_LensCenterRightU_Float_2024							UMETA(DisplayName = "HMDProp_LensCenterRightU_Float"),
	HMDProp_LensCenterRightV_Float_2025							UMETA(DisplayName = "HMDProp_LensCenterRightV_Float"),
	HMDProp_UserHeadToEyeDepthMeters_Float_2026					UMETA(DisplayName = "HMDProp_UserHeadToEyeDepthMeters_Float"),
	HMDProp_ScreenshotHorizontalFieldOfViewDegrees_Float_2034	UMETA(DisplayName = "HMDProp_ScreenshotHorizontalFieldOfViewDegrees_Float"),
	HMDProp_ScreenshotVerticalFieldOfViewDegrees_Float_2035		UMETA(DisplayName = "HMDProp_ScreenshotVerticalFieldOfViewDegrees_Float"),
	HMDProp_SecondsFromPhotonsToVblank_Float_2042				UMETA(DisplayName = "HMDProp_SecondsFromPhotonsToVblank_Float"),
	HMDProp_MinimumIpdStepMeters_Float_2060						UMETA(DisplayName = "HMDProp_MinimumIpdStepMeters_Float"),

	// 3 Prefix = 4000 series
	TrackRefProp_FieldOfViewLeftDegrees_Float_4000		UMETA(DisplayName = "TrackRefProp_FieldOfViewLeftDegrees_Float"),
	TrackRefProp_FieldOfViewRightDegrees_Float_4001		UMETA(DisplayName = "TrackRefProp_FieldOfViewRightDegrees_Float"),
	TrackRefProp_FieldOfViewTopDegrees_Float_4002		UMETA(DisplayName = "TrackRefProp_FieldOfViewTopDegrees_Float"),
	TrackRefProp_FieldOfViewBottomDegrees_Float_4003	UMETA(DisplayName = "TrackRefProp_FieldOfViewBottomDegrees_Float"),
	TrackRefProp_TrackingRangeMinimumMeters_Float_4004	UMETA(DisplayName = "TrackRefProp_TrackingRangeMinimumMeters_Float"),
	TrackRefProp_TrackingRangeMaximumMeters_Float_4005	UMETA(DisplayName = "TrackRefProp_TrackingRangeMaximumMeters_Float")
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Int32 : uint8
{
	// No prefix = 1000 series
	Prop_DeviceClass_Int32_1029						UMETA(DisplayName = "Prop_DeviceClass_Int32"),
	Prop_NumCameras_Int32_1039						UMETA(DisplayName = "Prop_NumCameras_Int32"),
	Prop_CameraFrameLayout_Int32_1040				UMETA(DisplayName = "Prop_CameraFrameLayout_Int32"), // EVRTrackedCameraFrameLayout value
	Prop_CameraStreamFormat_Int32_1041				UMETA(DisplayName = "Prop_CameraStreamFormat_Int32"),

	// 1 Prefix = 2000 series
	HMDProp_DisplayMCType_Int32_2008					UMETA(DisplayName = "HMDProp_DisplayMCType_Int32"),
	HMDProp_EdidVendorID_Int32_2011						UMETA(DisplayName = "HMDProp_EdidVendorID_Int32"),
	HMDProp_EdidProductID_Int32_2015					UMETA(DisplayName = "HMDProp_EdidProductID_Int32"),
	HMDProp_DisplayGCType_Int32_2017					UMETA(DisplayName = "HMDProp_DisplayGCType_Int32"),
	HMDProp_CameraCompatibilityMode_Int32_2033			UMETA(DisplayName = "HMDProp_CameraCompatibilityMode_Int32"),
	HMDProp_DisplayMCImageWidth_Int32_2038				UMETA(DisplayName = "HMDProp_DisplayMCImageWidth_Int32"),
	HMDProp_DisplayMCImageHeight_Int32_2039				UMETA(DisplayName = "HMDProp_DisplayMCImageHeight_Int32"),
	HMDProp_DisplayMCImageNumChannels_Int32_2040		UMETA(DisplayName = "HMDProp_DisplayMCImageNumChannels_Int32"),
	HMDProp_ExpectedTrackingReferenceCount_Int32_2049	UMETA(DisplayName = "HMDProp_ExpectedTrackingReferenceCount_Int32"),
	HMDProp_ExpectedControllerCount_Int32_2050			UMETA(DisplayName = "HMDProp_ExpectedControllerCount_Int32"),
	HMDProp_DistortionMeshResolution_Int32_2056			UMETA(DisplayName = "HMDProp_DistortionMeshResolution_Int32"),

	HMDProp_HmdTrackingStyle_Int32_2075					UMETA(DisplayName = "HMDProp_HmdTrackingStyle_Int32"),

	// 2 Prefix = 3000 series
	ControllerProp_Axis0Type_Int32_3002				UMETA(DisplayName = "ControllerProp_Axis0Type_Int32"),
	ControllerPropProp_Axis1Type_Int32_3003			UMETA(DisplayName = "ControllerPropProp_Axis1Type_Int32"),
	ControllerPropProp_Axis2Type_Int32_3004			UMETA(DisplayName = "ControllerPropProp_Axis2Type_Int32"),
	ControllerPropProp_Axis3Type_Int32_3005			UMETA(DisplayName = "ControllerPropProp_Axis3Type_Int32"),
	ControllerPropProp_Axis4Type_Int32_3006			UMETA(DisplayName = "ControllerPropProp_Axis4Type_Int32"),
	ControllerProp_ControllerRoleHint_Int32_3007	UMETA(DisplayName = "ControllerProp_ControllerRoleHint_Int32"),

	// Tracked device props
	TrackRefProp_Nonce_Int32_4008 	UMETA(DisplayName = "TrackRefProp_Nonce_Int32"),

	// Driver Props
	DriverProp_ControllerHandSelectionPriority_Int32_7002	UMETA(DisplayName = "DriverProp_ControllerHandSelectionPriority_Int32")
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_UInt64 : uint8
{
	// No prefix = 1000 series
	Prop_HardwareRevision_Uint64_1017				UMETA(DisplayName = "Prop_HardwareRevision_Uint64"),
	Prop_FirmwareVersion_Uint64_1018				UMETA(DisplayName = "Prop_FirmwareVersion_Uint64"),
	Prop_FPGAVersion_Uint64_1019					UMETA(DisplayName = "Prop_FPGAVersion_Uint64"),
	Prop_VRCVersion_Uint64_1020						UMETA(DisplayName = "Prop_VRCVersion_Uint64"),
	Prop_RadioVersion_Uint64_1021					UMETA(DisplayName = "Prop_RadioVersion_Uint64"),
	Prop_DongleVersion_Uint64_1022					UMETA(DisplayName = "Prop_DongleVersion_Uint64"),
	Prop_ParentDriver_Uint64_1034					UMETA(DisplayName = "Prop_ParentDriver_Uint64"),
	Prop_BootloaderVersion_Uint64_1044				UMETA(DisplayName = "Prop_BootloaderVersion_Uint64"),

	// 1 Prefix = 2000 series
	HMDProp_CurrentUniverseId_Uint64_2004			UMETA(DisplayName = "HMDProp_CurrentUniverseId_Uint64"),
	HMDProp_PreviousUniverseId_Uint64_2005			UMETA(DisplayName = "HMDProp_PreviousUniverseId_Uint64"),
	HMDProp_DisplayFirmwareVersion_Uint64_2006		UMETA(DisplayName = "HMDProp_DisplayFirmwareVersion_Uint64"),
	HMDProp_CameraFirmwareVersion_Uint64_2027		UMETA(DisplayName = "HMDProp_CameraFirmwareVersion_Uint64"),
	HMDProp_DisplayFPGAVersion_Uint64_2029			UMETA(DisplayName = "HMDProp_DisplayFPGAVersion_Uint64"),
	HMDProp_DisplayBootloaderVersion_Uint64_2030	UMETA(DisplayName = "HMDProp_DisplayBootloaderVersion_Uint64"),
	HMDProp_DisplayHardwareVersion_Uint64_2031		UMETA(DisplayName = "HMDProp_DisplayHardwareVersion_Uint64"),
	HMDProp_AudioFirmwareVersion_Uint64_2032		UMETA(DisplayName = "HMDProp_AudioFirmwareVersion_Uint64"),
	HMDProp_GraphicsAdapterLuid_Uint64_2045			UMETA(DisplayName = "HMDProp_GraphicsAdapterLuid_Uint64"),
	HMDProp_AudioBridgeFirmwareVersion_Uint64_2061	UMETA(DisplayName = "HMDProp_AudioBridgeFirmwareVersion_Uint64"),
	HMDProp_ImageBridgeFirmwareVersion_Uint64_2062	UMETA(DisplayName = "HMDProp_ImageBridgeFirmwareVersion_Uint64"),
	HMDProp_AdditionalRadioFeatures_Uint64_2070		UMETA(DisplayName = "HMDProp_AdditionalRadioFeatures_Uint64"),

	// 2 Prefix = 3000 series
	ControllerProp_SupportedButtons_Uint64_3001		UMETA(DisplayName = "ControllerProp_SupportedButtons_Uint64")
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Matrix34 : uint8
{
	// No prefix = 1000 series
	Prop_StatusDisplayTransform_Matrix34_1013		UMETA(DisplayName = "Prop_StatusDisplayTransform_Matrix34"),

	// 1 Prefix = 2000 series
	HMDProp_CameraToHeadTransform_Matrix34_2016		UMETA(DisplayName = "HMDProp_CameraToHeadTransform_Matrix34"),
	HMDProp_CameraToHeadTransforms_Matrix34_2055	UMETA(DisplayName = "HMDProp_CameraToHeadTransforms_Matrix34"),
	HMDProp_ImuToHeadTransform_Matrix34_2063		UMETA(DisplayName = "HMDProp_ImToHeadTransform_Matrix34")
};

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum class EBPOpenVRHMDDeviceType : uint8
{
	DT_SteamVR,
	DT_ValveIndex,
	DT_Vive,
	DT_ViveCosmos,
	DT_OculusQuestHMD,
	DT_OculusHMD,
	DT_WindowsMR,
	//DT_OSVR,
	DT_Unknown
};

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum class EBPOpenVRControllerDeviceType : uint8
{
	DT_IndexController,
	DT_ViveController,
	DT_CosmosController,
	DT_RiftController,
	DT_RiftSController,
	DT_QuestController,
	DT_WMRController,
	DT_UnknownController
};

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class OPENVREXPANSIONPLUGIN_API UOpenVRExpansionFunctionLibrary : public UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_BODY()

public:
	UOpenVRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer);

	~UOpenVRExpansionFunctionLibrary();
public:

#if STEAMVR_SUPPORTED_PLATFORM
	static FBPOpenVRCameraHandle OpenCamera;

	static FORCEINLINE FMatrix ToFMatrix(const vr::HmdMatrix34_t& tm)
	{
		// Rows and columns are swapped between vr::HmdMatrix34_t and FMatrix
		return FMatrix(
			FPlane(tm.m[0][0], tm.m[1][0], tm.m[2][0], 0.0f),
			FPlane(tm.m[0][1], tm.m[1][1], tm.m[2][1], 0.0f),
			FPlane(tm.m[0][2], tm.m[1][2], tm.m[2][2], 0.0f),
			FPlane(tm.m[0][3], tm.m[1][3], tm.m[2][3], 1.0f));
	}

	static FORCEINLINE FMatrix ToFMatrix(const vr::HmdMatrix44_t& tm)
	{
		// Rows and columns are swapped between vr::HmdMatrix44_t and FMatrix
		return FMatrix(
			FPlane(tm.m[0][0], tm.m[1][0], tm.m[2][0], tm.m[3][0]),
			FPlane(tm.m[0][1], tm.m[1][1], tm.m[2][1], tm.m[3][1]),
			FPlane(tm.m[0][2], tm.m[1][2], tm.m[2][2], tm.m[3][2]),
			FPlane(tm.m[0][3], tm.m[1][3], tm.m[2][3], tm.m[3][3]));
	}

	static FORCEINLINE vr::HmdMatrix34_t ToHmdMatrix34(const FMatrix& tm)
	{
		// Rows and columns are swapped between vr::HmdMatrix34_t and FMatrix
		vr::HmdMatrix34_t out;

		out.m[0][0] = tm.M[0][0];
		out.m[1][0] = tm.M[0][1];
		out.m[2][0] = tm.M[0][2];

		out.m[0][1] = tm.M[1][0];
		out.m[1][1] = tm.M[1][1];
		out.m[2][1] = tm.M[1][2];

		out.m[0][2] = tm.M[2][0];
		out.m[1][2] = tm.M[2][1];
		out.m[2][2] = tm.M[2][2];

		out.m[0][3] = tm.M[3][0];
		out.m[1][3] = tm.M[3][1];
		out.m[2][3] = tm.M[3][2];

		return out;
	}

#endif

	// Gets whether an HMD device is connected, this is an expanded version for SteamVR
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetOpenVRHMDType"))
		static EBPOpenVRHMDDeviceType GetOpenVRHMDType();

	// Gets what type of controller is plugged in
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetOpenVRControllerType"))
		static EBPOpenVRControllerDeviceType GetOpenVRControllerType();

	// Checks if a specific OpenVR device is connected, index names are assumed, they may not be exact
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true"))
	static bool IsOpenVRDeviceConnected(int32 DeviceIndex);

	// Get what type a specific openVR device index is
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true"))
	static EBPOpenVRTrackedDeviceClass GetOpenVRDeviceType(int32 DeviceIndex);

	// Get a list of all currently tracked devices and their types, index in the array is their device index
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true"))
	static void GetOpenVRDevices(TArray<EBPOpenVRTrackedDeviceClass> &FoundDevices);

	// Get a list of all currently tracked devices of a specific type
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true"))
	static void GetOpenVRDevicesByType(EBPOpenVRTrackedDeviceClass TypeToRetreive, TArray<int32> &FoundIndexs);

	// Gets the model / texture of a SteamVR Device, can use to fill procedural mesh components or just get the texture of them to apply to a pre-made model.
	// If the render model name override is empty then the render model name will be automatically retrieved from SteamVR and RenderModelNameOut will be filled with it.
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", DisplayName = "GetVRDeviceModelAndTexture", ExpandEnumAsExecs = "Result", AdvancedDisplay = "OverrideDeviceID"))
	static UTexture2D * GetVRDeviceModelAndTexture(UObject* WorldContextObject, FString RenderModelNameOverride, FString & RenderModelNameOut, EBPOpenVRTrackedDeviceClass DeviceType, TArray<UProceduralMeshComponent *> ProceduralMeshComponentsToFill, bool bCreateCollision, EAsyncBlueprintResultSwitch &Result, int32 OverrideDeviceID = -1);
	
	// Gets a String device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyString", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyString(EVRDeviceProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue, EBPOVRResultSwitch & Result);

	// Gets a Bool device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyBool", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyBool(EVRDeviceProperty_Bool PropertyToRetrieve, int32 DeviceID, bool & BoolValue, EBPOVRResultSwitch & Result);

	// Gets a Float device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyFloat", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyFloat(EVRDeviceProperty_Float PropertyToRetrieve, int32 DeviceID, float & FloatValue, EBPOVRResultSwitch & Result);

	// Gets a Int32 device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyInt32", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyInt32(EVRDeviceProperty_Int32 PropertyToRetrieve, int32 DeviceID, int32 & IntValue, EBPOVRResultSwitch & Result);

	// Gets a UInt64 device property as a string (Blueprints do not support int64)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyUInt64", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyUInt64(EVRDeviceProperty_UInt64 PropertyToRetrieve, int32 DeviceID, FString & UInt64Value, EBPOVRResultSwitch & Result);

	// Gets a Matrix34 device property as a Transform
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyMatrix34AsTransform", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyMatrix34AsTransform(EVRDeviceProperty_Matrix34 PropertyToRetrieve, int32 DeviceID, FTransform & TransformValue, EBPOVRResultSwitch & Result);

	// VR Camera options

	// Returns if there is a VR camera and what its pixel height / width is
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "HasVRCamera"))
	static bool HasVRCamera(EOpenVRCameraFrameType FrameType, int32 &Width, int32 &Height);

	// Gets a screen cap from the HMD camera if there is one
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "GetVRCameraFrame", ExpandEnumAsExecs = "Result"))
	static void GetVRCameraFrame(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EOpenVRCameraFrameType FrameType, EBPOVRResultSwitch & Result, UTexture2D * TargetRenderTarget = nullptr);

	// Create Camera Render Target, automatically pulls the correct texture size and format
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "CreateCameraTexture2D", ExpandEnumAsExecs = "Result"))
	static UTexture2D * CreateCameraTexture2D(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EOpenVRCameraFrameType FrameType, EBPOVRResultSwitch & Result);

	// Acquire the vr camera for access (wakes it up) and returns a handle to use for functions regarding it (MUST RELEASE CAMERA WHEN DONE)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "AcquireVRCamera", ExpandEnumAsExecs = "Result"))
	static void AcquireVRCamera(FBPOpenVRCameraHandle & CameraHandle, EBPOVRResultSwitch & Result);

	// Releases the vr camera from access - you MUST call this when done with the camera
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "ReleaseVRCamera", ExpandEnumAsExecs = "Result"))
	static void ReleaseVRCamera(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EBPOVRResultSwitch & Result);

	// Checks if a camera handle is valid
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "IsValid"))
	static bool IsValid(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle);

	// VR compositor

	// Override the standard skybox texture in steamVR - LatLong format - need to call ClearSkyboxOverride when finished
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool SetSkyboxOverride_LatLong(UTexture2D * LatLongSkybox);

	// Override the standard skybox texture in steamVR - LatLong stereo pair - need to call ClearSkyboxOverride when finished
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool SetSkyboxOverride_LatLongStereoPair(UTexture2D * LatLongSkyboxL, UTexture2D * LatLongSkyboxR);

	// Override the standard skybox texture in steamVR - 6 cardinal textures - need to call ClearSkyboxOverride when finished
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool SetSkyboxOverride(UTexture * tFront, UTexture2D * tBack, UTexture * tLeft, UTexture * tRight, UTexture * tTop, UTexture * tBottom);

	// Remove skybox override in steamVR
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool ClearSkyboxOverride();

	/** Fades the view on the HMD to the specified color. The fade will take fSeconds, and the color values are between
	* 0.0 and 1.0. This color is faded on top of the scene based on the alpha parameter. Removing the fade color instantly
	* would be FadeToColor( 0.0, 0.0, 0.0, 0.0, 0.0 ).  Values are in un-premultiplied alpha space. */
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool FadeHMDToColor(float fSeconds, FColor Color, bool bBackground = false);


	/** Get current fade color value. */
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool GetCurrentHMDFadeColor(FColor & ColorOut, bool bBackground = false);

	/** Fading the Grid in or out in fSeconds */
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool FadeVRGrid(float fSeconds, bool bFadeIn);

	/** Get current alpha value of grid. */
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool GetCurrentVRGridAlpha(float & VRGridAlpha);

	// Sets whether the compositor is allows to render or not (reverts to base compositor / grid when active)
	// Useful to place players out of the app during frame drops/hitches/loading and into the vr skybox.
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|Compositor", meta = (bIgnoreSelf = "true"))
		static bool SetSuspendRendering(bool bSuspendRendering);

#if STEAMVR_SUPPORTED_PLATFORM
	static vr::Texture_t CreateOpenVRTexture_t(UTexture * Texture)
	{
		vr::Texture_t VRTexture;

		if (Texture)
			VRTexture.handle = Texture->Resource->TextureRHI->GetNativeResource();
		else
			VRTexture.handle = NULL;

		VRTexture.eColorSpace = vr::EColorSpace::ColorSpace_Auto;

		VRTexture.eType = vr::ETextureType::TextureType_OpenGL;
#if PLATFORM_MAC
		VRTexture.eType = vr::ETextureType::TextureType_IOSurface;
#else
		if (IsPCPlatform(GMaxRHIShaderPlatform))
		{
			if (IsVulkanPlatform(GMaxRHIShaderPlatform))
			{
				VRTexture.eType = vr::ETextureType::TextureType_Vulkan;
			}
			else if (IsOpenGLPlatform(GMaxRHIShaderPlatform))
			{
				VRTexture.eType = vr::ETextureType::TextureType_OpenGL;
			}
#if PLATFORM_WINDOWS
			else
			{
				VRTexture.eType = vr::ETextureType::TextureType_DirectX;
			}
#endif
		}
#endif
		return VRTexture;
	}
#endif
};	
