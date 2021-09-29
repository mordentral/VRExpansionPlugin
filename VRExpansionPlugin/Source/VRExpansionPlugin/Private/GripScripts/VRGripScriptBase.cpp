// Fill out your copyright notice in the Description page of Project Settings.

#include "GripScripts/VRGripScriptBase.h"
#include "GripMotionControllerComponent.h"
#include "VRGripInterface.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/NetDriver.h"

 
UVRGripScriptBase::UVRGripScriptBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
//	PrimaryComponentTick.bCanEverTick = false;
//	PrimaryComponentTick.bStartWithTickEnabled = false;
//	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
	WorldTransformOverrideType = EGSTransformOverrideType::None;
	bDenyAutoDrop = false;
	bDenyLateUpdates = false;
	bForceDrop = false;
	bIsActive = false;

	bCanEverTick = false;
	bAllowTicking = false;
}

void UVRGripScriptBase::OnEndPlay_Implementation(const EEndPlayReason::Type EndPlayReason) {};
void UVRGripScriptBase::OnBeginPlay_Implementation(UObject * CallingOwner) {};

bool UVRGripScriptBase::GetWorldTransform_Implementation(UGripMotionControllerComponent* GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) { return true; }
void UVRGripScriptBase::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRGripScriptBase::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void UVRGripScriptBase::OnSecondaryGrip_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRGripScriptBase::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}


EGSTransformOverrideType UVRGripScriptBase::GetWorldTransformOverrideType() { return WorldTransformOverrideType; }
bool UVRGripScriptBase::IsScriptActive() { return bIsActive; }
//bool UVRGripScriptBase::Wants_DenyAutoDrop() { return bDenyAutoDrop; }
//bool UVRGripScriptBase::Wants_DenyLateUpdates() { return bDenyLateUpdates; }
//bool UVRGripScriptBase::Wants_ToForceDrop() { return bForceDrop; }
bool UVRGripScriptBase::Wants_DenyTeleport_Implementation(UGripMotionControllerComponent * Controller) { return false; }
void UVRGripScriptBase::HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation * HandleInfo, FTransform & KinPose) {}
void UVRGripScriptBase::HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation * HandleInfo) {}

UVRGripScriptBase* UVRGripScriptBase::GetGripScriptByClass(UObject* WorldContextObject, TSubclassOf<UVRGripScriptBase> GripScriptClass, EBPVRResultSwitch& Result)
{
	if (WorldContextObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		TArray<UVRGripScriptBase*> GripScripts;
		if (IVRGripInterface::Execute_GetGripScripts(WorldContextObject, GripScripts))
		{
			for (UVRGripScriptBase* Script : GripScripts)
			{
				if (Script && Script->IsA(GripScriptClass))
				{
					Result = EBPVRResultSwitch::OnSucceeded;
					return Script;
				}
			}
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
	return nullptr;
}

void UVRGripScriptBase::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	// Uobject has no replicated props
	//Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate here if required
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass());
	if (BPClass != NULL)
	{
		BPClass->GetLifetimeBlueprintReplicationList(OutLifetimeProps);
	}
}

void UVRGripScriptBase::Tick(float DeltaTime)
{
	// Do nothing by default
}

bool UVRGripScriptBase::IsTickable() const
{
	return bAllowTicking;
}

UWorld* UVRGripScriptBase::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

bool UVRGripScriptBase::IsTickableInEditor() const
{
	return false;
}

bool UVRGripScriptBase::IsTickableWhenPaused() const
{
	return false;
}

ETickableTickType UVRGripScriptBase::GetTickableTickType() const
{
	if(IsTemplate(RF_ClassDefaultObject))
		return ETickableTickType::Never;

	return bCanEverTick ? ETickableTickType::Conditional : ETickableTickType::Never;
}

TStatId UVRGripScriptBase::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVRGripScriptBase, STATGROUP_Tickables);
}

void UVRGripScriptBase::SetTickEnabled(bool bTickEnabled)
{
	bAllowTicking = bTickEnabled;
}


// Not currently compiling in editor builds....not entirely sure why...
/*
void UVRGripScriptBase::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{

	// In the grippables pre replication to pass it on
#ifndef WITH_EDITOR
	// Run pre-replication for any grip scripts
	if (GripLogicScripts.Num())
	{
		if (UNetDriver* NetDriver = GetNetDriver())
		{
			for (UVRGripScriptBase* Script : GripLogicScripts)
			{
				if (Script && !Script->IsPendingKill())
				{
					Script->PreReplication(*((IRepChangedPropertyTracker *)NetDriver->FindOrCreateRepChangedPropertyTracker(Script).Get()));
				}
			}
		}
	}
#endif

	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass());
	if (BPClass != NULL)
	{
		BPClass->InstancePreReplication(this, ChangedPropertyTracker);
	}
}*/

