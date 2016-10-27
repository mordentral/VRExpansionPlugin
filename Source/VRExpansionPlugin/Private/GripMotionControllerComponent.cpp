// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "GripMotionControllerComponent.h"
#include "Net/UnrealNetwork.h"
#include "KismetMathLibrary.h"
#include "PrimitiveSceneInfo.h"

#include "PhysicsPublic.h"

#if WITH_PHYSX
#include "PhysXSupport.h"
#endif // WITH_PHYSX

//#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 11
//#else
#include "Features/IModularFeatures.h"
//#endif

DEFINE_LOG_CATEGORY(LogVRMotionController);

namespace {
	/** This is to prevent destruction of motion controller components while they are
	in the middle of being accessed by the render thread */
	FCriticalSection CritSect;

} // anonymous namespace

static const auto CVarEnableMotionControllerLateUpdate = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.EnableMotionControllerLateUpdate"));


  //=============================================================================
UGripMotionControllerComponent::UGripMotionControllerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = true;

	PlayerIndex = 0;
	Hand = EControllerHand::Left;
	bDisableLowLatencyUpdate = false;
	bHasAuthority = false;
	bUseWithoutTracking = false;
	bAutoActivate = true;

	this->SetIsReplicated(true);

	// Default 100 htz update rate, same as the 100htz update rate of rep_notify, will be capped to 90/45 though because of vsync on HMD
	//bReplicateControllerTransform = true;
	ControllerNetUpdateRate = 100.0f; // 100 htz is default
	ControllerNetUpdateCount = 0.0f;

}

//=============================================================================
UGripMotionControllerComponent::~UGripMotionControllerComponent()
{
	if (ViewExtension.IsValid())
	{
		{
			// This component could be getting accessed from the render thread so it needs to wait
			// before clearing MotionControllerComponent and allowing the destructor to continue
			FScopeLock ScopeLock(&CritSect);
			ViewExtension->MotionControllerComponent = NULL;
		}

		if (GEngine)
		{
			GEngine->ViewExtensions.Remove(ViewExtension);
		}
	}
	ViewExtension.Reset();
}

void UGripMotionControllerComponent::OnUnregister()
{
	for (int i = 0; i < GrippedActors.Num(); i++)
	{
		DestroyPhysicsHandle(GrippedActors[i]);

		switch (GrippedActors[i].GripTargetType)
		{
		case EGripTargetType::ActorGrip:
		case EGripTargetType::InteractibleActorGrip:
		{DropActor(GrippedActors[i].Actor, false); }break;

		case EGripTargetType::ComponentGrip:
		case EGripTargetType::InteractibleComponentGrip:
		{DropComponent(GrippedActors[i].Component, false); }break;
		}
		
	}
	GrippedActors.Empty();

	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		DestroyPhysicsHandle(PhysicsGrips[i].SceneIndex, &PhysicsGrips[i].HandleData, &PhysicsGrips[i].KinActorData);
	}
	PhysicsGrips.Empty();

	Super::OnUnregister();
}

FBPActorPhysicsHandleInformation * UGripMotionControllerComponent::GetPhysicsGrip(const FBPActorGripInformation & GripInfo)
{
	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		if (PhysicsGrips[i] == GripInfo)
			return &PhysicsGrips[i];
	}
	return nullptr;
}


bool UGripMotionControllerComponent::GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo, int & index)
{
	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		if (PhysicsGrips[i] == GripInfo)
		{
			index = i;
			return true;
		}
	}

	return false;
}

FBPActorPhysicsHandleInformation * UGripMotionControllerComponent::CreatePhysicsGrip(const FBPActorGripInformation & GripInfo)
{
	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		if (PhysicsGrips[i] == GripInfo)
		{
			DestroyPhysicsHandle(PhysicsGrips[i].SceneIndex, &PhysicsGrips[i].HandleData, &PhysicsGrips[i].KinActorData);
			return &PhysicsGrips[i];
		}
	}

	FBPActorPhysicsHandleInformation NewInfo;
	NewInfo.Actor = GripInfo.Actor;
	NewInfo.Component = GripInfo.Component;

	int index = PhysicsGrips.Add(NewInfo);

	return &PhysicsGrips[index];
}


//=============================================================================
void UGripMotionControllerComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{

	// I am skipping the Scene and Primitive component replication here
	// Generally components aren't set to replicate anyway and I need it to NOT pass the Relative position through the network
	// There isn't much in the scene component to replicate anyway and primitive doesn't have ANY replicated variables
	// Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Skipping the owner with this as the owner will use the controllers location directly
	DOREPLIFETIME_CONDITION(UGripMotionControllerComponent, ReplicatedControllerTransform, COND_SkipOwner);
	DOREPLIFETIME(UGripMotionControllerComponent, GrippedActors);
	DOREPLIFETIME(UGripMotionControllerComponent, ControllerNetUpdateRate);
//	DOREPLIFETIME(UGripMotionControllerComponent, bReplicateControllerTransform);
}

void UGripMotionControllerComponent::Server_SendControllerTransform_Implementation(FBPVRComponentPosRep NewTransform)
{
	// Store new transform and trigger OnRep_Function
	ReplicatedControllerTransform = NewTransform;

	// Server should no longer call this RPC itself, but if is using non tracked then it will so keeping auth check
	if(!bHasAuthority)
		OnRep_ReplicatedControllerTransform();
}

bool UGripMotionControllerComponent::Server_SendControllerTransform_Validate(FBPVRComponentPosRep NewTransform)
{
	return true;
	// Optionally check to make sure that player is inside of their bounds and deny it if they aren't?
}

void UGripMotionControllerComponent::FViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (!MotionControllerComponent)
	{
		return;
	}
	FScopeLock ScopeLock(&CritSect);

	if (MotionControllerComponent->bDisableLowLatencyUpdate || !CVarEnableMotionControllerLateUpdate->GetValueOnGameThread())
	{
		return;
	}	

	LateUpdatePrimitives.Reset();
	GatherLateUpdatePrimitives(MotionControllerComponent, LateUpdatePrimitives);

	/*
	Add additional late updates registered to this controller that aren't children and aren't gripped
	This array is editable in blueprint and can be used for things like arms or the like.
	*/
	for (UPrimitiveComponent* primComp : MotionControllerComponent->AdditionalLateUpdateComponents)
	{
		if (primComp)
			GatherLateUpdatePrimitives(primComp, LateUpdatePrimitives);
	}	

	// Loop through gripped actors
	for (FBPActorGripInformation actor : MotionControllerComponent->GrippedActors)
	{
		// Skip actors that are colliding if turning off late updates during collision.
		// Also skip turning off late updates for SweepWithPhysics, as it should always be locked to the hand
		
		// Don't allow late updates with server sided movement, there is no point
		if (actor.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement)
			continue;

		switch (actor.GripLateUpdateSetting)
		{
		case EGripLateUpdateSettings::LateUpdatesAlwaysOff:
		{
			continue;
		}break;
		case EGripLateUpdateSettings::NotWhenColliding:
		{
			if (actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics)
				continue;
		}break;
		case EGripLateUpdateSettings::NotWhenDoubleGripping:
		{
			if (actor.bHasSecondaryAttachment)
				continue;
		}break;
		case EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping:
		{
			if (
				(actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics) ||
				(actor.bHasSecondaryAttachment)
				)
			{
				continue;
			}
		}break;
		case EGripLateUpdateSettings::LateUpdatesAlwaysOn:
		default:
		{}break;
		}
		
		// Get late update primitives
		switch (actor.GripTargetType)
		{
		case EGripTargetType::ActorGrip:
		case EGripTargetType::InteractibleActorGrip:
		{
			if (actor.Actor)
			{
				if (USceneComponent * rootComponent = actor.Actor->GetRootComponent())
				{
					GatherLateUpdatePrimitives(rootComponent, LateUpdatePrimitives);
				}
			}
		
		}break;

		case EGripTargetType::ComponentGrip:
		case EGripTargetType::InteractibleComponentGrip:
		{
			if (actor.Component)
			{
				GatherLateUpdatePrimitives(actor.Component, LateUpdatePrimitives);
			}
		}break;
		}

	}
}

void UGripMotionControllerComponent::GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity)
{
	UPrimitiveComponent * primComp = Grip.Component;

	if (!primComp && Grip.Actor)
		primComp = Cast<UPrimitiveComponent>(Grip.Actor->GetRootComponent());

	if (!primComp)
	{
		AngularVelocity = FVector::ZeroVector;
		LinearVelocity = FVector::ZeroVector;
		return;
	}

	AngularVelocity = primComp->GetPhysicsAngularVelocity();
	LinearVelocity = primComp->GetPhysicsLinearVelocity();
}

