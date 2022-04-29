// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/GrippableActor.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsReplication.h"
#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif


  //=============================================================================
AGrippableActor::AGrippableActor(const FObjectInitializer& ObjectInitializer)
	: Super()
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

	// Default replication on for multiplayer
	//this->bNetLoadOnClient = false;
	SetReplicatingMovement(true);
	bReplicates = true;
	
	bRepGripSettingsAndGameplayTags = true;
	bAllowIgnoringAttachOnOwner = true;
	bReplicateGripScripts = false;

	// Setting a minimum of every 3rd frame (VR 90fps) for replication consideration
	// Otherwise we will get some massive slow downs if the replication is allowed to hit the 2 per second minimum default
	MinNetUpdateFrequency = 30.0f;
}

void AGrippableActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AGrippableActor, GripLogicScripts, COND_Custom);
	DOREPLIFETIME(AGrippableActor, bReplicateGripScripts);
	DOREPLIFETIME(AGrippableActor, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME(AGrippableActor, bAllowIgnoringAttachOnOwner);
	DOREPLIFETIME(AGrippableActor, ClientAuthReplicationData);
	DOREPLIFETIME_CONDITION(AGrippableActor, VRGripInterfaceSettings, COND_Custom);
	DOREPLIFETIME_CONDITION(AGrippableActor, GameplayTags, COND_Custom);

	DISABLE_REPLICATED_PRIVATE_PROPERTY(AActor, AttachmentReplication);

	FDoRepLifetimeParams AttachmentReplicationParams{ COND_Custom, REPNOTIFY_Always, /*bIsPushBased=*/true };
	DOREPLIFETIME_WITH_PARAMS_FAST(AGrippableActor, AttachmentWeldReplication, AttachmentReplicationParams);
}

void AGrippableActor::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableActor, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableActor, GameplayTags, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableActor, GripLogicScripts, bReplicateGripScripts);

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
	DOREPLIFETIME_ACTIVE_OVERRIDE(AGrippableActor, AttachmentWeldReplication, RootComponent && !RootComponent->GetIsReplicated());

	// Don't need to replicate AttachmentReplication if the root component replicates, because it already handles it.
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(AActor, AttachmentReplication, RootComponent && !RootComponent->GetIsReplicated());


#if WITH_PUSH_MODEL
	if (UNLIKELY(OldAttachParent != AttachmentWeldReplication.AttachParent || OldAttachComponent != AttachmentWeldReplication.AttachComponent))
	{
		//MARK_PROPERTY_DIRTY_FROM_NAME(AGrippableActor, AttachmentWeldReplication, this);
	}
#endif

	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass());
	if (BPClass != nullptr)	
	{
		BPClass->InstancePreReplication(this, ChangedPropertyTracker);
	}
}

void AGrippableActor::GatherCurrentMovement()
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
			//MARK_PROPERTY_DIRTY_FROM_NAME(AGrippableActor, AttachmentWeldReplication, this);
		}
#endif
	}
}

void AGrippableActor::OnRep_AttachmentReplication()
{
	if (bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication())
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

bool AGrippableActor::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags)
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
AGrippableActor::~AGrippableActor()
{
}

void AGrippableActor::BeginPlay()
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


void AGrippableActor::SetDenyGripping(bool bDenyGripping)
{
	VRGripInterfaceSettings.bDenyGripping = bDenyGripping;
}

void AGrippableActor::SetGripPriority(int NewGripPriority)
{
	VRGripInterfaceSettings.AdvancedGripSettings.GripPriority = NewGripPriority;
}

void AGrippableActor::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) {}
void AGrippableActor::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void AGrippableActor::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) { }
void AGrippableActor::OnChildGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void AGrippableActor::OnChildGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void AGrippableActor::OnSecondaryGrip_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation) { OnSecondaryGripAdded.Broadcast(GripOwningController, GripInformation); }
void AGrippableActor::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* ReleasingSecondaryGripComponent, const FBPActorGripInformation& GripInformation) { OnSecondaryGripRemoved.Broadcast(GripOwningController, GripInformation); }
void AGrippableActor::OnUsed_Implementation() {}
void AGrippableActor::OnEndUsed_Implementation() {}
void AGrippableActor::OnSecondaryUsed_Implementation() {}
void AGrippableActor::OnEndSecondaryUsed_Implementation() {}
void AGrippableActor::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
bool AGrippableActor::RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) { return false; }

bool AGrippableActor::DenyGripping_Implementation(UGripMotionControllerComponent * GripInitiator)
{
	return VRGripInterfaceSettings.bDenyGripping;
}


EGripInterfaceTeleportBehavior AGrippableActor::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool AGrippableActor::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripCollisionType AGrippableActor::GetPrimaryGripType_Implementation(bool bIsSlot)
{
	return bIsSlot ? VRGripInterfaceSettings.SlotDefaultGripType : VRGripInterfaceSettings.FreeDefaultGripType;
}

ESecondaryGripType AGrippableActor::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}

EGripMovementReplicationSettings AGrippableActor::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings AGrippableActor::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

void AGrippableActor::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = VRGripInterfaceSettings.ConstraintStiffness;
	GripDampingOut = VRGripInterfaceSettings.ConstraintDamping;
}