bool UVRGripScriptBase::CallRemoteFunction(UFunction * Function, void * Parms, FOutParmRec * OutParms, FFrame * Stack)
{
	bool bProcessed = false;

	if (AActor* MyOwner = GetOwner())
	{
		FWorldContext* const Context = GEngine->GetWorldContextFromWorld(GetWorld());
		if (Context != nullptr)
		{
			for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
			{
				if (Driver.NetDriver != nullptr && Driver.NetDriver->ShouldReplicateFunction(MyOwner, Function))
				{
					Driver.NetDriver->ProcessRemoteFunction(MyOwner, Function, Parms, OutParms, Stack, this);

					bProcessed = true;
				}
			}
		}
	}

	return bProcessed;
}

int32 UVRGripScriptBase::GetFunctionCallspace(UFunction * Function, FFrame * Stack)
{
	AActor* Owner = GetOwner();

	if (HasAnyFlags(RF_ClassDefaultObject) || !IsSupportedForNetworking() || !Owner)
	{
		// This handles absorbing authority/cosmetic
		return GEngine->GetGlobalFunctionCallspace(Function, this, Stack);
	}

	// Owner is certified valid now
	return Owner->GetFunctionCallspace(Function, Stack);
}

FTransform UVRGripScriptBase::GetGripTransform(const FBPActorGripInformation &Grip, const FTransform & ParentTransform)
{
	return Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;
}

USceneComponent * UVRGripScriptBase::GetParentSceneComp()
{
	UObject* ParentObj = this->GetParent();

	if (USceneComponent * PrimParent = Cast<USceneComponent>(ParentObj))
	{
		return PrimParent;
	}
	else if (AActor * ParentActor = Cast<AActor>(ParentObj))
	{
		return ParentActor->GetRootComponent();
	}

	return nullptr;
}

FTransform UVRGripScriptBase::GetParentTransform(bool bGetWorldTransform, FName BoneName)
{
	UObject* ParentObj = this->GetParent();

	if (USceneComponent* PrimParent = Cast<USceneComponent>(ParentObj))
	{
		if (BoneName != NAME_None)
		{
			return PrimParent->GetSocketTransform(BoneName);
		}
		else
		{
			return PrimParent->GetComponentTransform();
		}
	}
	else if (AActor* ParentActor = Cast<AActor>(ParentObj))
	{
		return ParentActor->GetActorTransform();
	}

	return FTransform::Identity;
}

FBodyInstance * UVRGripScriptBase::GetParentBodyInstance(FName OptionalBoneName)
{
	UObject * ParentObj = this->GetParent();

	if (UPrimitiveComponent * PrimParent = Cast<UPrimitiveComponent>(ParentObj))
	{
		return PrimParent->GetBodyInstance(OptionalBoneName);
	}
	else if (AActor * ParentActor = Cast<AActor>(ParentObj))
	{
		if (UPrimitiveComponent * Prim = Cast<UPrimitiveComponent>(ParentActor->GetRootComponent()))
		{
			return Prim->GetBodyInstance(OptionalBoneName);
		}
	}

	return nullptr;
}

UObject * UVRGripScriptBase::GetParent()
{
	return this->GetOuter();
}

AActor * UVRGripScriptBase::GetOwner()
{
	UObject * myOuter = this->GetOuter();

	if (!myOuter)
		return nullptr;

	if (AActor * ActorOwner = Cast<AActor>(myOuter))
	{
		return ActorOwner;
	}
	else if (UActorComponent * ComponentOwner = Cast<UActorComponent>(myOuter))
	{
		return ComponentOwner->GetOwner();
	}

	return nullptr;
}

bool UVRGripScriptBase::HasAuthority()
{
	if (AActor * MyOwner = GetOwner())
	{
		return MyOwner->GetLocalRole() == ROLE_Authority;
	}

	return false;
}

bool UVRGripScriptBase::IsServer()
{
	if (AActor * MyOwner = GetOwner())
	{
		return MyOwner->GetNetMode() < ENetMode::NM_Client;
	}

	return false;
}

UWorld* UVRGripScriptBase::GetWorld() const
{
	if (IsTemplate())
		return nullptr;

	if (GIsEditor && !GIsPlayInEditorWorld)
	{
		return nullptr;
	}
	else if (UObject * Outer = GetOuter())
	{
		return Outer->GetWorld();
	}

	return nullptr;
}

void UVRGripScriptBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnEndPlay(EndPlayReason);
}

void UVRGripScriptBase::BeginPlay(UObject * CallingOwner)
{
	if (bAlreadyNotifiedPlay)
		return;

	bAlreadyNotifiedPlay = true;

	// Notify the subscripts about begin play
	OnBeginPlay(CallingOwner);
}

void UVRGripScriptBase::PostInitProperties()
{
	Super::PostInitProperties();

	//Called in game, when World exist . BeginPlay will not be called in editor
	if (GetWorld())
	{
		if (AActor* Owner = GetOwner())
		{
			if (Owner->IsActorInitialized())
			{
				BeginPlay(GetOwner());
			}
		}
	}
}

void UVRGripScriptBaseBP::Tick(float DeltaTime)
{
	ReceiveTick(DeltaTime);
}