void UGripMotionControllerComponent::GetGripByActor(FBPActorGripInformation &Grip, AActor * ActorToLookForGrip, TEnumAsByte<EBPVRResultSwitch::Type> &Result)
{
	if (!ActorToLookForGrip)
	{
		Result = EBPVRResultSwitch::OnFailed;
		return;
	}

	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i] == ActorToLookForGrip)
		{
			Grip = GrippedActors[i];
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::GetGripByComponent(FBPActorGripInformation &Grip, UPrimitiveComponent * ComponentToLookForGrip, TEnumAsByte<EBPVRResultSwitch::Type> &Result)
{
	if (!ComponentToLookForGrip)
	{
		Result = EBPVRResultSwitch::OnFailed;
		return;
	}

	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i] == ComponentToLookForGrip)
		{
			Grip = GrippedActors[i];
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripLateUpdateSetting(const FBPActorGripInformation &Grip, TEnumAsByte<EBPVRResultSwitch::Type> &Result, TEnumAsByte<EGripLateUpdateSettings> NewGripLateUpdateSetting)
{
	int fIndex = GrippedActors.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedActors[fIndex].GripLateUpdateSetting = NewGripLateUpdateSetting;
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripRelativeTransform(
	const FBPActorGripInformation &Grip,
	TEnumAsByte<EBPVRResultSwitch::Type> &Result,
	const FTransform & NewRelativeTransform
	)
{
	int fIndex = GrippedActors.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedActors[fIndex].RelativeTransform = NewRelativeTransform;
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripAdditionTransform(
	const FBPActorGripInformation &Grip,
	TEnumAsByte<EBPVRResultSwitch::Type> &Result,
	const FTransform & NewAdditionTransform, bool bMakeGripRelative
	)
{
	int fIndex = GrippedActors.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedActors[fIndex].AdditionTransform = CreateGripRelativeAdditionTransform(Grip, NewAdditionTransform, bMakeGripRelative);

		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}

	Result = EBPVRResultSwitch::OnFailed;
}

FTransform UGripMotionControllerComponent::CreateGripRelativeAdditionTransform_BP(
	const FBPActorGripInformation &GripToSample,
	const FTransform & AdditionTransform,
	bool bGripRelative
	)
{
	return CreateGripRelativeAdditionTransform(GripToSample, AdditionTransform, bGripRelative);
}

FTransform UGripMotionControllerComponent::CreateGripRelativeAdditionTransform(
	const FBPActorGripInformation &GripToSample,
	const FTransform & AdditionTransform,
	bool bGripRelative
	)
{

	FTransform FinalTransform;

	if (bGripRelative)
	{
		FinalTransform = FTransform(AdditionTransform.GetRotation(), GripToSample.RelativeTransform.GetRotation().RotateVector(AdditionTransform.GetLocation()), AdditionTransform.GetScale3D());
	}
	else
	{
		const FTransform PivotToWorld = FTransform(FQuat::Identity, GripToSample.RelativeTransform.GetLocation());
		const FTransform WorldToPivot = FTransform(FQuat::Identity, -GripToSample.RelativeTransform.GetLocation());

		// Create a transform from it
		FTransform RotationOffsetTransform(AdditionTransform.GetRotation(), FVector::ZeroVector);
		FinalTransform = FTransform(FQuat::Identity, AdditionTransform.GetLocation(), AdditionTransform.GetScale3D()) * WorldToPivot * RotationOffsetTransform * PivotToWorld;	
	}

	return FinalTransform;
}

bool UGripMotionControllerComponent::GripActor(
	AActor* ActorToGrip, 
	const FTransform &WorldOffset, 
	bool bWorldOffsetIsRelative, 
	FName OptionalSnapToSocketName, 
	TEnumAsByte<EGripCollisionType> GripCollisionType, 
	TEnumAsByte<EGripLateUpdateSettings> GripLateUpdateSetting,
	TEnumAsByte<EGripMovementReplicationSettings> GripMovementReplicationSetting,
	float GripStiffness, 
	float GripDamping
	)
{
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was called on the client side"));
		return false;
	}

	if (!ActorToGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an invalid actor"));
		return false;
	}

	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i].Actor == ActorToGrip)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already gripped actor"));
			return false;
		}
	}

	UPrimitiveComponent *root = Cast<UPrimitiveComponent>(ActorToGrip->GetRootComponent());

	if (!root)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController tried to grip an actor without a UPrimitiveComponent Root"));
		return false; // Need a primitive root
	}

	// Has to be movable to work
	if (root->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController tried to grip an actor set to static mobility and bAllowSetMobility is false"));
		return false; // It is not movable, can't grip it
	}

	bool bIsInteractible = false;

	if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(root))
			return false; // Interface is saying not to grip it right now

		bIsInteractible = IVRGripInterface::Execute_IsInteractible(root);
	}
	else if (ActorToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ActorToGrip))
			return false; // Interface is saying not to grip it right now

		bIsInteractible = IVRGripInterface::Execute_IsInteractible(ActorToGrip);
	}

	// So that events caused by sweep and the like will trigger correctly
	ActorToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.Actor = ActorToGrip;
	newActorGrip.bOriginalReplicatesMovement = ActorToGrip->bReplicateMovement;
	newActorGrip.Stiffness = GripStiffness;
	newActorGrip.Damping = GripDamping;

	// Ignore late update setting if it doesn't make sense with the grip
	switch(newActorGrip.GripCollisionType)
	{
	case EGripCollisionType::ManipulationGrip:
	{
		newActorGrip.GripLateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOff; // Late updates are bad for this grip
	}break;
	case EGripCollisionType::PhysicsOnly:
	case EGripCollisionType::SweepWithPhysics:
	{
		newActorGrip.GripLateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOn;
	}break;
	default:
	{
		newActorGrip.GripLateUpdateSetting = GripLateUpdateSetting;
	}break;
	}

	if (GripMovementReplicationSetting == EGripMovementReplicationSettings::KeepOriginalMovement)
	{
		if (ActorToGrip->bReplicateMovement)
		{
			newActorGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceServerSideMovement;
		}
		else
		{
			newActorGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
		}
	}
	else
		newActorGrip.GripMovementReplicationSetting = GripMovementReplicationSetting;

	if (bIsInteractible)
		newActorGrip.GripTargetType = EGripTargetType::InteractibleActorGrip;
	else
		newActorGrip.GripTargetType = EGripTargetType::ActorGrip;

	if (OptionalSnapToSocketName.IsValid() && root->DoesSocketExist(OptionalSnapToSocketName))
	{
		// I inverse it so that laying out the sockets makes sense
		FTransform sockTrans = root->GetSocketTransform(OptionalSnapToSocketName, ERelativeTransformSpace::RTS_Component);
		newActorGrip.RelativeTransform = sockTrans.Inverse();
		newActorGrip.RelativeTransform.SetScale3D(ActorToGrip->GetActorScale3D());
	}
	else if (bWorldOffsetIsRelative)
		newActorGrip.RelativeTransform = WorldOffset;
	else
		newActorGrip.RelativeTransform = WorldOffset.GetRelativeTransform(this->GetComponentTransform());

	GrippedActors.Add(newActorGrip);
	NotifyGrip(newActorGrip);

	return true;
}

bool UGripMotionControllerComponent::DropActor(AActor* ActorToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!ActorToDrop)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid actor"));
		return false;
	}
	
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side"));
		return false;
	}

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i] == ActorToDrop)
		{
			return DropGrip(GrippedActors[i], bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::GripComponent(
	UPrimitiveComponent* ComponentToGrip, 
	const FTransform &WorldOffset, 
	bool bWorldOffsetIsRelative, 
	FName OptionalSnapToSocketName, 
	TEnumAsByte<EGripCollisionType> GripCollisionType,
	TEnumAsByte<EGripLateUpdateSettings> GripLateUpdateSetting,
	TEnumAsByte<EGripMovementReplicationSettings> GripMovementReplicationSetting,
	float GripStiffness, 
	float GripDamping
	)
{
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was called on the client side"));
		return false;
	}

	if (!ComponentToGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an invalid or already gripped component"));
		return false;
	}


	for(int i=0; i<GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i].Component == ComponentToGrip)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already gripped component"));
			return false;
		}
	}


	// Has to be movable to work
	if (ComponentToGrip->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController tried to grip a component set to static mobility and bAllowSetMobility is false"));
		return false; // It is not movable, can't grip it
	}

	bool bIsInteractible = false;

	if (ComponentToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ComponentToGrip))
		return false; // Interface is saying not to grip it right now

		bIsInteractible = IVRGripInterface::Execute_IsInteractible(ComponentToGrip);
	}

	ComponentToGrip->IgnoreActorWhenMoving(this->GetOwner(), true);
	// So that events caused by sweep and the like will trigger correctly

	ComponentToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.Component = ComponentToGrip;
	
	if(ComponentToGrip->GetOwner())
		newActorGrip.bOriginalReplicatesMovement = ComponentToGrip->GetOwner()->bReplicateMovement;

	newActorGrip.Stiffness = GripStiffness;
	newActorGrip.Damping = GripDamping;

	if (bIsInteractible)
		newActorGrip.GripTargetType = EGripTargetType::InteractibleComponentGrip;
	else
		newActorGrip.GripTargetType = EGripTargetType::ComponentGrip;

	// Ignore late update setting if it doesn't make sense with the grip
	switch (newActorGrip.GripCollisionType)
	{
	case EGripCollisionType::ManipulationGrip:
	{
		newActorGrip.GripLateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOff; // Late updates are bad for this grip
	}break;
	case EGripCollisionType::PhysicsOnly:
	case EGripCollisionType::SweepWithPhysics:
	{
		newActorGrip.GripLateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOn;
	}break;
	default:
	{
		newActorGrip.GripLateUpdateSetting = GripLateUpdateSetting;
	}break;
	}


	if (GripMovementReplicationSetting == EGripMovementReplicationSettings::KeepOriginalMovement)
	{
		if (ComponentToGrip->GetOwner())
		{
			if (ComponentToGrip->GetOwner()->bReplicateMovement)
			{
				newActorGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceServerSideMovement;
			}
			else
			{
				newActorGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
			}
		}
		else
			newActorGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	}
	else
		newActorGrip.GripMovementReplicationSetting = GripMovementReplicationSetting;

	if (OptionalSnapToSocketName.IsValid() && ComponentToGrip->DoesSocketExist(OptionalSnapToSocketName))
	{
		// I inverse it so that laying out the sockets makes sense
		FTransform sockTrans = ComponentToGrip->GetSocketTransform(OptionalSnapToSocketName, ERelativeTransformSpace::RTS_Component);
		newActorGrip.RelativeTransform = sockTrans.Inverse();
		newActorGrip.RelativeTransform.SetScale3D(ComponentToGrip->GetComponentScale());
	}
	else if (bWorldOffsetIsRelative)
		newActorGrip.RelativeTransform = WorldOffset;
	else
		newActorGrip.RelativeTransform = WorldOffset.GetRelativeTransform(this->GetComponentTransform());

	GrippedActors.Add(newActorGrip);
	NotifyGrip(newActorGrip);

	return true;
}

