// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "VRGripInterface.h"
#include "Interactibles/VRInteractibleFunctionLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "VRLeverComponent.generated.h"

class UGripMotionControllerComponent;

UENUM(Blueprintable)
enum class EVRInteractibleLeverAxis : uint8
{
	/* Rotates only towards the X Axis */
	Axis_X,
	/* Rotates only towards the Y Axis */
	Axis_Y,
	/* Rotates only towards the Z Axis */
	Axis_Z,
	/* Rotates freely on the XY Axis' */
	Axis_XY,
	/* Acts like a flight stick, with AllCurrentLeverAngles being the positive / negative of the current full angle (yaw based on initial grip delta) */
	FlightStick_XY,
};

UENUM(Blueprintable)
enum class EVRInteractibleLeverEventType : uint8
{
	LeverPositive,
	LeverNegative
};

UENUM(Blueprintable)
enum class EVRInteractibleLeverReturnType : uint8
{
	/** Stays in place on drop */
	Stay,

	/** Returns to zero on drop (lerps) */
	ReturnToZero,

	/** Lerps to closest max (only works with X/Y/Z axis levers) */
	LerpToMax,

	/** Lerps to closest max if over the toggle threshold (only works with X/Y/Z axis levers) */
	LerpToMaxIfOverThreshold,

	/** Retains momentum on release (only works with X/Y/Z axis levers) */
	RetainMomentum
};

