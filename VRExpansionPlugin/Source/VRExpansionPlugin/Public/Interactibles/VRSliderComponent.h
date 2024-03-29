// Fill out your copyright notice in the Description page of Project Settings.
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VRGripInterface.h"
#include "GameplayTagAssetInterface.h"
#include "Interactibles/VRInteractibleFunctionLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "VRSliderComponent.generated.h"

class UGripMotionControllerComponent;
class USplineComponent;

UENUM(Blueprintable)
enum class EVRInteractibleSliderLerpType : uint8
{
	Lerp_None,
	Lerp_Interp,
	Lerp_InterpConstantTo
};

UENUM(Blueprintable)
enum class EVRInteractibleSliderDropBehavior : uint8
{
	/** Stays in place on drop */
	Stay,

	/** Retains momentum on release*/
	RetainMomentum
};

/** Delegate for notification when the slider state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRSliderHitPointSignature, float, SliderProgressPoint);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRSliderFinishedLerpingSignature, float, FinalProgress);

/**
* A slider component, can act like a scroll bar, or gun bolt, or spline following component
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRSliderComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UVRSliderComponent(const FObjectInitializer& ObjectInitializer);


	~UVRSliderComponent();

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRSliderComponent")
		FVRSliderHitPointSignature OnSliderHitPoint;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Slider State Changed"))
		void ReceiveSliderHitPoint(float SliderProgressPoint);

	UPROPERTY(BlueprintAssignable, Category = "VRSliderComponent")
		FVRSliderFinishedLerpingSignature OnSliderFinishedLerping;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Slider Finished Lerping"))
		void ReceiveSliderFinishedLerping(float FinalProgress);

	// If true then this slider will only update in its tick event instead of normally using the controllers update event
	// Keep in mind that you then must adjust the tick group in order to make sure it happens after the gripping controller
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bUpdateInTick;
	bool bPassThrough;

	float LastSliderProgressState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	FVector MaxSlideDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	FVector MinSlideDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		EVRInteractibleSliderDropBehavior SliderBehaviorWhenReleased;

	// Number of frames to average momentum across for the release momentum (avoids quick waggles)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent|Momentum Settings", meta = (ClampMin = "0", ClampMax = "12", UIMin = "0", UIMax = "12"))
		int FramesToAverage;

	// Units in % of total length per second to slow a momentum lerp down
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent|Momentum Settings", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0"))
		float SliderMomentumFriction;

	// % of elasticity on reaching the end 0 - 1.0 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent|Momentum Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float SliderRestitution;

	// Maximum momentum of the slider in units of the total distance per second (0.0 - 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent|Momentum Settings", meta = (ClampMin = "0.0", UIMin = "0.0"))
		float MaxSliderMomentum;

	UPROPERTY(BlueprintReadOnly, Category = "VRSliderComponent")
		bool bIsLerping;

	// For momentum retention
	FVector MomentumAtDrop;
	FVector LastSliderProgress;
	float SplineMomentumAtDrop;
	float SplineLastSliderProgress;

	// Gets filled in with the current slider location progress
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRSliderComponent")
	float CurrentSliderProgress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	bool bSlideDistanceIsInParentSpace;

	// If true then this slider is locked in place until unlocked again
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bIsLocked;

	// If true then this slider will auto drop even when locked
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bAutoDropWhenLocked;

	// Sets if the slider is locked or not
	UFUNCTION(BlueprintCallable, Category = "GripSettings")
		void SetIsLocked(bool bNewLockedState);

	// Uses the legacy slider logic that doesn't ABS the min and max values
	// Retains compatibility with some older projects
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bUseLegacyLogic;

	// How far away from an event state before the slider allows throwing the same state again, default of 1.0 means it takes a full toggle
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float EventThrowThreshold;
	bool bHitEventThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float PrimarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float SecondarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
	int GripPriority;

	// Sets the grip priority
	UFUNCTION(BlueprintCallable, Category = "GripSettings")
		void SetGripPriority(int NewGripPriority);

protected:
	// Set this to assign a spline component to the slider
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Replicated/*Using = OnRep_SplineComponentToFollow*/, Category = "VRSliderComponent")
	TObjectPtr<USplineComponent> SplineComponentToFollow;