bool UGripMotionControllerComponent::DropComponent(UPrimitiveComponent * ComponentToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side"));
		return false;
	}

	if (!ComponentToDrop)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid component"));
		return false;
	}

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i] == ComponentToDrop)
		{
			return DropGrip(GrippedActors[i], bSimulate,OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}
	return false;
}

bool UGripMotionControllerComponent::DropGrip(const FBPActorGripInformation &Grip, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side"));
		return false;
	}

	int FoundIndex = 0;
	if (!GrippedActors.Find(Grip, FoundIndex)) // This auto checks if Actor and Component are valid in the == operator
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid drop"));
		return false;
	}

	UPrimitiveComponent * PrimComp = GrippedActors[FoundIndex].Component;
	if (!PrimComp && GrippedActors[FoundIndex].Actor)
		PrimComp = Cast<UPrimitiveComponent>(GrippedActors[FoundIndex].Actor->GetRootComponent());

	if(!PrimComp)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid drop"));
		return false;
	}
	
	NotifyDrop(GrippedActors[FoundIndex], bSimulate);

	if (OptionalLinearVelocity != FVector::ZeroVector && OptionalAngularVelocity != FVector::ZeroVector)
	{
		PrimComp->SetPhysicsLinearVelocity(OptionalLinearVelocity);
		PrimComp->SetPhysicsAngularVelocity(OptionalAngularVelocity);
	}

	//GrippedActors.RemoveAt(FoundIndex);
		
	return true;
}

// No longer an RPC, now is called from RepNotify so that joining clients also correctly set up grips
void UGripMotionControllerComponent::NotifyGrip/*_Implementation*/(const FBPActorGripInformation &NewGrip)
{

	UPrimitiveComponent *root = NULL;

	switch (NewGrip.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	case EGripTargetType::InteractibleActorGrip:
	{
		if (NewGrip.Actor)
		{
			root = Cast<UPrimitiveComponent>(NewGrip.Actor->GetRootComponent());

			if (root)
			{
				root->SetEnableGravity(false);
				root->IgnoreActorWhenMoving(this->GetOwner(), true);
			}

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorAdd(NewGrip.Actor);
			}

			if (NewGrip.Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnGrip(NewGrip.Actor, this, NewGrip);
			}
		}
	}break;

	case EGripTargetType::ComponentGrip:
	case EGripTargetType::InteractibleComponentGrip:
	{
		if (NewGrip.Component)
		{
			root = NewGrip.Component;
			root->SetEnableGravity(false);
			root->IgnoreActorWhenMoving(this->GetOwner(), true);

			if (AActor* owner = root->GetOwner())
			{
				if (APawn* OwningPawn = Cast<APawn>(owner))
				{
					OwningPawn->MoveIgnoreActorAdd(root->GetOwner());
				}

				if (owner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					IVRGripInterface::Execute_OnChildGrip(owner, this, NewGrip);
				}
			}

			if (NewGrip.Component->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnGrip(NewGrip.Component, this, NewGrip);
			}
		}
	}break;
	}

	switch (NewGrip.GripMovementReplicationSetting)
	{
	case EGripMovementReplicationSettings::ForceClientSideMovement:
	{
		if (IsServer())
		{
			if (NewGrip.Component && NewGrip.Component->GetOwner())
				NewGrip.Component->GetOwner()->SetReplicateMovement(false);
			else if (NewGrip.Actor)
				NewGrip.Actor->SetReplicateMovement(false);
		}
	}break;

	case EGripMovementReplicationSettings::ForceServerSideMovement:
	{
		if (IsServer())
		{
			if (NewGrip.Component && NewGrip.Component->GetOwner())
				NewGrip.Component->GetOwner()->SetReplicateMovement(true);
			else if (NewGrip.Actor)
				NewGrip.Actor->SetReplicateMovement(true);
		}
	}break;

	case EGripMovementReplicationSettings::KeepOriginalMovement:
	default:
	{}break;
	}


	bool bHasMovementAuthority = HasGripMovementAuthority(NewGrip);


	switch (NewGrip.GripCollisionType.GetValue())
	{
	case EGripCollisionType::InteractiveCollisionWithPhysics:
	case EGripCollisionType::ManipulationGrip:
	{
		if(bHasMovementAuthority)
			SetUpPhysicsHandle(NewGrip);

	} break;

	case EGripCollisionType::InteractiveCollisionWithVelocity:
	{
		/*if (IsServer())
		{
			if (NewGrip.Component && NewGrip.Component->GetOwner())
				NewGrip.Component->GetOwner()->SetReplicateMovement(false);
			else if (NewGrip.Actor)
				NewGrip.Actor->SetReplicateMovement(false);
		}*/

		if (NewGrip.Component)
			NewGrip.Component->SetSimulatePhysics(true);
		else if (NewGrip.Actor)
		{
			if (UPrimitiveComponent * primComp = Cast<UPrimitiveComponent>(NewGrip.Actor->GetRootComponent()))
			{
				primComp->SetSimulatePhysics(true);
			}
		}
	} break;

	// Skip collision intersects with these types, they dont need it
	case EGripCollisionType::PhysicsOnly:
	case EGripCollisionType::SweepWithPhysics:

	case EGripCollisionType::InteractiveHybridCollisionWithSweep:
	case EGripCollisionType::InteractiveCollisionWithSweep:
	default: 
	{
		if (NewGrip.Component)
			NewGrip.Component->SetSimulatePhysics(false);
		else if(NewGrip.Actor)
			NewGrip.Actor->DisableComponentsSimulatePhysics();

		/*if (IsServer())
		{
			if (NewGrip.Component && NewGrip.Component->GetOwner())
				NewGrip.Component->GetOwner()->SetReplicateMovement(false);
			else if(NewGrip.Actor)
				NewGrip.Actor->SetReplicateMovement(false);
		}*/
	} break;

	}

	// Move it to the correct location automatically
	if (bHasMovementAuthority)
		TeleportMoveGrip(NewGrip);
}

void UGripMotionControllerComponent::NotifyDrop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate)
{
	DestroyPhysicsHandle(NewDrop);

	// Remove the drop from the array, can't wait around for replication as the tick function will start in on it.
	int fIndex = 0;
	if (GrippedActors.Find(NewDrop, fIndex))
	{
		GrippedActors.RemoveAt(fIndex);
	}

	UPrimitiveComponent *root = NULL;

	switch (NewDrop.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	case EGripTargetType::InteractibleActorGrip:
	{
		if (NewDrop.Actor)
		{
			root = Cast<UPrimitiveComponent>(NewDrop.Actor->GetRootComponent());

			NewDrop.Actor->RemoveTickPrerequisiteComponent(this);
			this->IgnoreActorWhenMoving(NewDrop.Actor, false);

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorRemove(NewDrop.Actor);
			}

			if (IsServer())
			{
				NewDrop.Actor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
			}

			if (NewDrop.Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(NewDrop.Actor, NewDrop.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(NewDrop.Actor, this, NewDrop);
			}
		}
	}break;

	case EGripTargetType::ComponentGrip:
	case EGripTargetType::InteractibleComponentGrip:
	{
		if (NewDrop.Component)
		{
			root = NewDrop.Component;
			NewDrop.Component->RemoveTickPrerequisiteComponent(this);

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorRemove(NewDrop.Component->GetOwner());
			}

			if (AActor * owner = NewDrop.Component->GetOwner())
			{
				if (IsServer())
					owner->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);

				if (owner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					IVRGripInterface::Execute_OnChildGripRelease(owner, this, NewDrop);
				}

			}

			if (NewDrop.Component->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(NewDrop.Component, NewDrop.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(NewDrop.Component, this, NewDrop);
			}
		}
	}break;
	}

	if (root)
	{
		root->IgnoreActorWhenMoving(this->GetOwner(), false);
		root->SetSimulatePhysics(bSimulate);
		if(bSimulate)
			root->WakeAllRigidBodies();
		root->SetEnableGravity(true);
	}
}

