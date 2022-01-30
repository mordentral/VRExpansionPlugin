// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "VRBPDatatypes.h"
#include "VRGripInterface.h"
#include "VRExpansionFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "Components/CapsuleComponent.h"
#include "GripScripts/VRGripScriptBase.h"
#include "Engine/ActorChannel.h"
#include "GrippableCapsuleComponent.generated.h"

/**
*
*/

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent, ChildCanTick), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UGrippableCapsuleComponent : public UCapsuleComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UGrippableCapsuleComponent(const FObjectInitializer& ObjectInitializer);


	~UGrippableCapsuleComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Instanced, Category = "VRGripInterface")
		TArray<class UVRGripScriptBase *> GripLogicScripts;

	// If true then the grip script array will be considered for replication, if false then it will not
	// This is an optimization for when you have a lot of grip scripts in use, you can toggle this off in cases
	// where the object will never have a replicating script
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		bool bReplicateGripScripts;

	bool ReplicateSubobjects(UActorChannel* Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags) override;

	// Sets the Deny Gripping variable on the FBPInterfaceSettings struct
	UFUNCTION(BlueprintCallable, Category = "VRGripInterface")
		void SetDenyGripping(bool bDenyGripping);

	// Sets the grip priority on the FBPInterfaceSettings struct
	UFUNCTION(BlueprintCallable, Category = "VRGripInterface")
		void SetGripPriority(int NewGripPriority);

	// Called when a object is gripped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnGripped;

	// Called when a object is dropped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnDropSignature OnDropped;

	// Called when an object we hold is secondary gripped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnSecondaryGripAdded;

	// Called when an object we hold is secondary dropped
	// If you override the OnGrip event then you will need to call the parent implementation or this event will not fire!!
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnSecondaryGripRemoved;

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

	/** Called right before being marked for destruction due to network replication */
	// Clean up our objects so that they aren't sitting around for GC
	virtual void PreDestroyFromReplication() override;

	virtual void GetSubobjectsWithStableNamesForNetworking(TArray<UObject*> &ObjList) override;

	// This one is for components to clean up
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	// Requires bReplicates to be true for the component
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
		bool bRepGripSettingsAndGameplayTags;

	// Overrides the default of : true and allows for controlling it like in an actor, should be default of off normally with grippable components
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
		bool bReplicateMovement;

	bool bOriginalReplicatesMovement;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		FBPInterfaceProperties VRGripInterfaceSettings;

	// Set up as deny instead of allow so that default allows for gripping
	// The GripInitiator is not guaranteed to be valid, check it for validity
	virtual bool DenyGripping_Implementation(UGripMotionControllerComponent * GripInitiator = nullptr) override;

	// How an interfaced object behaves when teleporting
	virtual EGripInterfaceTeleportBehavior TeleportBehavior_Implementation() override;

	// Should this object simulate on drop
	virtual bool SimulateOnDrop_Implementation() override;

	// Grip type to use
	virtual EGripCollisionType GetPrimaryGripType_Implementation(bool bIsSlot) override;

	// Secondary grip type
	virtual ESecondaryGripType SecondaryGripType_Implementation() override;

	// Define which movement repliation setting to use
	virtual EGripMovementReplicationSettings GripMovementReplicationType_Implementation() override;

	// Define the late update setting
	virtual EGripLateUpdateSettings GripLateUpdateSetting_Implementation() override;

	// What grip stiffness and damping to use if using a physics constraint
	virtual void GetGripStiffnessAndDamping_Implementation(float& GripStiffnessOut, float& GripDampingOut) override;

	// Get the advanced physics settings for this grip
	virtual FBPAdvGripSettings AdvancedGripSettings_Implementation() override;

	// What distance to break a grip at (only relevent with physics enabled grips
	virtual float GripBreakDistance_Implementation() override;

	// Get closest primary slot in range
	virtual  void ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool& bHadSlotInRange, FTransform& SlotWorldTransform, FName& SlotName, UGripMotionControllerComponent* CallingController = nullptr, FName OverridePrefix = NAME_None) override;

	// Check if an object allows multiple grips at one time
	virtual  bool AllowsMultipleGrips_Implementation() override;

	// Returns if the object is held and if so, which controllers are holding it
	virtual void IsHeld_Implementation(TArray<FBPGripPair>& HoldingControllers, bool& bIsHeld) override;

	// Sets is held, used by the plugin
	virtual void SetHeld_Implementation(UGripMotionControllerComponent* HoldingController, uint8 GripID, bool bIsHeld) override;

	// Interface function used to throw the delegates that is invisible to blueprints so that it can't be overridden
	virtual void Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	// Returns if the object wants to be socketed
	virtual bool RequestsSocketing_Implementation(USceneComponent*& ParentToSocketTo, FName& OptionalSocketName, FTransform_NetQuantize& RelativeTransform) override;

	// Get grip scripts
	virtual bool GetGripScripts_Implementation(TArray<UVRGripScriptBase*>& ArrayReference) override;

	// Events //

	// Event triggered each tick on the interfaced object when gripped, can be used for custom movement or grip based logic
	virtual void TickGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation, float DeltaTime) override;

	// Event triggered on the interfaced object when gripped
	virtual void OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) override;

	// Event triggered on the interfaced object when grip is released
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	// Event triggered on the interfaced object when child component is gripped
	virtual void OnChildGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) override;

	// Event triggered on the interfaced object when child component is released
	virtual void OnChildGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	// Event triggered on the interfaced object when secondary gripped
	virtual void OnSecondaryGrip_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation) override;

	// Event triggered on the interfaced object when secondary grip is released
	virtual void OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* ReleasingSecondaryGripComponent, const FBPActorGripInformation& GripInformation) override;

	// Interaction Functions

	// Call to use an object
	virtual void OnUsed_Implementation() override;

	// Call to stop using an object
	virtual void OnEndUsed_Implementation() override;

	// Call to use an object
	virtual void OnSecondaryUsed_Implementation() override;

	// Call to stop using an object
	virtual void OnEndSecondaryUsed_Implementation() override;

	// Call to send an action event to the object
	virtual void OnInput_Implementation(FKey Key, EInputEvent KeyEvent) override;
};