// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "VRBPDatatypes.h"
#include "VRGripInterface.h"
//#include "Engine/Engine.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "Engine/ActorChannel.h"
#include "Grippables/GrippableDataTypes.h"
#include "Grippables/GrippablePhysicsReplication.h"
#include "GrippableActor.generated.h"

class UGripMotionControllerComponent;
class UVRGripScriptBase;

/**
*
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API AGrippableActor : public AActor, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	AGrippableActor(const FObjectInitializer& ObjectInitializer);

	~AGrippableActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(Replicated, ReplicatedUsing = OnRep_AttachmentReplication)
		FRepAttachmentWithWeld AttachmentWeldReplication;

	virtual void GatherCurrentMovement() override;

protected:
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Instanced, Category = "VRGripInterface")
		TArray<TObjectPtr<UVRGripScriptBase>> GripLogicScripts;

	// If true then the grip script array will be considered for replication, if false then it will not
	// This is an optimization for when you have a lot of grip scripts in use, you can toggle this off in cases
	// where the object will never have a replicating script
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite,  Category = "VRGripInterface")
		bool bReplicateGripScripts;
public:

	void SetReplicateGripScripts(bool NewReplicateGripScripts);
	inline bool GetReplicateGripScripts() { return bReplicateGripScripts; };

	// Get the grip script array, will automatically dirty it if they are replicated as it is assumed if you are directly accessing it you are altering it
	TArray<TObjectPtr<UVRGripScriptBase>>& GetGripLogicScripts();

	bool ReplicateSubobjects(UActorChannel* Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags) override;
	virtual void GetSubobjectsWithStableNamesForNetworking(TArray<UObject*>& ObjList) override;

	// Sets the Deny Gripping variable on the FBPInterfaceSettings struct
	UFUNCTION(BlueprintCallable, Category = "VRGripInterface")
	void SetDenyGripping(bool bDenyGripping);

	// Sets the grip priority on the FBPInterfaceSettings struct
	UFUNCTION(BlueprintCallable, Category = "VRGripInterface")
		void SetGripPriority(int NewGripPriority);

	// Called when a object is gripped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnGripped;

	// Called when a object is dropped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnDropSignature OnDropped;

	// Called when an object we hold is secondary gripped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnSecondaryGripAdded;

	// Called when an object we hold is secondary dropped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnGripSignature OnSecondaryGripRemoved;

	// ------------------------------------------------
	// Client Auth Throwing Data and functions 
	// ------------------------------------------------

protected:
	// Replication settings for client auth throwing
	// Must call MarkClientAuthReplicationDirty if setting the properties of it live
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Replication")
		FVRClientAuthReplicationData ClientAuthReplicationData;
public:

	// If changing this in c++ it is using a getter to make sure it is dirtied
	FVRClientAuthReplicationData& GetClientAuthReplicationData(FVRClientAuthReplicationData& ClientAuthData);


	// Add this to client side physics replication (until coming to rest or timeout period is hit)
	UFUNCTION(BlueprintCallable, Category = "Networking")
		bool AddToClientReplicationBucket();

	// Remove this from client side physics replication
	UFUNCTION(BlueprintCallable, Category = "Networking")
		bool RemoveFromClientReplicationBucket();

	UFUNCTION()
	bool PollReplicationEvent();

	UFUNCTION(Category = "Networking")
		void CeaseReplicationBlocking();

	// Notify the server that we are no longer trying to run the throwing auth
	UFUNCTION(Reliable, Server, WithValidation, Category = "Networking")
		void Server_EndClientAuthReplication();

	// Notify the server about a new movement rep
	UFUNCTION(UnReliable, Server, WithValidation, Category = "Networking")
		void Server_GetClientAuthReplication(const FRepMovementVR & newMovement);

	// Returns if this object is currently client auth throwing
	UFUNCTION(BlueprintPure, Category = "Networking")
		FORCEINLINE bool IsCurrentlyClientAuthThrowing()
	{
		return ClientAuthReplicationData.bIsCurrentlyClientAuth;
	}

	// End client auth throwing data and functions //

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

	public:
	FGameplayTagContainer& GetGameplayTags();

	// End Gameplay Tag Interface

	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;

	protected:

	// Skips the attachment replication if we are locally owned and our grip settings say that we are a client authed grip.
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Replication")
		bool bAllowIgnoringAttachOnOwner;
	public:

	void SetAllowIgnoringAttachOnOwner(bool bNewAllowIgnoringAttachOnOwner);
	inline bool GetAllowIgnoringAttachOnOwner() { return bAllowIgnoringAttachOnOwner; };

	// Should we skip attachment replication (vr settings say we are a client auth grip and our owner is locally controlled)
	/*inline*/ bool ShouldWeSkipAttachmentReplication(bool bConsiderHeld = true) const;

	// Handle fixing some bugs and issues with ReplicateMovement being off
	virtual void OnRep_AttachmentReplication() override;
	virtual void OnRep_ReplicateMovement() override;
	virtual void OnRep_ReplicatedMovement() override;
	virtual void PostNetReceivePhysicState() override;

	// Debug printing of when the object is replication destroyed
	/*virtual void OnSubobjectDestroyFromReplication(UObject *Subobject) override
	{
	Super::OnSubobjectDestroyFromReplication(Subobject);

	GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString::Printf(TEXT("Killed Object On Actor: x: %s"), *Subobject->GetName()));
	}*/

	// This isn't called very many places but it does come up
	virtual void MarkComponentsAsGarbage(bool bModify) override;

	/** Called right before being marked for destruction due to network replication */
	// Clean up our objects so that they aren't sitting around for GC
	virtual void PreDestroyFromReplication() override;

	// On Destroy clean up our objects
	virtual void BeginDestroy() override;

	protected:
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		bool bRepGripSettingsAndGameplayTags;
	public:

	void SetRepGripSettingsAndGameplayTags(bool bNewRepGripSettingsAndGameplayTags);
	inline bool GetRepGripSettingsAndGameplayTags() { return bRepGripSettingsAndGameplayTags; };

	protected:
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		FBPInterfaceProperties VRGripInterfaceSettings;
	public:

	// Get VRGripInterfaceSettings, set MarkDirty if you intend to (or may) modify the values inside of it
	FBPInterfaceProperties& GetVRGripInterfaceSettings(bool bMarkDirty);

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