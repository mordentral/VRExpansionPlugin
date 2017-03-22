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
//For UE4 Profiler ~ Stat
DECLARE_CYCLE_STAT(TEXT("TickGrip ~ TickingGrip"), STAT_TickGrip, STATGROUP_TickGrip);

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
	bAlwaysSendTickGrip = false;
	bAutoActivate = true;

	this->SetIsReplicated(true);

	// Default 100 htz update rate, same as the 100htz update rate of rep_notify, will be capped to 90/45 though because of vsync on HMD
	//bReplicateControllerTransform = true;
	ControllerNetUpdateRate = 100.0f; // 100 htz is default
	ControllerNetUpdateCount = 0.0f;
	bReplicateWithoutTracking = false;
	bLerpingPosition = false;
	bSmoothReplicatedMotion = false;
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

		DropObject(GrippedActors[i].GrippedObject, false);	
	}
	GrippedActors.Empty();

	for (int i = 0; i < LocallyGrippedActors.Num(); i++)
	{
		DestroyPhysicsHandle(LocallyGrippedActors[i]);

		DropObject(LocallyGrippedActors[i].GrippedObject, false);
	}
	LocallyGrippedActors.Empty();

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
	NewInfo.HandledObject = GripInfo.GrippedObject;

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

void UGripMotionControllerComponent::FViewExtension::ProcessGripArrayLateUpdatePrimitives(TArray<FBPActorGripInformation> & GripArray)
{
	for (FBPActorGripInformation actor : GripArray)
	{
		// Skip actors that are colliding if turning off late updates during collision.
		// Also skip turning off late updates for SweepWithPhysics, as it should always be locked to the hand

		// Don't allow late updates with server sided movement, there is no point
		if (actor.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !MotionControllerComponent->IsServer())
			continue;

		switch (actor.GripLateUpdateSetting)
		{
		case EGripLateUpdateSettings::LateUpdatesAlwaysOff:
		{
			continue;
		}break;
		case EGripLateUpdateSettings::NotWhenColliding:
		{
			if (actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics && actor.GripCollisionType != EGripCollisionType::PhysicsOnly)
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
				(actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics && actor.GripCollisionType != EGripCollisionType::PhysicsOnly) ||
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
			//case EGripTargetType::InteractibleActorGrip:
		{
			AActor * pActor = actor.GetGrippedActor();
			if (pActor)
			{
				if (USceneComponent * rootComponent = pActor->GetRootComponent())
				{
					GatherLateUpdatePrimitives(rootComponent, LateUpdatePrimitives);
				}
			}

		}break;

		case EGripTargetType::ComponentGrip:
			//case EGripTargetType::InteractibleComponentGrip:
		{
			UPrimitiveComponent * cPrimComp = actor.GetGrippedComponent();
			if (cPrimComp)
			{
				GatherLateUpdatePrimitives(cPrimComp, LateUpdatePrimitives);
			}
		}break;
		}
	}
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


	// Was going to use a lambda here but the overhead cost is higher than just using another function, even more so than using an inline one
	ProcessGripArrayLateUpdatePrimitives(MotionControllerComponent->LocallyGrippedActors);
	ProcessGripArrayLateUpdatePrimitives(MotionControllerComponent->GrippedActors);

	/*for (FBPActorGripInformation actor : MotionControllerComponent->GrippedActors)
	{
		// Skip actors that are colliding if turning off late updates during collision.
		// Also skip turning off late updates for SweepWithPhysics, as it should always be locked to the hand

		// Don't allow late updates with server sided movement, there is no point
		if (actor.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !MotionControllerComponent->IsServer())
			continue;

		switch (actor.GripLateUpdateSetting)
		{
		case EGripLateUpdateSettings::LateUpdatesAlwaysOff:
		{
			continue;
		}break;
		case EGripLateUpdateSettings::NotWhenColliding:
		{
			if (actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics && actor.GripCollisionType != EGripCollisionType::PhysicsOnly)
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
				(actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics && actor.GripCollisionType != EGripCollisionType::PhysicsOnly) ||
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
			//case EGripTargetType::InteractibleActorGrip:
		{
			AActor * pActor = actor.GetGrippedActor();
			if (pActor)
			{
				if (USceneComponent * rootComponent = pActor->GetRootComponent())
				{
					GatherLateUpdatePrimitives(rootComponent, LateUpdatePrimitives);
				}
			}

		}break;

		case EGripTargetType::ComponentGrip:
			//case EGripTargetType::InteractibleComponentGrip:
		{
			UPrimitiveComponent * cPrimComp = actor.GetGrippedComponent();
			if (cPrimComp)
			{
				GatherLateUpdatePrimitives(cPrimComp, LateUpdatePrimitives);
			}
		}break;
		}

	}*/
}

void UGripMotionControllerComponent::GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity)
{
	UPrimitiveComponent * primComp = Grip.GetGrippedComponent();//Grip.Component;
	AActor * pActor = Grip.GetGrippedActor();

	if (!primComp && pActor)
		primComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if (!primComp)
	{
		AngularVelocity = FVector::ZeroVector;
		LinearVelocity = FVector::ZeroVector;
		return;
	}

	AngularVelocity = primComp->GetPhysicsAngularVelocity();
	LinearVelocity = primComp->GetPhysicsLinearVelocity();
}

