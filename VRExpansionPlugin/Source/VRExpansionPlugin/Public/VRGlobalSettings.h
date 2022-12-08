#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/InputSettings.h"
#include "VRBPDatatypes.h"
#include "Curves/CurveFloat.h"
#include "GripScripts/GS_Melee.h"
#include "GripScripts/GS_GunTools.h"
#include "VRGlobalSettings.generated.h"

class UGrippableSkeletalMeshComponent;

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

	// Set scaler values
	void SetScalers();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// The skeletal mesh component class to use for grippable characters
	// If you set this to none it will fall back to the default grippable class so that it doesn't brick your project
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "Misc")
		TSubclassOf<class UGrippableSkeletalMeshComponent> DefaultGrippableCharacterMeshComponentClass;

	// Using a getter to stay safe from bricking peoples projects if they set it to none somehow
	static TSubclassOf<class UGrippableSkeletalMeshComponent> GetDefaultGrippableCharacterMeshComponentClass();

	// If true we will use contact modification for the collision ignore subsystem
	// Its more expensive but works with non simulating pairs
	// #WARNING: Don't use yet EXPERIMENTAL
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|CollisionIgnore")
		bool bUseCollisionModificationForCollisionIgnore;

	// Number of updates a second to use for the collision cleanup checks
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|CollisionIgnore")
		float CollisionIgnoreSubsystemUpdateRate;

	// Whether we should use the physx to chaos translation scalers or not
	// This should be off on native chaos projects that have been set with the correct stiffness and damping settings already
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics")
		bool bUseChaosTranslationScalers; 

	// If true we will also set the engines chaos scalers as well to equal our overrides
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics")
		bool bSetEngineChaosScalers;

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

	// Hard joint stiffness
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|Constraints")
		float JointStiffness;

	// A scaler to apply to constraints when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|Constraints")
		float SoftLinearStiffnessScale;

	// A scaler to apply to constraints when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|Constraints")
		float SoftLinearDampingScale;

	// A scaler to apply to constraints when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|Constraints")
		float SoftAngularStiffnessScale;

	// A scaler to apply to constraints when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|Constraints")
		float SoftAngularDampingScale;

	// A scaler to apply to constraints when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|Constraints")
		float JointLinearBreakScale;

	// A scaler to apply to constraints when chaos is active
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "ChaosPhysics|Constraints")
		float JointAngularBreakScale;

	// If we should lerp hybrid with sweep grips out of collision
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "HybridWithSweepLerp")
		bool bLerpHybridWithSweepGrips;

	// If true we only lerp the rotation of hybrid grips
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "HybridWithSweepLerp")
		bool bOnlyLerpHybridRotation;

	// If true we calculate a speed off of the lerp duration and TInterp using it
	// The Hybrid with sweep lerp duration then is used to divide 1.0f (x10) to get the speed (lower duration = faster movement)
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "HybridWithSweepLerp")
		bool bHybridWithSweepUseDistanceBasedLerp;

	// Duration that the typical lerp takes, for the distance based lerp this is turned into a speed value based off of 1.0f / it
	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "HybridWithSweepLerp")
		float HybridWithSweepLerpDuration;

	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		bool bUseGlobalLerpToHand;

	UPROPERTY(config, BlueprintReadWrite, EditAnywhere, Category = "GlobalLerpToHand")
		bool bSkipLerpToHandIfHeld;

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
