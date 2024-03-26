// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/GrippableSphereComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GrippableSphereComponent)

#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "GripScripts/VRGripScriptBase.h"
#include "Net/UnrealNetwork.h"

#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif


  //=============================================================================
UGrippableSphereComponent::UGrippableSphereComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VRGripInterfaceSettings.bDenyGripping = false;
	VRGripInterfaceSettings.OnTeleportBehavior = EGripInterfaceTeleportBehavior::DropOnTeleport;
	VRGripInterfaceSettings.bSimulateOnDrop = true;
	VRGripInterfaceSettings.SlotDefaultGripType = EGripCollisionType::ManipulationGrip;
	VRGripInterfaceSettings.FreeDefaultGripType = EGripCollisionType::ManipulationGrip;
	VRGripInterfaceSettings.SecondaryGripType = ESecondaryGripType::SG_None;
	VRGripInterfaceSettings.MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
	VRGripInterfaceSettings.LateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOff;
	VRGripInterfaceSettings.ConstraintStiffness = 1500.0f;
	VRGripInterfaceSettings.ConstraintDamping = 200.0f;
	VRGripInterfaceSettings.ConstraintBreakDistance = 100.0f;
	VRGripInterfaceSettings.SecondarySlotRange = 20.0f;
	VRGripInterfaceSettings.PrimarySlotRange = 20.0f;

	bReplicateMovement = false;
	//this->bReplicates = true;

	VRGripInterfaceSettings.bIsHeld = false;
	bRepGripSettingsAndGameplayTags = true;
	bReplicateGripScripts = false;

	// #TODO we can register them maybe in the future
	// Don't use the replicated list, use our custom replication instead
	bReplicateUsingRegisteredSubObjectList = false;
}

void UGrippableSphereComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// For std properties
	FDoRepLifetimeParams PushModelParams{ COND_None, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UGrippableSphereComponent, bReplicateGripScripts, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGrippableSphereComponent, bRepGripSettingsAndGameplayTags, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGrippableSphereComponent, bReplicateMovement, PushModelParams);

	// For properties with special conditions
	FDoRepLifetimeParams PushModelParamsWithCondition{ COND_Custom, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UGrippableSphereComponent, GripLogicScripts, PushModelParamsWithCondition);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGrippableSphereComponent, VRGripInterfaceSettings, PushModelParamsWithCondition);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGrippableSphereComponent, GameplayTags, PushModelParamsWithCondition);
}

void UGrippableSphereComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(UGrippableSphereComponent, VRGripInterfaceSettings, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(UGrippableSphereComponent, GameplayTags, bRepGripSettingsAndGameplayTags);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(UGrippableSphereComponent, GripLogicScripts, bReplicateGripScripts);

	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeScale3D, bReplicateMovement);
}

bool UGrippableSphereComponent::ReplicateSubobjects(UActorChannel* Channel, class FOutBunch *Bunch, FReplicationFlags *RepFlags)
{
	bool WroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	if (bReplicateGripScripts && !IsUsingRegisteredSubObjectList())
	{
		for (UVRGripScriptBase* Script : GripLogicScripts)
		{
			if (Script && IsValid(Script))
			{
				WroteSomething |= Channel->ReplicateSubobject(Script, *Bunch, *RepFlags);
			}
		}
	}

	return WroteSomething;
}

//=============================================================================
UGrippableSphereComponent::~UGrippableSphereComponent()
{
}

void UGrippableSphereComponent::BeginPlay()
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

	bOriginalReplicatesMovement = bReplicateMovement;
}

void UGrippableSphereComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Call the base class 
	Super::EndPlay(EndPlayReason);

	// Call all grip scripts begin play events so they can perform any needed logic
	for (UVRGripScriptBase* Script : GripLogicScripts)
	{
		if (Script)
		{
			Script->EndPlay(EndPlayReason);
		}
	}
}

void UGrippableSphereComponent::SetDenyGripping(bool bDenyGripping)
{
	VRGripInterfaceSettings.bDenyGripping = bDenyGripping;
}

void UGrippableSphereComponent::SetGripPriority(int NewGripPriority)
{
	VRGripInterfaceSettings.AdvancedGripSettings.GripPriority = NewGripPriority;
}