bool UGripMotionControllerComponent::HasGripMovementAuthority(const FBPActorGripInformation &Grip)
{
	if (IsServer())
	{
		return true;
	}
	else
	{
		if (Grip.GripCollisionType == EGripCollisionType::PhysicsOnly ||
			Grip.GripCollisionType == EGripCollisionType::SweepWithPhysics ||
			Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceClientSideMovement)
		{
			return true;
		}
		else if (Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement)
		{
			return false;
		}

		// Use original movement type is overridden when initializing the grip and shouldn't happen
		check(Grip.GripMovementReplicationSetting != EGripMovementReplicationSettings::KeepOriginalMovement);
	}

	return false;
}

bool UGripMotionControllerComponent::AddSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform & OriginalTransform, bool bTransformIsAlreadyRelative, float LerpToTime, float SecondarySmoothingScaler)
{
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called on the client side"));
		return false;
	}

	if (!GrippedActorToAddAttachment || !SecondaryPointComponent || !GrippedActors.Num())
		return false;

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == GrippedActorToAddAttachment)
		{
			if(bTransformIsAlreadyRelative)
				GrippedActors[i].SecondaryRelativeLocation = OriginalTransform.GetLocation();
			else
				GrippedActors[i].SecondaryRelativeLocation = OriginalTransform.GetRelativeTransform(GrippedActorToAddAttachment->GetTransform()).GetLocation();

			GrippedActors[i].SecondaryAttachment = SecondaryPointComponent;
			GrippedActors[i].bHasSecondaryAttachment = true;	
			GrippedActors[i].SecondarySmoothingScaler = FMath::Clamp(SecondarySmoothingScaler, 0.01f, 1.0f);

			if (GrippedActors[i].GripLerpState == EGripLerpState::EndLerp)
				LerpToTime = 0.0f;

			if (LerpToTime > 0.0f)
			{
				GrippedActors[i].LerpToRate = LerpToTime;
				GrippedActors[i].GripLerpState = EGripLerpState::StartLerp;
				GrippedActors[i].curLerp = LerpToTime;
			}
						
			if (GrippedActorToAddAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnSecondaryGrip(GrippedActorToAddAttachment, SecondaryPointComponent, GrippedActors[i]);
			}

			return true;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::RemoveSecondaryAttachmentPoint(AActor * GrippedActorToRemoveAttachment, float LerpToTime)
{
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController remove secondary attachment function was called on the client side"));
		return false;
	}

	if (!GrippedActorToRemoveAttachment || !GrippedActors.Num())
		return false;

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == GrippedActorToRemoveAttachment)
		{
			if (GrippedActors[i].GripLerpState == EGripLerpState::StartLerp)
				LerpToTime = 0.0f;

			if (LerpToTime > 0.0f)
			{
				if (UPrimitiveComponent * rootComp = Cast<UPrimitiveComponent>(GrippedActors[i].Actor->GetRootComponent()))
				{
					GrippedActors[i].LerpToRate = LerpToTime;
					GrippedActors[i].GripLerpState = EGripLerpState::EndLerp;
					GrippedActors[i].curLerp = LerpToTime;
				}
				else
				{
					GrippedActors[i].LerpToRate = 0.0f;
					GrippedActors[i].GripLerpState = EGripLerpState::NotLerping;
				}
			}
			else
			{
				GrippedActors[i].LerpToRate = 0.0f;
				GrippedActors[i].GripLerpState = EGripLerpState::NotLerping;
			}

			if (GrippedActorToRemoveAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnSecondaryGripRelease(GrippedActorToRemoveAttachment, GrippedActors[i].SecondaryAttachment, GrippedActors[i]);
			}

			GrippedActors[i].SecondaryAttachment = nullptr;
			GrippedActors[i].bHasSecondaryAttachment = false;
			
			return true;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrippedActor(AActor * GrippedActorToMove)
{
	if (!GrippedActorToMove || !GrippedActors.Num())
		return false;

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == GrippedActorToMove)
		{
			return TeleportMoveGrip(GrippedActors[i]);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrippedComponent(UPrimitiveComponent * ComponentToMove)
{
	if (!ComponentToMove || !GrippedActors.Num())
		return false;

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Component && GrippedActors[i].Component == ComponentToMove)
		{
			return TeleportMoveGrip(GrippedActors[i]);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrip(const FBPActorGripInformation &Grip, bool bIsPostTeleport)
{
	UPrimitiveComponent * PrimComp = NULL;
	AActor * actor = NULL;

	switch (Grip.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	case EGripTargetType::InteractibleActorGrip:
	{
		if (Grip.Actor)
		{
			actor = Grip.Actor;
			PrimComp = Cast<UPrimitiveComponent>(Grip.Actor->GetRootComponent());
		}
	}break;

	case EGripTargetType::ComponentGrip:
	case EGripTargetType::InteractibleComponentGrip:
	{
		actor = Grip.Component->GetOwner();
		PrimComp = Grip.Component;
	}break;

	}

	if (!PrimComp || !actor)
		return false;

	// Only use with actual teleporting
	if (bIsPostTeleport)
	{
		EGripInterfaceTeleportBehavior TeleportBehavior = EGripInterfaceTeleportBehavior::TeleportAllComponents;
		bool bSimulateOnDrop = false;

		// Check for interaction interface
		if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(PrimComp);
			bSimulateOnDrop = IVRGripInterface::Execute_SimulateOnDrop(PrimComp);
		}
		else if (Grip.Actor && Grip.Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			// Actor grip interface is checked after component
			TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(Grip.Actor);
			bSimulateOnDrop = IVRGripInterface::Execute_SimulateOnDrop(Grip.Actor);
		}

		if (TeleportBehavior == EGripInterfaceTeleportBehavior::OnlyTeleportRootComponent)
		{
			if (AActor * owner = PrimComp->GetOwner())
			{
				if (PrimComp != owner->GetRootComponent())
				{
					return false;
				}
			}
		}
		else if (TeleportBehavior == EGripInterfaceTeleportBehavior::DropOnTeleport)
		{
			if (IsServer())
				DropGrip(Grip, bSimulateOnDrop);

			return false; // Didn't teleport
		}
		else if (TeleportBehavior == EGripInterfaceTeleportBehavior::DontTeleport)
		{
			return false; // Didn't teleport
		}
	}

	FTransform WorldTransform;
	FTransform ParentTransform = this->GetComponentTransform();

	FBPActorGripInformation copyGrip = Grip;

	GetGripWorldTransform(0.0f, WorldTransform, ParentTransform, copyGrip, actor, PrimComp);

	//WorldTransform = Grip.RelativeTransform * ParentTransform;

	// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
	PrimComp->SetWorldTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);

	FBPActorPhysicsHandleInformation * Handle = GetPhysicsGrip(Grip);
	if (Handle && Handle->KinActorData)
	{

		USkeletalMeshComponent * skele = Cast<USkeletalMeshComponent>(PrimComp);
	//	FTransform trans = P2UTransform(Handle->KinActorData->getGlobalPose());

		if (skele)
		{
			WorldTransform.ConcatenateRotation(skele->GetBoneTransform(0, FTransform::Identity).GetRotation());
		}

	#if WITH_PHYSX
		{
			PxScene* PScene = GetPhysXSceneFromIndex(Handle->SceneIndex);
			if (PScene)
			{
				if (Grip.GripCollisionType == EGripCollisionType::ManipulationGrip)
				{
					FTransform WTransform = WorldTransform;
					WTransform.SetLocation(this->GetComponentLocation());
					SCOPED_SCENE_WRITE_LOCK(PScene);
					Handle->KinActorData->setKinematicTarget(U2PTransform(WTransform));
					Handle->KinActorData->setGlobalPose(U2PTransform(WTransform));

				}
				else
				{
					SCOPED_SCENE_WRITE_LOCK(PScene);
					Handle->KinActorData->setKinematicTarget(U2PTransform(WorldTransform));
					Handle->KinActorData->setGlobalPose(U2PTransform(WorldTransform));
				}
			}
		}
	#endif

		FBodyInstance * body = PrimComp->GetBodyInstance();
		if (body)
		{
			body->SetBodyTransform(WorldTransform, ETeleportType::TeleportPhysics);
		}

	}

	return true;
}

void UGripMotionControllerComponent::PostTeleportMoveGrippedActors()
{
	if (!GrippedActors.Num())
		return;

	for (int i = 0; i < GrippedActors.Num(); i++)
	{
		TeleportMoveGrip(GrippedActors[i], true);
	}
}

void UGripMotionControllerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);


	if (!bIsActive)
		return;

	// Moved this here instead of in the polling function, it was ticking once per frame anyway so no loss of perf
	// It doesn't need to be there and now I can pre-check
	// Also epics implementation in the polling function didn't work anyway as it was based off of playercontroller which is not the owner of this controller
	
	// Cache state from the game thread for use on the render thread
	// No need to check if in game thread here as tick always is
	bHasAuthority = IsLocallyControlled();

	// Server/remote clients don't set the controller position in VR
	// Don't call positional checks and don't create the late update scene view
	if (bHasAuthority)
	{
		if (!ViewExtension.IsValid() && GEngine)
		{
			TSharedPtr< FViewExtension, ESPMode::ThreadSafe > NewViewExtension(new FViewExtension(this));
			ViewExtension = NewViewExtension;
			GEngine->ViewExtensions.Add(ViewExtension);
		}

		// This is the owning player, now you can get the controller's location and rotation from the correct source
		FVector Position;
		FRotator Orientation;
		bTracked = PollControllerState(Position, Orientation);

		if (bTracked)
		{
			SetRelativeLocationAndRotation(Position, Orientation);
		}

		if (!bTracked && !bUseWithoutTracking)
			return; // Don't update anything including location

		// Don't bother with any of this if not replicating transform
		if (bReplicates && bTracked)
		{
			ReplicatedControllerTransform.Position = Position;
			ReplicatedControllerTransform.SetRotation(Orientation);//.Orientation = Orientation;

			if (GetNetMode() == NM_Client)//bReplicateControllerTransform)
			{
				ControllerNetUpdateCount += DeltaTime;
				if (ControllerNetUpdateCount >= (1.0f / ControllerNetUpdateRate))
				{
					ControllerNetUpdateCount = 0.0f;
					Server_SendControllerTransform(ReplicatedControllerTransform);
				}
			}
		}
	}

	// Process the gripped actors
	TickGrip(DeltaTime);

}

