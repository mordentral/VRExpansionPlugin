// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/GrippableStaticMeshActor.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsReplication.h"
#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif

// #TODO: Pull request this? This macro could be very useful
/*#define DOREPLIFETIME_CHANGE_NOTIFY(c,v,rncond) \
{ \
	static UProperty* sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(),GET_MEMBER_NAME_CHECKED(c,v));	\
	bool bFound = false;																							\
	for ( int32 i = 0; i < OutLifetimeProps.Num(); i++ )															\
	{																												\
		if ( OutLifetimeProps[i].RepIndex == sp##v->RepIndex )														\
		{																											\
			for ( int32 j = 0; j < sp##v->ArrayDim; j++ )															\
			{																										\
				OutLifetimeProps[i + j].RepNotifyCondition = rncond;															\
			}																										\
			bFound = true;																							\
			break;																									\
		}																											\
	}																												\
	check( bFound );																								\
}*/


UOptionalRepStaticMeshComponent::UOptionalRepStaticMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicateMovement = true;
}

void UOptionalRepStaticMeshComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeScale3D, bReplicateMovement);


}

void UOptionalRepStaticMeshComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UOptionalRepStaticMeshComponent, bReplicateMovement);
}

  //=============================================================================
AGrippableStaticMeshActor::AGrippableStaticMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UOptionalRepStaticMeshComponent>(TEXT("StaticMeshComponent0")))
{
	VRGripInterfaceSettings.bDenyGripping = false;
	VRGripInterfaceSettings.OnTeleportBehavior = EGripInterfaceTeleportBehavior::TeleportAllComponents;
	VRGripInterfaceSettings.bSimulateOnDrop = true;
	VRGripInterfaceSettings.SlotDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	VRGripInterfaceSettings.FreeDefaultGripType = EGripCollisionType::InteractiveCollisionWithPhysics;
	VRGripInterfaceSettings.SecondaryGripType = ESecondaryGripType::SG_None;
	VRGripInterfaceSettings.MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
	VRGripInterfaceSettings.LateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping;
	VRGripInterfaceSettings.ConstraintStiffness = 1500.0f;
	VRGripInterfaceSettings.ConstraintDamping = 200.0f;
	VRGripInterfaceSettings.ConstraintBreakDistance = 100.0f;
	VRGripInterfaceSettings.SecondarySlotRange = 20.0f;
	VRGripInterfaceSettings.PrimarySlotRange = 20.0f;

	VRGripInterfaceSettings.bIsHeld = false;

	this->SetMobility(EComponentMobility::Movable);

	// Default replication on for multiplayer
	//this->bNetLoadOnClient = false;
	SetReplicatingMovement(true);
	this->bReplicates = true;
	
	bRepGripSettingsAndGameplayTags = true;
	bReplicateGripScripts = false;
	bAllowIgnoringAttachOnOwner = true;

	// Setting a minimum of every 3rd frame (VR 90fps) for replication consideration
	// Otherwise we will get some massive slow downs if the replication is allowed to hit the 2 per second minimum default
	MinNetUpdateFrequency = 30.0f;
}

void AGrippableStaticMeshActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME_CONDITION(AGrippableStaticMeshActor, GripLogicScripts, COND_Custom);
	DOREPLIFETIME(AGrippableStaticMeshActor, bReplicateGripScripts);
	DOREPLIFETIME(AGrippableStaticMeshActor, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME(AGrippableStaticMeshActor, bAllowIgnoringAttachOnOwner);
	DOREPLIFETIME(AGrippableStaticMeshActor, ClientAuthReplicationData);
	DOREPLIFETIME_CONDITION(AGrippableStaticMeshActor, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(AGrippableStaticMeshActor, GameplayTags, COND_Custom);

	DISABLE_REPLICATED_PRIVATE_PROPERTY(AActor, AttachmentReplication);

	FDoRepLifetimeParams AttachmentReplicationParams{ COND_Custom, REPNOTIFY_Always, /*bIsPushBased=*/true };
	DOREPLIFETIME_WITH_PARAMS_FAST(AGrippableStaticMeshActor, AttachmentWeldReplication, AttachmentReplicationParams);
}

void AGrippableStaticMeshActor::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	//Super::PreReplication(ChangedPropertyTracker);
	
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableStaticMeshActor, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableStaticMeshActor, GameplayTags, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableStaticMeshActor, GripLogicScripts, bReplicateGripScripts);
	
	//Super::PreReplication(ChangedPropertyTracker);

