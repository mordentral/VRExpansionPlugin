// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "GripMotionControllerComponent.h"
#include "IHeadMountedDisplay.h"
//#include "DestructibleComponent.h" 4.18 moved apex destruct to a plugin
#include "Misc/ScopeLock.h"
#include "Net/UnrealNetwork.h"
#include "PrimitiveSceneInfo.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "IXRSystemAssets.h"
#include "Components/StaticMeshComponent.h"
#include "MotionDelayBuffer.h"
#include "UObject/VRObjectVersion.h"
#include "UObject/UObjectGlobals.h" // for FindObject<>
#include "IXRTrackingSystem.h"
#include "IXRSystemAssets.h"
#include "DrawDebugHelpers.h"

#include "VRBaseCharacter.h"

#include "PhysicsPublic.h"
#include "PhysicsEngine/BodySetup.h"

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

// MAGIC NUMBERS
// Constraint multipliers for angular, to avoid having to have two sets of stiffness/damping variables
const float ANGULAR_STIFFNESS_MULTIPLIER = 1.5f;
const float ANGULAR_DAMPING_MULTIPLIER = 1.4f;

// Multiplier for the Interactive Hybrid With Physics grip - When not colliding increases stiffness by this value
const float HYBRID_PHYSICS_GRIP_MULTIPLIER = 10.0f;

namespace {
	/** This is to prevent destruction of motion controller components while they are
	in the middle of being accessed by the render thread */
	FCriticalSection CritSect;

} // anonymous namespace

  // CVars
namespace GripMotionControllerCvars
{
	static int32 DrawDebugGripCOM = 0;
	FAutoConsoleVariableRef CVarDrawCOMDebugSpheres(
		TEXT("vr.DrawDebugCenterOfMassForGrips"),
		DrawDebugGripCOM,
		TEXT("When on, will draw debug speheres for physics grips COM.\n")
		TEXT("0: Disable, 1: Enable"),
		ECVF_Default);
}

  //=============================================================================
UGripMotionControllerComponent::UGripMotionControllerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bTickEvenWhenPaused = true;

	PlayerIndex = 0;
	MotionSource = FXRMotionControllerBase::LeftHandSourceId;
	//Hand = EControllerHand::Left;
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
	bReppedOnce = false;
	bOffsetByHMD = false;
	bIsPostTeleport = false;

	GripIDIncrementer = 0;

	bOffsetByControllerProfile = true;
	GripRenderThreadProfileTransform = FTransform::Identity;
	CurrentControllerProfileTransform = FTransform::Identity;
}

//=============================================================================
UGripMotionControllerComponent::~UGripMotionControllerComponent()
{
	// Moved view extension destruction to BeginDestroy like the new controllers
	// Epic had it listed as a crash in the private bug tracker I guess.
}

void UGripMotionControllerComponent::NewControllerProfileLoaded()
{
	GetCurrentProfileTransform(false);
}

void UGripMotionControllerComponent::GetCurrentProfileTransform(bool bBindToNoticationDelegate)
{
	if (bOffsetByControllerProfile)
	{
		UVRGlobalSettings* VRSettings = const_cast<UVRGlobalSettings*>(GetDefault<UVRGlobalSettings>());

		if (VRSettings == nullptr)
			return;

		EControllerHand HandType;
		this->GetHandType(HandType);

		FTransform NewControllerProfileTransform = FTransform::Identity;

		if (HandType == EControllerHand::Left || HandType == EControllerHand::AnyHand || !VRSettings->bUseSeperateHandTransforms)
		{
			NewControllerProfileTransform = VRSettings->CurrentControllerProfileTransform;
		}
		else if (HandType == EControllerHand::Right)
		{
			NewControllerProfileTransform = VRSettings->CurrentControllerProfileTransformRight;
		}

		if (bBindToNoticationDelegate && !NewControllerProfileEvent_Handle.IsValid())
		{
			NewControllerProfileEvent_Handle = VRSettings->OnControllerProfileChangedEvent.AddUObject(this, &UGripMotionControllerComponent::NewControllerProfileLoaded);
		}

		if (!NewControllerProfileTransform.Equals(CurrentControllerProfileTransform))
		{
			FTransform OriginalControllerProfileTransform = CurrentControllerProfileTransform;
			CurrentControllerProfileTransform = NewControllerProfileTransform;

			// Auto adjust for FPS testing pawns
			if (!bTracked && bUseWithoutTracking)
			{
				this->SetRelativeTransform(CurrentControllerProfileTransform * (OriginalControllerProfileTransform.Inverse() * this->GetRelativeTransform()));
			}

			OnControllerProfileTransformChanged.Broadcast(CurrentControllerProfileTransform.Inverse() * OriginalControllerProfileTransform, CurrentControllerProfileTransform);
		}
	}
}

void UGripMotionControllerComponent::OnUnregister()
{

	if (NewControllerProfileEvent_Handle.IsValid())
	{
		UVRGlobalSettings* VRSettings = const_cast<UVRGlobalSettings*>(GetDefault<UVRGlobalSettings>());
		if (VRSettings != nullptr)
		{
			VRSettings->OnControllerProfileChangedEvent.Remove(NewControllerProfileEvent_Handle);
		}
	}

	for (int i = 0; i < GrippedObjects.Num(); i++)
	{
		DestroyPhysicsHandle(GrippedObjects[i]);

		DropObjectByInterface(GrippedObjects[i].GrippedObject);
		//DropObject(GrippedObjects[i].GrippedObject, false);	
	}
	GrippedObjects.Empty();

	for (int i = 0; i < LocallyGrippedObjects.Num(); i++)
	{
		DestroyPhysicsHandle(LocallyGrippedObjects[i]);
		DropObjectByInterface(LocallyGrippedObjects[i].GrippedObject);
		//DropObject(LocallyGrippedObjects[i].GrippedObject, false);
	}
	LocallyGrippedObjects.Empty();

	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		DestroyPhysicsHandle(PhysicsGrips[i].SceneIndex, &PhysicsGrips[i].HandleData, &PhysicsGrips[i].KinActorData);
	}
	PhysicsGrips.Empty();

	Super::OnUnregister();
}

void UGripMotionControllerComponent::BeginDestroy()
{
	Super::BeginDestroy();

	if (GripViewExtension.IsValid())
	{
		{
			// This component could be getting accessed from the render thread so it needs to wait
			// before clearing MotionControllerComponent and allowing the destructor to continue
			FScopeLock ScopeLock(&CritSect);
			GripViewExtension->MotionControllerComponent = NULL;
		}

		GripViewExtension.Reset();
	}
}

void UGripMotionControllerComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UGripMotionControllerComponent::CreateRenderState_Concurrent()
{
	Super::CreateRenderState_Concurrent();
	/*GripRenderThreadRelativeTransform = GetRelativeTransform();
	GripRenderThreadComponentScale = GetComponentScale();*/
	GripRenderThreadProfileTransform = CurrentControllerProfileTransform;
}

void UGripMotionControllerComponent::SendRenderTransform_Concurrent()
{
	GripRenderThreadRelativeTransform = GetRelativeTransform();
	GripRenderThreadComponentScale = GetComponentScale();
	GripRenderThreadProfileTransform = CurrentControllerProfileTransform;

	Super::SendRenderTransform_Concurrent();
}

FBPActorPhysicsHandleInformation * UGripMotionControllerComponent::GetPhysicsGrip(const FBPActorGripInformation & GripInfo)
{
	return PhysicsGrips.FindByKey(GripInfo);
}


bool UGripMotionControllerComponent::GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo, int & index)
{
	index = PhysicsGrips.IndexOfByKey(GripInfo);
	return index != INDEX_NONE;
}

FBPActorPhysicsHandleInformation * UGripMotionControllerComponent::CreatePhysicsGrip(const FBPActorGripInformation & GripInfo)
{
	FBPActorPhysicsHandleInformation * HandleInfo = PhysicsGrips.FindByKey(GripInfo);

	if (HandleInfo)
	{
		DestroyPhysicsHandle((*HandleInfo).SceneIndex, &((*HandleInfo).HandleData), &((*HandleInfo).KinActorData));
		return HandleInfo;
	}

	FBPActorPhysicsHandleInformation NewInfo;
	NewInfo.HandledObject = GripInfo.GrippedObject;
	NewInfo.GripID = GripInfo.GripID;

	int index = PhysicsGrips.Add(NewInfo);

	return &PhysicsGrips[index];
}


//=============================================================================
void UGripMotionControllerComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	 Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Skipping the owner with this as the owner will use the controllers location directly
	DOREPLIFETIME_CONDITION(UGripMotionControllerComponent, ReplicatedControllerTransform, COND_SkipOwner);
	DOREPLIFETIME(UGripMotionControllerComponent, GrippedObjects);
	DOREPLIFETIME(UGripMotionControllerComponent, ControllerNetUpdateRate);

	DOREPLIFETIME_CONDITION(UGripMotionControllerComponent, LocallyGrippedObjects, COND_SkipOwner);
//	DOREPLIFETIME(UGripMotionControllerComponent, bReplicateControllerTransform);
}

void UGripMotionControllerComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't ever replicate these, they are getting replaced by my custom send anyway
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, false);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, false);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, false);
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

void UGripMotionControllerComponent::FGripViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (!MotionControllerComponent)
	{
		return;
	}

	// Set up the late update state for the controller component
	LateUpdate.Setup(MotionControllerComponent->CalcNewComponentToWorld(FTransform()), MotionControllerComponent, false);
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

	AngularVelocity = primComp->GetPhysicsAngularVelocityInDegrees();
	LinearVelocity = primComp->GetPhysicsLinearVelocity();
}