void UGripMotionControllerComponent::GetGripByActor(FBPActorGripInformation &Grip, AActor * ActorToLookForGrip, EBPVRResultSwitch &Result)
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

	for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
	{
		if (LocallyGrippedActors[i] == ActorToLookForGrip)
		{
			Grip = LocallyGrippedActors[i];
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::GetGripByComponent(FBPActorGripInformation &Grip, UPrimitiveComponent * ComponentToLookForGrip, EBPVRResultSwitch &Result)
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

	for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
	{
		if (LocallyGrippedActors[i] == ComponentToLookForGrip)
		{
			Grip = LocallyGrippedActors[i];
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripCollisionType(const FBPActorGripInformation &Grip, EBPVRResultSwitch &Result, EGripCollisionType NewGripCollisionType)
{
	int fIndex = GrippedActors.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedActors[fIndex].GripCollisionType = NewGripCollisionType;
		ReCreateGrip(GrippedActors[fIndex]);
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedActors.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedActors[fIndex].GripCollisionType = NewGripCollisionType;
			ReCreateGrip(LocallyGrippedActors[fIndex]);
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripLateUpdateSetting(const FBPActorGripInformation &Grip, EBPVRResultSwitch &Result, EGripLateUpdateSettings NewGripLateUpdateSetting)
{
	int fIndex = GrippedActors.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedActors[fIndex].GripLateUpdateSetting = NewGripLateUpdateSetting;
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedActors.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedActors[fIndex].GripLateUpdateSetting = NewGripLateUpdateSetting;
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripRelativeTransform(
	const FBPActorGripInformation &Grip,
	EBPVRResultSwitch &Result,
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
	else
	{
		fIndex = LocallyGrippedActors.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedActors[fIndex].RelativeTransform = NewRelativeTransform;
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripAdditionTransform(
	const FBPActorGripInformation &Grip,
	EBPVRResultSwitch &Result,
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
	else
	{
		fIndex = LocallyGrippedActors.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedActors[fIndex].AdditionTransform = CreateGripRelativeAdditionTransform(Grip, NewAdditionTransform, bMakeGripRelative);

			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
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
bool UGripMotionControllerComponent::GripObject(
	UObject * ObjectToGrip,
	const FTransform &WorldOffset,
	bool bWorldOffsetIsRelative,
	FName OptionalSnapToSocketName,
	EGripCollisionType GripCollisionType,
	EGripLateUpdateSettings GripLateUpdateSetting,
	EGripMovementReplicationSettings GripMovementReplicationSetting,
	float GripStiffness,
	float GripDamping)
{
	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ObjectToGrip))
	{
		return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName,GripCollisionType,GripLateUpdateSetting,GripMovementReplicationSetting,GripStiffness,GripDamping);
	}
	else if (AActor * Actor = Cast<AActor>(ObjectToGrip))
	{
		return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName, GripCollisionType, GripLateUpdateSetting, GripMovementReplicationSetting, GripStiffness, GripDamping);
	}

	return false;
}

bool UGripMotionControllerComponent::DropObject(
	UObject * ObjectToDrop,
	bool bSimulate,
	FVector OptionalAngularVelocity,
	FVector OptionalLinearVelocity)
{
	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ObjectToDrop))
	{
		return DropComponent(PrimComp, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
	}
	else if (AActor * Actor = Cast<AActor>(ObjectToDrop))
	{
		return DropActor(Actor, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
	}

	return false;
}

bool UGripMotionControllerComponent::GripObjectByInterface(UObject * ObjectToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative, bool bIsSlotGrip)
{
	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ObjectToGrip))
	{
		AActor * Owner = PrimComp->GetOwner();

		if (!Owner)
			return false;

		if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType;
			if (bIsSlotGrip)
				CollisionType = IVRGripInterface::Execute_SlotGripType(PrimComp);
			else
				CollisionType = IVRGripInterface::Execute_FreeGripType(PrimComp);

			return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(PrimComp),
				IVRGripInterface::Execute_GripMovementReplicationType(PrimComp),
				IVRGripInterface::Execute_GripStiffness(PrimComp),
				IVRGripInterface::Execute_GripDamping(PrimComp)
				);
		}
		else if (Owner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType;
			if (bIsSlotGrip)
				CollisionType = IVRGripInterface::Execute_SlotGripType(Owner);
			else
				CollisionType = IVRGripInterface::Execute_FreeGripType(Owner);

			return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(Owner),
				IVRGripInterface::Execute_GripMovementReplicationType(Owner),
				IVRGripInterface::Execute_GripStiffness(Owner),
				IVRGripInterface::Execute_GripDamping(Owner)
				);
		}
		else
		{
			// No interface, no grip
			return false;
		}
	}
	else if (AActor * Actor = Cast<AActor>(ObjectToGrip))
	{
		UPrimitiveComponent * root = Cast<UPrimitiveComponent>(Actor->GetRootComponent());

		if (!root)
			return false;

		if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType;
			if (bIsSlotGrip)
				CollisionType = IVRGripInterface::Execute_SlotGripType(root);
			else
				CollisionType = IVRGripInterface::Execute_FreeGripType(root);

			return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(root),
				IVRGripInterface::Execute_GripMovementReplicationType(root),
				IVRGripInterface::Execute_GripStiffness(root),
				IVRGripInterface::Execute_GripDamping(root)
				);
		}
		else if (Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType;
			if (bIsSlotGrip)
				CollisionType = IVRGripInterface::Execute_SlotGripType(Actor);
			else
				CollisionType = IVRGripInterface::Execute_FreeGripType(Actor);

			return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(Actor),
				IVRGripInterface::Execute_GripMovementReplicationType(Actor),
				IVRGripInterface::Execute_GripStiffness(Actor),
				IVRGripInterface::Execute_GripDamping(Actor)
				);
		}
		else
		{
			// No interface, no grip
			return false;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::DropObjectByInterface(UObject * ObjectToDrop, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ObjectToDrop))
	{
		AActor * Owner = PrimComp->GetOwner();

		if (!Owner)
			return false;

		if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropComponent(PrimComp, IVRGripInterface::Execute_SimulateOnDrop(PrimComp), OptionalAngularVelocity, OptionalLinearVelocity);
		}
		else if (Owner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropComponent(PrimComp, IVRGripInterface::Execute_SimulateOnDrop(Owner), OptionalAngularVelocity, OptionalLinearVelocity);
		}
		else
		{
			// Allowing for failsafe dropping here.
			return DropComponent(PrimComp, true, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}
	else if (AActor * Actor = Cast<AActor>(ObjectToDrop))
	{
		UPrimitiveComponent * root = Cast<UPrimitiveComponent>(Actor->GetRootComponent());

		if (!root)
			return false;

		if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropActor(Actor, IVRGripInterface::Execute_SimulateOnDrop(root), OptionalAngularVelocity, OptionalLinearVelocity);
		}
		else if (Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropActor(Actor, IVRGripInterface::Execute_SimulateOnDrop(Actor), OptionalAngularVelocity, OptionalLinearVelocity);
		}
		else
		{
			// Failsafe drop here
			return DropActor(Actor, true, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}

	return false;
}


bool UGripMotionControllerComponent::GripActor(
	AActor* ActorToGrip, 
	const FTransform &WorldOffset, 
	bool bWorldOffsetIsRelative, 
	FName OptionalSnapToSocketName, 
	EGripCollisionType GripCollisionType, 
	EGripLateUpdateSettings GripLateUpdateSetting,
	EGripMovementReplicationSettings GripMovementReplicationSetting,
	float GripStiffness, 
	float GripDamping
	)
{
	bool bIsLocalGrip = GripMovementReplicationSetting == EGripMovementReplicationSettings::LocalOnly_Not_Replicated;

	if (!IsServer() && !bIsLocalGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was called on the client side as a replicated grip"));
		return false;
	}

	if (!ActorToGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an invalid actor"));
		return false;
	}

	// Checking both arrays
	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i].GrippedObject == ActorToGrip)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already gripped actor"));
			return false;
		}
	}

	for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
	{
		if (LocallyGrippedActors[i].GrippedObject == ActorToGrip)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already locally gripped actor"));
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
	UObject * ObjectToCheck = NULL; // Used if having to calculate the transform

	if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(root))
			return false; // Interface is saying not to grip it right now

		bIsInteractible = IVRGripInterface::Execute_IsInteractible(root);

		ObjectToCheck = root;
	}
	else if (ActorToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ActorToGrip))
			return false; // Interface is saying not to grip it right now

		bIsInteractible = IVRGripInterface::Execute_IsInteractible(ActorToGrip);

		ObjectToCheck = ActorToGrip;
	}

	// So that events caused by sweep and the like will trigger correctly
	ActorToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.GrippedObject = ActorToGrip;
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

	//if (bIsInteractible)
	//	newActorGrip.GripTargetType = EGripTargetType::InteractibleActorGrip;
	//else
	newActorGrip.GripTargetType = EGripTargetType::ActorGrip;

	if (OptionalSnapToSocketName.IsValid() && root->DoesSocketExist(OptionalSnapToSocketName))
	{
		// I inverse it so that laying out the sockets makes sense
		FTransform sockTrans = root->GetSocketTransform(OptionalSnapToSocketName, ERelativeTransformSpace::RTS_Component);
		newActorGrip.RelativeTransform = sockTrans.Inverse();
		newActorGrip.RelativeTransform.SetScale3D(ActorToGrip->GetActorScale3D());

		ObjectToCheck = NULL; // Null it back out, socketed grips don't use this
	}
	else if (bWorldOffsetIsRelative)
		newActorGrip.RelativeTransform = WorldOffset;
	else
	{
		newActorGrip.RelativeTransform = ConvertToControllerRelativeTransform(WorldOffset, ObjectToCheck);
	}

	if (!bIsLocalGrip)
		GrippedActors.Add(newActorGrip);
	else
		LocallyGrippedActors.Add(newActorGrip);

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

	for (int i = LocallyGrippedActors.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedActors[i] == ActorToDrop)
		{
			return DropGrip(LocallyGrippedActors[i], bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}

	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side with a replicated grip"));
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
	EGripCollisionType GripCollisionType,
	EGripLateUpdateSettings GripLateUpdateSetting,
	EGripMovementReplicationSettings GripMovementReplicationSetting,
	float GripStiffness, 
	float GripDamping
	)
{

	bool bIsLocalGrip = GripMovementReplicationSetting == EGripMovementReplicationSettings::LocalOnly_Not_Replicated;

	if (!IsServer() && !bIsLocalGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was called on the client side with a replicating grip"));
		return false;
	}

	if (!ComponentToGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an invalid or already gripped component"));
		return false;
	}

	//Checking both arrays for grip overlap
	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i].GrippedObject == ComponentToGrip)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already gripped component"));
			return false;
		}
	}

	for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
	{
		if (LocallyGrippedActors[i].GrippedObject == ComponentToGrip)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already locally gripped component"));
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
	UObject * ObjectToCheck = NULL;

	if (ComponentToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ComponentToGrip))
		return false; // Interface is saying not to grip it right now

		bIsInteractible = IVRGripInterface::Execute_IsInteractible(ComponentToGrip);
		ObjectToCheck = ComponentToGrip;
	}

	ComponentToGrip->IgnoreActorWhenMoving(this->GetOwner(), true);
	// So that events caused by sweep and the like will trigger correctly

	ComponentToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.GrippedObject = ComponentToGrip;
	
	if(ComponentToGrip->GetOwner())
		newActorGrip.bOriginalReplicatesMovement = ComponentToGrip->GetOwner()->bReplicateMovement;

	newActorGrip.Stiffness = GripStiffness;
	newActorGrip.Damping = GripDamping;

