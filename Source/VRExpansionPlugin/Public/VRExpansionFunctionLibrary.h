// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "IMotionController.h"

//Re-defined here as I can't load ISteamVRPlugin on non windows platforms
// Make sure to update if it changes
#define STEAMVR_SUPPORTED_PLATFORM (PLATFORM_WINDOWS && WINVER > 0x0502)

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
#include "VRExpansionFunctionLibrary.generated.h"

//General Advanced Sessions Log
DECLARE_LOG_CATEGORY_EXTERN(VRExpansionFunctionLibraryLog, Log, All);

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

UENUM(BlueprintType)
enum class EBPWorldType : uint8
{
	wNone,		// An untyped world, in most cases this will be the vestigial worlds of streamed in sub-levels
	wGame,		// The game world
	wEditor,	// A world being edited in the editor
	wPIE,		// A Play In Editor world
	wPreview,	// A preview world for an editor tool
	wInactive	// An editor world that was loaded but not currently being edited in the level editor
};

/**
* Redefining this for blueprint as it wasn't declared as BlueprintType
* Stores if the user is wearing the HMD or not. For HMDs without a sensor to detect the user wearing it, the state defaults to Unknown.
*/
UENUM(BlueprintType)
enum class EBPHMDWornState : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	Worn UMETA(DisplayName = "Worn"),
	NotWorn UMETA(DisplayName = "Not Worn"),
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
class VREXPANSIONPLUGIN_API UVRExpansionFunctionLibrary : public UActorComponent//UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_UCLASS_BODY()
	~UVRExpansionFunctionLibrary();
public:

	void* OpenVRDLLHandle;

	//@todo steamvr: Remove GetProcAddress() workaround once we have updated to Steamworks 1.33 or higher
//	pVRInit VRInitFn;
	//pVRShutdown VRShutdownFn;
	//pVRIsHmdPresent VRIsHmdPresentFn;
	//pVRGetStringForHmdError VRGetStringForHmdErrorFn;


#if STEAMVR_SUPPORTED_PLATFORM
	pVRGetGenericInterface VRGetGenericInterfaceFn;
	//vr::IVRChaperone* VRChaperone;
#endif

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

	// Opens the handles for the library
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true"))
	bool OpenVRHandles();

	// Closes the handles for the library
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true"))
	bool CloseVRHandles();

	UPROPERTY(BlueprintReadOnly)
	bool bInitialized;

	// Gets the unwound yaw of the HMD
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetHMDPureYaw"))
	static FRotator GetHMDPureYaw(FRotator HMDRotation);

	FORCEINLINE static FRotator GetHMDPureYaw_I(FRotator HMDRotation)
	{
		// Took this from UnityVRToolkit, no shame, I liked it
		FRotationMatrix HeadMat(HMDRotation);
		FVector forward = HeadMat.GetScaledAxis(EAxis::X);
		FVector forwardLeveled = forward;
		forwardLeveled.Z = 0;
		forwardLeveled.Normalize();
		FVector mixedInLocalForward = HeadMat.GetScaledAxis(EAxis::Z);

		if (forward.Z > 0)
		{
			mixedInLocalForward = -mixedInLocalForward;
		}

		mixedInLocalForward.Z = 0;
		mixedInLocalForward.Normalize();
		float dot = FMath::Clamp(FVector::DotProduct(forwardLeveled, forward), 0.0f, 1.0f);
		FVector finalForward = FMath::Lerp(mixedInLocalForward, forwardLeveled, dot * dot);

		return FRotationMatrix::MakeFromXZ(finalForward, FVector::UpVector).Rotator();
	}


	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetIsHMDConnected"))
	static bool GetIsHMDConnected();

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetIsHMDWorn"))
	static EBPHMDWornState GetIsHMDWorn();

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetHMDType"))
	static EBPHMDDeviceType GetHMDType();

	// Gets whether the game is running in VRPreview or is a non editor build game (returns true for either).
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "IsInVREditorPreviewOrGame"))
	static bool IsInVREditorPreviewOrGame();

	/**
	* Finds the minimum area rectangle that encloses all of the points in InVerts
	* Engine default version is server only for some reason
	* Uses algorithm found in http://www.geometrictools.com/Documentation/MinimumAreaRectangle.pdf
	*
	* @param		InVerts	- Points to enclose in the rectangle
	* @outparam	OutRectCenter - Center of the enclosing rectangle
	* @outparam	OutRectSideA - Vector oriented and sized to represent one edge of the enclosing rectangle, orthogonal to OutRectSideB
	* @outparam	OutRectSideB - Vector oriented and sized to represent one edge of the enclosing rectangle, orthogonal to OutRectSideA
	*/
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (WorldContext = "WorldContextObject", CallableWithoutWorldContext))
	static void NonAuthorityMinimumAreaRectangle(UObject* WorldContextObject, const TArray<FVector>& InVerts, const FVector& SampleSurfaceNormal, FVector& OutRectCenter, FRotator& OutRectRotation, float& OutSideLengthX, float& OutSideLengthY, bool bDebugDraw = false);


	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetIsActorMovable"))
	static bool GetIsActorMovable(AActor * ActorToCheck);

	// Gets if an actors root component contains a grip slot within range
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (bIgnoreSelf = "true", DisplayName = "GetGripSlotInRangeByTypeName"))
	static void GetGripSlotInRangeByTypeName(FName SlotType, AActor * Actor, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform);

	// Gets if an actors root component contains a grip slot within range
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (bIgnoreSelf = "true", DisplayName = "GetGripSlotInRangeByTypeName_Component"))
	static void GetGripSlotInRangeByTypeName_Component(FName SlotType, UPrimitiveComponent * Component, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform);

	// Gets the model / texture of a SteamVR Device, can use to fill procedural mesh components or just get the texture of them to apply to a pre-made model.
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", DisplayName = "GetVRDeviceModelAndTexture", ExpandEnumAsExecs = "Result"))
	UTexture2D * GetVRDeviceModelAndTexture(UObject* WorldContextObject, EBPSteamVRTrackedDeviceType DeviceType, TArray<UProceduralMeshComponent *> ProceduralMeshComponentsToFill, bool bCreateCollision, EAsyncBlueprintResultSwitch &Result/*, TArray<uint8> & OutRawTexture, bool bReturnRawTexture = false*/);
	
	// Gets a String device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyString"))
	bool GetVRDevicePropertyString(EVRDeviceProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue);

	// Gets a Bool device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyBool"))
	bool GetVRDevicePropertyBool(EVRDeviceProperty_Bool PropertyToRetrieve, int32 DeviceID, bool & BoolValue);

	// Gets a Float device property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRDevicePropertyFloat"))
	bool GetVRDevicePropertyFloat(EVRDeviceProperty_Float PropertyToRetrieve, int32 DeviceID, float & FloatValue);

	// Gets a String controller property
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", DisplayName = "GetVRControllerPropertyString"))
	bool GetVRControllerPropertyString(EVRControllerProperty_String PropertyToRetrieve, int32 DeviceID, FString & StringValue);


	/* Returns true if the values are equal (A == B) */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Equal VR Grip", CompactNodeTitle = "==", Keywords = "== equal"), Category = "VRExpansionFunctions")
	static bool EqualEqual_FBPActorGripInformation(const FBPActorGripInformation &A, const FBPActorGripInformation &B);
};	