public:
	// Gets the initial relative transform, if you want to set it you should be using ResetInitialButtonLocation
	TObjectPtr<USplineComponent> GetSplineComponentToFollow() { return SplineComponentToFollow; }

	/*UFUNCTION()
	virtual void OnRep_SplineComponentToFollow()
	{
		CalculateSliderProgress();
	}*/

	// Where the slider should follow the rotation and scale of the spline as well
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	bool bFollowSplineRotationAndScale;

	// Does not allow the slider to skip past nodes on the spline, it requires it to progress from node to node
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bEnforceSplineLinearity;
	float LastInputKey;
	float LerpedKey;

	// Type of lerp to use when following a spline
	// For lerping I would suggest using ConstantTo in general as it will be the smoothest.
	// Normal Interp will change speed based on distance, that may also have its uses.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		EVRInteractibleSliderLerpType SplineLerpType;

	// Lerp Value for the spline, when in InterpMode it is the speed of interpolation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (ClampMin = "0", UIMin = "0"))
		float SplineLerpValue;

	// Uses snap increments to move between, not compatible with retain momentum.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bSliderUsesSnapPoints;

	// Portion of the slider that the slider snaps to on release and when within the threshold distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (editcondition = "bSliderUsesSnapPoints", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float SnapIncrement;

	// Threshold distance that when within the slider will stay snapped to its current snap increment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (editcondition = "bSliderUsesSnapPoints", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float SnapThreshold;

	// If true then the slider progress will keep incrementing between snap points if outside of the threshold
	// However events will not be thrown
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (editcondition = "bSliderUsesSnapPoints") )
		bool bIncrementProgressBetweenSnapPoints;


	// Resetting the initial transform here so that it comes in prior to BeginPlay and save loading.
	virtual void OnRegister() override;

protected:
	// Now replicating this so that it works correctly over the network
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InitialRelativeTransform, Category = "VRSliderComponent")
		FTransform_NetQuantize InitialRelativeTransform;
public:
	// Gets the initial relative transform, if you want to set it you should be using ResetInitialButtonLocation
	FTransform GetInitialRelativeTransform() { return InitialRelativeTransform; }

	UFUNCTION()
	virtual void OnRep_InitialRelativeTransform()
	{
		CalculateSliderProgress();
	}

	FVector InitialInteractorLocation;
	FVector InitialGripLoc;
	FVector InitialDropLocation;

	// Checks if we should throw some events
	void CheckSliderProgress();

	/*
	Credit to:
	https://github.com/Seurabimn
	
	Who had this function in an engine pull request:
	https://github.com/EpicGames/UnrealEngine/pull/6646
	*/
	float GetDistanceAlongSplineAtSplineInputKey(float InKey) const;

	// Calculates the current slider progress
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
		float CalculateSliderProgress();

	// Forcefully sets the slider progress to the defined value
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
		void SetSliderProgress(float NewSliderProgress);

	// Should be called after the slider is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
		void ResetInitialSliderLocation();

	// Sets the spline component to follow, if empty, just reinitializes the transform and removes the follow component
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
		void SetSplineComponentToFollow(USplineComponent * SplineToFollow);

	void ResetToParentSplineLocation();

	void GetLerpedKey(float &ClosestKey, float DeltaTime);
	float GetCurrentSliderProgress(FVector CurLocation, bool bUseKeyInstead = false, float CurKey = 0.f);

	// Returns the slider progress as it is currently per axis (not the total progress, just the amount per axis that has a min/max)
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
		FVector GetPerAxisSliderProgress();

	FVector ClampSlideVector(FVector ValueToClamp);

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

	// Distance before the object will break out of the hand, 0.0f == never will
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float BreakDistance;

	// Checks and applies auto drop if we need too
	bool CheckAutoDrop(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation);

	// Should we deny gripping on this object
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface", meta = (ScriptName = "IsDenyGripping"))
		bool bDenyGripping;

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface", meta = (ScriptName = "IsCurrentlyHeld"))
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		FBPGripPair HoldingGrip; // Set on grip notify, not net serializing
	bool bOriginalReplicatesMovement;

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