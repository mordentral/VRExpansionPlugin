
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Interactibles/VRInteractibleFunctionLibrary.h"
#include "VRGripInterface.h"
#include "VRMountComponent.generated.h"

class UGripMotionControllerComponent;


UENUM(Blueprintable)
enum class EVRInteractibleMountAxis : uint8
{
	/** Limit Rotation to Yaw and Roll */
	Axis_XZ
};

// A mounted lever/interactible implementation - Created by SpaceHarry - Merged into the plugin 01/29/2018
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRMountComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UVRMountComponent(const FObjectInitializer& ObjectInitializer);


	~UVRMountComponent();


	// Rotation axis to use, XY is combined X and Y, only LerpToZero and PositiveLimits work with this mode
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMountComponent")
		EVRInteractibleMountAxis MountRotationAxis;

	// Resetting the initial transform here so that it comes in prior to BeginPlay and save loading.
	virtual void OnRegister() override;

	FTransform InitialRelativeTransform;
	FVector InitialInteractorLocation;
	FVector InitialInteractorDropLocation;
	float InitialGripRot;
	FQuat qRotAtGrab;

	// If the mount is swirling around a 90 degree pitch increase this number by 0.1 steps. 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMountComponent")
		float FlipingZone;

	// If the mount feels lagging behind in yaw at some point after 90 degree pitch increase this number by 0.1 steps
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMountComponent")
		float FlipReajustYawSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		int GripPriority;

	// Sets the grip priority
	UFUNCTION(BlueprintCallable, Category = "GripSettings")
		void SetGripPriority(int NewGripPriority);

	bool GrippedOnBack;

	bool bIsInsideFrontFlipingZone;
	bool bIsInsideBackFlipZone;
	FVector CurInterpGripLoc;

	float TwistDiff;
	FVector InitialGripToForwardVec;
	FVector InitialForwardVector;
	FVector EntryUpXYNeg;
	FVector EntryUpVec;
	FVector EntryRightVec;

	bool bFirstEntryToHalfFlipZone;
	bool bLerpingOutOfFlipZone;
	bool bIsFlipped;

	FPlane FlipPlane;
	FPlane ForwardPullPlane;
	FVector LastPointOnForwardPlane;
	FVector CurPointOnForwardPlane;

	float LerpOutAlpha;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float PrimarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripSettings")
		float SecondarySlotRange;

	// Should we deny gripping on this object
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface", meta = (ScriptName = "IsDenyGripping"))
		bool bDenyGripping;

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface", meta = (ScriptName = "IsCurrentlyHeld"))
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		FBPGripPair HoldingGrip; // Set on grip notify, not net serializing
	bool bOriginalReplicatesMovement;

	// Should be called after the Mount is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRMountComponent")
		void ResetInitialMountLocation();

	virtual void OnUnregister() override;;

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