void UGrippableSphereComponent::TickGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime) {}
void UGrippableSphereComponent::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void UGrippableSphereComponent::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) { }
void UGrippableSphereComponent::OnChildGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) {}
void UGrippableSphereComponent::OnChildGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed) {}
void UGrippableSphereComponent::OnSecondaryGrip_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation) { OnSecondaryGripAdded.Broadcast(GripOwningController, GripInformation); }
void UGrippableSphereComponent::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent* GripOwningController, USceneComponent* ReleasingSecondaryGripComponent, const FBPActorGripInformation& GripInformation) { OnSecondaryGripRemoved.Broadcast(GripOwningController, GripInformation); }
void UGrippableSphereComponent::OnUsed_Implementation() {}
void UGrippableSphereComponent::OnEndUsed_Implementation() {}
void UGrippableSphereComponent::OnSecondaryUsed_Implementation() {}
void UGrippableSphereComponent::OnEndSecondaryUsed_Implementation() {}
void UGrippableSphereComponent::OnInput_Implementation(FKey Key, EInputEvent KeyEvent) {}
bool UGrippableSphereComponent::RequestsSocketing_Implementation(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform) { return false; }

bool UGrippableSphereComponent::DenyGripping_Implementation(UGripMotionControllerComponent * GripInitiator)
{
	return VRGripInterfaceSettings.bDenyGripping;
}

EGripInterfaceTeleportBehavior UGrippableSphereComponent::TeleportBehavior_Implementation()
{
	return VRGripInterfaceSettings.OnTeleportBehavior;
}

bool UGrippableSphereComponent::SimulateOnDrop_Implementation()
{
	return VRGripInterfaceSettings.bSimulateOnDrop;
}

EGripCollisionType UGrippableSphereComponent::GetPrimaryGripType_Implementation(bool bIsSlot)
{
	return bIsSlot ? VRGripInterfaceSettings.SlotDefaultGripType : VRGripInterfaceSettings.FreeDefaultGripType;
}

ESecondaryGripType UGrippableSphereComponent::SecondaryGripType_Implementation()
{
	return VRGripInterfaceSettings.SecondaryGripType;
}

EGripMovementReplicationSettings UGrippableSphereComponent::GripMovementReplicationType_Implementation()
{
	return VRGripInterfaceSettings.MovementReplicationType;
}

EGripLateUpdateSettings UGrippableSphereComponent::GripLateUpdateSetting_Implementation()
{
	return VRGripInterfaceSettings.LateUpdateSetting;
}

void UGrippableSphereComponent::GetGripStiffnessAndDamping_Implementation(float &GripStiffnessOut, float &GripDampingOut)
{
	GripStiffnessOut = VRGripInterfaceSettings.ConstraintStiffness;
	GripDampingOut = VRGripInterfaceSettings.ConstraintDamping;
}

FBPAdvGripSettings UGrippableSphereComponent::AdvancedGripSettings_Implementation()
{
	return VRGripInterfaceSettings.AdvancedGripSettings;
}

float UGrippableSphereComponent::GripBreakDistance_Implementation()
{
	return VRGripInterfaceSettings.ConstraintBreakDistance;
}

void UGrippableSphereComponent::ClosestGripSlotInRange_Implementation(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent * CallingController, FName OverridePrefix)
{
	if (OverridePrefix.IsNone())
		bSecondarySlot ? OverridePrefix = "VRGripS" : OverridePrefix = "VRGripP";

	UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(OverridePrefix, this, WorldLocation, bSecondarySlot ? VRGripInterfaceSettings.SecondarySlotRange : VRGripInterfaceSettings.PrimarySlotRange, bHadSlotInRange, SlotWorldTransform, SlotName, CallingController);
}

bool UGrippableSphereComponent::AllowsMultipleGrips_Implementation()
{
	return VRGripInterfaceSettings.bAllowMultipleGrips;
}

void UGrippableSphereComponent::IsHeld_Implementation(TArray<FBPGripPair> & HoldingControllers, bool & bIsHeld)
{
	HoldingControllers = VRGripInterfaceSettings.HoldingControllers;
	bIsHeld = VRGripInterfaceSettings.bIsHeld;
}