//	if (bIsInteractible)
//		newActorGrip.GripTargetType = EGripTargetType::InteractibleComponentGrip;
//	else
	newActorGrip.GripTargetType = EGripTargetType::ComponentGrip;

	// Ignore late update setting if it doesn't make sense with the grip
	switch (newActorGrip.GripCollisionType)
	{
	case EGripCollisionType::ManipulationGrip:
	{
		newActorGrip.GripLateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOff; // Late updates are bad for this grip
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

		ObjectToCheck = NULL; // Null it out, socketed grips don't use this
	}
	else if (bWorldOffsetIsRelative)
		newActorGrip.RelativeTransform = WorldOffset;
	else
		newActorGrip.RelativeTransform = ConvertToControllerRelativeTransform(WorldOffset, ObjectToCheck);

	if (!bIsLocalGrip)
		GrippedActors.Add(newActorGrip);
	else
		LocallyGrippedActors.Add(newActorGrip);

	NotifyGrip(newActorGrip);

	return true;
}

bool UGripMotionControllerComponent::DropComponent(UPrimitiveComponent * ComponentToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!ComponentToDrop)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid component"));
		return false;
	}

	for (int i = LocallyGrippedActors.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedActors[i] == ComponentToDrop)
		{
			return DropGrip(LocallyGrippedActors[i], bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}

	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side for a replicated grip"));
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

	int FoundIndex = 0;
	bool bWasLocalGrip = false;
	if (!LocallyGrippedActors.Find(Grip, FoundIndex)) // This auto checks if Actor and Component are valid in the == operator
	{
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side for a replicated grip"));
			return false;
		}

		if (!GrippedActors.Find(Grip, FoundIndex)) // This auto checks if Actor and Component are valid in the == operator
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid drop"));
			return false;
		}

		bWasLocalGrip = false;
	}
	else
		bWasLocalGrip = true;


	UPrimitiveComponent * PrimComp = nullptr;

	AActor * pActor = nullptr;
	if (bWasLocalGrip)
	{
		PrimComp = LocallyGrippedActors[FoundIndex].GetGrippedComponent();
		pActor = LocallyGrippedActors[FoundIndex].GetGrippedActor();
	}
	else
	{
		PrimComp = GrippedActors[FoundIndex].GetGrippedComponent();
		pActor = GrippedActors[FoundIndex].GetGrippedActor();
	}

	if (!PrimComp && pActor)
		PrimComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if(!PrimComp)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid drop"));
		return false;
	}
	

	// Had to move in front of deletion to properly set velocity
	if (Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceClientSideMovement && (OptionalLinearVelocity != FVector::ZeroVector || OptionalAngularVelocity != FVector::ZeroVector))
	{
		PrimComp->SetPhysicsLinearVelocity(OptionalLinearVelocity);
		PrimComp->SetPhysicsAngularVelocity(OptionalAngularVelocity);
	}

	if(bWasLocalGrip)
		NotifyDrop_Implementation(LocallyGrippedActors[FoundIndex], bSimulate);
	else
		NotifyDrop(GrippedActors[FoundIndex], bSimulate);

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
	//case EGripTargetType::InteractibleActorGrip:
	{
		AActor * pActor = NewGrip.GetGrippedActor();
		if (pActor)
		{
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());
			
			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorAdd(pActor);
			}

			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnGrip(pActor, this, NewGrip);
				IVRGripInterface::Execute_SetHeld(pActor, this, true);
			}

			if (root)
			{
				// Have to turn off gravity locally
				if(NewGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer())
					root->SetEnableGravity(false);

				root->IgnoreActorWhenMoving(this->GetOwner(), true);
			}


		}
	}break;

	case EGripTargetType::ComponentGrip:
	//case EGripTargetType::InteractibleComponentGrip:
	{
		UPrimitiveComponent * primComp = NewGrip.GetGrippedComponent();
		if (primComp)
		{
			root = primComp;

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

			if (primComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnGrip(primComp, this, NewGrip);
				IVRGripInterface::Execute_SetHeld(primComp, this, true);
			}

			// Have to turn off gravity locally
			if (NewGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer())
				root->SetEnableGravity(false);

			root->IgnoreActorWhenMoving(this->GetOwner(), true);
		}
	}break;
	}

	switch (NewGrip.GripMovementReplicationSetting)
	{
	case EGripMovementReplicationSettings::ForceClientSideMovement:
	{
		if (IsServer())
		{
			switch (NewGrip.GripTargetType)
			{
			case EGripTargetType::ComponentGrip:
			{
				UPrimitiveComponent * primComp = NewGrip.GetGrippedComponent();
				if (primComp && primComp->GetOwner())
					primComp->GetOwner()->SetReplicateMovement(false);
			}break;
			case EGripTargetType::ActorGrip:
			{
				AActor * pActor = NewGrip.GetGrippedActor();
				if(pActor)
					pActor->SetReplicateMovement(false);
			} break;
			}
		}
	}break;

	case EGripMovementReplicationSettings::ForceServerSideMovement:
	{
		if (IsServer())
		{

			switch (NewGrip.GripTargetType)
			{
			case EGripTargetType::ComponentGrip:
			{
				UPrimitiveComponent * primComp = NewGrip.GetGrippedComponent();
				if (primComp && primComp->GetOwner())
					primComp->GetOwner()->SetReplicateMovement(true);
			}break;
			case EGripTargetType::ActorGrip:
			{
				AActor * pActor = NewGrip.GetGrippedActor();
				if (pActor)
					pActor->SetReplicateMovement(true);
			} break;
			}
		}
	}break;

	case EGripMovementReplicationSettings::KeepOriginalMovement:
	case EGripMovementReplicationSettings::LocalOnly_Not_Replicated:
	default:
	{}break;
	}

	bool bHasMovementAuthority = HasGripMovementAuthority(NewGrip);

	switch (NewGrip.GripCollisionType)
	{
	case EGripCollisionType::InteractiveCollisionWithPhysics:
	case EGripCollisionType::ManipulationGrip:
	{
		if(bHasMovementAuthority)
			SetUpPhysicsHandle(NewGrip);

	} break;

	// Skip collision intersects with these types, they dont need it
	case EGripCollisionType::CustomGrip:
	case EGripCollisionType::PhysicsOnly:
	case EGripCollisionType::SweepWithPhysics:
	case EGripCollisionType::InteractiveHybridCollisionWithSweep:
	case EGripCollisionType::InteractiveCollisionWithSweep:
	default: 
	{

		switch (NewGrip.GripTargetType)
		{
		case EGripTargetType::ComponentGrip:
		{
			UPrimitiveComponent * primComp = NewGrip.GetGrippedComponent();
			if (primComp)
				primComp->SetSimulatePhysics(false);
		}break;
		case EGripTargetType::ActorGrip:
		{
			AActor * pActor = NewGrip.GetGrippedActor();
			if (pActor)
				pActor->DisableComponentsSimulatePhysics();
		} break;
		}

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

	UPrimitiveComponent *root = NULL;

	switch (NewDrop.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	//case EGripTargetType::InteractibleActorGrip:
	{
		AActor * pActor = NewDrop.GetGrippedActor();

		if (pActor)
		{
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

			pActor->RemoveTickPrerequisiteComponent(this);
			this->IgnoreActorWhenMoving(pActor, false);

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorRemove(pActor);
			}

			if (root)
			{
				root->IgnoreActorWhenMoving(this->GetOwner(), false);
				
				root->SetSimulatePhysics(bSimulate);
				if (bSimulate)
					root->WakeAllRigidBodies();

				if (NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer())
					root->SetEnableGravity(true);
			}

			if (IsServer())
			{
				pActor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
			}

			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(pActor, NewDrop.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(pActor, this, NewDrop);
				IVRGripInterface::Execute_SetHeld(pActor, nullptr, false);
			}
		}
	}break;

	case EGripTargetType::ComponentGrip:
	//case EGripTargetType::InteractibleComponentGrip:
	{
		UPrimitiveComponent * primComp = NewDrop.GetGrippedComponent();
		if (primComp)
		{
			root = primComp;
			primComp->RemoveTickPrerequisiteComponent(this);

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorRemove(primComp->GetOwner());
			}

			if (root)
			{
				root->IgnoreActorWhenMoving(this->GetOwner(), false);
			
				root->SetSimulatePhysics(bSimulate);
				if (bSimulate)
					root->WakeAllRigidBodies();

				if (NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer())
					root->SetEnableGravity(true);
			}

			if (AActor * owner = primComp->GetOwner())
			{
				if (IsServer())
					owner->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);

				if (owner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					IVRGripInterface::Execute_OnChildGripRelease(owner, this, NewDrop);
				}

			}

			if (primComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(primComp, NewDrop.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(primComp, this, NewDrop);
				IVRGripInterface::Execute_SetHeld(primComp, nullptr, false);
			}
		}
	}break;
	}

	// Remove the drop from the array, can't wait around for replication as the tick function will start in on it.
	int fIndex = 0;
	if (LocallyGrippedActors.Find(NewDrop, fIndex))
	{
		LocallyGrippedActors.RemoveAt(fIndex);
	}
	else
	{
		fIndex = 0;
		if (GrippedActors.Find(NewDrop, fIndex))
		{
			GrippedActors.RemoveAt(fIndex);
		}
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
		if (Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceClientSideMovement || Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::LocalOnly_Not_Replicated)
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
	if (!GrippedActorToAddAttachment || !SecondaryPointComponent || (!GrippedActors.Num() && !LocallyGrippedActors.Num()))
		return false;


	if (GrippedActorToAddAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if (!IVRGripInterface::Execute_CanHaveDoubleGrip(GrippedActorToAddAttachment))
		{
			return false;
		}
	}

	FBPActorGripInformation * GripToUse = nullptr;

	for (int i = LocallyGrippedActors.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedActors[i].GrippedObject == GrippedActorToAddAttachment)
		{
			GripToUse = &LocallyGrippedActors[i];
			break;
		}
	}

	// Search replicated grips if not found in local
	if (!GripToUse)
	{
		// Replicated grips need to be called from server side
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called on the client side with a replicated grip"));
			return false;
		}

		for (int i = GrippedActors.Num() - 1; i >= 0; --i)
		{
			if (GrippedActors[i].GrippedObject == GrippedActorToAddAttachment)
			{
				GripToUse = &GrippedActors[i];
				break;
			}
		}
	}

	if (GripToUse)
	{
		if (bTransformIsAlreadyRelative)
			GripToUse->SecondaryRelativeLocation = OriginalTransform.GetLocation();
		else
			GripToUse->SecondaryRelativeLocation = OriginalTransform.GetRelativeTransform(GrippedActorToAddAttachment->GetTransform()).GetLocation();

		GripToUse->SecondaryAttachment = SecondaryPointComponent;
		GripToUse->bHasSecondaryAttachment = true;
		GripToUse->SecondarySmoothingScaler = FMath::Clamp(SecondarySmoothingScaler, 0.01f, 1.0f);

		if (GripToUse->GripLerpState == EGripLerpState::EndLerp)
			LerpToTime = 0.0f;

		if (LerpToTime > 0.0f)
		{
			GripToUse->LerpToRate = LerpToTime;
			GripToUse->GripLerpState = EGripLerpState::StartLerp;
			GripToUse->curLerp = LerpToTime;
		}

		if (GrippedActorToAddAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			IVRGripInterface::Execute_OnSecondaryGrip(GrippedActorToAddAttachment, SecondaryPointComponent, *GripToUse);
		}

		GripToUse = nullptr;

		return true;
	}

	return false;
}

