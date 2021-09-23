#pragma once

#include "GameFramework/PlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "VRBPDatatypes.h"
#include "GripScripts/GS_Melee.h"
#include "GripScripts/GS_GunTools.h"
#include "VRGlobalSettings.generated.h"


/*namespace ControllerProfileStatics
{
	static const FTransform OculusTouchStaticOffset(FRotator(-70.f, 0.f, 0.f));
}*/

// As of 4.24 these do nothing, they are left for a few versions as a reference of old bindings
// #TODO: Delete around 4.26
/*USTRUCT(BlueprintType, Category = "VRGlobalSettings")
struct FAxisMappingDetails
{
	GENERATED_BODY()
public:

	// List of all axis key mappings that correspond to the axis name in the containing map 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	TArray<FInputAxisKeyMapping> AxisMappings;

};

// As of 4.24 these do nothing, they are left for a few versions as a reference of old bindings
// #TODO: Delete around 4.26
USTRUCT(BlueprintType, Category = "VRGlobalSettings")
struct FActionMappingDetails
{
	GENERATED_BODY()
public:

	// List of all axis key mappings that correspond to the axis name in the containing map 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
		TArray<FInputActionKeyMapping> ActionMappings;
};*/

USTRUCT(BlueprintType, Category = "ControllerProfiles")
struct VREXPANSIONPLUGIN_API FBPVRControllerProfile
{
	GENERATED_BODY()
public:

	// Name of controller profile
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles")
		FName ControllerName;

	// Offset to use with this controller
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles")
		FTransform_NetQuantize SocketOffsetTransform;

	// Offset to use with this controller
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles")
		bool bUseSeperateHandOffsetTransforms;

	// Offset to use with this controller
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ControllerProfiles", meta = (editcondition = "bUseSeperateHandOffsetTransforms"))
		FTransform_NetQuantize SocketOffsetTransformRightHand;

	// Setting an axis value here with key bindings will override the equivalent bindings on profile load
	// As of 4.24 these do nothing, they are left for a few versions as a reference of old bindings
	// #TODO: Delete around 4.26
	/*UPROPERTY(EditDefaultsOnly, NotReplicated, Category = "ControllerProfiles")
	TMap<FName, FAxisMappingDetails> AxisOverrides;

	// Setting action mappings here will override the equivalent bindings on profile load
		// As of 4.24 these do nothing, they are left for a few versions as a reference of old bindings
	// #TODO: Delete around 4.26
	UPROPERTY(EditDefaultsOnly, NotReplicated, Category = "ControllerProfiles")
	TMap<FName, FActionMappingDetails> ActionOverrides;*/


	FBPVRControllerProfile() :
		ControllerName(NAME_None),
		SocketOffsetTransform(FTransform::Identity),
		bUseSeperateHandOffsetTransforms(false),
		SocketOffsetTransformRightHand(FTransform::Identity)
	{}

	FBPVRControllerProfile(FName ControllerName) :
		ControllerName(ControllerName),
		SocketOffsetTransform(FTransform::Identity),		
		bUseSeperateHandOffsetTransforms(false),
		SocketOffsetTransformRightHand(FTransform::Identity)
	{}

	FBPVRControllerProfile(FName ControllerNameIn, const FTransform & Offset) :
		ControllerName(ControllerNameIn),
		SocketOffsetTransform(Offset),
		bUseSeperateHandOffsetTransforms(false),
		SocketOffsetTransformRightHand(FTransform::Identity)
	{}

	FBPVRControllerProfile(FName ControllerNameIn, const FTransform & Offset, const FTransform & OffsetRight) :
		ControllerName(ControllerNameIn),
		SocketOffsetTransform(Offset),
		bUseSeperateHandOffsetTransforms(true),
		SocketOffsetTransformRightHand(OffsetRight)
	{}

	FORCEINLINE bool operator==(const FBPVRControllerProfile &Other) const
	{
		return this->ControllerName == Other.ControllerName;
	}
};

