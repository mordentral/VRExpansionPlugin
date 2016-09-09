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

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 11
#else
#include "Features/IModularFeatures.h"
#endif

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

	PlayerIndex = 0;
	Hand = EControllerHand::Left;
	bDisableLowLatencyUpdate = false;
	bHasAuthority = false;
	bUseWithoutTracking = false;

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
		DropActor(GrippedActors[i].Actor, false);
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
		if ((PhysicsGrips[i].Actor && PhysicsGrips[i].Actor == GripInfo.Actor) || PhysicsGrips[i].Component && PhysicsGrips[i].Component == GripInfo.Component)
			return &PhysicsGrips[i];
	}

	return nullptr;
}


int UGripMotionControllerComponent::GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo)
{
	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		if ((PhysicsGrips[i].Actor && PhysicsGrips[i].Actor == GripInfo.Actor) || (PhysicsGrips[i].Component && PhysicsGrips[i].Component == GripInfo.Component))
			return i;
	}

	return 0;
}

FBPActorPhysicsHandleInformation * UGripMotionControllerComponent::CreatePhysicsGrip(const FBPActorGripInformation & GripInfo)
{
	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		if ((PhysicsGrips[i].Actor && PhysicsGrips[i].Actor == GripInfo.Actor) || (PhysicsGrips[i].Component && PhysicsGrips[i].Component == GripInfo.Component))
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
		if (actor.bTurnOffLateUpdateWhenColliding && actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics)
			continue;

		if (actor.Actor)
		{
			// Get primitive components attached to the actor and update them with the late motion controller offset
			if (USceneComponent * rootComponent = actor.Actor->GetRootComponent())
			{
				GatherLateUpdatePrimitives(rootComponent, LateUpdatePrimitives);
			}

			// No idea why I was using this originally....with children this would have cause issues...wtf was I thinking
			//TInlineComponentArray<UPrimitiveComponent*> PrimComps(actor.Actor);
			
			/*for (UPrimitiveComponent * PrimitiveComponent : PrimComps)
			{
				GatherLateUpdatePrimitives(PrimitiveComponent, LateUpdatePrimitives);
			}*/
		}
		else if (actor.Component)
		{
			GatherLateUpdatePrimitives(actor.Component, LateUpdatePrimitives);
		}

	}
}

bool UGripMotionControllerComponent::GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity)
{
	UPrimitiveComponent * primComp = Grip.Component;

	if (!primComp && Grip.Actor)
		primComp = Cast<UPrimitiveComponent>(Grip.Actor->GetRootComponent());

	if (!primComp)
		return false;

	AngularVelocity = primComp->GetPhysicsAngularVelocity();
	LinearVelocity = primComp->GetPhysicsLinearVelocity();

	return true;
}

bool UGripMotionControllerComponent::GripActor(
	AActor* ActorToGrip, 
	const FTransform &WorldOffset, 
	bool bWorldOffsetIsRelative, 
	FName OptionalSnapToSocketName, 
	TEnumAsByte<EGripCollisionType> GripCollisionType,  
	float GripStiffness, 
	float GripDamping, 
	bool bTurnOffLateUpdateWhenColliding
	)
{
	if (!bIsServer)
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

	// So that events caused by sweep and the like will trigger correctly
	ActorToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.Actor = ActorToGrip;
	newActorGrip.bOriginalReplicatesMovement = ActorToGrip->bReplicateMovement;
	newActorGrip.Stiffness = GripStiffness;
	newActorGrip.Damping = GripDamping;
	newActorGrip.bTurnOffLateUpdateWhenColliding = bTurnOffLateUpdateWhenColliding;

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
	
	NotifyGrip(newActorGrip);
	GrippedActors.Add(newActorGrip);

	return true;
}

bool UGripMotionControllerComponent::DropActor(AActor* ActorToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!ActorToDrop)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid actor"));
		return false;
	}
	
	if (!bIsServer)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side"));
		return false;
	}

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == ActorToDrop)
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
	float GripStiffness, 
	float GripDamping, 
	bool bTurnOffLateUpdateWhenColliding
	)
{
	if (!bIsServer)
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
	newActorGrip.bTurnOffLateUpdateWhenColliding = bTurnOffLateUpdateWhenColliding;

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

	NotifyGrip(newActorGrip);
	GrippedActors.Add(newActorGrip);

	return true;
}