void UGripMotionControllerComponent::GetGripByActor(FBPActorGripInformation &Grip, AActor * ActorToLookForGrip, EBPVRResultSwitch &Result)
{
	if (!ActorToLookForGrip)
	{
		Result = EBPVRResultSwitch::OnFailed;
		return;
	}

	FBPActorGripInformation * GripInfo = GrippedObjects.FindByKey(ActorToLookForGrip);
	if(!GripInfo)
		GripInfo = LocallyGrippedObjects.FindByKey(ActorToLookForGrip);
	
	if (GripInfo)
	{
		Grip = *GripInfo;// GrippedObjects[i];
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
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

	FBPActorGripInformation * GripInfo = GrippedObjects.FindByKey(ComponentToLookForGrip);
	if(!GripInfo)
		GripInfo = LocallyGrippedObjects.FindByKey(ComponentToLookForGrip);

	if (GripInfo)
	{
		Grip = *GripInfo;// GrippedObjects[i];
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::GetGripByObject(FBPActorGripInformation &Grip, UObject * ObjectToLookForGrip, EBPVRResultSwitch &Result)
{
	if (!ObjectToLookForGrip)
	{
		Result = EBPVRResultSwitch::OnFailed;
		return;
	}

	FBPActorGripInformation * GripInfo = GrippedObjects.FindByKey(ObjectToLookForGrip);
	if(!GripInfo)
		GripInfo = LocallyGrippedObjects.FindByKey(ObjectToLookForGrip);

	if (GripInfo)
	{
		Grip = *GripInfo;// GrippedObjects[i];
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::GetGripByID(FBPActorGripInformation &Grip, uint8 IDToLookForGrip, EBPVRResultSwitch &Result)
{
	FBPActorGripInformation * GripInfo = GrippedObjects.FindByKey(IDToLookForGrip);
	if (!GripInfo)
		GripInfo = LocallyGrippedObjects.FindByKey(IDToLookForGrip);

	if (GripInfo)
	{
		Grip = *GripInfo;// GrippedObjects[i];
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripPaused(const FBPActorGripInformation &Grip, EBPVRResultSwitch &Result, bool bIsPaused, bool bNoConstraintWhenPaused)
{
	int fIndex = GrippedObjects.Find(Grip);

	FBPActorGripInformation * GripInformation = nullptr;

	if (fIndex != INDEX_NONE)
	{
		GripInformation = &GrippedObjects[fIndex];
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			GripInformation = &LocallyGrippedObjects[fIndex];
		}
	}

	if (GripInformation != nullptr)
	{
		if (bNoConstraintWhenPaused)
		{
			if (bIsPaused)
			{
				if (FBPActorPhysicsHandleInformation * PhysHandle = GetPhysicsGrip(*GripInformation))
				{
					DestroyPhysicsHandle(*GripInformation);
				}
			}
			else
			{
				ReCreateGrip(*GripInformation);
			}
		}

		GripInformation->bIsPaused = bIsPaused;
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetPausedTransform(const FBPActorGripInformation &Grip, const FTransform & PausedTransform, bool bTeleport)
{

	FBPActorGripInformation * GripInformation = nullptr;

	int fIndex = GrippedObjects.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GripInformation = &GrippedObjects[fIndex];
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			GripInformation = &LocallyGrippedObjects[fIndex];
		}
	}
	
	if (GripInformation != nullptr && GripInformation->GrippedObject != nullptr)
	{
		if (bTeleport)
		{
			FTransform ProxyTrans = PausedTransform;
			TeleportMoveGrip_Impl(*GripInformation, true, true, ProxyTrans);
		}
		else
		{
			if (FBPActorPhysicsHandleInformation * PhysHandle = GetPhysicsGrip(GrippedObjects[fIndex]))
			{
				UpdatePhysicsHandleTransform(*GripInformation, PausedTransform);
			}
			else
			{
				if (GripInformation->GripTargetType == EGripTargetType::ActorGrip)
				{
					GripInformation->GetGrippedActor()->SetActorTransform(PausedTransform);
				}
				else
				{
					GripInformation->GetGrippedComponent()->SetWorldTransform(PausedTransform);
				}
			}
		}
	}
}




void UGripMotionControllerComponent::SetGripCollisionType(const FBPActorGripInformation &Grip, EBPVRResultSwitch &Result, EGripCollisionType NewGripCollisionType)
{
	int fIndex = GrippedObjects.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedObjects[fIndex].GripCollisionType = NewGripCollisionType;
		ReCreateGrip(GrippedObjects[fIndex]);
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].GripCollisionType = NewGripCollisionType;

			if (GetNetMode() == ENetMode::NM_Client && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
				Server_NotifyLocalGripAddedOrChanged(LocallyGrippedObjects[fIndex]);

			ReCreateGrip(LocallyGrippedObjects[fIndex]);

			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripLateUpdateSetting(const FBPActorGripInformation &Grip, EBPVRResultSwitch &Result, EGripLateUpdateSettings NewGripLateUpdateSetting)
{
	int fIndex = GrippedObjects.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedObjects[fIndex].GripLateUpdateSetting = NewGripLateUpdateSetting;
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].GripLateUpdateSetting = NewGripLateUpdateSetting;

			if (GetNetMode() == ENetMode::NM_Client && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
				Server_NotifyLocalGripAddedOrChanged(LocallyGrippedObjects[fIndex]);

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
	int fIndex = GrippedObjects.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedObjects[fIndex].RelativeTransform = NewRelativeTransform;
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].RelativeTransform = NewRelativeTransform;

			if (GetNetMode() == ENetMode::NM_Client && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
				Server_NotifyLocalGripAddedOrChanged(LocallyGrippedObjects[fIndex]);

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
	int fIndex = GrippedObjects.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedObjects[fIndex].AdditionTransform = CreateGripRelativeAdditionTransform(Grip, NewAdditionTransform, bMakeGripRelative);

		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].AdditionTransform = CreateGripRelativeAdditionTransform(Grip, NewAdditionTransform, bMakeGripRelative);

			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}
	Result = EBPVRResultSwitch::OnFailed;
}

void UGripMotionControllerComponent::SetGripStiffnessAndDamping(
	const FBPActorGripInformation &Grip,
	EBPVRResultSwitch &Result,
	float NewStiffness, float NewDamping, bool bAlsoSetAngularValues, float OptionalAngularStiffness, float OptionalAngularDamping
	)
{
	Result = EBPVRResultSwitch::OnFailed;
	int fIndex = GrippedObjects.Find(Grip);

	if (fIndex != INDEX_NONE)
	{
		GrippedObjects[fIndex].Stiffness = NewStiffness;
		GrippedObjects[fIndex].Damping = NewDamping;

		if (bAlsoSetAngularValues)
		{
			GrippedObjects[fIndex].AdvancedGripSettings.PhysicsSettings.AngularStiffness = OptionalAngularStiffness;
			GrippedObjects[fIndex].AdvancedGripSettings.PhysicsSettings.AngularDamping = OptionalAngularDamping;
		}

		Result = EBPVRResultSwitch::OnSucceeded;
		SetGripConstraintStiffnessAndDamping(&GrippedObjects[fIndex]);
		//return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].Stiffness = NewStiffness;
			LocallyGrippedObjects[fIndex].Damping = NewDamping;

			if (bAlsoSetAngularValues)
			{
				LocallyGrippedObjects[fIndex].AdvancedGripSettings.PhysicsSettings.AngularStiffness = OptionalAngularStiffness;
				LocallyGrippedObjects[fIndex].AdvancedGripSettings.PhysicsSettings.AngularDamping = OptionalAngularDamping;
			}

			if (GetNetMode() == ENetMode::NM_Client && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
				Server_NotifyLocalGripAddedOrChanged(LocallyGrippedObjects[fIndex]);

			Result = EBPVRResultSwitch::OnSucceeded;
			SetGripConstraintStiffnessAndDamping(&LocallyGrippedObjects[fIndex]);
		//	return;
		}
	}
}

FTransform UGripMotionControllerComponent::CreateGripRelativeAdditionTransform_BP(
	const FBPActorGripInformation &GripToSample,
	const FTransform & AdditionTransform,
	bool bGripRelative
)
{
	return CreateGripRelativeAdditionTransform(GripToSample, AdditionTransform, bGripRelative);
}

bool UGripMotionControllerComponent::GripObject(
	UObject * ObjectToGrip,
	const FTransform &WorldOffset,
	bool bWorldOffsetIsRelative,
	FName OptionalSnapToSocketName,
	FName OptionalBoneToGripName,
	EGripCollisionType GripCollisionType,
	EGripLateUpdateSettings GripLateUpdateSetting,
	EGripMovementReplicationSettings GripMovementReplicationSetting,
	float GripStiffness,
	float GripDamping,
	bool bIsSlotGrip)
{
	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ObjectToGrip))
	{
		return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName, OptionalBoneToGripName, GripCollisionType,GripLateUpdateSetting,GripMovementReplicationSetting,GripStiffness,GripDamping, bIsSlotGrip);
	}
	else if (AActor * Actor = Cast<AActor>(ObjectToGrip))
	{
		return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName, OptionalBoneToGripName, GripCollisionType, GripLateUpdateSetting, GripMovementReplicationSetting, GripStiffness, GripDamping, bIsSlotGrip);
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

bool UGripMotionControllerComponent::GripObjectByInterface(UObject * ObjectToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative, FName OptionalBoneToGripName, bool bIsSlotGrip)
{
	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ObjectToGrip))
	{
		AActor * Owner = PrimComp->GetOwner();

		if (!Owner)
			return false;

		if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType = IVRGripInterface::Execute_GetPrimaryGripType(PrimComp, bIsSlotGrip);

			float Stiffness;
			float Damping;
			IVRGripInterface::Execute_GetGripStiffnessAndDamping(PrimComp, Stiffness, Damping);

			return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(PrimComp),
				IVRGripInterface::Execute_GripMovementReplicationType(PrimComp),
				Stiffness,
				Damping,
				bIsSlotGrip
				);
		}
		else if (Owner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType = IVRGripInterface::Execute_GetPrimaryGripType(Owner, bIsSlotGrip);

			float Stiffness;
			float Damping;
			IVRGripInterface::Execute_GetGripStiffnessAndDamping(Owner, Stiffness, Damping);

			return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(Owner),
				IVRGripInterface::Execute_GripMovementReplicationType(Owner),
				Stiffness,
				Damping,
				bIsSlotGrip
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
			EGripCollisionType CollisionType = IVRGripInterface::Execute_GetPrimaryGripType(root, bIsSlotGrip);

			float Stiffness;
			float Damping;
			IVRGripInterface::Execute_GetGripStiffnessAndDamping(root, Stiffness, Damping);

			return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(root),
				IVRGripInterface::Execute_GripMovementReplicationType(root),
				Stiffness,
				Damping,
				bIsSlotGrip
				);
		}
		else if (Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType = IVRGripInterface::Execute_GetPrimaryGripType(Actor, bIsSlotGrip);

			float Stiffness;
			float Damping;
			IVRGripInterface::Execute_GetGripStiffnessAndDamping(Actor, Stiffness, Damping);

			return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, NAME_None,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(Actor),
				IVRGripInterface::Execute_GripMovementReplicationType(Actor),
				Stiffness,
				Damping,
				bIsSlotGrip
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
	FName OptionalBoneToGripName,
	EGripCollisionType GripCollisionType, 
	EGripLateUpdateSettings GripLateUpdateSetting,
	EGripMovementReplicationSettings GripMovementReplicationSetting,
	float GripStiffness, 
	float GripDamping,
	bool bIsSlotGrip)
{
	bool bIsLocalGrip = (GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive || GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep);

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

	if (GetIsObjectHeld(ActorToGrip))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already gripped actor"));
		return false;
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

	FBPAdvGripSettings AdvancedGripSettings;
	UObject * ObjectToCheck = NULL; // Used if having to calculate the transform
	bool bIgnoreHandRotation = false;

	if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(root))
			return false; // Interface is saying not to grip it right now

		AdvancedGripSettings = IVRGripInterface::Execute_AdvancedGripSettings(root);
		ObjectToCheck = root;

		// Removed in 4.20
		/*if (IVRGripInterface::Execute_IsInteractible(root))
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("Gripping an object with bIsInteractible set, this functionality will soon be deprecated. Please use custom grips or interactible components instead."));
			FBPInteractionSettings IntSettings = IVRGripInterface::Execute_GetInteractionSettings(root);
			bIgnoreHandRotation = IntSettings.bIgnoreHandRotation;
		}*/
	}
	else if (ActorToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ActorToGrip))
			return false; // Interface is saying not to grip it right now

		AdvancedGripSettings = IVRGripInterface::Execute_AdvancedGripSettings(ActorToGrip);
		ObjectToCheck = ActorToGrip;

		// Removed in 4.20
		/*if (IVRGripInterface::Execute_IsInteractible(ActorToGrip))
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("Gripping an object with bIsInteractible set, this functionality will soon be deprecated. Please use custom grips or interactible components instead."));
			FBPInteractionSettings IntSettings = IVRGripInterface::Execute_GetInteractionSettings(ActorToGrip);
			bIgnoreHandRotation = IntSettings.bIgnoreHandRotation;
		}*/
	}

	// So that events caused by sweep and the like will trigger correctly
	ActorToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripID = GetNextGripID(bIsLocalGrip);
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.GrippedObject = ActorToGrip;
	newActorGrip.bOriginalReplicatesMovement = ActorToGrip->bReplicateMovement;
	newActorGrip.bOriginalGravity = root->IsGravityEnabled();
	newActorGrip.Stiffness = GripStiffness;
	newActorGrip.Damping = GripDamping;
	newActorGrip.AdvancedGripSettings = AdvancedGripSettings;
	newActorGrip.ValueCache.bWasInitiallyRepped = true; // Set this true on authority side so we can skip a function call on tick
	newActorGrip.bIsSlotGrip = bIsSlotGrip;
	newActorGrip.GrippedBoneName = OptionalBoneToGripName;

	// Ignore late update setting if it doesn't make sense with the grip
	switch(newActorGrip.GripCollisionType)
	{
	case EGripCollisionType::ManipulationGrip:
	case EGripCollisionType::ManipulationGripWithWristTwist:
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

	newActorGrip.GripTargetType = EGripTargetType::ActorGrip;

	if (OptionalSnapToSocketName.IsValid() && root->DoesSocketExist(OptionalSnapToSocketName))
	{
		// I inverse it so that laying out the sockets makes sense
		FTransform sockTrans = root->GetSocketTransform(OptionalSnapToSocketName, ERelativeTransformSpace::RTS_Component);
		newActorGrip.RelativeTransform = sockTrans.Inverse();
		newActorGrip.RelativeTransform.SetScale3D(ActorToGrip->GetActorScale3D());
		newActorGrip.bIsSlotGrip = true; // Set this to a slot grip

		ObjectToCheck = NULL; // Null it back out, socketed grips don't use this
	}
	else if (bWorldOffsetIsRelative)
	{
		FTransform FinalOffset = WorldOffset;
		if (bIgnoreHandRotation)
		{
			// Reconstitute the controller transform relative to the object, then remove the rotation and set it back to relative to controller
			// This could likely be done easier by just removing rotation that the object doesn't possess but for now this will do.
			FTransform compTrans = root->GetComponentTransform();

			FinalOffset = FinalOffset.Inverse() * compTrans; // Reconstitute transform
			FinalOffset.SetRotation(FQuat::Identity); // Remove rotation

			FinalOffset = compTrans.GetRelativeTransform(FinalOffset); // Set back to relative
		}
		newActorGrip.RelativeTransform = FinalOffset;
	}
	else
	{
		FTransform controllerTrans = this->GetComponentTransform();
		if (bIgnoreHandRotation)
		{
			controllerTrans.SetRotation(FQuat::Identity);
		}
		newActorGrip.RelativeTransform = WorldOffset.GetRelativeTransform(controllerTrans);
	}

	if (!bIsLocalGrip)
	{
		int32 Index = GrippedObjects.Add(newActorGrip);
		if(Index != INDEX_NONE)
			NotifyGrip(GrippedObjects[Index]);
	}
	else
	{
		int32 Index = LocallyGrippedObjects.Add(newActorGrip);

		if(GetNetMode() == ENetMode::NM_Client && !IsTornOff() && newActorGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			Server_NotifyLocalGripAddedOrChanged(newActorGrip);

		if (Index != INDEX_NONE)
			NotifyGrip(LocallyGrippedObjects[Index]);
	}

	return true;
}

bool UGripMotionControllerComponent::DropActor(AActor* ActorToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!ActorToDrop)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid actor"));
		return false;
	}

	FBPActorGripInformation * GripToDrop = LocallyGrippedObjects.FindByKey(ActorToDrop);

	if(GripToDrop)
		return DropGrip(*GripToDrop, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);

	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side with a replicated grip"));
		return false;
	}

	GripToDrop = GrippedObjects.FindByKey(ActorToDrop);
	if (GripToDrop)
		return DropGrip(*GripToDrop, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);

	return false;
}

bool UGripMotionControllerComponent::GripComponent(
	UPrimitiveComponent* ComponentToGrip, 
	const FTransform &WorldOffset, 
	bool bWorldOffsetIsRelative, 
	FName OptionalSnapToSocketName, 
	FName OptionalBoneToGripName,
	EGripCollisionType GripCollisionType,
	EGripLateUpdateSettings GripLateUpdateSetting,
	EGripMovementReplicationSettings GripMovementReplicationSetting,
	float GripStiffness, 
	float GripDamping,
	bool bIsSlotGrip
	)
{

	bool bIsLocalGrip = (GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive || GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep);

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

	if (GetIsObjectHeld(ComponentToGrip))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already gripped component"));
		return false;
	}

	// Has to be movable to work
	if (ComponentToGrip->Mobility != EComponentMobility::Movable)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController tried to grip a component set to static mobility and bAllowSetMobility is false"));
		return false; // It is not movable, can't grip it
	}

	FBPAdvGripSettings AdvancedGripSettings;
	UObject * ObjectToCheck = NULL;
	bool bIgnoreHandRotation = false;

	if (ComponentToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ComponentToGrip))
			return false; // Interface is saying not to grip it right now

		AdvancedGripSettings = IVRGripInterface::Execute_AdvancedGripSettings(ComponentToGrip);
		ObjectToCheck = ComponentToGrip;

		// Removed in 4.20
		/*if (IVRGripInterface::Execute_IsInteractible(ComponentToGrip))
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("Gripping an object with bIsInteractible set, this functionality will soon be deprecated. Please use custom grips or interactible components instead."));
			FBPInteractionSettings IntSettings = IVRGripInterface::Execute_GetInteractionSettings(ComponentToGrip);
			bIgnoreHandRotation = IntSettings.bIgnoreHandRotation;
		}*/
	}

	//ComponentToGrip->IgnoreActorWhenMoving(this->GetOwner(), true);
	// So that events caused by sweep and the like will trigger correctly

	ComponentToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripID = GetNextGripID(bIsLocalGrip);
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.GrippedObject = ComponentToGrip;
	
	if(ComponentToGrip->GetOwner())
		newActorGrip.bOriginalReplicatesMovement = ComponentToGrip->GetOwner()->bReplicateMovement;

	newActorGrip.bOriginalGravity = ComponentToGrip->IsGravityEnabled();
	newActorGrip.Stiffness = GripStiffness;
	newActorGrip.Damping = GripDamping;
	newActorGrip.AdvancedGripSettings = AdvancedGripSettings;
	newActorGrip.GripTargetType = EGripTargetType::ComponentGrip;
	newActorGrip.ValueCache.bWasInitiallyRepped = true; // Set this true on authority side so we can skip a function call on tick
	newActorGrip.bIsSlotGrip = bIsSlotGrip;
	newActorGrip.GrippedBoneName = OptionalBoneToGripName;

	// Ignore late update setting if it doesn't make sense with the grip
	switch (newActorGrip.GripCollisionType)
	{
	case EGripCollisionType::ManipulationGrip:
	case EGripCollisionType::ManipulationGripWithWristTwist:
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
	{
		FTransform FinalOffset = WorldOffset;
		if (bIgnoreHandRotation)
		{
			// Reconstitute the controller transform relative to the object, then remove the rotation and set it back to relative to controller
			// This could likely be done easier by just removing rotation that the object doesn't possess but for now this will do.
			FTransform compTrans = ComponentToGrip->GetComponentTransform();

			FinalOffset = FinalOffset.Inverse() * compTrans; // Reconstitute transform
			FinalOffset.SetRotation(FQuat::Identity); // Remove rotation

			FinalOffset = compTrans.GetRelativeTransform(FinalOffset); // Set back to relative
		}
		newActorGrip.RelativeTransform = FinalOffset;
	}
	else
	{
		FTransform controllerTrans = this->GetComponentTransform();
		if (bIgnoreHandRotation)
		{
			controllerTrans.SetRotation(FQuat::Identity);
		}
		newActorGrip.RelativeTransform = WorldOffset.GetRelativeTransform(controllerTrans);
	}

	if (!bIsLocalGrip)
	{
		int32 Index = GrippedObjects.Add(newActorGrip);
		if (Index != INDEX_NONE)
			NotifyGrip(GrippedObjects[Index]);
	}
	else
	{
		int32 Index = LocallyGrippedObjects.Add(newActorGrip);

		if (GetNetMode() == ENetMode::NM_Client && !IsTornOff() && newActorGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			Server_NotifyLocalGripAddedOrChanged(newActorGrip);

		if (Index != INDEX_NONE)
			NotifyGrip(LocallyGrippedObjects[Index]);
	}

	return true;
}

bool UGripMotionControllerComponent::DropComponent(UPrimitiveComponent * ComponentToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	if (!ComponentToDrop)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid component"));
		return false;
	}

	for (int i = LocallyGrippedObjects.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedObjects[i] == ComponentToDrop)
		{
			return DropGrip(LocallyGrippedObjects[i], bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}

	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side for a replicated grip"));
		return false;
	}

	for (int i = GrippedObjects.Num() - 1; i >= 0; --i)
	{
		if (GrippedObjects[i] == ComponentToDrop)
		{
			return DropGrip(GrippedObjects[i], bSimulate,OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}
	return false;
}

bool UGripMotionControllerComponent::DropGrip(const FBPActorGripInformation &Grip, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	int FoundIndex = 0;
	bool bWasLocalGrip = false;
	if (!LocallyGrippedObjects.Find(Grip, FoundIndex)) // This auto checks if Actor and Component are valid in the == operator
	{
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side for a replicated grip"));
			return false;
		}

		if (!GrippedObjects.Find(Grip, FoundIndex)) // This auto checks if Actor and Component are valid in the == operator
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
		PrimComp = LocallyGrippedObjects[FoundIndex].GetGrippedComponent();
		pActor = LocallyGrippedObjects[FoundIndex].GetGrippedActor();
	}
	else
	{
		PrimComp = GrippedObjects[FoundIndex].GetGrippedComponent();
		pActor = GrippedObjects[FoundIndex].GetGrippedActor();
	}

	if (!PrimComp && pActor)
		PrimComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if(!PrimComp)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid drop or CleanUpBadGrip wascalled"));
		//return false;
	}
	else
	{

		// Had to move in front of deletion to properly set velocity
		if (((bWasLocalGrip && !IsLocallyControlled()) ||
			Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceClientSideMovement) &&
			(!OptionalLinearVelocity.IsNearlyZero() || !OptionalAngularVelocity.IsNearlyZero())
		)
		{
			PrimComp->SetPhysicsLinearVelocity(OptionalLinearVelocity);
			PrimComp->SetPhysicsAngularVelocityInDegrees(OptionalAngularVelocity);
		}
	}

	if (bWasLocalGrip)
	{
		if (GetNetMode() == ENetMode::NM_Client)
		{
			if(!IsTornOff())
				Server_NotifyLocalGripRemoved(LocallyGrippedObjects[FoundIndex].GripID, OptionalAngularVelocity, OptionalLinearVelocity);

			// Have to call this ourselves
			Drop_Implementation(LocallyGrippedObjects[FoundIndex], bSimulate);
		}
		else // Server notifyDrop it
		{
			NotifyDrop(LocallyGrippedObjects[FoundIndex], bSimulate);
		}
	}
	else
		NotifyDrop(GrippedObjects[FoundIndex], bSimulate);

	//GrippedObjects.RemoveAt(FoundIndex);		
	return true;
}

