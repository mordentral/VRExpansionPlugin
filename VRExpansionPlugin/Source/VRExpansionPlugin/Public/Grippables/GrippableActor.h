// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "VRGripInterface.h"
#include "VRExpansionFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "GripScripts/VRGripScriptBase.h"
#include "Engine/ActorChannel.h"
#include "DrawDebugHelpers.h"
#include "GrippableActor.generated.h"

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

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Instanced, Category = "VRGripInterface")
		TArray<class UVRGripScriptBase *> GripLogicScripts;

	bool ReplicateSubobjects(UActorChannel* Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags) override;

	// Sets the Deny Gripping variable on the FBPInterfaceSettings struct
	UFUNCTION(BlueprintCallable, Category = "VRGripInterface")
	void SetDenyGripping(bool bDenyGripping);

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

	// Skips the attachment replication if we are locally owned and our grip settings say that we are a client authed grip.
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Replication")
		bool bAllowIgnoringAttachOnOwner;

	// Should we skip attachment replication (vr settings say we are a client auth grip and our owner is locally controlled)
	inline bool ShouldWeSkipAttachmentReplication() const
	{
		if (!VRGripInterfaceSettings.bWasHeld || GetNetMode() < ENetMode::NM_Client)
			return false;

		if (VRGripInterfaceSettings.MovementReplicationType == EGripMovementReplicationSettings::ClientSide_Authoritive ||
			VRGripInterfaceSettings.MovementReplicationType == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep)
		{
			const APawn* MyPawn = Cast<APawn>(GetOwner());
			return (MyPawn ? MyPawn->IsLocallyControlled() : false);
		}
		else
			return false;
	}

	virtual void OnRep_AttachmentReplication() override
	{
		if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication())
		{
			return;
		}

		// None of our overrides are required, lets just pass it on now
		Super::OnRep_AttachmentReplication();
	}

	virtual void OnRep_ReplicateMovement() override
	{
		if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication())
		{
			return;
		}

		if (RootComponent)
		{
			const FRepAttachment ReplicationAttachment = GetAttachmentReplication();
			if (!ReplicationAttachment.AttachParent)
			{
				// This "fix" corrects the simulation state not replicating over correctly
				// If you turn off movement replication, simulate an object, turn movement replication back on and un-simulate, it never knows the difference
				// This change ensures that it is checking against the current state
				if (RootComponent->IsSimulatingPhysics() != ReplicatedMovement.bRepPhysics)//SavedbRepPhysics != ReplicatedMovement.bRepPhysics)
				{
					// Turn on/off physics sim to match server.
					SyncReplicatedPhysicsSimulation();

					// It doesn't really hurt to run it here, the super can call it again but it will fail out as they already match
				}
			}
		}

		Super::OnRep_ReplicateMovement();

		// 4.21 "fixed" the bReplicateMovement issue, had to comment out my old fix to play nice
		// Leaving the original code here commented out for now as a reference in case I need it.
		/*
		// Since ReplicatedMovement and AttachmentReplication are REPNOTIFY_Always (and OnRep_AttachmentReplication may call OnRep_ReplicatedMovement directly),
		// this check is needed since this can still be called on actors for which bReplicateMovement is false - for example, during fast-forward in replay playback.
		// When this happens, the values in ReplicatedMovement aren't valid, and must be ignored.
		if (!bReplicateMovement)
		{
			return;
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		static const auto CVarDrawDebugRepMovement = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("Net.RepMovement.DrawDebug"));
		if (CVarDrawDebugRepMovement->GetValueOnGameThread() > 0)
		{
			DrawDebugCapsule(GetWorld(), ReplicatedMovement.Location, GetSimpleCollisionHalfHeight(), GetSimpleCollisionRadius(), ReplicatedMovement.Rotation.Quaternion(), FColor(100, 255, 100), true, 1.f);
		}
#endif

		if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication())
		{
			return;
		}

		if (RootComponent)
		{
			// This "fix" corrects the simulation state not replicating over correctly
			// If you turn off movement replication, simulate an object, turn movement replication back on and un-simulate, it never knows the difference
			// This change ensures that it is checking against the current state
			if (RootComponent->IsSimulatingPhysics() != ReplicatedMovement.bRepPhysics)//SavedbRepPhysics != ReplicatedMovement.bRepPhysics)
			{
				// Turn on/off physics sim to match server.
				SyncReplicatedPhysicsSimulation();

				// It doesn't really hurt to run it here, the super can call it again but it will fail out as they already match
			}

		}

		Super::OnRep_ReplicateMovement();*/
	}

	void PostNetReceivePhysicState() override
	{
		if (VRGripInterfaceSettings.bIsHeld && bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication())
		{
			return;
		}

		Super::PostNetReceivePhysicState();
	}

	// Debug printing of when the object is replication destroyed
	/*virtual void OnSubobjectDestroyFromReplication(UObject *Subobject) override
	{
	Super::OnSubobjectDestroyFromReplication(Subobject);

	GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString::Printf(TEXT("Killed Object On Actor: x: %s"), *Subobject->GetName()));
	}*/

	// This isn't called very many places but it does come up
	virtual void MarkComponentsAsPendingKill() override
	{
		Super::MarkComponentsAsPendingKill();

		for (int32 i = 0; i < GripLogicScripts.Num(); ++i)
		{
			if (UObject *SubObject = GripLogicScripts[i])
			{
				SubObject->MarkPendingKill();
			}
		}

		GripLogicScripts.Empty();
	}

	/** Called right before being marked for destruction due to network replication */
	// Clean up our objects so that they aren't sitting around for GC
	virtual void PreDestroyFromReplication() override
	{
		Super::PreDestroyFromReplication();

		// Destroy any sub-objects we created
		for (int32 i = 0; i < GripLogicScripts.Num(); ++i)
		{
			if (UObject *SubObject = GripLogicScripts[i])
			{
				OnSubobjectDestroyFromReplication(SubObject); //-V595
				SubObject->PreDestroyFromReplication();
				SubObject->MarkPendingKill();
			}
		}

		for (UActorComponent * ActorComp : GetComponents())
		{
			// Pending kill components should have already had this called as they were network spawned and are being killed
			if (ActorComp && !ActorComp->IsPendingKill() && ActorComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				ActorComp->PreDestroyFromReplication();
		}

		GripLogicScripts.Empty();
	}

	// On Destroy clean up our objects
	virtual void BeginDestroy() override
	{
		Super::BeginDestroy();

		for (int32 i = 0; i < GripLogicScripts.Num(); i++)
		{
			if (UObject *SubObject = GripLogicScripts[i])
			{
				SubObject->MarkPendingKill();
			}
		}

		GripLogicScripts.Empty();
	}

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		bool bRepGripSettingsAndGameplayTags;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		FBPInterfaceProperties VRGripInterfaceSettings;

	// Set up as deny instead of allow so that default allows for gripping
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface", meta = (DisplayName = "IsDenyingGrips"))
		bool DenyGripping();

	// How an interfaced object behaves when teleporting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripInterfaceTeleportBehavior TeleportBehavior();

	// Should this object simulate on drop
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool SimulateOnDrop();

		// Grip type to use
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType GetPrimaryGripType(bool bIsSlot);

	// Secondary grip type
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		ESecondaryGripType SecondaryGripType();

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

		// Get closest primary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestGripSlotInRange(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);


	// Check if the object is an interactable
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		//bool IsInteractible();

	// Returns if the object is held and if so, which pawn is holding it
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void IsHeld(UGripMotionControllerComponent *& HoldingController, bool & bIsHeld);

	// Sets is held, used by the plugin
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void SetHeld(UGripMotionControllerComponent * HoldingController, bool bIsHeld);

	// Returns if the object is socketed currently
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool RequestsSocketing(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform);

	// Get interactable settings
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		//FBPInteractionSettings GetInteractionSettings();

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
		void OnSecondaryGrip(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnSecondaryGripRelease(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation);

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
};