#if WITH_PUSH_MODEL
	const AActor* const OldAttachParent = AttachmentWeldReplication.AttachParent;
	const UActorComponent* const OldAttachComponent = AttachmentWeldReplication.AttachComponent;
#endif

	// Attachment replication gets filled in by GatherCurrentMovement(), but in the case of a detached root we need to trigger remote detachment.
	AttachmentWeldReplication.AttachParent = nullptr;
	AttachmentWeldReplication.AttachComponent = nullptr;

	GatherCurrentMovement();

	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(AActor, ReplicatedMovement, IsReplicatingMovement());

	// Don't need to replicate AttachmentReplication if the root component replicates, because it already handles it.
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableStaticMeshActor, AttachmentWeldReplication, RootComponent && !RootComponent->GetIsReplicated());

	// Don't need to replicate AttachmentReplication if the root component replicates, because it already handles it.
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(AActor, AttachmentReplication, RootComponent && !RootComponent->GetIsReplicated());


#if WITH_PUSH_MODEL
	if (UNLIKELY(OldAttachParent != AttachmentWeldReplication.AttachParent || OldAttachComponent != AttachmentWeldReplication.AttachComponent))
	{
		//MARK_PROPERTY_DIRTY_FROM_NAME(AGrippableStaticMeshActor, AttachmentWeldReplication, this);
	}
#endif

	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass());
	if (BPClass != nullptr)
	{
		BPClass->InstancePreReplication(this, ChangedPropertyTracker);
	}
}