bool UGripMotionControllerComponent::DropAndSocketObject(UObject * ObjectToDrop, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent)
{
	if (!SocketingParent)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid socketing parent"));
		return false;
	}

	if (!ObjectToDrop)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid object"));
		return false;
	}

	bool bWasLocalGrip = false;
	FBPActorGripInformation * GripInfo = nullptr;

	GripInfo = LocallyGrippedObjects.FindByKey(ObjectToDrop);
	if(GripInfo) // This auto checks if Actor and Component are valid in the == operator
	{
		bWasLocalGrip = true;
	}
	else
	{
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was called on the client side for a replicated grip"));
			return false;
		}

		GripInfo = GrippedObjects.FindByKey(ObjectToDrop);

		if(GripInfo) // This auto checks if Actor and Component are valid in the == operator
		{
			bWasLocalGrip = false;
		}
		else
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid drop"));
			return false;
		}
	}

	if(GripInfo)
		return DropAndSocketGrip(*GripInfo, SocketingParent, OptionalSocketName, RelativeTransformToParent);
	
	return false;
}

bool UGripMotionControllerComponent::DropAndSocketGrip(const FBPActorGripInformation & GripToDrop, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent)
{
	if (!SocketingParent)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid socketing parent"));
		return false;
	}

	bool bWasLocalGrip = false;
	FBPActorGripInformation * GripInfo = nullptr;

	GripInfo = LocallyGrippedObjects.FindByKey(GripToDrop);
	if (GripInfo) // This auto checks if Actor and Component are valid in the == operator
	{
		bWasLocalGrip = true;
	}
	else
	{
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was called on the client side for a replicated grip"));
			return false;
		}

		GripInfo = GrippedObjects.FindByKey(GripToDrop);

		if (GripInfo) // This auto checks if Actor and Component are valid in the == operator
		{
			bWasLocalGrip = false;
		}
		else
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid drop"));
			return false;
		}
	}

	UPrimitiveComponent * PrimComp = nullptr;

	AActor * pActor = nullptr;
	if (bWasLocalGrip)
	{
		PrimComp = GripInfo->GetGrippedComponent();
		pActor = GripInfo->GetGrippedActor();
	}
	else
	{
		PrimComp = GripInfo->GetGrippedComponent();
		pActor = GripInfo->GetGrippedActor();
	}

	if (!PrimComp && pActor)
		PrimComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if (!PrimComp)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid drop or CleanUpBadGrip wascalled"));
		//return false;
	}


	UObject * GrippedObject = GripInfo->GrippedObject;

	if (bWasLocalGrip)
	{
		if (GetNetMode() == ENetMode::NM_Client)
		{
			if(!IsTornOff())
				Server_NotifyDropAndSocketGrip(GripInfo->GripID, SocketingParent, OptionalSocketName, RelativeTransformToParent);

			// Have to call this ourselves
			DropAndSocket_Implementation(*GripInfo);
			if (GrippedObject)
				Socket_Implementation(GrippedObject, SocketingParent, OptionalSocketName, RelativeTransformToParent);

		}
		else // Server notifyDrop it
		{
			NotifyDropAndSocket(*GripInfo);
			if (GrippedObject)
				Socket_Implementation(GrippedObject, SocketingParent, OptionalSocketName, RelativeTransformToParent);
		}
	}
	else
	{
		NotifyDropAndSocket(*GripInfo);
		if (GrippedObject)
			Socket_Implementation(GrippedObject, SocketingParent, OptionalSocketName, RelativeTransformToParent);
	}

	//GrippedObjects.RemoveAt(FoundIndex);		
	return true;
}

