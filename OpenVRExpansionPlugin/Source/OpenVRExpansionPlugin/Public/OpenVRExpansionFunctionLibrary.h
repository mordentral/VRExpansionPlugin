// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "IMotionController.h"
#include "VRBPDatatypes.h"

// #Note: Can now access VRSystem from the SteamHMD directly, however still cannot use the static pVRGenericInterface point due to 
// linking errors since can't attain .cpp reference. So useless to convert to blueprint library as the render models wouldn't work.

//Re-defined here as I can't load ISteamVRPlugin on non windows platforms
// Make sure to update if it changes

/*4.16 linux now support under steamvr supported platforms*/
// 4.16 UNCOMMENT
#define STEAMVR_SUPPORTED_PLATFORM (PLATFORM_LINUX || (PLATFORM_WINDOWS && WINVER > 0x0502))

// 4.16 COMMENT
//#define STEAMVR_SUPPORTED_PLATFORM (PLATFORM_WINDOWS && WINVER > 0x0502)
// #TODO: Check for #isdef and value instead?


#if STEAMVR_SUPPORTED_PLATFORM
#include "openvr.h"
#include "ISteamVRPlugin.h"
#include "SteamVRFunctionLibrary.h"

#endif // STEAMVR_SUPPORTED_PLATFORM

#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
// Or procedural mesh component throws an error....
//#include "PhysicsEngine/ConvexElem.h" // Fixed in 4.13.1?

//#include "HeadMountedDisplay.h" 
//#include "HeadMountedDisplayFunctionLibrary.h"
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

	// Describes what kind of object is being tracked at a given ID 
	enum ETrackedDeviceClass
{
	TrackedDeviceClass_Invalid = 0,				// the ID was not valid.
	TrackedDeviceClass_HMD = 1,					// Head-Mounted Displays
	TrackedDeviceClass_Controller = 2,			// Tracked controllers
	TrackedDeviceClass_GenericTracker = 3,		// Generic trackers, similar to controllers
	TrackedDeviceClass_TrackingReference = 4,	// Camera and base stations that serve as tracking reference points
};
	*/

	// On Success
	HMD = 0,
	FirstTracking_Reference = 1,
	SecondTracking_Reference = 2,
	FirstController = 3,
	SecondController = 4,

	//FirstController = 1,
	//SecondController = 2,
	//FirstTracking_Reference = 3,
	//SecondTracking_Reference = 4,
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
		//static const VROverlayHandle_t k_ulOverlayHandleInvalid = 0;	
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

// #TODO: Update these
UENUM(BlueprintType)
enum class EVRDeviceProperty_String : uint8
{
	// No prefix = 1000 series
	Prop_TrackingSystemName_String				= 0, ////
	Prop_ModelNumber_String						= 1,
	Prop_SerialNumber_String					= 2,
	Prop_RenderModelName_String					= 3,
	Prop_ManufacturerName_String				= 5,
	Prop_TrackingFirmwareVersion_String			= 6,
	Prop_HardwareRevision_String				= 7,
	Prop_AllWirelessDongleDescriptions_String	= 8,
	Prop_ConnectedWirelessDongle_String			= 9,
	Prop_Firmware_ManualUpdateURL_String		= 16,
	Prop_Firmware_ProgrammingTarget_String		= 28,
	Prop_DriverVersion_String					= 31,

	// 1 prefix = 2000 series
	HMDProp_DisplayMCImageLeft_String			= 112,
	HMDProp_DisplayMCImageRight_String			= 113,
	HMDProp_DisplayGCImage_String				= 121,
	HMDProp_CameraFirmwareDescription_String	= 128,