UCLASS(config = Engine, defaultconfig)
class VREXPANSIONPLUGIN_API UVRGlobalSettings : public UObject
{
	GENERATED_BODY()

public:
	UVRGlobalSettings(const FObjectInitializer& ObjectInitializer);

	// A scaler to apply to constraint drives when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics")
		float LinearDriveStiffnessScale;

	// A scaler to apply to constraint drives when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics")
		float LinearDriveDampingScale;

	// A scaler to apply to constraint drives when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics")
		float AngularDriveStiffnessScale;

	// A scaler to apply to constraint drives when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics")
		float AngularDriveDampingScale;


	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		bool bUseGlobalLerpToHand;

	// If the initial grip distance is closer than this value then the lerping will not be performed.
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		float MinDistanceForLerp;

	// How many seconds the lerp should take
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		float LerpDuration;

	// The minimum speed (in UU per second) that that the lerp should have across the initial grip distance
	// Will speed the LerpSpeed up to try and maintain this initial speed if required
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		float MinSpeedForLerp;

	// The maximum speed (in UU per second) that the lerp should have across the initial grip distance
	// Will slow the LerpSpeed down to try and maintain this initial speed if required
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		float MaxSpeedForLerp;

	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		EVRLerpInterpolationMode LerpInterpolationMode;

	// Whether to use a curve map to map the lerp to
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand|Curve")
		bool bUseCurve;

	// The curve to follow when using a curve map, only uses from 0.0 - 1.0 of the curve timeline and maps it across the entire duration
	UPROPERTY(config, Category = "GlobalLerpToHand|Curve", EditAnywhere, meta = (editcondition = "bUseCurve"))
		FRuntimeFloatCurve OptionalCurveToFollow;

	// Alter the values of the virtual stock settings and save them out
	UFUNCTION(BlueprintPure, Category = "GlobalLerpToHand")
		static bool IsGlobalLerpEnabled();

	// How many passes CCD will take during simulation, larger values significantly increase the cost of CCD calculation but also prevent tunneling artifacts
	// Physx only
	UPROPERTY(config, EditAnywhere, Category = "Physics")
		int MaxCCDPasses;

	// List of surfaces and their properties for the melee script
	UPROPERTY(config, EditAnywhere, Category = "MeleeSettings")
		TArray<FBPHitSurfaceProperties> MeleeSurfaceSettings;

	// Default global virtual stock settings for the gun script
	UPROPERTY(config, EditAnywhere, Category = "GunSettings")
		FBPVirtualStockSettings VirtualStockSettings;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	UPROPERTY(config, EditAnywhere, Category = "GunSettings|Secondary Grip 1Euro Settings")
		float OneEuroMinCutoff;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	UPROPERTY(config, EditAnywhere, Category = "GunSettings|Secondary Grip 1Euro Settings")
		float OneEuroCutoffSlope;

	// Setting to use for the OneEuro smoothing low pass filter when double gripping something held with a hand
	UPROPERTY(config, EditAnywhere, Category = "GunSettings|Secondary Grip 1Euro Settings")
		float OneEuroDeltaCutoff;

	// Get the values of the virtual stock settings
	UFUNCTION(BlueprintCallable, Category = "MeleeSettings")
		static void GetMeleeSurfaceGlobalSettings(TArray<FBPHitSurfaceProperties>& OutMeleeSurfaceSettings);

	// Get the values of the virtual stock settings
	UFUNCTION(BlueprintCallable, Category = "GunSettings|VirtualStock")
		static void GetVirtualStockGlobalSettings(FBPVirtualStockSettings& OutVirtualStockSettings);

	// Alter the values of the virtual stock settings and save them out
	UFUNCTION(BlueprintCallable, Category = "GunSettings|VirtualStock")
		static void SaveVirtualStockGlobalSettings(FBPVirtualStockSettings NewVirtualStockSettings);


	DECLARE_MULTICAST_DELEGATE(FVRControllerProfileChangedEvent);
	/** Delegate for notification when the controller profile changes. */
	FVRControllerProfileChangedEvent OnControllerProfileChangedEvent;