void UGrippableSphereComponent::Native_NotifyThrowGripDelegates(UGripMotionControllerComponent* Controller, bool bGripped, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
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

void UGrippableSphereComponent::SetHeld_Implementation(UGripMotionControllerComponent * HoldingController, uint8 GripID, bool bIsHeld)
{
	if (bIsHeld)
	{
		if (VRGripInterfaceSettings.MovementReplicationType != EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			if (!VRGripInterfaceSettings.bIsHeld)
				bOriginalReplicatesMovement = bReplicateMovement;
			bReplicateMovement = false;
		}

		VRGripInterfaceSettings.bWasHeld = true;
		VRGripInterfaceSettings.HoldingControllers.AddUnique(FBPGripPair(HoldingController, GripID));
	}
	else
	{
		if (VRGripInterfaceSettings.MovementReplicationType != EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			bReplicateMovement = bOriginalReplicatesMovement;
		}

		VRGripInterfaceSettings.HoldingControllers.Remove(FBPGripPair(HoldingController, GripID));
	}

	VRGripInterfaceSettings.bIsHeld = VRGripInterfaceSettings.HoldingControllers.Num() > 0;
}

/*FBPInteractionSettings UGrippableSphereComponent::GetInteractionSettings_Implementation()
{
	return VRGripInterfaceSettings.InteractionSettings;
}*/


bool UGrippableSphereComponent::GetGripScripts_Implementation(TArray<UVRGripScriptBase*> & ArrayReference)
{
	ArrayReference = GripLogicScripts;
	return GripLogicScripts.Num() > 0;
}

void UGrippableSphereComponent::PreDestroyFromReplication()
{
	Super::PreDestroyFromReplication();

	// Destroy any sub-objects we created
	for (int32 i = 0; i < GripLogicScripts.Num(); ++i)
	{
		if (UObject *SubObject = GripLogicScripts[i])
		{
			SubObject->PreDestroyFromReplication();
			SubObject->MarkAsGarbage();
		}
	}

	GripLogicScripts.Empty();
}

void UGrippableSphereComponent::GetSubobjectsWithStableNamesForNetworking(TArray<UObject*> &ObjList)
{
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

void UGrippableSphereComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// Call the super at the end, after we've done what we needed to do
	Super::OnComponentDestroyed(bDestroyingHierarchy);

	// Don't set these in editor preview window and the like, it causes saving issues
	if (UWorld * World = GetWorld())
	{
		EWorldType::Type WorldType = World->WorldType;
		if (WorldType == EWorldType::Editor || WorldType == EWorldType::EditorPreview)
		{
			return;
		}
	}

	for (int32 i = 0; i < GripLogicScripts.Num(); i++)
	{
		if (UObject *SubObject = GripLogicScripts[i])
		{
			SubObject->MarkAsGarbage();
		}
	}

	GripLogicScripts.Empty();
}


/////////////////////////////////////////////////
//- Push networking getter / setter functions
/////////////////////////////////////////////////

void UGrippableSphereComponent::SetReplicateGripScripts(bool bNewReplicateGripScripts)
{
	bReplicateGripScripts = bNewReplicateGripScripts;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGrippableSphereComponent, bReplicateGripScripts, this);
#endif
}

TArray<TObjectPtr<UVRGripScriptBase>>& UGrippableSphereComponent::GetGripLogicScripts()
{
#if WITH_PUSH_MODEL
	if (bReplicateGripScripts)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UGrippableSphereComponent, GripLogicScripts, this);
	}
#endif

	return GripLogicScripts;
}

void UGrippableSphereComponent::SetRepGripSettingsAndGameplayTags(bool bNewRepGripSettingsAndGameplayTags)
{
	bRepGripSettingsAndGameplayTags = bNewRepGripSettingsAndGameplayTags;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGrippableSphereComponent, bRepGripSettingsAndGameplayTags, this);
#endif
}

void UGrippableSphereComponent::SetReplicateMovement(bool bNewReplicateMovement)
{
	bReplicateMovement = bNewReplicateMovement;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGrippableSphereComponent, bReplicateMovement, this);
#endif
}

FBPInterfaceProperties& UGrippableSphereComponent::GetVRGripInterfaceSettings(bool bMarkDirty)
{
#if WITH_PUSH_MODEL
	if (bMarkDirty && bRepGripSettingsAndGameplayTags)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UGrippableSphereComponent, VRGripInterfaceSettings, this);
	}
#endif

	return VRGripInterfaceSettings;
}

FGameplayTagContainer& UGrippableSphereComponent::GetGameplayTags()
{
#if WITH_PUSH_MODEL
	if (bRepGripSettingsAndGameplayTags)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UGrippableSphereComponent, GameplayTags, this);
	}
#endif

	return GameplayTags;
}

/////////////////////////////////////////////////
//- End Push networking getter / setter functions
/////////////////////////////////////////////////