/** Delegate for notification when the lever state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FVRLeverStateChangedSignature, bool, LeverStatus, EVRInteractibleLeverEventType, LeverStatusType, float, LeverAngleAtTime, float, FullLeverAngleAtTime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRLeverFinishedLerpingSignature, float, FinalAngle);

/**
* A Lever component, can act like a lever, door, wheel, joystick.
* If set to replicates it will replicate its values to the clients.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRLeverComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UVRLeverComponent(const FObjectInitializer& ObjectInitializer);


	~UVRLeverComponent();

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRLeverComponent")
		FVRLeverStateChangedSignature OnLeverStateChanged;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Lever State Changed"))
		void ReceiveLeverStateChanged(bool LeverStatus, EVRInteractibleLeverEventType LeverStatusType, float LeverAngleAtTime, float FullLeverAngleAttime);

	UPROPERTY(BlueprintAssignable, Category = "VRLeverComponent")
		FVRLeverFinishedLerpingSignature OnLeverFinishedLerping;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Lever Finished Lerping"))
		void ReceiveLeverFinishedLerping(float LeverFinalAngle);

	// Primary axis angle only
	UPROPERTY(BlueprintReadOnly, Category = "VRLeverComponent")
		float CurrentLeverAngle;

	// Writes out all current angles to this rotator, useful mostly for XY and Flight stick modes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		FRotator AllCurrentLeverAngles;

	// Bearing Direction, for X/Y is their signed direction, for XY mode it is an actual 2D directional vector
	UPROPERTY(BlueprintReadOnly, Category = "VRLeverComponent")
		FVector CurrentLeverForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		bool bUngripAtTargetRotation;

	// Rotation axis to use, XY is combined X and Y, only LerpToZero and PositiveLimits work with this mode
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		EVRInteractibleLeverAxis LeverRotationAxis;

	// The percentage of the angle at witch the lever will toggle
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent", meta = (ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.01", UIMax = "1.0"))
		float LeverTogglePercentage;

	// The max angle of the lever in the positive direction
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent", meta = (ClampMin = "0.0", ClampMax = "179.9", UIMin = "0.0", UIMax = "180.0"))
		float LeverLimitPositive;

	// The max angle of the lever in the negative direction
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent", meta = (ClampMin = "0.0", ClampMax = "179.9", UIMin = "0.0", UIMax = "180.0"))
		float LeverLimitNegative;

	// The max angle of the flightsticks yaw in either direction off of 0
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent|Flight Stick Settings", meta = (ClampMin = "0.0", ClampMax = "179.9", UIMin = "0.0", UIMax = "180.0"))
		float FlightStickYawLimit;

	// If true then this lever is locked in place until unlocked again
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		bool bIsLocked;

	// If true then this lever will auto drop even when locked
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		bool bAutoDropWhenLocked;

	// Sets if the lever is locked or not
	UFUNCTION(BlueprintCallable, Category = "GripSettings")
		void SetIsLocked(bool bNewLockedState);

	// Checks and applies auto drop if we need too
	bool CheckAutoDrop(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		EVRInteractibleLeverReturnType LeverReturnTypeWhenReleased;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		bool bSendLeverEventsDuringLerp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		float LeverReturnSpeed;

	// If true then we will blend the values of the XY axis' by the AngleThreshold, lowering Pitch/Yaw influence based on how far from leaning into the axis that the lever is
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent|Flight Stick Settings")
		bool bBlendAxisValuesByAngleThreshold;

	// The angle threshold to blend around, default of 90.0 blends 0.0 to 1.0 smoothly across entire sweep
	// A value of 45 would blend it to 0 halfway rotated to the other axis, while 180 would still leave half the influence when fully rotated out of facing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent|Flight Stick Settings", meta = (ClampMin = "1.0", ClampMax = "360.0", UIMin = "1.0", UIMax = "360.0"))
		float AngleThreshold;

	// Number of frames to average momentum across for the release momentum (avoids quick waggles)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent|Momentum Settings", meta = (ClampMin = "0", ClampMax = "12", UIMin = "0", UIMax = "12"))
		int FramesToAverage;

	// Units in degrees per second to slow a momentum lerp down
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent|Momentum Settings", meta = (ClampMin = "0.0", ClampMax = "180", UIMin = "0.0", UIMax = "180.0"))
		float LeverMomentumFriction;

	// % of elasticity on reaching the end 0 - 1.0 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent|Momentum Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float LeverRestitution;

	// Maximum momentum of the lever in degrees per second
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent|Momentum Settings", meta = (ClampMin = "0.0", UIMin = "0.0"))
		float MaxLeverMomentum;

	UPROPERTY(BlueprintReadWrite, Category = "VRLeverComponent")
		bool bIsLerping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float PrimarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float SecondarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		int GripPriority;

	// Sets the grip priority
	UFUNCTION(BlueprintCallable, Category = "GripSettings")
		void SetGripPriority(int NewGripPriority);

	// Full precision current angle
	float FullCurrentAngle;

	float LastDeltaAngle;

	// Resetting the initial transform here so that it comes in prior to BeginPlay and save loading.
	virtual void OnRegister() override;

protected:
	// Now replicating this so that it works correctly over the network
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InitialRelativeTransform, Category = "VRLeverComponent")
	FTransform_NetQuantize InitialRelativeTransform;
public:
	// Gets the initial relative transform, if you want to set it you should be using ResetInitialButtonLocation
	FTransform GetInitialRelativeTransform() { return InitialRelativeTransform; }

	UFUNCTION()
	virtual void OnRep_InitialRelativeTransform()
	{
		ReCalculateCurrentAngle();
	}

	// Flight stick variables
	FTransform InteractorOffsetTransform;

	FVector InitialInteractorLocation;
	FVector InitialInteractorDropLocation;
	float InitialGripRot;
	float RotAtGrab;
	FQuat qRotAtGrab;
	bool bLeverState;
	bool bIsInFirstTick;

	// For momentum retention
	float MomentumAtDrop;
	float LastLeverAngle;

	float CalcAngle(EVRInteractibleLeverAxis AxisToCalc, FVector CurInteractorLocation, bool bSkipLimits = false);

	void LerpAxis(float CurrentAngle, float DeltaTime);

	void CalculateCurrentAngle(FTransform & CurrentTransform);

	// ------------------------------------------------
	// Gameplay tag interface
	// ------------------------------------------------

	/** Overridden to return requirements tags */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
	{
		TagContainer = GameplayTags;
	}

protected:
	/** Tags that are set on this object */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "GameplayTags")
		FGameplayTagContainer GameplayTags;
	// End Gameplay Tag Interface

	// Requires bReplicates to be true for the component
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		bool bRepGameplayTags;
		
	// Overrides the default of : true and allows for controlling it like in an actor, should be default of off normally with grippable components
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
		bool bReplicateMovement;