bool UGripMotionControllerComponent::RemoveSecondaryAttachmentPoint(AActor * GrippedActorToRemoveAttachment, float LerpToTime)
{
	if (!GrippedActorToRemoveAttachment || (!GrippedActors.Num() && !LocallyGrippedActors.Num()))
		return false;

	FBPActorGripInformation * GripToUse = nullptr;

	// Duplicating the logic for each array for now
	for (int i = LocallyGrippedActors.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedActors[i].GrippedObject == GrippedActorToRemoveAttachment)
		{
			GripToUse = &LocallyGrippedActors[i];
			break;
		}
	}

	// Check replicated grips if it wasn't found in local
	if (!GripToUse)
	{
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController remove secondary attachment function was called on the client side for a replicating grip"));
			return false;
		}

		for (int i = GrippedActors.Num() - 1; i >= 0; --i)
		{
			if (GrippedActors[i].GrippedObject == GrippedActorToRemoveAttachment)
			{
				GripToUse = &GrippedActors[i];
				break;
			}
		}
	}

	// Handle the grip if it was found
	if (GripToUse)
	{
		if (GripToUse->GripLerpState == EGripLerpState::StartLerp)
			LerpToTime = 0.0f;

		if (LerpToTime > 0.0f)
		{
			UPrimitiveComponent * primComp = nullptr;

			switch (GripToUse->GripTargetType)
			{
			case EGripTargetType::ComponentGrip:
			{
				primComp = GripToUse->GetGrippedComponent();
			}break;
			case EGripTargetType::ActorGrip:
			{
				AActor * pActor = GripToUse->GetGrippedActor();
				if (pActor)
					primComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());
			} break;
			}

			if (primComp)
			{
				GripToUse->LerpToRate = LerpToTime;
				GripToUse->GripLerpState = EGripLerpState::EndLerp;
				GripToUse->curLerp = LerpToTime;
			}
			else
			{
				GripToUse->LerpToRate = 0.0f;
				GripToUse->GripLerpState = EGripLerpState::NotLerping;
			}
		}
		else
		{
			GripToUse->LerpToRate = 0.0f;
			GripToUse->GripLerpState = EGripLerpState::NotLerping;
		}

		if (GrippedActorToRemoveAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			IVRGripInterface::Execute_OnSecondaryGripRelease(GrippedActorToRemoveAttachment, GripToUse->SecondaryAttachment, *GripToUse);
		}

		GripToUse->SecondaryAttachment = nullptr;
		GripToUse->bHasSecondaryAttachment = false;
		GripToUse = nullptr;

		return true;
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrippedActor(AActor * GrippedActorToMove)
{
	if (!GrippedActorToMove || (!GrippedActors.Num() && !LocallyGrippedActors.Num()))
		return false;

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	for (int i = LocallyGrippedActors.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedActors[i].GrippedObject == GrippedActorToMove)
		{
			return TeleportMoveGrip(LocallyGrippedActors[i]);
		}
	}

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].GrippedObject == GrippedActorToMove)
		{
			return TeleportMoveGrip(GrippedActors[i]);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrippedComponent(UPrimitiveComponent * ComponentToMove)
{
	if (!ComponentToMove || (!GrippedActors.Num() && !LocallyGrippedActors.Num()))
		return false;

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	for (int i = LocallyGrippedActors.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedActors[i].GrippedObject == ComponentToMove)
		{
			return TeleportMoveGrip(LocallyGrippedActors[i]);
		}
	}

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].GrippedObject == ComponentToMove)
		{
			return TeleportMoveGrip(GrippedActors[i]);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrip(const FBPActorGripInformation &Grip, bool bIsPostTeleport)
{
	bool bHasMovementAuthority = HasGripMovementAuthority(Grip);

	if (!bHasMovementAuthority)
		return false;

	UPrimitiveComponent * PrimComp = NULL;
	AActor * actor = NULL;

	switch (Grip.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	//case EGripTargetType::InteractibleActorGrip:
	{
		actor = Grip.GetGrippedActor();
		if (actor)
		{
			PrimComp = Cast<UPrimitiveComponent>(actor->GetRootComponent());
		}
	}break;

	case EGripTargetType::ComponentGrip:
	//case EGripTargetType::InteractibleComponentGrip:
	{
		PrimComp = Grip.GetGrippedComponent();

		if(PrimComp)
		actor = PrimComp->GetOwner();
	}break;

	}

	if (!PrimComp || !actor)
		return false;

	// Check if either implements the interface
	bool bRootHasInterface = false;
	bool bActorHasInterface = false;

	if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		bRootHasInterface = true;
	}
	if (actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		// Actor grip interface is checked after component
		bActorHasInterface = true;
	}


	// Only use with actual teleporting
	if (bIsPostTeleport)
	{
		EGripInterfaceTeleportBehavior TeleportBehavior = EGripInterfaceTeleportBehavior::TeleportAllComponents;
		bool bSimulateOnDrop = false;

		// Check for interaction interface
		if (bRootHasInterface)
		{
			TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(PrimComp);
			bSimulateOnDrop = IVRGripInterface::Execute_SimulateOnDrop(PrimComp);
		}
		else if (bActorHasInterface)
		{
			// Actor grip interface is checked after component
			TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(actor);
			bSimulateOnDrop = IVRGripInterface::Execute_SimulateOnDrop(actor);
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
			if (IsServer() || Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::LocalOnly_Not_Replicated)
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

	GetGripWorldTransform(0.0f, WorldTransform, ParentTransform, copyGrip, actor, PrimComp, bRootHasInterface, bActorHasInterface);

	//WorldTransform = Grip.RelativeTransform * ParentTransform;

	// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
	
	FBPActorPhysicsHandleInformation * Handle = GetPhysicsGrip(Grip);

	if (!Handle)
	{
		PrimComp->SetWorldTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);
	}
	else if (Handle && Handle->KinActorData && bIsPostTeleport)
	{
		PrimComp->SetWorldTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);

		USkeletalMeshComponent * skele = Cast<USkeletalMeshComponent>(PrimComp);
	//	FTransform trans = P2UTransform(Handle->KinActorData->getGlobalPose());

		// This corrects for root bone transforms not being handled with physics in the engine
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
					Handle->KinActorData->setKinematicTarget(U2PTransform(WorldTransform) * Handle->COMPosition);
					Handle->KinActorData->setGlobalPose(U2PTransform(WorldTransform) * Handle->COMPosition);
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
	if (!GrippedActors.Num() && !LocallyGrippedActors.Num())
		return;

	for (int i = 0; i < LocallyGrippedActors.Num(); i++)
	{
		TeleportMoveGrip(LocallyGrippedActors[i], true);
	}

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
		FVector Position;
		FRotator Orientation;

		if (!bUseWithoutTracking)
		{
			if (!ViewExtension.IsValid() && GEngine)
			{
				TSharedPtr< FViewExtension, ESPMode::ThreadSafe > NewViewExtension(new FViewExtension(this));
				ViewExtension = NewViewExtension;
				GEngine->ViewExtensions.Add(ViewExtension);
			}

			// This is the owning player, now you can get the controller's location and rotation from the correct source
			bTracked = PollControllerState(Position, Orientation);

			if (bTracked)
			{
				SetRelativeLocationAndRotation(Position, Orientation);
			}
		}

		if (!bTracked && !bUseWithoutTracking)
			return; // Don't update anything including location

		// Don't bother with any of this if not replicating transform
		if (bReplicates && (bTracked || bReplicateWithoutTracking))
		{
			if (bTracked)
			{
				ReplicatedControllerTransform.Position = Position;
				ReplicatedControllerTransform.SetRotation(Orientation);//.Orientation = Orientation;
			}
			else
			{
				ReplicatedControllerTransform.Position = this->RelativeLocation;
				ReplicatedControllerTransform.SetRotation(this->RelativeRotation);
			}

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
	else
	{
		if (bLerpingPosition)
		{
			ControllerNetUpdateCount += DeltaTime;
			float LerpVal = FMath::Clamp(ControllerNetUpdateCount / (1.0f / ControllerNetUpdateRate), 0.0f, 1.0f);

			if (LerpVal >= 1.0f)
			{
				SetRelativeLocationAndRotation(ReplicatedControllerTransform.UnpackedLocation, ReplicatedControllerTransform.UnpackedRotation);

				// Stop lerping, wait for next update if it is delayed or lost then it will hitch here
				// Actual prediction might be something to consider in the future, but rough to do in VR
				// considering the speed and accuracy of movements
				// would like to consider sub stepping but since there is no server rollback...not sure how useful it would be
				// and might be perf taxing enough to not make it worth it.
				bLerpingPosition = false;
				ControllerNetUpdateCount = 0.0f;
			}
			else
			{
				// Removed variables to speed this up a bit
				SetRelativeLocationAndRotation(
					FMath::Lerp(LastUpdatesRelativePosition, ReplicatedControllerTransform.UnpackedLocation, LerpVal), 
					FMath::Lerp(LastUpdatesRelativeRotation, ReplicatedControllerTransform.UnpackedRotation, LerpVal)
				);
			}
		}
	}

	// Process the gripped actors
	TickGrip(DeltaTime);

}

void UGripMotionControllerComponent::GetGripWorldTransform(float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface)
{
	// Check for interaction interface and modify transform by it
	if (bRootHasInterface && IVRGripInterface::Execute_IsInteractible(root))
	{
		WorldTransform = HandleInteractionSettings(DeltaTime, ParentTransform, root, IVRGripInterface::Execute_GetInteractionSettings(root), Grip);
	}
	else if (bActorHasInterface && IVRGripInterface::Execute_IsInteractible(actor))
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

	SCOPE_CYCLE_COUNTER(STAT_TickGrip);

	// Debug test that we aren't floating physics handles
	check(PhysicsGrips.Num() <= (GrippedActors.Num() + LocallyGrippedActors.Num()));

	FTransform ParentTransform = this->GetComponentTransform();
	FVector MotionControllerLocDelta = this->GetComponentLocation() - LastControllerLocation;

	// Set the last controller world location for next frame
	LastControllerLocation = this->GetComponentLocation();

	// Split into separate functions so that I didn't have to combine arrays since I have some removal going on
	HandleGripArray(GrippedActors, ParentTransform, MotionControllerLocDelta, DeltaTime, true);
	HandleGripArray(LocallyGrippedActors, ParentTransform, MotionControllerLocDelta, DeltaTime);

}

void UGripMotionControllerComponent::HandleGripArray(TArray<FBPActorGripInformation> &GrippedObjects, const FTransform & ParentTransform, const FVector &MotionControllerLocDelta, float DeltaTime, bool bReplicatedArray)
{
	if (GrippedObjects.Num())
	{
		FTransform WorldTransform;

		for (int i = GrippedObjects.Num() - 1; i >= 0; --i)
		{
			if (!HasGripMovementAuthority(GrippedObjects[i]))
				continue;

			FBPActorGripInformation * Grip = &GrippedObjects[i];

			if (!Grip)
				continue;

			if (Grip->GrippedObject && !Grip->GrippedObject->IsPendingKill())
			{
				UPrimitiveComponent *root = NULL;
				AActor *actor = NULL;

				// Getting the correct variables depending on the grip target type
				switch (Grip->GripTargetType)
				{
					case EGripTargetType::ActorGrip:
					//case EGripTargetType::InteractibleActorGrip:
					{
						actor = Grip->GetGrippedActor();
						if(actor)
							root = Cast<UPrimitiveComponent>(actor->GetRootComponent());
					}break;

					case EGripTargetType::ComponentGrip:
					//case EGripTargetType::InteractibleComponentGrip :
					{
						root = Grip->GetGrippedComponent();
						if(root)
							actor = root->GetOwner();
					}break;

				default:break;
				}

				// Last check to make sure the variables are valid
				if (!root || !actor)
					continue;

				// #TODO: Should this even be here? Or should I enforce destructible components being sub components and users doing proper cleanup?
#if WITH_APEX
				// Checking for a gripped destructible object, and if held and is fractured, auto drop it and continue
				if (UDestructibleComponent * dest = Cast<UDestructibleComponent>(root))
				{
					if (!dest->ApexDestructibleActor->getChunkPhysXActor(0)) // Fractured - lost its scene actor
					{
						UE_LOG(LogVRMotionController, Warning, TEXT("Gripped Destructible Component has been fractured, auto dropping it"));
						CleanUpBadGrip(GrippedObjects, i, bReplicatedArray);
						continue;
					}
				}
#endif // #if WITH_APEX

				// Check if either implements the interface
				bool bRootHasInterface = false;
				bool bActorHasInterface = false;
				
				if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					bRootHasInterface = true;
				}
				if (actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					// Actor grip interface is checked after component
					bActorHasInterface = true;
				}

				if (Grip->GripCollisionType == EGripCollisionType::CustomGrip)
				{
					// Don't perform logic on the movement for this object, just pass in the GripTick() event with the controller difference instead
					if (bRootHasInterface)
					{
						IVRGripInterface::Execute_TickGrip(root, this, *Grip, MotionControllerLocDelta, DeltaTime);
					}
					
					if (bActorHasInterface)
					{
						IVRGripInterface::Execute_TickGrip(actor, this, *Grip, MotionControllerLocDelta, DeltaTime);
					}

					continue;
				}
				
				// Get the world transform for this grip after handling secondary grips and interaction differences
				GetGripWorldTransform(DeltaTime, WorldTransform, ParentTransform, *Grip, actor, root, bRootHasInterface, bActorHasInterface);

				// Auto drop based on distance from expected point
				// Not perfect, should be done post physics or in next frame prior to changing controller location
				// However I don't want to recalculate world transform
				// Maybe add a grip variable of "expected loc" and use that to check next frame, but for now this will do.
				if (IsServer() && (bRootHasInterface || bActorHasInterface) &&
					(
						(
							Grip->GripCollisionType != EGripCollisionType::PhysicsOnly &&
							Grip->GripCollisionType != EGripCollisionType::SweepWithPhysics) &&
						(Grip->GripCollisionType != EGripCollisionType::InteractiveHybridCollisionWithSweep ||
							Grip->GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithSweep && Grip->bColliding)
						)
					)
				{
					float BreakDistance = 0.0f;
					if (bRootHasInterface)
					{
						BreakDistance = IVRGripInterface::Execute_GripBreakDistance(root);
					}
					else if (bActorHasInterface)
					{
						// Actor grip interface is checked after component
						BreakDistance = IVRGripInterface::Execute_GripBreakDistance(actor);
					}

					if (BreakDistance > 0.0f)
					{
						FVector CheckDistance;
						if (!GetPhysicsJointLength(*Grip, root, CheckDistance))
						{
							CheckDistance = (WorldTransform.GetLocation() - root->GetComponentLocation());
						}

						if (CheckDistance.Size() >= BreakDistance)
						{
							switch (Grip->GripTargetType)
							{
							case EGripTargetType::ComponentGrip:
							//case EGripTargetType::InteractibleComponentGrip:
							{
								if (bRootHasInterface)
									DropComponent(root, IVRGripInterface::Execute_SimulateOnDrop(root));
								else
									DropComponent(root, IVRGripInterface::Execute_SimulateOnDrop(actor));
							}break;
							case EGripTargetType::ActorGrip:
							//case EGripTargetType::InteractibleActorGrip:
							{
								if (bRootHasInterface)
									DropActor(actor, IVRGripInterface::Execute_SimulateOnDrop(root));
								else
									DropActor(actor, IVRGripInterface::Execute_SimulateOnDrop(actor));
							}break;
							}

							// Don't bother moving it, dropped now
							continue;
						}
					}
				}

				// Start handling the grip types and their functions
				switch (Grip->GripCollisionType)
				{
					case EGripCollisionType::InteractiveCollisionWithPhysics:
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

					}break;

					case EGripCollisionType::InteractiveCollisionWithSweep:
					{
						FVector OriginalPosition(root->GetComponentLocation());
						FVector NewPosition(WorldTransform.GetTranslation());

						if (!Grip->bIsLocked)
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
					}break;

					case EGripCollisionType::InteractiveHybridCollisionWithSweep:
					{

						// Make sure that there is no collision on course before turning off collision and snapping to controller
						FBPActorPhysicsHandleInformation * GripHandle = GetPhysicsGrip(*Grip);

						//if (Grip->bColliding)
						//{
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
								//Grip->bColliding = false;

								// Check with next intended location and rotation
								Hits.Empty();
								//FComponentQueryParams Params(NAME_None, this->GetOwner());
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
						//}
						//else if (!Grip->bColliding)
						//{
							// Check for overlap beginning
						/*	TArray<FOverlapResult> Hits;
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
							}*/
						//}

						if (!Grip->bColliding)
						{
							if (GripHandle)
							{
								DestroyPhysicsHandle(*Grip);

								switch (Grip->GripTargetType)
								{
								case EGripTargetType::ComponentGrip:
								{
									root->SetSimulatePhysics(false);
								}break;
								case EGripTargetType::ActorGrip:
								{
									actor->DisableComponentsSimulatePhysics();
								} break;
								}
							}

							FTransform OrigTransform = root->GetComponentTransform();

							FHitResult OutHit;
							root->SetWorldTransform(WorldTransform, true, &OutHit);

							if (OutHit.bBlockingHit)
							{
								Grip->bColliding = true;
								root->SetWorldTransform(OrigTransform, false);
								root->SetSimulatePhysics(true);

								SetUpPhysicsHandle(*Grip);
								UpdatePhysicsHandleTransform(*Grip, WorldTransform);
							}
							else
							{
								Grip->bColliding = false;
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

					}break;

					case EGripCollisionType::SweepWithPhysics:
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

					}break;

					case EGripCollisionType::PhysicsOnly:
					{
						// Move the actor, we are not offsetting by the hit result anyway
						root->SetWorldTransform(WorldTransform, false);
					}break;

					case EGripCollisionType::ManipulationGrip:
					{
						UpdatePhysicsHandleTransform(*Grip, WorldTransform);
					}break;

					default:
					{}break;
				}

				// We only do this if specifically requested, it has a slight perf hit and isn't normally needed for non Custom Grip types
				if (bAlwaysSendTickGrip)
				{
					// All non custom grips tick after translation, this is still pre physics so interactive grips location will be wrong, but others will be correct
					if (bRootHasInterface)
					{
						IVRGripInterface::Execute_TickGrip(root, this, *Grip, MotionControllerLocDelta, DeltaTime);
					}

					if (bActorHasInterface)
					{
						IVRGripInterface::Execute_TickGrip(actor, this, *Grip, MotionControllerLocDelta, DeltaTime);
					}
				}
			}
			else
			{
				// Object has been destroyed without notification to plugin
				CleanUpBadGrip(GrippedObjects, i, bReplicatedArray);
			}
		}
	}
}


void UGripMotionControllerComponent::CleanUpBadGrip(TArray<FBPActorGripInformation> &GrippedObjects, int GripIndex, bool bReplicatedArray)
{
	// Object has been destroyed without notification to plugin

	// Clean up tailing physics handles with null objects
	for (int g = PhysicsGrips.Num() - 1; g >= 0; --g)
	{
		if (!PhysicsGrips[g].HandledObject || PhysicsGrips[g].HandledObject == GrippedObjects[GripIndex].GrippedObject || PhysicsGrips[g].HandledObject->IsPendingKill())
		{
			// Need to delete it from the physics thread
			DestroyPhysicsHandle(PhysicsGrips[g].SceneIndex, &PhysicsGrips[g].HandleData, &PhysicsGrips[g].KinActorData);
			PhysicsGrips.RemoveAt(g);
		}
	}

	// Doesn't work, uses the object as the search parameter which can now be null
	//	DestroyPhysicsHandle(*Grip);

	if (!bReplicatedArray || IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("Gripped object was null or destroying, auto dropping it"));
		GrippedObjects.RemoveAt(GripIndex); // If it got garbage collected then just remove the pointer, won't happen with new uproperty use, but keeping it here anyway
	}
}


