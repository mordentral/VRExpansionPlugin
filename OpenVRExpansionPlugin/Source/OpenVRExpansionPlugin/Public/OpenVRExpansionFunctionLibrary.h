// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "IMotionController.h"
#include "VRBPDataTypes.h"

// #Note: Can now access VRSystem from the SteamHMD directly, however still cannot use the static pVRGenericInterface point due to 
// linking errors since can't attain .cpp reference. So useless to convert to blueprint library as the render models wouldn't work.

//Re-defined here as I can't load ISteamVRPlugin on non windows platforms
// Make sure to update if it changes

/*4.16 linux now support under steamvr supported platforms*/
// 4.16 UNCOMMENT
//#define STEAMVR_SUPPORTED_PLATFORMS(PLATFORM_LINUX || (PLATFORM_WINDOWS && WINVER > 0x0502))

// 4.16 COMMENT
#define STEAMVR_SUPPORTED_PLATFORM (PLATFORM_WINDOWS && WINVER > 0x0502)
// #TODO: Check for #isdef and value instead?


#if STEAMVR_SUPPORTED_PLATFORM
#include "openvr.h"
#include "ISteamVRPlugin.h"

//This is a stupid way of gaining access to this header...see build.cs
#include "SteamVRHMD.h"
//#include "SteamVRPrivatePCH.h" // Need a define in here....this is so ugly
#include "SteamVRPrivate.h" // Now in here since 4.15
#include "SteamVRFunctionLibrary.h"

#endif // STEAMVR_SUPPORTED_PLATFORM
//#include "openvr.h"

#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
// Or procedural mesh component throws an error....
//#include "PhysicsEngine/ConvexElem.h" // Fixed in 4.13.1?

#include "HeadMountedDisplay.h" 
#include "HeadMountedDisplayFunctionLibrary.h"

#include "OpenVRExpansionFunctionLibrary.generated.h"

//General Advanced Sessions Log
DECLARE_LOG_CATEGORY_EXTERN(OpenVRExpansionFunctionLibraryLog, Log, All);


// This makes a lot of the blueprint functions cleaner
UENUM()
enum class EBPVRDeviceIndex : uint8
{
	/*0 TrackedDeviceClass_HMD
	1 TrackedDeviceClass_TrackingReference
	2 TrackedDeviceClass_TrackingReference
	3 TrackedDeviceClass_Controller
	4 TrackedDeviceClass_Controller
	5 TrackedDeviceClass_GenericTracker
	6 TrackedDeviceClass_GenericTracker
	7 TrackedDeviceClass_GenericTracker
	*/

	// On Success
	HMD = 0,
	FirstController = 1,
	SecondController = 2,
	FirstTracking_Reference = 3,
	SecondTracking_Reference = 4,
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
	None = 255
};

//USTRUCT(BlueprintType, Category = "VRExpansionFunctions|SteamVR")
struct OPENVREXPANSIONPLUGIN_API FBPOpenVRKeyboardHandle
{
	//GENERATED_BODY()
public:

	uint64_t VRKeyboardHandle;