void UGripMotionControllerComponent::GetGripWorldTransform(float DeltaTime,FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root)
{
	// Check for interaction interface and modify transform by it
	if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()) && IVRGripInterface::Execute_IsInteractible(root))
	{
		WorldTransform = HandleInteractionSettings(DeltaTime, ParentTransform, root, IVRGripInterface::Execute_GetInteractionSettings(root), Grip);
	}
	else if (actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()) && IVRGripInterface::Execute_IsInteractible(actor))
	{
		// Actor grip interface is checked after component
		WorldTransform = HandleInteractionSettings(DeltaTime, ParentTransform, root, IVRGripInterface::Execute_GetInteractionSettings(actor), Grip);
	}
	else
	{
		// Just simple transform setting
		WorldTransform = Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;

	}

	// Check the grip lerp state, this it ouside of the secondary attach check below because it can change the result of it
	if ((Grip.bHasSecondaryAttachment && Grip.SecondaryAttachment) || Grip.GripLerpState == EGripLerpState::EndLerp)
	{
		switch (Grip.GripLerpState)
		{
		case EGripLerpState::StartLerp:
		case EGripLerpState::EndLerp:
		{
			if (Grip.curLerp > 0.01f)
				Grip.curLerp -= DeltaTime;
			else
			{
				if (Grip.bHasSecondaryAttachment && Grip.SecondarySmoothingScaler < 1.0f)
					Grip.GripLerpState = EGripLerpState::ConstantLerp;
				else
					Grip.GripLerpState = EGripLerpState::NotLerping;
			}

		}break;
		case EGripLerpState::ConstantLerp:
		case EGripLerpState::NotLerping:
		default:break;
		}
	}

	// Handle the interp and multi grip situations, re-checking the grip situation here as it may have changed in the switch above.
	if ((Grip.bHasSecondaryAttachment && Grip.SecondaryAttachment) || Grip.GripLerpState == EGripLerpState::EndLerp)
	{
		// Variables needed for multi grip transform
		FVector BasePoint = this->GetComponentLocation();
		const FTransform PivotToWorld = FTransform(FQuat::Identity, BasePoint);
		const FTransform WorldToPivot = FTransform(FQuat::Identity, -BasePoint);

		FVector frontLocOrig;
		FVector frontLoc;

		// Ending lerp out of a multi grip
		if (Grip.GripLerpState == EGripLerpState::EndLerp)
		{
			frontLocOrig = (WorldTransform.TransformPosition(Grip.SecondaryRelativeLocation)) - BasePoint;
			frontLoc = Grip.LastRelativeLocation;

			frontLocOrig = FMath::Lerp(frontLoc, frontLocOrig, Grip.curLerp / Grip.LerpToRate);
		}
		else // Is in a multi grip, might be lerping into it as well.
		{
			FVector curLocation;

			bool bPulledControllerLoc = false;
			if (bHasAuthority && Grip.SecondaryAttachment->GetOwner() == this->GetOwner())
			{
				if (UGripMotionControllerComponent * OtherController = Cast<UGripMotionControllerComponent>(Grip.SecondaryAttachment))
				{
					FVector Position;
					FRotator Orientation;

					if (OtherController->PollControllerState(Position, Orientation))
					{
						curLocation = OtherController->CalcNewComponentToWorld(FTransform(Orientation, Position)).GetLocation() - BasePoint;
						bPulledControllerLoc = true;
					}
				}
			}

			if(!bPulledControllerLoc)
				curLocation = Grip.SecondaryAttachment->GetComponentLocation() - BasePoint;


			frontLocOrig = (WorldTransform.TransformPosition(Grip.SecondaryRelativeLocation)) - BasePoint;
			frontLoc = curLocation;// -BasePoint;

			if (Grip.GripLerpState == EGripLerpState::StartLerp) // Lerp into the new grip to smooth the transtion
			{
				frontLocOrig = FMath::Lerp(frontLocOrig, frontLoc, Grip.curLerp / Grip.LerpToRate);
			}
			else if (Grip.GripLerpState == EGripLerpState::ConstantLerp) // If there is a frame by frame lerp
			{
				frontLoc = FMath::Lerp(Grip.LastRelativeLocation, frontLoc, Grip.SecondarySmoothingScaler);
			}
			Grip.LastRelativeLocation = frontLoc;//curLocation;// -BasePoint;
		}

		// Get the rotation difference from the initial second grip
		FQuat rotVal = FQuat::FindBetweenVectors(frontLocOrig, frontLoc);

		// Create a transform from it
		FTransform RotationOffsetTransform(rotVal, FVector::ZeroVector);

		// Rebase the world transform to the pivot point, add the rotation, remove the pivot point rebase
		WorldTransform = WorldTransform * WorldToPivot * RotationOffsetTransform * PivotToWorld;
	}
}