void AGrippableStaticMeshActor::GatherCurrentMovement()
{
	if (IsReplicatingMovement() || (RootComponent && RootComponent->GetAttachParent()))
	{
		bool bWasAttachmentModified = false;
		bool bWasRepMovementModified = false;

		AActor* OldAttachParent = AttachmentWeldReplication.AttachParent;
		USceneComponent* OldAttachComponent = AttachmentWeldReplication.AttachComponent;

		AttachmentWeldReplication.AttachParent = nullptr;
		AttachmentWeldReplication.AttachComponent = nullptr;

		FRepMovement& RepMovement = GetReplicatedMovement_Mutable();

		UPrimitiveComponent* RootPrimComp = Cast<UPrimitiveComponent>(GetRootComponent());
		if (RootPrimComp && RootPrimComp->IsSimulatingPhysics())
		{
			FRigidBodyState RBState;
			RootPrimComp->GetRigidBodyState(RBState);

			RepMovement.FillFrom(RBState, this);
			// Don't replicate movement if we're welded to another parent actor.
			// Their replication will affect our position indirectly since we are attached.
			RepMovement.bRepPhysics = !RootPrimComp->IsWelded();

			if (!RepMovement.bRepPhysics)
			{
				if (RootComponent->GetAttachParent() != nullptr)
				{
					// Networking for attachments assumes the RootComponent of the AttachParent actor. 
					// If that's not the case, we can't update this, as the client wouldn't be able to resolve the Component and would detach as a result.
					AttachmentWeldReplication.AttachParent = RootComponent->GetAttachParent()->GetAttachmentRootActor();
					if (AttachmentWeldReplication.AttachParent != nullptr)
					{
						AttachmentWeldReplication.LocationOffset = RootComponent->GetRelativeLocation();
						AttachmentWeldReplication.RotationOffset = RootComponent->GetRelativeRotation();
						AttachmentWeldReplication.RelativeScale3D = RootComponent->GetRelativeScale3D();
						AttachmentWeldReplication.AttachComponent = RootComponent->GetAttachParent();
						AttachmentWeldReplication.AttachSocket = RootComponent->GetAttachSocketName();
						AttachmentWeldReplication.bIsWelded = RootPrimComp ? RootPrimComp->IsWelded() : false;

						// Technically, the values might have stayed the same, but we'll just assume they've changed.
						bWasAttachmentModified = true;
					}
				}
			}

			// Technically, the values might have stayed the same, but we'll just assume they've changed.
			bWasRepMovementModified = true;
		}
		else if (RootComponent != nullptr)
		{
			// If we are attached, don't replicate absolute position, use AttachmentReplication instead.
			if (RootComponent->GetAttachParent() != nullptr)
			{
				// Networking for attachments assumes the RootComponent of the AttachParent actor. 
				// If that's not the case, we can't update this, as the client wouldn't be able to resolve the Component and would detach as a result.
				AttachmentWeldReplication.AttachParent = RootComponent->GetAttachParent()->GetAttachmentRootActor();
				if (AttachmentWeldReplication.AttachParent != nullptr)
				{
					AttachmentWeldReplication.LocationOffset = RootComponent->GetRelativeLocation();
					AttachmentWeldReplication.RotationOffset = RootComponent->GetRelativeRotation();
					AttachmentWeldReplication.RelativeScale3D = RootComponent->GetRelativeScale3D();
					AttachmentWeldReplication.AttachComponent = RootComponent->GetAttachParent();
					AttachmentWeldReplication.AttachSocket = RootComponent->GetAttachSocketName();
					AttachmentWeldReplication.bIsWelded = RootPrimComp ? RootPrimComp->IsWelded() : false;

					// Technically, the values might have stayed the same, but we'll just assume they've changed.
					bWasAttachmentModified = true;
				}
			}
			else
			{
				RepMovement.Location = FRepMovement::RebaseOntoZeroOrigin(RootComponent->GetComponentLocation(), this);
				RepMovement.Rotation = RootComponent->GetComponentRotation();
				RepMovement.LinearVelocity = GetVelocity();
				RepMovement.AngularVelocity = FVector::ZeroVector;

				// Technically, the values might have stayed the same, but we'll just assume they've changed.
				bWasRepMovementModified = true;
			}

			bWasRepMovementModified = (bWasRepMovementModified || RepMovement.bRepPhysics);
			RepMovement.bRepPhysics = false;
		}
#if WITH_PUSH_MODEL
		if (bWasRepMovementModified)
		{
		//	MARK_PROPERTY_DIRTY_FROM_NAME(AActor, ReplicatedMovement, this);
		}

		if (bWasAttachmentModified ||
			OldAttachParent != AttachmentWeldReplication.AttachParent ||
			OldAttachComponent != AttachmentWeldReplication.AttachComponent)
		{
			//MARK_PROPERTY_DIRTY_FROM_NAME(AGrippableStaticMeshActor, AttachmentWeldReplication, this);
		}
#endif
	}
}

bool AGrippableStaticMeshActor::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	if (bReplicateGripScripts)
	{
		for (UVRGripScriptBase* Script : GripLogicScripts)
		{
			if (Script && !Script->IsPendingKill())
			{
				WroteSomething |= Channel->ReplicateSubobject(Script, *Bunch, *RepFlags);
			}
		}
	}

	return WroteSomething;
}

//=============================================================================
AGrippableStaticMeshActor::~AGrippableStaticMeshActor()
{
}

void AGrippableStaticMeshActor::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	// Call all grip scripts begin play events so they can perform any needed logic
	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script)
		{
			Script->BeginPlay(this);
		}
	}
}

void AGrippableStaticMeshActor::SetDenyGripping(bool bDenyGripping)
{
	VRGripInterfaceSettings.bDenyGripping = bDenyGripping;
}

void AGrippableStaticMeshActor::SetGripPriority(int NewGripPriority)
{
	VRGripInterfaceSettings.AdvancedGripSettings.GripPriority = NewGripPriority;
}