	// Controller profiles to store related information on a per profile basis
	UPROPERTY(config, EditAnywhere, Category = "ControllerProfiles")
	TArray<FBPVRControllerProfile> ControllerProfiles;

	// Store these to save some processing when getting the transform after a profile is loaded
	FName CurrentControllerProfileInUse;
	FTransform CurrentControllerProfileTransform;
	bool bUseSeperateHandTransforms;
	FTransform CurrentControllerProfileTransformRight;

	// Adjust the transform of a socket for a particular controller model, if a name is not sent in, it will use the currently loaded one
	// If there is no currently loaded one, it will return the input transform as is.
	// If bIsRightHand and the target profile uses seperate hand transforms it will use the right hand transform
	UFUNCTION(BlueprintPure, Category = "VRControllerProfiles")
		static FTransform AdjustTransformByControllerProfile(FName OptionalControllerProfileName, const FTransform& SocketTransform, bool bIsRightHand = false);

	// Adjust the transform of a socket for a particular controller model
	// If there is no currently loaded one, it will return the input transform as is.
	// If bIsRightHand and the target profile uses seperate hand transforms it will use the right hand transform
	UFUNCTION(BlueprintPure, Category = "VRControllerProfiles")
		static FTransform AdjustTransformByGivenControllerProfile(UPARAM(ref) FBPVRControllerProfile& ControllerProfile, const FTransform& SocketTransform, bool bIsRightHand = false);

	// Get array of controller profiles
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static TArray<FBPVRControllerProfile> GetControllerProfiles();

	// Overwrite a controller profile
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles|Operations")
		static void OverwriteControllerProfile(UPARAM(ref)FBPVRControllerProfile& OverwritingProfile, bool bSaveOutToConfig = true);

	// Add a controller profile
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles|Operations")
		static void AddControllerProfile(UPARAM(ref)FBPVRControllerProfile& NewProfile, bool bSaveOutToConfig = true);

	// Overwrite a controller profile
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles|Operations")
		static void DeleteControllerProfile(FName ControllerProfileName, bool bSaveOutToConfig = true);

	// Saved the VRGlobalSettings out to its config file, will include any alterations that you made to the profile
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles|Operations")
		static void SaveControllerProfiles();


	// Get name of currently loaded profile (if one is loaded)
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static FName GetCurrentProfileName(bool& bHadLoadedProfile);

	// Get name of currently loaded profile (if one is loaded)
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static FBPVRControllerProfile GetCurrentProfile(bool& bHadLoadedProfile);

	// Get a controller profile by name
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static bool GetControllerProfile(FName ControllerProfileName, FBPVRControllerProfile& OutProfile);

	// Load a controller profile by name
	// Action / Axis mappings overwrite ones with the same action/axis name in the input settings.
	// If you have an action/axis override but don't assign buttons to it then it will just delete it.
	// This can be useful for removing actions and using multiple actions (IE: Grip Touch, Grip Vive actions)
	// For when just changing the buttons isn't good enough
	// If bSetDefaults is true, will set this as the currently loaded profile
	// Otherwise will just load it (useful for Left/Right handed profile additions and the like to have it false)
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static bool LoadControllerProfileByName(FName ControllerProfileName, bool bSetAsCurrentProfile = true);

	// Load a controller profile
	// Action / Axis mappings overwrite ones with the same action/axis name in the input settings.
	// If you have an action/axis override but don't assign buttons to it then it will just delete it.
	// This can be useful for removing actions and using multiple actions (IE: Grip Touch, Grip Vive actions)
	// For when just changing the buttons isn't good enough
	// If bSetDefaults is true, will set this as the currently loaded profile
	// Otherwise will just load it (useful for Left/Right handed profile additions and the like to have it false)
	// AS OF 4.25 AXIS and ACTION mappings do nothing, will be deleting around 4.26 #TODO: Delete around 4.26
	UFUNCTION(BlueprintCallable, Category = "VRControllerProfiles")
		static bool LoadControllerProfile(const FBPVRControllerProfile& ControllerProfile, bool bSetAsCurrentProfile = true);

	virtual void PostInitProperties() override;
};