void UGripMotionControllerComponent::TickGrip(float DeltaTime)
{

	// Debug test that we aren't floating physics handles
	check(PhysicsGrips.Num() <= GrippedActors.Num());

	if (GrippedActors.Num())
	{
		FTransform WorldTransform;
		FTransform ParentTransform = this->GetComponentTransform();

		for (int i = GrippedActors.Num() - 1; i >= 0; --i)
		{
			if (!HasGripMovementAuthority(GrippedActors[i]))
				continue;

			FBPActorGripInformation * Grip = &GrippedActors[i];

			if (!Grip)
				continue;

			if (Grip->Actor || Grip->Component)
			{
				UPrimitiveComponent *root = NULL;
				AActor *actor = NULL;

				// Getting the correct variables depending on the grip target type
				switch (Grip->GripTargetType)
				{
					case EGripTargetType::ActorGrip:
					case EGripTargetType::InteractibleActorGrip:
					{
						actor = Grip->Actor;
						if(actor)
							root = Cast<UPrimitiveComponent>(actor->GetRootComponent());
					}break;

					case EGripTargetType::ComponentGrip:
					case EGripTargetType::InteractibleComponentGrip :
					{
						root = Grip->Component;
						if(root)
							actor = root->GetOwner();
					}break;

				default:break;
				}

				// Last check to make sure the variables are valid
				if (!root || !actor)
					continue;

				// Get the world transform for this grip after handling secondary grips and interaction differences
				GetGripWorldTransform(DeltaTime, WorldTransform, ParentTransform, *Grip, actor, root);

				// Start handling the grip types and their functions
				if (Grip->GripCollisionType == EGripCollisionType::InteractiveCollisionWithPhysics)
				{
					UpdatePhysicsHandleTransform(*Grip, WorldTransform);
					
					// Sweep current collision state, only used for client side late update removal
					if (	
						(bHasAuthority &&
						((Grip->GripLateUpdateSetting == EGripLateUpdateSettings::NotWhenColliding) ||
						(Grip->GripLateUpdateSetting == EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping)))		
						)
					{
						TArray<FOverlapResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);
						if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, root->GetComponentLocation(), root->GetComponentQuat(), root->GetCollisionObjectType(), Params))
						{
							Grip->bColliding = true;
						}
						else
						{
							Grip->bColliding = false;
						}
					}

				}
				else if (Grip->GripCollisionType == EGripCollisionType::InteractiveCollisionWithVelocity)
				{

					TArray<FOverlapResult> Hits;
					FComponentQueryParams Params(NAME_None, this->GetOwner());
					Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
					Params.AddIgnoredActor(actor);
					Params.AddIgnoredActors(root->MoveIgnoreActors);
					if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, root->GetComponentLocation(), root->GetComponentQuat(), root->GetCollisionObjectType(), Params))
					{
						Grip->bColliding = true;
					}
					else
					{
						Grip->bColliding = false;
					}


					// BETA CODE
					FBodyInstance * body = root->GetBodyInstance();
					FVector linVel = (WorldTransform.GetLocation() - root->GetComponentLocation()) / DeltaTime;// (1.0f / DeltaTime);

					// Stop that jitter
					if (Grip->bColliding)
						linVel = linVel.GetClampedToSize(-200.0f, 200.0f);

					root->SetAllPhysicsLinearVelocity(linVel, false);

					FQuat RotationDelta = WorldTransform.GetRotation() * root->GetComponentQuat().Inverse();
					RotationDelta.Normalize();
					FVector axis;
					float angle;
					RotationDelta.ToAxisAndAngle(axis, angle);

					// Correcting for over rotation, using Radians
					if (angle > PI)
						angle -= PI * 2.0f;

					if (angle != 0)
					{
						body->MaxAngularVelocity = PX_MAX_F32;
						FVector AngularTarget = ((angle * axis) / DeltaTime) * 40.0f;

						// Stop that jitter
						if (Grip->bColliding)
							AngularTarget = AngularTarget.GetClampedToSize(-200.0f, 200.0f);

						root->SetAllPhysicsAngularVelocity(AngularTarget, false);
					}

				}
				else if (Grip->GripCollisionType == EGripCollisionType::InteractiveCollisionWithSweep)
				{
					FVector OriginalPosition(root->GetComponentLocation());
					FVector NewPosition(WorldTransform.GetTranslation());

					if(!Grip->bIsLocked)
						root->ComponentVelocity = (NewPosition - OriginalPosition) / DeltaTime;

					if (Grip->bIsLocked)
						WorldTransform.SetRotation(Grip->LastLockedRotation);

					FHitResult OutHit;
					// Need to use without teleport so that the physics velocity is updated for when the actor is released to throw
					
					root->SetWorldTransform(WorldTransform, true, &OutHit);

					if (OutHit.bBlockingHit)
					{
						Grip->bColliding = true;

						if (!Grip->bIsLocked)
						{
							Grip->bIsLocked = true;
							Grip->LastLockedRotation = root->GetComponentQuat();
						}
					}
					else
					{
						Grip->bColliding = false;

						if (Grip->bIsLocked)
							Grip->bIsLocked = false;
					}
				}
				else if (Grip->GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithSweep)
				{

					// Make sure that there is no collision on course before turning off collision and snapping to controller
					FBPActorPhysicsHandleInformation * GripHandle = GetPhysicsGrip(*Grip);

					if (Grip->bColliding)
					{
						// Check for overlap ending
						TArray<FOverlapResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);
						if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, root->GetComponentLocation(), root->GetComponentQuat(), root->GetCollisionObjectType(), Params))
						{
							Grip->bColliding = true;
						}
						else
						{
							Grip->bColliding = false;
						}
					}
					else if (!Grip->bColliding)
					{
						// Check for overlap beginning
						TArray<FOverlapResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);
						if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, WorldTransform.GetLocation(), WorldTransform.GetRotation(), root->GetCollisionObjectType(), Params))
						{
							Grip->bColliding = true;
						}
						else
						{
							Grip->bColliding = false;
						}
					}

					if (!Grip->bColliding)
					{	

						if (GripHandle)
						{
							if(GripHandle)
								DestroyPhysicsHandle(*Grip);

							if(Grip->Actor)
								Grip->Actor->DisableComponentsSimulatePhysics();
							else
								root->SetSimulatePhysics(false);
						}

						FHitResult OutHit;
						root->SetWorldTransform(WorldTransform, true, &OutHit);

						if (OutHit.bBlockingHit)
						{
							Grip->bColliding = true;
						}

					}
					else if (Grip->bColliding && !GripHandle)
					{
						root->SetSimulatePhysics(true);

						SetUpPhysicsHandle(*Grip);
						UpdatePhysicsHandleTransform(*Grip, WorldTransform);
					}
					else
					{
						// Shouldn't be a grip handle if not server when server side moving
						if (GripHandle)
							UpdatePhysicsHandleTransform(*Grip, WorldTransform);
					}

				}
				else if (Grip->GripCollisionType == EGripCollisionType::SweepWithPhysics)
				{			
					FVector OriginalPosition(root->GetComponentLocation());
					FRotator OriginalOrientation(root->GetComponentRotation());

					FVector NewPosition(WorldTransform.GetTranslation());
					FRotator NewOrientation(WorldTransform.GetRotation());

					root->ComponentVelocity = (NewPosition - OriginalPosition) / DeltaTime;

					// Now sweep collision separately so we can get hits but not have the location altered
					if (bUseWithoutTracking || NewPosition != OriginalPosition || NewOrientation != OriginalOrientation)
					{
						FVector move = NewPosition - OriginalPosition;

						// ComponentSweepMulti does nothing if moving < KINDA_SMALL_NUMBER in distance, so it's important to not try to sweep distances smaller than that. 
						const float MinMovementDistSq = (FMath::Square(4.f*KINDA_SMALL_NUMBER));

						if (bUseWithoutTracking || move.SizeSquared() > MinMovementDistSq || NewOrientation != OriginalOrientation)
						{
							if (CheckComponentWithSweep(root, move, OriginalOrientation, false))
							{
								Grip->bColliding = true;
							}
							else
							{
								Grip->bColliding = false;
							}
						}
					}

					// Move the actor, we are not offsetting by the hit result anyway
					root->SetWorldTransform(WorldTransform, false);

				}
				else if (Grip->GripCollisionType == EGripCollisionType::PhysicsOnly)
				{
					// Move the actor, we are not offsetting by the hit result anyway
					root->SetWorldTransform(WorldTransform, false);
				}
				else if (Grip->GripCollisionType == EGripCollisionType::ManipulationGrip)
				{
					UpdatePhysicsHandleTransform(*Grip, WorldTransform);
				}
			}
			else
			{
				DestroyPhysicsHandle(*Grip);

				if (IsServer())
				{
					GrippedActors.RemoveAt(i); // If it got garbage collected then just remove the pointer, won't happen with new uproperty use, but keeping it here anyway
				}
			}
		}
	}

}

FTransform UGripMotionControllerComponent::HandleInteractionSettings(float DeltaTime, const FTransform & ParentTransform, UPrimitiveComponent * root, FBPInteractionSettings InteractionSettings, FBPActorGripInformation & GripInfo)
{
	FTransform LocalTransform = GripInfo.RelativeTransform * GripInfo.AdditionTransform;
	FTransform WorldTransform;
	
	WorldTransform = LocalTransform * ParentTransform;
	
	if (InteractionSettings.bLimitsInLocalSpace)
	{
		if (USceneComponent * parent = root->GetAttachParent())
			LocalTransform = parent->GetComponentTransform();
		else
			LocalTransform = FTransform::Identity;

		WorldTransform = WorldTransform.GetRelativeTransform(LocalTransform);
	}

	FVector componentLoc = WorldTransform.GetLocation();

	// Translation settings
	if (InteractionSettings.bLimitX)
		componentLoc.X = FMath::Clamp(componentLoc.X, InteractionSettings.InitialLinearTranslation.X + InteractionSettings.MinLinearTranslation.X, InteractionSettings.InitialLinearTranslation.X + InteractionSettings.MaxLinearTranslation.X);
		
	if (InteractionSettings.bLimitY)
		componentLoc.Y = FMath::Clamp(componentLoc.Y, InteractionSettings.InitialLinearTranslation.Y + InteractionSettings.MinLinearTranslation.Y, InteractionSettings.InitialLinearTranslation.Y + InteractionSettings.MaxLinearTranslation.Y);

	if (InteractionSettings.bLimitZ)
		componentLoc.Z = FMath::Clamp(componentLoc.Z, InteractionSettings.InitialLinearTranslation.Z + InteractionSettings.MinLinearTranslation.Z, InteractionSettings.InitialLinearTranslation.Z + InteractionSettings.MaxLinearTranslation.Z);

	WorldTransform.SetLocation(componentLoc);

	FRotator componentRot = WorldTransform.GetRotation().Rotator();

	if (InteractionSettings.bRotateLeverToFaceController)
	{
		FRotator leverRot = FRotationMatrix::MakeFromX(ParentTransform.GetLocation() - componentLoc).Rotator();
		if (!InteractionSettings.bLimitPitch)
			componentRot.Pitch = leverRot .Pitch;

		if (!InteractionSettings.bLimitYaw)
			componentRot.Yaw = leverRot.Yaw;

		if (!InteractionSettings.bLimitRoll)
			componentRot.Roll = leverRot.Roll;
	}

	// Rotation Settings
	if ((InteractionSettings.bRotateLeverToFaceController && !InteractionSettings.bLimitPitch) || InteractionSettings.bLimitPitch)
		componentRot.Pitch = FMath::Clamp(componentRot.Pitch, InteractionSettings.InitialAngularTranslation.Pitch + InteractionSettings.MinAngularTranslation.Pitch, InteractionSettings.InitialAngularTranslation.Pitch + InteractionSettings.MaxAngularTranslation.Pitch);
		
	if ((InteractionSettings.bRotateLeverToFaceController && !InteractionSettings.bLimitYaw) || InteractionSettings.bLimitYaw)
		componentRot.Yaw = FMath::Clamp(componentRot.Yaw, InteractionSettings.InitialAngularTranslation.Yaw + InteractionSettings.MinAngularTranslation.Yaw, InteractionSettings.InitialAngularTranslation.Yaw + InteractionSettings.MaxAngularTranslation.Yaw);

	if ((InteractionSettings.bRotateLeverToFaceController && !InteractionSettings.bLimitRoll) || InteractionSettings.bLimitRoll)
		componentRot.Roll = FMath::Clamp(componentRot.Roll, InteractionSettings.InitialAngularTranslation.Roll + InteractionSettings.MinAngularTranslation.Roll, InteractionSettings.InitialAngularTranslation.Roll + InteractionSettings.MaxAngularTranslation.Roll);
	
	WorldTransform.SetRotation(componentRot.Quaternion());

	if (InteractionSettings.bLimitsInLocalSpace)
	{
		WorldTransform = WorldTransform * LocalTransform;
	}

	return WorldTransform;
}