void AGrippableStaticMeshActor::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) {}
void AGrippableStaticMeshActor::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) { }
void AGrippableStaticMeshActor::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) { }
void AGrippableStaticMeshActor::OnChildGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void AGrippableStaticMeshActor::OnChildGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) {}
void AGrippableStaticMeshActor::OnSecondaryGrip_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation) { OnSecondaryGripAdded.Broadcast(GripOwningController, GripInformation); }
void AGrippableStaticMeshActor::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* ReleasingSecondaryGripComponent, const FBPActorGripInformation& GripInformation) { OnSecondaryGripRemoved.Broadcast(GripOwningController, GripInformation); }
void AGrippableStaticMeshActor::OnUsed_Implementation() {}
void AGrippableStaticMeshActor::OnEndUsed_Implementation() {}
void AGrippableStaticMeshActor::OnSecondaryUsed_Implementation() {}
void AGrippableStaticMeshActor::OnEndSecondaryUsed_Implementation() {}
void AGrippableStaticMeshActor::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
bool AGrippableStaticMeshActor::RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) { return false; }

bool AGrippableStaticMeshActor::DenyGripping_Implementation(UGripMotionControllerComponent * GripInitiator)
{
	return VRGripInterfaceSettings.bDenyGripping;
}


EGripInterfaceTeleportBehavior AGrippableStaticMeshActor::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool AGrippableStaticMeshActor::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripCollisionType AGrippableStaticMeshActor::GetPrimaryGripType_Implementation(bool bIsSlot)
{
	return bIsSlot ? VRGripInterfaceSettings.SlotDefaultGripType : VRGripInterfaceSettings.FreeDefaultGripType;
}

ESecondaryGripType AGrippableStaticMeshActor::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}

EGripMovementReplicationSettings AGrippableStaticMeshActor::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings AGrippableStaticMeshActor::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

void AGrippableStaticMeshActor::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = VRGripInterfaceSettings.ConstraintStiffness;
	GripDampingOut = VRGripInterfaceSettings.ConstraintDamping;
}

FBPAdvGripSettings AGrippableStaticMeshActor::AdvancedGripSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedGripSettings;
}

float AGrippableStaticMeshActor::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void AGrippableStaticMeshActor::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(OverridePrefix, this, WorldLocation, bSecondarySlot ? VRGripInterfaceSettings.SecondarySlotRange : VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform, SlotName, CallingController);
}

bool AGrippableStaticMeshActor::AllowsMultipleGrips_Implementation()
{
	return VRGripInterfaceSettings.bAllowMultipleGrips;
}

void AGrippableStaticMeshActor::IsHeld_Implementation(TArray<FBPGripPair> & HoldingControllers, bool & bIsHeld)
{
	HoldingControllers = VRGripInterfaceSettings.HoldingControllers;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

bool AGrippableStaticMeshActor::AddToClientReplicationBucket()
{
	if (ShouldWeSkipAttachmentReplication(false))
	{
		// The subsystem automatically removes entries with the same function signature so its safe to just always add here
		GetWorld()->GetSubsystem<UBucketUpdateSubsystem>()->AddObjectToBucket(ClientAuthReplicationData.UpdateRate, this, FName(TEXT("PollReplicationEvent")));
		ClientAuthReplicationData.bIsCurrentlyClientAuth = true;

		if (UWorld * World = GetWorld())
			ClientAuthReplicationData.TimeAtInitialThrow = World->GetTimeSeconds();

		return true;
	}

	return false;
}

bool AGrippableStaticMeshActor::RemoveFromClientReplicationBucket()
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth)
	{
		GetWorld()->GetSubsystem<UBucketUpdateSubsystem>()->RemoveObjectFromBucketByFunctionName(this, FName(TEXT("PollReplicationEvent")));
		CeaseReplicationBlocking();
		return true;
	}

	return false;
}

void AGrippableStaticMeshActor::Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
{
	if (bGripped)
	{
		OnGripped.Broadcast(Controller, GripInformation);
	}
	else
	{
		OnDropped.Broadcast(Controller, GripInformation, bWasSocketed);
	}
}