FBPAdvGripSettings AGrippableActor::AdvancedGripSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedGripSettings;
}

float AGrippableActor::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void AGrippableActor::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(OverridePrefix, this, WorldLocation, bSecondarySlot ? VRGripInterfaceSettings.SecondarySlotRange : VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform, SlotName, CallingController);
}

bool AGrippableActor::AllowsMultipleGrips_Implementation()
{
	return VRGripInterfaceSettings.bAllowMultipleGrips;
}

void AGrippableActor::IsHeld_Implementation(TArray<FBPGripPair> & HoldingControllers, bool & bIsHeld)
{
	HoldingControllers = VRGripInterfaceSettings.HoldingControllers;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

bool AGrippableActor::AddToClientReplicationBucket()
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

bool AGrippableActor::RemoveFromClientReplicationBucket()
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth)
	{
		GetWorld()->GetSubsystem<UBucketUpdateSubsystem>()->RemoveObjectFromBucketByFunctionName(this, FName(TEXT("PollReplicationEvent")));
		CeaseReplicationBlocking();
		return true;
	}

	return false;
}

void AGrippableActor::Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
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

void AGrippableActor::SetHeld_Implementation(UGripMotionControllerComponent * HoldingController, uint8 GripID, bool bIsHeld)
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

bool AGrippableActor::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	ArrayReference = GripLogicScripts;
	return GripLogicScripts.Num() > 0;
}

/*FBPInteractionSettings AGrippableActor::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}*/

bool AGrippableActor::PollReplicationEvent()
{
	if (!ClientAuthReplicationData.bIsCurrentlyClientAuth || !this->HasLocalNetOwner() || VRGripInterfaceSettings.bIsHeld)
		return false; // Tell the bucket subsystem to remove us from consideration

	UWorld* OurWorld = GetWorld();
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
		AActor* tempOwner = TopOwner->GetOwner();

		// I have an owner so search that for the top owner
		while (tempOwner)
		{
			TopOwner = tempOwner;
			tempOwner = TopOwner->GetOwner();
		}

		if (APlayerController * PlayerController = Cast<APlayerController>(TopOwner))
		{
			if (APlayerState * PlayerState = PlayerController->PlayerState)
			{
				if (ClientAuthReplicationData.ResetReplicationHandle.IsValid())
				{
					OurWorld->GetTimerManager().ClearTimer(ClientAuthReplicationData.ResetReplicationHandle);
				}

				// Lets clamp the ping to a min / max value just in case
				float clampedPing = FMath::Clamp(PlayerState->ExactPing * 0.001f, 0.0f, 1000.0f);
				OurWorld->GetTimerManager().SetTimer(ClientAuthReplicationData.ResetReplicationHandle, this, &AGrippableActor::CeaseReplicationBlocking, clampedPing, false);
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

void AGrippableActor::CeaseReplicationBlocking()
{
	if (ClientAuthReplicationData.bIsCurrentlyClientAuth)
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

void AGrippableActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

bool AGrippableActor::Server_EndClientAuthReplication_Validate()
{
	return true;
}

void AGrippableActor::Server_EndClientAuthReplication_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (FPhysicsReplication* PhysicsReplication = PhysScene->GetPhysicsReplication())
			{
				if (UPrimitiveComponent* RootPrim = Cast<UPrimitiveComponent>(GetRootComponent()))
				{
					PhysicsReplication->RemoveReplicatedTarget(RootPrim);
				}
			}
		}
	}
}

bool AGrippableActor::Server_GetClientAuthReplication_Validate(const FRepMovementVR & newMovement)
{
	return true;
}

void AGrippableActor::Server_GetClientAuthReplication_Implementation(const FRepMovementVR & newMovement)
{
	if (!VRGripInterfaceSettings.bIsHeld)
	{
		FRepMovement& MovementRep = GetReplicatedMovement_Mutable();
		newMovement.CopyTo(MovementRep);
		OnRep_ReplicatedMovement();
	}
}

void AGrippableActor::OnRep_ReplicateMovement()
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

void AGrippableActor::OnRep_ReplicatedMovement()
{
	if (bAllowIgnoringAttachOnOwner && (ClientAuthReplicationData.bIsCurrentlyClientAuth || ShouldWeSkipAttachmentReplication()))
	//if (ClientAuthReplicationData.bIsCurrentlyClientAuth && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::OnRep_ReplicatedMovement();
}

void AGrippableActor::PostNetReceivePhysicState()
{
	if (bAllowIgnoringAttachOnOwner && (ClientAuthReplicationData.bIsCurrentlyClientAuth || ShouldWeSkipAttachmentReplication()))
	//if ((ClientAuthReplicationData.bIsCurrentlyClientAuth || VRGripInterfaceSettings.bIsHeld) && bAllowIgnoringAttachOnOwner && ShouldWeSkipAttachmentReplication(false))
	{
		return;
	}

	Super::PostNetReceivePhysicState();
}
// This isn't called very many places but it does come up
void AGrippableActor::MarkComponentsAsPendingKill()
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
void AGrippableActor::PreDestroyFromReplication()
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
void AGrippableActor::BeginDestroy()
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

void AGrippableActor::GetSubobjectsWithStableNamesForNetworking(TArray<UObject*>& ObjList)
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