bool UGripMotionControllerComponent::DestroyPhysicsHandle(int32 SceneIndex, physx::PxD6Joint** HandleData, physx::PxRigidDynamic** KinActorData)
{
	#if WITH_PHYSX
		if (HandleData && *HandleData)
		{
			check(*KinActorData);

			// use correct scene
			PxScene* PScene = GetPhysXSceneFromIndex(SceneIndex);
			if (PScene)
			{
				SCOPED_SCENE_WRITE_LOCK(PScene);
				// Destroy joint.
				(*HandleData)->release();

				// Destroy temporary actor.
				(*KinActorData)->release();

			}
			*KinActorData = NULL;
			*HandleData = NULL;
		}
		else
		return false;
	#endif // WITH_PHYSX

	return true;
}

bool UGripMotionControllerComponent::DestroyPhysicsHandle(const FBPActorGripInformation &Grip)
{
	UPrimitiveComponent *root = Grip.Component;
	if(!root && Grip.Actor)
		root = Cast<UPrimitiveComponent>(Grip.Actor->GetRootComponent());

	//if (root)
	//	root->SetEnableGravity(true);

	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(Grip);

	if (!HandleInfo)
	{
		return true;
	}

	DestroyPhysicsHandle(HandleInfo->SceneIndex, &HandleInfo->HandleData, &HandleInfo->KinActorData);

	int index;
	if (GetPhysicsGripIndex(Grip, index))
		PhysicsGrips.RemoveAt(index);

	return true;
}

bool UGripMotionControllerComponent::SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip)
{
	UPrimitiveComponent *root = NewGrip.Component;
	if(!root && NewGrip.Actor)
		root = Cast<UPrimitiveComponent>(NewGrip.Actor->GetRootComponent());
	
	if (!root)
		return false;

	// Needs to be simulating in order to run physics
	root->SetSimulatePhysics(true);

	FBPActorPhysicsHandleInformation * HandleInfo = CreatePhysicsGrip(NewGrip);

#if WITH_PHYSX
	// Get the PxRigidDynamic that we want to grab.
	FBodyInstance* rBodyInstance = root->GetBodyInstance(NAME_None/*InBoneName*/);
	if (!rBodyInstance)
	{
		return false;
	}

	ExecuteOnPxRigidDynamicReadWrite(rBodyInstance, [&](PxRigidDynamic* Actor)
	{
		PxScene* Scene = Actor->getScene();
			
		PxTransform KinPose;
		PxTransform GrabbedActorPose;
		FTransform trans = root->GetComponentTransform();


		if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGrip)
		{

			FTransform WorldTransform;
			WorldTransform = NewGrip.RelativeTransform * this->GetComponentTransform();
			trans.SetLocation(root->GetComponentTransform().GetLocation() - (WorldTransform.GetLocation() - this->GetComponentLocation()));
		}
		else
		{
			USkeletalMeshComponent * skele = Cast<USkeletalMeshComponent>(root);
			if (skele)
			{
				trans.ConcatenateRotation(skele->GetBoneTransform(0, FTransform::Identity).GetRotation());
			}
		}

		// Get transform of actor we are grabbing
		KinPose = U2PTransform(trans);

		// set target and current, so we don't need another "Tick" call to have it right
		//TargetTransform = CurrentTransform = P2UTransform(KinPose);

		// If we don't already have a handle - make one now.
		if (!HandleInfo->HandleData)
		{
			// Create kinematic actor we are going to create joint with. This will be moved around with calls to SetLocation/SetRotation.
			PxRigidDynamic* KinActor = Scene->getPhysics().createRigidDynamic(KinPose);
			KinActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

			KinActor->setMass(0.0f); // 1.0f;
			KinActor->setMassSpaceInertiaTensor(PxVec3(0.0f, 0.0f, 0.0f));// PxVec3(1.0f, 1.0f, 1.0f));
			KinActor->setMaxDepenetrationVelocity(PX_MAX_F32);

			// No bodyinstance
			KinActor->userData = NULL;

			// Add to Scene
			Scene->addActor(*KinActor);

			// Save reference to the kinematic actor.
			HandleInfo->KinActorData = KinActor;
			PxD6Joint* NewJoint = NULL;

			if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGrip)
			{
				// Create the joint
				NewJoint = PxD6JointCreate(Scene->getPhysics(), KinActor, PxTransform(PxIdentity), Actor, Actor->getGlobalPose().transformInv(KinPose));
			}
			else
			{
				// Create the joint
				NewJoint = PxD6JointCreate(Scene->getPhysics(), KinActor, PxTransform(PxIdentity), Actor, PxTransform(PxIdentity));
			}

			if (!NewJoint)
			{
				HandleInfo->HandleData = 0;
			}
			else
			{
				// No constraint instance
				NewJoint->userData = NULL;
				HandleInfo->HandleData = NewJoint;

				// Remember the scene index that the handle joint/actor are in.
				FPhysScene* RBScene = FPhysxUserData::Get<FPhysScene>(Scene->userData);
				const uint32 SceneType = root->BodyInstance.UseAsyncScene(RBScene) ? PST_Async : PST_Sync;
				HandleInfo->SceneIndex = RBScene->PhysXSceneIndex[SceneType];

				// Pretty Much Unbreakable
				NewJoint->setBreakForce(PX_MAX_REAL, PX_MAX_REAL);

				// Different settings for manip grip
				if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGrip)
				{
					
					// Setting up the joint
					/*NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eLIMITED);
					NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eLIMITED);
					NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eLIMITED);
					PxJointLinearLimit newLimiter(1.0f, PxSpring(1500.0f, 200.0f));
					newLimiter.restitution = 0.0f;
					NewJoint->setLinearLimit(newLimiter);
					*/
					NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);

					NewJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);

				/*	PxJointLinearLimit newLimiter(10.0f, PxSpring(1500.0f, 200.0f));
					newLimiter.restitution = 0.0f;
					NewJoint->setLinearLimit(newLimiter);
					*/
					PxD6JointDrive drive = PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
					NewJoint->setDrive(PxD6Drive::eX, drive);
					NewJoint->setDrive(PxD6Drive::eY, drive);
					NewJoint->setDrive(PxD6Drive::eZ, drive);
					NewJoint->setDrive(PxD6Drive::eTWIST, drive);
				}
				else
				{
					PxD6JointDrive drive = PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
					PxD6JointDrive Angledrive = PxD6JointDrive(NewGrip.Stiffness * 1.5f, NewGrip.Damping /** 1.4f*/, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);

					// Setting up the joint
					NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
						
					NewJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);

					//NewJoint->setDrivePosition(PxTransform(Actor->getGlobalPose().transformInv(U2PVector(rBodyInstance->GetCOMPosition()))));			
					NewJoint->setDrivePosition(PxTransform(PxVec3(0, 0, 0)));

					NewJoint->setDrive(PxD6Drive::eX, drive);
					NewJoint->setDrive(PxD6Drive::eY, drive);
					NewJoint->setDrive(PxD6Drive::eZ, drive);
					//NewJoint->setDrive(PxD6Drive::eTWIST, Angledrive);
					//NewJoint->setDrive(PxD6Drive::eSWING, Angledrive);
					NewJoint->setDrive(PxD6Drive::eSLERP, Angledrive);
				}
			}
		}
	});
#else
	return false;
#endif // WITH_PHYSX

	return true;
}

void UGripMotionControllerComponent::UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform)
{
	if (!GrippedActor.Actor && !GrippedActor.Component)
		return;

	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(GrippedActor);

	if (!HandleInfo || !HandleInfo->KinActorData)
		return;

#if WITH_PHYSX
	bool bChangedPosition = true;
	bool bChangedRotation = true;

	PxRigidDynamic* KinActor = HandleInfo->KinActorData;
	PxScene* PScene = GetPhysXSceneFromIndex(HandleInfo->SceneIndex);
	SCOPED_SCENE_WRITE_LOCK(PScene);
	
	// Check if the new location is worthy of change
	PxVec3 PNewLocation = U2PVector(NewTransform.GetTranslation());
	PxVec3 PCurrentLocation = KinActor->getGlobalPose().p;
	if ((PNewLocation - PCurrentLocation).magnitudeSquared() <= 0.01f*0.01f)
	{
		PNewLocation = PCurrentLocation;
		bChangedPosition = false;
	}

	// Check if the new rotation is worthy of change
	PxQuat PNewOrientation = U2PQuat(NewTransform.GetRotation());
	PxQuat PCurrentOrientation = KinActor->getGlobalPose().q;
	if ((FMath::Abs(PNewOrientation.dot(PCurrentOrientation)) > (1.f - SMALL_NUMBER)))
	{
		PNewOrientation = PCurrentOrientation;
		bChangedRotation = false;
	}
	
	// Don't call moveKinematic if it hasn't changed - that will stop bodies from going to sleep.
	if (bChangedPosition || bChangedRotation)
	{
		FTransform terns = NewTransform;

		if (GrippedActor.GripCollisionType == EGripCollisionType::ManipulationGrip)
		{
			terns.SetLocation(this->GetComponentLocation());
		}
		else
		{
			USkeletalMeshComponent * skele = NULL;
			if (GrippedActor.Component)
			{
				skele = Cast<USkeletalMeshComponent>(GrippedActor.Component);
			}
			else if (GrippedActor.Actor)
			{
				skele = Cast<USkeletalMeshComponent>(GrippedActor.Actor->GetRootComponent());
			}

			if (skele)
			{
				terns.ConcatenateRotation(skele->GetBoneTransform(0, FTransform::Identity).GetRotation());
			}
		}

		KinActor->setKinematicTarget(PxTransform(U2PTransform(terns)/*PNewLocation, PNewOrientation*/));
	}
#endif // WITH_PHYSX
}

