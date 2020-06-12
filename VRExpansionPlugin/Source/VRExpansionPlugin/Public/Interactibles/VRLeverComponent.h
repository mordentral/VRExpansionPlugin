// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "MotionControllerComponent.h"
#include "VRGripInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "Interactibles/VRInteractibleFunctionLibrary.h"
#include "VRExpansionFunctionLibrary.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Components/StaticMeshComponent.h"

#include "PhysicsPublic.h"

#if WITH_PHYSX
#include "PhysXPublic.h"
#endif // WITH_PHYSX

#include "VRLeverComponent.generated.h"


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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FVRLeverStateChangedSignature, bool, LeverStatus, EVRInteractibleLeverEventType, LeverStatusType, float, LeverAngleAtTime);
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
		void ReceiveLeverStateChanged(bool LeverStatus, EVRInteractibleLeverEventType LeverStatusType, float LeverAngleAtTime);

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
		bool bIsPhysicsLever;

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

	UPROPERTY(BlueprintReadOnly, Category = "VRLeverComponent")
		bool bIsLerping;

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

	// Now replicating this so that it works correctly over the network
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InitialRelativeTransform, Category = "VRLeverComponent")
	FTransform_NetQuantize InitialRelativeTransform;

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

	/** Tags that are set on this object */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "GameplayTags")
		FGameplayTagContainer GameplayTags;

	// End Gameplay Tag Interface

	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	

	// Requires bReplicates to be true for the component
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		bool bRepGameplayTags;
		
	// Overrides the default of : true and allows for controlling it like in an actor, should be default of off normally with grippable components
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
		bool bReplicateMovement;

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
		void ResetInitialLeverLocation();

	/**
	 *    Sets the angle of the lever forcefully
	 *    @param NewAngle				The new angle to use, for single axis levers the sign of the angle will be used
	 *    @param DualAxisForwardVector	Only used with dual axis levers, you need to define the forward axis for the angle to apply too
	*/
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		void SetLeverAngle(float NewAngle, FVector DualAxisForwardVector);

	// ReCalculates the current angle, sets it on the back end, and returns it
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		float ReCalculateCurrentAngle();

	virtual void OnUnregister() override;;

	// Called when a object is gripped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnGripped;

	// Called when a object is dropped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnDropSignature OnDropped;

#if WITH_PHYSX
	physx::PxD6Joint* HandleData;
	//int32 SceneIndex;
#endif

	bool DestroyConstraint();
	bool SetupConstraint();


	// Grip interface setup

	// Set up as deny instead of allow so that default allows for gripping
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface", meta = (DisplayName = "IsDenyingGrips"))
		bool DenyGripping();

	// How an interfaced object behaves when teleporting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripInterfaceTeleportBehavior TeleportBehavior();

	// Should this object simulate on drop
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool SimulateOnDrop();

	// Secondary grip type
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		ESecondaryGripType SecondaryGripType();

		// Grip type to use
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType GetPrimaryGripType(bool bIsSlot);

	// Define which movement repliation setting to use
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripMovementReplicationSettings GripMovementReplicationType();

	// Define the late update setting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripLateUpdateSettings GripLateUpdateSetting();

		// What grip stiffness and damping to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void GetGripStiffnessAndDamping(float &GripStiffnessOut, float &GripDampingOut);

	// Get the advanced physics settings for this grip
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		FBPAdvGripSettings AdvancedGripSettings();

	// What distance to break a grip at (only relevent with physics enabled grips
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripBreakDistance();

	// Get grip primary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestGripSlotInRange(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName,  UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);

	// Check if an object allows multiple grips at one time
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool AllowsMultipleGrips();

	// Returns if the object is held and if so, which controllers are holding it
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void IsHeld(TArray<FBPGripPair>& CurHoldingControllers, bool & bCurIsHeld);

	// Sets is held, used by the plugin
	UFUNCTION(BlueprintNativeEvent, /*BlueprintCallable,*/ Category = "VRGripInterface")
		void SetHeld(UGripMotionControllerComponent * NewHoldingController, uint8 GripID, bool bNewIsHeld);

	// Returns if the object wants to be socketed
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool RequestsSocketing(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform);

	// Get grip scripts
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool GetGripScripts(TArray<UVRGripScriptBase*> & ArrayReference);

	// Events //

	// Event triggered each tick on the interfaced object when gripped, can be used for custom movement or grip based logic
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void TickGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime);

	// Event triggered on the interfaced object when gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);

	// Event triggered on the interfaced object when child component is gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);

	// Event triggered on the interfaced object when secondary gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnSecondaryGrip(UGripMotionControllerComponent * GripOwningController, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnSecondaryGripRelease(UGripMotionControllerComponent * GripOwningController, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Interaction Functions

	// Call to use an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnUsed();

	// Call to stop using an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnEndUsed();

	// Call to use an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnSecondaryUsed();

	// Call to stop using an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnEndSecondaryUsed();

	// Call to send an action event to the object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnInput(FKey Key, EInputEvent KeyEvent);

	protected:

};