FTransform UGripMotionControllerComponent::HandleInteractionSettings(float DeltaTime, const FTransform & ParentTransform, UPrimitiveComponent * root, FBPInteractionSettings InteractionSettings, FBPActorGripInformation & GripInfo)
{
	FTransform LocalTransform = GripInfo.RelativeTransform * GripInfo.AdditionTransform;
	FTransform WorldTransform;
	
	if (InteractionSettings.bIgnoreHandRotation)
	{
		FTransform RotationalessTransform = ParentTransform;
		RotationalessTransform.SetRotation(FQuat::Identity);

		WorldTransform = LocalTransform * RotationalessTransform;
	}
	else
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

	// Rotation Settings
	if (InteractionSettings.bLimitPitch)
		componentRot.Pitch = FMath::Clamp(componentRot.Pitch, InteractionSettings.InitialAngularTranslation.Pitch + InteractionSettings.MinAngularTranslation.Pitch, InteractionSettings.InitialAngularTranslation.Pitch + InteractionSettings.MaxAngularTranslation.Pitch);
		
	if (InteractionSettings.bLimitYaw)
		componentRot.Yaw = FMath::Clamp(componentRot.Yaw, InteractionSettings.InitialAngularTranslation.Yaw + InteractionSettings.MinAngularTranslation.Yaw, InteractionSettings.InitialAngularTranslation.Yaw + InteractionSettings.MaxAngularTranslation.Yaw);

	if (InteractionSettings.bLimitRoll)
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
		{
			return false;
		}
	#endif // WITH_PHYSX

	return true;
}

