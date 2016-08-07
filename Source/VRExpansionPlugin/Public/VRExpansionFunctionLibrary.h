// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "IMotionController.h"
#include "ISteamVRPlugin.h"
#if STEAMVR_SUPPORTED_PLATFORMS
#include "openvr.h"
#endif // STEAMVR_SUPPORTED_PLATFORMS
//#include "openvr.h"
#include "HeadMountedDisplay.h" 

//This is a stupid way of gaining access to this header...see build.cs
#include "SteamVRHMD.h"
#include "SteamVRPrivatePCH.h" // Need a define in here....this is so ugly

// Or procedural mesh component throws an error....
#include "PhysicsEngine/ConvexElem.h"
#include "ProceduralMeshComponent.h"

#include "SteamVRFunctionLibrary.h"
#include "KismetProceduralMeshLibrary.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "VRExpansionFunctionLibrary.generated.h"

//General Advanced Sessions Log
DECLARE_LOG_CATEGORY_EXTERN(VRExpansionFunctionLibraryLog, Log, All);


UENUM(BlueprintType)
enum class EVRDeviceProperty_String
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
enum EVRDeviceProperty_Bool
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
enum EVRDeviceProperty_Float
{
	Prop_DeviceBatteryPercentage_Float = 12 // 0 is empty, 1 is full
};

UENUM(BlueprintType)
enum EVRControllerProperty_String
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
class VREXPANSIONPLUGIN_API UVRExpansionFunctionLibrary : public UActorComponent//UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_UCLASS_BODY()
	~UVRExpansionFunctionLibrary();
public:

	void* OpenVRDLLHandle;

	//@todo steamvr: Remove GetProcAddress() workaround once we have updated to Steamworks 1.33 or higher
	pVRInit VRInitFn;
	pVRShutdown VRShutdownFn;
	//pVRIsHmdPresent VRIsHmdPresentFn;
	pVRGetStringForHmdError VRGetStringForHmdErrorFn;
	pVRGetGenericInterface VRGetGenericInterfaceFn;

	bool LoadOpenVRModule();
	void UnloadOpenVRModule();


	bool IsLocallyControlled() const
	{
		// Epic used a check for a player controller to control has authority, however the controllers are always attached to a pawn
		// So this check would have always failed to work in the first place.....

		APawn* Owner = Cast<APawn>(GetOwner());

		if (!Owner)
		{
			//const APlayerController* Actor = Cast<APlayerController>(GetOwner());
			//if (!Actor)
			return false;

			//return Actor->IsLocalPlayerController();
		}

		return Owner->IsLocallyControlled();
	}

	// Opends the handles for the library
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true"))
	bool OpenVRHandles();

	// Closes the handles for the library
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true"))
	bool CloseVRHandles();

	UPROPERTY(BlueprintReadWrite)
	bool bInitialized;

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetIsHMDConnected"))
	static bool GetIsHMDConnected();

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetHMDType"))
	static EBPHMDDeviceType GetHMDType();

	// Gets the model / texture of a SteamVR Device, can use to fill procedural mesh components or just get the texture of them to apply to a pre-made model.
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", DisplayName = "GetVRDeviceModelAndTexture"))
	UTexture2D * GetVRDeviceModelAndTexture(UObject* WorldContextObject, TEnumAsByte<ESteamVRTrackedDeviceType> DeviceType, TArray<UProceduralMeshComponent *> ProceduralMeshComponentsToFill, bool & bSucceeded, bool bCreateCollision/*, TArray<uint8> & OutRawTexture, bool bReturnRawTexture = false*/);
	
	// Gets a String device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyString"))
	bool GetVRDevicePropertyString(TEnumAsByte<EVRDeviceProperty_String> PropertyToRetrieve, int32 DeviceID, FString & StringValue);

	// Gets a Bool device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyBool"))
	bool GetVRDevicePropertyBool(TEnumAsByte<EVRDeviceProperty_Bool> PropertyToRetrieve, int32 DeviceID, bool & BoolValue);

	// Gets a Float device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyFloat"))
	bool GetVRDevicePropertyFloat(TEnumAsByte<EVRDeviceProperty_Float> PropertyToRetrieve, int32 DeviceID, float & FloatValue);

	// Gets a String controller property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetVRControllerPropertyString"))
	bool GetVRControllerPropertyString(TEnumAsByte<EVRControllerProperty_String> PropertyToRetrieve, int32 DeviceID, FString & StringValue);

};	