void AGrippableStaticMeshActor::SetHeld_Implementation(UGripMotionControllerComponent* HoldingController, uint8 GripID, bool bIsHeld)
{
	if (bIsHeld)
	{
		VRGripInterfaceSettings.HoldingControllers.AddUnique(FBPGripPair(HoldingController, GripID));
		RemoveFromClientReplicationBucket();

		VRGripInterfaceSettings.bWasHeld = true;
		VRGripInterfaceSettings.bIsHeld = VRGripInterfaceSettings.HoldingControllers.Num() > 0;
	}
	else
	{
		VRGripInterfaceSettings.HoldingControllers.Remove(FBPGripPair(HoldingController, GripID));
		VRGripInterfaceSettings.bIsHeld = VRGripInterfaceSettings.HoldingControllers.Num() > 0;

		if (ClientAuthReplicationData.bUseClientAuthThrowing && !VRGripInterfaceSettings.bIsHeld)
		{
			bool bWasLocallyOwned = HoldingController ? HoldingController->IsLocallyControlled() : false;
			if (bWasLocallyOwned && ShouldWeSkipAttachmentReplication(false))
			{
				if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(GetRootComponent()))
				{
					if (PrimComp->IsSimulatingPhysics())
					{
						AddToClientReplicationBucket();
					}
				}
			}
		}
	}
}

bool AGrippableStaticMeshActor::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	ArrayReference = GripLogicScripts;
	return GripLogicScripts.Num() > 0;
}

bool AGrippableStaticMeshActor::PollReplicationEvent()
{
	if (!ClientAuthReplicationData.bIsCurrentlyClientAuth || !this->HasLocalNetOwner() || VRGripInterfaceSettings.bIsHeld)
		return false; // Tell the bucket subsystem to remove us from consideration

	UWorld *OurWorld = GetWorld();
	if (!OurWorld)
		return false; // Tell the bucket subsystem to remove us from consideration

	bool bRemoveBlocking = false;

	if ((OurWorld->GetTimeSeconds() - ClientAuthReplicationData.TimeAtInitialThrow) > 10.0f)
	{
		// Lets time out sending, its been 10 seconds since we threw the object and its likely that it is conflicting with some server
		// Authed movement that is forcing it to keep momentum.
		//return false; // Tell the bucket subsystem to remove us from consideration
		bRemoveBlocking = true;
	}

	// Store current transform for resting check
	FTransform CurTransform = this->GetActorTransform();

	if (!bRemoveBlocking)
	{
		if (!CurTransform.GetRotation().Equals(ClientAuthReplicationData.LastActorTransform.GetRotation()) || !CurTransform.GetLocation().Equals(ClientAuthReplicationData.LastActorTransform.GetLocation()))
		{
			ClientAuthReplicationData.LastActorTransform = CurTransform;

			if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(RootComponent))
			{
				// Need to clamp to a max time since start, to handle cases with conflicting collisions
				if (PrimComp->IsSimulatingPhysics() && ShouldWeSkipAttachmentReplication(false))
				{
					FRepMovementVR ClientAuthMovementRep;
					if (ClientAuthMovementRep.GatherActorsMovement(this))
					{
						Server_GetClientAuthReplication(ClientAuthMovementRep);

						if (PrimComp->RigidBodyIsAwake())
						{
							return true;
						}
					}
				}
			}
			else
			{
				bRemoveBlocking = true;
				//return false; // Tell the bucket subsystem to remove us from consideration
			}
		}
		//else
	//	{
			// Difference is too small, lets end sending location
			//ClientAuthReplicationData.LastActorTransform = FTransform::Identity;
	//	}
	}

	bool TimedBlockingRelease = false;

	AActor* TopOwner = GetOwner();
	if (TopOwner != nullptr)
	{
		AActor * tempOwner = TopOwner->GetOwner();

		// I have an owner so search that for the top owner
		while (tempOwner)
		{
			TopOwner = tempOwner;
			tempOwner = TopOwner->GetOwner();
		}

		if (APlayerController* PlayerController = Cast<APlayerController>(TopOwner))
		{
			if (APlayerState* PlayerState = PlayerController->PlayerState)
			{
				if (ClientAuthReplicationData.ResetReplicationHandle.IsValid())
				{
					OurWorld->GetTimerManager().ClearTimer(ClientAuthReplicationData.ResetReplicationHandle);
				}

				// Lets clamp the ping to a min / max value just in case
				float clampedPing = FMath::Clamp(PlayerState->ExactPing * 0.001f, 0.0f, 1000.0f);
				OurWorld->GetTimerManager().SetTimer(ClientAuthReplicationData.ResetReplicationHandle, this, &AGrippableStaticMeshActor::CeaseReplicationBlocking, clampedPing, false);
				TimedBlockingRelease = true;
			}
		}
	}

	if (!TimedBlockingRelease)
	{
		CeaseReplicationBlocking();
	}

	// Tell server to kill us
	Server_EndClientAuthReplication();
	return false; // Tell the bucket subsystem to remove us from consideration
}