bool UGripMotionControllerComponent::DestroyPhysicsHandle(const FBPActorGripInformation &Grip)
{
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
	UPrimitiveComponent *root = NewGrip.GetGrippedComponent();
	AActor * pActor = NewGrip.GetGrippedActor();

	if(!root && pActor)
		root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());
	
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
		PxVec3 KinLocation;
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
			trans.SetLocation(rBodyInstance->GetCOMPosition());
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
				HandleInfo->COMPosition = PxTransform( U2PVector(rBodyInstance->GetUnrealWorldTransform().InverseTransformPosition(rBodyInstance->GetCOMPosition())));
				NewJoint = PxD6JointCreate(Scene->getPhysics(), KinActor, PxTransform(PxIdentity), Actor, /*PxTransform(PxIdentity)*/HandleInfo->COMPosition);
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
					
					NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);

					NewJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);

					PxD6JointDrive drive = PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
					NewJoint->setDrive(PxD6Drive::eX, drive);
					NewJoint->setDrive(PxD6Drive::eY, drive);
					NewJoint->setDrive(PxD6Drive::eZ, drive);
					NewJoint->setDrive(PxD6Drive::eTWIST, drive);
				}
				else
				{
					PxD6JointDrive drive = PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
					PxD6JointDrive Angledrive = PxD6JointDrive(NewGrip.Stiffness * 1.5f, NewGrip.Damping * 1.4f, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);

					// Setting up the joint
					NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
						
					NewJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);
		
					NewJoint->setDrivePosition(PxTransform(PxVec3(0, 0, 0)));

					NewJoint->setDrive(PxD6Drive::eX, drive);
					NewJoint->setDrive(PxD6Drive::eY, drive);
					NewJoint->setDrive(PxD6Drive::eZ, drive);
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

