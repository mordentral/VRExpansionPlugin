// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "MotionControllerComponent.h"
#include "VRGripInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "VRInteractibleFunctionLibrary.h"
#include "Components/StaticMeshComponent.h"


#include "VRDialComponent.generated.h"


/** Delegate for notification when the lever state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRDialStateChangedSignature, float, DialMilestoneAngle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRDialFinishedLerpingSignature);

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRDialComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UVRDialComponent(const FObjectInitializer& ObjectInitializer);


	~UVRDialComponent();

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRDialComponent")
		FVRDialStateChangedSignature OnDialHitSnapAngle;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Dial Hit Snap Angle"))
		void ReceiveDialHitSnapAngle(float DialMilestoneAngle);

	// If true the dial will lerp back to zero on release
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent|Lerping")
		bool bLerpBackOnRelease;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent|Lerping")
		bool bSendDialEventsDuringLerp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent|Lerping")
		float DialReturnSpeed;

	UPROPERTY(BlueprintReadOnly, Category = "VRDialComponent|Lerping")
		bool bIsLerping;

	UPROPERTY(BlueprintAssignable, Category = "VRDialComponent|Lerping")
		FVRDialFinishedLerpingSignature OnDialFinishedLerping;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Dial Finished Lerping"))
		void ReceiveDialFinishedLerping();

	UPROPERTY(BlueprintReadOnly, Category = "VRDialComponent")
	float CurrentDialAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")//, meta = (ClampMin = "0.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0"))
	float ClockwiseMaximumDialAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")//, meta = (ClampMin = "0.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0"))
	float CClockwiseMaximumDialAngle;

	// If true then the dial can "roll over" past 360/0 degrees in a direction
	// Allowing unlimited dial angle values
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
		bool bUseRollover;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	bool bDialUsesAngleSnap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	bool bDialUseSnapAngleList;

	// Optional list of snap angles for the dial
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (editcondition = "bDialUseSnapAngleList"))
		TArray<float> DialSnapAngleList;

	// Angle that the dial snaps to on release and when within the threshold distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (editcondition = "!bDialUseSnapAngleList", ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float SnapAngleIncrement;

	// Threshold distance that when within the dial will stay snapped to its closest snap increment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (editcondition = "!bDialUseSnapAngleList", ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float SnapAngleThreshold;

	// Scales rotational input to speed up or slow down the rotation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float RotationScaler;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	EVRInteractibleAxis DialRotationAxis;

	// If true then the dial will directly sample the hands rotation instead of using its movement around it.
	// This is good for roll specific dials but is fairly bad elsewhere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
		bool bDialUseDirectHandRotation;

	float LastGripRot;
	float InitialGripRot;
	float InitialRotBackEnd;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (editcondition = "bDialUseDirectHandRotation"))
	EVRInteractibleAxis InteractorRotationAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float PrimarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float SecondarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		int GripPriority;

	// Sets the grip priority
	UFUNCTION(BlueprintCallable, Category = "GripSettings")
		void SetGripPriority(int NewGripPriority);

	// Resetting the initial transform here so that it comes in prior to BeginPlay and save loading.
	virtual void OnRegister() override;

	// Now replicating this so that it works correctly over the network
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InitialRelativeTransform, Category = "VRDialComponent")
		FTransform_NetQuantize InitialRelativeTransform;

	UFUNCTION()
		virtual void OnRep_InitialRelativeTransform()
	{
		CalculateDialProgress();
	}

	void CalculateDialProgress();

	//FTransform InitialRelativeTransform;
	FVector InitialInteractorLocation;
	FVector InitialDropLocation;
	float CurRotBackEnd;
	FRotator LastRotation;
	float LastSnapAngle;

	// Should be called after the dial is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		void ResetInitialDialLocation();

	// Can be called to recalculate the dial angle after you move it if you want different values
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		void AddDialAngle(float DialAngleDelta, bool bCallEvents = false, bool bSkipSettingRot = false);

	// Directly sets the dial angle, still obeys maximum limits and snapping though
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		void SetDialAngle(float DialAngle, bool bCallEvents = false);

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

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface", meta = (ScriptName = "IsCurrentlyHeld"))
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		FBPGripPair HoldingGrip; // Set on grip notify, not net serializing
	bool bOriginalReplicatesMovement;

	// Called when a object is gripped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnGripped;

	// Called when a object is dropped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnDropSignature OnDropped;
	
	// Distance before the object will break out of the hand, 0.0f == never will
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float BreakDistance;

	// Should we deny gripping on this object
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface", meta = (ScriptName = "IsDenyGripping"))
		bool bDenyGripping;

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
		void GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut) override;

	// Get the advanced physics settings for this grip
		FBPAdvGripSettings AdvancedGripSettings_Implementation() override;

	// What distance to break a grip at (only relevent with physics enabled grips
		float GripBreakDistance_Implementation() override;

	// Get grip slot in range
		void ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName,  UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None) override;

	// Check if an object allows multiple grips at one time
		bool AllowsMultipleGrips_Implementation() override;

	// Returns if the object is held and if so, which controllers are holding it
		void IsHeld_Implementation(TArray<FBPGripPair>& CurHoldingControllers, bool & bCurIsHeld) override;

	// Interface function used to throw the delegates that is invisible to blueprints so that it can't be overridden
		virtual void Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	// Sets is held, used by the plugin
		void SetHeld_Implementation(UGripMotionControllerComponent * NewHoldingController, uint8 GripID, bool bNewIsHeld) override;

	// Returns if the object wants to be socketed
		bool RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) override;

	// Get grip scripts
		bool GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference) override;


	// Events //

	// Event triggered each tick on the interfaced object when gripped, can be used for custom movement or grip based logic
		void TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) override;

	// Event triggered on the interfaced object when gripped
		void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;

	// Event triggered on the interfaced object when grip is released
		void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false) override;

	// Event triggered on the interfaced object when child component is gripped
		void OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;

	// Event triggered on the interfaced object when child component is released
		void OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false) override;

	// Event triggered on the interfaced object when secondary gripped
		void OnSecondaryGrip_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) override;

	// Event triggered on the interfaced object when secondary grip is released
		void OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * GripOwningController, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) override;

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