bool UGripMotionControllerComponent::CheckComponentWithSweep(UPrimitiveComponent * ComponentToCheck, FVector Move, FRotator newOrientation, bool bSkipSimulatingComponents/*,  bool &bHadBlockingHitOut*/)
{
	TArray<FHitResult> Hits;
	// WARNING: HitResult is only partially initialized in some paths. All data is valid only if bFilledHitResult is true.
	FHitResult BlockingHit(NoInit);
	BlockingHit.bBlockingHit = false;
	BlockingHit.Time = 1.f;
	bool bFilledHitResult = false;
	bool bMoved = false;
	bool bIncludesOverlapsAtEnd = false;
	bool bRotationOnly = false;

	UPrimitiveComponent *root = ComponentToCheck;

	if (!root || !root->IsQueryCollisionEnabled())
		return false;

	FVector start(root->GetComponentLocation());

	const bool bCollisionEnabled = root->IsQueryCollisionEnabled();

	if (bCollisionEnabled)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!root->IsRegistered())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("MovedComponent %s not initialized in grip motion controller"), *root->GetFullName());
		}
#endif

		UWorld* const MyWorld = GetWorld();
		FComponentQueryParams Params(TEXT("sweep_params"), root->GetOwner());

		FCollisionResponseParams ResponseParam;
		root->InitSweepCollisionParams(Params, ResponseParam);

		bool const bHadBlockingHit = MyWorld->ComponentSweepMulti(Hits, root, start, start + Move, newOrientation.Quaternion(), Params);

		if (bHadBlockingHit)
		{
			int32 BlockingHitIndex = INDEX_NONE;
			float BlockingHitNormalDotDelta = BIG_NUMBER;
			for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
			{
				const FHitResult& TestHit = Hits[HitIdx];

				// Ignore the owning actor to the motion controller
				if (TestHit.Actor == this->GetOwner() || (bSkipSimulatingComponents && TestHit.Component->IsSimulatingPhysics()))
				{
					if (Hits.Num() == 1)
					{
						//bHadBlockingHitOut = false;
						return false;
					}
					else
						continue;
				}

				if (TestHit.bBlockingHit && TestHit.IsValidBlockingHit())
				{
					if (TestHit.Time == 0.f)
					{
						// We may have multiple initial hits, and want to choose the one with the normal most opposed to our movement.
						const float NormalDotDelta = (TestHit.ImpactNormal | Move);
						if (NormalDotDelta < BlockingHitNormalDotDelta)
						{
							BlockingHitNormalDotDelta = NormalDotDelta;
							BlockingHitIndex = HitIdx;
						}
					}
					else if (BlockingHitIndex == INDEX_NONE)
					{
						// First non-overlapping blocking hit should be used, if an overlapping hit was not.
						// This should be the only non-overlapping blocking hit, and last in the results.
						BlockingHitIndex = HitIdx;
						break;
					}
					//}
				}
			}

			// Update blocking hit, if there was a valid one.
			if (BlockingHitIndex >= 0)
			{
				BlockingHit = Hits[BlockingHitIndex];
				bFilledHitResult = true;
			}
		}
	}

	// Handle blocking hit notifications. Avoid if pending kill (which could happen after overlaps).
	if (BlockingHit.bBlockingHit && !root->IsPendingKill())
	{
		check(bFilledHitResult);
		if (root->IsDeferringMovementUpdates())
		{
			FScopedMovementUpdate* ScopedUpdate = root->GetCurrentScopedMovement();
			ScopedUpdate->AppendBlockingHitAfterMove(BlockingHit);
		}
		else
		{
			
			if(root->GetOwner())
				root->DispatchBlockingHit(*root->GetOwner(), BlockingHit);
		}

		return true;
	}

	return false;
}

//=============================================================================
bool UGripMotionControllerComponent::PollControllerState(FVector& Position, FRotator& Orientation)
{
	// THIS IS WRONG, it would have never worked!!!
	/*if (IsInGameThread())
	{
		// Cache state from the game thread for use on the render thread
		const APlayerController* Actor = Cast<APlayerController>(GetOwner());
		bHasAuthority = !Actor || Actor->IsLocalPlayerController();
	}*/

	if ((PlayerIndex != INDEX_NONE) && bHasAuthority)
	{
		// New iteration and retrieval for 4.12
		TArray<IMotionController*> MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
		for (auto MotionController : MotionControllers)
		{
			if ((MotionController != nullptr) && MotionController->GetControllerOrientationAndPosition(PlayerIndex, Hand, Orientation, Position))
			{
				CurrentTrackingStatus = (EBPTrackingStatus)MotionController->GetControllerTrackingStatus(PlayerIndex, Hand);
				return true;
			}
		}
	}
	return false;
}

//=============================================================================
void UGripMotionControllerComponent::FViewExtension::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	if (!MotionControllerComponent)
	{
		return;
	}
	FScopeLock ScopeLock(&CritSect);

	if (MotionControllerComponent->bDisableLowLatencyUpdate || !CVarEnableMotionControllerLateUpdate->GetValueOnRenderThread())
	{
		return;
	}

	// Poll state for the most recent controller transform
	FVector Position;
	FRotator Orientation;

	//bool bRenderTracked = bRenderTracked = MotionControllerComponent->PollControllerState(Position, Orientation);
	if (!MotionControllerComponent->PollControllerState(Position, Orientation))
	{
		return;
	}

	if (LateUpdatePrimitives.Num())
	{
		// Calculate the late update transform that will rebase all children proxies within the frame of reference
		FTransform OldLocalToWorldTransform = MotionControllerComponent->CalcNewComponentToWorld(MotionControllerComponent->GetRelativeTransform());
		FTransform NewLocalToWorldTransform = MotionControllerComponent->CalcNewComponentToWorld(FTransform(Orientation, Position));
		FMatrix LateUpdateTransform = (OldLocalToWorldTransform.Inverse() * NewLocalToWorldTransform).ToMatrixWithScale();
		
		FPrimitiveSceneInfo* RetrievedSceneInfo;
		FPrimitiveSceneInfo* CachedSceneInfo;
		
		// Apply delta to the affected scene proxies
		for (auto PrimitiveInfo : LateUpdatePrimitives)
		{
			RetrievedSceneInfo = InViewFamily.Scene->GetPrimitiveSceneInfo(*PrimitiveInfo.IndexAddress);
			CachedSceneInfo = PrimitiveInfo.SceneInfo;

			// If the retrieved scene info is different than our cached scene info then the primitive was removed from the scene
			if (CachedSceneInfo == RetrievedSceneInfo && CachedSceneInfo->Proxy)
			{
				CachedSceneInfo->Proxy->ApplyLateUpdateTransform(LateUpdateTransform);
			}
		}
		LateUpdatePrimitives.Reset();
	}
}

void UGripMotionControllerComponent::FViewExtension::GatherLateUpdatePrimitives(USceneComponent* Component, TArray<LateUpdatePrimitiveInfo>& Primitives)
{
	// If a scene proxy is present, cache it
	UPrimitiveComponent* PrimitiveComponent = dynamic_cast<UPrimitiveComponent*>(Component);
	if (PrimitiveComponent && PrimitiveComponent->SceneProxy)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveComponent->SceneProxy->GetPrimitiveSceneInfo();
		if (PrimitiveSceneInfo)
		{
			LateUpdatePrimitiveInfo PrimitiveInfo;
			PrimitiveInfo.IndexAddress = PrimitiveSceneInfo->GetIndexAddress();
			PrimitiveInfo.SceneInfo = PrimitiveSceneInfo;
			Primitives.Add(PrimitiveInfo);
		}
	}

	// Gather children proxies
	const int32 ChildCount = Component->GetNumChildrenComponents();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		USceneComponent* ChildComponent = Component->GetChildComponent(ChildIndex);
		if (!ChildComponent)
		{
			continue;
		}

		GatherLateUpdatePrimitives(ChildComponent, Primitives);
	}
}

void UGripMotionControllerComponent::GetGrippedActors(TArray<AActor*> &GrippedActorsArray)
{
	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if(GrippedActors[i].Actor)
			GrippedActorsArray.Add(GrippedActors[i].Actor);
	}
}

void UGripMotionControllerComponent::GetGrippedComponents(TArray<UPrimitiveComponent*> &GrippedComponentsArray)
{
	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i].Component)
			GrippedComponentsArray.Add(GrippedActors[i].Component);
	}
}