bool UGripMotionControllerComponent::DropComponent(UPrimitiveComponent * ComponentToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!bIsServer)
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
		if (GrippedActors[i].Component == ComponentToDrop)
		{
			return DropGrip(GrippedActors[i], bSimulate,OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}
	return false;
}

bool UGripMotionControllerComponent::DropGrip(const FBPActorGripInformation &Grip, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!bIsServer)
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
	if (!PrimComp)
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

	GrippedActors.RemoveAt(FoundIndex);
		
	return true;
}

// No longer an RPC, now is called from RepNotify so that joining clients also correctly set up grips
void UGripMotionControllerComponent::NotifyGrip/*_Implementation*/(const FBPActorGripInformation &NewGrip)
{

	switch (NewGrip.GripCollisionType.GetValue())
	{
	case EGripCollisionType::InteractiveCollisionWithPhysics:
	{
		if (bIsServer)
		{
			if (NewGrip.Component && NewGrip.Component->GetOwner())
				NewGrip.Component->GetOwner()->SetReplicateMovement(false);
			else if (NewGrip.Actor)
				NewGrip.Actor->SetReplicateMovement(false);
		}
		
		SetUpPhysicsHandle(NewGrip);

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

		if (bIsServer)
		{
			if (NewGrip.Component && NewGrip.Component->GetOwner())
				NewGrip.Component->GetOwner()->SetReplicateMovement(false);
			else if(NewGrip.Actor)
				NewGrip.Actor->SetReplicateMovement(false);
		}
	} break;

	}

	UPrimitiveComponent *root = NewGrip.Component;

	if (!root && NewGrip.Actor)
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
	}
	else if (root)
	{
		root->SetEnableGravity(false);
		root->IgnoreActorWhenMoving(this->GetOwner(), true);

		if (root->GetOwner())
		{
			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorAdd(root->GetOwner());
			}
		}
	}

	// Move it to the correct location automatically
	TeleportMoveGrip(NewGrip);
}

void UGripMotionControllerComponent::NotifyDrop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate)
{

	DestroyPhysicsHandle(NewDrop);

	if (NewDrop.Actor)
	{
		NewDrop.Actor->RemoveTickPrerequisiteComponent(this);
		this->IgnoreActorWhenMoving(NewDrop.Actor, false);

		if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
		{
			OwningPawn->MoveIgnoreActorRemove(NewDrop.Actor);
		}

		if (bIsServer)
			NewDrop.Actor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
	}
	else if (NewDrop.Component)
	{
		NewDrop.Component->RemoveTickPrerequisiteComponent(this);

		if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
		{
			OwningPawn->MoveIgnoreActorRemove(NewDrop.Component->GetOwner());
		}

		if (NewDrop.Component->GetOwner())
		{
			if (bIsServer)
				NewDrop.Component->GetOwner()->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
		}
	}

	UPrimitiveComponent *root = NewDrop.Component;

	if (!root && NewDrop.Actor)
	{
		root = Cast<UPrimitiveComponent>(NewDrop.Actor->GetRootComponent());

		if (root)
		{
			root->IgnoreActorWhenMoving(this->GetOwner(), false);
			root->SetSimulatePhysics(bSimulate);
			if(bSimulate)
				root->WakeAllRigidBodies();
			root->SetEnableGravity(true);
		}
	}
	else if (root)
	{
		root->IgnoreActorWhenMoving(this->GetOwner(), false);
		root->SetSimulatePhysics(bSimulate);
		if(bSimulate)
			root->WakeAllRigidBodies();
		root->SetEnableGravity(true);
	}
}