void AGrippableStaticMeshActor::CeaseReplicationBlocking()
{
	if(ClientAuthReplicationData.bIsCurrentlyClientAuth)
		ClientAuthReplicationData.bIsCurrentlyClientAuth = false;

	ClientAuthReplicationData.LastActorTransform = FTransform::Identity;

	if (ClientAuthReplicationData.ResetReplicationHandle.IsValid())
	{
		if (UWorld * OurWorld = GetWorld())
		{
			OurWorld->GetTimerManager().ClearTimer(ClientAuthReplicationData.ResetReplicationHandle);
		}
	}
}

void AGrippableStaticMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveFromClientReplicationBucket();

	// Call all grip scripts begin play events so they can perform any needed logic
	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script)
		{
			Script->EndPlay(EndPlayReason);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool AGrippableStaticMeshActor::Server_EndClientAuthReplication_Validate()
{
	return true;
}

void AGrippableStaticMeshActor::Server_EndClientAuthReplication_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (FPhysicsReplication* PhysicsReplication = PhysScene->GetPhysicsReplication())
			{
				PhysicsReplication->RemoveReplicatedTarget(this->GetStaticMeshComponent());
			}
		}
	}
}

bool AGrippableStaticMeshActor::Server_GetClientAuthReplication_Validate(const FRepMovementVR & newMovement)
{
	return true;
}

void AGrippableStaticMeshActor::Server_GetClientAuthReplication_Implementation(const FRepMovementVR & newMovement)
{
	if (!VRGripInterfaceSettings.bIsHeld)
	{
		FRepMovement& MovementRep = GetReplicatedMovement_Mutable();
		newMovement.CopyTo(MovementRep);
		OnRep_ReplicatedMovement();
	}
}

void AGrippableStaticMeshActor::OnRep_AttachmentReplication()
{
	if (bAllowIgnoringAttachOnOwner && (ClientAuthReplicationData.bIsCurrentlyClientAuth || ShouldWeSkipAttachmentReplication()))
	//if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication())
	{
		return;
	}

	if (AttachmentWeldReplication.AttachParent)
	{
		if (RootComponent)
		{
			USceneComponent* AttachParentComponent = (AttachmentWeldReplication.AttachComponent ? AttachmentWeldReplication.AttachComponent : AttachmentWeldReplication.AttachParent->GetRootComponent());

			if (AttachParentComponent)
			{
				RootComponent->SetRelativeLocation_Direct(AttachmentWeldReplication.LocationOffset);
				RootComponent->SetRelativeRotation_Direct(AttachmentWeldReplication.RotationOffset);
				RootComponent->SetRelativeScale3D_Direct(AttachmentWeldReplication.RelativeScale3D);

				// If we're already attached to the correct Parent and Socket, then the update must be position only.
				// AttachToComponent would early out in this case.
				// Note, we ignore the special case for simulated bodies in AttachToComponent as AttachmentReplication shouldn't get updated
				// if the body is simulated (see AActor::GatherMovement).
				const bool bAlreadyAttached = (AttachParentComponent == RootComponent->GetAttachParent() && AttachmentWeldReplication.AttachSocket == RootComponent->GetAttachSocketName() && AttachParentComponent->GetAttachChildren().Contains(RootComponent));
				if (bAlreadyAttached)
				{
					// Note, this doesn't match AttachToComponent, but we're assuming it's safe to skip physics (see comment above).
					RootComponent->UpdateComponentToWorld(EUpdateTransformFlags::SkipPhysicsUpdate, ETeleportType::None);
				}
				else
				{
					FAttachmentTransformRules attachRules = FAttachmentTransformRules::KeepRelativeTransform;
					attachRules.bWeldSimulatedBodies = AttachmentWeldReplication.bIsWelded;
					RootComponent->AttachToComponent(AttachParentComponent, attachRules, AttachmentWeldReplication.AttachSocket);
				}
			}
		}
	}
	else
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

		// Handle the case where an object was both detached and moved on the server in the same frame.
		// Calling this extraneously does not hurt but will properly fire events if the movement state changed while attached.
		// This is needed because client side movement is ignored when attached
		if (IsReplicatingMovement())
		{
			OnRep_ReplicatedMovement();
		}
	}
}