public:
	FGameplayTagContainer& GetGameplayTags();
	bool GetRepGameplayTags() { return bRepGameplayTags; }
	void SetRepGameplayTags(bool bNewRepGameplayTags);
	bool GetReplicateMovement() { return bReplicateMovement; }
	void SetReplicateMovement(bool bNewReplicateMovement);

	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripMovementReplicationSettings MovementReplicationSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float Stiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float Damping;

	// Distance before the object will break out of the hand, 0.0f == never will
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float BreakDistance;

	// Should we deny gripping on this object
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface", meta = (ScriptName = "IsDenyGripping"))
		bool bDenyGripping;

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface", meta = (ScriptName = "IsCurrentlyHeld"))
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		FBPGripPair HoldingGrip; // Set on grip notify, not net serializing
	bool bOriginalReplicatesMovement;

	TWeakObjectPtr<USceneComponent> ParentComponent;

	// Should be called after the lever is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		void ResetInitialLeverLocation(bool bAllowThrowingEvents = true);

	/**
	 *    Sets the angle of the lever forcefully
	 *    @param NewAngle				The new angle to use, for single axis levers the sign of the angle will be used
	 *    @param DualAxisForwardVector	Only used with dual axis levers, you need to define the forward axis for the angle to apply too
	*/
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		void SetLeverAngle(float NewAngle, FVector DualAxisForwardVector, bool bAllowThrowingEvents = true);

	// ReCalculates the current angle, sets it on the back end, and returns it
	// If allow throwing events is true then it will trigger the callbacks for state changes as well
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		float ReCalculateCurrentAngle(bool bAllowThrowingEvents = true);

	void ProccessCurrentState(bool bWasLerping = false, bool bThrowEvents = true, bool bCheckAutoDrop = true);

	virtual void OnUnregister() override;

	// Called when a object is gripped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnGripped;

	// Called when a object is dropped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnDropSignature OnDropped;

	// Grip interface setup

	// Set up as deny instead of allow so that default allows for gripping
	// The GripInitiator is not guaranteed to be valid, check it for validity
	bool DenyGripping_Implementation(UGripMotionControllerComponent * GripInitiator = nullptr) override;

	// How an interfaced object behaves when teleporting
	EGripInterfaceTeleportBehavior TeleportBehavior_Implementation() override;

	// Should this object simulate on drop
	bool SimulateOnDrop_Implementation() override;

	// Grip type to use
	EGripCollisionType GetPrimaryGripType_Implementation(bool bIsSlot) override;

	// Secondary grip type
	ESecondaryGripType SecondaryGripType_Implementation() override;

	// Define which movement repliation setting to use
	EGripMovementReplicationSettings GripMovementReplicationType_Implementation() override;

	// Define the late update setting
	EGripLateUpdateSettings GripLateUpdateSetting_Implementation() override;

	// What grip stiffness and damping to use if using a physics constraint
	void GetGripStiffnessAndDamping_Implementation(float& GripStiffnessOut, float& GripDampingOut) override;

	// Get the advanced physics settings for this grip
	FBPAdvGripSettings AdvancedGripSettings_Implementation() override;

	// What distance to break a grip at (only relevent with physics enabled grips
	float GripBreakDistance_Implementation() override;

	// Get grip slot in range
	void ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool& bHadSlotInRange, FTransform& SlotWorldTransform, FName& SlotName, UGripMotionControllerComponent* CallingController = nullptr, FName OverridePrefix = NAME_None) override;

	// Check if an object allows multiple grips at one time
	bool AllowsMultipleGrips_Implementation() override;

	// Returns if the object is held and if so, which controllers are holding it
	void IsHeld_Implementation(TArray<FBPGripPair>& CurHoldingControllers, bool& bCurIsHeld) override;

	// Interface function used to throw the delegates that is invisible to blueprints so that it can't be overridden
	virtual void Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	// Sets is held, used by the plugin
	void SetHeld_Implementation(UGripMotionControllerComponent* NewHoldingController, uint8 GripID, bool bNewIsHeld) override;

	// Returns if the object wants to be socketed
	bool RequestsSocketing_Implementation(USceneComponent*& ParentToSocketTo, FName& OptionalSocketName, FTransform_NetQuantize& RelativeTransform) override;

	// Get grip scripts
	bool GetGripScripts_Implementation(TArray<UVRGripScriptBase*>& ArrayReference) override;


	// Events //

	// Event triggered each tick on the interfaced object when gripped, can be used for custom movement or grip based logic
	void TickGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation, float DeltaTime) override;

	// Event triggered on the interfaced object when gripped
	void OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) override;

	// Event triggered on the interfaced object when grip is released
	void OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	// Event triggered on the interfaced object when child component is gripped
	void OnChildGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) override;

	// Event triggered on the interfaced object when child component is released
	void OnChildGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	// Event triggered on the interfaced object when secondary gripped
	void OnSecondaryGrip_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation) override;

	// Event triggered on the interfaced object when secondary grip is released
	void OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* ReleasingSecondaryGripComponent, const FBPActorGripInformation& GripInformation) override;

	// Interaction Functions

	// Call to use an object
	void OnUsed_Implementation() override;

	// Call to stop using an object
	void OnEndUsed_Implementation() override;

	// Call to use an object
	void OnSecondaryUsed_Implementation() override;

	// Call to stop using an object
	void OnEndSecondaryUsed_Implementation() override;

	// Call to send an action event to the object
	void OnInput_Implementation(FKey Key, EInputEvent KeyEvent) override;

	protected:

};