bool UGripMotionControllerComponent::GetPhysicsJointLength(const FBPActorGripInformation &GrippedActor, UPrimitiveComponent * rootComp, FVector & LocOut)
{
	if (!GrippedActor.GrippedObject)
		return false;

	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(GrippedActor);

	if (!HandleInfo || !HandleInfo->KinActorData)
		return false;

#if WITH_PHYSX
	if (!HandleInfo->HandleData)
		return false;
	// This is supposed to be the difference between the actor and the kinactor / constraint base
	FTransform tran3 = P2UTransform(HandleInfo->HandleData->getLocalPose(PxJointActorIndex::eACTOR1));
	
	FTransform rr = rootComp->GetComponentTransform();
	// Physx location throws out scale, this is where the problem was
	rr.SetScale3D(FVector(1,1,1)); 
	// Make the local pose global
	tran3 = tran3 * rr;

	// Get the global pose for the kin actor
	FTransform kinPose = P2UTransform(HandleInfo->KinActorData->getGlobalPose());

	// Return the difference
	LocOut = FTransform::SubtractTranslations(kinPose, tran3);

	return true;
#else
	return false;
#endif // WITH_PHYSX
}

void UGripMotionControllerComponent::UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform)
{
	if (!GrippedActor.GrippedObject)
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

			KinActor->setKinematicTarget(PxTransform(U2PTransform(terns))/*PNewLocation, PNewOrientation*/);
		}
		else
		{
			USkeletalMeshComponent * skele = NULL;

			switch (GrippedActor.GripTargetType)
			{
			case EGripTargetType::ComponentGrip:
			{
				skele = Cast<USkeletalMeshComponent>(GrippedActor.GetGrippedComponent());
			}break;
			case EGripTargetType::ActorGrip:
			{
				skele = Cast<USkeletalMeshComponent>(GrippedActor.GetGrippedActor()->GetRootComponent());
			} break;
			}

			if (skele)
			{
				terns.ConcatenateRotation(skele->GetBoneTransform(0, FTransform::Identity).GetRotation());
			}

			KinActor->setKinematicTarget(PxTransform(U2PTransform(terns)) * HandleInfo->COMPosition/*PNewLocation, PNewOrientation*/);
		}
		

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
	if ((PlayerIndex != INDEX_NONE) && bHasAuthority)
	{
		// New iteration and retrieval for 4.12
		TArray<IMotionController*> MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
		for (auto MotionController : MotionControllers)
		{
			if ((MotionController != nullptr) && MotionController->GetControllerOrientationAndPosition(PlayerIndex, Hand, Orientation, Position))
			{
				CurrentTrackingStatus = (ETrackingStatus)MotionController->GetControllerTrackingStatus(PlayerIndex, Hand);
				
				if (bOffsetByHMD)
				{
					if (IsInGameThread())
					{
						if (GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed() && GEngine->HMDDevice->HasValidTrackingPosition())
						{
							FQuat curRot;
							FVector curLoc;
							GEngine->HMDDevice->GetCurrentOrientationAndPosition(curRot, curLoc);
							curLoc.Z = 0;

							LastLocationForLateUpdate = curLoc;
						}
						else
							LastLocationForLateUpdate = FVector::ZeroVector;
					}

					Position -= LastLocationForLateUpdate;
				}
				
				
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
	if (!Component || Component->IsPendingKill())
		return;

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
		if(GrippedActors[i].GetGrippedActor())
			GrippedActorsArray.Add(GrippedActors[i].GetGrippedActor());
	}

	for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
	{
		if (LocallyGrippedActors[i].GetGrippedActor())
			GrippedActorsArray.Add(LocallyGrippedActors[i].GetGrippedActor());
	}

}

void UGripMotionControllerComponent::GetGrippedComponents(TArray<UPrimitiveComponent*> &GrippedComponentsArray)
{
	for (int i = 0; i < GrippedActors.Num(); ++i)
	{
		if (GrippedActors[i].GetGrippedComponent())
			GrippedComponentsArray.Add(GrippedActors[i].GetGrippedComponent());
	}

	for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
	{
		if (LocallyGrippedActors[i].GetGrippedComponent())
			GrippedComponentsArray.Add(LocallyGrippedActors[i].GetGrippedComponent());
	}
}