bool UGripMotionControllerComponent::AddSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment, USceneComponent * SecondaryPointComponent, bool bUsePrimaryRoll, float SecondaryInfluenceScaler)
{
	if (!bIsServer)
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
			if (UPrimitiveComponent * rootComp = Cast<UPrimitiveComponent>(GrippedActors[i].Actor->GetRootComponent()))
			{
				GrippedActors[i].SecondaryRelativeTransform = SecondaryPointComponent->GetComponentTransform().GetRelativeTransform(rootComp->GetComponentTransform());
			}
			GrippedActors[i].SecondaryAttachment = SecondaryPointComponent;
			GrippedActors[i].bHasSecondaryAttachment = true;
			GrippedActors[i].bUsePrimaryRoll = bUsePrimaryRoll;
			GrippedActors[i].fSecondaryInfluenceScaler = SecondaryInfluenceScaler;
			return true;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::RemoveSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment)
{
	if (!bIsServer)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController remove secondary attachment function was called on the client side"));
		return false;
	}

	if (!GrippedActorToAddAttachment || !GrippedActors.Num())
		return false;

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == GrippedActorToAddAttachment)
		{
			GrippedActors[i].SecondaryAttachment = nullptr;
			GrippedActors[i].bHasSecondaryAttachment = false;
			return false;
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

bool UGripMotionControllerComponent::TeleportMoveGrip(const FBPActorGripInformation &Grip)
{
	int FoundIndex = 0;
	if (!GrippedActors.Find(Grip, FoundIndex))
		return false;

	UPrimitiveComponent * PrimComp = GrippedActors[FoundIndex].Component;

	if (!PrimComp && GrippedActors[FoundIndex].Actor)
		PrimComp = Cast<UPrimitiveComponent>(GrippedActors[FoundIndex].Actor->GetRootComponent());

	if (!PrimComp)
		return false;

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	// GetRelativeTransformReverse had some serious floating point errors associated with it
	// Not sure whats wrong with the function but I might want to push a patch out eventually
	WorldTransform = GrippedActors[FoundIndex].RelativeTransform.GetRelativeTransform(InverseTransform);

	// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
	PrimComp->SetWorldTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);

	FBPActorPhysicsHandleInformation * Handle = GetPhysicsGrip(GrippedActors[FoundIndex]);
	if (Handle && Handle->KinActorData)
	{
#if WITH_PHYSX
	{
		PxScene* PScene = GetPhysXSceneFromIndex(Handle->SceneIndex);
		if (PScene)
		{
			SCOPED_SCENE_WRITE_LOCK(PScene);
			Handle->KinActorData->setKinematicTarget(PxTransform(U2PVector(WorldTransform.GetLocation()), Handle->KinActorData->getGlobalPose().q));
			Handle->KinActorData->setGlobalPose(PxTransform(U2PVector(WorldTransform.GetLocation()), Handle->KinActorData->getGlobalPose().q));
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

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		// GetRelativeTransformReverse had some serious floating point errors associated with it
		// Not sure whats wrong with the function but I might want to push a patch out eventually
		WorldTransform = GrippedActors[i].RelativeTransform.GetRelativeTransform(InverseTransform);

		if (GrippedActors[i].Actor)
		{
			// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
			GrippedActors[i].Actor->SetActorTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);
		}
		else if (GrippedActors[i].Component)
		{
			// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
			GrippedActors[i].Component->SetWorldTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);
		}

		FBPActorPhysicsHandleInformation * Handle = GetPhysicsGrip(GrippedActors[i]);
		if (Handle && Handle->KinActorData)
		{
			#if WITH_PHYSX
			{
				PxScene* PScene = GetPhysXSceneFromIndex(Handle->SceneIndex);
				if (PScene)
				{
					SCOPED_SCENE_WRITE_LOCK(PScene);
					Handle->KinActorData->setKinematicTarget(PxTransform(U2PVector(WorldTransform.GetLocation()), Handle->KinActorData->getGlobalPose().q));
					Handle->KinActorData->setGlobalPose(PxTransform(U2PVector(WorldTransform.GetLocation()), Handle->KinActorData->getGlobalPose().q));
				}
			}
			#endif
			//Handle->KinActorData->setGlobalPose(PxTransform(U2PVector(WorldTransform.GetLocation()), Handle->KinActorData->getGlobalPose().q));

			if (GrippedActors[i].Actor)
			{
				UPrimitiveComponent *root = Cast<UPrimitiveComponent>(GrippedActors[i].Actor->GetRootComponent());
				if (root)
				{
					FBodyInstance * body = root->GetBodyInstance();
					if (body)
					{
						body->SetBodyTransform(WorldTransform, ETeleportType::TeleportPhysics);
					}
				}
			}
			else if (GrippedActors[i].Component)
			{
				FBodyInstance * body = GrippedActors[i].Component->GetBodyInstance();
				if (body)
				{
					body->SetBodyTransform(WorldTransform, ETeleportType::TeleportPhysics);
				}
			}
		}
		/*else
		{
			if (bIsServer)
			{
				DestroyPhysicsHandle(GrippedActors[i]);
				GrippedActors.RemoveAt(i); // If it got garbage collected then just remove the pointer, won't happen with new uproperty use, but keeping it here anyway
			}
		}*/
	}
}

void UGripMotionControllerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Moved this here instead of in the polling function, it was ticking once per frame anyway so no loss of perf
	// It doesn't need to be there and now I can pre-check
	// Also epics implementation in the polling function didn't work anyway as it was based off of playercontroller which is not the owner of this controller
	
	// Cache state from the game thread for use on the render thread
	// No need to check if in game thread here as tick always is
	bHasAuthority = IsLocallyControlled();
	bIsServer = IsServer();

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

void UGripMotionControllerComponent::RotateTransformAroundPivot(FVector WorldPivot, FRotator RotationDelta, FTransform & Transform, float SlerpValue)
{
	// Compute new rotation
	const FQuat OldRotation = Transform.GetRotation();
	//const FQuat DeltaRotation = (RotationDelta).Quaternion();
	const FQuat NewRotation = FQuat::Slerp(OldRotation, RotationDelta.Quaternion(), SlerpValue);// (DeltaRotation * OldRotation);

	// Compute new location
	FVector DeltaLocation = FVector::ZeroVector;
	if (!WorldPivot.IsZero())
	{
		const FVector OldPivot = OldRotation.RotateVector(WorldPivot);
		const FVector NewPivot = NewRotation.RotateVector(WorldPivot);
		DeltaLocation = (OldPivot - NewPivot); // ConstrainDirectionToPlane() not necessary because it's done by MoveUpdatedComponent() below.
	}

	Transform.SetLocation(Transform.GetLocation() + DeltaLocation);
	Transform.SetRotation(NewRotation);
}