	FBPOpenVRKeyboardHandle()
	{
		VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
	}
	const bool IsValid()
	{
		return VRKeyboardHandle != vr::k_ulOverlayHandleInvalid;
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
		pCameraHandle = INVALID_TRACKED_CAMERA_HANDLE;
	}
	const bool IsValid()
	{
		return pCameraHandle != INVALID_TRACKED_CAMERA_HANDLE;
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

UENUM(BlueprintType)
enum class EVRDeviceProperty_String : uint8
{
	Prop_TrackingSystemName_String				= 0, ////
	Prop_ModelNumber_String						= 1,
	Prop_SerialNumber_String					= 2,
	Prop_RenderModelName_String					= 3,
	Prop_ManufacturerName_String				= 5,
	Prop_TrackingFirmwareVersion_String			= 6,
	Prop_HardwareRevision_String				= 7,
	Prop_AllWirelessDongleDescriptions_String	= 8,
	Prop_ConnectedWirelessDongle_String			= 9,
	Prop_Firmware_ManualUpdateURL_String		= 16
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Bool : uint8
{	
	Prop_WillDriftInYaw_Bool = 4,	
	Prop_DeviceIsWireless_Bool = 10,
	Prop_DeviceIsCharging_Bool = 11,
	Prop_Firmware_UpdateAvailable_Bool = 14,
	Prop_Firmware_ManualUpdate_Bool = 15,
	Prop_BlockServerShutdown_Bool = 23,
	Prop_CanUnifyCoordinateSystemWithHmd_Bool = 24,
	Prop_ContainsProximitySensor_Bool = 25,
	Prop_DeviceProvidesBatteryStatus_Bool = 26 ///////
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Float : uint8
{
	Prop_DeviceBatteryPercentage_Float = 12 // 0 is empty, 1 is full
};

UENUM(BlueprintType)
enum class EVRControllerProperty_String : uint8
{
	Prop_AttachedDeviceId_String = 0
};

// Not using due to BP incompatibility
/*
UENUM(BlueprintType)
enum class EVRDeviceProperty_UInt64
{
	Prop_HardwareRevision_Uint64 = 17,
	Prop_FirmwareVersion_Uint64 = 18,
	Prop_FPGAVersion_Uint64 = 19,
	Prop_VRCVersion_Uint64 = 20,
	Prop_RadioVersion_Uint64 = 21,
	Prop_DongleVersion_Uint64 = 22
};
*/

/*
// Properties that are unique to TrackedDeviceClass_HMD
Prop_ReportsTimeSinceVSync_Bool				= 2000,
Prop_SecondsFromVsyncToPhotons_Float		= 2001,
Prop_DisplayFrequency_Float					= 2002,
Prop_UserIpdMeters_Float					= 2003,
Prop_CurrentUniverseId_Uint64				= 2004,
Prop_PreviousUniverseId_Uint64				= 2005,
Prop_DisplayFirmwareVersion_String			= 2006,
Prop_IsOnDesktop_Bool						= 2007,
Prop_DisplayMCType_Int32					= 2008,
Prop_DisplayMCOffset_Float					= 2009,
Prop_DisplayMCScale_Float					= 2010,
Prop_EdidVendorID_Int32						= 2011,
Prop_DisplayMCImageLeft_String              = 2012,
Prop_DisplayMCImageRight_String             = 2013,
Prop_DisplayGCBlackClamp_Float				= 2014,
Prop_EdidProductID_Int32					= 2015,
Prop_CameraToHeadTransform_Matrix34		    = 2016,

// Properties that are unique to TrackedDeviceClass_TrackingReference
Prop_FieldOfViewLeftDegrees_Float			= 4000,
Prop_FieldOfViewRightDegrees_Float			= 4001,
Prop_FieldOfViewTopDegrees_Float			= 4002,
Prop_FieldOfViewBottomDegrees_Float			= 4003,
Prop_TrackingRangeMinimumMeters_Float		= 4004,
Prop_TrackingRangeMaximumMeters_Float		= 4005,

*/

// Had to turn this in to a UObject, I felt the easiest way to use it was as an actor component to the player controller
// It can be returned to just a blueprint library if epic ever upgrade steam to 1.33 or above
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class OPENVREXPANSIONPLUGIN_API UOpenVRExpansionFunctionLibrary : public /*UActorComponent*/UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_UCLASS_BODY()
	~UOpenVRExpansionFunctionLibrary();
public:

	//void* OpenVRDLLHandle;

	// Currently OpenVR only supports a single HMD camera as it only supports a single HMD (default index 0)
	// So support auto releasing it with this, in case the user messes up and doesn't do so.

	//@todo steamvr: Remove GetProcAddress() workaround once we have updated to Steamworks 1.33 or higher
//	pVRInit VRInitFn;
	//pVRShutdown VRShutdownFn;
	//pVRIsHmdPresent VRIsHmdPresentFn;
	//pVRGetStringForHmdError VRGetStringForHmdErrorFn;

#if STEAMVR_SUPPORTED_PLATFORM
	//static pVRIsHmdPresent VRIsHmdPresentFn;
	static pVRGetGenericInterface VRGetGenericInterfaceFn;
	static FBPOpenVRCameraHandle OpenCamera;
	//vr::IVRChaperone* VRChaperone;
#endif

	//UPROPERTY(BlueprintReadOnly)
	//bool VRGetGenericInterfaceFn;

	// Closes the handles for the library
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true"))
	static bool IsOpenVRDeviceConnected(EBPVRDeviceIndex OpenVRDeviceIndex);

	// Gets the model / texture of a SteamVR Device, can use to fill procedural mesh components or just get the texture of them to apply to a pre-made model.
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", DisplayName = "GetVRDeviceModelAndTexture", ExpandEnumAsExecs = "Result"))
	static UTexture2D * GetVRDeviceModelAndTexture(UObject* WorldContextObject, EBPSteamVRTrackedDeviceType DeviceType, TArray<UProceduralMeshComponent *> ProceduralMeshComponentsToFill, bool bCreateCollision, EAsyncBlueprintResultSwitch &Result, EBPVRDeviceIndex OverrideDeviceID = EBPVRDeviceIndex::None);
	
	// Gets a String device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyString"))
	static bool GetVRDevicePropertyString(EVRDeviceProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue);

	// Gets a Bool device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyBool"))
	static bool GetVRDevicePropertyBool(EVRDeviceProperty_Bool PropertyToRetrieve, int32 DeviceID, bool & BoolValue);

	// Gets a Float device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyFloat"))
	static bool GetVRDevicePropertyFloat(EVRDeviceProperty_Float PropertyToRetrieve, int32 DeviceID, float & FloatValue);

	// Gets a String controller property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRControllerPropertyString"))
	static bool GetVRControllerPropertyString(EVRControllerProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue);

	// VR Camera options

	// Returns if there is a VR camera and what its pixel height / width is
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "HasVRCamera"))
	static bool HasVRCamera(EOpenVRCameraFrameType FrameType, int32 &Width, int32 &Height);

	// Gets a screen cap from the HMD camera if there is one
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "GetVRCameraFrame", ExpandEnumAsExecs = "Result"))
	static void GetVRCameraFrame(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EOpenVRCameraFrameType FrameType, EBPVRResultSwitch & Result, UTexture2D * TargetRenderTarget = nullptr);

	// Create Camera Render Target
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "CreateCameraTexture2D", ExpandEnumAsExecs = "Result"))
	static UTexture2D * CreateCameraTexture2D(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EOpenVRCameraFrameType FrameType, EBPVRResultSwitch & Result);

	// Acquire the vr camera for access (wakes it up)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "AcquireVRCamera", ExpandEnumAsExecs = "Result"))
	static void AcquireVRCamera(FBPOpenVRCameraHandle & CameraHandle, EBPVRResultSwitch & Result);

	// Releases the vr camera from access - you MUST call this when done with the camera
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "ReleaseVRCamera", ExpandEnumAsExecs = "Result"))
	static void ReleaseVRCamera(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle, EBPVRResultSwitch & Result);

	// Releases the vr camera from access
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR|VRCamera", meta = (bIgnoreSelf = "true", DisplayName = "IsValid"))
	static bool IsValid(UPARAM(ref) FBPOpenVRCameraHandle & CameraHandle);
};	