void AGrippableStaticMeshActor::OnRep_ReplicateMovement()
{
	if (bAllowIgnoringAttachOnOwner && (ClientAuthReplicationData.bIsCurrentlyClientAuth || ShouldWeSkipAttachmentReplication()))
	//if (bAllowIgnoringAttachOnOwner && (ClientAuthReplicationData.bIsCurrentlyClientAuth || ShouldWeSkipAttachmentReplication()))
	{
		return;
	}

	if (RootComponent)
	{
		const FRepAttachment ReplicationAttachment = GetAttachmentReplication();
		if (!ReplicationAttachment.AttachParent)
		{
			const FRepMovement& RepMove = GetReplicatedMovement();

			// This "fix" corrects the simulation state not replicating over correctly
			// If you turn off movement replication, simulate an object, turn movement replication back on and un-simulate, it never knows the difference
			// This change ensures that it is checking against the current state
			if (RootComponent->IsSimulatingPhysics() != RepMove.bRepPhysics)//SavedbRepPhysics != ReplicatedMovement.bRepPhysics)
			{
				// Turn on/off physics sim to match server.
				SyncReplicatedPhysicsSimulation();

				// It doesn't really hurt to run it here, the super can call it again but it will fail out as they already match
			}
		}
	}

	Super::OnRep_ReplicateMovement();
}

void AGrippableStaticMeshActor::OnRep_ReplicatedMovement()
{
	if (bAllowIgnoringAttachOnOwner && (ClientAuthReplicationData.bIsCurrentlyClientAuth || ShouldWeSkipAttachmentReplication()))
	//if (ClientAuthReplicationData.bIsCurrentlyClientAuth && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

void AGrippableStaticMeshActor::PostNetReceivePhysicState()
{
	if (bAllowIgnoringAttachOnOwner && (ClientAuthReplicationData.bIsCurrentlyClientAuth || ShouldWeSkipAttachmentReplication()))
	//if ((ClientAuthReplicationData.bIsCurrentlyClientAuth || VRGripInterfaceSettings.bIsHeld) && bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::PostNetReceivePhysicState();
}

void AGrippableStaticMeshActor::MarkComponentsAsPendingKill()
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

void AGrippableStaticMeshActor::PreDestroyFromReplication()
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
		// We only call this on our interfaced components since they are the only ones that should implement grip scripts
		if (ActorComp && !ActorComp->IsPendingKill() && ActorComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			ActorComp->PreDestroyFromReplication();
	}

	GripLogicScripts.Empty();
}

void AGrippableStaticMeshActor::BeginDestroy()
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

void AGrippableStaticMeshActor::GetSubobjectsWithStableNamesForNetworking(TArray<UObject*>& ObjList)
{
	Super::GetSubobjectsWithStableNamesForNetworking(ObjList);

	if (bReplicateGripScripts)
	{
		for (int32 i = 0; i < GripLogicScripts.Num(); ++i)
		{
			if (UObject* SubObject = GripLogicScripts[i])
			{
				ObjList.Add(SubObject);
			}
		}
	}
}