	// 2 prefix = 3000 series
	ControllerProp_AttachedDeviceId_String		= 200
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Bool : uint8
{	
	// No prefix = 1000 series
	Prop_WillDriftInYaw_Bool					= 4,	
	Prop_DeviceIsWireless_Bool					= 10,
	Prop_DeviceIsCharging_Bool					= 11,
	Prop_Firmware_UpdateAvailable_Bool			= 14,
	Prop_Firmware_ManualUpdate_Bool				= 15,
	Prop_BlockServerShutdown_Bool				= 23,
	Prop_CanUnifyCoordinateSystemWithHmd_Bool	= 24,
	Prop_ContainsProximitySensor_Bool			= 25,
	Prop_DeviceProvidesBatteryStatus_Bool		= 26,
	Prop_DeviceCanPowerOff_Bool					= 27,
	Prop_HasCamera_Bool							= 30,
	Prop_Firmware_ForceUpdateRequired_Bool		= 32,
	Prop_ViveSystemButtonFixRequired_Bool		= 33,

	// 1 prefix = 2000 series
	HMDProp_ReportsTimeSinceVSync_Bool			= 100,
	HMDProp_IsOnDesktop_Bool					= 107,
	HMDProp_DisplaySuppressed_Bool				= 136,
	HMDProp_DisplayAllowNightMode_Bool			= 137,
	HMDProp_UsesDriverDirectMode_Bool			= 142
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Float : uint8
{
	// No Prefix = 1000 series
	Prop_DeviceBatteryPercentage_Float						= 12, // 0 is empty, 1 is full

	// 1 Prefix = 2000 series
	HMDProp_SecondsFromVsyncToPhotons_Float					= 101,
	HMDProp_DisplayFrequency_Float							= 102,
	HMDProp_UserIpdMeters_Float								= 103,
	HMDProp_DisplayMCOffset_Float							= 109,
	HMDProp_DisplayMCScale_Float							= 110,
	HMDProp_DisplayGCBlackClamp_Float						= 114,
	HMDProp_DisplayGCOffset_Float							= 118,
	HMDProp_DisplayGCScale_Float							= 119,
	HMDProp_DisplayGCPrescale_Float							= 120,
	HMDProp_LensCenterLeftU_Float							= 122,
	HMDProp_LensCenterLeftV_Float							= 123,
	HMDProp_LensCenterRightU_Float							= 124,
	HMDProp_LensCenterRightV_Float							= 125,
	HMDProp_UserHeadToEyeDepthMeters_Float					= 126,
	HMDProp_ScreenshotHorizontalFieldOfViewDegrees_Float	= 134,
	HMDProp_ScreenshotVerticalFieldOfViewDegrees_Float		= 135
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Int32 : uint8
{
	// No prefix = 1000 series
	Prop_DeviceClass_Int32					= 29,

	// 1 Prefix = 2000 series
	HMDProp_DisplayMCType_Int32				= 108,
	HMDProp_EdidVendorID_Int32				= 111,
	HMDProp_EdidProductID_Int32				= 115,
	HMDProp_DisplayGCType_Int32				= 117,
	HMDProp_CameraCompatibilityMode_Int32	= 133,
	HMDProp_DisplayMCImageWidth_Int32		= 138,
	HMDProp_DisplayMCImageHeight_Int32		= 139,
	HMDProp_DisplayMCImageNumChannels_Int32 = 140,

	// 2 Prefix = 3000 series
	ControllerProp_Axis0Type_Int32			= 202, // Return value is of type EVRControllerAxisType
	ControllerPropProp_Axis1Type_Int32		= 203, // Return value is of type EVRControllerAxisType
	ControllerPropProp_Axis2Type_Int32		= 204, // Return value is of type EVRControllerAxisType
	ControllerPropProp_Axis3Type_Int32		= 205, // Return value is of type EVRControllerAxisType
	ControllerPropProp_Axis4Type_Int32		= 206, // Return value is of type EVRControllerAxisType
	ControllerProp_ControllerRoleHint_Int32 = 207 // Return value is of type ETrackedControllerRole
};

UENUM(BlueprintType)
enum class EVRDeviceProperty_UInt64 : uint8
{
	// No prefix = 1000 series
	Prop_HardwareRevision_Uint64			= 17,
	Prop_FirmwareVersion_Uint64				= 18,
	Prop_FPGAVersion_Uint64					= 19,
	Prop_VRCVersion_Uint64					= 20,
	Prop_RadioVersion_Uint64				= 21,
	Prop_DongleVersion_Uint64				= 22,
	Prop_ParentDriver_Uint64				= 34,

	// 1 Prefix = 2000 series
	HMDProp_CurrentUniverseId_Uint64		= 104,
	HMDProp_PreviousUniverseId_Uint64		= 105,
	HMDProp_DisplayFirmwareVersion_Uint64	= 106,
	HMDProp_CameraFirmwareVersion_Uint64	= 127,
	HMDProp_DisplayFPGAVersion_Uint64		= 129,
	HMDProp_DisplayBootloaderVersion_Uint64 = 130,
	HMDProp_DisplayHardwareVersion_Uint64	= 131,
	HMDProp_AudioFirmwareVersion_Uint64		= 132,

	// 2 Prefix = 3000 series
	ControllerProp_SupportedButtons_Uint64	= 201

};

UENUM(BlueprintType)
enum class EVRDeviceProperty_Matrix34 : uint8
{
	// No prefix = 1000 series
	Prop_StatusDisplayTransform_Matrix34	= 13,

	// 1 Prefix = 2000 series
	HMDProp_CameraToHeadTransform_Matrix34	= 116
};


/*
// Not implementing currently
Prop_StatusDisplayTransform_Matrix34 = 1013
*/

/*
// Not implementing currently

// Properties that are unique to TrackedDeviceClass_HMD
Prop_DisplayMCImageData_Binary				= 2041,
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
	//static pVRGetGenericInterface VRGetGenericInterfaceFn;
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
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyString", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyString(EVRDeviceProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue, EBPVRResultSwitch & Result);

	// Gets a Bool device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyBool", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyBool(EVRDeviceProperty_Bool PropertyToRetrieve, int32 DeviceID, bool & BoolValue, EBPVRResultSwitch & Result);

	// Gets a Float device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyFloat", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyFloat(EVRDeviceProperty_Float PropertyToRetrieve, int32 DeviceID, float & FloatValue, EBPVRResultSwitch & Result);

	// Gets a Int32 device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyInt32", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyInt32(EVRDeviceProperty_Int32 PropertyToRetrieve, int32 DeviceID, int32 & IntValue, EBPVRResultSwitch & Result);

	// Gets a UInt64 device property as a string (Blueprints do not support int64)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyUInt64", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyUInt64(EVRDeviceProperty_UInt64 PropertyToRetrieve, int32 DeviceID, FString & UInt64Value, EBPVRResultSwitch & Result);

	// Gets a Matrix34 device property as a Transform
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyMatrix34AsTransform", ExpandEnumAsExecs = "Result"))
	static void GetVRDevicePropertyMatrix34AsTransform(EVRDeviceProperty_Matrix34 PropertyToRetrieve, int32 DeviceID, FTransform & TransformValue, EBPVRResultSwitch & Result);

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