bool UGripMotionControllerComponent::Server_NotifyDropAndSocketGrip_Validate(uint8 GripID, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifyDropAndSocketGrip_Implementation(uint8 GripID, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent)
{
	FBPActorGripInformation FoundGrip;
	EBPVRResultSwitch Result;
	
	GetGripByID(FoundGrip, GripID, Result);

	if (Result == EBPVRResultSwitch::OnFailed)
		return;

	if (!DropAndSocketGrip(FoundGrip, SocketingParent, OptionalSocketName, RelativeTransformToParent))
	{
		DropGrip(FoundGrip, false);
	}
	
	if (FoundGrip.GrippedObject)
		Socket_Implementation(FoundGrip.GrippedObject, SocketingParent, OptionalSocketName, RelativeTransformToParent);
}

void UGripMotionControllerComponent::Socket_Implementation(UObject * ObjectToSocket, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent)
{
	// Check for valid objects
	if (!ObjectToSocket || !SocketingParent)
		return;

	if (UPrimitiveComponent * root = Cast<UPrimitiveComponent>(ObjectToSocket))
	{
		root->AttachToComponent(SocketingParent, FAttachmentTransformRules::KeepWorldTransform, OptionalSocketName);
		root->SetRelativeTransform(RelativeTransformToParent);
	}
	else if (AActor * pActor = Cast<AActor>(ObjectToSocket))
	{
		pActor->AttachToComponent(SocketingParent, FAttachmentTransformRules::KeepWorldTransform, OptionalSocketName);
		pActor->SetActorRelativeTransform(RelativeTransformToParent);

		//if (!bRetainOwnership)
			//pActor->SetOwner(nullptr);
	}
}

void UGripMotionControllerComponent::NotifyDropAndSocket_Implementation(const FBPActorGripInformation &NewDrop)
{
	// Don't do this if we are the owning player on a local grip, there is no filter for multicast to not send to owner
	if ((NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive ||
		NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) &&
		IsLocallyControlled() &&
		GetNetMode() == ENetMode::NM_Client)
	{
		return;
	}

	DropAndSocket_Implementation(NewDrop);
}

void UGripMotionControllerComponent::DropAndSocket_Implementation(const FBPActorGripInformation &NewDrop)
{
	UGripMotionControllerComponent * HoldingController = nullptr;
	bool bIsHeld = false;

	DestroyPhysicsHandle(NewDrop);

	bool bHadGripAuthority = HasGripAuthority(NewDrop);

	UPrimitiveComponent *root = NULL;
	AActor * pActor = NULL;

	switch (NewDrop.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
		//case EGripTargetType::InteractibleActorGrip:
	{
		pActor = NewDrop.GetGrippedActor();

		if (pActor)
		{
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

			pActor->RemoveTickPrerequisiteComponent(this);
			//this->IgnoreActorWhenMoving(pActor, false);

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorRemove(pActor);

				// Clearing owner out here
				// Now I am setting the owner to the owning pawn if we are one
				// This makes sure that some special replication needs are taken care of
				// Only doing this for actor grips
				// #TODO: Add the removal back in?
				//pActor->SetOwner(nullptr);
			}

			if (root)
			{
				root->IgnoreActorWhenMoving(this->GetOwner(), false);
				root->UpdateComponentToWorld(); // This fixes the late update offset
				root->SetSimulatePhysics(false);

				if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
					(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
					root->SetEnableGravity(NewDrop.bOriginalGravity);
			}

			if (IsServer()) //&& !bSkipFullDrop)
			{
				pActor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
			}

			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(pActor, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(pActor, this, NewDrop, true);
				IVRGripInterface::Execute_SetHeld(pActor, nullptr, false);
			}
		}
	}break;

	case EGripTargetType::ComponentGrip:
		//case EGripTargetType::InteractibleComponentGrip:
	{
		root = NewDrop.GetGrippedComponent();
		if (root)
		{
			pActor = root->GetOwner();

			root->RemoveTickPrerequisiteComponent(this);
			root->IgnoreActorWhenMoving(this->GetOwner(), false);
			root->UpdateComponentToWorld();
			root->SetSimulatePhysics(false);

			if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
				(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
				root->SetEnableGravity(NewDrop.bOriginalGravity);

			if (pActor)
			{
				if (IsServer() && root == pActor->GetRootComponent()) //&& !bSkipFullDrop)
				{
					pActor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
				}

				if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					IVRGripInterface::Execute_OnChildGripRelease(pActor, this, NewDrop, true);
				}
			}

			// Call on child grip release on attached parent component
			if (root->GetAttachParent() && root->GetAttachParent()->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnChildGripRelease(root->GetAttachParent(), this, NewDrop, true);
			}

			if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(root, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(root, this, NewDrop, true);
				IVRGripInterface::Execute_SetHeld(root, nullptr, false);
			}
		}
	}break;
	}


	int fIndex = 0;
	if (LocallyGrippedObjects.Find(NewDrop, fIndex))
	{
		if (HasGripAuthority(NewDrop) || GetNetMode() < ENetMode::NM_Client)
		{
			LocallyGrippedObjects.RemoveAt(fIndex);
		}
		else
			LocallyGrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
	}
	else
	{
		fIndex = 0;
		if (GrippedObjects.Find(NewDrop, fIndex))
		{
			if (HasGripAuthority(NewDrop) || GetNetMode() < ENetMode::NM_Client)
			{
				GrippedObjects.RemoveAt(fIndex);
			}
			else
				GrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
		}
	}

	// Broadcast a new drop
	OnDroppedObject.Broadcast(NewDrop);
}


// No longer an RPC, now is called from RepNotify so that joining clients also correctly set up grips
bool UGripMotionControllerComponent::NotifyGrip(FBPActorGripInformation &NewGrip, bool bIsReInit)
{
	UPrimitiveComponent *root = NULL;
	AActor *pActor = NULL;

	switch (NewGrip.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	//case EGripTargetType::InteractibleActorGrip:
	{
		pActor = NewGrip.GetGrippedActor();

		if (pActor)
		{
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				OwningPawn->MoveIgnoreActorAdd(pActor);

				// Now I am setting the owner to the owning pawn if we are one
				// This makes sure that some special replication needs are taken care of
				// Only doing this for actor grips
				if(NewGrip.AdvancedGripSettings.bSetOwnerOnGrip)
					pActor->SetOwner(OwningPawn);
			}

			if (!bIsReInit && pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_SetHeld(pActor, this, true);
				IVRGripInterface::Execute_OnGrip(pActor, this, NewGrip);
			}

			if (root)
			{
				// Have to turn off gravity locally
				if ((NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
					(NewGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
					root->SetEnableGravity(false);

				root->IgnoreActorWhenMoving(this->GetOwner(), true);
			}


		}
		else
			return false;
	}break;

	case EGripTargetType::ComponentGrip:
	//case EGripTargetType::InteractibleComponentGrip:
	{
		root = NewGrip.GetGrippedComponent();

		if (root)
		{
			pActor = root->GetOwner();

			if (!bIsReInit && root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_SetHeld(root, this, true);
				IVRGripInterface::Execute_OnGrip(root, this, NewGrip);
			}

			if (pActor)
			{
				/*if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
				{
					OwningPawn->MoveIgnoreActorAdd(root->GetOwner());
				}*/

				if (!bIsReInit && pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					IVRGripInterface::Execute_OnChildGrip(pActor, this, NewGrip);
				}

			}

			// Call OnChildGrip for attached grip parent
			if (!bIsReInit && root->GetAttachParent() && root->GetAttachParent()->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnChildGrip(root->GetAttachParent(), this, NewGrip);
			}

			if ((NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
				(NewGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
				root->SetEnableGravity(false);

			root->IgnoreActorWhenMoving(this->GetOwner(), true);
		}
		else
			return false;
	}break;
	}

	switch (NewGrip.GripMovementReplicationSetting)
	{
	case EGripMovementReplicationSettings::ForceClientSideMovement:
	case EGripMovementReplicationSettings::ClientSide_Authoritive:
	case EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep:
	{
		if (IsServer() && pActor && ((NewGrip.GripTargetType == EGripTargetType::ActorGrip) || (root && root == pActor->GetRootComponent())))
		{
			pActor->SetReplicateMovement(false);
		}

	}break;

	case EGripMovementReplicationSettings::ForceServerSideMovement:
	{
		if (IsServer() && pActor && ((NewGrip.GripTargetType == EGripTargetType::ActorGrip) || (root && root == pActor->GetRootComponent())))
		{
			pActor->SetReplicateMovement(true);
		}
	}break;

	case EGripMovementReplicationSettings::KeepOriginalMovement:
	default:
	{}break;
	}

	bool bHasMovementAuthority = HasGripMovementAuthority(NewGrip);

	switch (NewGrip.GripCollisionType)
	{
	case EGripCollisionType::InteractiveCollisionWithPhysics:
	case EGripCollisionType::InteractiveHybridCollisionWithPhysics:
	case EGripCollisionType::ManipulationGrip:
	case EGripCollisionType::ManipulationGripWithWristTwist:
	{
		if (bHasMovementAuthority)
		{
			SetUpPhysicsHandle(NewGrip);
		}
	} break;

	// Skip collision intersects with these types, they dont need it
	case EGripCollisionType::CustomGrip:
	{		
		if (root)
			root->SetSimulatePhysics(false);

	} break;

	case EGripCollisionType::AttachmentGrip:
	{
		if (root)
			root->SetSimulatePhysics(false);

		// Move it to the correct location automatically
		if (bHasMovementAuthority)
			TeleportMoveGrip(NewGrip);

		if(HasGripAuthority(NewGrip) || IsServer())
			root->AttachToComponent(this, FAttachmentTransformRules::KeepWorldTransform);

	}break;

	case EGripCollisionType::PhysicsOnly:
	case EGripCollisionType::SweepWithPhysics:
	case EGripCollisionType::InteractiveHybridCollisionWithSweep:
	case EGripCollisionType::InteractiveCollisionWithSweep:
	default: 
	{
		if (root)
			root->SetSimulatePhysics(false);

		// Move it to the correct location automatically
		if (bHasMovementAuthority)
			TeleportMoveGrip(NewGrip);
	} break;

	}

	if (!bIsReInit)
	{
		// Broadcast a new grip
		OnGrippedObject.Broadcast(NewGrip);
	}

	return true;
}

void UGripMotionControllerComponent::NotifyDrop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate)
{
	// Don't do this if we are the owning player on a local grip, there is no filter for multicast to not send to owner
	if ((NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive || 
		NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) && 
		IsLocallyControlled() && 
		GetNetMode() == ENetMode::NM_Client)
	{
		return;
	}

	Drop_Implementation(NewDrop, bSimulate);
}

void UGripMotionControllerComponent::Drop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate)
{

	bool bSkipFullDrop = false;
	UGripMotionControllerComponent * HoldingController = nullptr;
	bool bIsHeld = false;

	// Check if a different controller is holding it
	if(NewDrop.GrippedObject && NewDrop.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		IVRGripInterface::Execute_IsHeld(NewDrop.GrippedObject, HoldingController, bIsHeld);

	if (bIsHeld && HoldingController != this)
	{
		// Skip the full drop if held
		bSkipFullDrop = true;
	}	
	else // Now check for this same hand with duplicate grips on this object
	{
		for (int i = 0; i < LocallyGrippedObjects.Num(); ++i)
		{
			if (LocallyGrippedObjects[i].GrippedObject == NewDrop.GrippedObject && LocallyGrippedObjects[i].GripID != NewDrop.GripID)
			{
				bSkipFullDrop = true;
			}
		}
		for (int i = 0; i < GrippedObjects.Num(); ++i)
		{
			if (GrippedObjects[i].GrippedObject == NewDrop.GrippedObject && GrippedObjects[i].GripID != NewDrop.GripID)
			{
				bSkipFullDrop = true;
			}
		}
	}

	DestroyPhysicsHandle(NewDrop);

	bool bHadGripAuthority = HasGripAuthority(NewDrop);

	UPrimitiveComponent *root = NULL;
	AActor * pActor = NULL;

	switch (NewDrop.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
		//case EGripTargetType::InteractibleActorGrip:
	{
		pActor = NewDrop.GetGrippedActor();

		if (pActor)
		{
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

			if (!bSkipFullDrop)
			{
				pActor->RemoveTickPrerequisiteComponent(this);
				//this->IgnoreActorWhenMoving(pActor, false);

				if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
				{
					OwningPawn->MoveIgnoreActorRemove(pActor);

					// Clearing owner out here
					// Now I am setting the owner to the owning pawn if we are one
					// This makes sure that some special replication needs are taken care of
					// Only doing this for actor grips
					// #TODO: Add the removal back in?
					//pActor->SetOwner(nullptr);
				}

				if (root)
				{

					if (NewDrop.GripCollisionType == EGripCollisionType::AttachmentGrip && (HasGripAuthority(NewDrop) || IsServer()))
						root->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

					root->IgnoreActorWhenMoving(this->GetOwner(), false);

					if (IsServer() || bHadGripAuthority)
					{
						root->SetSimulatePhysics(bSimulate);
						if (bSimulate)
							root->WakeAllRigidBodies();
					}

					root->UpdateComponentToWorld(); // This fixes the late update offset

					/*if (NewDrop.GrippedBoneName == NAME_None)
					{
						root->SetSimulatePhysics(bSimulate);
						root->UpdateComponentToWorld(); // This fixes the late update offset
						if (bSimulate)
							root->WakeAllRigidBodies();
					}
					else
					{
						USkeletalMeshComponent * skele = Cast<USkeletalMeshComponent>(root);
						if (skele)
						{
							skele->SetAllBodiesBelowSimulatePhysics(NewDrop.GrippedBoneName, bSimulate);
							root->UpdateComponentToWorld(); // This fixes the late update offset
						}
						else
						{
							root->SetSimulatePhysics(bSimulate);
							root->UpdateComponentToWorld(); // This fixes the late update offset
							if (bSimulate)
								root->WakeAllRigidBodies();
						}
					}*/

					if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
						(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
						root->SetEnableGravity(NewDrop.bOriginalGravity);
				}
			}

			if (IsServer() && !bSkipFullDrop)
			{
				pActor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
			}

			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(pActor, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(pActor, this, NewDrop, false);

				if(!bSkipFullDrop)
					IVRGripInterface::Execute_SetHeld(pActor, nullptr, false);
			}
		}
	}break;

	case EGripTargetType::ComponentGrip:
		//case EGripTargetType::InteractibleComponentGrip:
	{
		root = NewDrop.GetGrippedComponent();
		if (root)
		{
			pActor = root->GetOwner();

			if (!bSkipFullDrop)
			{
				root->RemoveTickPrerequisiteComponent(this);

				/*if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
				{
					OwningPawn->MoveIgnoreActorRemove(pActor);
				}*/

				if (NewDrop.GripCollisionType == EGripCollisionType::AttachmentGrip && (HasGripAuthority(NewDrop) || IsServer()))
					root->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

				root->IgnoreActorWhenMoving(this->GetOwner(), false);

				if (IsServer() || bHadGripAuthority)
				{
					root->SetSimulatePhysics(bSimulate);
					if (bSimulate)
						root->WakeAllRigidBodies();
				}

				root->UpdateComponentToWorld(); // This fixes the late update offset

				/*if (NewDrop.GrippedBoneName == NAME_None)
				{
					root->SetSimulatePhysics(bSimulate);
					root->UpdateComponentToWorld(); // This fixes the late update offset
					if (bSimulate)
						root->WakeAllRigidBodies();
				}
				else
				{
					USkeletalMeshComponent * skele = Cast<USkeletalMeshComponent>(root);
					if (skele)
					{
						skele->SetAllBodiesBelowSimulatePhysics(NewDrop.GrippedBoneName, bSimulate);
						root->UpdateComponentToWorld(); // This fixes the late update offset
					}
					else
					{
						root->SetSimulatePhysics(bSimulate);
						root->UpdateComponentToWorld(); // This fixes the late update offset
						if (bSimulate)
							root->WakeAllRigidBodies();
					}
				}*/

				if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
					(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
					root->SetEnableGravity(NewDrop.bOriginalGravity);
			}

			if (pActor)
			{
				if (IsServer() && root == pActor->GetRootComponent() && !bSkipFullDrop)
				{
					pActor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
				}

				if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					IVRGripInterface::Execute_OnChildGripRelease(pActor, this, NewDrop, false);
				}

			}

			// Call on child grip release on attached parent component
			if (root->GetAttachParent() && root->GetAttachParent()->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnChildGripRelease(root->GetAttachParent(), this, NewDrop, false);
			}

			if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
					IVRGripInterface::Execute_OnSecondaryGripRelease(root, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

				IVRGripInterface::Execute_OnGripRelease(root, this, NewDrop, false);

				if(!bSkipFullDrop)
					IVRGripInterface::Execute_SetHeld(root, nullptr, false);
			}
		}
	}break;
	}


	int fIndex = 0;
	if (LocallyGrippedObjects.Find(NewDrop, fIndex))
	{
		if (HasGripAuthority(NewDrop) || GetNetMode() < ENetMode::NM_Client)
		{
			LocallyGrippedObjects.RemoveAt(fIndex);
		}
		else
			LocallyGrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
	}
	else
	{
		fIndex = 0;
		if (GrippedObjects.Find(NewDrop, fIndex))
		{
			if (HasGripAuthority(NewDrop) || GetNetMode() < ENetMode::NM_Client)
			{
				GrippedObjects.RemoveAt(fIndex);
			}
			else
				GrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
		}
	}

	// Broadcast a new drop
	OnDroppedObject.Broadcast(NewDrop);
}

bool UGripMotionControllerComponent::BP_HasGripAuthority(const FBPActorGripInformation &Grip)
{
	return HasGripAuthority(Grip);
}

bool UGripMotionControllerComponent::BP_HasGripMovementAuthority(const FBPActorGripInformation &Grip)
{
	return HasGripMovementAuthority(Grip);
}

bool UGripMotionControllerComponent::AddSecondaryAttachmentPoint(UObject * GrippedObjectToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform & OriginalTransform, bool bTransformIsAlreadyRelative, float LerpToTime,/* float SecondarySmoothingScaler,*/ bool bIsSlotGrip)
{
	if (!GrippedObjectToAddAttachment || !SecondaryPointComponent || (!GrippedObjects.Num() && !LocallyGrippedObjects.Num()))
		return false;


	if (GrippedObjectToAddAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		ESecondaryGripType SecondaryType = IVRGripInterface::Execute_SecondaryGripType(GrippedObjectToAddAttachment);
	
		switch (SecondaryType)
		{
		case ESecondaryGripType::SG_None:return false; break;

		//case ESecondaryGripType::SG_FreeWithScaling_Retain:
		//case ESecondaryGripType::SG_SlotOnly_Retain:
		//case ESecondaryGripType::SG_SlotOnlyWithScaling_Retain:
		//case ESecondaryGripType::SG_Free_Retain:
		//{
		//	LerpToTime = 0.0f;
		//}break;

		default:break;
		}
	}

	FBPActorGripInformation * GripToUse = nullptr;

	for (int i = LocallyGrippedObjects.Num() - 1; i >= 0; --i)
	{
		if (LocallyGrippedObjects[i].GrippedObject == GrippedObjectToAddAttachment)
		{
			GripToUse = &LocallyGrippedObjects[i];
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

		for (int i = GrippedObjects.Num() - 1; i >= 0; --i)
		{
			if (GrippedObjects[i].GrippedObject == GrippedObjectToAddAttachment)
			{
				GripToUse = &GrippedObjects[i];
				break;
			}
		}
	}

	if (GripToUse)
	{
		UPrimitiveComponent * root = nullptr;

		switch (GripToUse->GripTargetType)
		{
		case EGripTargetType::ActorGrip:
		{
			AActor * pActor = GripToUse->GetGrippedActor();

			if (pActor)
			{
				root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());
			}
		}
			break;
		case EGripTargetType::ComponentGrip:
		{
			root = GripToUse->GetGrippedComponent();
		}
			break;
		}

		if (!root)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was unable to get root component or gripped component."));
			return false;
		}

		if (bTransformIsAlreadyRelative)
			GripToUse->SecondaryGripInfo.SecondaryRelativeTransform = OriginalTransform;
		else
			GripToUse->SecondaryGripInfo.SecondaryRelativeTransform = OriginalTransform.GetRelativeTransform(root->GetComponentTransform());

		GripToUse->SecondaryGripInfo.SecondaryAttachment = SecondaryPointComponent;
		GripToUse->SecondaryGripInfo.bHasSecondaryAttachment = true;
		GripToUse->SecondaryGripInfo.SecondaryGripDistance = 0.0f;

		const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
		GripToUse->AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.CutoffSlope = VRSettings.OneEuroCutoffSlope;
		GripToUse->AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.DeltaCutoff = VRSettings.OneEuroDeltaCutoff;
		GripToUse->AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.MinCutoff = VRSettings.OneEuroMinCutoff;
		
		GripToUse->AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.ResetSmoothingFilter();
	//	GripToUse->SecondaryGripInfo.SecondarySmoothingScaler = FMath::Clamp(SecondarySmoothingScaler, 0.01f, 1.0f);
		GripToUse->SecondaryGripInfo.bIsSlotGrip = bIsSlotGrip;

		if (GripToUse->SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
			LerpToTime = 0.0f;

		if (LerpToTime > 0.0f)
		{
			GripToUse->SecondaryGripInfo.LerpToRate = LerpToTime;
			GripToUse->SecondaryGripInfo.GripLerpState = EGripLerpState::StartLerp;
			GripToUse->SecondaryGripInfo.curLerp = LerpToTime;
		}

		if (GrippedObjectToAddAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			IVRGripInterface::Execute_OnSecondaryGrip(GrippedObjectToAddAttachment, SecondaryPointComponent, *GripToUse);
		}

		if (GripToUse->GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive && GetNetMode() == ENetMode::NM_Client && !IsTornOff())
		{
			Server_NotifySecondaryAttachmentChanged(GripToUse->GripID, GripToUse->SecondaryGripInfo);
		}

		GripToUse = nullptr;

		return true;
	}

	return false;
}

bool UGripMotionControllerComponent::RemoveSecondaryAttachmentPoint(UObject * GrippedObjectToRemoveAttachment, float LerpToTime)
{
	if (!GrippedObjectToRemoveAttachment || (!GrippedObjects.Num() && !LocallyGrippedObjects.Num()))
		return false;

	FBPActorGripInformation * GripToUse = nullptr;

	// Duplicating the logic for each array for now
	GripToUse = LocallyGrippedObjects.FindByKey(GrippedObjectToRemoveAttachment);

	// Check replicated grips if it wasn't found in local
	if (!GripToUse)
	{
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController remove secondary attachment function was called on the client side for a replicating grip"));
			return false;
		}

		GripToUse = GrippedObjects.FindByKey(GrippedObjectToRemoveAttachment);
	}

	// Handle the grip if it was found
	if (GripToUse && GripToUse->GrippedObject)
	{
		if (GripToUse->SecondaryGripInfo.GripLerpState == EGripLerpState::StartLerp)
			LerpToTime = 0.0f;

		//if (LerpToTime > 0.0f)
		//{
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

		ESecondaryGripType SecondaryType = ESecondaryGripType::SG_None;
		if (GripToUse->GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			SecondaryType = IVRGripInterface::Execute_SecondaryGripType(GripToUse->GrippedObject);
			//else if (SecondaryType == ESecondaryGripType::SG_FreeWithScaling || SecondaryType == ESecondaryGripType::SG_SlotOnlyWithScaling)
			//LerpToTime = 0.0f;
		}

		if (primComp)
		{
			switch (SecondaryType)
			{
			// All of these retain the position on release
			case ESecondaryGripType::SG_FreeWithScaling_Retain:
			case ESecondaryGripType::SG_SlotOnlyWithScaling_Retain:
			case ESecondaryGripType::SG_Free_Retain:
			case ESecondaryGripType::SG_SlotOnly_Retain:
			case ESecondaryGripType::SG_ScalingOnly:
			{
				GripToUse->RelativeTransform = primComp->GetComponentTransform().GetRelativeTransform(this->GetComponentTransform());
				GripToUse->SecondaryGripInfo.LerpToRate = 0.0f;
				GripToUse->SecondaryGripInfo.GripLerpState = EGripLerpState::NotLerping;
			}break;
			default:
			{
				if (LerpToTime > 0.0f)
				{
					// #TODO: This had a hitch in it just prior to lerping back, fix it eventually and allow lerping from scaling secondaries
					//GripToUse->RelativeTransform.SetScale3D(GripToUse->RelativeTransform.GetScale3D() * FVector(GripToUse->SecondaryScaler));
					GripToUse->SecondaryGripInfo.LerpToRate = LerpToTime;
					GripToUse->SecondaryGripInfo.GripLerpState = EGripLerpState::EndLerp;
					GripToUse->SecondaryGripInfo.curLerp = LerpToTime;
				}
			}break;
			}

		}
		else
		{
			GripToUse->SecondaryGripInfo.LerpToRate = 0.0f;
			GripToUse->SecondaryGripInfo.GripLerpState = EGripLerpState::NotLerping;
		}

		if (GrippedObjectToRemoveAttachment->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			IVRGripInterface::Execute_OnSecondaryGripRelease(GrippedObjectToRemoveAttachment, GripToUse->SecondaryGripInfo.SecondaryAttachment, *GripToUse);
		}

		GripToUse->SecondaryGripInfo.SecondaryAttachment = nullptr;
		GripToUse->SecondaryGripInfo.bHasSecondaryAttachment = false;

		if (GripToUse->GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive && GetNetMode() == ENetMode::NM_Client)
		{
			switch (SecondaryType)
			{
				// All of these retain the position on release
			case ESecondaryGripType::SG_FreeWithScaling_Retain:
			case ESecondaryGripType::SG_SlotOnlyWithScaling_Retain:
			case ESecondaryGripType::SG_Free_Retain:
			case ESecondaryGripType::SG_SlotOnly_Retain:
			case ESecondaryGripType::SG_ScalingOnly:
			{
				if(!IsTornOff())
					Server_NotifySecondaryAttachmentChanged_Retain(GripToUse->GripID, GripToUse->SecondaryGripInfo, GripToUse->RelativeTransform);
			}break;
			default:
			{
				if(!IsTornOff())
					Server_NotifySecondaryAttachmentChanged(GripToUse->GripID, GripToUse->SecondaryGripInfo);
			}break;
			}

		}

		GripToUse = nullptr;

		return true;
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrippedActor(AActor * GrippedActorToMove, bool bTeleportPhysicsGrips)
{
	if (!GrippedActorToMove || (!GrippedObjects.Num() && !LocallyGrippedObjects.Num()))
		return false;

	FBPActorGripInformation * GripInfo = LocallyGrippedObjects.FindByKey(GrippedActorToMove);
	if (!GripInfo)
		GrippedObjects.FindByKey(GrippedActorToMove);

	if (GripInfo)
	{
		return TeleportMoveGrip(*GripInfo, bTeleportPhysicsGrips);
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrippedComponent(UPrimitiveComponent * ComponentToMove, bool bTeleportPhysicsGrips)
{
	if (!ComponentToMove || (!GrippedObjects.Num() && !LocallyGrippedObjects.Num()))
		return false;

	FBPActorGripInformation * GripInfo = LocallyGrippedObjects.FindByKey(ComponentToMove);
	if (!GripInfo)
		GrippedObjects.FindByKey(ComponentToMove);

	if (GripInfo)
	{
		return TeleportMoveGrip(*GripInfo, bTeleportPhysicsGrips);
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrip(FBPActorGripInformation &Grip, bool bTeleportPhysicsGrips, bool bIsForPostTeleport)
{
	FTransform EmptyTransform = FTransform::Identity;
	return TeleportMoveGrip_Impl(Grip, bTeleportPhysicsGrips, bIsForPostTeleport, EmptyTransform);
}

bool UGripMotionControllerComponent::TeleportMoveGrip_Impl(FBPActorGripInformation &Grip, bool bTeleportPhysicsGrips, bool bIsForPostTeleport, FTransform & OptionalTransform)
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

	if (bIsForPostTeleport)
	{
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
			if (IsServer() ||
				Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive ||
				Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep)
			{
				DropObjectByInterface(Grip.GrippedObject);
			}
			
			return false; // Didn't teleport
		}
		else if (TeleportBehavior == EGripInterfaceTeleportBehavior::DontTeleport)
		{
			return false; // Didn't teleport
		}
	}
	else
	{
		switch (TeleportBehavior)
		{
		case EGripInterfaceTeleportBehavior::DontTeleport:
		case EGripInterfaceTeleportBehavior::DropOnTeleport:
		{
			return false;
		}break;
		default:break;
		}
	}

	FTransform WorldTransform;
	FTransform ParentTransform = this->GetComponentTransform();

	FBPActorGripInformation copyGrip = Grip;
	
	//bool bRescalePhysicsGrips = false;

	//FTransform EmptyTransform = FTransform::Identity;
	if (!OptionalTransform.Equals(FTransform::Identity))
		WorldTransform = OptionalTransform;
	else
		GetGripWorldTransform(0.0f, WorldTransform, ParentTransform, copyGrip, actor, PrimComp, bRootHasInterface, bActorHasInterface/*, bRescalePhysicsGrips*/);

	//WorldTransform = Grip.RelativeTransform * ParentTransform;

	// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
	
	FBPActorPhysicsHandleInformation * Handle = GetPhysicsGrip(Grip);

	if (!Handle)
	{
		PrimComp->SetWorldTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);
	}
	else if (Handle && Handle->KinActorData && bTeleportPhysicsGrips)
	{
		// Don't try to autodrop on next tick, let the physx constraint update its local frame first
		if (HasGripAuthority(Grip))
			Grip.bSkipNextConstraintLengthCheck = true;

		PrimComp->SetWorldTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);

#if WITH_PHYSX
		{
			PxScene* PScene = GetPhysXSceneFromIndex(Handle->SceneIndex);
			if (PScene)
			{
				SCOPED_SCENE_WRITE_LOCK(PScene);
				Handle->KinActorData->setKinematicTarget(U2PTransform(Handle->RootBoneRotation * WorldTransform) * Handle->COMPosition);
				Handle->KinActorData->setGlobalPose(U2PTransform(Handle->RootBoneRotation * WorldTransform) * Handle->COMPosition);
			}
		}
#endif

	}

	return true;
}

void UGripMotionControllerComponent::PostTeleportMoveGrippedObjects()
{
	if (!GrippedObjects.Num() && !LocallyGrippedObjects.Num())
		return;

	this->bIsPostTeleport = true;
	/*for (int i = 0; i < LocallyGrippedObjects.Num(); i++)
	{
		TeleportMoveGrip(LocallyGrippedObjects[i], true);
	}

	for (int i = 0; i < GrippedObjects.Num(); i++)
	{
		TeleportMoveGrip(GrippedObjects[i], true);
	}*/
}


void UGripMotionControllerComponent::Deactivate()
{
	Super::Deactivate();

	if (bIsActive == false && GripViewExtension.IsValid())
	{
		{
			// This component could be getting accessed from the render thread so it needs to wait
			// before clearing MotionControllerComponent 
			FScopeLock ScopeLock(&CritSect);
			GripViewExtension->MotionControllerComponent = NULL;
		}

		GripViewExtension.Reset();
	}
}


void UGripMotionControllerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Skip motion controller tick, we override a lot of things that it does and we don't want it to perform the same functions
	Super::Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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
		if (bOffsetByControllerProfile && !NewControllerProfileEvent_Handle.IsValid())
		{
			GetCurrentProfileTransform(true);
		}

		FVector Position;
		FRotator Orientation;

		if (!bUseWithoutTracking)
		{
			if (!GripViewExtension.IsValid() && GEngine)
			{
				GripViewExtension = FSceneViewExtensions::NewExtension<FGripViewExtension>(this);
			}
			// This is the owning player, now you can get the controller's location and rotation from the correct source

			float WorldToMeters = GetWorld() ? GetWorld()->GetWorldSettings()->WorldToMeters : 100.0f;
			const bool bNewTrackedState = GripPollControllerState(Position, Orientation, WorldToMeters);

			if (bNewTrackedState)
			{
				SetRelativeTransform(FTransform(Orientation, Position, this->RelativeScale3D));
				//SetRelativeLocationAndRotation(Position, Orientation);
			}

			// if controller tracking just kicked in 
			if (!bTracked && bNewTrackedState && bDisplayDeviceModel && DisplayModelSource != UMotionControllerComponent::CustomModelSourceId)
			{
				RefreshDisplayComponent();
			}

			bTracked = bNewTrackedState;
		}

		if (!bTracked && !bUseWithoutTracking)
			return; // Don't update anything including location

		// Don't bother with any of this if not replicating transform
		if (bReplicates && (bTracked || bReplicateWithoutTracking))
		{
			// Don't rep if no changes
			if (!this->RelativeLocation.Equals(ReplicatedControllerTransform.Position) || !this->RelativeRotation.Equals(ReplicatedControllerTransform.Rotation))
			{
				ControllerNetUpdateCount += DeltaTime;
				if (ControllerNetUpdateCount >= (1.0f / ControllerNetUpdateRate))
				{
					ControllerNetUpdateCount = 0.0f;

					// Tracked doesn't matter, already set the relative location above in that case
					ReplicatedControllerTransform.Position = this->RelativeLocation;
					ReplicatedControllerTransform.Rotation = this->RelativeRotation;

					// I would keep the torn off check here, except this can be checked on tick if they
					// Set 100 htz updates, and in the TornOff case, it actually can't hurt any besides some small
					// Perf difference.
					if (GetNetMode() == NM_Client/* && !IsTornOff()*/)
					{		
						AVRBaseCharacter * OwningChar = Cast<AVRBaseCharacter>(GetOwner());
						if (OverrideSendTransform != nullptr && OwningChar != nullptr)
						{
							(OwningChar->* (OverrideSendTransform))(ReplicatedControllerTransform);
						}
						else
							Server_SendControllerTransform(ReplicatedControllerTransform);
					}
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
				SetRelativeLocationAndRotation(ReplicatedControllerTransform.Position, ReplicatedControllerTransform.Rotation);

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
					FMath::Lerp(LastUpdatesRelativePosition, (FVector)ReplicatedControllerTransform.Position, LerpVal), 
					FMath::Lerp(LastUpdatesRelativeRotation, ReplicatedControllerTransform.Rotation, LerpVal)
				);
			}
		}
	}

	// Process the gripped actors
	TickGrip(DeltaTime);

}

void UGripMotionControllerComponent::GetGripWorldTransform(float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface/*, bool & bRescalePhysicsGrips*/)
{

	// Removed in 4.20
	// Check for interaction interface and modify transform by it
	/*if (bRootHasInterface && IVRGripInterface::Execute_IsInteractible(root))
	{
		WorldTransform = HandleInteractionSettings(DeltaTime, ParentTransform, root, IVRGripInterface::Execute_GetInteractionSettings(root), Grip);
	}
	else if (bActorHasInterface && IVRGripInterface::Execute_IsInteractible(actor))
	{
		// Actor grip interface is checked after component
		WorldTransform = HandleInteractionSettings(DeltaTime, ParentTransform, root, IVRGripInterface::Execute_GetInteractionSettings(actor), Grip);
	}
	else*/
	{
		// Just simple transform setting
		WorldTransform = Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;
	}

	// Check the grip lerp state, this it ouside of the secondary attach check below because it can change the result of it
	if ((Grip.SecondaryGripInfo.bHasSecondaryAttachment && Grip.SecondaryGripInfo.SecondaryAttachment) || Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
	{
		switch (Grip.SecondaryGripInfo.GripLerpState)
		{
		case EGripLerpState::StartLerp:
		case EGripLerpState::EndLerp:
		{
			if (Grip.SecondaryGripInfo.curLerp > 0.01f)
				Grip.SecondaryGripInfo.curLerp -= DeltaTime;
			else
			{
				if (Grip.SecondaryGripInfo.bHasSecondaryAttachment && 
					Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripSettings &&
					Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler < 1.0f)
				{
					Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::ConstantLerp;
				}
				else
					Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::NotLerping;
			}

		}break;
		case EGripLerpState::ConstantLerp:
		case EGripLerpState::NotLerping:
		default:break;
		}
	}

	// Handle the interp and multi grip situations, re-checking the grip situation here as it may have changed in the switch above.
	if ((Grip.SecondaryGripInfo.bHasSecondaryAttachment && Grip.SecondaryGripInfo.SecondaryAttachment) || Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
	{
		FTransform SecondaryTransform = Grip.RelativeTransform * ParentTransform;

		// Checking secondary grip type for the scaling setting
		ESecondaryGripType SecondaryType = ESecondaryGripType::SG_None;

		if (bRootHasInterface)
			SecondaryType = IVRGripInterface::Execute_SecondaryGripType(root);
		else if (bActorHasInterface)
			SecondaryType = IVRGripInterface::Execute_SecondaryGripType(actor);

		// If the grip is a custom one, skip all of this logic we won't be changing anything
		if (SecondaryType != ESecondaryGripType::SG_Custom)
		{
			// Variables needed for multi grip transform
			FVector BasePoint = this->GetComponentLocation();
			const FTransform PivotToWorld = FTransform(FQuat::Identity, BasePoint);
			const FTransform WorldToPivot = FTransform(FQuat::Identity, -BasePoint);

			FVector frontLocOrig;
			FVector frontLoc;

			// Ending lerp out of a multi grip
			if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
			{
				frontLocOrig = (/*WorldTransform*/SecondaryTransform.TransformPosition(Grip.SecondaryGripInfo.SecondaryRelativeTransform.GetLocation())) - BasePoint;
				frontLoc = Grip.SecondaryGripInfo.LastRelativeLocation;

				frontLocOrig = FMath::Lerp(frontLoc, frontLocOrig, FMath::Clamp(Grip.SecondaryGripInfo.curLerp / Grip.SecondaryGripInfo.LerpToRate, 0.0f, 1.0f));
			}
			else // Is in a multi grip, might be lerping into it as well.
			{
				//FVector curLocation; // Current location of the secondary grip

				bool bPulledControllerLoc = false;
				if (bHasAuthority && Grip.SecondaryGripInfo.SecondaryAttachment->GetOwner() == this->GetOwner())
				{
					if (UGripMotionControllerComponent * OtherController = Cast<UGripMotionControllerComponent>(Grip.SecondaryGripInfo.SecondaryAttachment))
					{
						if (!OtherController->bUseWithoutTracking)
						{
							FVector Position;
							FRotator Orientation;
							float WorldToMeters = GetWorld() ? GetWorld()->GetWorldSettings()->WorldToMeters : 100.0f;
							if (OtherController->GripPollControllerState(Position, Orientation, WorldToMeters))
							{
								/*curLocation*/ frontLoc = OtherController->CalcNewComponentToWorld(FTransform(Orientation, Position)).GetLocation() - BasePoint;
								bPulledControllerLoc = true;
							}
						}
					}
				}

				if (!bPulledControllerLoc)
					/*curLocation*/ frontLoc = Grip.SecondaryGripInfo.SecondaryAttachment->GetComponentLocation() - BasePoint;

				frontLocOrig = (/*WorldTransform*/SecondaryTransform.TransformPosition(Grip.SecondaryGripInfo.SecondaryRelativeTransform.GetLocation())) - BasePoint;
				//frontLoc = curLocation;// -BasePoint;

				if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::StartLerp) // Lerp into the new grip to smooth the transition
				{
					if (Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler < 1.0f)
					{
						FVector SmoothedValue = Grip.AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.RunFilterSmoothing(frontLoc, DeltaTime);

						frontLoc = FMath::Lerp(SmoothedValue, frontLoc, Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler);
					}

					frontLocOrig = FMath::Lerp(frontLocOrig, frontLoc, FMath::Clamp(Grip.SecondaryGripInfo.curLerp / Grip.SecondaryGripInfo.LerpToRate, 0.0f, 1.0f));
				}
				else if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::ConstantLerp) // If there is a frame by frame lerp
				{
					FVector SmoothedValue = Grip.AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.RunFilterSmoothing(frontLoc, DeltaTime);

					frontLoc = FMath::Lerp(SmoothedValue, frontLoc, Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler);
					//frontLoc = FMath::Lerp(Grip.SecondaryGripInfo.LastRelativeLocation, frontLoc, Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler);
				}

				Grip.SecondaryGripInfo.LastRelativeLocation = frontLoc;
			}

			FVector Scaler = FVector(1.0f);
			if (Grip.SecondaryGripInfo.GripLerpState != EGripLerpState::EndLerp)
			{

				//float Scaler = 1.0f;
				if (SecondaryType == ESecondaryGripType::SG_FreeWithScaling_Retain || SecondaryType == ESecondaryGripType::SG_SlotOnlyWithScaling_Retain || SecondaryType == ESecondaryGripType::SG_ScalingOnly)
				{
					/*Grip.SecondaryScaler*/ Scaler = FVector(frontLoc.Size() / frontLocOrig.Size());
					//bRescalePhysicsGrips = true; // This is for the physics grips

					if (Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripSettings && Grip.AdvancedGripSettings.SecondaryGripSettings.bLimitGripScaling)
					{
						// Get the total scale after modification
						// #TODO: convert back to singular float version? Can get Min() & Max() to convert the float to a range...think about it
						FVector WorldScale = /*WorldTransform*/SecondaryTransform.GetScale3D();
						FVector CombinedScale = WorldScale * Scaler;

						// Clamp to the minimum and maximum values
						CombinedScale.X = FMath::Clamp(CombinedScale.X, Grip.AdvancedGripSettings.SecondaryGripSettings.MinimumGripScaling.X, Grip.AdvancedGripSettings.SecondaryGripSettings.MaximumGripScaling.X);
						CombinedScale.Y = FMath::Clamp(CombinedScale.Y, Grip.AdvancedGripSettings.SecondaryGripSettings.MinimumGripScaling.Y, Grip.AdvancedGripSettings.SecondaryGripSettings.MaximumGripScaling.Y);
						CombinedScale.Z = FMath::Clamp(CombinedScale.Z, Grip.AdvancedGripSettings.SecondaryGripSettings.MinimumGripScaling.Z, Grip.AdvancedGripSettings.SecondaryGripSettings.MaximumGripScaling.Z);

						// Recreate in scaler form so that the transform chain below works as normal
						Scaler = CombinedScale / WorldScale;
					}
					//Scaler = Grip.SecondaryScaler;
				}
			}

			Grip.SecondaryGripInfo.SecondaryGripDistance = FVector::Dist(frontLocOrig, frontLoc);

			if (Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripSettings && Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripDistanceInfluence)
			{
				//  If this is true it will treat the deadzone value as a constant value to apply instead
				/*if (Grip.AdvancedGripSettings.SecondaryGripSettings.bUseGripInfluenceDeadZoneAsConstant)
				{
					frontLoc = FMath::Lerp(frontLocOrig, frontLoc, FMath::Clamp(Grip.AdvancedGripSettings.SecondaryGripSettings.GripInfluenceDeadZone, 0.0f, 1.0f));
				}
				else
				{*/
					float rotScaler = 1.0f - FMath::Clamp((Grip.SecondaryGripInfo.SecondaryGripDistance - Grip.AdvancedGripSettings.SecondaryGripSettings.GripInfluenceDeadZone) / FMath::Max(Grip.AdvancedGripSettings.SecondaryGripSettings.GripInfluenceDistanceToZero, 1.0f), 0.0f, 1.0f);
					frontLoc = FMath::Lerp(frontLocOrig, frontLoc, rotScaler);
				//}
			}

			// Skip rot val for scaling only
			if (SecondaryType != ESecondaryGripType::SG_ScalingOnly)
			{
				// Get the rotation difference from the initial second grip
				FQuat rotVal = FQuat::FindBetweenVectors(frontLocOrig, frontLoc);

				// Rebase the world transform to the pivot point, add the rotation, remove the pivot point rebase
				WorldTransform = WorldTransform * WorldToPivot * FTransform(rotVal, FVector::ZeroVector, Scaler) * PivotToWorld;
			}
			else
			{
				// Rebase the world transform to the pivot point, add the scaler, remove the pivot point rebase
				WorldTransform = WorldTransform * WorldToPivot * FTransform(FQuat::Identity, FVector::ZeroVector, Scaler) * PivotToWorld;
			}
		}
	}
}

// Removed in 4.20
/*
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
}*/

void UGripMotionControllerComponent::TickGrip(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_TickGrip);

	// Debug test that we aren't floating physics handles
	if (PhysicsGrips.Num() > (GrippedObjects.Num() + LocallyGrippedObjects.Num()))
	{
		CleanUpBadPhysicsHandles();
		UE_LOG(LogVRMotionController, Warning, TEXT("Something went wrong, there were too many physics handles for how many grips exist! Cleaned up bad handles."));
	}
	//check(PhysicsGrips.Num() <= (GrippedObjects.Num() + LocallyGrippedObjects.Num()));

	FTransform ParentTransform = this->GetComponentTransform();

	// Split into separate functions so that I didn't have to combine arrays since I have some removal going on
	HandleGripArray(GrippedObjects, ParentTransform, DeltaTime, true);
	HandleGripArray(LocallyGrippedObjects, ParentTransform, DeltaTime);
}

void UGripMotionControllerComponent::HandleGripArray(TArray<FBPActorGripInformation> &GrippedObjectsArray, const FTransform & ParentTransform, float DeltaTime, bool bReplicatedArray)
{
	if (GrippedObjectsArray.Num())
	{
		FTransform WorldTransform;

		for (int i = GrippedObjectsArray.Num() - 1; i >= 0; --i)
		{
			if (!HasGripMovementAuthority(GrippedObjectsArray[i]))
				continue;

			FBPActorGripInformation * Grip = &GrippedObjectsArray[i];


			if (!Grip) // Shouldn't be possible, but why not play it safe
				continue;

			// Double checking here for a failed rep due to out of order replication from a spawned actor
			if (!Grip->ValueCache.bWasInitiallyRepped && !HasGripAuthority(*Grip) && !HandleGripReplication(*Grip))
				continue; // If we didn't successfully handle the replication (out of order) then continue on.

			// Continue if the grip is paused
			if (Grip->bIsPaused)
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
					if(bRootHasInterface)
						IVRGripInterface::Execute_TickGrip(root, this, *Grip, DeltaTime);
					else if(bActorHasInterface)
						IVRGripInterface::Execute_TickGrip(actor, this, *Grip, DeltaTime);

					continue;
				}

				bool bRescalePhysicsGrips = false;
				
				// Get the world transform for this grip after handling secondary grips and interaction differences
				GetGripWorldTransform(DeltaTime, WorldTransform, ParentTransform, *Grip, actor, root, bRootHasInterface, bActorHasInterface/*, bRescalePhysicsGrips*/);

				if (!root->GetComponentScale().Equals(WorldTransform.GetScale3D()))
					bRescalePhysicsGrips = true;

				// If we just teleported, skip this update and just teleport forward
				if (bIsPostTeleport)
				{
					TeleportMoveGrip_Impl(*Grip, true, true, WorldTransform);
					continue;
				}

				// Auto drop based on distance from expected point
				// Not perfect, should be done post physics or in next frame prior to changing controller location
				// However I don't want to recalculate world transform
				// Maybe add a grip variable of "expected loc" and use that to check next frame, but for now this will do.
				if ((bRootHasInterface || bActorHasInterface) &&
					(
							(Grip->GripCollisionType != EGripCollisionType::AttachmentGrip) &&
							(Grip->GripCollisionType != EGripCollisionType::PhysicsOnly) && 
							(Grip->GripCollisionType != EGripCollisionType::SweepWithPhysics)) &&
							((Grip->GripCollisionType != EGripCollisionType::InteractiveHybridCollisionWithSweep) || ((Grip->GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithSweep) && Grip->bColliding))
					)
				{

					// After initial teleportation the constraint local pose can be not updated yet, so lets delay a frame to let it update
					// Otherwise may cause unintended auto drops
					if (Grip->bSkipNextConstraintLengthCheck)
					{
						Grip->bSkipNextConstraintLengthCheck = false;
					}
					else
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

						FVector CheckDistance;
						if (!GetPhysicsJointLength(*Grip, root, CheckDistance))
						{
							CheckDistance = (WorldTransform.GetLocation() - root->GetComponentLocation());
						}

						// Set grip distance now for people to use
						Grip->GripDistance = CheckDistance.Size();

						if (BreakDistance > 0.0f)
						{
							if (Grip->GripDistance >= BreakDistance)
							{
								if (OnGripOutOfRange.IsBound())
								{
									uint8 GripID = Grip->GripID;
									OnGripOutOfRange.Broadcast(*Grip, Grip->GripDistance);

									// Check if we still have the grip or not
									FBPActorGripInformation GripInfo;
									EBPVRResultSwitch Result;
									GetGripByID(GripInfo, GripID, Result);
									if (Result == EBPVRResultSwitch::OnFailed)
									{
										// Don't bother moving it, it is dropped now
										continue;
									}
								}
								else if(HasGripAuthority(*Grip))
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

									// Don't bother moving it, it is dropped now
									continue;
								}
							}
						}
					}
				}

				// Start handling the grip types and their functions
				switch (Grip->GripCollisionType)
				{
					case EGripCollisionType::InteractiveCollisionWithPhysics:
					{
						UpdatePhysicsHandleTransform(*Grip, WorldTransform);
						
						if(bRescalePhysicsGrips)
							root->SetWorldScale3D(WorldTransform.GetScale3D());

						// Sweep current collision state, only used for client side late update removal
						if (
							(bHasAuthority &&
								((Grip->GripLateUpdateSetting == EGripLateUpdateSettings::NotWhenColliding) ||
									(Grip->GripLateUpdateSetting == EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping)))
							)
						{
							//TArray<FOverlapResult> Hits;
							FComponentQueryParams Params(NAME_None, this->GetOwner());
							Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
							Params.AddIgnoredActor(actor);
							Params.AddIgnoredActors(root->MoveIgnoreActors);

							FHitResult Hit;
							if(GetWorld()->SweepSingleByChannel(Hit, root->GetComponentLocation(), WorldTransform.GetLocation(), WorldTransform.GetRotation(), root->GetCollisionObjectType(), root->GetCollisionShape(),Params))
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

					case EGripCollisionType::InteractiveHybridCollisionWithPhysics:
					{
						UpdatePhysicsHandleTransform(*Grip, WorldTransform);

						if (bRescalePhysicsGrips)
							root->SetWorldScale3D(WorldTransform.GetScale3D());

						// Always Sweep current collision state with this, used for constraint strength
						//TArray<FOverlapResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);

						FHitResult Hit;
						// Checking both current and next position for overlap using this grip type #TODO: Do this for normal interactive physics as well?
						if (GetWorld()->SweepSingleByChannel(Hit, root->GetComponentLocation(), WorldTransform.GetLocation(), WorldTransform.GetRotation(), root->GetCollisionObjectType(), root->GetCollisionShape(), Params))
						/*if (GetWorld()->ComponentOverlapMultiByChannel(Hits, root, root->GetComponentLocation(), root->GetComponentQuat(), root->GetCollisionObjectType(), Params) ||
							GetWorld()->ComponentOverlapMultiByChannel(Hits, root, WorldTransform.GetLocation(), WorldTransform.GetRotation(), root->GetCollisionObjectType(), Params)
							)*/
						{
							if (!Grip->bColliding)
							{
								SetGripConstraintStiffnessAndDamping(Grip, false);
							}
							Grip->bColliding = true;
						}
						else
						{
							if (Grip->bColliding)
							{
								SetGripConstraintStiffnessAndDamping(Grip, true);
							}

							Grip->bColliding = false;
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
								if (bRescalePhysicsGrips)
									root->SetWorldScale3D(WorldTransform.GetScale3D());
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
							if (bRescalePhysicsGrips)
								root->SetWorldScale3D(WorldTransform.GetScale3D());
						}
						else
						{
							// Shouldn't be a grip handle if not server when server side moving
							if (GripHandle)
							{
								UpdatePhysicsHandleTransform(*Grip, WorldTransform);
								if (bRescalePhysicsGrips)
									root->SetWorldScale3D(WorldTransform.GetScale3D());
							}
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

								TArray<USceneComponent* > PrimChildren;
								root->GetChildrenComponents(true, PrimChildren);
								for (USceneComponent * Prim : PrimChildren)
								{
									if (UPrimitiveComponent * primComp = Cast<UPrimitiveComponent>(Prim))
									{
										CheckComponentWithSweep(primComp, move, primComp->GetComponentRotation(), false);
									}
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

					case EGripCollisionType::AttachmentGrip:
					{
						FTransform RelativeTrans = WorldTransform.GetRelativeTransform(ParentTransform);
						if (!root->GetRelativeTransform().Equals(RelativeTrans))
						{
							root->SetRelativeTransform(RelativeTrans);
						}

					}break;

					case EGripCollisionType::ManipulationGrip:
					case EGripCollisionType::ManipulationGripWithWristTwist:
					{
						UpdatePhysicsHandleTransform(*Grip, WorldTransform);
						if (bRescalePhysicsGrips)
							root->SetWorldScale3D(WorldTransform.GetScale3D());
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
						IVRGripInterface::Execute_TickGrip(root, this, *Grip, DeltaTime);
					}

					if (bActorHasInterface)
					{
						IVRGripInterface::Execute_TickGrip(actor, this, *Grip, DeltaTime);
					}
				}
			}
			else
			{
				// Object has been destroyed without notification to plugin
				CleanUpBadGrip(GrippedObjectsArray, i, bReplicatedArray);
			}
		}

		// Empty out the teleport flag
		bIsPostTeleport = false;
	}
}


void UGripMotionControllerComponent::CleanUpBadGrip(TArray<FBPActorGripInformation> &GrippedObjectsArray, int GripIndex, bool bReplicatedArray)
{
	// Object has been destroyed without notification to plugin
	if (!DestroyPhysicsHandle(GrippedObjectsArray[GripIndex]))
	{
		// Clean up tailing physics handles with null objects
		for (int g = PhysicsGrips.Num() - 1; g >= 0; --g)
		{
			if (!PhysicsGrips[g].HandledObject || PhysicsGrips[g].HandledObject == GrippedObjectsArray[GripIndex].GrippedObject || PhysicsGrips[g].HandledObject->IsPendingKill())
			{
				// Need to delete it from the physics thread
				DestroyPhysicsHandle(PhysicsGrips[g].SceneIndex, &PhysicsGrips[g].HandleData, &PhysicsGrips[g].KinActorData);
				PhysicsGrips.RemoveAt(g);
			}
		}
	}

	if (HasGripAuthority(GrippedObjectsArray[GripIndex]))
	{
		DropGrip(GrippedObjectsArray[GripIndex], false);
		UE_LOG(LogVRMotionController, Warning, TEXT("Gripped object was null or destroying, auto dropping it"));
	}
	else
	{
		GrippedObjectsArray[GripIndex].bIsPaused = true;
	}
}

void UGripMotionControllerComponent::CleanUpBadPhysicsHandles()
{
	// Clean up tailing physics handles with null objects
	for (int g = PhysicsGrips.Num() - 1; g >= 0; --g)
	{
		FBPActorGripInformation * GripInfo = LocallyGrippedObjects.FindByKey(PhysicsGrips[g].GripID);
		if(!GripInfo)
			GrippedObjects.FindByKey(PhysicsGrips[g].GripID);

		if (!GripInfo)
		{
			// Need to delete it from the physics thread
			DestroyPhysicsHandle(PhysicsGrips[g].SceneIndex, &PhysicsGrips[g].HandleData, &PhysicsGrips[g].KinActorData);
			PhysicsGrips.RemoveAt(g);
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

	// Reset center of mass to zero
	if (HandleInfo->bSetCOM)
	{
		UPrimitiveComponent *root = Grip.GetGrippedComponent();
		AActor * pActor = Grip.GetGrippedActor();

		if (!root && pActor)
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

		if (root)
		{
			root->SetCenterOfMass(FVector(0, 0, 0), Grip.GrippedBoneName);
		}
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
	/*if(NewGrip.GrippedBoneName == NAME_None)
		root->SetSimulatePhysics(true);
	else
	{
		USkeletalMeshComponent * skele = Cast<USkeletalMeshComponent>(root);
		if (skele)
			skele->SetAllBodiesBelowSimulatePhysics(NewGrip.GrippedBoneName, true);
	}*/

	FBPActorPhysicsHandleInformation * HandleInfo = CreatePhysicsGrip(NewGrip);

#if WITH_PHYSX
	// Get the PxRigidDynamic that we want to grab.
	FBodyInstance* rBodyInstance = root->GetBodyInstance(NewGrip.GrippedBoneName);
	if (!rBodyInstance || !rBodyInstance->IsValidBodyInstance())
	{	
		return false;
	}
	
	check(rBodyInstance->BodySetup->GetCollisionTraceFlag() != CTF_UseComplexAsSimple);

	/*if (NewGrip.GrippedBoneName != NAME_None)
	{
		rBodyInstance->SetInstanceSimulatePhysics(true);
	}*/

	ExecuteOnPxRigidDynamicReadWrite(rBodyInstance, [&](PxRigidDynamic* Actor)
	{
		PxScene* Scene = Actor->getScene();
			
		PxTransform KinPose;
		FTransform trans = P2UTransform(Actor->getGlobalPose()); //root->GetComponentTransform();
		FTransform controllerTransform = this->GetComponentTransform();
		FTransform WorldTransform = NewGrip.RelativeTransform * controllerTransform;
		FTransform RootBoneRotation = FTransform::Identity;

		if (NewGrip.GrippedBoneName != NAME_None)
		{
			// Skip root bone rotation
		}
		else
		{
			// I actually don't need any of this code anymore or the HandleInfo->RootBoneRotation
			// However I would have to expect people to pass in the bone transform without it.
			// For now I am keeping it to keep it backwards compatible as it will adjust for root bone rotation automatically then
			USkeletalMeshComponent * skele = Cast<USkeletalMeshComponent>(root);
			if (skele && skele->GetNumBones() > 0)
			{
				RootBoneRotation = FTransform(skele->GetBoneTransform(0, FTransform::Identity));
				HandleInfo->RootBoneRotation = RootBoneRotation;
			}
			// Add in root bone rotation
			WorldTransform = RootBoneRotation * WorldTransform;
		}
		EPhysicsGripCOMType COMType = NewGrip.AdvancedGripSettings.PhysicsSettings.PhysicsGripLocationSettings;

		if (!NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings || COMType == EPhysicsGripCOMType::COM_Default)
		{
			if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGrip || NewGrip.GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
			{
				COMType = EPhysicsGripCOMType::COM_GripAtControllerLoc;
			}
			else
			{
				COMType = EPhysicsGripCOMType::COM_SetAndGripAt;
			}
		}

		if (COMType == EPhysicsGripCOMType::COM_SetAndGripAt)
		{
			FVector curCOMPosition = P2UVector(Actor->getCMassLocalPose().p);

			// Trans is our physics location, and it has no scale
			// Need the scale to get the correct COM position 
			curCOMPosition /= rBodyInstance->Scale3D;

			rBodyInstance->COMNudge = controllerTransform.GetRelativeTransform(WorldTransform).GetLocation() - curCOMPosition;
			rBodyInstance->UpdateMassProperties();
			HandleInfo->bSetCOM = true;
		}

		if (COMType == EPhysicsGripCOMType::COM_GripAtControllerLoc)
		{
			trans.SetLocation(controllerTransform.GetLocation());
			HandleInfo->COMPosition = U2PTransform(FTransform(rBodyInstance->GetUnrealWorldTransform().InverseTransformPosition(controllerTransform.GetLocation())));
		}
		else if (COMType != EPhysicsGripCOMType::COM_AtPivot)
		{
			trans.SetLocation(rBodyInstance->GetCOMPosition());
			HandleInfo->COMPosition = U2PTransform(FTransform(rBodyInstance->GetUnrealWorldTransform().InverseTransformPosition(rBodyInstance->GetCOMPosition())));
		}

		KinPose = U2PTransform(trans);
		
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

			// Create the joint
			NewJoint = PxD6JointCreate(Scene->getPhysics(), KinActor, PxTransform(PxIdentity), Actor, Actor->getGlobalPose().transformInv(KinPose));

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
				const uint32 SceneType = rBodyInstance->UseAsyncScene(RBScene) ? PST_Async : PST_Sync;
				HandleInfo->SceneIndex = RBScene->PhysXSceneIndex[SceneType];

				// Pretty Much Unbreakable
				NewJoint->setBreakForce(PX_MAX_REAL, PX_MAX_REAL);

				// Different settings for manip grip
				if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGrip || NewGrip.GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
				{
					NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eFREE);

					NewJoint->setMotion(PxD6Axis::eTWIST, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING1, PxD6Motion::eFREE);
					NewJoint->setMotion(PxD6Axis::eSWING2, PxD6Motion::eFREE);

					PxD6JointDrive drive;

					if (NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.PhysicsConstraintType == EPhysicsGripConstraintType::ForceConstraint)
					{
						drive = PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32);
					}
					else
					{
						drive = PxD6JointDrive(NewGrip.Stiffness, NewGrip.Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
					}

					NewJoint->setDrive(PxD6Drive::eX, drive);
					NewJoint->setDrive(PxD6Drive::eY, drive);
					NewJoint->setDrive(PxD6Drive::eZ, drive);

					if( NewGrip.GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
						NewJoint->setDrive(PxD6Drive::eTWIST, drive);
				}
				else
				{
					float Stiffness = NewGrip.Stiffness;
					float Damping = NewGrip.Damping;
					float AngularStiffness;
					float AngularDamping;

					if (NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.bUseCustomAngularValues)
					{
						AngularStiffness = NewGrip.AdvancedGripSettings.PhysicsSettings.AngularStiffness;
						AngularDamping = NewGrip.AdvancedGripSettings.PhysicsSettings.AngularDamping;
					}
					else
					{
						AngularStiffness = Stiffness * ANGULAR_STIFFNESS_MULTIPLIER; // Default multiplier
						AngularDamping = Damping * ANGULAR_DAMPING_MULTIPLIER; // Default multiplier
					}

					if (NewGrip.GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithPhysics)
					{
						// Do not effect damping, just increase stiffness so that it is stronger
						// Default multiplier
						Stiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;
						AngularStiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;
					}

					PxD6JointDrive drive;
					PxD6JointDrive Angledrive;

					if (NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.PhysicsConstraintType == EPhysicsGripConstraintType::ForceConstraint)
					{
						drive = PxD6JointDrive(Stiffness, Damping, PX_MAX_F32);
						Angledrive = PxD6JointDrive(AngularStiffness, AngularDamping, PX_MAX_F32);
					}
					else
					{
						drive = PxD6JointDrive(Stiffness, Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
						Angledrive = PxD6JointDrive(AngularStiffness, AngularDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
					}

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

bool UGripMotionControllerComponent::SetGripConstraintStiffnessAndDamping(const FBPActorGripInformation *Grip, bool bUseHybridMultiplier)
{
	if (!Grip)
		return false;

	FBPActorPhysicsHandleInformation * Handle = GetPhysicsGrip(*Grip);

	if (Handle)
	{
#if WITH_PHYSX
		if (Handle->HandleData != nullptr)
		{
			// Different settings for manip grip
			if (Grip->GripCollisionType == EGripCollisionType::ManipulationGrip || Grip->GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
			{
				PxD6JointDrive drive;

				if (Grip->AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && Grip->AdvancedGripSettings.PhysicsSettings.PhysicsConstraintType == EPhysicsGripConstraintType::ForceConstraint)
				{
					drive = PxD6JointDrive(Grip->Stiffness, Grip->Damping, PX_MAX_F32);
				}
				else
				{
					drive = PxD6JointDrive(Grip->Stiffness, Grip->Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
				}

				Handle->HandleData->setDrive(PxD6Drive::eX, drive);
				Handle->HandleData->setDrive(PxD6Drive::eY, drive);
				Handle->HandleData->setDrive(PxD6Drive::eZ, drive);

				if (Grip->GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
					Handle->HandleData->setDrive(PxD6Drive::eTWIST, drive);
			}
			else
			{
				float Stiffness = Grip->Stiffness;
				float Damping = Grip->Damping;
				float AngularStiffness;
				float AngularDamping;

				if (Grip->AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && Grip->AdvancedGripSettings.PhysicsSettings.bUseCustomAngularValues)
				{
					AngularStiffness = Grip->AdvancedGripSettings.PhysicsSettings.AngularStiffness;
					AngularDamping = Grip->AdvancedGripSettings.PhysicsSettings.AngularDamping;
				}
				else
				{
					AngularStiffness = Stiffness * ANGULAR_STIFFNESS_MULTIPLIER; // Default multiplier
					AngularDamping = Damping * ANGULAR_DAMPING_MULTIPLIER; // Default multiplier
				}

				// Used for interactive hybrid with physics grip when not colliding
				if (bUseHybridMultiplier)
				{
					// Do not effect damping, just increase stiffness so that it is stronger
					// Default multiplier
					Stiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;
					AngularStiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;
				}

				PxD6JointDrive drive;
				PxD6JointDrive Angledrive;

				if (Grip->AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && Grip->AdvancedGripSettings.PhysicsSettings.PhysicsConstraintType == EPhysicsGripConstraintType::ForceConstraint)
				{
					drive = PxD6JointDrive(Stiffness, Damping, PX_MAX_F32);
					Angledrive = PxD6JointDrive(AngularStiffness, AngularDamping, PX_MAX_F32);
				}
				else
				{
					drive = PxD6JointDrive(Stiffness, Damping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
					Angledrive = PxD6JointDrive(AngularStiffness, AngularDamping, PX_MAX_F32, PxD6JointDriveFlag::eACCELERATION);
				}

				Handle->HandleData->setDrive(PxD6Drive::eX, drive);
				Handle->HandleData->setDrive(PxD6Drive::eY, drive);
				Handle->HandleData->setDrive(PxD6Drive::eZ, drive);
				Handle->HandleData->setDrive(PxD6Drive::eSLERP, Angledrive);
			}
		}
#endif // WITH_PHYSX
		return true;
	}

	return false;
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

	FTransform rr;
	FBodyInstance* rBodyInstance = rootComp->GetBodyInstance(GrippedActor.GrippedBoneName);
	if (!rBodyInstance || !rBodyInstance->IsValidBodyInstance())
	{
		rr = rootComp->GetComponentTransform();
		// Physx location throws out scale, this is where the problem was
		rr.SetScale3D(FVector(1, 1, 1));
	}
	else
		rr = rBodyInstance->GetUnrealWorldTransform();

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

		// Debug draw for COM movement with physics grips
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (GripMotionControllerCvars::DrawDebugGripCOM)
		{
			UPrimitiveComponent * me = Cast<UPrimitiveComponent>(GrippedActor.GripTargetType == EGripTargetType::ActorGrip ? GrippedActor.GetGrippedActor()->GetRootComponent() : GrippedActor.GetGrippedComponent());
			FVector curCOMPosition = me->GetBodyInstance(GrippedActor.GrippedBoneName)->GetCOMPosition();//rBodyInstance->GetUnrealWorldTransform().InverseTransformPosition(rBodyInstance->GetCOMPosition());
			DrawDebugSphere(GetWorld(), curCOMPosition, 4, 32, FColor::Red, false);
			DrawDebugSphere(GetWorld(), P2UTransform(U2PTransform(HandleInfo->RootBoneRotation * terns) * HandleInfo->COMPosition).GetLocation(), 4, 32, FColor::Cyan, false);
			//DrawDebugSphere(GetWorld(), terns.GetLocation(), 4, 32, FColor::Cyan, false);
		}
#endif
		KinActor->setKinematicTarget(U2PTransform(HandleInfo->RootBoneRotation * terns) * HandleInfo->COMPosition);
	}
#endif // WITH_PHYSX
}

static void PullBackHitComp(FHitResult& Hit, const FVector& Start, const FVector& End, const float Dist)
{
	const float DesiredTimeBack = FMath::Clamp(0.1f, 0.1f / Dist, 1.f / Dist) + 0.001f;
	Hit.Time = FMath::Clamp(Hit.Time - DesiredTimeBack, 0.f, 1.f);
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

		FVector end = start + Move;
		bool const bHadBlockingHit = MyWorld->ComponentSweepMulti(Hits, root, start, end, newOrientation.Quaternion(), Params);

		if (Hits.Num() > 0)
		{
			const float DeltaSize = FVector::Dist(start, end);
			for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
			{
				PullBackHitComp(Hits[HitIdx], start, end, DeltaSize);
			}
		}

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
bool UGripMotionControllerComponent::GripPollControllerState(FVector& Position, FRotator& Orientation , float WorldToMetersScale)
{
	// Not calling PollControllerState from the parent because its private.......

	bool bIsInGameThread = IsInGameThread();

	if (bHasAuthority)
	{
		// New iteration and retrieval for 4.12
		TArray<IMotionController*> MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
		for (auto MotionController : MotionControllers)
		{
			if (MotionController == nullptr)
			{
				continue;
			}
			
			CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, MotionSource);
			if (MotionController->GetControllerOrientationAndPosition(PlayerIndex, MotionSource, Orientation, Position, WorldToMetersScale))
			{
				if (bOffsetByHMD)
				{
					if (bIsInGameThread)
					{
						if (GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowed())
						{
							FQuat curRot;
							FVector curLoc;
							if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, curRot, curLoc))
							{
								curLoc.Z = 0;
								LastLocationForLateUpdate = curLoc;
							}
							else
							{
								 // Keep last location instead
							}
						}
					}

					// #TODO: This is technically unsafe, need to use a seperate value like the transforms for the render thread
					// If I ever delete the simple char then this setup can just go away anyway though
					// It has a data race condition right now though
					Position -= LastLocationForLateUpdate;
				}

				if (bOffsetByControllerProfile)
				{
					FTransform FinalControllerTransform(Orientation,Position);
					if (bIsInGameThread)
					{
						FinalControllerTransform = CurrentControllerProfileTransform * FinalControllerTransform;
					}
					else
					{
						FinalControllerTransform = GripRenderThreadProfileTransform * FinalControllerTransform;
					}
					
					Orientation = FinalControllerTransform.GetRotation().Rotator();
					Position = FinalControllerTransform.GetTranslation();
				}

				// Render thread also calls this, shouldn't be flagging this event in the render thread.
				if (bIsInGameThread)
				{
					InUseMotionController = MotionController;
					OnMotionControllerUpdated();
					InUseMotionController = nullptr;
				}
							
				return true;
			}
		}

		// #NOTE: This was adding in 4.20, I presume to allow for HMDs as tracking sources for mixed reality.
		// Skipping all of my special logic here for now
		if (MotionSource == FXRMotionControllerBase::HMDSourceId)
		{
			IXRTrackingSystem* TrackingSys = GEngine->XRSystem.Get();
			if (TrackingSys)
			{
				FQuat OrientationQuat;
				if (TrackingSys->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, OrientationQuat, Position))
				{
					Orientation = OrientationQuat.Rotator();
					return true;
				}
			}
		}
	}
	return false;
}

//=============================================================================
UGripMotionControllerComponent::FGripViewExtension::FGripViewExtension(const FAutoRegister& AutoRegister, UGripMotionControllerComponent* InMotionControllerComponent)
	: FSceneViewExtensionBase(AutoRegister)
	, MotionControllerComponent(InMotionControllerComponent)
{}


void UGripMotionControllerComponent::FGripViewExtension::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	FTransform OldTransform;
	FTransform NewTransform;

	{
		FScopeLock ScopeLock(&CritSect);

		if (!MotionControllerComponent)
			return;

		// Find a view that is associated with this player.
		float WorldToMetersScale = -1.0f;
		for (const FSceneView* SceneView : InViewFamily.Views)
		{
			if (SceneView && SceneView->PlayerIndex == MotionControllerComponent->PlayerIndex)
			{
				WorldToMetersScale = SceneView->WorldToMetersScale;
				break;
			}
		}

		// If there are no views associated with this player use view 0.
		if (WorldToMetersScale < 0.0f)
		{
			check(InViewFamily.Views.Num() > 0);
			WorldToMetersScale = InViewFamily.Views[0]->WorldToMetersScale;
		}

		// Poll state for the most recent controller transform
		FVector Position;
		FRotator Orientation;

		if (!MotionControllerComponent->GripPollControllerState(Position, Orientation, WorldToMetersScale))
		{
			return;
		}

		OldTransform = MotionControllerComponent->GripRenderThreadRelativeTransform;
		NewTransform = FTransform(Orientation, Position, MotionControllerComponent->GripRenderThreadComponentScale);
	} // Release lock on motion controller component

	  // Tell the late update manager to apply the offset to the scene components
	LateUpdate.Apply_RenderThread(InViewFamily.Scene, OldTransform, NewTransform);
}

void UGripMotionControllerComponent::FGripViewExtension::PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	if (!MotionControllerComponent)
	{
		return;
	}
	LateUpdate.PostRender_RenderThread();
}

bool UGripMotionControllerComponent::FGripViewExtension::IsActiveThisFrame(class FViewport* InViewport) const
{
	check(IsInGameThread());

	static const auto CVarEnableMotionControllerLateUpdate = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.EnableMotionControllerLateUpdate"));
	return MotionControllerComponent && !MotionControllerComponent->bDisableLowLatencyUpdate && CVarEnableMotionControllerLateUpdate->GetValueOnGameThread();
}

void UGripMotionControllerComponent::GetAllGrips(TArray<FBPActorGripInformation> &GripArray)
{
	GripArray.Append(GrippedObjects);
	GripArray.Append(LocallyGrippedObjects);
}

void UGripMotionControllerComponent::GetGrippedObjects(TArray<UObject*> &GrippedObjectsArray)
{
	for (int i = 0; i < GrippedObjects.Num(); ++i)
	{
		if (GrippedObjects[i].GrippedObject)
			GrippedObjectsArray.Add(GrippedObjects[i].GrippedObject);
	}

	for (int i = 0; i < LocallyGrippedObjects.Num(); ++i)
	{
		if (LocallyGrippedObjects[i].GrippedObject)
			GrippedObjectsArray.Add(LocallyGrippedObjects[i].GrippedObject);
	}

}

void UGripMotionControllerComponent::GetGrippedActors(TArray<AActor*> &GrippedObjectsArray)
{
	for (int i = 0; i < GrippedObjects.Num(); ++i)
	{
		if(GrippedObjects[i].GetGrippedActor())
			GrippedObjectsArray.Add(GrippedObjects[i].GetGrippedActor());
	}

	for (int i = 0; i < LocallyGrippedObjects.Num(); ++i)
	{
		if (LocallyGrippedObjects[i].GetGrippedActor())
			GrippedObjectsArray.Add(LocallyGrippedObjects[i].GetGrippedActor());
	}

}

void UGripMotionControllerComponent::GetGrippedComponents(TArray<UPrimitiveComponent*> &GrippedComponentsArray)
{
	for (int i = 0; i < GrippedObjects.Num(); ++i)
	{
		if (GrippedObjects[i].GetGrippedComponent())
			GrippedComponentsArray.Add(GrippedObjects[i].GetGrippedComponent());
	}

	for (int i = 0; i < LocallyGrippedObjects.Num(); ++i)
	{
		if (LocallyGrippedObjects[i].GetGrippedComponent())
			GrippedComponentsArray.Add(LocallyGrippedObjects[i].GetGrippedComponent());
	}
}

// Locally gripped functions

bool UGripMotionControllerComponent::Client_NotifyInvalidLocalGrip_Validate(UObject * LocallyGrippedObject)
{
	return true;
}

void UGripMotionControllerComponent::Client_NotifyInvalidLocalGrip_Implementation(UObject * LocallyGrippedObject)
{
	FBPActorGripInformation FoundGrip;
	EBPVRResultSwitch Result;
	GetGripByObject(FoundGrip, LocallyGrippedObject, Result);

	if (Result == EBPVRResultSwitch::OnFailed)
		return;

	// Drop it, server told us that it was a bad grip
	DropObjectByInterface(FoundGrip.GrippedObject);
}

bool UGripMotionControllerComponent::Server_NotifyLocalGripAddedOrChanged_Validate(const FBPActorGripInformation & newGrip)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifyLocalGripAddedOrChanged_Implementation(const FBPActorGripInformation & newGrip)
{
	if (!newGrip.GrippedObject || newGrip.GripMovementReplicationSetting != EGripMovementReplicationSettings::ClientSide_Authoritive)
	{
		Client_NotifyInvalidLocalGrip(newGrip.GrippedObject);
		return;
	}

	if (!LocallyGrippedObjects.Contains(newGrip))
	{
		LocallyGrippedObjects.Add(newGrip);

		// Initialize the differences, clients will do this themselves on the rep back, this sets up the cache
		//HandleGripReplication(LocallyGrippedObjects[LocallyGrippedObjects.Num() - 1]);
	}
	else
	{
		int32 IndexFound;
		if (LocallyGrippedObjects.Find(newGrip, IndexFound))
		{
			LocallyGrippedObjects[IndexFound].RepCopy(newGrip);
		}
	}

	// Server has to call this themselves
	OnRep_LocallyGrippedObjects();
}


bool UGripMotionControllerComponent::Server_NotifyLocalGripRemoved_Validate(uint8 GripID, FVector_NetQuantize100 AngularVelocity, FVector_NetQuantize100 LinearVelocity)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifyLocalGripRemoved_Implementation(uint8 GripID, FVector_NetQuantize100 AngularVelocity, FVector_NetQuantize100 LinearVelocity)
{
	FBPActorGripInformation FoundGrip;
	EBPVRResultSwitch Result;
	GetGripByID(FoundGrip, GripID, Result);

	if (Result == EBPVRResultSwitch::OnFailed)
		return;

	if (!DropObjectByInterface(FoundGrip.GrippedObject, AngularVelocity, LinearVelocity))
	{
		DropGrip(FoundGrip, false, AngularVelocity, LinearVelocity);
	}
}


bool UGripMotionControllerComponent::Server_NotifySecondaryAttachmentChanged_Validate(
	uint8 GripID,
	const FBPSecondaryGripInfo& SecondaryGripInfo)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifySecondaryAttachmentChanged_Implementation(
	uint8 GripID,
	const FBPSecondaryGripInfo& SecondaryGripInfo)
{

	for (FBPActorGripInformation & Grip : LocallyGrippedObjects)
	{
		if (Grip == GripID)
		{
			// I override the = operator now so that it won't set the lerp components
			Grip.SecondaryGripInfo.RepCopy(SecondaryGripInfo);

			// Initialize the differences, clients will do this themselves on the rep back
			HandleGripReplication(Grip);
			break;
		}
	}

}

bool UGripMotionControllerComponent::Server_NotifySecondaryAttachmentChanged_Retain_Validate(
	uint8 GripID,
	const FBPSecondaryGripInfo& SecondaryGripInfo, const FTransform_NetQuantize & NewRelativeTransform)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifySecondaryAttachmentChanged_Retain_Implementation(
	uint8 GripID,
	const FBPSecondaryGripInfo& SecondaryGripInfo, const FTransform_NetQuantize & NewRelativeTransform)
{

	for (FBPActorGripInformation & Grip : LocallyGrippedObjects)
	{
		if (Grip == GripID)
		{
			// I override the = operator now so that it won't set the lerp components
			Grip.SecondaryGripInfo.RepCopy(SecondaryGripInfo);
			Grip.RelativeTransform = NewRelativeTransform;

			// Initialize the differences, clients will do this themselves on the rep back
			HandleGripReplication(Grip);
			break;
		}
	}

}
void UGripMotionControllerComponent::GetControllerDeviceID(FXRDeviceId & DeviceID, EBPVRResultSwitch &Result, bool bCheckOpenVROnly)
{
	EControllerHand ControllerHandIndex;
	if (!FXRMotionControllerBase::GetHandEnumForSourceName(MotionSource, ControllerHandIndex))
	{
		Result = EBPVRResultSwitch::OnFailed;
		return;
	}

	TArray<IXRSystemAssets*> XRAssetSystems = IModularFeatures::Get().GetModularFeatureImplementations<IXRSystemAssets>(IXRSystemAssets::GetModularFeatureName());
	for (IXRSystemAssets* AssetSys : XRAssetSystems)
	{
		if (bCheckOpenVROnly && !AssetSys->GetSystemName().IsEqual(FName(TEXT("SteamVR"))))
			continue;

		const int32 XRID = AssetSys->GetDeviceId(ControllerHandIndex);

		if (XRID != INDEX_NONE)
		{
			DeviceID = FXRDeviceId(AssetSys, XRID);
			Result = EBPVRResultSwitch::OnSucceeded;
			return;
		}
	}

	DeviceID = FXRDeviceId();
	Result = EBPVRResultSwitch::OnFailed;
	return;
}


/*
*
*	Custom late update manager implementation
*
*/

FExpandedLateUpdateManager::FExpandedLateUpdateManager()
	: LateUpdateGameWriteIndex(0)
	, LateUpdateRenderReadIndex(0)
{
	SkipLateUpdate[0] = false;
	SkipLateUpdate[1] = false;
}

void FExpandedLateUpdateManager::Setup(const FTransform& ParentToWorld, UGripMotionControllerComponent* Component, bool bSkipLateUpdate)
{
	if (!Component)
		return;

	check(IsInGameThread());

	LateUpdateParentToWorld[LateUpdateGameWriteIndex] = ParentToWorld;
	LateUpdatePrimitives[LateUpdateGameWriteIndex].Reset();
	GatherLateUpdatePrimitives(Component);
	SkipLateUpdate[LateUpdateGameWriteIndex] = bSkipLateUpdate;

	//Add additional late updates registered to this controller that aren't children and aren't gripped
	//This array is editable in blueprint and can be used for things like arms or the like.

	for (UPrimitiveComponent* primComp : Component->AdditionalLateUpdateComponents)
	{
		if (primComp)
			GatherLateUpdatePrimitives(primComp);
	}


	ProcessGripArrayLateUpdatePrimitives(Component, Component->LocallyGrippedObjects);
	ProcessGripArrayLateUpdatePrimitives(Component, Component->GrippedObjects);

	LateUpdateGameWriteIndex = (LateUpdateGameWriteIndex + 1) % 2;
}

bool FExpandedLateUpdateManager::GetSkipLateUpdate_RenderThread() const
{
	return SkipLateUpdate[LateUpdateRenderReadIndex];
}


void FExpandedLateUpdateManager::Apply_RenderThread(FSceneInterface* Scene, const FTransform& OldRelativeTransform, const FTransform& NewRelativeTransform)
{
	check(IsInRenderingThread());

	if (!LateUpdatePrimitives[LateUpdateRenderReadIndex].Num())
	{
		return;
	}

	if (GetSkipLateUpdate_RenderThread())
	{
		return;
	}

	const FTransform OldTransform = OldRelativeTransform * LateUpdateParentToWorld[LateUpdateRenderReadIndex];
	const FTransform NewTransform = NewRelativeTransform * LateUpdateParentToWorld[LateUpdateRenderReadIndex];
	const FMatrix LateUpdateTransform = (OldTransform.Inverse() * NewTransform).ToMatrixWithScale();

	// Apply delta to the affected scene proxies
	for (auto PrimitiveInfo : LateUpdatePrimitives[LateUpdateRenderReadIndex])
	{
		FPrimitiveSceneInfo* RetrievedSceneInfo = Scene->GetPrimitiveSceneInfo(*PrimitiveInfo.IndexAddress);
		FPrimitiveSceneInfo* CachedSceneInfo = PrimitiveInfo.SceneInfo;
		// If the retrieved scene info is different than our cached scene info then the primitive was removed from the scene
		if (CachedSceneInfo == RetrievedSceneInfo && CachedSceneInfo->Proxy)
		{
			CachedSceneInfo->Proxy->ApplyLateUpdateTransform(LateUpdateTransform);
		}
	}
}

void FExpandedLateUpdateManager::PostRender_RenderThread()
{
	LateUpdatePrimitives[LateUpdateRenderReadIndex].Reset();
	SkipLateUpdate[LateUpdateRenderReadIndex] = false;
	LateUpdateRenderReadIndex = (LateUpdateRenderReadIndex + 1) % 2;
}

void FExpandedLateUpdateManager::CacheSceneInfo(USceneComponent* Component)
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
			LateUpdatePrimitives[LateUpdateGameWriteIndex].Add(PrimitiveInfo);
		}
	}
}

void FExpandedLateUpdateManager::GatherLateUpdatePrimitives(USceneComponent* ParentComponent)
{
	CacheSceneInfo(ParentComponent);

	TArray<USceneComponent*> Components;
	ParentComponent->GetChildrenComponents(true, Components);
	for (USceneComponent* Component : Components)
	{	
		if (Component != nullptr)
			CacheSceneInfo(Component);
	}
}

void FExpandedLateUpdateManager::ProcessGripArrayLateUpdatePrimitives(UGripMotionControllerComponent * MotionControllerComponent, TArray<FBPActorGripInformation> & GripArray)
{
	for (FBPActorGripInformation actor : GripArray)
	{
		// Skip actors that are colliding if turning off late updates during collision.
		// Also skip turning off late updates for SweepWithPhysics, as it should always be locked to the hand

		// Don't allow late updates with server sided movement, there is no point
		if (actor.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !MotionControllerComponent->IsServer())
			continue;

		// Don't late update paused grips
		if (actor.bIsPaused)
			continue;

		// Attachment should already handle this for us, don't double transpose the late update
		if (actor.GripCollisionType == EGripCollisionType::AttachmentGrip)
			continue;

		switch (actor.GripLateUpdateSetting)
		{
		case EGripLateUpdateSettings::LateUpdatesAlwaysOff:
		{
			continue;
		}break;
		case EGripLateUpdateSettings::NotWhenColliding:
		{
			if (actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics && 
				actor.GripCollisionType != EGripCollisionType::PhysicsOnly)
				continue;
		}break;
		case EGripLateUpdateSettings::NotWhenDoubleGripping:
		{
			if (actor.SecondaryGripInfo.bHasSecondaryAttachment)
				continue;
		}break;
		case EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping:
		{
			if (
				(actor.bColliding && actor.GripCollisionType != EGripCollisionType::SweepWithPhysics && actor.GripCollisionType != EGripCollisionType::PhysicsOnly) ||
				(actor.SecondaryGripInfo.bHasSecondaryAttachment)
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
					GatherLateUpdatePrimitives(rootComponent);
				}
			}

		}break;

		case EGripTargetType::ComponentGrip:
			//case EGripTargetType::InteractibleComponentGrip:
		{
			UPrimitiveComponent * cPrimComp = actor.GetGrippedComponent();
			if (cPrimComp)
			{
				GatherLateUpdatePrimitives(cPrimComp);
			}
		}break;
		}
	}
}