void UGripMotionControllerComponent::TickGrip(float DeltaTime)
{
	if (GrippedActors.Num())
	{
		FTransform WorldTransform;
		FTransform InverseTransform = this->GetComponentTransform().Inverse();

		for (int i = GrippedActors.Num() - 1; i >= 0; --i)
		{
			if (GrippedActors[i].Actor || GrippedActors[i].Component)
			{

				UPrimitiveComponent *root = GrippedActors[i].Component;
				AActor *actor = GrippedActors[i].Actor;

				if (!root)
					root = Cast<UPrimitiveComponent>(GrippedActors[i].Actor->GetRootComponent());

				if (!root)
					continue;

				if (!actor)
					actor = root->GetOwner();

				if (!actor)
					continue;

				// GetRelativeTransformReverse had some floating point errors associated with it
				// Not sure whats wrong with the function but I might want to push a patch out eventually
				WorldTransform = GrippedActors[i].RelativeTransform.GetRelativeTransform(InverseTransform);
			
				// Need to figure out best default behavior
				if (GrippedActors[i].bHasSecondaryAttachment && GrippedActors[i].SecondaryAttachment)
				{
					FTransform PivotTrans = this->GetComponentTransform().GetRelativeTransform(WorldTransform);
					FVector customPivot = (PivotTrans.GetLocation()) * WorldTransform.GetScale3D();
				
					FVector nLoc = GrippedActors[i].SecondaryRelativeTransform.GetLocation();
					nLoc.X = 0;

					FVector newLoc = WorldTransform.TransformPosition((GrippedActors[i].SecondaryAttachment->GetComponentTransform().GetRelativeTransform(WorldTransform).GetLocation() - nLoc));

					FVector BasePoint = WorldTransform.TransformPosition(FVector(PivotTrans.GetLocation().X, 0, 0));

					FRotator FinalRot = FRotationMatrix::MakeFromX(newLoc - BasePoint).Rotator();

					if(GrippedActors[i].bUsePrimaryRoll)
						FinalRot.Roll = WorldTransform.GetRotation().Rotator().Roll;

					RotateTransformAroundPivot(customPivot, FinalRot, WorldTransform, GrippedActors[i].fSecondaryInfluenceScaler);
				}

				if (GrippedActors[i].GripCollisionType == EGripCollisionType::InteractiveCollisionWithPhysics)
				{
					UpdatePhysicsHandleTransform(GrippedActors[i], WorldTransform);
					
					// Sweep current collision state
					if (bHasAuthority)
					{
						TArray<FOverlapResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);
						if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, root->GetComponentLocation(), root->GetComponentQuat(), root->GetCollisionObjectType(), Params))
							//if(GetWorld()->ComponentOverlapMulti(Hits, root, root->GetComponentLocation(), root->GetComponentQuat(), Params))
						{
							GrippedActors[i].bColliding = true;
						}
						else
						{
							GrippedActors[i].bColliding = false;							
						}
					}

				}
				else if (GrippedActors[i].GripCollisionType == EGripCollisionType::InteractiveCollisionWithSweep)
				{
					FVector OriginalPosition(root->GetComponentLocation());
					FVector NewPosition(WorldTransform.GetTranslation());

					if(!GrippedActors[i].bIsLocked)
						root->ComponentVelocity = (NewPosition - OriginalPosition) / DeltaTime;

					if (GrippedActors[i].bIsLocked)
						WorldTransform.SetRotation(GrippedActors[i].LastLockedRotation);

					FHitResult OutHit;
					// Need to use without teleport so that the physics velocity is updated for when the actor is released to throw
					root->SetWorldTransform(WorldTransform, true, &OutHit);

					if (OutHit.bBlockingHit)
					{
						GrippedActors[i].bColliding = true;

						if (!GrippedActors[i].bIsLocked)
						{
							GrippedActors[i].bIsLocked = true;
							GrippedActors[i].LastLockedRotation = root->GetComponentQuat();
						}

						//if (!actor->bReplicateMovement)
						//	actor->SetReplicateMovement(true);
					}
					else
					{
						GrippedActors[i].bColliding = false;

						if (GrippedActors[i].bIsLocked)
							GrippedActors[i].bIsLocked = false;

						//if (actor->bReplicateMovement)
						//	actor->SetReplicateMovement(false);
					}
				}
				else if (GrippedActors[i].GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithSweep)
				{

					// Make sure that there is no collision on course before turning off collision and snapping to controller
					FBPActorPhysicsHandleInformation * GripHandle = GetPhysicsGrip(GrippedActors[i]);

					if (GrippedActors[i].bColliding/* && GripHandle*/)
					{
						TArray<FOverlapResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);
						if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, root->GetComponentLocation(), root->GetComponentQuat(), root->GetCollisionObjectType(), Params))
						{
							GrippedActors[i].bColliding = true;
						}
						else
						{
							GrippedActors[i].bColliding = false;
						}
					}
					else if(!GrippedActors[i].bColliding)
					{
						TArray<FOverlapResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);
						if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, WorldTransform.GetLocation(), WorldTransform.GetRotation(), root->GetCollisionObjectType(), Params))
						{
							GrippedActors[i].bColliding = true;
						}
						else
						{
							GrippedActors[i].bColliding = false;
						}
					}

					if (!GrippedActors[i].bColliding)
					{		
						// Removing server side physics totally, it isn't worth it, client side isn't goign to be perfect but it is less jarring.
						//if (bIsServer && actor->bReplicateMovement) // So we don't call on on change event over and over locally
						//	actor->SetReplicateMovement(false);

						if (/*bIsServer && */GripHandle)
						{
							DestroyPhysicsHandle(GrippedActors[i]);

							if(GrippedActors[i].Actor)
								GrippedActors[i].Actor->DisableComponentsSimulatePhysics();
							else
								root->SetSimulatePhysics(false);
						}

						FHitResult OutHit;
						root->SetWorldTransform(WorldTransform, true, &OutHit);

						if (OutHit.bBlockingHit)
							GrippedActors[i].bColliding = true;
						//else
							//GrippedActors[i].bColliding = false;

					}
					else if (GrippedActors[i].bColliding && !GripHandle)
					{
					//	if (bIsServer && !actor->bReplicateMovement)
						//	actor->SetReplicateMovement(true);

						root->SetSimulatePhysics(true);

						//if (bIsServer)
						SetUpPhysicsHandle(GrippedActors[i]);
						UpdatePhysicsHandleTransform(GrippedActors[i], WorldTransform);
					}
					else
					{
						if (/*bIsServer && GrippedActors[i].bColliding &&*/ GripHandle)
							UpdatePhysicsHandleTransform(GrippedActors[i], WorldTransform);
					}

				}
				else if (GrippedActors[i].GripCollisionType == EGripCollisionType::SweepWithPhysics)
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
								GrippedActors[i].bColliding = true;
							}
							else
								GrippedActors[i].bColliding = false;
						}
					}

					// Move the actor, we are not offsetting by the hit result anyway
					root->SetWorldTransform(WorldTransform, false);

				}
				else if (GrippedActors[i].GripCollisionType == EGripCollisionType::PhysicsOnly)
				{
					// Move the actor, we are not offsetting by the hit result anyway
					root->SetWorldTransform(WorldTransform, false);
				}
			}
			else
			{
				DestroyPhysicsHandle(GrippedActors[i]);

				if (bIsServer)
				{
					GrippedActors.RemoveAt(i); // If it got garbage collected then just remove the pointer, won't happen with new uproperty use, but keeping it here anyway
				}
			}
		}
	}

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
	if(!root)
		root = Cast<UPrimitiveComponent>(Grip.Actor->GetRootComponent());

	if (!root)
		return false;

	root->SetEnableGravity(true);

	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(Grip);

	if (!HandleInfo)
		return true;

	DestroyPhysicsHandle(HandleInfo->SceneIndex, &HandleInfo->HandleData, &HandleInfo->KinActorData);
	PhysicsGrips.RemoveAt(GetPhysicsGripIndex(Grip));

	return true;
}

bool UGripMotionControllerComponent::SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip)
{
	UPrimitiveComponent *root = NewGrip.Component;
	if(!root)
		root = Cast<UPrimitiveComponent>(NewGrip.Actor->GetRootComponent());
	
	if (!root)
		return false;

	// Needs to be simulating in order to run physics
	root->SetSimulatePhysics(true);
	root->SetEnableGravity(false);

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
	
		// Get transform of actor we are grabbing

		FTransform WorldTransform;
		FTransform InverseTransform = this->GetComponentTransform().Inverse();
		WorldTransform = NewGrip.RelativeTransform.GetRelativeTransform(InverseTransform);

		PxVec3 KinLocation = U2PVector(WorldTransform.GetLocation() - (WorldTransform.GetLocation() - root->GetComponentLocation()));
		PxTransform GrabbedActorPose = Actor->getGlobalPose();
		PxTransform KinPose(KinLocation, GrabbedActorPose.q);

		// set target and current, so we don't need another "Tick" call to have it right
		//TargetTransform = CurrentTransform = P2UTransform(KinPose);

		// If we don't already have a handle - make one now.
		if (!HandleInfo->HandleData)
		{
			// Create kinematic actor we are going to create joint with. This will be moved around with calls to SetLocation/SetRotation.
			PxRigidDynamic* KinActor = Scene->getPhysics().createRigidDynamic(KinPose);
			KinActor->setRigidDynamicFlag(PxRigidDynamicFlag::eKINEMATIC, true);
			KinActor->setMass(0.0f); // 1.0f;
			KinActor->setMassSpaceInertiaTensor(PxVec3(0.0f, 0.0f, 0.0f));// PxVec3(1.0f, 1.0f, 1.0f));
			KinActor->setMaxDepenetrationVelocity(PX_MAX_F32);

			// No bodyinstance
			KinActor->userData = NULL;
			
			// Add to Scene
			Scene->addActor(*KinActor);

			// Save reference to the kinematic actor.
			HandleInfo->KinActorData = KinActor;

			// Create the joint
			PxVec3 LocalHandlePos = GrabbedActorPose.transformInv(KinLocation);
			PxD6Joint* NewJoint = PxD6JointCreate(Scene->getPhysics(), KinActor, PxTransform::createIdentity(), Actor, PxTransform(LocalHandlePos));
			
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
				
				// Setting up the joint
				NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
				NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
				NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);
				NewJoint->setDrivePosition(PxTransform(PxVec3(0, 0, 0)));

				NewJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
				NewJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
				NewJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);

				//UpdateDriveSettings();
				if (HandleInfo->HandleData != nullptr)
				{
					HandleInfo->HandleData->setDrive(PxD6Drive::eX, PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
					HandleInfo->HandleData->setDrive(PxD6Drive::eY, PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
					HandleInfo->HandleData->setDrive(PxD6Drive::eZ, PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));

					HandleInfo->HandleData->setDrive(PxD6Drive::eSLERP, PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));

						//HandleData->setDrive(PxD6Drive::eTWIST, PxD6JointDrive(Stiffness, Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
						//HandleData->setDrive(PxD6Drive::eSWING, PxD6JointDrive(Stiffness, Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION));
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
		KinActor->setKinematicTarget(PxTransform(PNewLocation, PNewOrientation));
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

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 11
		for (auto MotionController : GEngine->MotionControllerDevices)
		{
			if ((MotionController != nullptr) && MotionController->GetControllerOrientationAndPosition(PlayerIndex, Hand, Orientation, Position))
			{
				CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, Hand);
				return true;
			}
		}
#else
		// New iteration and retrieval for 4.12
		TArray<IMotionController*> MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
		for (auto MotionController : MotionControllers)
		{
			if ((MotionController != nullptr) && MotionController->GetControllerOrientationAndPosition(PlayerIndex, Hand, Orientation, Position))
			{
				CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, Hand);
				return true;
			}
		}
#endif
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
		GrippedActorsArray.Add(GrippedActors[i].Actor);
	}
}

