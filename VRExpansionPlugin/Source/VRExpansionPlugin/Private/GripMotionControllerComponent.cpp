// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "GripMotionControllerComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GripMotionControllerComponent)

#include "VRExpansionFunctionLibrary.h"
#include "IHeadMountedDisplay.h"
#include "HeadMountedDisplayTypes.h"
#include "Misc/ScopeLock.h"
#include "Net/UnrealNetwork.h"
#include "PrimitiveSceneInfo.h"
#include "Engine/World.h"
#include "PrimitiveSceneProxy.h"
#include "GameFramework/WorldSettings.h"
#include "IXRSystemAssets.h"
#include "Components/StaticMeshComponent.h"
#include "MotionDelayBuffer.h"
#include "UObject/VRObjectVersion.h"
#include "UObject/UObjectGlobals.h" // for FindObject<>
#include "IXRTrackingSystem.h"
#include "IXRSystemAssets.h"
#include "SceneView.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"
#include "VRBaseCharacter.h"
#include "VRCharacter.h"
#include "VRRootComponent.h"
#include "VRGlobalSettings.h"
#include "Math/DualQuat.h"
#include "IIdentifiableXRDevice.h" // for FXRDeviceId
#include "XRMotionControllerBase.h" // for GetHandEnumForSourceName()
//#include "XRDeviceVisualizationComponent.h" // For visualization component

#include "Physics/Experimental/PhysScene_Chaos.h"

#include "GripScripts/GS_Default.h"
#include "GripScripts/GS_LerpToHand.h"

#include "PhysicsPublic.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConstraintDrives.h"
#include "PhysicsReplication.h"
#include "PhysicsEngine/PhysicsAsset.h"

#include "Chaos/ParticleHandle.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/PBDJointConstraintTypes.h"
#include "Chaos/PBDConstraintBaseData.h"
#include "Chaos/PBDJointConstraintData.h"
#include "Chaos/Sphere.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/ChaosConstraintSettings.h"
#include "Chaos/PhysicsObject.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "Chaos/PhysicsObjectInterface.h"

#include "Misc/CollisionIgnoreSubsystem.h"

#include "Features/IModularFeatures.h"

#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif

DEFINE_LOG_CATEGORY(LogVRMotionController);
//For UE4 Profiler ~ Stat
DECLARE_CYCLE_STAT(TEXT("TickGrip ~ TickingGrip"), STAT_TickGrip, STATGROUP_TickGrip);
DECLARE_CYCLE_STAT(TEXT("GetGripWorldTransform ~ GettingTransform"), STAT_GetGripTransform, STATGROUP_TickGrip);

// MAGIC NUMBERS
// Constraint multipliers for angular, to avoid having to have two sets of stiffness/damping variables
const float ANGULAR_STIFFNESS_MULTIPLIER = 1.5f;
const float ANGULAR_DAMPING_MULTIPLIER = 1.4f;
const float ANGULAR_STIFFNESS_MULTIPLIER_CHAOS = 0.45f;
const float ANGULAR_DAMPING_MULTIPLIER_CHAOS = 0.45f;

// Multiplier for the Interactive Hybrid With Physics grip - When not colliding increases stiffness by this value
const float HYBRID_PHYSICS_GRIP_MULTIPLIER = 10.0f;

namespace {
	/** This is to prevent destruction of motion controller components while they are
	in the middle of being accessed by the render thread */
	FCriticalSection CritSect;

} // anonymous namespace

namespace GripUEMotionController {
	// A scoped lock that must be explicitly locked and will unlock upon destruction if locked.
	// Convenient if you only sometimes want to lock and the scopes are complicated.
	class FScopeLockOptional
	{
	public:
		FScopeLockOptional()
		{
		}

		void Lock(FCriticalSection* InSynchObject)
		{
			SynchObject = InSynchObject;
			SynchObject->Lock();
		}

		/** Destructor that performs a release on the synchronization object. */
		~FScopeLockOptional()
		{
			Unlock();
		}

		void Unlock()
		{
			if (SynchObject)
			{
				SynchObject->Unlock();
				SynchObject = nullptr;
			}
		}

	private:
		/** Copy constructor( hidden on purpose). */
		FScopeLockOptional(const FScopeLockOptional& InScopeLock);

		/** Assignment operator (hidden on purpose). */
		FScopeLockOptional& operator=(FScopeLockOptional& InScopeLock)
		{
			return *this;
		}

	private:

		// Holds the synchronization object to aggregate and scope manage.
		FCriticalSection* SynchObject = nullptr;
	};
}

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
	MotionSource = IMotionController::LeftHandSourceId;
	//Hand = EControllerHand::Left;
	bDisableLowLatencyUpdate = false;
	bHasAuthority = false;
	bIgnoreTrackingStatus = false;
	bUseWithoutTracking = false;
	ClientAuthConflictResolutionMethod = EVRClientAuthConflictResolutionMode::VRGRIP_CONFLICT_First;
	bAlwaysSendTickGrip = false;
	bAutoActivate = true;

	SetIsReplicatedByDefault(true);

	// Epic never initializes this variable, so I need to
	CurrentTrackingStatus = ETrackingStatus::NotTracked;

	// Default 100 htz update rate, same as the 100htz update rate of rep_notify, will be capped to 90/45 though because of vsync on HMD
	//bReplicateControllerTransform = true;
	ControllerNetUpdateRate = 100.0f; // 100 htz is default
	ControllerNetUpdateCount = 0.0f;
	bReplicateWithoutTracking = false;
	bLerpingPosition = false;
	bSmoothReplicatedMotion = false;
	bReppedOnce = false;
	bScaleTracking = false;
	TrackingScaler = FVector(1.0f);
	bLimitMinHeight = false;
	MinimumHeight = 0.0f;
	bLimitMaxHeight = false;
	MaximumHeight = 240.0f;
	//bOffsetByHMD = false;
	bLeashToHMD = false;
	LeashRange = 300.0f;
	bConstrainToPivot = false;

	bSmoothHandTracking = false;
	bWasSmoothingHand = false;
	bSmoothWithEuroLowPassFunction = true;
	LastSmoothRelativeTransform = FTransform::Identity;
	SmoothingSpeed = 20.0f;
	EuroSmoothingParams.MinCutoff = 0.1f;
	EuroSmoothingParams.DeltaCutoff = 10.f;
	EuroSmoothingParams.CutoffSlope = 10.f;

	bIsPostTeleport = false;

	GripIDIncrementer = INVALID_VRGRIP_ID;

	// Pivot Variables
	CustomPivotComponentSocketName = NAME_None;
	bSkipPivotTransformAdjustment = false;

	bOffsetByControllerProfile = true;
	CurrentControllerProfileTransform = FTransform::Identity;

	DefaultGripScript = nullptr;
	DefaultGripScriptClass = UGS_Default::StaticClass();
	//DisplayComponentReference = nullptr;

	VelocityCalculationType = EVRVelocityType::VRLOCITY_Default;
	LastRelativePosition = FTransform::Identity;
	bSampleVelocityInWorldSpace = false;
	VelocitySamples = 30.f;

	bProjectNonSimulatingGrips = false;
	EndPhysicsTickFunction.TickGroup = TG_EndPhysics;
	EndPhysicsTickFunction.bCanEverTick = true;
	EndPhysicsTickFunction.bStartWithTickEnabled = false;
}

void UGripMotionControllerComponent::RegisterEndPhysicsTick(bool bRegister)
{
	if (bRegister != EndPhysicsTickFunction.IsTickFunctionRegistered())
	{
		if (bRegister)
		{
			if (SetupActorComponentTickFunction(&EndPhysicsTickFunction))
			{
				EndPhysicsTickFunction.Target = this;
				// Make sure our EndPhysicsTick gets called after physics simulation is finished
				UWorld* World = GetWorld();
				if (World != nullptr)
				{
					EndPhysicsTickFunction.AddPrerequisite(World, World->EndPhysicsTickFunction);
				}
			}
		}
		else
		{
			EndPhysicsTickFunction.UnRegisterTickFunction();
		}
	}
}

void UGripMotionControllerComponent::EndPhysicsTickComponent(FGripComponentEndPhysicsTickFunction& ThisTickFunction)
{

	if (!IsValidChecked(this))
		return;

	// Now check if we should turn off any post physics ticking
	FTransform baseTrans = this->GetAttachParent()->GetComponentTransform().Inverse();

	for (int i = 0; i < LocallyGrippedObjects.Num(); ++i)
	{
		if (!LocallyGrippedObjects[i].GrippedObject || !IsValid(LocallyGrippedObjects[i].GrippedObject))
			continue; // Skip, don't process this

		if (LocallyGrippedObjects[i].GrippedObject && IsValid(LocallyGrippedObjects[i].GrippedObject) && LocallyGrippedObjects[i].GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			bool bSampleRelativeTransform = bProjectNonSimulatingGrips;

			if (!bSampleRelativeTransform)
			{
				EGripInterfaceTeleportBehavior TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(LocallyGrippedObjects[i].GrippedObject);
				bSampleRelativeTransform = TeleportBehavior == EGripInterfaceTeleportBehavior::DeltaTeleportation;
			}

			if (bSampleRelativeTransform)
			{
				switch(LocallyGrippedObjects[i].GripTargetType)
				{
					case EGripTargetType::ActorGrip:
					{
						if (AActor* Actor = Cast<AActor>(LocallyGrippedObjects[i].GrippedObject))
						{
							if (UPrimitiveComponent* root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
							{
								LocallyGrippedObjects[i].LastWorldTransform = root->GetComponentTransform() * baseTrans;
								LocallyGrippedObjects[i].bSetLastWorldTransform = true;
							}
						}
					}break;
					case EGripTargetType::ComponentGrip:
					{
						if (UPrimitiveComponent* root = Cast<UPrimitiveComponent>(LocallyGrippedObjects[i].GrippedObject))
						{
							LocallyGrippedObjects[i].LastWorldTransform = root->GetComponentTransform() * baseTrans;
							LocallyGrippedObjects[i].bSetLastWorldTransform = true;
						}
					}break;
				}			
			}
		}
	}

	for (int i = 0; i < GrippedObjects.Num(); ++i)
	{
		if (!GrippedObjects[i].GrippedObject || !IsValid(GrippedObjects[i].GrippedObject))
			continue; // Skip, don't process this

		if (GrippedObjects[i].GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			bool bSampleRelativeTransform = bProjectNonSimulatingGrips;

			if (!bSampleRelativeTransform)
			{
				EGripInterfaceTeleportBehavior TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(GrippedObjects[i].GrippedObject);
				bSampleRelativeTransform = TeleportBehavior == EGripInterfaceTeleportBehavior::DeltaTeleportation;
			}

			if (bSampleRelativeTransform)
			{
				switch (GrippedObjects[i].GripTargetType)
				{
				case EGripTargetType::ActorGrip:
				{
					if (AActor* Actor = Cast<AActor>(GrippedObjects[i].GrippedObject))
					{
						if (UPrimitiveComponent* root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
						{
							GrippedObjects[i].LastWorldTransform = root->GetComponentTransform() * baseTrans;
							GrippedObjects[i].bSetLastWorldTransform = true;
						}
					}
				}break;
				case EGripTargetType::ComponentGrip:
				{
					if (UPrimitiveComponent* root = Cast<UPrimitiveComponent>(GrippedObjects[i].GrippedObject))
					{
						GrippedObjects[i].LastWorldTransform = root->GetComponentTransform() * baseTrans;
						GrippedObjects[i].bSetLastWorldTransform = true;
					}
				}break;
				}
			}
		}
	}
}

void FGripComponentEndPhysicsTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	QUICK_SCOPE_CYCLE_COUNTER(FGripComponentEndPhysicsTickFunction_ExecuteTick);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Physics);

	if (Target && IsValid(Target))
	{
		FActorComponentTickFunction::ExecuteTickHelper(Target, /*bTickInEditor=*/ false, DeltaTime, TickType, [this](float DilatedTime)
		{
			Target->EndPhysicsTickComponent(*this);
		});
	}
}

FString FGripComponentEndPhysicsTickFunction::DiagnosticMessage()
{
	return TEXT("GripComponentEndPhysicsTickFunction");
}

FName FGripComponentEndPhysicsTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("GripComponentEndPhysicsTick"));
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
		UVRGlobalSettings* VRSettings = GetMutableDefault<UVRGlobalSettings>();

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

void UGripMotionControllerComponent::InitializeComponent()
{
	Super::InitializeComponent();

	if (!DefaultGripScript && DefaultGripScriptClass)
		DefaultGripScript = NewObject<UGS_Default>(this, DefaultGripScriptClass); //DefaultGripScriptClass.GetDefaultObject();
	else
		DefaultGripScript = NewObject<UGS_Default>(this, UGS_Default::StaticClass());
}

void UGripMotionControllerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// Cancel end physics tick
	RegisterEndPhysicsTick(false);

	if (NewControllerProfileEvent_Handle.IsValid())
	{
		UVRGlobalSettings* VRSettings = GetMutableDefault<UVRGlobalSettings>();
		if (VRSettings != nullptr)
		{
			VRSettings->OnControllerProfileChangedEvent.Remove(NewControllerProfileEvent_Handle);
			NewControllerProfileEvent_Handle.Reset();
		}
	}

	for (int i = 0; i < GrippedObjects.Num(); i++)
	{
		DestroyPhysicsHandle(GrippedObjects[i]);

		if (/*HasGripAuthority(GrippedObjects[i]) || */IsServer())
		{
			DropObjectByInterface(nullptr, GrippedObjects[i].GripID);
		}
		else
		{
			if (IsValid(GrippedObjects[i].GrippedObject))
			{
				bool bSimulateOnDrop = true;
				if (GrippedObjects[i].GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					bSimulateOnDrop = IVRGripInterface::Execute_SimulateOnDrop(GrippedObjects[i].GrippedObject);
				}

				NotifyDrop(GrippedObjects[i], bSimulateOnDrop);
			}
		}
	}
	GrippedObjects.Empty();

	for (int i = 0; i < LocallyGrippedObjects.Num(); i++)
	{
		DestroyPhysicsHandle(LocallyGrippedObjects[i]);

		if (/*HasGripAuthority(LocallyGrippedObjects[i]) || */IsServer())
		{
			DropObjectByInterface(nullptr, LocallyGrippedObjects[i].GripID);
		}
		else
		{
			if (IsValid(LocallyGrippedObjects[i].GrippedObject))
			{
				bool bSimulateOnDrop = true;
				if (LocallyGrippedObjects[i].GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					bSimulateOnDrop = IVRGripInterface::Execute_SimulateOnDrop(LocallyGrippedObjects[i].GrippedObject);
				}

				NotifyDrop(LocallyGrippedObjects[i], bSimulateOnDrop);
			}
		}
	}
	LocallyGrippedObjects.Empty();

	for (int i = 0; i < PhysicsGrips.Num(); i++)
	{
		DestroyPhysicsHandle(&PhysicsGrips[i]);
	}
	PhysicsGrips.Empty();

	// Clear any timers that we are managing
	if (UWorld * myWorld = GetWorld())
	{
		myWorld->GetTimerManager().ClearAllTimersForObject(this);
	}

	ObjectsWaitingForSocketUpdate.Empty();
}

void UGripMotionControllerComponent::OnUnregister()
{
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

void UGripMotionControllerComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{	

	// Don't bother updating this stuff if we aren't local or using them
	if (bHasAuthority && !bDisableLowLatencyUpdate && IsActive())
	{
		LateUpdateParams.GripRenderThreadRelativeTransform = GetRelativeTransform();
		LateUpdateParams.GripRenderThreadComponentScale = GetComponentScale();
		LateUpdateParams.GripRenderThreadProfileTransform = CurrentControllerProfileTransform;
		LateUpdateParams.GripRenderThreadLastLocationForLateUpdate = LastLocationForLateUpdate;

		LateUpdateParams.bRenderSmoothHandTracking = bSmoothHandTracking;
		if (LateUpdateParams.bRenderSmoothHandTracking)
		{
			if (UWorld* world = GetWorld())
			{
				LateUpdateParams.RenderLastDeltaTime = world->GetDeltaSeconds();
			}

			LateUpdateParams.bRenderSmoothWithEuroLowPassFunction = bSmoothWithEuroLowPassFunction;
			
			if (LateUpdateParams.bRenderSmoothWithEuroLowPassFunction)
			{
				LateUpdateParams.RenderEuroSmoothingParams = EuroSmoothingParams;
			}
			else
			{
				LateUpdateParams.RenderSmoothingSpeed = SmoothingSpeed;
				LateUpdateParams.RenderLastSmoothRelativeTransform = LastSmoothRelativeTransform;
			}
		}
	}

	Super::Super::CreateRenderState_Concurrent(Context);
}

void UGripMotionControllerComponent::SendRenderTransform_Concurrent()
{
	// Don't bother updating this stuff if we aren't local or using them
	if (bHasAuthority && !bDisableLowLatencyUpdate && IsActive())
	{
		struct FPrimitiveUpdateRenderThreadRelativeTransformParams
		{
			FRenderTrackingParams LateUpdateParams;
		};

		FPrimitiveUpdateRenderThreadRelativeTransformParams UpdateParams;
		UpdateParams.LateUpdateParams.GripRenderThreadRelativeTransform = GetRelativeTransform();
		UpdateParams.LateUpdateParams.GripRenderThreadComponentScale = GetComponentScale();
		UpdateParams.LateUpdateParams.GripRenderThreadProfileTransform = CurrentControllerProfileTransform;
		UpdateParams.LateUpdateParams.GripRenderThreadLastLocationForLateUpdate = LastLocationForLateUpdate;

		UpdateParams.LateUpdateParams.bRenderSmoothHandTracking = bSmoothHandTracking;
		if (UpdateParams.LateUpdateParams.bRenderSmoothHandTracking)
		{
			if (UWorld* world = GetWorld())
			{
				UpdateParams.LateUpdateParams.RenderLastDeltaTime = world->GetDeltaSeconds();
			}

			UpdateParams.LateUpdateParams.bRenderSmoothWithEuroLowPassFunction = bSmoothWithEuroLowPassFunction;

			if (UpdateParams.LateUpdateParams.bRenderSmoothWithEuroLowPassFunction)
			{
				UpdateParams.LateUpdateParams.RenderEuroSmoothingParams = EuroSmoothingParams;
			}
			else
			{
				UpdateParams.LateUpdateParams.RenderSmoothingSpeed = SmoothingSpeed;
				UpdateParams.LateUpdateParams.RenderLastSmoothRelativeTransform = LastSmoothRelativeTransform;
			}
		}

		ENQUEUE_RENDER_COMMAND(UpdateRTRelativeTransformCommand)(
			[UpdateParams, this](FRHICommandListImmediate& RHICmdList)
			{
				LateUpdateParams = UpdateParams.LateUpdateParams;
			});
	}

	// Skip bases motion controllers implementation, we don't want to double update to the render thread
	Super::Super::SendRenderTransform_Concurrent();
}

FBPActorPhysicsHandleInformation * UGripMotionControllerComponent::GetPhysicsGrip(const FBPActorGripInformation & GripInfo)
{
	return PhysicsGrips.FindByKey(GripInfo);
}

FBPActorPhysicsHandleInformation* UGripMotionControllerComponent::GetPhysicsGrip(const uint8 GripID)
{
	return PhysicsGrips.FindByKey(GripID);
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
		DestroyPhysicsHandle(HandleInfo);
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
	 
	 // Don't ever replicate these, they are getting replaced by my custom send anyway
	 DISABLE_REPLICATED_PRIVATE_PROPERTY(USceneComponent, RelativeLocation);
	 DISABLE_REPLICATED_PRIVATE_PROPERTY(USceneComponent, RelativeRotation);
	 DISABLE_REPLICATED_PRIVATE_PROPERTY(USceneComponent, RelativeScale3D);

	// For std properties
	FDoRepLifetimeParams PushModelParams{ COND_None, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UGripMotionControllerComponent, GrippedObjects, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGripMotionControllerComponent, ControllerNetUpdateRate, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGripMotionControllerComponent, bSmoothReplicatedMotion, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGripMotionControllerComponent, bReplicateWithoutTracking, PushModelParams);

	// For properties with special conditions
	FDoRepLifetimeParams PushModelParamsWithCondition{ COND_SkipOwner, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };
	
	// Skipping the owner with this as the owner will use the controllers location directly
	DOREPLIFETIME_WITH_PARAMS_FAST(UGripMotionControllerComponent, ReplicatedControllerTransform, PushModelParamsWithCondition);
	DOREPLIFETIME_WITH_PARAMS_FAST(UGripMotionControllerComponent, LocallyGrippedObjects, PushModelParamsWithCondition);

	// For properties with special conditions
	FDoRepLifetimeParams PushModelParamsWithConditionOwnerOnly{ COND_OwnerOnly, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UGripMotionControllerComponent, LocalTransactionBuffer, PushModelParamsWithConditionOwnerOnly);
//	DOREPLIFETIME(UGripMotionControllerComponent, bReplicateControllerTransform);
}

/*void UGripMotionControllerComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't ever replicate these, they are getting replaced by my custom send anyway
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, false);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, false);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, false);
}*/

void UGripMotionControllerComponent::Server_SendControllerTransform_Implementation(FBPVRComponentPosRep NewTransform)
{
	// Store new transform and trigger OnRep_Function
	ReplicatedControllerTransform = NewTransform;
#if WITH_PUSH_MODEL
		MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, ReplicatedControllerTransform, this);
#endif

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

void UGripMotionControllerComponent::GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &CurAngularVelocity, FVector &CurLinearVelocity)
{
	UPrimitiveComponent * primComp = Grip.GetGrippedComponent();//Grip.Component;
	AActor * pActor = Grip.GetGrippedActor();

	if (!primComp && pActor)
		primComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if (!primComp)
	{
		CurAngularVelocity = FVector::ZeroVector;
		CurLinearVelocity = FVector::ZeroVector;
		return;
	}

	// TMP #TODO: Fix when 5.4 bug is fixed
	if (!primComp->IsSimulatingPhysics())
	{
		CurLinearVelocity = Grip.LinVel;
		CurAngularVelocity = Grip.RotVel;
		return;
	}

	CurAngularVelocity = primComp->GetPhysicsAngularVelocityInDegrees();
	CurLinearVelocity = primComp->GetPhysicsLinearVelocity();
}

bool UGripMotionControllerComponent::GetPhysicsConstraintForce(const FBPActorGripInformation& Grip, FVector& AngularForce, FVector& LinearForce)
{
	if (FBPActorPhysicsHandleInformation * PhysHandle = GetPhysicsGrip(Grip.GripID))
	{
		if (PhysHandle->HandleData2.IsValid())
		{
			FPhysicsInterface::GetForce(PhysHandle->HandleData2, LinearForce, AngularForce);
			return true;
		}
	}

	return false;
}

void UGripMotionControllerComponent::GetGripMass(const FBPActorGripInformation& Grip, float& Mass)
{
	UPrimitiveComponent* primComp = Grip.GetGrippedComponent();//Grip.Component;
	AActor* pActor = Grip.GetGrippedActor();

	if (!primComp && pActor)
		primComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if (!primComp || !primComp->IsSimulatingPhysics())
	{
		Mass = 0.f;
		return;
	}

	Mass = primComp->GetMass();
}

FTransform UGripMotionControllerComponent::GetGrippedObjectTransform(const FBPActorGripInformation& Grip)
{
	FTransform returnTrans = FTransform::Identity;

	if (!IsValid(Grip.GrippedObject))
	{
		return returnTrans;
	}

	if (Grip.GripTargetType == EGripTargetType::ActorGrip)
	{
		if (AActor* GrippedActor = Cast<AActor>(Grip.GrippedObject))
		{
			returnTrans = GrippedActor->GetActorTransform();
		}
	}
	else
	{
		if (UPrimitiveComponent* GrippedComp = Cast<UPrimitiveComponent>(Grip.GrippedObject))
		{
			returnTrans = GrippedComp->GetComponentTransform();
		}
	}

	return returnTrans;
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

FBPActorGripInformation * UGripMotionControllerComponent::GetGripPtrByID(uint8 IDToLookForGrip)
{
	if (IDToLookForGrip == INVALID_VRGRIP_ID)
	{
		return nullptr;
	}

	FBPActorGripInformation* GripInfo = GrippedObjects.FindByKey(IDToLookForGrip);
	if (!GripInfo)
		GripInfo = LocallyGrippedObjects.FindByKey(IDToLookForGrip);

	return GripInfo;
}

void UGripMotionControllerComponent::GetGripByID(FBPActorGripInformation &Grip, uint8 IDToLookForGrip, EBPVRResultSwitch &Result)
{
	if (IDToLookForGrip == INVALID_VRGRIP_ID)
	{
		Result = EBPVRResultSwitch::OnFailed;
		return;
	}

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

void UGripMotionControllerComponent::SetGripHybridLock(const FBPActorGripInformation& Grip, EBPVRResultSwitch& Result, bool bIsLocked)
{
	int fIndex = GrippedObjects.Find(Grip);

	FBPActorGripInformation* GripInformation = nullptr;

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
		GripInformation->bLockHybridGrip = bIsLocked;
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
		DIRTY_GRIPPED_OBJECTS();
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].GripCollisionType = NewGripCollisionType;

			if (IsLocallyControlled() && !IsServer() && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			{
				FBPActorGripInformation GripInfo = LocallyGrippedObjects[fIndex];
				Server_NotifyLocalGripAddedOrChanged(GripInfo);
			}

			ReCreateGrip(LocallyGrippedObjects[fIndex]);
			DIRTY_LOCALLY_GRIPPED_OBJECTS();

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
		DIRTY_GRIPPED_OBJECTS();
		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].GripLateUpdateSetting = NewGripLateUpdateSetting;

			if (IsLocallyControlled() && !IsServer() && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			{
				FBPActorGripInformation GripInfo = LocallyGrippedObjects[fIndex];
				Server_NotifyLocalGripAddedOrChanged(GripInfo);
			}

			DIRTY_LOCALLY_GRIPPED_OBJECTS();
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
		if (FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(Grip))
		{
			UpdatePhysicsHandle(Grip.GripID, true);
			NotifyGripTransformChanged(Grip);
		}

		DIRTY_GRIPPED_OBJECTS();

		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].RelativeTransform = NewRelativeTransform;
			if (FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(Grip))
			{
				UpdatePhysicsHandle(Grip.GripID, true);
				NotifyGripTransformChanged(Grip);
			}

			if (IsLocallyControlled() && !IsServer() && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			{
				FBPActorGripInformation GripInfo = LocallyGrippedObjects[fIndex];
				Server_NotifyLocalGripAddedOrChanged(GripInfo);
			}

			DIRTY_LOCALLY_GRIPPED_OBJECTS();

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
		DIRTY_GRIPPED_OBJECTS();

		Result = EBPVRResultSwitch::OnSucceeded;
		return;
	}
	else
	{
		fIndex = LocallyGrippedObjects.Find(Grip);

		if (fIndex != INDEX_NONE)
		{
			LocallyGrippedObjects[fIndex].AdditionTransform = CreateGripRelativeAdditionTransform(Grip, NewAdditionTransform, bMakeGripRelative);
			DIRTY_LOCALLY_GRIPPED_OBJECTS();

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

		DIRTY_GRIPPED_OBJECTS();

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

			if (IsLocallyControlled() && !IsServer() && !IsTornOff() && LocallyGrippedObjects[fIndex].GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			{
				FBPActorGripInformation GripInfo = LocallyGrippedObjects[fIndex];
				Server_NotifyLocalGripAddedOrChanged(GripInfo);
			}

			DIRTY_LOCALLY_GRIPPED_OBJECTS();

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

	// Skip if we are traveling
	if (IsTravelingOrNullWorld())
		return false;

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
	UObject* ObjectToDrop,
	uint8 GripIDToDrop,
	bool bSimulate,
	FVector OptionalAngularVelocity,
	FVector OptionalLinearVelocity)
{
	if (IsValid(ObjectToDrop))
	{
		FBPActorGripInformation * GripInfo = GrippedObjects.FindByKey(ObjectToDrop);
		if (!GripInfo)
			GripInfo = LocallyGrippedObjects.FindByKey(ObjectToDrop);

		if (GripInfo != nullptr && IsValid(GripInfo->GrippedObject))
		{
			return DropGrip_Implementation(*GripInfo, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}
	else if (GripIDToDrop != INVALID_VRGRIP_ID)
	{
		FBPActorGripInformation * GripInfo = GrippedObjects.FindByKey(GripIDToDrop);
		if (!GripInfo)
			GripInfo = LocallyGrippedObjects.FindByKey(GripIDToDrop);

		if (GripInfo != nullptr && IsValid(GripInfo->GrippedObject))
		{
			return DropGrip_Implementation(*GripInfo, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::GripObjectByInterface(UObject* ObjectToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative, FName OptionalBoneToGripName, FName OptionalSnapToSocketName, bool bIsSlotGrip)
{
	// Skip if we are traveling
	if (IsTravelingOrNullWorld())
		return false;

	if (!IsValid(ObjectToGrip))
	{
		return false;
	}

	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ObjectToGrip))
	{
		AActor * Owner = PrimComp->GetOwner();

		if (!IsValid(Owner))
			return false;

		if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType = IVRGripInterface::Execute_GetPrimaryGripType(PrimComp, bIsSlotGrip);

			float Stiffness;
			float Damping;
			IVRGripInterface::Execute_GetGripStiffnessAndDamping(PrimComp, Stiffness, Damping);

			return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName,
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

			return GripActor(Owner, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(Owner),
				IVRGripInterface::Execute_GripMovementReplicationType(Owner),
				Stiffness,
				Damping,
				bIsSlotGrip
			);
			/*return GripComponent(PrimComp, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(Owner),
				IVRGripInterface::Execute_GripMovementReplicationType(Owner),
				Stiffness,
				Damping,
				bIsSlotGrip
				);*/
		}
		else
		{
			// No interface, no grip
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController GripObjectByInterface was called on an object that doesn't implement the interface and doesn't have a parent that implements the interface!"));
			return false;
		}
	}
	else if (AActor * Actor = Cast<AActor>(ObjectToGrip))
	{
		UPrimitiveComponent * root = Cast<UPrimitiveComponent>(Actor->GetRootComponent());

		if (!IsValid(root))
			return false;

		if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType = IVRGripInterface::Execute_GetPrimaryGripType(root, bIsSlotGrip);

			float Stiffness;
			float Damping;
			IVRGripInterface::Execute_GetGripStiffnessAndDamping(root, Stiffness, Damping);

			return GripComponent(root, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(root),
				IVRGripInterface::Execute_GripMovementReplicationType(root),
				Stiffness,
				Damping,
				bIsSlotGrip
				);
			/*return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName,
				OptionalBoneToGripName,
				CollisionType,
				IVRGripInterface::Execute_GripLateUpdateSetting(root),
				IVRGripInterface::Execute_GripMovementReplicationType(root),
				Stiffness,
				Damping,
				bIsSlotGrip
				);*/
		}
		else if (Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			EGripCollisionType CollisionType = IVRGripInterface::Execute_GetPrimaryGripType(Actor, bIsSlotGrip);

			float Stiffness;
			float Damping;
			IVRGripInterface::Execute_GetGripStiffnessAndDamping(Actor, Stiffness, Damping);

			return GripActor(Actor, WorldOffset, bWorldOffsetIsRelative, OptionalSnapToSocketName,
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
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController GripObjectByInterface was called on an object that doesn't implement the interface and doesn't have a parent that implements the interface!"));
			return false;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::DropObjectByInterface(UObject* ObjectToDrop, uint8 GripIDToDrop, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	return DropObjectByInterface_Implementation(ObjectToDrop, GripIDToDrop, OptionalAngularVelocity, OptionalLinearVelocity, false);
}

bool UGripMotionControllerComponent::DropObjectByInterface_Implementation(UObject* ObjectToDrop, uint8 GripIDToDrop, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity, bool bSkipNotify)
{

	FBPActorGripInformation * GripInfo = nullptr;
	if (IsValid(ObjectToDrop))
	{
		GripInfo = GrippedObjects.FindByKey(ObjectToDrop);
		if (!GripInfo)
			GripInfo = LocallyGrippedObjects.FindByKey(ObjectToDrop);
	}
	else if (GripIDToDrop != INVALID_VRGRIP_ID)
	{
		GripInfo = GrippedObjects.FindByKey(GripIDToDrop);
		if (!GripInfo)
			GripInfo = LocallyGrippedObjects.FindByKey(GripIDToDrop);
	}

	if (GripInfo == nullptr || !IsValid(GripInfo->GrippedObject))
	{
		return false;
	}

	if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(GripInfo->GrippedObject))
	{
		AActor * Owner = PrimComp->GetOwner();

		if (!Owner)
			return false;

		if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropGrip_Implementation(*GripInfo, IVRGripInterface::Execute_SimulateOnDrop(PrimComp), OptionalAngularVelocity, OptionalLinearVelocity, bSkipNotify);
			//return DropComponent(PrimComp, IVRGripInterface::Execute_SimulateOnDrop(PrimComp), OptionalAngularVelocity, OptionalLinearVelocity);
		}
		else if (Owner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropGrip_Implementation(*GripInfo, IVRGripInterface::Execute_SimulateOnDrop(Owner), OptionalAngularVelocity, OptionalLinearVelocity, bSkipNotify);
			//return DropComponent(PrimComp, IVRGripInterface::Execute_SimulateOnDrop(Owner), OptionalAngularVelocity, OptionalLinearVelocity);
		}
		else
		{
			// Allowing for failsafe dropping here.
			return DropGrip_Implementation(*GripInfo, true, OptionalAngularVelocity, OptionalLinearVelocity, bSkipNotify);
			//return DropComponent(PrimComp, true, OptionalAngularVelocity, OptionalLinearVelocity);
		}
	}
	else if (AActor * Actor = Cast<AActor>(GripInfo->GrippedObject))
	{
		UPrimitiveComponent * root = Cast<UPrimitiveComponent>(Actor->GetRootComponent());

		if (!IsValid(root))
			return false;

		if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropGrip_Implementation(*GripInfo, IVRGripInterface::Execute_SimulateOnDrop(root), OptionalAngularVelocity, OptionalLinearVelocity, bSkipNotify);
		}
		else if (Actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			return DropGrip_Implementation(*GripInfo, IVRGripInterface::Execute_SimulateOnDrop(Actor), OptionalAngularVelocity, OptionalLinearVelocity, bSkipNotify);
		}
		else
		{
			// Failsafe drop here
			return DropGrip_Implementation(*GripInfo, true, OptionalAngularVelocity, OptionalLinearVelocity, bSkipNotify);
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
	// Skip if we are traveling
	if (IsTravelingOrNullWorld())
		return false;

	bool bIsLocalGrip = (GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive || GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep);

	if (!IsServer() && !bIsLocalGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was called on the client side as a replicated grip"));
		return false;
	}

	if (!ActorToGrip || !IsValid(ActorToGrip))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an invalid or pending kill actor"));
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
	if (root->Mobility != EComponentMobility::Movable && (GripCollisionType != EGripCollisionType::CustomGrip && GripCollisionType != EGripCollisionType::EventsOnly))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController tried to grip an actor set to static mobility not with a Custom Grip"));
		return false; // It is not movable, can't grip it
	}

	FBPAdvGripSettings AdvancedGripSettings;
	UObject * ObjectToCheck = NULL; // Used if having to calculate the transform
	//bool bIgnoreHandRotation = false;

	TArray<FBPGripPair> HoldingControllers;
	bool bIsHeld = false;
	bool bHadOriginalSettings = false;
	bool bOriginalGravity = false;
	bool bOriginalReplication = false;

	if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(root, this))
			return false; // Interface is saying not to grip it right now

		IVRGripInterface::Execute_IsHeld(root, HoldingControllers, bIsHeld);
		bool bAllowMultipleGrips = IVRGripInterface::Execute_AllowsMultipleGrips(root);
		if (bIsHeld && !bAllowMultipleGrips)
		{
			return false; // Can't multiple grip this object
		}
		else if (bIsHeld)
		{
			// If we are held by multiple controllers then lets copy our original values from the first one	
			if (HoldingControllers[0].HoldingController != nullptr)
			{
				FBPActorGripInformation* gripInfo = HoldingControllers[0].HoldingController->GetGripPtrByID(HoldingControllers[0].GripID);

				if (gripInfo != nullptr)
				{
					bHadOriginalSettings = true;
					bOriginalGravity = gripInfo->bOriginalGravity;
					bOriginalReplication = gripInfo->bOriginalReplicatesMovement;
				}
			}
		}

		AdvancedGripSettings = IVRGripInterface::Execute_AdvancedGripSettings(root);
		ObjectToCheck = root;
	}
	else if (ActorToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ActorToGrip, this))
			return false; // Interface is saying not to grip it right now

		IVRGripInterface::Execute_IsHeld(ActorToGrip, HoldingControllers, bIsHeld);
		bool bAllowMultipleGrips = IVRGripInterface::Execute_AllowsMultipleGrips(ActorToGrip);
		if (bIsHeld && !bAllowMultipleGrips)
		{
			return false; // Can't multiple grip this object
		}
		else if (bIsHeld)
		{
			// If we are held by multiple controllers then lets copy our original values from the first one	
			if (HoldingControllers[0].HoldingController != nullptr)
			{
				FBPActorGripInformation* gripInfo = HoldingControllers[0].HoldingController->GetGripPtrByID(HoldingControllers[0].GripID);

				if (gripInfo != nullptr)
				{
					bHadOriginalSettings = true;
					bOriginalGravity = gripInfo->bOriginalGravity;
					bOriginalReplication = gripInfo->bOriginalReplicatesMovement;
				}
			}
		}

		AdvancedGripSettings = IVRGripInterface::Execute_AdvancedGripSettings(ActorToGrip);
		ObjectToCheck = ActorToGrip;
	}

	// So that events caused by sweep and the like will trigger correctly
	ActorToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripID = GetNextGripID(bIsLocalGrip);
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.GrippedObject = ActorToGrip;
	if (bHadOriginalSettings)
	{
		newActorGrip.bOriginalReplicatesMovement = bOriginalReplication;
		newActorGrip.bOriginalGravity = bOriginalGravity;
	}
	else
	{
		newActorGrip.bOriginalReplicatesMovement = ActorToGrip->IsReplicatingMovement();
		newActorGrip.bOriginalGravity = root->IsGravityEnabled();
	}
	newActorGrip.Stiffness = GripStiffness;
	newActorGrip.Damping = GripDamping;
	newActorGrip.AdvancedGripSettings = AdvancedGripSettings;
	newActorGrip.ValueCache.bWasInitiallyRepped = true; // Set this true on authority side so we can skip a function call on tick
	newActorGrip.bIsSlotGrip = bIsSlotGrip;
	newActorGrip.GrippedBoneName = OptionalBoneToGripName;
	newActorGrip.SlotName = OptionalSnapToSocketName;

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
		if (ActorToGrip->IsReplicatingMovement())
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

	if (OptionalSnapToSocketName.IsValid() && WorldOffset.Equals(FTransform::Identity) && root->DoesSocketExist(OptionalSnapToSocketName))
	{
		// I inverse it so that laying out the sockets makes sense
		FTransform sockTrans = root->GetSocketTransform(OptionalSnapToSocketName, ERelativeTransformSpace::RTS_Component);
		sockTrans.SetScale3D(FVector(1.f) / root->GetComponentScale()); // Prep this so that the inverse works correctly
		newActorGrip.RelativeTransform = sockTrans.Inverse();
		newActorGrip.bIsSlotGrip = true; // Set this to a slot grip

		ObjectToCheck = NULL; // Null it back out, socketed grips don't use this

		newActorGrip.SlotName = OptionalSnapToSocketName;
	}
	else if (bWorldOffsetIsRelative)
	{
		if (bSkipPivotTransformAdjustment && IsValid(CustomPivotComponent) && !bIsSlotGrip)
		{
			newActorGrip.RelativeTransform = (WorldOffset * this->GetComponentTransform()).GetRelativeTransform(CustomPivotComponent->GetComponentTransform());
		}
		else
		{
			newActorGrip.RelativeTransform = WorldOffset;
		}
	}
	else
	{
		newActorGrip.RelativeTransform = WorldOffset.GetRelativeTransform(GetPivotTransform());
	}

	if (!bIsLocalGrip)
	{
		int32 Index = GrippedObjects.Add(newActorGrip);
		DIRTY_GRIPPED_OBJECTS();

		if (Index != INDEX_NONE)
			NotifyGrip(GrippedObjects[Index]);
		//NotifyGrip(newActorGrip);
	}
	else
	{
		if (!IsLocallyControlled())
		{
			LocalTransactionBuffer.Add(newActorGrip);
#if WITH_PUSH_MODEL
			MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, LocalTransactionBuffer, this);
#endif
		}

		int32 Index = LocallyGrippedObjects.Add(newActorGrip);
		DIRTY_LOCALLY_GRIPPED_OBJECTS();

		if (Index != INDEX_NONE)
		{
			if (!IsLocallyControlled())
			{
				if (!HandleGripReplication(LocallyGrippedObjects[Index]))
				{
					return true;
				}
			}
			else
			{
				if (!NotifyGrip(LocallyGrippedObjects[Index]))
				{
					return true;
				}
			}

			if (bIsLocalGrip && IsLocallyControlled() && !IsServer() && !IsTornOff() && newActorGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			{
				Index = LocallyGrippedObjects.IndexOfByKey(newActorGrip.GripID);
				if (Index != INDEX_NONE)
				{
					FBPActorGripInformation GripInfo = LocallyGrippedObjects[Index];
					Server_NotifyLocalGripAddedOrChanged(GripInfo);
				}
			}
		}
	}
	//NotifyGrip(newActorGrip);

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
		return DropGrip_Implementation(*GripToDrop, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);

	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side with a replicated grip"));
		return false;
	}

	GripToDrop = GrippedObjects.FindByKey(ActorToDrop);
	if (GripToDrop)
		return DropGrip_Implementation(*GripToDrop, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);

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
	// Skip if we are traveling
	if (IsTravelingOrNullWorld())
		return false;

	bool bIsLocalGrip = (GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive || GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep);

	if (!IsServer() && !bIsLocalGrip)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was called on the client side with a replicating grip"));
		return false;
	}

	if (!ComponentToGrip || !IsValid(ComponentToGrip))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an invalid or pending kill component"));
		return false;
	}

	if (GetIsObjectHeld(ComponentToGrip))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController grab function was passed an already gripped component"));
		return false;
	}

	// Has to be movable to work
	if (ComponentToGrip->Mobility != EComponentMobility::Movable && (GripCollisionType != EGripCollisionType::CustomGrip && GripCollisionType != EGripCollisionType::EventsOnly))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController tried to grip a component set to static mobility not in CustomGrip mode"));
		return false; // It is not movable, can't grip it
	}

	FBPAdvGripSettings AdvancedGripSettings;
	UObject * ObjectToCheck = NULL;
	//bool bIgnoreHandRotation = false;

	TArray<FBPGripPair> HoldingControllers;
	bool bIsHeld = false;
	bool bHadOriginalSettings = false;
	bool bOriginalGravity = false;
	bool bOriginalReplication = false;

	if (ComponentToGrip->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		if(IVRGripInterface::Execute_DenyGripping(ComponentToGrip, this))
			return false; // Interface is saying not to grip it right now

		IVRGripInterface::Execute_IsHeld(ComponentToGrip, HoldingControllers, bIsHeld);
		bool bAllowMultipleGrips = IVRGripInterface::Execute_AllowsMultipleGrips(ComponentToGrip);
		if (bIsHeld && !bAllowMultipleGrips)
		{
			return false; // Can't multiple grip this object
		}
		else if(bIsHeld)
		{
			// If we are held by multiple controllers then lets copy our original values from the first one	
			if (HoldingControllers[0].HoldingController != nullptr)
			{
				FBPActorGripInformation gripInfo;
				EBPVRResultSwitch result;
				HoldingControllers[0].HoldingController->GetGripByID(gripInfo, HoldingControllers[0].GripID, result);

				if (result != EBPVRResultSwitch::OnFailed)
				{
					bHadOriginalSettings = true;
					bOriginalGravity = gripInfo.bOriginalGravity;
					bOriginalReplication = gripInfo.bOriginalReplicatesMovement;
				}
			}
		}
		

		AdvancedGripSettings = IVRGripInterface::Execute_AdvancedGripSettings(ComponentToGrip);
		ObjectToCheck = ComponentToGrip;
	}

	//ComponentToGrip->IgnoreActorWhenMoving(this->GetOwner(), true);
	// So that events caused by sweep and the like will trigger correctly

	ComponentToGrip->AddTickPrerequisiteComponent(this);

	FBPActorGripInformation newComponentGrip;
	newComponentGrip.GripID = GetNextGripID(bIsLocalGrip);
	newComponentGrip.GripCollisionType = GripCollisionType;
	newComponentGrip.GrippedObject = ComponentToGrip;
	
	if (bHadOriginalSettings)
	{
		newComponentGrip.bOriginalReplicatesMovement = bOriginalReplication;
		newComponentGrip.bOriginalGravity = bOriginalGravity;
	}
	else
	{
		if (ComponentToGrip->GetOwner())
			newComponentGrip.bOriginalReplicatesMovement = ComponentToGrip->GetOwner()->IsReplicatingMovement();

		newComponentGrip.bOriginalGravity = ComponentToGrip->IsGravityEnabled();
	}
	newComponentGrip.Stiffness = GripStiffness;
	newComponentGrip.Damping = GripDamping;
	newComponentGrip.AdvancedGripSettings = AdvancedGripSettings;
	newComponentGrip.GripTargetType = EGripTargetType::ComponentGrip;
	newComponentGrip.ValueCache.bWasInitiallyRepped = true; // Set this true on authority side so we can skip a function call on tick
	newComponentGrip.bIsSlotGrip = bIsSlotGrip;
	newComponentGrip.GrippedBoneName = OptionalBoneToGripName;
	newComponentGrip.SlotName = OptionalSnapToSocketName;

	// Ignore late update setting if it doesn't make sense with the grip
	switch (newComponentGrip.GripCollisionType)
	{
	case EGripCollisionType::ManipulationGrip:
	case EGripCollisionType::ManipulationGripWithWristTwist:
	{
		newComponentGrip.GripLateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOff; // Late updates are bad for this grip
	}break;

	default:
	{
		newComponentGrip.GripLateUpdateSetting = GripLateUpdateSetting;
	}break;
	}


	if (GripMovementReplicationSetting == EGripMovementReplicationSettings::KeepOriginalMovement)
	{
		if (ComponentToGrip->GetOwner())
		{
			if (ComponentToGrip->GetOwner()->IsReplicatingMovement())
			{
				newComponentGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceServerSideMovement;
			}
			else
			{
				newComponentGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
			}
		}
		else
			newComponentGrip.GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement;
	}
	else
		newComponentGrip.GripMovementReplicationSetting = GripMovementReplicationSetting;

	if (OptionalSnapToSocketName.IsValid() && WorldOffset.Equals(FTransform::Identity) && ComponentToGrip->DoesSocketExist(OptionalSnapToSocketName))
	{
		// I inverse it so that laying out the sockets makes sense
		FTransform sockTrans = ComponentToGrip->GetSocketTransform(OptionalSnapToSocketName, ERelativeTransformSpace::RTS_Component);
		sockTrans.SetScale3D(FVector(1.f) / ComponentToGrip->GetComponentScale()); // Prep this so that the inverse works correctly
		newComponentGrip.RelativeTransform = sockTrans.Inverse();
		newComponentGrip.bIsSlotGrip = true; // Set this to a slot grip

		ObjectToCheck = NULL; // Null it out, socketed grips don't use this
	}
	else if (bWorldOffsetIsRelative)
	{
		if (bSkipPivotTransformAdjustment && IsValid(CustomPivotComponent) && !bIsSlotGrip)
		{
			newComponentGrip.RelativeTransform = (WorldOffset * this->GetComponentTransform()).GetRelativeTransform(CustomPivotComponent->GetComponentTransform());
		}
		else
		{
			newComponentGrip.RelativeTransform = WorldOffset;
		}
	}
	else
	{
		newComponentGrip.RelativeTransform = WorldOffset.GetRelativeTransform(GetPivotTransform());
	}

	if (!bIsLocalGrip)
	{
		int32 Index = GrippedObjects.Add(newComponentGrip);
		DIRTY_GRIPPED_OBJECTS();

		if (Index != INDEX_NONE)
			NotifyGrip(GrippedObjects[Index]);

		//NotifyGrip(newComponentGrip);
	}
	else
	{
		if (!IsLocallyControlled())
		{
			LocalTransactionBuffer.Add(newComponentGrip);
#if WITH_PUSH_MODEL
			MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, LocalTransactionBuffer, this);
#endif
		}

		int32 Index = LocallyGrippedObjects.Add(newComponentGrip);
		DIRTY_LOCALLY_GRIPPED_OBJECTS();

		if (Index != INDEX_NONE)
		{
			if (!IsLocallyControlled())
			{		
				if (!HandleGripReplication(LocallyGrippedObjects[Index]))
				{
					return true;
				}
			}
			else
			{
				if (!NotifyGrip(LocallyGrippedObjects[Index]))
				{
					return true;
				}
			}

			if (bIsLocalGrip && IsLocallyControlled() && !IsServer() && !IsTornOff() && newComponentGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive)
			{
				Index = LocallyGrippedObjects.IndexOfByKey(newComponentGrip.GripID);
				if (Index != INDEX_NONE)
				{
					FBPActorGripInformation GripInfo = LocallyGrippedObjects[Index];
					Server_NotifyLocalGripAddedOrChanged(GripInfo);
				}
			}
		}
	}

	return true;
}

bool UGripMotionControllerComponent::DropComponent(UPrimitiveComponent * ComponentToDrop, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	FBPActorGripInformation *GripInfo;
	
	// First check for it in the local grips	
	GripInfo = LocallyGrippedObjects.FindByKey(ComponentToDrop);

	if (GripInfo != nullptr)
	{
		return DropGrip_Implementation(*GripInfo, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
	}

	// If we aren't the server then fail out
	if (!IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was called on the client side for a replicated grip"));
		return false;
	}

	// Now check in the server auth gripsop)
	GripInfo = GrippedObjects.FindByKey(ComponentToDrop);

	if (GripInfo != nullptr)
	{
		return DropGrip_Implementation(*GripInfo, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
	}
	else
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop function was passed an invalid component"));
		return false;
	}

	//return false;
}

bool UGripMotionControllerComponent::DropGrip(const FBPActorGripInformation& Grip, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity)
{
	return DropGrip_Implementation(Grip, bSimulate, OptionalAngularVelocity, OptionalLinearVelocity);
}

bool UGripMotionControllerComponent::DropGrip_Implementation(const FBPActorGripInformation &Grip, bool bSimulate, FVector OptionalAngularVelocity, FVector OptionalLinearVelocity, bool bSkipNotify)
{
	int FoundIndex = 0;
	bool bIsServer = IsServer();
	bool bWasLocalGrip = false;
	if (!LocallyGrippedObjects.Find(Grip, FoundIndex)) // This auto checks if Actor and Component are valid in the == operator
	{
		if (!bIsServer)
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

	if (bWasLocalGrip && bIsServer)
	{
		for (int i = LocalTransactionBuffer.Num() - 1; i >= 0; i--)
		{
			if (LocalTransactionBuffer[i].GripID == Grip.GripID)
				LocalTransactionBuffer.RemoveAt(i);
		}
#if WITH_PUSH_MODEL
		MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, LocalTransactionBuffer, this);
#endif
	}

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
		if (bSimulate && (!OptionalLinearVelocity.IsNearlyZero() || !OptionalAngularVelocity.IsNearlyZero()))
		{
			if (Grip.GripCollisionType != EGripCollisionType::EventsOnly)
			{
				// Need to set simulation in all of these cases, including if it isn't the root component (simulation isn't replicated on non roots)
				if (!Grip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings || !Grip.AdvancedGripSettings.PhysicsSettings.bSkipSettingSimulating)
				{
					if (PrimComp->IsSimulatingPhysics() != bSimulate)
					{
						PrimComp->SetSimulatePhysics(bSimulate);
					}
				}
			}

			// Had to move in front of deletion to properly set velocity
			if (PrimComp->IsSimulatingPhysics())
			{
				PrimComp->SetPhysicsLinearVelocity(OptionalLinearVelocity);
				PrimComp->SetPhysicsAngularVelocityInDegrees(OptionalAngularVelocity);
			}
		}
	}

	if (bWasLocalGrip)
	{
		// Store out a local copy so we can't get funky issues with the engine dropping the grip out from under us
		FBPActorGripInformation GripInfo = LocallyGrippedObjects[FoundIndex];

		if (IsLocallyControlled() && !IsServer()) //GetNetMode() == ENetMode::NM_Client)
		{
			if (!IsTornOff())
			{
				FTransform_NetQuantize TransformAtDrop = FTransform::Identity;

				switch (GripInfo.GripTargetType)
				{
				case EGripTargetType::ActorGrip:
				{
					if (AActor * GrippedActor = GripInfo.GetGrippedActor())
					{
						TransformAtDrop = GrippedActor->GetActorTransform();
					}
				}; break;
				case EGripTargetType::ComponentGrip:
				{
					if (UPrimitiveComponent * GrippedPrim = GripInfo.GetGrippedComponent())
					{
						TransformAtDrop = GrippedPrim->GetComponentTransform();
					}
				}break;
				default:break;
				}

				if(!bSkipNotify)
					Server_NotifyLocalGripRemoved(GripInfo.GripID, TransformAtDrop, OptionalAngularVelocity, OptionalLinearVelocity);
			}

			// Double check we didn't lose the grip (seems to be possible in UE5 from the Server RPC above being ran on the client)
			if (LocallyGrippedObjects.Num() > 0 && LocallyGrippedObjects.Find(GripInfo, FoundIndex))
			{
				// Have to call this ourselves
				Drop_Implementation(GripInfo, bSimulate);
			}
		}
		else // Server notifyDrop it
		{
			NotifyDrop(GripInfo, bSimulate);
		}
	}
	else
		NotifyDrop(GrippedObjects[FoundIndex], bSimulate);

	//GrippedObjects.RemoveAt(FoundIndex);		
	return true;
}

bool UGripMotionControllerComponent::DropAndSocketObject(const FTransform_NetQuantize & RelativeTransformToParent, UObject * ObjectToDrop, uint8 GripIDToDrop, USceneComponent * SocketingParent, FName OptionalSocketName, bool bWeldBodies)
{
	if (!SocketingParent)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid socketing parent"));
		return false;
	}

	if (!ObjectToDrop && GripIDToDrop == INVALID_VRGRIP_ID)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid object"));
		return false;
	}

	bool bWasLocalGrip = false;
	FBPActorGripInformation * GripInfo = nullptr;

	if (ObjectToDrop)
		GripInfo = LocallyGrippedObjects.FindByKey(ObjectToDrop);
	else if (GripIDToDrop != INVALID_VRGRIP_ID)
		GripInfo = LocallyGrippedObjects.FindByKey(GripIDToDrop);

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

		if(ObjectToDrop)
			GripInfo = GrippedObjects.FindByKey(ObjectToDrop);
		else if(GripIDToDrop != INVALID_VRGRIP_ID)
			GripInfo = GrippedObjects.FindByKey(GripIDToDrop);

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
		return DropAndSocketGrip_Implementation(*GripInfo, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
	
	return false;
}

bool UGripMotionControllerComponent::DropAndSocketGrip(const FBPActorGripInformation& GripToDrop, USceneComponent* SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize& RelativeTransformToParent, bool bWeldBodies)
{
	return DropAndSocketGrip_Implementation(GripToDrop, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
}

bool UGripMotionControllerComponent::DropAndSocketGrip_Implementation(const FBPActorGripInformation & GripToDrop, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent, bool bWeldBodies, bool bSkipServerNotify)
{
	if (!SocketingParent || !IsValid(SocketingParent))
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

	PrimComp = GripInfo->GetGrippedComponent();
	pActor = GripInfo->GetGrippedActor();

	if (!PrimComp && pActor)
		PrimComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if (!PrimComp)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid drop or CleanUpBadGrip wascalled"));
		//return false;
	}

	UObject * GrippedObject = GripInfo->GrippedObject;

	if (!GrippedObject || !IsValid(GrippedObject))
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController drop and socket function was passed an invalid or pending kill gripped object"));
		return false;
	}

	int PhysicsHandleIndex = INDEX_NONE;
	GetPhysicsGripIndex(*GripInfo, PhysicsHandleIndex);

	if (bWasLocalGrip)
	{
		if (IsLocallyControlled() && !IsServer())
		{
			if (!IsTornOff() && !bSkipServerNotify)
			{
				Server_NotifyDropAndSocketGrip(GripInfo->GripID, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
			}

			OnSocketingObject.Broadcast(*GripInfo, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
			Socket_Implementation(GrippedObject, (PhysicsHandleIndex != INDEX_NONE), SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);

			// Have to call this ourselves
			DropAndSocket_Implementation(*GripInfo);
		}
		else // Server notifyDrop it
		{
			//Socket_Implementation(GrippedObject, (PhysicsHandleIndex != INDEX_NONE), SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
			NotifyDropAndSocket(*GripInfo, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
		}
	}
	else
	{
		//Socket_Implementation(GrippedObject, (PhysicsHandleIndex != INDEX_NONE), SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
		NotifyDropAndSocket(*GripInfo, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
	}

	//GrippedObjects.RemoveAt(FoundIndex);		
	return true;
}

void UGripMotionControllerComponent::SetSocketTransform(UObject* ObjectToSocket, /*USceneComponent * SocketingParent,*/ const FTransform_NetQuantize RelativeTransformToParent/*, FName OptionalSocketName, bool bWeldBodies*/)
{
	if (ObjectsWaitingForSocketUpdate.RemoveSingle(ObjectToSocket) < 1)
	{
		// I know that technically it should never happen that the pointers get reset with a uproperty
		// But does it really hurt to add this pathway anyway?
		for (int i = ObjectsWaitingForSocketUpdate.Num() - 1; i >= 0; --i)
		{
			if (ObjectsWaitingForSocketUpdate[i] == nullptr)
				ObjectsWaitingForSocketUpdate.RemoveAt(i);
		}

		return;
	}

	if (!ObjectToSocket || !IsValid(ObjectToSocket))
		return;

	/*FAttachmentTransformRules TransformRule = FAttachmentTransformRules::KeepWorldTransform;//KeepWorldTransform;
	TransformRule.bWeldSimulatedBodies = bWeldBodies;*/

	if (UPrimitiveComponent * root = Cast<UPrimitiveComponent>(ObjectToSocket))
	{
		//root->AttachToComponent(SocketingParent, TransformRule, OptionalSocketName);
		//root->SetRelativeTransform(RelativeTransformToParent);

		if(root->GetAttachParent())
			root->SetRelativeTransform(RelativeTransformToParent);
	}
	else if (AActor * pActor = Cast<AActor>(ObjectToSocket))
	{
		//pActor->AttachToComponent(SocketingParent, TransformRule, OptionalSocketName);
		//pActor->SetActorRelativeTransform(RelativeTransformToParent);

		if(pActor->GetAttachParentActor())
			pActor->SetActorRelativeTransform(RelativeTransformToParent);
	}
}


bool UGripMotionControllerComponent::Server_NotifyDropAndSocketGrip_Validate(uint8 GripID, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent, bool bWeldBodies)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifyDropAndSocketGrip_Implementation(uint8 GripID, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent, bool bWeldBodies)
{
	FBPActorGripInformation FoundGrip;
	EBPVRResultSwitch Result;
	
	GetGripByID(FoundGrip, GripID, Result);

	if (Result == EBPVRResultSwitch::OnFailed)
		return;

	int PhysicsHandleIndex = INDEX_NONE;
	GetPhysicsGripIndex(FoundGrip, PhysicsHandleIndex);

	if (FoundGrip.GrippedObject)
	{
		OnSocketingObject.Broadcast(FoundGrip, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
		Socket_Implementation(FoundGrip.GrippedObject, (PhysicsHandleIndex != INDEX_NONE), SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
	}

	if (!DropAndSocketGrip_Implementation(FoundGrip, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies, true))
	{
		DropGrip_Implementation(FoundGrip, false, FVector::ZeroVector, FVector::ZeroVector, true);
	}

}

void UGripMotionControllerComponent::Socket_Implementation(UObject * ObjectToSocket, bool bWasSimulating, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent, bool bWeldBodies)
{
	// Check for valid objects
	if (!SocketingParent || !SocketingParent->IsValidLowLevelFast() || !ObjectToSocket || !ObjectToSocket->IsValidLowLevelFast())
	{
		if (!SocketingParent || !SocketingParent->IsValidLowLevelFast())
		{
			UE_LOG(LogVRMotionController, Error, TEXT("VRGripMotionController Socket_Implementation was called with an invalid Socketing Parent object"));
		}
		else
		{
			UE_LOG(LogVRMotionController, Error, TEXT("VRGripMotionController Socket_Implementation was called with an invalid Object to Socket"));
		}
		return;
	}

	FAttachmentTransformRules TransformRule = FAttachmentTransformRules::KeepWorldTransform;//KeepWorldTransform;
	TransformRule.bWeldSimulatedBodies = bWeldBodies;

	//UPrimitiveComponent * ParentPrim = Cast<UPrimitiveComponent>(SocketingParent);

	if (UPrimitiveComponent * root = Cast<UPrimitiveComponent>(ObjectToSocket))
	{
		if (FBodyInstance* rBodyInstance = root->GetBodyInstance())
		{
			if (rBodyInstance->OnRecalculatedMassProperties().IsBoundToObject(this))
			{
				rBodyInstance->OnRecalculatedMassProperties().RemoveAll(this);
			}
		}

		// Stop simulation for socketing
		if (bWasSimulating || root->IsSimulatingPhysics())
		{
			root->SetSimulatePhysics(false);
			bWasSimulating = true;
		}

		root->AttachToComponent(SocketingParent, TransformRule, OptionalSocketName);
		root->SetRelativeTransform(RelativeTransformToParent);
	}
	else if (AActor * pActor = Cast<AActor>(ObjectToSocket))
	{

		if (UPrimitiveComponent * rootComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent()))
		{
			if (FBodyInstance* rBodyInstance = rootComp->GetBodyInstance())
			{
				if (rBodyInstance->OnRecalculatedMassProperties().IsBoundToObject(this))
				{
					rBodyInstance->OnRecalculatedMassProperties().RemoveAll(this);
				}
			}

			if (bWasSimulating || rootComp->IsSimulatingPhysics())
			{
				// Stop simulation for socketing
				rootComp->SetSimulatePhysics(false);
				bWasSimulating = true;
			}
		}

		pActor->AttachToComponent(SocketingParent, TransformRule, OptionalSocketName);
		pActor->SetActorRelativeTransform(RelativeTransformToParent);

		//if (!bRetainOwnership)
			//pActor->SetOwner(nullptr);
	}

	// It had a physics handle or was simulating, I need to delay a tick and set the transform to ensure it skips a race condition
	// I may need to consider running the entire attachment in here instead in the future
	if (bWasSimulating)
	{
		ObjectsWaitingForSocketUpdate.Add(ObjectToSocket);
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UGripMotionControllerComponent::SetSocketTransform, ObjectToSocket, /*SocketingParent, */RelativeTransformToParent/*, OptionalSocketName, bWeldBodies*/));
	}
}

void UGripMotionControllerComponent::NotifyDropAndSocket_Implementation(const FBPActorGripInformation &NewDrop, USceneComponent* SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize& RelativeTransformToParent, bool bWeldBodies)
{
	// Don't do this if we are the owning player on a local grip, there is no filter for multicast to not send to owner
	if ((NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive ||
		NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) &&
		IsLocallyControlled() &&
		!IsServer())
	{

		// If we still have the grip then the server is asking us to drop it even though it is locally controlled
		if (FBPActorGripInformation * GripInfo = GetGripPtrByID(NewDrop.GripID))
		{
			DropAndSocketGrip_Implementation(*GripInfo, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies, true);
		}
		return;
	}

	int PhysicsHandleIndex = INDEX_NONE;
	GetPhysicsGripIndex(NewDrop, PhysicsHandleIndex);

	if (NewDrop.GrippedObject)
	{
		OnSocketingObject.Broadcast(NewDrop, SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
		Socket_Implementation(NewDrop.GrippedObject, (PhysicsHandleIndex != INDEX_NONE), SocketingParent, OptionalSocketName, RelativeTransformToParent, bWeldBodies);
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
				//root->IgnoreActorWhenMoving(this->GetOwner(), false);

				// Attachment already handles both of these
				//root->UpdateComponentToWorld(); // This fixes the late update offset
				//root->SetSimulatePhysics(false);

				if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
					(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
					root->SetEnableGravity(NewDrop.bOriginalGravity);
				
				// Stop Physics sim for socketing
				root->SetSimulatePhysics(false);
			}

			if (IsServer()) //&& !bSkipFullDrop)
			{
				pActor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
			}

			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_SetHeld(pActor, this, NewDrop.GripID, false);

				if(NewDrop.SecondaryGripInfo.bHasSecondaryAttachment || SecondaryGripIDs.Contains(NewDrop.GripID))
				{
					IVRGripInterface::Execute_OnSecondaryGripRelease(pActor, this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);
					OnSecondaryGripRemoved.Broadcast(NewDrop);
				}

				SecondaryGripIDs.Remove(NewDrop.GripID);

				TArray<UVRGripScriptBase*> GripScripts;
				if (IVRGripInterface::Execute_GetGripScripts(pActor, GripScripts))
				{
					for (UVRGripScriptBase* Script : GripScripts)
					{
						if (Script)
						{
							if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
								Script->OnSecondaryGripRelease(this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

							Script->OnGripRelease(this, NewDrop, true);
						}
					}
				}

				IVRGripInterface::Execute_OnGripRelease(pActor, this, NewDrop, true);
				if (IVRGripInterface* GripInterface = Cast<IVRGripInterface>(pActor))
				{
					GripInterface->Native_NotifyThrowGripDelegates(this, false, NewDrop, true);
				}

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
			//root->IgnoreActorWhenMoving(this->GetOwner(), false);

			// Attachment already handles both of these
			//root->UpdateComponentToWorld();
			//root->SetSimulatePhysics(false);

			if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
				(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
				root->SetEnableGravity(NewDrop.bOriginalGravity);

			// Stop Physics sim for socketing
			root->SetSimulatePhysics(false);

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

			if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_SetHeld(root, this, NewDrop.GripID, false);

				if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment || SecondaryGripIDs.Contains(NewDrop.GripID))
				{
					IVRGripInterface::Execute_OnSecondaryGripRelease(root, this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);
					OnSecondaryGripRemoved.Broadcast(NewDrop);
				}

				SecondaryGripIDs.Remove(NewDrop.GripID);

				TArray<UVRGripScriptBase*> GripScripts;
				if (IVRGripInterface::Execute_GetGripScripts(root, GripScripts))
				{
					for (UVRGripScriptBase* Script : GripScripts)
					{
						if (Script)
						{
							if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
								Script->OnSecondaryGripRelease(this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

							Script->OnGripRelease(this, NewDrop, true);
						}
					}
				}

				IVRGripInterface::Execute_OnGripRelease(root, this, NewDrop, true);
				if (IVRGripInterface* GripInterface = Cast<IVRGripInterface>(root))
				{
					GripInterface->Native_NotifyThrowGripDelegates(this, false, NewDrop, true);
				}
			}

			// Call on child grip release on attached parent component
			if (root->GetAttachParent() && root->GetAttachParent()->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnChildGripRelease(root->GetAttachParent(), this, NewDrop, true);
			}
		}
	}break;
	}

	// Copy over the information instead of working with a reference for the OnDroppedBroadcast
	FBPActorGripInformation DropBroadcastData = NewDrop;

	int fIndex = 0;
	if (LocallyGrippedObjects.Find(NewDrop, fIndex))
	{
		DIRTY_LOCALLY_GRIPPED_OBJECTS();

		if (HasGripAuthority(NewDrop) || IsServer())
		{
			LocallyGrippedObjects.RemoveAt(fIndex);
		}
		else
		{
			LocallyGrippedObjects[fIndex].bIsPendingKill = true;
			LocallyGrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
		}
	}
	else
	{
		fIndex = 0;
		if (GrippedObjects.Find(NewDrop, fIndex))
		{
			DIRTY_GRIPPED_OBJECTS();
			if (HasGripAuthority(NewDrop) || IsServer())
			{
				GrippedObjects.RemoveAt(fIndex);
			}
			else
			{
				GrippedObjects[fIndex].bIsPendingKill = true;
				GrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
			}
		}
	}

	// Broadcast a new drop
	OnDroppedObject.Broadcast(DropBroadcastData, true);
}


// No longer an RPC, now is called from RepNotify so that joining clients also correctly set up grips
bool UGripMotionControllerComponent::NotifyGrip(FBPActorGripInformation &NewGrip, bool bIsReInit)
{
	UPrimitiveComponent *root = NULL;
	AActor *pActor = NULL;

	bool bRootHasInterface = false;
	bool bActorHasInterface = false;

	if (!NewGrip.GrippedObject || !NewGrip.GrippedObject->IsValidLowLevelFast())
		return false;

	if (!NewGrip.AdvancedGripSettings.bDisallowLerping && !bIsReInit && NewGrip.GripCollisionType != EGripCollisionType::EventsOnly && NewGrip.GripCollisionType != EGripCollisionType::CustomGrip)
	{
		// Init lerping
		InitializeLerpToHand(NewGrip);
	}

	switch (NewGrip.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	//case EGripTargetType::InteractibleActorGrip:
	{
		pActor = NewGrip.GetGrippedActor();

		if (pActor)
		{
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

			if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				bRootHasInterface = true;
			}
			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				// Actor grip interface is checked after component
				bActorHasInterface = true;
			}

			if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
			{
				if (NewGrip.GripCollisionType != EGripCollisionType::EventsOnly)
				{
					OwningPawn->MoveIgnoreActorAdd(pActor);
				}

				// Now I am setting the owner to the owning pawn if we are one
				// This makes sure that some special replication needs are taken care of
				// Only doing this for actor grips
				if (NewGrip.AdvancedGripSettings.bSetOwnerOnGrip)
				{
					if (IsServer())
					{
						pActor->SetOwner(OwningPawn);
					}
				}
			}

			if (!bIsReInit && bActorHasInterface)
			{
				IVRGripInterface::Execute_SetHeld(pActor, this, NewGrip.GripID, true);

				TArray<UVRGripScriptBase*> GripScripts;
				if (IVRGripInterface::Execute_GetGripScripts(pActor, GripScripts))
				{
					for (UVRGripScriptBase* Script : GripScripts)
					{
						if (Script)
						{
							Script->OnGrip(this, NewGrip);
						}
					}
				}

				uint8 GripID = NewGrip.GripID;
				IVRGripInterface::Execute_OnGrip(pActor, this, NewGrip);
				if (!LocallyGrippedObjects.Contains(GripID) && !GrippedObjects.Contains(GripID))
				{
					return false;
				}

				// Now check for c++ specific implementation and throw the native event if we need too
				if (IVRGripInterface* GripInterface = Cast<IVRGripInterface>(pActor))
				{
					GripInterface->Native_NotifyThrowGripDelegates(this, true, NewGrip, false);

					if (!LocallyGrippedObjects.Contains(GripID) && !GrippedObjects.Contains(GripID))
					{
						return false;
					}
				}

			}

			if (root)
			{
				if (NewGrip.GripCollisionType != EGripCollisionType::EventsOnly)
				{
					// Have to turn off gravity locally
					if ((NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
						(NewGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
						root->SetEnableGravity(false);
				}
				//root->IgnoreActorWhenMoving(this->GetOwner(), true);
			}


		}
		else
			return false;

		if (bActorHasInterface && !EndPhysicsTickFunction.IsTickFunctionRegistered())
		{
			if (bProjectNonSimulatingGrips)
			{
				RegisterEndPhysicsTick(true);
			}
			else
			{
				EGripInterfaceTeleportBehavior TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(pActor);

				if (TeleportBehavior == EGripInterfaceTeleportBehavior::DeltaTeleportation)
				{
					RegisterEndPhysicsTick(true);
				}
			}
		}

	}break;

	case EGripTargetType::ComponentGrip:
	//case EGripTargetType::InteractibleComponentGrip:
	{
		root = NewGrip.GetGrippedComponent();

		if (root)
		{
			pActor = root->GetOwner();

			if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				bRootHasInterface = true;
			}
			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				// Actor grip interface is checked after component
				bActorHasInterface = true;
			}

			if (!bIsReInit && bRootHasInterface)
			{
				IVRGripInterface::Execute_SetHeld(root, this, NewGrip.GripID, true);

				TArray<UVRGripScriptBase*> GripScripts;
				if (IVRGripInterface::Execute_GetGripScripts(root, GripScripts))
				{
					for (UVRGripScriptBase* Script : GripScripts)
					{
						if (Script)
						{
							Script->OnGrip(this, NewGrip);
						}
					}
				}
				
				uint8 GripID = NewGrip.GripID;
				IVRGripInterface::Execute_OnGrip(root, this, NewGrip);
				if (!LocallyGrippedObjects.Contains(GripID) && !GrippedObjects.Contains(GripID))
				{
					return false;
				}

				// Now throw the native event if it implements the native interface
				if (IVRGripInterface* GripInterface = Cast<IVRGripInterface>(root))
				{
					//GripInterface->Execute_OnGrip(root, this, NewGrip);
					GripInterface->Native_NotifyThrowGripDelegates(this, true, NewGrip, false);

					if (!LocallyGrippedObjects.Contains(GripID) && !GrippedObjects.Contains(GripID))
					{
						return false;
					}

				}

			}

			if (pActor)
			{
				/*if (APawn* OwningPawn = Cast<APawn>(GetOwner()))
				{
					OwningPawn->MoveIgnoreActorAdd(root->GetOwner());
				}*/

				if (!bIsReInit && bActorHasInterface)
				{
					uint8 GripID = NewGrip.GripID;
					IVRGripInterface::Execute_OnChildGrip(pActor, this, NewGrip);
					if (!LocallyGrippedObjects.Contains(GripID) && !GrippedObjects.Contains(GripID))
					{
						return false;
					}
				}

			}

			// Call OnChildGrip for attached grip parent
			if (!bIsReInit && root->GetAttachParent() && root->GetAttachParent()->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				uint8 GripID = NewGrip.GripID;
				IVRGripInterface::Execute_OnChildGrip(root->GetAttachParent(), this, NewGrip);
				if (!LocallyGrippedObjects.Contains(GripID) && !GrippedObjects.Contains(GripID))
				{
					return false;
				}
			}

			if (NewGrip.GripCollisionType != EGripCollisionType::EventsOnly)
			{
				if ((NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
					(NewGrip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
					root->SetEnableGravity(false);
			}

			//root->IgnoreActorWhenMoving(this->GetOwner(), true);
		}
		else
			return false;

		if (bRootHasInterface && !EndPhysicsTickFunction.IsTickFunctionRegistered())
		{
			if (bProjectNonSimulatingGrips)
			{
				RegisterEndPhysicsTick(true);
			}
			else
			{
				EGripInterfaceTeleportBehavior TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(root);

				if (TeleportBehavior == EGripInterfaceTeleportBehavior::DeltaTeleportation)
				{
					RegisterEndPhysicsTick(true);
				}
			}
		}

	}break;
	}

	switch (NewGrip.GripMovementReplicationSetting)
	{
	case EGripMovementReplicationSettings::ForceClientSideMovement:
	case EGripMovementReplicationSettings::ClientSide_Authoritive:
	case EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep:
	{
		if (NewGrip.GripCollisionType != EGripCollisionType::EventsOnly)
		{
			if (IsServer() && pActor && ((NewGrip.GripTargetType == EGripTargetType::ActorGrip) || (root && root == pActor->GetRootComponent())))
			{
				pActor->SetReplicateMovement(false);
			}
			if (root)
			{
				// #TODO: This is a hack until Epic fixes their new physics replication code
				//		  It forces the replication target to null on grip if we aren't repping movement.

				if (UWorld* World = GetWorld())
				{
					if (FPhysScene* PhysScene = World->GetPhysicsScene())
					{
						if (IPhysicsReplication* PhysicsReplication = PhysScene->GetPhysicsReplication())
						{
							FBodyInstance* BI = root->GetBodyInstance(NewGrip.GrippedBoneName);
							if (BI && BI->IsInstanceSimulatingPhysics())
							{
								PhysicsReplication->RemoveReplicatedTarget(root);
								//PhysicsReplication->SetReplicatedTarget(this, BoneName, UpdatedState);
							}
						}
					}
				}
			}
		}

	}break; 

	case EGripMovementReplicationSettings::ForceServerSideMovement:
	{
		if (NewGrip.GripCollisionType != EGripCollisionType::EventsOnly)
		{
			if (IsServer() && pActor && ((NewGrip.GripTargetType == EGripTargetType::ActorGrip) || (root && root == pActor->GetRootComponent())))
			{
				pActor->SetReplicateMovement(true);
			}
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
	case EGripCollisionType::LockedConstraint:
	case EGripCollisionType::ManipulationGrip:
	case EGripCollisionType::ManipulationGripWithWristTwist:
	{
		if (bHasMovementAuthority)
		{
			SetUpPhysicsHandle(NewGrip);
		}
	} break;


	case EGripCollisionType::InteractiveHybridCollisionWithPhysics:
	{
		if (bHasMovementAuthority)
		{
			SetUpPhysicsHandle(NewGrip);
		}
	} break;

	// Skip collision intersects with these types, they dont need it
	case EGripCollisionType::EventsOnly:
	case EGripCollisionType::CustomGrip:
	{		
		// Should have never been turning off physics here, simulating is a valid custom grip state
		//if (root)
			//root->SetSimulatePhysics(false);

	} break;

	case EGripCollisionType::AttachmentGrip:
	{
		if (IsValid(root))
			root->SetSimulatePhysics(false);

		// Move it to the correct location automatically
		if (bHasMovementAuthority)
		{
			if (!NewGrip.bIsLerping)
			{
				TeleportMoveGrip(NewGrip);
			}
		}

		if (bHasMovementAuthority || IsServer())
		{
			FName BoneName = IsValid(CustomPivotComponent) ? CustomPivotComponentSocketName : NAME_None;
			root->AttachToComponent(IsValid(CustomPivotComponent) ? CustomPivotComponent.Get() : this, FAttachmentTransformRules(EAttachmentRule::KeepWorld, true), BoneName);
		}

	}break;

	case EGripCollisionType::PhysicsOnly:
	case EGripCollisionType::SweepWithPhysics:
	case EGripCollisionType::InteractiveHybridCollisionWithSweep:
	case EGripCollisionType::InteractiveCollisionWithSweep:
	default: 
	{

		if (IsValid(root))
		{
			if (root->IsSimulatingPhysics())
			{
				root->SetSimulatePhysics(false);
			}
			// #TODO Remove the ELSE below when chaos is fixed for body welding without simulation
			else
			{
				root->SetSimulatePhysics(true); // Forces it to weld children
				root->SetSimulatePhysics(false); // Stop it
			}

			if(root->GetAttachParent())
			{
				root->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
			}
		}

		// Move it to the correct location automatically
		if (bHasMovementAuthority)
		{
			if (!NewGrip.bIsLerping)
			{
				TeleportMoveGrip(NewGrip);
			}
		}
	} break;

	}

	if (!bIsReInit)
	{
		// TMP #TODO: Remove when 5.4 velocity bug is fixed
		if (IsValid(root))
		{
			// Set initial world transform for velocity
			NewGrip.LastVelWorldTrans = root->GetComponentTransform();
		}

		// Broadcast a new grip
		OnGrippedObject.Broadcast(NewGrip);
		if (!LocallyGrippedObjects.Contains(NewGrip.GripID) && !GrippedObjects.Contains(NewGrip.GripID))
		{
			return false;
		}
	}

	return true;
}

void UGripMotionControllerComponent::InitializeLerpToHand(FBPActorGripInformation & GripInformation)
{
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	if (!VRSettings.bUseGlobalLerpToHand || VRSettings.LerpDuration <= 0.f)
		return;

	if (VRSettings.bSkipLerpToHandIfHeld && GripInformation.GrippedObject && GripInformation.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		bool bIsHeld = false;
		TArray<FBPGripPair> HoldingControllers;
		// Check if a different controller is holding it
		IVRGripInterface::Execute_IsHeld(GripInformation.GrippedObject, HoldingControllers, bIsHeld);

		if (HoldingControllers.Num() > 0)
		{
			for (FBPGripPair& ControllerPair : HoldingControllers)
			{
				if (ControllerPair.HoldingController && ControllerPair.HoldingController != this)
				{
					FBPActorGripInformation Grip;
					EBPVRResultSwitch Result;
					ControllerPair.HoldingController->GetGripByID(Grip, ControllerPair.GripID, Result);

					if (Result != EBPVRResultSwitch::OnFailed)
					{
						if (Grip.IsValid())
						{
							// We are skipping lerping now
							GripInformation.bIsLerping = false;
							return;
						}
					}
				}
			}
		}
	}

	EBPVRResultSwitch Result;
	TSubclassOf<class UVRGripScriptBase> Class = UGS_LerpToHand::StaticClass();
	UVRGripScriptBase::GetGripScriptByClass(GripInformation.GrippedObject, Class, Result);
	if (Result == EBPVRResultSwitch::OnSucceeded)
	{
		return;
	}

	if (USceneComponent* PrimParent = Cast<USceneComponent>(GripInformation.GrippedObject))
	{
		if (GripInformation.GrippedBoneName != NAME_None)
		{
			GripInformation.OnGripTransform = PrimParent->GetSocketTransform(GripInformation.GrippedBoneName);
		}
		else
		{
			GripInformation.OnGripTransform = PrimParent->GetComponentTransform();
		}
	}
	else if (AActor* ParentActor = Cast<AActor>(GripInformation.GrippedObject))
	{
		GripInformation.OnGripTransform = ParentActor->GetActorTransform();
	}

	FTransform TargetTransform = GripInformation.RelativeTransform * this->GetPivotTransform();
	float Distance = FVector::Dist(GripInformation.OnGripTransform.GetLocation(), TargetTransform.GetLocation());
	if (VRSettings.MinDistanceForLerp > 0.0f && Distance < VRSettings.MinDistanceForLerp)
	{
		// Don't init
		GripInformation.bIsLerping = false;
		//OnLerpToHandFinished.Broadcast(GripInformation);
		return;
	}
	else
	{
		float LerpScaler = 1.0f;
		float DistanceToSpeed = Distance / VRSettings.LerpDuration;
		if (DistanceToSpeed < VRSettings.MinSpeedForLerp)
		{
			LerpScaler = VRSettings.MinSpeedForLerp / DistanceToSpeed;
		}
		else if (VRSettings.MaxSpeedForLerp > 0.f && DistanceToSpeed > VRSettings.MaxSpeedForLerp)
		{
			LerpScaler = VRSettings.MaxSpeedForLerp / DistanceToSpeed;
		}
		else
		{
			LerpScaler = 1.0f;
		}

		// Get the modified lerp speed
		GripInformation.LerpSpeed = ((1.f / VRSettings.LerpDuration) * LerpScaler);
		GripInformation.bIsLerping = true;
		GripInformation.CurrentLerpTime = 0.0f;
	}

	GripInformation.CurrentLerpTime = 0.0f;
}

void UGripMotionControllerComponent::HandleGlobalLerpToHand(FBPActorGripInformation& GripInformation, FTransform& WorldTransform, float DeltaTime)
{
	UVRGlobalSettings* VRSettings = GetMutableDefault<UVRGlobalSettings>();

	if (!VRSettings->bUseGlobalLerpToHand || !GripInformation.bIsLerping)
		return;

	EBPVRResultSwitch Result;
	TSubclassOf<class UVRGripScriptBase> Class = UGS_LerpToHand::StaticClass();
	UVRGripScriptBase * LerpScript = UVRGripScriptBase::GetGripScriptByClass(GripInformation.GrippedObject, Class, Result);
	if (Result == EBPVRResultSwitch::OnSucceeded && LerpScript && LerpScript->IsScriptActive())
	{
		return;
	}

	if (VRSettings->LerpDuration <= 0.f)
	{
		GripInformation.bIsLerping = false;
		GripInformation.CurrentLerpTime = 0.f;
		OnLerpToHandFinished.Broadcast(GripInformation);
		return;
	}

	FTransform NA = GripInformation.OnGripTransform;//root->GetComponentTransform();
	float Alpha = 0.0f;

	GripInformation.CurrentLerpTime += DeltaTime * GripInformation.LerpSpeed;
	float OrigAlpha = FMath::Clamp(GripInformation.CurrentLerpTime, 0.f, 1.0f);
	Alpha = OrigAlpha;

	if (VRSettings->bUseCurve)
	{
		if (FRichCurve* richCurve = VRSettings->OptionalCurveToFollow.GetRichCurve())
		{
			/*if (CurrentLerpTime > richCurve->GetLastKey().Time)
			{
				// Stop lerping
				OnLerpToHandFinished.Broadcast();
				CurrentLerpTime = 0.0f;
				bIsActive = false;
				return true;
			}
			else*/
			{
				Alpha = FMath::Clamp(richCurve->Eval(Alpha), 0.f, 1.f);
				//CurrentLerpTime += DeltaTime;
			}
		}
	}

	FTransform NB = WorldTransform;
	NA.NormalizeRotation();
	NB.NormalizeRotation();

	// Quaternion interpolation
	if (VRSettings->LerpInterpolationMode == EVRLerpInterpolationMode::QuatInterp)
	{
		WorldTransform.Blend(NA, NB, Alpha);
	}

	// Euler Angle interpolation
	else if (VRSettings->LerpInterpolationMode == EVRLerpInterpolationMode::EulerInterp)
	{
		WorldTransform.SetTranslation(FMath::Lerp(NA.GetTranslation(), NB.GetTranslation(), Alpha));
		WorldTransform.SetScale3D(FMath::Lerp(NA.GetScale3D(), NB.GetScale3D(), Alpha));

		FRotator A = NA.Rotator();
		FRotator B = NB.Rotator();
		WorldTransform.SetRotation(FQuat(A + (Alpha * (B - A))));
	}
	// Dual quaternion interpolation
	else
	{
		if ((NB.GetRotation() | NA.GetRotation()) < 0.0f)
		{
			NB.SetRotation(NB.GetRotation() * -1.0f);
		}
		WorldTransform = (FDualQuat(NA) * (1 - Alpha) + FDualQuat(NB) * Alpha).Normalized().AsFTransform(FMath::Lerp(NA.GetScale3D(), NB.GetScale3D(), Alpha));
	}

	// Turn it off if we need to
	if (OrigAlpha >= 1.0f)
	{
		GripInformation.CurrentLerpTime = 0.0f;
		GripInformation.bIsLerping = false;

		if (bConstrainToPivot)
		{
			DestroyPhysicsHandle(GripInformation, false);
			SetUpPhysicsHandle(GripInformation);
		}


		OnLerpToHandFinished.Broadcast(GripInformation);
	}
}

void UGripMotionControllerComponent::CancelGlobalLerpToHand(uint8 GripID)
{
	FBPActorGripInformation* GripToUse = nullptr;
	if (GripID != INVALID_VRGRIP_ID)
	{
		GripToUse = GrippedObjects.FindByKey(GripID);
		if (!GripToUse)
		{
			GripToUse = LocallyGrippedObjects.FindByKey(GripID);
		}

		if (GripToUse)
		{
			GripToUse->bIsLerping = false;

			if (bConstrainToPivot)
			{
				DestroyPhysicsHandle(*GripToUse, false);
				SetUpPhysicsHandle(*GripToUse);
			}

			GripToUse->CurrentLerpTime = 0.0f;
			OnLerpToHandFinished.Broadcast(*GripToUse);
		}
	}
}

void UGripMotionControllerComponent::NotifyDrop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate)
{
	// Don't do this if we are the owning player on a local grip, there is no filter for multicast to not send to owner
	if ((NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive || 
		NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) && 
		IsLocallyControlled() && 
		!IsServer())
	{
		// If we still have the grip then the server is asking us to drop it even though it is locally controlled
		if (FBPActorGripInformation * GripInfo = GetGripPtrByID(NewDrop.GripID))
		{
			DropGrip_Implementation(*GripInfo, bSimulate, FVector::ZeroVector, FVector::ZeroVector, true);
		}

		return;
	}

	Drop_Implementation(NewDrop, bSimulate);
}

void UGripMotionControllerComponent::Drop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate)
{
	bool bSkipFullDrop = false;
	bool bHadAnotherSelfGrip = false;
	TArray<FBPGripPair> HoldingControllers;
	bool bIsHeld = false;

	// Check if a different controller is holding it
	if(NewDrop.GrippedObject && NewDrop.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		IVRGripInterface::Execute_IsHeld(NewDrop.GrippedObject, HoldingControllers, bIsHeld);

	if (bIsHeld && (!HoldingControllers.Contains(this) || HoldingControllers.Num() > 1))
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
				bHadAnotherSelfGrip = true;
			}
		}
		for (int i = 0; i < GrippedObjects.Num(); ++i)
		{
			if (GrippedObjects[i].GrippedObject == NewDrop.GrippedObject && GrippedObjects[i].GripID != NewDrop.GripID)
			{
				bSkipFullDrop = true;
				bHadAnotherSelfGrip = true;
			}
		}
	}

	DestroyPhysicsHandle(NewDrop, bHadAnotherSelfGrip);

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

				if (NewDrop.GripCollisionType != EGripCollisionType::EventsOnly)
				{
					if (APawn * OwningPawn = Cast<APawn>(GetOwner()))
					{
						OwningPawn->MoveIgnoreActorRemove(pActor);

						// Clearing owner out here
						// Now I am setting the owner to the owning pawn if we are one
						// This makes sure that some special replication needs are taken care of
						// Only doing this for actor grips
						// #TODO: Add the removal back in?
						//pActor->SetOwner(nullptr);
					}
				}

				if (root)
				{

					if (NewDrop.GripCollisionType == EGripCollisionType::AttachmentGrip && (HasGripAuthority(NewDrop) || IsServer()))
						root->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

					//root->IgnoreActorWhenMoving(this->GetOwner(), false);

					if (NewDrop.GripCollisionType != EGripCollisionType::EventsOnly)
					{
						if (IsServer() || bHadGripAuthority || !NewDrop.bOriginalReplicatesMovement || !pActor->GetIsReplicated())
						{
							if (!NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings || !NewDrop.AdvancedGripSettings.PhysicsSettings.bSkipSettingSimulating)
							{
								if (root->IsSimulatingPhysics() != bSimulate)
								{
									root->SetSimulatePhysics(bSimulate);
								}

								if (bSimulate)
									root->WakeAllRigidBodies();
							}
						}

						root->UpdateComponentToWorld(); // This fixes the late update offset
					}

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

					if (NewDrop.GripCollisionType != EGripCollisionType::EventsOnly)
					{
						if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
							(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
							root->SetEnableGravity(NewDrop.bOriginalGravity);
					}
				}
			}

			if (IsServer() && !bSkipFullDrop)
			{
				pActor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);
			}

			if (pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_SetHeld(pActor, this, NewDrop.GripID, false);

				if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment || SecondaryGripIDs.Contains(NewDrop.GripID))
				{
					IVRGripInterface::Execute_OnSecondaryGripRelease(pActor, this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);
					OnSecondaryGripRemoved.Broadcast(NewDrop);
				}

				SecondaryGripIDs.Remove(NewDrop.GripID);

				TArray<UVRGripScriptBase*> GripScripts;
				if (IVRGripInterface::Execute_GetGripScripts(pActor, GripScripts))
				{
					for (UVRGripScriptBase* Script : GripScripts)
					{
						if (Script)
						{
							if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
								Script->OnSecondaryGripRelease(this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

							Script->OnGripRelease(this, NewDrop, false);
						}
					}
				}

				IVRGripInterface::Execute_OnGripRelease(pActor, this, NewDrop, false);
				if (IVRGripInterface* GripInterface = Cast<IVRGripInterface>(pActor))
				{
					//GripInterface->Execute_OnGripRelease(pActor, this, NewDrop, false);
					GripInterface->Native_NotifyThrowGripDelegates(this, false, NewDrop, false);
				}
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

				//root->IgnoreActorWhenMoving(this->GetOwner(), false);

				if (NewDrop.GripCollisionType != EGripCollisionType::EventsOnly)
				{
					// Need to set simulation in all of these cases, including if it isn't the root component (simulation isn't replicated on non roots)
					if (IsServer() || bHadGripAuthority || !NewDrop.bOriginalReplicatesMovement || (pActor && (pActor->GetRootComponent() != root || !pActor->GetIsReplicated())))
					{
						if (!NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings || !NewDrop.AdvancedGripSettings.PhysicsSettings.bSkipSettingSimulating)
						{
							if (root->IsSimulatingPhysics() != bSimulate)
							{
								root->SetSimulatePhysics(bSimulate);
							}

							if (bSimulate)
								root->WakeAllRigidBodies();
						}
					}

					root->UpdateComponentToWorld(); // This fixes the late update offset
				}
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

				if (NewDrop.GripCollisionType != EGripCollisionType::EventsOnly)
				{
					if ((NewDrop.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewDrop.AdvancedGripSettings.PhysicsSettings.bTurnOffGravityDuringGrip) ||
						(NewDrop.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !IsServer()))
						root->SetEnableGravity(NewDrop.bOriginalGravity);
				}
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

			if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_SetHeld(root, this, NewDrop.GripID, false);

				if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment || SecondaryGripIDs.Contains(NewDrop.GripID))
				{
					IVRGripInterface::Execute_OnSecondaryGripRelease(root, this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);
					OnSecondaryGripRemoved.Broadcast(NewDrop);
				}

				SecondaryGripIDs.Remove(NewDrop.GripID);

				TArray<UVRGripScriptBase*> GripScripts;
				if (IVRGripInterface::Execute_GetGripScripts(root, GripScripts))
				{
					for (UVRGripScriptBase* Script : GripScripts)
					{
						if (Script)
						{
							if (NewDrop.SecondaryGripInfo.bHasSecondaryAttachment)
								Script->OnSecondaryGripRelease(this, NewDrop.SecondaryGripInfo.SecondaryAttachment, NewDrop);

							Script->OnGripRelease(this, NewDrop, false);
						}
					}
				}

				IVRGripInterface::Execute_OnGripRelease(root, this, NewDrop, false);
				if (IVRGripInterface* GripInterface = Cast<IVRGripInterface>(root))
				{
					GripInterface->Native_NotifyThrowGripDelegates(this, false, NewDrop, false);
				}

			}

			// Call on child grip release on attached parent component
			if (root->GetAttachParent() && root->GetAttachParent()->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				IVRGripInterface::Execute_OnChildGripRelease(root->GetAttachParent(), this, NewDrop, false);
			}
		}
	}break;
	}


	switch (NewDrop.GripMovementReplicationSetting)
	{
	case EGripMovementReplicationSettings::ForceClientSideMovement:
	case EGripMovementReplicationSettings::ClientSide_Authoritive:
	case EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep:
	{
		if (NewDrop.GripCollisionType != EGripCollisionType::EventsOnly)
		{
			if (root)
			{
				// #TODO: This is a hack until Epic fixes their new physics replication code
				//		  It forces the replication target to null on grip if we aren't repping movement.
				if (UWorld * World = GetWorld())
				{
					if (FPhysScene * PhysScene = World->GetPhysicsScene())
					{
						if (IPhysicsReplication* PhysicsReplication = PhysScene->GetPhysicsReplication())
						{
							FBodyInstance* BI = root->GetBodyInstance(NewDrop.GrippedBoneName);
							if (BI && BI->IsInstanceSimulatingPhysics())
							{
								PhysicsReplication->RemoveReplicatedTarget(root);
								//PhysicsReplication->SetReplicatedTarget(this, BoneName, UpdatedState);
							}
						}
					}
				}
			}
		}

	}break;

	};

	// Copy over the information instead of working with a reference for the OnDroppedBroadcast
	FBPActorGripInformation DropBroadcastData = NewDrop;

	int fIndex = 0;
	if (LocallyGrippedObjects.Find(NewDrop, fIndex))
	{
		DIRTY_LOCALLY_GRIPPED_OBJECTS();

		if (HasGripAuthority(NewDrop) || IsServer())
		{
			LocallyGrippedObjects.RemoveAt(fIndex);
		}
		else
		{
			LocallyGrippedObjects[fIndex].bIsPendingKill = true;
			LocallyGrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
		}
	}
	else
	{
		fIndex = 0;
		if (GrippedObjects.Find(NewDrop, fIndex))
		{
			DIRTY_GRIPPED_OBJECTS();
			if (HasGripAuthority(NewDrop) || IsServer())
			{
				GrippedObjects.RemoveAt(fIndex);
			}
			else
			{
				GrippedObjects[fIndex].bIsPendingKill = true;
				GrippedObjects[fIndex].bIsPaused = true; // Pause it instead of dropping, dropping can corrupt the array in rare cases
			}
		}
	}

	// Broadcast a new drop
	OnDroppedObject.Broadcast(DropBroadcastData, false);


	// Now check if we should turn off any post physics ticking
	if (EndPhysicsTickFunction.IsTickFunctionRegistered())
	{
		bool bNeedsPhysicsTick = false;

		if (LocallyGrippedObjects.Num() > 0 || GrippedObjects.Num() > 0)
		{
			if (bProjectNonSimulatingGrips)
			{
				bNeedsPhysicsTick = true;
			}
			else
			{

				for (int i = 0; i < LocallyGrippedObjects.Num(); ++i)
				{
					if (IsValid(LocallyGrippedObjects[i].GrippedObject) && LocallyGrippedObjects[i].GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
					{
						if (LocallyGrippedObjects[i].GripCollisionType != EGripCollisionType::CustomGrip && LocallyGrippedObjects[i].GripCollisionType != EGripCollisionType::EventsOnly)
						{
							EGripInterfaceTeleportBehavior TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(LocallyGrippedObjects[i].GrippedObject);
							if (TeleportBehavior == EGripInterfaceTeleportBehavior::DeltaTeleportation)
							{
								bNeedsPhysicsTick = true;
								break;
							}
						}
					}
				}

				if (!bNeedsPhysicsTick)
				{
					for (int i = 0; i < GrippedObjects.Num(); ++i)
					{
						if (IsValid(GrippedObjects[i].GrippedObject) && GrippedObjects[i].GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
						{
							if (GrippedObjects[i].GripCollisionType != EGripCollisionType::CustomGrip && GrippedObjects[i].GripCollisionType != EGripCollisionType::EventsOnly)
							{
								EGripInterfaceTeleportBehavior TeleportBehavior = IVRGripInterface::Execute_TeleportBehavior(GrippedObjects[i].GrippedObject);
								if (TeleportBehavior == EGripInterfaceTeleportBehavior::DeltaTeleportation)
								{
									bNeedsPhysicsTick = true;
									break;
								}
							}
						}
					}
				}
			}
		}

		if (!bNeedsPhysicsTick)
		{
			RegisterEndPhysicsTick(false);
		}
	}
}

bool UGripMotionControllerComponent::BP_IsLocallyControlled()
{
	return IsLocallyControlled();
}

bool UGripMotionControllerComponent::BP_HasGripAuthority(const FBPActorGripInformation &Grip)
{
	return HasGripAuthority(Grip);
}

bool UGripMotionControllerComponent::BP_HasGripAuthorityForObject(const UObject * ObjToCheck)
{
	return HasGripAuthority(ObjToCheck);
}

bool UGripMotionControllerComponent::BP_HasGripMovementAuthority(const FBPActorGripInformation &Grip)
{
	return HasGripMovementAuthority(Grip);
}

bool UGripMotionControllerComponent::AddSecondaryAttachmentPoint(UObject * GrippedObjectToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform & OriginalTransform, bool bTransformIsAlreadyRelative, float LerpToTime,/* float SecondarySmoothingScaler,*/ bool bIsSlotGrip, FName SecondarySlotName)
{
	if (!GrippedObjectToAddAttachment || !SecondaryPointComponent || (!GrippedObjects.Num() && !LocallyGrippedObjects.Num()))
		return false;

	FBPActorGripInformation * GripToUse = nullptr;

	GripToUse = LocallyGrippedObjects.FindByKey(GrippedObjectToAddAttachment);

	// Search replicated grips if not found in local
	if (!GripToUse)
	{
		// Replicated grips need to be called from server side
		if (!IsServer())
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called on the client side with a replicated grip"));
			return false;
		}

		GripToUse = GrippedObjects.FindByKey(GrippedObjectToAddAttachment);
	}

	if (GripToUse)
	{
		return AddSecondaryAttachmentToGrip(*GripToUse, SecondaryPointComponent, OriginalTransform, bTransformIsAlreadyRelative, LerpToTime, bIsSlotGrip, SecondarySlotName);
	}

	return false;
}

bool UGripMotionControllerComponent::AddSecondaryAttachmentToGripByID(const uint8 GripID, USceneComponent* SecondaryPointComponent, const FTransform& OriginalTransform, bool bTransformIsAlreadyRelative, float LerpToTime, bool bIsSlotGrip, FName SecondarySlotName)
{
	FBPActorGripInformation* GripToUse = nullptr;
	if (GripID != INVALID_VRGRIP_ID)
	{
		GripToUse = GrippedObjects.FindByKey(GripID);
		if (!GripToUse)
		{
			GripToUse = LocallyGrippedObjects.FindByKey(GripID);
		}

		if (GripToUse)
		{
			return AddSecondaryAttachmentToGrip(*GripToUse, SecondaryPointComponent, OriginalTransform, bTransformIsAlreadyRelative, LerpToTime, bIsSlotGrip, SecondarySlotName);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::AddSecondaryAttachmentToGrip(const FBPActorGripInformation & GripToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform &OriginalTransform, bool bTransformIsAlreadyRelative, float LerpToTime, bool bIsSlotGrip, FName SecondarySlotName)
{
	if (!SecondaryPointComponent)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called with a bad secondary component target!"));
		return false;
	}

	FBPActorGripInformation* GripToUse = nullptr;
	bool bWasLocal = false;
	if (GripToAddAttachment.GrippedObject && GripToAddAttachment.GripID != INVALID_VRGRIP_ID)
	{
		GripToUse = GrippedObjects.FindByKey(GripToAddAttachment.GripID);
		if (!GripToUse)
		{
			GripToUse = LocallyGrippedObjects.FindByKey(GripToAddAttachment.GripID);
			bWasLocal = true;
		}
	}

	if (!GripToUse || GripToUse->GripID == INVALID_VRGRIP_ID)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called with a bad grip! It was not valid / found."));
		return false;
	}

	if (!GripToUse->GrippedObject)
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called with a bad grip (gripped object invalid)!"));
		return false;
	}

	// Replicated grips need to be called from server side
	if (!bWasLocal && !IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called on the client side with a replicated grip"));
		return false;
	}

	bool bGrippedObjectIsInterfaced = GripToUse->GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass());

	if (bGrippedObjectIsInterfaced)
	{
		ESecondaryGripType SecondaryType = IVRGripInterface::Execute_SecondaryGripType(GripToUse->GrippedObject);

		if (SecondaryType == ESecondaryGripType::SG_None)
		{
			UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController add secondary attachment function was called on an interface object set to SG_None!"));
			return false;
		}
	}

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
	GripToUse->SecondaryGripInfo.SecondarySlotName = SecondarySlotName;

	/*const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
	GripToUse->AdvancedGripSettings.SecondaryGripSettings.SecondarySmoothing.CutoffSlope = VRSettings.OneEuroCutoffSlope;
	GripToUse->AdvancedGripSettings.SecondaryGripSettings.SecondarySmoothing.DeltaCutoff = VRSettings.OneEuroDeltaCutoff;
	GripToUse->AdvancedGripSettings.SecondaryGripSettings.SecondarySmoothing.MinCutoff = VRSettings.OneEuroMinCutoff;

	GripToUse->AdvancedGripSettings.SecondaryGripSettings.SecondarySmoothing.ResetSmoothingFilter();*/
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

	if (bGrippedObjectIsInterfaced)
	{
		SecondaryGripIDs.Add(GripToUse->GripID);

		IVRGripInterface::Execute_OnSecondaryGrip(GripToUse->GrippedObject, this, SecondaryPointComponent, *GripToUse);

		TArray<UVRGripScriptBase*> GripScripts;
		if (IVRGripInterface::Execute_GetGripScripts(GripToUse->GrippedObject, GripScripts))
		{
			for (UVRGripScriptBase* Script : GripScripts)
			{
				if (Script)
				{
					Script->OnSecondaryGrip(this, SecondaryPointComponent, *GripToUse);
				}
			}
		}
	}

	if (GripToUse->GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive && !IsServer() && !IsTornOff())
	{
		Server_NotifySecondaryAttachmentChanged(GripToUse->GripID, GripToUse->SecondaryGripInfo);
	}

	if (bWasLocal)
	{
		DIRTY_LOCALLY_GRIPPED_OBJECTS();
	}
	else
	{
		DIRTY_GRIPPED_OBJECTS();
	}

	OnSecondaryGripAdded.Broadcast(*GripToUse);
	GripToUse = nullptr;

	return true;
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
		return RemoveSecondaryAttachmentFromGrip(*GripToUse, LerpToTime);
	}

	return false;
}
bool UGripMotionControllerComponent::RemoveSecondaryAttachmentFromGripByID(const uint8 GripID, float LerpToTime)
{
	FBPActorGripInformation* GripToUse = nullptr;
	if (GripID != INVALID_VRGRIP_ID)
	{
		GripToUse = GrippedObjects.FindByKey(GripID);
		if (!GripToUse)
		{
			GripToUse = LocallyGrippedObjects.FindByKey(GripID);
		}

		if (GripToUse)
		{
			return RemoveSecondaryAttachmentFromGrip(*GripToUse, LerpToTime);
		}
	}

	return false;
}

bool UGripMotionControllerComponent::RemoveSecondaryAttachmentFromGrip(const FBPActorGripInformation & GripToRemoveAttachment, float LerpToTime)
{
	FBPActorGripInformation* GripToUse = nullptr;
	bool bWasLocal = false;
	if (GripToRemoveAttachment.GrippedObject && GripToRemoveAttachment.GripID != INVALID_VRGRIP_ID)
	{
		GripToUse = GrippedObjects.FindByKey(GripToRemoveAttachment.GripID);
		if (!GripToUse)
		{
			GripToUse = LocallyGrippedObjects.FindByKey(GripToRemoveAttachment.GripID);
			bWasLocal = true;
		}
	}

	if (GripToUse && !bWasLocal && !IsServer())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("VRGripMotionController remove secondary attachment function was called on the client side for a replicating grip"));
		return false;
	}

	// Handle the grip if it was found
	if (GripToUse && GripToUse->GrippedObject && GripToUse->GripID != INVALID_VRGRIP_ID)
	{
		SecondaryGripIDs.Remove(GripToUse->GripID);

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

		bool bGripObjectHasInterface = GripToUse->GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass());

		ESecondaryGripType SecondaryType = ESecondaryGripType::SG_None;
		if (bGripObjectHasInterface)
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
				GripToUse->RelativeTransform = primComp->GetComponentTransform().GetRelativeTransform(GetPivotTransform());
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

		if (bGripObjectHasInterface)
		{
			IVRGripInterface::Execute_OnSecondaryGripRelease(GripToUse->GrippedObject, this, GripToUse->SecondaryGripInfo.SecondaryAttachment, *GripToUse);

			TArray<UVRGripScriptBase*> GripScripts;
			if (IVRGripInterface::Execute_GetGripScripts(GripToUse->GrippedObject, GripScripts))
			{
				for (UVRGripScriptBase* Script : GripScripts)
				{
					if (Script)
					{
						Script->OnSecondaryGripRelease(this, GripToUse->SecondaryGripInfo.SecondaryAttachment, *GripToUse);
					}
				}
			}
		}

		GripToUse->SecondaryGripInfo.SecondaryAttachment = nullptr;
		GripToUse->SecondaryGripInfo.bHasSecondaryAttachment = false;

		if (GripToUse->GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive && !IsServer())
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
				if (!IsTornOff())
					Server_NotifySecondaryAttachmentChanged_Retain(GripToUse->GripID, GripToUse->SecondaryGripInfo, GripToUse->RelativeTransform);
			}break;
			default:
			{
				if (!IsTornOff())
					Server_NotifySecondaryAttachmentChanged(GripToUse->GripID, GripToUse->SecondaryGripInfo);
			}break;
			}

		}

		if (bWasLocal)
		{
			DIRTY_LOCALLY_GRIPPED_OBJECTS();
		}
		else
		{
			DIRTY_GRIPPED_OBJECTS();
		}

		SecondaryGripIDs.Remove(GripToUse->GripID);
		OnSecondaryGripRemoved.Broadcast(*GripToUse);
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
		GripInfo = GrippedObjects.FindByKey(GrippedActorToMove);

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
		GripInfo = GrippedObjects.FindByKey(ComponentToMove);

	if (GripInfo)
	{
		return TeleportMoveGrip(*GripInfo, bTeleportPhysicsGrips);
	}

	return false;
}

void UGripMotionControllerComponent::TeleportMoveGrips(bool bTeleportPhysicsGrips, bool bIsForPostTeleport)
{
	FTransform EmptyTransform = FTransform::Identity;
	for (FBPActorGripInformation& GripInfo : LocallyGrippedObjects)
	{
		TeleportMoveGrip_Impl(GripInfo, bTeleportPhysicsGrips, bIsForPostTeleport, EmptyTransform);
	}

	for (FBPActorGripInformation& GripInfo : GrippedObjects)
	{
		TeleportMoveGrip_Impl(GripInfo, bTeleportPhysicsGrips, bIsForPostTeleport, EmptyTransform);
	}
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

	// Check if either implements the interface
	bool bRootHasInterface = false;
	bool bActorHasInterface = false;

	switch (Grip.GripTargetType)
	{
	case EGripTargetType::ActorGrip:
	//case EGripTargetType::InteractibleActorGrip:
	{
		actor = Grip.GetGrippedActor();
		if (actor)
		{
			PrimComp = Cast<UPrimitiveComponent>(actor->GetRootComponent());
			if (actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				bActorHasInterface = true;
			}
		}
	}break;

	case EGripTargetType::ComponentGrip:
	//case EGripTargetType::InteractibleComponentGrip:
	{
		PrimComp = Grip.GetGrippedComponent();

		if (PrimComp)
		{
			actor = PrimComp->GetOwner();
			if (PrimComp->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				bRootHasInterface = true;
			}
		}

	}break;

	}

	if (!PrimComp || !actor || !IsValid(actor) || !IsValid(PrimComp))
		return false;

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
				DropObjectByInterface(nullptr, Grip.GripID);
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

	// We don't teleport these two grip types at all
	if (Grip.GripCollisionType == EGripCollisionType::AttachmentGrip || Grip.GripCollisionType == EGripCollisionType::EventsOnly)
	{
		return false;
	}

	FTransform WorldTransform;
	FTransform ParentTransform = GetPivotTransform();

	FBPActorGripInformation copyGrip = Grip;
	
	if (!OptionalTransform.Equals(FTransform::Identity))
	{
		WorldTransform = OptionalTransform;
	}
	else
	{
		TArray<UVRGripScriptBase*> Scripts;

		if (bRootHasInterface)
		{
			IVRGripInterface::Execute_GetGripScripts(PrimComp, Scripts);
		}
		else if (bActorHasInterface)
		{
			IVRGripInterface::Execute_GetGripScripts(actor, Scripts);
		}

		bool bForceADrop = false;
		bool bHadValidWorldTransform = GetGripWorldTransform(Scripts, 0.0f, WorldTransform, ParentTransform, copyGrip, actor, PrimComp, bRootHasInterface, bActorHasInterface, true, bForceADrop);
	
		if (!bHadValidWorldTransform)
			return false;
	}

	if (!WorldTransform.IsValid())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("Something went wrong, TeleportGrip_Impl's target transform contained NAN."));
		return false;
	}
	
	// Saving this out prior as we are still setting our physics thread to the correct value, the delta is only applied to the object
	FTransform physicsTrans = WorldTransform;
	if (TeleportBehavior == EGripInterfaceTeleportBehavior::DeltaTeleportation && !Grip.LastWorldTransform.Equals(FTransform::Identity))
	{
		FTransform baseTrans = this->GetAttachParent()->GetComponentTransform();
		WorldTransform = Grip.LastWorldTransform * baseTrans;

		//physicsTrans = WorldTransform;

		// Cancel out all other holding controllers teleport operations, we hit first
		if (!Grip.bSkipNextTeleportCheck && (bRootHasInterface || bActorHasInterface))
		{
			TArray<FBPGripPair> HoldingControllers;
			bool bIsHeld = false;
			IVRGripInterface::Execute_IsHeld(Grip.GrippedObject, HoldingControllers, bIsHeld);

			for (FBPGripPair pair : HoldingControllers)
			{
				if (pair.HoldingController && pair.HoldingController != this && pair.HoldingController->bIsPostTeleport)
				{
					FBPActorGripInformation* pGrip = pair.HoldingController->GetGripPtrByID(pair.GripID);

					if (pGrip)
					{
						pGrip->bSkipNextTeleportCheck = true;
					}
				}
			}
		}
	}

	// Run some error checks and logging against the resulting transform
	if (!WorldTransform.IsValid())
	{
		if (WorldTransform.ContainsNaN())
		{
			UE_LOG(LogVRMotionController, Error, TEXT("Failed to teleport grip, bad transform, NaN detected with object: %s"), *Grip.GrippedObject->GetName());
			return false;
		}
		else if (!WorldTransform.GetRotation().IsNormalized())
		{
			WorldTransform.NormalizeRotation();

			if (!WorldTransform.IsValid())
			{
				UE_LOG(LogVRMotionController, Error, TEXT("Failed to teleport grip, bad transform, rotation normalization issue: %s"), *Grip.GrippedObject->GetName());
				return false;
			}
			else
			{
				UE_LOG(LogVRMotionController, Error, TEXT("Error during teleport grip, rotation not normalized for object: %s"), *Grip.GrippedObject->GetName());
			}
		}
	}

	// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
	FBPActorPhysicsHandleInformation * Handle = GetPhysicsGrip(Grip);

	if (!Handle)
	{
		PrimComp->SetWorldTransform(WorldTransform, bSweepGripTeleports, nullptr, ETeleportType::TeleportPhysics);
	}
	else if (Handle && FPhysicsInterface::IsValid(Handle->KinActorData2) && bTeleportPhysicsGrips)
	{

		// Don't try to autodrop on next tick, let the physx constraint update its local frame first
		if (HasGripAuthority(Grip))
			Grip.bSkipNextConstraintLengthCheck = true;

		if (Grip.bSkipNextTeleportCheck)
		{
			Grip.bSkipNextTeleportCheck = false;
		}
		else
		{
			PrimComp->SetWorldTransform(WorldTransform, bSweepGripTeleports, nullptr, ETeleportType::TeleportPhysics);
		}

		// Zero out our scale now that we are working outside of physx
		physicsTrans.SetScale3D(FVector(1.0f));

		if (Grip.bIsLerping || !bConstrainToPivot)
		{
			FBodyInstance* pInstance = PrimComp->GetBodyInstance();
			FPhysicsActorHandle ActorHandle = Handle->KinActorData2;
			FTransform newTrans = Handle->COMPosition * (Handle->RootBoneRotation * physicsTrans);
			if (pInstance && pInstance->IsValidBodyInstance())
			{
				if (FPhysScene* PhysicalScene = pInstance->GetPhysicsScene())
				{
					//FPhysicsCommand::ExecuteWrite(ActorHandle, [&](const FPhysicsActorHandle& Actor)
					FPhysicsCommand::ExecuteWrite(PhysicalScene, [&]()
					{
						if (FPhysicsInterface::IsValid(ActorHandle))
						{
							FPhysicsInterface::SetKinematicTarget_AssumesLocked(ActorHandle, newTrans);
							FPhysicsInterface::SetGlobalPose_AssumesLocked(ActorHandle, newTrans);
						}
					});
				}
			}
		}
	}

	// TMP #TODO: Remove with 5.4 bug with velocity is fixed
	// Reset our last world transform velocity as typically we don't want to accumulate teleport speeds into it.
	Grip.LastVelWorldTrans = PrimComp->GetComponentTransform();

	return true;
}

void UGripMotionControllerComponent::PostTeleportMoveGrippedObjects()
{
	if (!GrippedObjects.Num() && !LocallyGrippedObjects.Num())
		return;

	this->bIsPostTeleport = true;
}


void UGripMotionControllerComponent::Deactivate()
{
	Super::Deactivate();

	if (IsActive() == false && GripViewExtension.IsValid())
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

void UGripMotionControllerComponent::OnAttachmentChanged()
{
	if (AVRCharacter* CharacterOwner = Cast<AVRCharacter>(this->GetOwner()))
	{
		AttachChar = CharacterOwner;
	}
	else
	{
		AttachChar = nullptr;
	}

	Super::OnAttachmentChanged();
}

void UGripMotionControllerComponent::OnRep_ReplicatedControllerTransform()
{
	//ReplicatedControllerTransform.Unpack();

	if (IsServer() && HasTrackingParameters())
	{
		// Ensure that the client is sending valid boundries
		ApplyTrackingParameters(ReplicatedControllerTransform.Position, true, false);
	}

	if (bSmoothReplicatedMotion)
	{
		if (bReppedOnce)
		{
			bLerpingPosition = true;
			ControllerNetUpdateCount = 0.0f;
			LastUpdatesRelativePosition = this->GetRelativeLocation();
			LastUpdatesRelativeRotation = this->GetRelativeRotation();

			if (bUseExponentialSmoothing)
			{
				FVector OldToNewVector = ReplicatedControllerTransform.Position - LastUpdatesRelativePosition;
				float NewDistance = OldToNewVector.SizeSquared();

				// Too far, snap to the new value
				if (NewDistance >= FMath::Square(NetworkNoSmoothUpdateDistance))
				{
					SetRelativeLocationAndRotation(ReplicatedControllerTransform.Position, ReplicatedControllerTransform.Rotation);
					bLerpingPosition = false;
				}
				// Outside of the buffer distance, snap within buffer and keep smoothing from there
				else if (NewDistance >= FMath::Square(NetworkMaxSmoothUpdateDistance))
				{
					FVector Offset = (OldToNewVector.Size() - NetworkMaxSmoothUpdateDistance) * OldToNewVector.GetSafeNormal();
					SetRelativeLocation(LastUpdatesRelativePosition + Offset);
				}
			}
		}
		else
		{
			SetRelativeLocationAndRotation(ReplicatedControllerTransform.Position, ReplicatedControllerTransform.Rotation);
			bReppedOnce = true;
		}
	}
	else
		SetRelativeLocationAndRotation(ReplicatedControllerTransform.Position, ReplicatedControllerTransform.Rotation);
}

void UGripMotionControllerComponent::UpdateTracking(float DeltaTime)
{
	// Server/remote clients don't set the controller position in VR
	// Don't call positional checks and don't create the late update scene view
	if (bHasAuthority)
	{
		if (bOffsetByControllerProfile && !NewControllerProfileEvent_Handle.IsValid())
		{
			GetCurrentProfileTransform(true);
		}

		FVector Position = GetRelativeLocation();
		FRotator Orientation = GetRelativeRotation();

		if (!bUseWithoutTracking)
		{
			if (!GripViewExtension.IsValid() && GEngine)
			{
				GripViewExtension = FSceneViewExtensions::NewExtension<FGripViewExtension>(this);
			}
			
			float WorldToMeters = GetWorld() ? GetWorld()->GetWorldSettings()->WorldToMeters : 100.0f;
			ETrackingStatus LastTrackingStatus = CurrentTrackingStatus;
			const bool bNewTrackedState = GripPollControllerState_GameThread(Position, Orientation, bProvidedLinearVelocity, LinearVelocity, bProvidedAngularVelocity, AngularVelocityAsAxisAndLength, bProvidedLinearAcceleration, LinearAcceleration, WorldToMeters);

			// if controller tracking just kicked in or we haven't started rendering in the (possibly present) 
			// visualization component.
			if (!bTracked && bNewTrackedState)
			{
				OnActivateVisualizationComponent.Broadcast(true);
			}


			bTracked = bNewTrackedState && (bIgnoreTrackingStatus || CurrentTrackingStatus != ETrackingStatus::NotTracked);
			if (bTracked)
			{
				if (bSmoothHandTracking)
				{
					FTransform CalcedTransform = FTransform(Orientation, Position, this->GetRelativeScale3D());
					
					if (bSmoothWithEuroLowPassFunction)
					{
						SetRelativeTransform(EuroSmoothingParams.RunFilterSmoothing(CalcedTransform, DeltaTime));
					}
					else
					{
						if (SmoothingSpeed <= 0.f || LastSmoothRelativeTransform.Equals(FTransform::Identity))
						{
							SetRelativeTransform(CalcedTransform);
							LastSmoothRelativeTransform = CalcedTransform;
						}
						else
						{
							const float Alpha = FMath::Clamp(DeltaTime * SmoothingSpeed, 0.f, 1.f);
							LastSmoothRelativeTransform.Blend(LastSmoothRelativeTransform, CalcedTransform, Alpha);
							SetRelativeTransform(LastSmoothRelativeTransform);
						}
					}

					bWasSmoothingHand = true;
				}
				else
				{
					if (bWasSmoothingHand)
					{
						// Clear the smoothing information so that we start with a fresh log when its enabled again
						LastSmoothRelativeTransform = FTransform::Identity;
						EuroSmoothingParams.ResetSmoothingFilter();

						bWasSmoothingHand = false;
					}

					SetRelativeTransform(FTransform(Orientation, Position, this->GetRelativeScale3D()));
				}
			}

			// if controller tracking just changed
			if (LastTrackingStatus != CurrentTrackingStatus)
			{
				OnTrackingChanged.Broadcast(CurrentTrackingStatus);
			}
		}

		if (!bTracked && !bUseWithoutTracking)
			return; // Don't update anything including location

		// Don't bother with any of this if not replicating transform
		if (GetIsReplicated() && (bTracked || bReplicateWithoutTracking))
		{
			FVector RelLoc = GetRelativeLocation();
			FRotator RelRot = GetRelativeRotation();

			// Don't rep if no changes
			if (!RelLoc.Equals(ReplicatedControllerTransform.Position) || !RelRot.Equals(ReplicatedControllerTransform.Rotation))
			{
				ControllerNetUpdateCount += DeltaTime;
				if (ControllerNetUpdateCount >= (1.0f / ControllerNetUpdateRate))
				{
					ControllerNetUpdateCount = 0.0f;

					// Tracked doesn't matter, already set the relative location above in that case
					ReplicatedControllerTransform.Position = RelLoc;
					ReplicatedControllerTransform.Rotation = RelRot;

#if WITH_PUSH_MODEL
					MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, ReplicatedControllerTransform, this);
#endif

					// I would keep the torn off check here, except this can be checked on tick if they
					// Set 100 htz updates, and in the TornOff case, it actually can't hurt any besides some small
					// Perf difference.
					if (!IsServer()/* && !IsTornOff()*/)
					{
						AVRBaseCharacter* OwningChar = Cast<AVRBaseCharacter>(GetOwner());
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
		// Clear the view extension if active after unpossessing, just in case
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

		// Run any networked smoothing
		RunNetworkedSmoothing(DeltaTime);
	}
}

void UGripMotionControllerComponent::RunNetworkedSmoothing(float DeltaTime)
{
	if (bLerpingPosition)
	{
		if (!bUseExponentialSmoothing)
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
		else // Exponential Smoothing
		{
			if (InterpolationSpeed <= 0.f)
			{
				SetRelativeLocationAndRotation((FVector)ReplicatedControllerTransform.Position, ReplicatedControllerTransform.Rotation);
				bLerpingPosition = false;
				return;
			}

			const float Alpha = FMath::Clamp(DeltaTime * InterpolationSpeed, 0.f, 1.f);

			FTransform NA = FTransform(GetRelativeRotation(), GetRelativeLocation(), FVector(1.0f));
			FTransform NB = FTransform(ReplicatedControllerTransform.Rotation, (FVector)ReplicatedControllerTransform.Position, FVector(1.0f));
			NA.NormalizeRotation();
			NB.NormalizeRotation();

			NA.Blend(NA, NB, Alpha);

			// If we are nearly equal then snap to final position
			if (NA.EqualsNoScale(NB))
			{
				SetRelativeLocationAndRotation(ReplicatedControllerTransform.Position, ReplicatedControllerTransform.Rotation);
				bLerpingPosition = false;
			}
			else // Else just keep going
			{
				SetRelativeLocationAndRotation(NA.GetTranslation(), NA.Rotator());
			}			
		}
	}
}

void UGripMotionControllerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Skip if we are traveling
	if (IsTravelingOrNullWorld())
		return;

	// Skip motion controller tick, we override a lot of things that it does and we don't want it to perform the same functions
	Super::Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!IsActive())
		return;

	// Moved this here instead of in the polling function, it was ticking once per frame anyway so no loss of perf
	// It doesn't need to be there and now I can pre-check
	// Also epics implementation in the polling function didn't work anyway as it was based off of playercontroller which is not the owner of this controller
	
	// Cache state from the game thread for use on the render thread
	// No need to check if in game thread here as tick always is
	bHasAuthority = IsLocallyControlled();

	// No longer updating in character, was a waste as it wouldn't scope this component anyway
	UpdateTracking(DeltaTime);

	/*if (!bUpdateInCharacterMovement)
	{
		UpdateTracking(DeltaTime);
	}
	else if (AttachChar.IsValid())
	{
		UCharacterMovementComponent* CharMove = AttachChar->GetCharacterMovement();
		if (!CharMove || !CharMove->IsComponentTickEnabled() || !CharMove->IsActive() || (GetWorld()->IsPaused() && !AttachChar->GetCharacterMovement()->PrimaryComponentTick.bTickEvenWhenPaused))
		{
			// Our character movement isn't handling our updates, lets do it ourself.
			UpdateTracking(DeltaTime);
		}
	}*/

	// Process the gripped actors
	TickGrip(DeltaTime);
}

bool UGripMotionControllerComponent::GetGripWorldTransform(TArray<UVRGripScriptBase*>& GripScripts, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport, bool &bForceADrop)
{
	SCOPE_CYCLE_COUNTER(STAT_GetGripTransform);

	bool bHasValidTransform = true;

	if (GripScripts.Num())
	{
		bool bGetDefaultTransform = true;

		// Get grip script world transform overrides (if there are any)
		for (UVRGripScriptBase* Script: GripScripts)
		{
			if (Script && Script->IsScriptActive() && Script->GetWorldTransformOverrideType() == EGSTransformOverrideType::OverridesWorldTransform)
			{
				// One of the grip scripts overrides the default transform
				bGetDefaultTransform = false;
				break;
			}
		}

		// If none of the scripts override the base transform
		if (bGetDefaultTransform && DefaultGripScript)
		{		
			bHasValidTransform = DefaultGripScript->CallCorrect_GetWorldTransform(this, DeltaTime, WorldTransform, ParentTransform, Grip, actor, root, bRootHasInterface, bActorHasInterface, bIsForTeleport);
			bForceADrop = DefaultGripScript->Wants_ToForceDrop();
		}

		// Get grip script world transform modifiers (if there are any)
		for (UVRGripScriptBase* Script : GripScripts)
		{
			if (Script && Script->IsScriptActive() && Script->GetWorldTransformOverrideType() != EGSTransformOverrideType::None)
			{
				bHasValidTransform = Script->CallCorrect_GetWorldTransform(this, DeltaTime, WorldTransform, ParentTransform, Grip, actor, root, bRootHasInterface, bActorHasInterface, bIsForTeleport);
				bForceADrop = Script->Wants_ToForceDrop();

				// Early out, one of the scripts is telling us that the transform isn't valid, something went wrong or the grip is flagged for drop
				if (!bHasValidTransform || bForceADrop)
					break;
			}
		}
	}
	else
	{
		if (DefaultGripScript)
		{
			bHasValidTransform = DefaultGripScript->CallCorrect_GetWorldTransform(this, DeltaTime, WorldTransform, ParentTransform, Grip, actor, root, bRootHasInterface, bActorHasInterface, bIsForTeleport);
			bForceADrop = DefaultGripScript->Wants_ToForceDrop();
		}
	}

	HandleGlobalLerpToHand(Grip, WorldTransform, DeltaTime);

	if (bHasValidTransform && !WorldTransform.IsValid())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("Something went wrong, GetGripWorldTransform tried to return NAN!."));
		bHasValidTransform = false;
	}

	return bHasValidTransform;
}

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

	FTransform ParentTransform = GetPivotTransform();

	// Check for floating server sided client auth grips and handle them if we need too
	if(!IsServer())
		CheckTransactionBuffer();

	bool bOriginalPostTeleport = bIsPostTeleport;

	// Split into separate functions so that I didn't have to combine arrays since I have some removal going on
	HandleGripArray(GrippedObjects, ParentTransform, DeltaTime, true);
	HandleGripArray(LocallyGrippedObjects, ParentTransform, DeltaTime);

	// Empty out the teleport flag, checking original state just in case the player changed it while processing bps
	if (bOriginalPostTeleport)
	{
		if ((GrippedObjects.Num() || LocallyGrippedObjects.Num()))
		{
			OnTeleportedGrips.Broadcast();
		}

		bIsPostTeleport = false;
	}

	// Save out the component velocity from this and last frame

	FVector newVelocitySample = ((bSampleVelocityInWorldSpace ? GetComponentLocation() : GetRelativeLocation()) - LastRelativePosition.GetTranslation()) / DeltaTime;

	switch (VelocityCalculationType)
	{
	case EVRVelocityType::VRLOCITY_Default:
	{
		ComponentVelocity = newVelocitySample;
	}break;
	case EVRVelocityType::VRLOCITY_RunningAverage:
	{
		UVRExpansionFunctionLibrary::LowPassFilter_RollingAverage(ComponentVelocity, newVelocitySample, ComponentVelocity, VelocitySamples);
	}break;
	case EVRVelocityType::VRLOCITY_SamplePeak:
	{
		if (PeakFilter.VelocitySamples != VelocitySamples)
			PeakFilter.VelocitySamples = VelocitySamples;
		UVRExpansionFunctionLibrary::UpdatePeakLowPassFilter(PeakFilter, newVelocitySample);
	}break;
	}

	// #TODO:
	// Relative angular velocity too?
	// Maybe add some running averaging here to make it work across frames?
	// Or Valves 30 frame high point average buffer
	LastRelativePosition = bSampleVelocityInWorldSpace ? this->GetComponentTransform() : this->GetRelativeTransform();
}

FVector UGripMotionControllerComponent::GetComponentVelocity() const
{
	if(VelocityCalculationType == EVRVelocityType::VRLOCITY_SamplePeak)
	{ 
		return PeakFilter.GetPeak();
	}

	return Super::GetComponentVelocity();
}

// TEMP: #TODO: Remove
void UGripMotionControllerComponent::CalculateGripVelocity(FBPActorGripInformation& GripToFill, UPrimitiveComponent* ComponentToSample, float DeltaTime)
{
	if (!bHasAuthority && !IsServer())
	{
		return;
	}

	FTransform CurTrans = ComponentToSample->GetComponentTransform();

	GripToFill.LinVel = (CurTrans.GetLocation() - GripToFill.LastVelWorldTrans.GetLocation()) / DeltaTime;
	GripToFill.RotVel = FVector::RadiansToDegrees(((CurTrans.GetRotation().ToRotationVector() - GripToFill.LastVelWorldTrans.GetRotation().ToRotationVector()))) / DeltaTime;

	GripToFill.LastVelWorldTrans = CurTrans;
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

			if (Grip->IsValid())
			{
				// Continue if the grip is paused
				if (Grip->bIsPaused)
					continue;

				if (Grip->GripCollisionType == EGripCollisionType::EventsOnly)
					continue; // Earliest safe spot to continue at, we needed to check if the object is pending kill or invalid first

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
				if (!root || !actor || !IsValid(root) || !IsValid(actor))
					continue;

				// Keep checking for pending kill on gripped objects, and ptr removals, but don't run grip logic when seamless
				// traveling, to avoid physx scene issues.
				if (GetWorld()->IsInSeamlessTravel())
				{
					continue;
				}

				// Check if either implements the interface
				bool bRootHasInterface = false;
				bool bActorHasInterface = false;
				
				if (root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					bRootHasInterface = true;
				}
				else if (actor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
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

					// TEMP 5.4 (or fixed)
					CalculateGripVelocity(*Grip, root, DeltaTime);
					continue;
				}

				bool bRescalePhysicsGrips = false;
				
				TArray<UVRGripScriptBase*> GripScripts;

				if (bRootHasInterface)
				{
					IVRGripInterface::Execute_GetGripScripts(root, GripScripts);
				}
				else if (bActorHasInterface)
				{
					IVRGripInterface::Execute_GetGripScripts(actor, GripScripts);
				}


				bool bForceADrop = false;

				// Get the world transform for this grip after handling secondary grips and interaction differences
				bool bHasValidWorldTransform = GetGripWorldTransform(GripScripts, DeltaTime, WorldTransform, ParentTransform, *Grip, actor, root, bRootHasInterface, bActorHasInterface, false, bForceADrop);

				// If a script or behavior is telling us to skip this and continue on (IE: it dropped the grip)
				if (bForceADrop)
				{
					if (HasGripAuthority(*Grip))
					{
						if (bRootHasInterface)
							DropGrip_Implementation(*Grip, IVRGripInterface::Execute_SimulateOnDrop(root));
						else if (bActorHasInterface)
							DropGrip_Implementation(*Grip, IVRGripInterface::Execute_SimulateOnDrop(actor));
						else
							DropGrip_Implementation(*Grip, true);
					}

					continue;
				}
				else if (!bHasValidWorldTransform)
				{
					continue;
				}
			
				if (Grip->GrippedBoneName == NAME_None && !root->GetComponentTransform().GetScale3D().Equals(WorldTransform.GetScale3D()))
					bRescalePhysicsGrips = true;

				// If we just teleported, skip this update and just teleport forward
				if (bIsPostTeleport)
				{

					bool bSkipTeleport = false;
					for (UVRGripScriptBase* Script : GripScripts)
					{
						if (Script && Script->IsScriptActive() && Script->Wants_DenyTeleport(this))
						{
							bSkipTeleport = true;
							break;
						}
					}

					
					if (!bSkipTeleport)
					{
						TeleportMoveGrip_Impl(*Grip, true, true, WorldTransform);
						continue;
					}
				}
				else
				{
					//Grip->LastWorldTransform = WorldTransform;
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
								bool bIgnoreDrop = false;
								for (UVRGripScriptBase* Script : GripScripts)
								{
									if (Script && Script->IsScriptActive() && Script->Wants_DenyAutoDrop())
									{
										bIgnoreDrop = true;
										break;
									}
								}

								if (bIgnoreDrop)
								{
									// Script canceled this out
								}
								else if (OnGripOutOfRange.IsBound())
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
									if(bRootHasInterface)
										DropGrip_Implementation(*Grip, IVRGripInterface::Execute_SimulateOnDrop(root));
									else
										DropGrip_Implementation(*Grip, IVRGripInterface::Execute_SimulateOnDrop(actor));

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
					case EGripCollisionType::LockedConstraint:
					{
						UpdatePhysicsHandleTransform(*Grip, WorldTransform);
						
						if (bRescalePhysicsGrips)
							root->SetWorldScale3D(WorldTransform.GetScale3D());


						// Sweep current collision state, only used for client side late update removal
						if (
							(bHasAuthority && !this->bDisableLowLatencyUpdate &&
								((Grip->GripLateUpdateSetting == EGripLateUpdateSettings::NotWhenColliding) ||
									(Grip->GripLateUpdateSetting == EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping)))
							)
						{
							//TArray<FOverlapResult> Hits;
							FComponentQueryParams Params(NAME_None, this->GetOwner());
							//Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
							Params.AddIgnoredActor(actor);
							Params.AddIgnoredActors(root->MoveIgnoreActors);

							actor->ForEachAttachedActors([&Params](AActor* Actor)
							{
								Params.AddIgnoredActor(Actor);
								return true;
							});

							TArray<FHitResult> Hits;
							
							// Switched over to component sweep because it picks up on pivot offsets without me manually calculating it
							if (
									GetWorld()->ComponentSweepMulti(Hits, root, root->GetComponentLocation(), WorldTransform.GetLocation(), WorldTransform.GetRotation(), Params)
								)
							{

								// Check if the two components are ignoring collisions with each other
								UCollisionIgnoreSubsystem* CollisionIgnoreSubsystem = GetWorld()->GetSubsystem<UCollisionIgnoreSubsystem>();
								
								if (CollisionIgnoreSubsystem->HasCollisionIgnorePairs())
								{
									// Pre-set this so it falls back to false if none of these hits are valid
									Grip->bColliding = false;

									for (const FHitResult& Hit : Hits)
									{
										if (Hit.bBlockingHit && !CollisionIgnoreSubsystem->AreComponentsIgnoringCollisions(root, Hit.Component.Get()))
										{
											Grip->bColliding = true;
											break;
										}
									}
								}
								else
								{
									if (FHitResult::GetFirstBlockingHit(Hits) != nullptr)
									{
										Grip->bColliding = true;
									}
								}
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
						if (bProjectNonSimulatingGrips && !Grip->bIsLocked && Grip->bSetLastWorldTransform)
						{
							FScopedMovementUpdate ScopedMovementUpdate(root, EScopedUpdate::DeferredUpdates);
							FTransform baseTrans = this->GetAttachParent()->GetComponentTransform();
							root->SetWorldTransform(Grip->LastWorldTransform * baseTrans, false, nullptr, ETeleportType::None);
							root->SetWorldTransform(WorldTransform, true, &OutHit);
						}
						else
						{
							root->SetWorldTransform(WorldTransform, true, &OutHit);
						}

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
						//Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);

						actor->ForEachAttachedActors([&Params](AActor* Actor)
						{
							Params.AddIgnoredActor(Actor);
							return true;
						});

						TArray<FHitResult> Hits;
						// Checking both current and next position for overlap using this grip type
						// Switched over to component sweep because it picks up on pivot offsets without me manually calculating it
						if (Grip->bLockHybridGrip)
						{
							if (!Grip->bColliding)
							{
								SetGripConstraintStiffnessAndDamping(Grip, false);
							}

							Grip->bColliding = true;
						}
						else if (GetWorld()->ComponentSweepMulti(Hits, root, root->GetComponentLocation(), WorldTransform.GetLocation(), WorldTransform.GetRotation(), Params) && FHitResult::GetFirstBlockingHit(Hits) != nullptr)
						{
							// Assume true by default, will revert if checking ignored below
							Grip->bColliding = true;

							// Check if the two components are ignoring collisions with each other
							UCollisionIgnoreSubsystem* CollisionIgnoreSubsystem = GetWorld()->GetSubsystem<UCollisionIgnoreSubsystem>();

							if (CollisionIgnoreSubsystem->HasCollisionIgnorePairs())
							{

								bool bOriginalColliding = Grip->bColliding;
								// Pre-set this so it falls back to false if none of these hits are valid
								Grip->bColliding = false;

								for (const FHitResult& Hit : Hits)
								{
									if (Hit.bBlockingHit && !CollisionIgnoreSubsystem->AreComponentsIgnoringCollisions(root, Hit.Component.Get()))
									{
										if (!bOriginalColliding)
										{
											SetGripConstraintStiffnessAndDamping(Grip, false);
										}
										Grip->bColliding = true;
										break;
									}
								}

								if (!Grip->bColliding)
								{
									if (bOriginalColliding)
									{
										SetGripConstraintStiffnessAndDamping(Grip, true);
									}
								}


							}
							else
							{
								if (!Grip->bColliding)
								{
									SetGripConstraintStiffnessAndDamping(Grip, false);
								}
								//Grip->bColliding = true;
							}
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

						TArray<FHitResult> Hits;
						FComponentQueryParams Params(NAME_None, this->GetOwner());
						//Params.bTraceAsyncScene = root->bCheckAsyncSceneOnMove;
						Params.AddIgnoredActor(actor);
						Params.AddIgnoredActors(root->MoveIgnoreActors);

						actor->ForEachAttachedActors([&Params](AActor* Actor)
						{
							Params.AddIgnoredActor(Actor);
							return true;
						});

						FTransform BaseTransform = root->GetComponentTransform();

						if (bProjectNonSimulatingGrips && !Grip->bColliding && Grip->bSetLastWorldTransform)
						{
							FTransform baseTrans = this->GetAttachParent()->GetComponentTransform();
							BaseTransform = Grip->LastWorldTransform * baseTrans;
						}
						
						bool bWasColliding = Grip->bColliding;
						bool bLerpCollisions = false;
						bool bLerpRotationOnly = false;
						bool bDistanceBasedInterpolation = false;
						float LerpSpeed = 0.0f;
						const UVRGlobalSettings* VRSettings = GetDefault<UVRGlobalSettings>();

						if (VRSettings)
						{
							bLerpCollisions = VRSettings->bLerpHybridWithSweepGrips;
							LerpSpeed = VRSettings->HybridWithSweepLerpDuration;
							bLerpRotationOnly = VRSettings->bOnlyLerpHybridRotation;
							bDistanceBasedInterpolation = VRSettings->bHybridWithSweepUseDistanceBasedLerp;
						}

						if (Grip->bLockHybridGrip)
						{
							Grip->bColliding = true;
						}
						// Check our target rotation
						else if (GetWorld()->ComponentSweepMulti(Hits, root, BaseTransform.GetLocation(), WorldTransform.GetLocation(), WorldTransform.GetRotation(), Params) && FHitResult::GetFirstBlockingHit(Hits) != nullptr)
						{
							// Assume true by default, will revert if checking ignored below
							Grip->bColliding = true;

							// Check if the two components are ignoring collisions with each other
							UCollisionIgnoreSubsystem* CollisionIgnoreSubsystem = GetWorld()->GetSubsystem<UCollisionIgnoreSubsystem>();
							if (CollisionIgnoreSubsystem->HasCollisionIgnorePairs())
							{
								// Pre-set this so it falls back to false if none of these hits are valid
								Grip->bColliding = false;

								for (const FHitResult& Hit : Hits)
								{
									if (Hit.bBlockingHit && !CollisionIgnoreSubsystem->AreComponentsIgnoringCollisions(root, Hit.Component.Get()))
									{
										Grip->bColliding = true;
										break;
									}
								}

								// We need to also check the other rotation here as a fallback
								if (bLerpCollisions && !Grip->bColliding)
								{
									if (bLerpCollisions && GetWorld()->ComponentSweepMulti(Hits, root, BaseTransform.GetLocation(), WorldTransform.GetLocation(), root->GetComponentRotation(), Params))
									{
										for (const FHitResult& Hit : Hits)
										{
											if (Hit.bBlockingHit && !CollisionIgnoreSubsystem->AreComponentsIgnoringCollisions(root, Hit.Component.Get()))
											{
												Grip->bColliding = true;
												break;
											}
										}
									}
								}
							}
						}
						// Check the other rotation
						else if (bLerpCollisions && GetWorld()->ComponentSweepMulti(Hits, root, BaseTransform.GetLocation(), WorldTransform.GetLocation(), root->GetComponentRotation(), Params) && FHitResult::GetFirstBlockingHit(Hits) != nullptr)
						{
							// Assume true by default, will revert if checking ignored below
							Grip->bColliding = true;

							// Check if the two components are ignoring collisions with each other
							UCollisionIgnoreSubsystem* CollisionIgnoreSubsystem = GetWorld()->GetSubsystem<UCollisionIgnoreSubsystem>();

							if (CollisionIgnoreSubsystem->HasCollisionIgnorePairs())
							{
								// Pre-set this so it falls back to false if none of these hits are valid
								Grip->bColliding = false;

								for (const FHitResult& Hit : Hits)
								{
									if (Hit.bBlockingHit && !CollisionIgnoreSubsystem->AreComponentsIgnoringCollisions(root, Hit.Component.Get()))
									{
										Grip->bColliding = true;
										break;
									}
								}
							}
						}
						else
						{
							Grip->bColliding = false;
						}

						if (!Grip->bColliding)
						{
							if (GripHandle && !GripHandle->bIsPaused)
							{
								PausePhysicsHandle(GripHandle);
								//DestroyPhysicsHandle(*Grip);

								switch (Grip->GripTargetType)
								{
								case EGripTargetType::ComponentGrip:
								{
									root->SetSimulatePhysics(false);
								}break;
								case EGripTargetType::ActorGrip:
								{
									root->SetSimulatePhysics(false);
									//actor->DisableComponentsSimulatePhysics();
								} break;
								}
							}

							if (bLerpCollisions && !Grip->bIsLerping)
							{
								if (bWasColliding && !Grip->bIsLerping)
								{
									// Store relative transform and base movements off of lerping out of it to the target transform

									// Re-use this transform as it will let us not add additional variables
									Grip->OnGripTransform = root->GetComponentTransform().GetRelativeTransform(this->GetPivotTransform());
									Grip->CurrentLerpTime = 1.0f;	
									Grip->LerpSpeed = (1.f / LerpSpeed);

									if (bDistanceBasedInterpolation)
									{
										// Just multiplying to make the values easier
										Grip->LerpSpeed *= 10.0f;
										Grip->CurrentLerpTime = LerpSpeed;
									}
								}

								if (Grip->CurrentLerpTime > 0.0f)
								{
									FTransform NB = (Grip->OnGripTransform * this->GetPivotTransform());
									float Alpha = 0.0f;

									if (bDistanceBasedInterpolation)
									{
										if (Grip->LerpSpeed <= 0.f)
										{
											Alpha = 1.0f;
											Grip->CurrentLerpTime = 0.0f;
										}
										else
										{
											Grip->CurrentLerpTime = FMath::Clamp(Grip->CurrentLerpTime - DeltaTime, 0.0f, 1.0f);
											Alpha = FMath::Clamp(DeltaTime * Grip->LerpSpeed, 0.f, 1.f);
										}

										Alpha = FMath::Clamp(DeltaTime * Grip->LerpSpeed, 0.f, 1.f);
									}
									else
									{
										Grip->CurrentLerpTime -= DeltaTime * Grip->LerpSpeed;
										float OrigAlpha = FMath::Clamp(1.0f - Grip->CurrentLerpTime, 0.f, 1.0f);
										Alpha = OrigAlpha;
									}

									FTransform NA = WorldTransform;
									NA.NormalizeRotation();
									NB.NormalizeRotation();

									if (!bLerpRotationOnly)
									{
										WorldTransform.Blend(NB, NA, Alpha);
									}
									else
									{
										WorldTransform.SetRotation(FQuat::Slerp(NB.GetRotation(), NA.GetRotation(), Alpha));
									}

									if (bDistanceBasedInterpolation)
									{
										if(NA.Equals(WorldTransform, 0.01f))
										{
											Grip->CurrentLerpTime = 0.0f;
										}
										else
										{
											// Save out current distance back to the originating transform
											Grip->OnGripTransform = WorldTransform.GetRelativeTransform(this->GetPivotTransform());
										}
									}
								}
							}

							if (bProjectNonSimulatingGrips && Grip->bSetLastWorldTransform)
							{
								FScopedMovementUpdate ScopedMovementUpdate(root, EScopedUpdate::DeferredUpdates);
								FTransform baseTrans = this->GetAttachParent()->GetComponentTransform();
								root->SetWorldTransform(Grip->LastWorldTransform * baseTrans, false, nullptr, ETeleportType::None);
								root->SetWorldTransform(WorldTransform, false);// , &OutHit);
							}
							else
							{
								root->SetWorldTransform(WorldTransform, false);// , &OutHit);
							}

							if (GripHandle)
							{
								UpdatePhysicsHandleTransform(*Grip, WorldTransform);
							}

						}
						else if (Grip->bColliding)
						{
							if (!GripHandle)
							{
								SetUpPhysicsHandle(*Grip, &GripScripts);
							}
							else if (GripHandle->bIsPaused)
							{
								UnPausePhysicsHandle(*Grip, GripHandle);
							}

							if (GripHandle)
							{
								UpdatePhysicsHandleTransform(*Grip, WorldTransform);
								if (bRescalePhysicsGrips)
									root->SetWorldScale3D(WorldTransform.GetScale3D());
							}
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
						// Ensure physics simulation is off in case something sneaked it on
						if (root->IsSimulatingPhysics())
						{
							root->SetSimulatePhysics(false);
						}

						FVector OriginalPosition(root->GetComponentLocation());
						FRotator OriginalOrientation(root->GetComponentRotation());

						FVector NewPosition(WorldTransform.GetTranslation());
						FRotator NewOrientation(WorldTransform.GetRotation());

						root->ComponentVelocity = (NewPosition - OriginalPosition) / DeltaTime;

						// Now sweep collision separately so we can get hits but not have the location altered
						if (bUseWithoutTracking || NewPosition != OriginalPosition || NewOrientation != OriginalOrientation)
						{
							FVector move = NewPosition - OriginalPosition;

							// ComponentSweepMulti does nothing if moving < UE_KINDA_SMALL_NUMBER in distance, so it's important to not try to sweep distances smaller than that. 
							const float MinMovementDistSq = (FMath::Square(4.f*UE_KINDA_SMALL_NUMBER));

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

						if (bProjectNonSimulatingGrips && Grip->bSetLastWorldTransform)
						{
							FScopedMovementUpdate ScopedMovementUpdate(root, EScopedUpdate::DeferredUpdates);
							FTransform baseTrans = this->GetAttachParent()->GetComponentTransform();
							root->SetWorldTransform(Grip->LastWorldTransform * baseTrans, false, nullptr, ETeleportType::None);
							// Move the actor, we are not offsetting by the hit result anyway
							root->SetWorldTransform(WorldTransform, false);
						}
						else
						{
							// Move the actor, we are not offsetting by the hit result anyway
							root->SetWorldTransform(WorldTransform, false);
						}

					}break;

					case EGripCollisionType::PhysicsOnly:
					{
						// Ensure physics simulation is off in case something sneaked it on
						if (root->IsSimulatingPhysics())
						{
							root->SetSimulatePhysics(false);
						}

						if (bProjectNonSimulatingGrips && Grip->bSetLastWorldTransform)
						{
							FScopedMovementUpdate ScopedMovementUpdate(root, EScopedUpdate::DeferredUpdates);
							FTransform baseTrans = this->GetAttachParent()->GetComponentTransform();
							root->SetWorldTransform(Grip->LastWorldTransform * baseTrans, false, nullptr, ETeleportType::None);
							// Move the actor, we are not offsetting by the hit result anyway
							root->SetWorldTransform(WorldTransform, false);
						}
						else
						{
							// Move the actor, we are not offsetting by the hit result anyway
							root->SetWorldTransform(WorldTransform, false);
						}
					}break;

					case EGripCollisionType::AttachmentGrip:
					{
						FTransform RelativeTrans = WorldTransform.GetRelativeTransform(ParentTransform);

						if (!root->GetAttachParent() || root->IsSimulatingPhysics())
						{
							UE_LOG(LogVRMotionController, Warning, TEXT("Attachment Grip was missing attach parent - Attempting to Re-attach"));

							if (HasGripMovementAuthority(*Grip) || IsServer())
							{
								root->SetSimulatePhysics(false);
								if (root->AttachToComponent(IsValid(CustomPivotComponent) ? CustomPivotComponent.Get() : this, FAttachmentTransformRules::KeepWorldTransform))
								{
									UE_LOG(LogVRMotionController, Warning, TEXT("Re-attached"));
									if (!root->GetRelativeTransform().Equals(RelativeTrans))
									{
										root->SetRelativeTransform(RelativeTrans);
									}
								}
							}
						}
						else
						{
							if (!root->GetRelativeTransform().Equals(RelativeTrans))
							{
								root->SetRelativeTransform(RelativeTrans);
							}
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

				// TEMP 5.4 (or fixed)
				CalculateGripVelocity(*Grip, root, DeltaTime); // Technically for physical grips it would be post physics thre

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
				// Object has been destroyed without notification to plugin or is pending kill
				if (!Grip->bIsPendingKill)
				{
					CleanUpBadGrip(GrippedObjectsArray, i, bReplicatedArray);
				}
			}
		}
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
			if (!PhysicsGrips[g].HandledObject || PhysicsGrips[g].HandledObject == GrippedObjectsArray[GripIndex].GrippedObject || !IsValid(PhysicsGrips[g].HandledObject))
			{
				// Need to delete it from the physics thread
				DestroyPhysicsHandle(&PhysicsGrips[g]);
				PhysicsGrips.RemoveAt(g);
			}
		}
	}

	if (IsServer() || HasGripAuthority(GrippedObjectsArray[GripIndex]))
	{
		DropGrip_Implementation(GrippedObjectsArray[GripIndex], false);
		UE_LOG(LogVRMotionController, Warning, TEXT("Gripped object was null or destroying, auto dropping it"));
	}
	else
	{
		GrippedObjectsArray[GripIndex].bIsPendingKill = true;
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
			GripInfo = GrippedObjects.FindByKey(PhysicsGrips[g].GripID);

		if (!GripInfo)
		{
			// Need to delete it from the physics thread
			DestroyPhysicsHandle(&PhysicsGrips[g]);
			PhysicsGrips.RemoveAt(g);
		}
	}
}

bool UGripMotionControllerComponent::UpdatePhysicsHandle(uint8 GripID, bool bFullyRecreate)
{
	FBPActorGripInformation* GripInfo = GrippedObjects.FindByKey(GripID);
	if (!GripInfo)
		GripInfo = LocallyGrippedObjects.FindByKey(GripID);

	if (!GripInfo)
		return false;

	return UpdatePhysicsHandle(*GripInfo, bFullyRecreate);
}

bool UGripMotionControllerComponent::UpdatePhysicsHandle(const FBPActorGripInformation& GripInfo, bool bFullyRecreate)
{
	int HandleIndex = 0;
	FBPActorPhysicsHandleInformation* HandleInfo = GetPhysicsGrip(GripInfo);

	// Don't update if the handle doesn't exist or is currently paused
	if (!HandleInfo || HandleInfo->bIsPaused || !HandleInfo->bInitiallySetup)
		return false;

	if (bFullyRecreate || !HandleInfo->HandleData2.IsValid() || HandleInfo->bSkipResettingCom)
	{
		return SetUpPhysicsHandle(GripInfo);
	}

	// Not fully recreating, we just re-set important variables
	UPrimitiveComponent* root = GripInfo.GetGrippedComponent();
	AActor* pActor = GripInfo.GetGrippedActor();

	if (!root && pActor)
		root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

	if (!root)
		return false;

	FBodyInstance* rBodyInstance = root->GetBodyInstance(GripInfo.GrippedBoneName);
	if (!rBodyInstance || !rBodyInstance->IsValidBodyInstance() || !FPhysicsInterface::IsValid(rBodyInstance->ActorHandle))
	{
		return false;
	}

	check(rBodyInstance->BodySetup->GetCollisionTraceFlag() != CTF_UseComplexAsSimple);

	FPhysicsCommand::ExecuteWrite(rBodyInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
	{
			if (HandleInfo)
			{
				if (HandleInfo->KinActorData2 && FPhysicsInterface::IsValid(HandleInfo->KinActorData2))
				{
					// Make sure that the constraint particles are correct, the body instance may have changed
					Chaos::FConstraintBase* ConstraintHandle = HandleInfo->HandleData2.Constraint;
					if (ConstraintHandle)
					{
						((Chaos::FJointConstraint*)ConstraintHandle)->SetParticleProxies({ HandleInfo->KinActorData2, Actor });
					}

					// Ensure center of mass is still correct
					if (HandleInfo->bSetCOM && !HandleInfo->bSkipResettingCom)
					{
						FTransform localCom = FPhysicsInterface::GetComTransformLocal_AssumesLocked(Actor);
						//localCom.SetLocation(Loc);
						localCom.SetLocation(HandleInfo->COMPosition.GetTranslation());//Loc);
						FPhysicsInterface::SetComLocalPose_AssumesLocked(Actor, localCom);
					}
				}
			}
	});

	return true;
//#endif

	//return false;
}

bool UGripMotionControllerComponent::PausePhysicsHandle(FBPActorPhysicsHandleInformation* HandleInfo)
{
	if (!HandleInfo)
		return false;

	HandleInfo->bIsPaused = true;
	HandleInfo->bInitiallySetup = false;
	FPhysicsInterface::ReleaseConstraint(HandleInfo->HandleData2);
	return true;
}

bool UGripMotionControllerComponent::UnPausePhysicsHandle(FBPActorGripInformation& GripInfo, FBPActorPhysicsHandleInformation* HandleInfo)
{
	if (!HandleInfo)
		return false;

	HandleInfo->bIsPaused = false;
	SetUpPhysicsHandle(GripInfo);

	return true;
}

bool UGripMotionControllerComponent::DestroyPhysicsHandle(FBPActorPhysicsHandleInformation* HandleInfo)
{
	if (!HandleInfo)
		return false;

	FPhysicsInterface::ReleaseConstraint(HandleInfo->HandleData2);

	if (!HandleInfo->bSkipDeletingKinematicActor)
	{
		if (FPhysicsInterface::IsValid(HandleInfo->KinActorData2))
		{
			FPhysicsActorHandle ActorHandle = HandleInfo->KinActorData2;
			FPhysicsCommand::ExecuteWrite(ActorHandle, [&](const FPhysicsActorHandle& Actor)
			{
					FPhysicsInterface::ReleaseActor(HandleInfo->KinActorData2, FPhysicsInterface::GetCurrentScene(HandleInfo->KinActorData2));
			});
		}
	}

	return true;
}

bool UGripMotionControllerComponent::DestroyPhysicsHandle(const FBPActorGripInformation &Grip, bool bSkipUnregistering)
{
	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(Grip);

	if (!HandleInfo)
	{
		return true;
	}

	if (IsValid(Grip.GrippedObject))
	{

		UPrimitiveComponent* root = Grip.GetGrippedComponent();
		AActor* pActor = Grip.GetGrippedActor();

		if (!root && pActor)
			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());

		if (root)
		{
			if (FBodyInstance* rBodyInstance = root->GetBodyInstance(Grip.GrippedBoneName))
			{
				// #TODO: Should this be done on drop instead?
				// Remove event registration
				if (!bSkipUnregistering)
				{
					if (rBodyInstance->OnRecalculatedMassProperties().IsBoundToObject(this))
					{
						rBodyInstance->OnRecalculatedMassProperties().RemoveAll(this);
					}
				}

				if (HandleInfo->bSetCOM)
				{
					// Reset center of mass to zero
					// Get our original values
					FVector vel = rBodyInstance->GetUnrealWorldVelocity();
					FVector aVel = rBodyInstance->GetUnrealWorldAngularVelocityInRadians();
					FVector originalCOM = rBodyInstance->GetCOMPosition();

					if (rBodyInstance->IsValidBodyInstance() && rBodyInstance->BodySetup.IsValid())
					{
						rBodyInstance->UpdateMassProperties();
					}

					if (rBodyInstance->IsInstanceSimulatingPhysics())
					{
						// Offset the linear velocity by the new COM position and set it
						vel += FVector::CrossProduct(aVel, rBodyInstance->GetCOMPosition() - originalCOM);
						rBodyInstance->SetLinearVelocity(vel, false);
					}
				}
			}
		}
	}

	DestroyPhysicsHandle(HandleInfo);

	int index;
	if (GetPhysicsGripIndex(Grip, index))
		PhysicsGrips.RemoveAt(index);

	return true;
}

void UGripMotionControllerComponent::OnGripMassUpdated(FBodyInstance* GripBodyInstance)
{
	TArray<FBPActorGripInformation> GripArray;
	this->GetAllGrips(GripArray);
	FBPActorGripInformation NewGrip;

	for (int i = 0; i < GripArray.Num(); i++)
	{
		NewGrip = GripArray[i];

		UPrimitiveComponent *root = NewGrip.GetGrippedComponent();
		AActor * pActor = NewGrip.GetGrippedActor();

		if (!root && pActor)
		{
			if (!IsValid(pActor))
				continue;

			root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());
		}

		if (!root || root != GripBodyInstance->OwnerComponent)
			continue;

		if (IsValid(root))
		{
			UpdatePhysicsHandle(NewGrip, false);
		}
		break;
	}
}

bool UGripMotionControllerComponent::SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip, TArray<UVRGripScriptBase*> * GripScripts)
{
	UPrimitiveComponent *root = NewGrip.GetGrippedComponent();
	AActor * pActor = NewGrip.GetGrippedActor();

	if(!root && pActor)
		root = Cast<UPrimitiveComponent>(pActor->GetRootComponent());
	
	if (!root)
		return false;

	FBPActorPhysicsHandleInformation* HandleInfo = GetPhysicsGrip(NewGrip);
	if (HandleInfo == nullptr)
	{
		HandleInfo = CreatePhysicsGrip(NewGrip);
	}

	// If currently paused lets skip this step
	if (HandleInfo->bIsPaused)
	{
		return false;
	}

	HandleInfo->bSetCOM = false; // Zero this out in case it is a re-init
	HandleInfo->bSkipDeletingKinematicActor = (bConstrainToPivot && !NewGrip.bIsLerping);

	// Check for grip scripts if we weren't passed in any
	TArray<UVRGripScriptBase*> LocalGripScripts;
	if (GripScripts == nullptr)
	{
		if (root && root->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			if (IVRGripInterface::Execute_GetGripScripts(root, LocalGripScripts))
			{
				GripScripts = &LocalGripScripts;
			}
		}
		else if (pActor && pActor->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			if (IVRGripInterface::Execute_GetGripScripts(pActor, LocalGripScripts))
			{
				GripScripts = &LocalGripScripts;
			}
		}
	}

	// Needs to be simulating in order to run physics
	if (!NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings || !NewGrip.AdvancedGripSettings.PhysicsSettings.bSkipSettingSimulating)
	{
		root->SetSimulatePhysics(true);
	}

	// Get the PxRigidDynamic that we want to grab.
	FBodyInstance* rBodyInstance = root->GetBodyInstance(NewGrip.GrippedBoneName);
	Chaos::FPhysicsObject* PhysicsActor = root->GetPhysicsObjectByName(NewGrip.GrippedBoneName);

	bool bUseActorHandle = true;
	if (!rBodyInstance || !rBodyInstance->IsValidBodyInstance() || !FPhysicsInterface::IsValid(rBodyInstance->ActorHandle) || !rBodyInstance->BodySetup.IsValid())
	{	
		if (PhysicsActor)
		{
			bUseActorHandle = false;
		}
		else
		{
			return false;
		}
	}

	if (rBodyInstance && bUseActorHandle)
	{
		check(rBodyInstance->BodySetup->GetCollisionTraceFlag() != CTF_UseComplexAsSimple);

		if (!HandleInfo->bSkipResettingCom && !FPhysicsInterface::IsValid(HandleInfo->KinActorData2) && !rBodyInstance->OnRecalculatedMassProperties().IsBoundToObject(this))
		{
			// Reset the mass properties, this avoids an issue with some weird replication issues
			// We only do this on initial grip
			rBodyInstance->UpdateMassProperties();

		}
	}

	/*if (NewGrip.GrippedBoneName != NAME_None)
	{
		rBodyInstance->SetInstanceSimulatePhysics(true);
	}*/

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
		if (USkeletalMeshComponent* skele = Cast<USkeletalMeshComponent>(root))
		{
			int32 RootBodyIndex = INDEX_NONE;
			if (const UPhysicsAsset* PhysicsAsset = skele->GetPhysicsAsset())
			{
				for (int32 i = 0; i < skele->GetNumBones(); i++)
				{
					if (PhysicsAsset->FindBodyIndex(skele->GetBoneName(i)) != INDEX_NONE)
					{
						RootBodyIndex = i;
						break;
					}
				}
			}

			if (RootBodyIndex != INDEX_NONE)
			{
				RootBoneRotation = FTransform(skele->GetBoneTransform(RootBodyIndex, FTransform::Identity));
				RootBoneRotation.SetScale3D(FVector(1.f));
				RootBoneRotation.NormalizeRotation();
				HandleInfo->RootBoneRotation = RootBoneRotation;
			}
		}
	}

	// They forgot to make a single chaos::FPhysicsBody version of the write lock....
	bool bExecutedPhys = FPhysicsCommand::ExecuteWrite(PhysicsActor, PhysicsActor, [&](/*const FPhysicsActorHandle& Actor*/Chaos::FPhysicsObject* PhysActor, Chaos::FPhysicsObject* PhysActorNULL)
	{
		const FPhysicsActorHandle& Actor = bUseActorHandle ? rBodyInstance->GetPhysicsActorHandle() : nullptr;
		Chaos::FWritePhysicsObjectInterface_External Interface = FPhysicsObjectExternalInterface::GetWrite_AssumesLocked();
		FChaosScene* PhysScene = PhysicsObjectPhysicsCoreInterface::GetScene({ &PhysActor, 1 });
		FTransform PhysActorTransform = Interface.GetTransform(PhysActor);

		if (!PhysScene)
		{
			return;
		}

		FTransform KinPose;
		FTransform trans = Interface.GetTransform(PhysActor);//FPhysicsInterface::GetGlobalPose_AssumesLocked(Actor);

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

		// For geometry collections we can't set COM
		if (!bUseActorHandle)
		{
			if (COMType == EPhysicsGripCOMType::COM_SetAndGripAt)
			{
				COMType = EPhysicsGripCOMType::COM_GripAtControllerLoc;
			}
		}

		if (bUseActorHandle && COMType == EPhysicsGripCOMType::COM_SetAndGripAt)
		{
			// Update the center of mass
			FTransform ForwardTrans = (RootBoneRotation * NewGrip.RelativeTransform);
			ForwardTrans.NormalizeRotation();
			FVector Loc = (FTransform(ForwardTrans.ToInverseMatrixWithScale())).GetLocation();
			Loc *= root->GetComponentScale();

			FTransform localCom = FPhysicsInterface::GetComTransformLocal_AssumesLocked(Actor);//Interface.GetWorldCoM(PhysActor); 
			localCom.SetLocation(Loc);
			FPhysicsInterface::SetComLocalPose_AssumesLocked(Actor, localCom);

			FVector ComLoc = FPhysicsInterface::GetComTransform_AssumesLocked(Actor).GetLocation(); // Interface.GetWorldCoM(PhysActor);
			trans.SetLocation(ComLoc);
			HandleInfo->COMPosition = FTransform(PhysActorTransform.InverseTransformPosition(ComLoc));
			HandleInfo->bSetCOM = true;
		}
		else if (COMType == EPhysicsGripCOMType::COM_GripAtControllerLoc)
		{
			FTransform ObjectTransform = PhysActorTransform;
			ObjectTransform.SetScale3D(root->GetComponentScale());

			FVector ControllerLoc = (FTransform(NewGrip.RelativeTransform.ToInverseMatrixWithScale()) * ObjectTransform).GetLocation();
			trans.SetLocation(ControllerLoc);
			HandleInfo->COMPosition = FTransform(PhysActorTransform.InverseTransformPosition(ControllerLoc));
		}
		else if (COMType != EPhysicsGripCOMType::COM_AtPivot)
		{
			FVector ComLoc = Interface.GetWorldCoM(PhysActor);// FPhysicsInterface::GetComTransform_AssumesLocked(Actor).GetLocation();
			trans.SetLocation(ComLoc);
			HandleInfo->COMPosition = FTransform(PhysActorTransform.InverseTransformPosition(ComLoc));
		}

		KinPose = trans;
		bool bRecreatingConstraint = false;


		// If using twist only, lets rotate the kinematic actor to face the controller X+
		if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
		{
			FTransform PivTrans = GetPivotTransform();

			FQuat DeltaQuat = (PivTrans.GetRotation().Inverse() * KinPose.GetRotation()).Inverse();

			// This moves the kinematic actor to face the pivot components X+ direction
			KinPose.SetRotation(KinPose.GetRotation() * DeltaQuat);
			HandleInfo->COMPosition.SetRotation(HandleInfo->COMPosition.GetRotation()* DeltaQuat);
		}

		if (GripScripts)
		{
			bool bResetCom = false;

			// Inject any alterations that the grip scripts want to make
			for (UVRGripScriptBase* Script : *GripScripts)
			{
				if (Script && Script->IsScriptActive() && Script->InjectPrePhysicsHandle())
				{
					Script->HandlePrePhysicsHandle(this, NewGrip, HandleInfo, KinPose);
					bResetCom = true;
				}
			}

			if (bUseActorHandle && HandleInfo->bSetCOM && bResetCom)
			{
				FTransform localCom = FPhysicsInterface::GetComTransformLocal_AssumesLocked(Actor);
				localCom.SetLocation(HandleInfo->COMPosition.GetTranslation());//Loc);

				FPhysicsInterface::SetComLocalPose_AssumesLocked(Actor, localCom);
			}
		}

		if (!NewGrip.bIsLerping && bConstrainToPivot && IsValid(CustomPivotComponent))
		{
			if (UPrimitiveComponent* PivotPrim = Cast<UPrimitiveComponent>(CustomPivotComponent))
			{
				if (FBodyInstance* Bodyinst = PivotPrim->GetBodyInstance(CustomPivotComponentSocketName))
				{
					HandleInfo->KinActorData2 = Bodyinst->GetPhysicsActorHandle();
				}
			}
		}
		
		if (!FPhysicsInterface::IsValid(HandleInfo->KinActorData2))
		{
			// Create kinematic actor we are going to create joint with. This will be moved around with calls to SetLocation/SetRotation.
			
			FActorCreationParams ActorParams;
			ActorParams.InitialTM = KinPose;
			ActorParams.DebugName = nullptr;
			ActorParams.bEnableGravity = false;
			ActorParams.bQueryOnly = false;// true; // True or false?
			ActorParams.bStatic = false;
			ActorParams.Scene = PhysScene;//FPhysicsInterface::GetCurrentScene(Actor);
			FPhysicsInterface::CreateActor(ActorParams, HandleInfo->KinActorData2);
			
			if (FPhysicsInterface::IsValid(HandleInfo->KinActorData2))
			{
				FPhysicsInterface::SetMass_AssumesLocked(HandleInfo->KinActorData2, 1.0f);
				FPhysicsInterface::SetMassSpaceInertiaTensor_AssumesLocked(HandleInfo->KinActorData2, FVector(1.f));
				FPhysicsInterface::SetIsKinematic_AssumesLocked(HandleInfo->KinActorData2, true);
				FPhysicsInterface::SetMaxDepenetrationVelocity_AssumesLocked(HandleInfo->KinActorData2, MAX_FLT);
				//FPhysicsInterface::SetActorUserData_AssumesLocked(HandleInfo->KinActorData2, NULL);
			}

			using namespace Chaos;
			// Missing from physx, not sure how it is working for them currently.
			//TArray<FPhysicsActorHandle> ActorHandles;
			HandleInfo->KinActorData2->GetGameThreadAPI().SetGeometry(MakeImplicitObjectPtr<TSphere<FReal, 3>>(TVector<FReal, 3>(0.f), 1000.f));
			HandleInfo->KinActorData2->GetGameThreadAPI().SetObjectState(EObjectStateType::Kinematic);
			FPhysicsInterface::AddActorToSolver(HandleInfo->KinActorData2, ActorParams.Scene->GetSolver());
			//ActorHandles.Add(HandleInfo->KinActorData2);
			//ActorParams.Scene->AddActorsToScene_AssumesLocked(ActorHandles);
		}

		// If we don't already have a handle - make one now.
		if (!HandleInfo->HandleData2.IsValid())
		{
			// If this is true we will totally ignore the COM type, either we are gripping unstable or the com was set to our controller, we never accept
			// Grip at COM
			if (!NewGrip.bIsLerping && bConstrainToPivot)
			{
				FTransform TargetTrans(FTransform(NewGrip.RelativeTransform.ToMatrixNoScale().Inverse()) * HandleInfo->RootBoneRotation.Inverse());
				HandleInfo->HandleData2 = FPhysicsInterface::CreateConstraint(HandleInfo->KinActorData2->GetPhysicsObject(), PhysActor, FTransform::Identity, TargetTrans);
			}
			else
			{
				HandleInfo->HandleData2 = FPhysicsInterface::CreateConstraint(HandleInfo->KinActorData2->GetPhysicsObject(), PhysActor, FTransform::Identity, KinPose.GetRelativeTransform(PhysActorTransform));
			}
		}
		else
		{
			bRecreatingConstraint = true;

			/*
			FTransform newTrans = HandleInfo->COMPosition * (HandleInfo->RootBoneRotation * HandleInfo->LastPhysicsTransform);
			*/

			// There isn't a direct set for the particles, so keep it as a recreation instead.
			FPhysicsInterface::ReleaseConstraint(HandleInfo->HandleData2);

			if (!NewGrip.bIsLerping && bConstrainToPivot)
			{
				FTransform TargetTrans(NewGrip.RelativeTransform.ToMatrixNoScale().Inverse());
				HandleInfo->HandleData2 = FPhysicsInterface::CreateConstraint(HandleInfo->KinActorData2->GetPhysicsObject(), PhysActor, FTransform::Identity, TargetTrans);
			}
			else
			{
				HandleInfo->HandleData2 = FPhysicsInterface::CreateConstraint(HandleInfo->KinActorData2->GetPhysicsObject(), PhysActor, FTransform::Identity, KinPose.GetRelativeTransform(PhysActorTransform));
			}
		
		}

		if (HandleInfo->HandleData2.IsValid())
		{
			FPhysicsInterface::SetBreakForces_AssumesLocked(HandleInfo->HandleData2, MAX_FLT, MAX_FLT);


			if (NewGrip.GripCollisionType == EGripCollisionType::LockedConstraint)
			{
				FPhysicsInterface::SetLinearMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::X, ELinearConstraintMotion::LCM_Locked);
				FPhysicsInterface::SetLinearMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Y, ELinearConstraintMotion::LCM_Locked);
				FPhysicsInterface::SetLinearMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Z, ELinearConstraintMotion::LCM_Locked);
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Twist, EAngularConstraintMotion::ACM_Locked);
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Swing1, EAngularConstraintMotion::ACM_Locked);
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Swing2, EAngularConstraintMotion::ACM_Locked);
				FPhysicsInterface::SetProjectionEnabled_AssumesLocked(HandleInfo->HandleData2, true, 0.01f, 0.01f);
			}
			else
			{
				FPhysicsInterface::SetLinearMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::X, ELinearConstraintMotion::LCM_Free);
				FPhysicsInterface::SetLinearMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Y, ELinearConstraintMotion::LCM_Free);
				FPhysicsInterface::SetLinearMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Z, ELinearConstraintMotion::LCM_Free);
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Twist, EAngularConstraintMotion::ACM_Free);
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Swing1, EAngularConstraintMotion::ACM_Free);
				FPhysicsInterface::SetAngularMotionLimitType_AssumesLocked(HandleInfo->HandleData2, PhysicsInterfaceTypes::ELimitAxis::Swing2, EAngularConstraintMotion::ACM_Free);
				FPhysicsInterface::SetProjectionEnabled_AssumesLocked(HandleInfo->HandleData2, false);
			}

			FPhysicsInterface::SetDrivePosition(HandleInfo->HandleData2, FVector::ZeroVector);

			bool bUseForceDrive = (NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.PhysicsConstraintType == EPhysicsGripConstraintType::ForceConstraint);

			float Stiffness = NewGrip.Stiffness;
			float Damping = NewGrip.Damping;
			float MaxForce;
			float AngularStiffness;
			float AngularDamping;
			float AngularMaxForce;

			const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

			if (NewGrip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && NewGrip.AdvancedGripSettings.PhysicsSettings.bUseCustomAngularValues)
			{
				AngularStiffness = NewGrip.AdvancedGripSettings.PhysicsSettings.AngularStiffness;
				AngularDamping = NewGrip.AdvancedGripSettings.PhysicsSettings.AngularDamping;
			}
			else
			{
				AngularStiffness = Stiffness * ANGULAR_STIFFNESS_MULTIPLIER; // Default multiplier
				AngularDamping = Damping * ANGULAR_DAMPING_MULTIPLIER; // Default multiplier

				if (!VRSettings.bUseChaosTranslationScalers)
				{
					AngularStiffness *= ANGULAR_STIFFNESS_MULTIPLIER_CHAOS;
					AngularDamping *= ANGULAR_DAMPING_MULTIPLIER_CHAOS;
				}
			}
			
			if (VRSettings.bUseChaosTranslationScalers)
			{
				Stiffness *= VRSettings.LinearDriveStiffnessScale;
				Damping *= VRSettings.LinearDriveDampingScale;
				AngularStiffness *= VRSettings.AngularDriveStiffnessScale;
				AngularDamping *= VRSettings.AngularDriveDampingScale;
			}
			else
			{
				auto CVarLinearDriveStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.LinearDriveStiffnessScale"));
				auto CVarLinearDriveDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.LinaearDriveDampingScale"));
				auto CVarAngularDriveStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveStiffnessScale"));
				auto CVarAngularDriveDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveDampingScale"));

				Stiffness *= CVarLinearDriveStiffnessScale->GetFloat();
				Damping *= CVarLinearDriveDampingScale->GetFloat();
				AngularStiffness *= CVarAngularDriveStiffnessScale->GetFloat();
				AngularDamping *= CVarAngularDriveDampingScale->GetFloat();
			}

			AngularMaxForce = (float)FMath::Clamp<double>((double)AngularStiffness * (double)NewGrip.AdvancedGripSettings.PhysicsSettings.AngularMaxForceCoefficient, 0, (double)MAX_FLT);
			MaxForce = (float)FMath::Clamp<double>((double)Stiffness * (double)NewGrip.AdvancedGripSettings.PhysicsSettings.LinearMaxForceCoefficient, 0, (double)MAX_FLT);

			// Different settings for manip grip
			if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGrip || NewGrip.GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
			{
				if (!bRecreatingConstraint)
				{
					FConstraintDrive NewLinDrive;
					NewLinDrive.bEnablePositionDrive = true;
					NewLinDrive.bEnableVelocityDrive = true;
					NewLinDrive.Damping = Damping;
					NewLinDrive.Stiffness = Stiffness;
					NewLinDrive.MaxForce = MaxForce;

					//HandleInfo->LinConstraint.bEnablePositionDrive = true;
					HandleInfo->LinConstraint.XDrive = NewLinDrive;
					HandleInfo->LinConstraint.YDrive = NewLinDrive;
					HandleInfo->LinConstraint.ZDrive = NewLinDrive;
				}


				if (NewGrip.GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
				{
					if (!bRecreatingConstraint)
					{
						FConstraintDrive NewAngDrive;
						NewAngDrive.bEnablePositionDrive = true;
						NewAngDrive.bEnableVelocityDrive = true;
						NewAngDrive.Damping = AngularDamping;
						NewAngDrive.Stiffness = AngularStiffness;
						NewAngDrive.MaxForce = AngularMaxForce;
						//NewAngDrive.MaxForce = MAX_FLT;

						HandleInfo->AngConstraint.AngularDriveMode = EAngularDriveMode::TwistAndSwing;
						//AngParams.AngularDriveMode = EAngularDriveMode::SLERP;
						HandleInfo->AngConstraint.TwistDrive = NewAngDrive;
					}
				}

				if (GripScripts)
				{
					// Inject any alterations that the grip scripts want to make
					for (UVRGripScriptBase* Script : *GripScripts)
					{
						if (Script && Script->IsScriptActive() && Script->InjectPostPhysicsHandle())
						{
							Script->HandlePostPhysicsHandle(this, HandleInfo);
						}
					}
				}

				FPhysicsInterface::UpdateLinearDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->LinConstraint);
				FPhysicsInterface::UpdateAngularDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->AngConstraint);
			}
			else
			{
				if (NewGrip.GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithPhysics)
				{
					// Do not effect damping, just increase stiffness so that it is stronger
					// Default multiplier
					Stiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;
					AngularStiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;
					AngularMaxForce = (float)FMath::Clamp<double>((double)AngularStiffness * (double)NewGrip.AdvancedGripSettings.PhysicsSettings.AngularMaxForceCoefficient, 0, (double)MAX_FLT);
					MaxForce = (float)FMath::Clamp<double>((double)Stiffness * (double)NewGrip.AdvancedGripSettings.PhysicsSettings.LinearMaxForceCoefficient, 0, (double)MAX_FLT);
				}

				if (!bRecreatingConstraint)
				{
					if (NewGrip.GripCollisionType != EGripCollisionType::LockedConstraint)
					{
						FConstraintDrive NewLinDrive;
						NewLinDrive.bEnablePositionDrive = true;
						NewLinDrive.bEnableVelocityDrive = true;
						NewLinDrive.Damping = Damping;
						NewLinDrive.Stiffness = Stiffness;
						NewLinDrive.MaxForce = MaxForce;
						//NewLinDrive.MaxForce = MAX_FLT;

						FConstraintDrive NewAngDrive;
						NewAngDrive.bEnablePositionDrive = true;
						NewAngDrive.bEnableVelocityDrive = true;
						NewAngDrive.Damping = AngularDamping;
						NewAngDrive.Stiffness = AngularStiffness;
						NewAngDrive.MaxForce = AngularMaxForce;
						//NewAngDrive.MaxForce = MAX_FLT;

						//HandleInfo->LinConstraint.bEnablePositionDrive = true;
						HandleInfo->LinConstraint.XDrive.bEnablePositionDrive = true;
						HandleInfo->LinConstraint.YDrive.bEnablePositionDrive = true;
						HandleInfo->LinConstraint.ZDrive.bEnablePositionDrive = true;

						HandleInfo->LinConstraint.XDrive = NewLinDrive;
						HandleInfo->LinConstraint.YDrive = NewLinDrive;
						HandleInfo->LinConstraint.ZDrive = NewLinDrive;

						HandleInfo->AngConstraint.AngularDriveMode = EAngularDriveMode::SLERP;
						HandleInfo->AngConstraint.SlerpDrive = NewAngDrive;
					}
				}

				if (GripScripts)
				{
					// Inject any alterations that the grip scripts want to make
					for (UVRGripScriptBase* Script : *GripScripts)
					{
						if (Script && Script->IsScriptActive() && Script->InjectPostPhysicsHandle())
						{
							Script->HandlePostPhysicsHandle(this, HandleInfo);
						}
					}
				}
					
				FPhysicsInterface::UpdateLinearDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->LinConstraint);
				FPhysicsInterface::UpdateAngularDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->AngConstraint);
			}


			// This is a temp workaround until epic fixes the drive creation to allow force constraints
			// I wanted to use the new interface and not directly set the drive so that it is ready to delete this section
			// When its fixed
			if (bUseForceDrive && HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint)
			{
				if (HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
				{
					if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(HandleInfo->HandleData2.Constraint))
					{
						Constraint->SetLinearDriveForceMode(Chaos::EJointForceMode::Force);
						Constraint->SetAngularDriveForceMode(Chaos::EJointForceMode::Force);
					}
				}
			}
		}
	});

	if (!bExecutedPhys)
	{
		return false;
	}

	HandleInfo->bInitiallySetup = true;

	// Bind to further updates in order to keep it alive
	if (bUseActorHandle && !rBodyInstance->OnRecalculatedMassProperties().IsBoundToObject(this))
	{
		rBodyInstance->OnRecalculatedMassProperties().AddUObject(this, &UGripMotionControllerComponent::OnGripMassUpdated);
	}

	return true;
}

bool UGripMotionControllerComponent::SetGripConstraintStiffnessAndDamping(const FBPActorGripInformation* Grip, bool bUseHybridMultiplier)
{
	if (!Grip)
		return false;

	FBPActorPhysicsHandleInformation* HandleInfo = GetPhysicsGrip(*Grip);

	if (HandleInfo)
	{
		if (HandleInfo->HandleData2.IsValid())
		{
			FPhysicsInterface::ExecuteOnUnbrokenConstraintReadWrite(HandleInfo->HandleData2, [&](const FPhysicsConstraintHandle& InUnbrokenConstraint)
				{
					bool bUseForceDrive = (Grip->AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && Grip->AdvancedGripSettings.PhysicsSettings.PhysicsConstraintType == EPhysicsGripConstraintType::ForceConstraint);

					float Stiffness = Grip->Stiffness;
					float Damping = Grip->Damping;
					float MaxForce;
					float AngularStiffness;
					float AngularDamping;
					float AngularMaxForce;

					const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

					if (Grip->AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && Grip->AdvancedGripSettings.PhysicsSettings.bUseCustomAngularValues)
					{
						AngularStiffness = Grip->AdvancedGripSettings.PhysicsSettings.AngularStiffness;
						AngularDamping = Grip->AdvancedGripSettings.PhysicsSettings.AngularDamping;
					}
					else
					{
						AngularStiffness = Stiffness * ANGULAR_STIFFNESS_MULTIPLIER; // Default multiplier
						AngularDamping = Damping * ANGULAR_DAMPING_MULTIPLIER; // Default multiplier

						if (!VRSettings.bUseChaosTranslationScalers)
						{
							AngularStiffness *= ANGULAR_STIFFNESS_MULTIPLIER_CHAOS;
							AngularDamping *= ANGULAR_DAMPING_MULTIPLIER_CHAOS;
						}
					}
					
					if (VRSettings.bUseChaosTranslationScalers)
					{
						Stiffness *= VRSettings.LinearDriveStiffnessScale;
						Damping *= VRSettings.LinearDriveDampingScale;
						AngularStiffness *= VRSettings.AngularDriveStiffnessScale;
						AngularDamping *= VRSettings.AngularDriveDampingScale;
					}
					else
					{
						auto CVarLinearDriveStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.LinearDriveStiffnessScale"));
						auto CVarLinearDriveDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.LinaearDriveDampingScale"));
						auto CVarAngularDriveStiffnessScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveStiffnessScale"));
						auto CVarAngularDriveDampingScale = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Chaos.JointConstraint.AngularDriveDampingScale"));

						Stiffness *= CVarLinearDriveStiffnessScale->GetFloat();
						Damping *= CVarLinearDriveDampingScale->GetFloat();
						AngularStiffness *= CVarAngularDriveStiffnessScale->GetFloat();
						AngularDamping *= CVarAngularDriveDampingScale->GetFloat();
					}

					AngularMaxForce = (float)FMath::Clamp<double>((double)AngularStiffness * (double)Grip->AdvancedGripSettings.PhysicsSettings.AngularMaxForceCoefficient, 0, (double)MAX_FLT);
					MaxForce = (float)FMath::Clamp<double>((double)Stiffness * (double)Grip->AdvancedGripSettings.PhysicsSettings.LinearMaxForceCoefficient, 0, (double)MAX_FLT);


					// Different settings for manip grip
					if (Grip->GripCollisionType == EGripCollisionType::ManipulationGrip || Grip->GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
					{
						HandleInfo->LinConstraint.XDrive.Damping = Damping;
						HandleInfo->LinConstraint.XDrive.Stiffness = Stiffness;
						HandleInfo->LinConstraint.XDrive.MaxForce = MaxForce;
						HandleInfo->LinConstraint.YDrive.Damping = Damping;
						HandleInfo->LinConstraint.YDrive.Stiffness = Stiffness;
						HandleInfo->LinConstraint.YDrive.MaxForce = MaxForce;
						HandleInfo->LinConstraint.ZDrive.Damping = Damping;
						HandleInfo->LinConstraint.ZDrive.Stiffness = Stiffness;
						HandleInfo->LinConstraint.ZDrive.MaxForce = MaxForce;

						FPhysicsInterface::UpdateLinearDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->LinConstraint);
						if (bUseForceDrive && HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint)
						{
							if (HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
							{
								if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(HandleInfo->HandleData2.Constraint))
								{
									Constraint->SetLinearDriveForceMode(Chaos::EJointForceMode::Force);
									Constraint->SetAngularDriveForceMode(Chaos::EJointForceMode::Force);
								}
							}
						}

						if (Grip->GripCollisionType == EGripCollisionType::ManipulationGripWithWristTwist)
						{
							HandleInfo->AngConstraint.TwistDrive.Damping = AngularDamping;
							HandleInfo->AngConstraint.TwistDrive.Stiffness = AngularStiffness;
							HandleInfo->AngConstraint.TwistDrive.MaxForce = AngularMaxForce;

							FPhysicsInterface::UpdateAngularDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->AngConstraint);
						}

						FPhysicsInterface::SetDrivePosition(HandleInfo->HandleData2, FVector::ZeroVector);
						FPhysicsInterface::SetDriveOrientation(HandleInfo->HandleData2, FQuat::Identity);
					}
					else
					{
						if (Grip->GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithPhysics)
						{
							// Do not effect damping, just increase stiffness so that it is stronger
							// Default multiplier
							Stiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;
							AngularStiffness *= HYBRID_PHYSICS_GRIP_MULTIPLIER;

							AngularMaxForce = (float)FMath::Clamp<double>((double)AngularStiffness * (double)Grip->AdvancedGripSettings.PhysicsSettings.AngularMaxForceCoefficient, 0, (double)MAX_FLT);
							MaxForce = (float)FMath::Clamp<double>((double)Stiffness * (double)Grip->AdvancedGripSettings.PhysicsSettings.LinearMaxForceCoefficient, 0, (double)MAX_FLT);
						}

						HandleInfo->LinConstraint.XDrive.Damping = Damping;
						HandleInfo->LinConstraint.XDrive.Stiffness = Stiffness;
						HandleInfo->LinConstraint.XDrive.MaxForce = MaxForce;
						HandleInfo->LinConstraint.YDrive.Damping = Damping;
						HandleInfo->LinConstraint.YDrive.Stiffness = Stiffness;
						HandleInfo->LinConstraint.YDrive.MaxForce = MaxForce;
						HandleInfo->LinConstraint.ZDrive.Damping = Damping;
						HandleInfo->LinConstraint.ZDrive.Stiffness = Stiffness;
						HandleInfo->LinConstraint.ZDrive.MaxForce = MaxForce;

						FPhysicsInterface::UpdateLinearDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->LinConstraint);
						if (bUseForceDrive && HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint)
						{
							if (HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
							{
								if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(HandleInfo->HandleData2.Constraint))
								{
									Constraint->SetLinearDriveForceMode(Chaos::EJointForceMode::Force);
								}
							}
						}


						HandleInfo->AngConstraint.SlerpDrive.Damping = AngularDamping;
						HandleInfo->AngConstraint.SlerpDrive.Stiffness = AngularStiffness;
						HandleInfo->AngConstraint.SlerpDrive.MaxForce = AngularMaxForce;
						FPhysicsInterface::UpdateAngularDrive_AssumesLocked(HandleInfo->HandleData2, HandleInfo->AngConstraint);
						if (bUseForceDrive && HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint)
						{
							if (HandleInfo->HandleData2.IsValid() && HandleInfo->HandleData2.Constraint->IsType(Chaos::EConstraintType::JointConstraintType))
							{
								if (Chaos::FJointConstraint* Constraint = static_cast<Chaos::FJointConstraint*>(HandleInfo->HandleData2.Constraint))
								{
									Constraint->SetAngularDriveForceMode(Chaos::EJointForceMode::Force);
								}
							}
						}
					}

				});

		}
		return true;
	}

	return false;
}

bool UGripMotionControllerComponent::GetPhysicsJointLength(const FBPActorGripInformation &GrippedActor, UPrimitiveComponent * rootComp, FVector & LocOut)
{
	if (!GrippedActor.GrippedObject)
		return false;

	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(GrippedActor);

	if (!HandleInfo || !FPhysicsInterface::IsValid(HandleInfo->KinActorData2))
		return false;

	if (!HandleInfo->HandleData2.IsValid())
		return false;

	// Not using com with skipping mass check as the COM can change on us
	// Also skipping it on skip resetting com as we aren't gripped to the COM then
	bool bUseComLoc = 
		(
			!HandleInfo->bSkipResettingCom &&
			(HandleInfo->bSetCOM || 
			(GrippedActor.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && GrippedActor.AdvancedGripSettings.PhysicsSettings.PhysicsGripLocationSettings == EPhysicsGripCOMType::COM_GripAt))
		);

	// This is supposed to be the difference between the actor and the kinactor / constraint base
	FTransform tran3 = FTransform::Identity;
	
	FBodyInstance* rBodyInstance = rootComp->GetBodyInstance(GrippedActor.GrippedBoneName);

	if (bUseComLoc && rBodyInstance && rBodyInstance->IsValidBodyInstance())
	{
		tran3 = FTransform(rBodyInstance->GetCOMPosition());
	}
	else
	{
		FTransform rr;
		tran3 = FPhysicsInterface::GetLocalPose(HandleInfo->HandleData2, EConstraintFrame::Frame2);

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
	}

	// Get the global pose for the kin actor
	FTransform kinPose = FTransform::Identity;
	FPhysicsCommand::ExecuteRead(HandleInfo->KinActorData2, [&](const FPhysicsActorHandle & Actor)
	{
		kinPose = FPhysicsInterface::GetGlobalPose_AssumesLocked(Actor);
	});

	// Return the difference
	LocOut = FTransform::SubtractTranslations(kinPose, tran3);

	return true;
}

void UGripMotionControllerComponent::UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform)
{
	if (!GrippedActor.GrippedObject || (bConstrainToPivot && !GrippedActor.bIsLerping) || IsTravelingOrNullWorld())
		return;

	if (!NewTransform.IsValid())
	{
		UE_LOG(LogVRMotionController, Warning, TEXT("Something went wrong, UpdatePhysicsHandeTransforms target transform contained NAN!."));
		return;
	}

	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(GrippedActor);

	if (!HandleInfo || !FPhysicsInterface::IsValid(HandleInfo->KinActorData2))// || !HandleInfo->HandleData2.IsValid())
		return;

	// Don't call moveKinematic if it hasn't changed - that will stop bodies from going to sleep.
	if (!HandleInfo->LastPhysicsTransform.EqualsNoScale(NewTransform))
	{
		HandleInfo->LastPhysicsTransform = NewTransform;
		HandleInfo->LastPhysicsTransform.SetScale3D(FVector(1.0f));
		FPhysicsActorHandle ActorHandle = HandleInfo->KinActorData2;
		FTransform newTrans = HandleInfo->COMPosition * (HandleInfo->RootBoneRotation * HandleInfo->LastPhysicsTransform);
		FPhysicsCommand::ExecuteWrite(ActorHandle, [&](const FPhysicsActorHandle & Actor)
		{
			FPhysicsInterface::SetKinematicTarget_AssumesLocked(Actor, newTrans);
		});
	}

	// Debug draw for COM movement with physics grips
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (GripMotionControllerCvars::DrawDebugGripCOM)
	{
		UPrimitiveComponent* me = Cast<UPrimitiveComponent>(GrippedActor.GripTargetType == EGripTargetType::ActorGrip ? GrippedActor.GetGrippedActor()->GetRootComponent() : GrippedActor.GetGrippedComponent());
		FVector curCOMPosition = me->GetBodyInstance(GrippedActor.GrippedBoneName)->GetCOMPosition();
		DrawDebugSphere(GetWorld(), curCOMPosition, 4, 32, FColor::Red, false);
		FTransform TargetTransform = (HandleInfo->COMPosition * (HandleInfo->RootBoneRotation * HandleInfo->LastPhysicsTransform));
		DrawDebugSphere(GetWorld(), TargetTransform.GetLocation(), 4, 32, FColor::Cyan, false);
		DrawDebugLine(GetWorld(), TargetTransform.GetTranslation(), TargetTransform.GetTranslation() + (TargetTransform.GetRotation().GetForwardVector() * 20.f), FColor::Red);
		DrawDebugLine(GetWorld(), TargetTransform.GetTranslation(), TargetTransform.GetTranslation() + (TargetTransform.GetRotation().GetRightVector() * 20.f), FColor::Green);
		DrawDebugLine(GetWorld(), TargetTransform.GetTranslation(), TargetTransform.GetTranslation() + (TargetTransform.GetRotation().GetUpVector() * 20.f), FColor::Blue);
	}
#endif

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
			float BlockingHitNormalDotDelta = UE_BIG_NUMBER;
			for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
			{
				const FHitResult& TestHit = Hits[HitIdx];

				// Ignore the owning actor to the motion controller
				if (TestHit.GetActor() == this->GetOwner() || (bSkipSimulatingComponents && TestHit.Component->IsSimulatingPhysics()))
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
	if (BlockingHit.bBlockingHit && IsValid(root))
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

bool UGripMotionControllerComponent::HasTrackingParameters()
{
	return /*bOffsetByHMD ||*/ bScaleTracking || bLeashToHMD || bLimitMinHeight || bLimitMaxHeight || (AttachChar && !AttachChar->bRetainRoomscale);
}

void UGripMotionControllerComponent::ApplyTrackingParameters(FVector& OriginalPosition, bool bIsInGameThread, bool bApplyZeroing)
{
	if (bScaleTracking)
	{
		OriginalPosition *= TrackingScaler;
	}

	if (bLimitMinHeight)
	{
		OriginalPosition.Z = FMath::Max(OriginalPosition.Z, MinimumHeight);
	}

	if (bLimitMaxHeight)
	{
		OriginalPosition.Z = FMath::Min(OriginalPosition.Z, MaximumHeight);
	}

	if (bApplyZeroing && (/*bOffsetByHMD ||*/ bLeashToHMD || (AttachChar && !AttachChar->bRetainRoomscale)))
	{
		if (bIsInGameThread)
		{
			if (IsLocallyControlled() && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowedForWorld(*GetWorld()))
			{
				FQuat curRot;
				FVector curLoc;
				if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, curRot, curLoc))
				{

					/*if (IsValid(AttachChar) && AttachChar->VRReplicatedCamera)
					{
						AttachChar->VRReplicatedCamera->ApplyTrackingParameters(curLoc, true);
					}*/

					//curLoc.Z = 0;
					LastLocationForLateUpdate = curLoc;

					if (IsValid(AttachChar) && !AttachChar->bRetainRoomscale)
					{
						if (AttachChar->VRMovementReference && AttachChar->VRMovementReference->GetReplicatedMovementMode() == EVRConjoinedMovementModes::C_VRMOVE_Seated)
						{

						}
						else
						{
							FRotator StoredCameraRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curRot.Rotator());
							LastLocationForLateUpdate += StoredCameraRotOffset.RotateVector(FVector(AttachChar->VRRootReference->VRCapsuleOffset.X, AttachChar->VRRootReference->VRCapsuleOffset.Y, 0.0f));
						}
					}
				}
			}
			else
			{
				if (IsValid(AttachChar) && AttachChar->VRReplicatedCamera)
				{
					// Sample camera location instead
					LastLocationForLateUpdate = AttachChar->VRReplicatedCamera->ReplicatedCameraTransform.Position; //GetRelativeLocation();

					if (!AttachChar->bRetainRoomscale && IsLocallyControlled())
					{
						if (AttachChar->VRMovementReference && AttachChar->VRMovementReference->GetReplicatedMovementMode() == EVRConjoinedMovementModes::C_VRMOVE_Seated)
						{

						}
						else
						{
							FRotator StoredCameraRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(AttachChar->VRReplicatedCamera->GetRelativeRotation());
							LastLocationForLateUpdate += StoredCameraRotOffset.RotateVector(FVector(AttachChar->VRRootReference->VRCapsuleOffset.X, AttachChar->VRRootReference->VRCapsuleOffset.Y, 0.0f));
						}
					}
				}
			}
		}

		// #TODO: This is technically unsafe, need to use a seperate value like the transforms for the render thread
		// If I ever delete the simple char then this setup can just go away anyway though
		// It has a data race condition right now though
		FVector CorrectLastLocation = bIsInGameThread ? LastLocationForLateUpdate : LateUpdateParams.GripRenderThreadLastLocationForLateUpdate;

		if (bLeashToHMD)
		{
			FVector DifferenceVec = OriginalPosition - CorrectLastLocation;

			if (DifferenceVec.SizeSquared() > FMath::Square(LeashRange))
			{
				OriginalPosition = CorrectLastLocation + (DifferenceVec.GetSafeNormal() * LeashRange);
			}
		}

		if (/*bOffsetByHMD ||*/ (AttachChar && !AttachChar->bRetainRoomscale))
		{
			OriginalPosition -= FVector(CorrectLastLocation.X, CorrectLastLocation.Y, 0.0f);
		}
	}
}

void UGripMotionControllerComponent::OnModularFeatureUnregistered(const FName& Type, class IModularFeature* ModularFeature)
{
	FScopeLock Lock(&PolledMotionControllerMutex);

	if (ModularFeature == PolledMotionController_GameThread)
	{
		PolledMotionController_GameThread = nullptr;
	}
	if (ModularFeature == PolledMotionController_RenderThread)
	{
		PolledMotionController_RenderThread = nullptr;
	}
}


//=============================================================================
bool UGripMotionControllerComponent::GripPollControllerState(FVector& Position, FRotator& Orientation, float WorldToMetersScale)
{
	if (IsInGameThread())
	{
		bool OutbProvidedLinearVelocity;
		bool OutbProvidedAngularVelocity;
		bool OutbProvidedLinearAcceleration;
		FVector OutLinearVelocity;
		FVector OutAngularVelocityAsAxisAndLength;
		FVector OutLinearAcceleration;
		return GripPollControllerState_GameThread(Position, Orientation, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityAsAxisAndLength, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
	}
	else
	{
		return GripPollControllerState_RenderThread(Position, Orientation, WorldToMetersScale);
	}
}

bool UGripMotionControllerComponent::GripPollControllerState_GameThread(FVector& Position, FRotator& Orientation, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityAsAxisAndLength, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale)
{
	// Not calling PollControllerState from the parent because its private.......

	bool bIsInGameThread = true;

	if (bHasAuthority)
	{
		{
			FScopeLock Lock(&PolledMotionControllerMutex);
			PolledMotionController_GameThread = nullptr;
			bPolledHMD_GameThread = false;
		}

		// #NOTE: This was adding in 4.20, I presume to allow for HMDs as tracking sources for mixed reality.
		// Skipping all of my special logic here for now
		if (MotionSource == IMotionController::HMDSourceId || MotionSource == IMotionController::HeadSourceId)
		{
			IXRTrackingSystem* TrackingSys = GEngine->XRSystem.Get();
			if (TrackingSys)
			{
				FQuat OrientationQuat;
				if (TrackingSys->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, OrientationQuat, Position))
				{
					Orientation = OrientationQuat.Rotator();
					{
						FScopeLock Lock(&PolledMotionControllerMutex);
						bPolledHMD_GameThread = true;  // We only want a render thread update from the hmd if we polled it on the game thread.
					}
					return true;
				}
			}
		}
		else
		{
			//GripUEMotionController::FScopeLockOptional LockOptional;
			TArray<IMotionController*> MotionControllers;
			MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
			for (auto MotionController : MotionControllers)
			{
				if (MotionController == nullptr)
				{
					continue;
				}

				if (bIsInGameThread)
				{
					EControllerHand HandType;
					GetHandType(HandType);
					FName GripSource = (HandType == EControllerHand::Left) ? FName("LeftGrip") : FName("RightGrip");

					CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, MotionSource);
					if (!bIgnoreTrackingStatus && CurrentTrackingStatus == ETrackingStatus::NotTracked)
						continue;
				}

				if (MotionController->GetControllerOrientationAndPosition(PlayerIndex, MotionSource, Orientation, Position, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityAsAxisAndLength, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale))
				{
					/*#if PLATFORM_PS4
					// Moving this in here to work around a PSVR module bug
					if (bIsInGameThread)
					{
						CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, MotionSource);
						if (CurrentTrackingStatus == ETrackingStatus::NotTracked)
							continue;
					}
					#endif*/

					if (HasTrackingParameters())
					{
						ApplyTrackingParameters(Position, bIsInGameThread);
					}

					if (bOffsetByControllerProfile)
					{
						FTransform FinalControllerTransform(Orientation, Position);
						if (bIsInGameThread)
						{
							FinalControllerTransform = CurrentControllerProfileTransform * FinalControllerTransform;
						}
						else
						{
							FinalControllerTransform = LateUpdateParams.GripRenderThreadProfileTransform * FinalControllerTransform;
						}

						Orientation = FinalControllerTransform.Rotator();
						Position = FinalControllerTransform.GetTranslation();
					}

					InUseMotionController = MotionController;
					OnMotionControllerUpdated();
					InUseMotionController = nullptr;

					{
						FScopeLock Lock(&PolledMotionControllerMutex);
						PolledMotionController_GameThread = MotionController;  // We only want a render thread update from the motion controller we polled on the game thread.
					}
					return true;
				}

				/*#if PLATFORM_PS4
				else if (bIsInGameThread)
				{
					CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, MotionSource);
				}
				#endif*/
			}
		}
	}
	return false;
}

bool UGripMotionControllerComponent::GripPollControllerState_RenderThread(FVector& Position, FRotator& Orientation, float WorldToMetersScale)
{
	check(IsInRenderingThread());
	bool bIsInGameThread = false;

	if (PolledMotionController_RenderThread)
	{
		EControllerHand HandType;
		GetHandType(HandType);
		FName GripSource = (HandType == EControllerHand::Left) ? FName("LeftGrip") : FName("RightGrip");

		CurrentTrackingStatus = PolledMotionController_RenderThread->GetControllerTrackingStatus(PlayerIndex, GripSource);
		if (PolledMotionController_RenderThread->GetControllerOrientationAndPosition(PlayerIndex, MotionSource, Orientation, Position, WorldToMetersScale))
		{
			if (HasTrackingParameters())
			{
				ApplyTrackingParameters(Position, bIsInGameThread);
			}

			if (bOffsetByControllerProfile)
			{
				FTransform FinalControllerTransform(Orientation, Position);
				if (bIsInGameThread)
				{
					FinalControllerTransform = CurrentControllerProfileTransform * FinalControllerTransform;
				}
				else
				{
					FinalControllerTransform = LateUpdateParams.GripRenderThreadProfileTransform * FinalControllerTransform;
				}

				Orientation = FinalControllerTransform.Rotator();
				Position = FinalControllerTransform.GetTranslation();
			}
			return true;
		}
	}

	if (bPolledHMD_RenderThread)
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

	return false;
}

//=============================================================================
UGripMotionControllerComponent::FGripViewExtension::FGripViewExtension(const FAutoRegister& AutoRegister, UGripMotionControllerComponent* InMotionControllerComponent)
	: FSceneViewExtensionBase(AutoRegister)
	, MotionControllerComponent(InMotionControllerComponent)
{
	FSceneViewExtensionIsActiveFunctor IsActiveFunc;
	IsActiveFunc.IsActiveFunction = [this](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
	{
		check(IsInGameThread());

		static const auto CVarEnableMotionControllerLateUpdate = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("vr.EnableMotionControllerLateUpdate"));
		return MotionControllerComponent && !MotionControllerComponent->bDisableLowLatencyUpdate && MotionControllerComponent->bTracked && CVarEnableMotionControllerLateUpdate->GetValueOnGameThread();
	};

	IsActiveThisFrameFunctions.Add(IsActiveFunc);
}


void UGripMotionControllerComponent::FGripViewExtension::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	FTransform OldTransform;
	FTransform NewTransform;

	{
		FScopeLock ScopeLock(&CritSect);

		if (!MotionControllerComponent)
			return;

		{
			FScopeLock Lock(&MotionControllerComponent->PolledMotionControllerMutex);
			MotionControllerComponent->PolledMotionController_RenderThread = MotionControllerComponent->PolledMotionController_GameThread;
		}

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
		FVector Position = MotionControllerComponent->LateUpdateParams.GripRenderThreadRelativeTransform.GetTranslation();
		FRotator Orientation = MotionControllerComponent->LateUpdateParams.GripRenderThreadRelativeTransform.GetRotation().Rotator();

		if (!MotionControllerComponent->GripPollControllerState_RenderThread(Position, Orientation, WorldToMetersScale))
		{
			return;
		}

		if (MotionControllerComponent->LateUpdateParams.bRenderSmoothHandTracking)
		{
			FTransform CalcedTransform = FTransform(Orientation, Position, MotionControllerComponent->LateUpdateParams.GripRenderThreadComponentScale);

			if (MotionControllerComponent->LateUpdateParams.bRenderSmoothWithEuroLowPassFunction)
			{
				CalcedTransform = MotionControllerComponent->LateUpdateParams.RenderEuroSmoothingParams.RunFilterSmoothing(CalcedTransform, MotionControllerComponent->LateUpdateParams.RenderLastDeltaTime);
				//SetRelativeTransform(RenderEuroSmoothingParams.RunFilterSmoothing(CalcedTransform, RenderLastDeltaTime));
			}
			else
			{
				if (MotionControllerComponent->LateUpdateParams.RenderSmoothingSpeed <= 0.f || MotionControllerComponent->LateUpdateParams.RenderLastSmoothRelativeTransform.Equals(FTransform::Identity))
				{
					//SetRelativeTransform(CalcedTransform);
				}
				else
				{
					const float Alpha = FMath::Clamp(MotionControllerComponent->LateUpdateParams.RenderLastDeltaTime * MotionControllerComponent->LateUpdateParams.RenderSmoothingSpeed, 0.f, 1.f);
					MotionControllerComponent->LateUpdateParams.RenderLastSmoothRelativeTransform.Blend(MotionControllerComponent->LateUpdateParams.RenderLastSmoothRelativeTransform, CalcedTransform, Alpha);
					CalcedTransform = MotionControllerComponent->LateUpdateParams.RenderLastSmoothRelativeTransform;
					//SetRelativeTransform(LastSmoothRelativeTransform);
				}
			}

			// Set smoothed properties
			NewTransform = CalcedTransform;
		}
		else
		{
			NewTransform = FTransform(Orientation, Position, MotionControllerComponent->LateUpdateParams.GripRenderThreadComponentScale);
		}

		OldTransform = MotionControllerComponent->LateUpdateParams.GripRenderThreadRelativeTransform;
		//NewTransform = FTransform(Orientation, Position, MotionControllerComponent->GripRenderThreadComponentScale);
		MotionControllerComponent->LateUpdateParams.GripRenderThreadRelativeTransform = NewTransform;
	} // Release lock on motion controller component

	  // Tell the late update manager to apply the offset to the scene components
	LateUpdate.Apply_RenderThread(InViewFamily.Scene, OldTransform, NewTransform);
	// #TODO: UE5 is missing this pull
	//LateUpdate.Apply_RenderThread(InViewFamily.Scene, InViewFamily.bLateLatchingEnabled ? InViewFamily.FrameNumber : -1, OldTransform, NewTransform);
}

/*void UGripMotionControllerComponent::FGripViewExtension::LateLatchingViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	SCOPED_NAMED_EVENT(UMotionControllerComponent_Latch, FColor::Orange);
	if (!MotionControllerComponent)
	{
		return;
	}

	FTransform OldTransform;
	FTransform NewTransform;
	{
		FScopeLock ScopeLock(&CritSect);
		if (!MotionControllerComponent)
		{
			return;
		}

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
		MotionControllerComponent->GripRenderThreadRelativeTransform = NewTransform;

	} // Release the lock on the MotionControllerComponent

	// Tell the late update manager to apply the offset to the scene components
	LateUpdate.Apply_RenderThread(InViewFamily.Scene, InViewFamily.FrameNumber, OldTransform, NewTransform);
}*/

bool UGripMotionControllerComponent::K2_GetFirstActiveGrip(FBPActorGripInformation& FirstActiveGrip)
{
	FBPActorGripInformation* FirstGrip = GetFirstActiveGrip();

	if (FirstGrip)
	{
		FirstActiveGrip = *FirstGrip;
		return true;
	}

	return false;
}

FBPActorGripInformation* UGripMotionControllerComponent::GetFirstActiveGrip()
{
	for (FBPActorGripInformation& Grip : GrippedObjects)
	{
		if (Grip.IsValid() && !Grip.bIsPaused)
		{
			return &Grip;
		}
	}

	for (FBPActorGripInformation& LocalGrip : LocallyGrippedObjects)
	{
		if (LocalGrip.IsValid() && !LocalGrip.bIsPaused)
		{
			return &LocalGrip;
		}
	}

	return nullptr;
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
void UGripMotionControllerComponent::Client_NotifyInvalidLocalGrip_Implementation(UObject * LocallyGrippedObject, uint8 GripID, bool bWasAGripConflict)
{
	if (GripID != INVALID_VRGRIP_ID)
	{
		if (FBPActorGripInformation* GripInfo = GetGripPtrByID(GripID))
		{
			DropObjectByInterface(nullptr, GripID);

			if (LocallyGrippedObject && bWasAGripConflict)
			{
				OnClientAuthGripConflict.Broadcast(LocallyGrippedObject, ClientAuthConflictResolutionMethod);
			}

			return;
		}		
	}

	FBPActorGripInformation FoundGrip;
	EBPVRResultSwitch Result;

	GetGripByObject(FoundGrip, LocallyGrippedObject, Result);

	if (Result == EBPVRResultSwitch::OnFailed)
		return;

	// Drop it, server told us that it was a bad grip
	DropObjectByInterface(nullptr, FoundGrip.GripID);

	if (LocallyGrippedObject && bWasAGripConflict)
	{
		OnClientAuthGripConflict.Broadcast(LocallyGrippedObject, ClientAuthConflictResolutionMethod);
	}
}

bool UGripMotionControllerComponent::Server_NotifyHandledTransaction_Validate(uint8 GripID)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifyHandledTransaction_Implementation(uint8 GripID)
{
	for (int i = LocalTransactionBuffer.Num() - 1; i >= 0; i--)
	{
		if(LocalTransactionBuffer[i].GripID == GripID)
			LocalTransactionBuffer.RemoveAt(i);
	}

#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, LocalTransactionBuffer, this);
#endif
}

bool UGripMotionControllerComponent::Server_NotifyLocalGripAddedOrChanged_Validate(const FBPActorGripInformation & newGrip)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifyLocalGripAddedOrChanged_Implementation(const FBPActorGripInformation & newGrip)
{
	DIRTY_LOCALLY_GRIPPED_OBJECTS();

	if (!newGrip.GrippedObject || newGrip.GripMovementReplicationSetting != EGripMovementReplicationSettings::ClientSide_Authoritive)
	{
		Client_NotifyInvalidLocalGrip(newGrip.GrippedObject, newGrip.GripID);
		return;
	}

	if (!LocallyGrippedObjects.Contains(newGrip))
	{

		bool bImplementsInterface = newGrip.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass());

		TArray<FBPGripPair> HoldingControllers;
		bool bIsHeld = false;
		if (bImplementsInterface)
		{
			IVRGripInterface::Execute_IsHeld(newGrip.GrippedObject, HoldingControllers, bIsHeld);

			if (bIsHeld && ClientAuthConflictResolutionMethod != EVRClientAuthConflictResolutionMode::VRGRIP_CONFLICT_None)
			{
				// Its held and doesn't allow more than one grip at a time
				if (!IVRGripInterface::Execute_AllowsMultipleGrips(newGrip.GrippedObject))
				{
					// Lets see if a different owner is holding it, if so, deny this request
					for (FBPGripPair& GripPair : HoldingControllers)
					{
						if (!GripPair.HoldingController || GripPair.GripID == INVALID_VRGRIP_ID)
						{
							continue;
						}

						// If the controllers have different owners (if its the same then consider them as locally transacting and let it go)
						if (GripPair.HoldingController->GetOwner()->GetNetOwner() != this->GetOwner()->GetNetOwner())
						{
							switch (ClientAuthConflictResolutionMethod)
							{
							case EVRClientAuthConflictResolutionMode::VRGRIP_CONFLICT_First:
							{
								// Deny the grip, another connection already came and gripped it
								Client_NotifyInvalidLocalGrip(newGrip.GrippedObject, newGrip.GripID, true);

								OnClientAuthGripConflict.Broadcast(newGrip.GrippedObject, ClientAuthConflictResolutionMethod);
								return;
							}break;
							case EVRClientAuthConflictResolutionMode::VRGRIP_CONFLICT_Last:
							{				
								// Deny the old grip, another connection came and gripped it
								GripPair.HoldingController->DropObjectByInterface(newGrip.GrippedObject, GripPair.GripID);
								GripPair.HoldingController->Client_NotifyInvalidLocalGrip(newGrip.GrippedObject, GripPair.GripID, true);
								OnClientAuthGripConflict.Broadcast(newGrip.GrippedObject, ClientAuthConflictResolutionMethod);
							}break;
							case EVRClientAuthConflictResolutionMode::VRGRIP_CONFLICT_DropAll:
							{
								// Deny both grips
								Client_NotifyInvalidLocalGrip(newGrip.GrippedObject, newGrip.GripID, true);
								GripPair.HoldingController->DropObjectByInterface(newGrip.GrippedObject, GripPair.GripID);
								GripPair.HoldingController->Client_NotifyInvalidLocalGrip(newGrip.GrippedObject, GripPair.GripID, true);
								OnClientAuthGripConflict.Broadcast(newGrip.GrippedObject, ClientAuthConflictResolutionMethod);
								return;
							}break;
							case EVRClientAuthConflictResolutionMode::VRGRIP_CONFLICT_None:
							default:
							{
								OnClientAuthGripConflict.Broadcast(newGrip.GrippedObject, ClientAuthConflictResolutionMethod);
							}break;

							}
						}
					}
				}
			}
		}


		UPrimitiveComponent* PrimComp = nullptr;
		AActor* pActor = nullptr;

		PrimComp = newGrip.GetGrippedComponent();
		pActor = newGrip.GetGrippedActor();

		if (!PrimComp && pActor)
		{
			PrimComp = Cast<UPrimitiveComponent>(pActor->GetRootComponent());
		}
		else if (!pActor && PrimComp)
		{
			pActor = PrimComp->GetOwner();
		}

		if (!PrimComp || !pActor)
		{
			Client_NotifyInvalidLocalGrip(newGrip.GrippedObject, newGrip.GripID);
			return;
		}

		bool bHadOriginalSettings = false;
		bool bOriginalGravity = false;
		bool bOriginalReplication = false;

		if (bImplementsInterface)
		{
			//if (IVRGripInterface::Execute_DenyGripping(root))
			//	return false; // Interface is saying not to grip it right now
			if (bIsHeld)
			{
				// If we are held by multiple controllers then lets copy our original values from the first one	
				if (HoldingControllers.Num() > 0 && HoldingControllers[0].HoldingController != nullptr)
				{
					FBPActorGripInformation* gripInfo = HoldingControllers[0].HoldingController->GetGripPtrByID(HoldingControllers[0].GripID);

					if (gripInfo != nullptr)
					{
						bHadOriginalSettings = true;
						bOriginalGravity = gripInfo->bOriginalGravity;
						bOriginalReplication = gripInfo->bOriginalReplicatesMovement;
					}
				}
			}
		}

		int32 NewIndex = LocallyGrippedObjects.Add(newGrip);
		DIRTY_LOCALLY_GRIPPED_OBJECTS();

		if (NewIndex != INDEX_NONE && LocallyGrippedObjects.Num() > 0)
		{
			if (bHadOriginalSettings)
			{
				LocallyGrippedObjects[NewIndex].bOriginalReplicatesMovement = bOriginalReplication;
				LocallyGrippedObjects[NewIndex].bOriginalGravity = bOriginalGravity;
			}
			else
			{
				LocallyGrippedObjects[NewIndex].bOriginalReplicatesMovement = pActor->IsReplicatingMovement();
				LocallyGrippedObjects[NewIndex].bOriginalGravity = PrimComp->IsGravityEnabled();
			}

			HandleGripReplication(LocallyGrippedObjects[NewIndex]);
		}

		// Initialize the differences, clients will do this themselves on the rep back, this sets up the cache
		//HandleGripReplication(LocallyGrippedObjects[LocallyGrippedObjects.Num() - 1]);
	}
	else
	{
		int32 IndexFound = INDEX_NONE;
		if (LocallyGrippedObjects.Find(newGrip, IndexFound) && IndexFound != INDEX_NONE)
		{
			FBPActorGripInformation OriginalGrip = LocallyGrippedObjects[IndexFound];
			LocallyGrippedObjects[IndexFound].RepCopy(newGrip);
			HandleGripReplication(LocallyGrippedObjects[IndexFound], &OriginalGrip);
		}
	}

	// Server has to call this themselves
	//OnRep_LocallyGrippedObjects();
}


bool UGripMotionControllerComponent::Server_NotifyLocalGripRemoved_Validate(uint8 GripID, const FTransform_NetQuantize &TransformAtDrop, FVector_NetQuantize100 OptAngularVelocity, FVector_NetQuantize100 OptLinearVelocity)
{
	return true;
}

void UGripMotionControllerComponent::Server_NotifyLocalGripRemoved_Implementation(uint8 GripID, const FTransform_NetQuantize &TransformAtDrop, FVector_NetQuantize100 OptAngularVelocity, FVector_NetQuantize100 OptLinearVelocity)
{
	DIRTY_LOCALLY_GRIPPED_OBJECTS();

	FBPActorGripInformation FoundGrip;
	EBPVRResultSwitch Result;
	GetGripByID(FoundGrip, GripID, Result);

	if (Result == EBPVRResultSwitch::OnFailed)
		return;

	if (FoundGrip.GripCollisionType != EGripCollisionType::EventsOnly)
	{
		switch (FoundGrip.GripTargetType)
		{
		case EGripTargetType::ActorGrip:
		{
			if (AActor* DroppingActor = FoundGrip.GetGrippedActor())
			{
				if (IsValid(DroppingActor) && TransformAtDrop.IsValid())
				{
					DroppingActor->SetActorTransform(TransformAtDrop, false, nullptr, ETeleportType::None);
				}
			}
		}break;
		case EGripTargetType::ComponentGrip:
		{
			if (UPrimitiveComponent* DroppingComp = FoundGrip.GetGrippedComponent())
			{
				if (IsValid(DroppingComp) && TransformAtDrop.IsValid())
				{
					DroppingComp->SetWorldTransform(TransformAtDrop, false, nullptr, ETeleportType::None);
				}
			}
		}break;
		default:break;
		}
	}

	if (!DropObjectByInterface_Implementation(nullptr, FoundGrip.GripID, OptAngularVelocity, OptLinearVelocity, true))
	{
		DropGrip_Implementation(FoundGrip, false, OptAngularVelocity, OptLinearVelocity,true);
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
	DIRTY_LOCALLY_GRIPPED_OBJECTS();

	FBPActorGripInformation * GripInfo = LocallyGrippedObjects.FindByKey(GripID);
	if (GripInfo != nullptr)
	{
		FBPActorGripInformation OriginalGrip = *GripInfo;

		// I override the = operator now so that it won't set the lerp components
		GripInfo->SecondaryGripInfo.RepCopy(SecondaryGripInfo);

		// Initialize the differences, clients will do this themselves on the rep back
		HandleGripReplication(*GripInfo, &OriginalGrip);
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
	DIRTY_LOCALLY_GRIPPED_OBJECTS();

	FBPActorGripInformation * GripInfo = LocallyGrippedObjects.FindByKey(GripID);
	if (GripInfo != nullptr)
	{
		FBPActorGripInformation OriginalGrip = *GripInfo;

		// I override the = operator now so that it won't set the lerp components
		GripInfo->SecondaryGripInfo.RepCopy(SecondaryGripInfo);
		GripInfo->RelativeTransform = NewRelativeTransform;

		// Initialize the differences, clients will do this themselves on the rep back
		HandleGripReplication(*GripInfo, &OriginalGrip);
	}

}
void UGripMotionControllerComponent::GetControllerDeviceID(FXRDeviceId & DeviceID, EBPVRResultSwitch &Result, bool bCheckOpenVROnly)
{
	EControllerHand ControllerHandIndex;
	if (!IMotionController::GetHandEnumForSourceName(MotionSource, ControllerHandIndex))
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
}

void FExpandedLateUpdateManager::Setup(const FTransform& ParentToWorld, UGripMotionControllerComponent* Component, bool bSkipLateUpdate)
{
	if (!Component)
		return;

	check(IsInGameThread());


	UpdateStates[LateUpdateGameWriteIndex].Primitives.Reset();
	UpdateStates[LateUpdateGameWriteIndex].ParentToWorld = ParentToWorld;

	//Add additional late updates registered to this controller that aren't children and aren't gripped
	//This array is editable in blueprint and can be used for things like arms or the like.
	for (UPrimitiveComponent* primComp : Component->AdditionalLateUpdateComponents)
	{
		if (primComp)
			GatherLateUpdatePrimitives(primComp);
	}

	ProcessGripArrayLateUpdatePrimitives(Component, Component->LocallyGrippedObjects);
	ProcessGripArrayLateUpdatePrimitives(Component, Component->GrippedObjects);

	GatherLateUpdatePrimitives(Component);
	//GatherLateUpdatePrimitives(Component);

	UpdateStates[LateUpdateGameWriteIndex].bSkip = bSkipLateUpdate;
	++UpdateStates[LateUpdateGameWriteIndex].TrackingNumber;

	int32 NextFrameRenderReadIndex = LateUpdateGameWriteIndex;
	LateUpdateGameWriteIndex = 1 - LateUpdateGameWriteIndex;

	ENQUEUE_RENDER_COMMAND(UpdateLateUpdateRenderReadIndexCommand)(
		[NextFrameRenderReadIndex, this](FRHICommandListImmediate& RHICmdList)
		{
			LateUpdateRenderReadIndex = NextFrameRenderReadIndex;
		});

}

//void FExpandedLateUpdateManager::Apply_RenderThread(FSceneInterface* Scene, const int32 FrameNumber, const FTransform& OldRelativeTransform, const FTransform& NewRelativeTransform)
void FExpandedLateUpdateManager::Apply_RenderThread(FSceneInterface* Scene, const FTransform& OldRelativeTransform, const FTransform& NewRelativeTransform)
{
	FRHICommandListBase& RHICmdList = FRHICommandListImmediate::Get();

	check(IsInRenderingThread());

	if (!UpdateStates[LateUpdateRenderReadIndex].Primitives.Num() || UpdateStates[LateUpdateRenderReadIndex].bSkip)
	{
		return;
	}

	const FTransform OldCameraTransform = OldRelativeTransform * UpdateStates[LateUpdateRenderReadIndex].ParentToWorld;
	const FTransform NewCameraTransform = NewRelativeTransform * UpdateStates[LateUpdateRenderReadIndex].ParentToWorld;
	const FMatrix LateUpdateTransform = (OldCameraTransform.Inverse() * NewCameraTransform).ToMatrixWithScale();

	bool bIndicesHaveChanged = false;

	/*
	// Under HMD late-latching senario, Apply_RenderThread will be called twice in same frame under PreRenderViewFamily_RenderThread and 
	// LateLatchingViewFamily_RenderThread. We don't want to apply PrimitivePair.Value = -1 directly on UpdateStates[LateUpdateRenderReadIndex].Primitives
	// because the modification will affect second Apply_RenderThread's logic under LateLatchingViewFamily_RenderThread.
	// Since the list is very small(only affect stuff attaching to the controller),  we just make a local copy PrimitivesLocal.
	TMap<FPrimitiveSceneInfo*, int32> PrimitivesLocal = UpdateStates[LateUpdateRenderReadIndex].Primitives;
	for (auto& PrimitivePair : PrimitivesLocal)*/
	for (auto& PrimitivePair : UpdateStates[LateUpdateRenderReadIndex].Primitives)
	{
		if (PrimitivePair.Value == -1)
			continue;

		FPrimitiveSceneInfo* RetrievedSceneInfo = Scene->GetPrimitiveSceneInfo(PrimitivePair.Value);
		FPrimitiveSceneInfo* CachedSceneInfo = PrimitivePair.Key;

		// If the retrieved scene info is different than our cached scene info then the scene has changed in the meantime
		// and we need to search through the entire scene to make sure it still exists.
		if (CachedSceneInfo != RetrievedSceneInfo)
		{
			bIndicesHaveChanged = true;
			break; // No need to continue here, as we are going to brute force the scene primitives below anyway.
		}
		else if (CachedSceneInfo->Proxy)
		{
			CachedSceneInfo->Proxy->ApplyLateUpdateTransform(RHICmdList, LateUpdateTransform);
			PrimitivePair.Value = -1; // Set the cached index to -1 to indicate that this primitive was already processed
			/*if (FrameNumber >= 0)
			{
				CachedSceneInfo->Proxy->SetPatchingFrameNumber(FrameNumber);
			}*/
		}
	}

	// Indices have changed, so we need to scan the entire scene for primitives that might still exist
	if (bIndicesHaveChanged)
	{
		int32 Index = 0;
		FPrimitiveSceneInfo* RetrievedSceneInfo = Scene->GetPrimitiveSceneInfo(Index++);
		while (RetrievedSceneInfo)
		{
			/*int32* PrimitiveIndex = PrimitivesLocal.Find(RetrievedSceneInfo);
			if (RetrievedSceneInfo->Proxy && PrimitiveIndex != nullptr && *PrimitiveIndex >= 0)*/
			if (RetrievedSceneInfo->Proxy && UpdateStates[LateUpdateRenderReadIndex].Primitives.Contains(RetrievedSceneInfo) && UpdateStates[LateUpdateRenderReadIndex].Primitives[RetrievedSceneInfo] >= 0)
			{
				RetrievedSceneInfo->Proxy->ApplyLateUpdateTransform(RHICmdList, LateUpdateTransform);
				/*if (FrameNumber >= 0)
				{
					RetrievedSceneInfo->Proxy->SetPatchingFrameNumber(FrameNumber);
				}*/
			}
			RetrievedSceneInfo = Scene->GetPrimitiveSceneInfo(Index++);
		}
	}
}

void FExpandedLateUpdateManager::CacheSceneInfo(USceneComponent* Component)
{
	ensureMsgf(!Component->IsUsingAbsoluteLocation() && !Component->IsUsingAbsoluteRotation(), TEXT("SceneComponents that use absolute location or rotation are not supported by the LateUpdateManager"));
	// If a scene proxy is present, cache it
	UPrimitiveComponent* PrimitiveComponent = dynamic_cast<UPrimitiveComponent*>(Component);
	if (PrimitiveComponent && PrimitiveComponent->SceneProxy)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveComponent->SceneProxy->GetPrimitiveSceneInfo();
		if (PrimitiveSceneInfo && PrimitiveSceneInfo->IsIndexValid())
		{
			UpdateStates[LateUpdateGameWriteIndex].Primitives.Emplace(PrimitiveSceneInfo, PrimitiveSceneInfo->GetIndex());
		}
	}
}

void FExpandedLateUpdateManager::GatherLateUpdatePrimitives(USceneComponent* ParentComponent)
{
	CacheSceneInfo(ParentComponent);
	TArray<USceneComponent*> DirectComponents;

	// Std late updates
	ParentComponent->GetChildrenComponents(true, DirectComponents);
	for (USceneComponent* Component : DirectComponents)
	{
		if (Component != nullptr)
		{
			CacheSceneInfo(Component);
		}
	}
}

void FExpandedLateUpdateManager::ProcessGripArrayLateUpdatePrimitives(UGripMotionControllerComponent * MotionControllerComponent, TArray<FBPActorGripInformation> & GripArray)
{
	for (FBPActorGripInformation actor : GripArray)
	{
		// Skip actors that are colliding if turning off late updates during collision.
		// Also skip turning off late updates for SweepWithPhysics, as it should always be locked to the hand
		if (!actor.GrippedObject || actor.GripCollisionType == EGripCollisionType::EventsOnly)
			continue;

		// Handle late updates even with attachment, we need to add it to a skip list for the primary gatherer to process
		if (actor.GripCollisionType == EGripCollisionType::AttachmentGrip)
		{
			continue;
		}

		// Don't allow late updates with server sided movement, there is no point
		if (actor.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceServerSideMovement && !MotionControllerComponent->IsServer())
			continue;

		// Don't late update paused grips
		if (actor.bIsPaused)
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

		// Don't run late updates if we have a grip script that denies it
		if (actor.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			TArray<UVRGripScriptBase*> GripScripts;
			if (IVRGripInterface::Execute_GetGripScripts(actor.GrippedObject, GripScripts))
			{
				bool bContinueOn = false;
				for (UVRGripScriptBase* Script : GripScripts)
				{
					if (Script && Script->IsScriptActive() && Script->Wants_DenyLateUpdates())
					{
						bContinueOn = true;
						break;
					}
				}

				if (bContinueOn)
					continue;
			}
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

void UGripMotionControllerComponent::GetHandType(EControllerHand& Hand)
{
	if (!IMotionController::GetHandEnumForSourceName(MotionSource, Hand))
	{
		// Check if the palm motion source extension is being used
		// I assume eventually epic will handle this case
		if (MotionSource.Compare(FName(TEXT("RightPalm"))) == 0 || MotionSource.Compare(FName(TEXT("RightWrist"))) == 0)
		{
			Hand = EControllerHand::Right;
		}
		// Could skip this and default to left now but would rather check
		else if (MotionSource.Compare(FName(TEXT("LeftPalm"))) == 0 || MotionSource.Compare(FName(TEXT("LeftWrist"))) == 0)
		{
			Hand = EControllerHand::Left;
		}
		else
		{
			Hand = EControllerHand::Left;
		}
	}
}

void UGripMotionControllerComponent::SetCustomPivotComponent(USceneComponent * NewCustomPivotComponent, FName PivotSocketName)
{
	CustomPivotComponent = NewCustomPivotComponent;
	CustomPivotComponentSocketName = PivotSocketName;
}

FTransform UGripMotionControllerComponent::GetPivotTransform_BP()
{
	return GetPivotTransform();
}

FVector UGripMotionControllerComponent::GetPivotLocation_BP()
{
	return GetPivotLocation();
}

FTransform UGripMotionControllerComponent::ConvertToControllerRelativeTransform(const FTransform & InTransform)
{
	return InTransform.GetRelativeTransform(!bSkipPivotTransformAdjustment && IsValid(CustomPivotComponent) ? CustomPivotComponent->GetSocketTransform(CustomPivotComponentSocketName) : this->GetComponentTransform());
}

FTransform UGripMotionControllerComponent::ConvertToGripRelativeTransform(const FTransform& GrippedActorTransform, const FTransform & InTransform)
{
	return InTransform.GetRelativeTransform(GrippedActorTransform);
}

bool UGripMotionControllerComponent::GetIsObjectHeld(const UObject * ObjectToCheck)
{
	if (!ObjectToCheck)
		return false;

	return (GrippedObjects.FindByKey(ObjectToCheck) || LocallyGrippedObjects.FindByKey(ObjectToCheck));
}

bool UGripMotionControllerComponent::GetIsHeld(const AActor * ActorToCheck)
{
	if (!ActorToCheck)
		return false;

	return (GrippedObjects.FindByKey(ActorToCheck) || LocallyGrippedObjects.FindByKey(ActorToCheck));
}

bool UGripMotionControllerComponent::GetIsComponentHeld(const UPrimitiveComponent * ComponentToCheck)
{
	if (!ComponentToCheck)
		return false;

	return (GrippedObjects.FindByKey(ComponentToCheck) || LocallyGrippedObjects.FindByKey(ComponentToCheck));

	//return false;
}

bool UGripMotionControllerComponent::GetIsSecondaryAttachment(const USceneComponent * ComponentToCheck, FBPActorGripInformation & Grip)
{
	if (!ComponentToCheck)
		return false;

	for (int i = 0; i < GrippedObjects.Num(); ++i)
	{
		if (GrippedObjects[i].SecondaryGripInfo.bHasSecondaryAttachment && GrippedObjects[i].SecondaryGripInfo.SecondaryAttachment == ComponentToCheck)
		{
			Grip = GrippedObjects[i];
			return true;
		}
	}

	for (int i = 0; i < LocallyGrippedObjects.Num(); ++i)
	{
		if (LocallyGrippedObjects[i].SecondaryGripInfo.bHasSecondaryAttachment && LocallyGrippedObjects[i].SecondaryGripInfo.SecondaryAttachment == ComponentToCheck)
		{
			Grip = LocallyGrippedObjects[i];
			return true;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::HasGrippedObjects()
{
	return GrippedObjects.Num() > 0 || LocallyGrippedObjects.Num() > 0;
}

bool UGripMotionControllerComponent::SetUpPhysicsHandle_BP(const FBPActorGripInformation &Grip)
{
	return SetUpPhysicsHandle(Grip);
}

bool UGripMotionControllerComponent::DestroyPhysicsHandle_BP(const FBPActorGripInformation &Grip)
{
	return DestroyPhysicsHandle(Grip);
}

bool UGripMotionControllerComponent::UpdatePhysicsHandle_BP(const FBPActorGripInformation& Grip, bool bFullyRecreate)
{
	return UpdatePhysicsHandle(Grip.GripID, bFullyRecreate);
}

bool UGripMotionControllerComponent::GetPhysicsHandleSettings(const FBPActorGripInformation& Grip, FBPAdvancedPhysicsHandleSettings& PhysicsHandleSettingsOut)
{
	FBPActorPhysicsHandleInformation * HandleInfo = GetPhysicsGrip(Grip);
	
	if (!HandleInfo)
		return false;

	PhysicsHandleSettingsOut.FillFrom(HandleInfo);
	return true;
}

bool UGripMotionControllerComponent::SetPhysicsHandleSettings(const FBPActorGripInformation& Grip, const FBPAdvancedPhysicsHandleSettings& PhysicsHandleSettingsIn)
{
	FBPActorPhysicsHandleInformation* HandleInfo = GetPhysicsGrip(Grip);

	if (!HandleInfo)
		return false;

	PhysicsHandleSettingsIn.FillTo(HandleInfo);
	return UpdatePhysicsHandle(Grip, true);
}


void UGripMotionControllerComponent::UpdatePhysicsHandleTransform_BP(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform)
{
	return UpdatePhysicsHandleTransform(GrippedActor, NewTransform);
}

bool UGripMotionControllerComponent::GetGripDistance_BP(FBPActorGripInformation &Grip, FVector ExpectedLocation, float & CurrentDistance)
{
	if (!Grip.GrippedObject)
		return false;

	UPrimitiveComponent * RootComp = nullptr;

	if (Grip.GripTargetType == EGripTargetType::ActorGrip)
	{
		RootComp = Cast<UPrimitiveComponent>(Grip.GetGrippedActor()->GetRootComponent());
	}
	else
		RootComp = Grip.GetGrippedComponent();

	if (!RootComp)
		return false;

	FVector CheckDistance;
	if (!GetPhysicsJointLength(Grip, RootComp, CheckDistance))
	{
		CheckDistance = (ExpectedLocation - RootComp->GetComponentLocation());
	}
	// Set grip distance now for people to use
	CurrentDistance = CheckDistance.Size();
	return true;
}

bool UGripMotionControllerComponent::GripControllerIsTracked() const
{
	return IsTracked();
}

bool UGripMotionControllerComponent::HasAuthority() const
{
	return bHasAuthority;
}

void UGripMotionControllerComponent::CheckTransactionBuffer()
{
	if (LocalTransactionBuffer.Num())
	{
		for (int i = LocalTransactionBuffer.Num() - 1; i >= 0; --i)
		{
			if (LocalTransactionBuffer[i].ValueCache.bWasInitiallyRepped && LocalTransactionBuffer[i].GripID != LocalTransactionBuffer[i].ValueCache.CachedGripID)
			{
				// There appears to be a bug with TArray replication where if you replace an index with another value of that
				// Index, it doesn't fully re-init the object, this is a workaround to re-zero non replicated variables
				// when that happens.
				LocalTransactionBuffer[i].ClearNonReppingItems();
			}

			if (!LocalTransactionBuffer[i].ValueCache.bWasInitiallyRepped && LocalTransactionBuffer[i].GrippedObject->IsValidLowLevelFast())
			{
				LocalTransactionBuffer[i].ValueCache.bWasInitiallyRepped = true;
				LocalTransactionBuffer[i].ValueCache.CachedGripID = LocalTransactionBuffer[i].GripID;

				int32 Index = LocallyGrippedObjects.Add(LocalTransactionBuffer[i]);
				DIRTY_LOCALLY_GRIPPED_OBJECTS();

				if (Index != INDEX_NONE)
				{
					NotifyGrip(LocallyGrippedObjects[Index]);
				}

				Server_NotifyHandledTransaction(LocalTransactionBuffer[i].GripID);
			}
		}
	}
}

/////////////////////////////////////////////////
//- Push networking getter / setter functions
/////////////////////////////////////////////////

void UGripMotionControllerComponent::SetSmoothReplicatedMotion(bool bNewSmoothReplicatedMotion)
{
	bSmoothReplicatedMotion = bNewSmoothReplicatedMotion;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, bSmoothReplicatedMotion, this);
#endif
}

void UGripMotionControllerComponent::SetReplicateWithoutTracking(bool bNewReplicateWithoutTracking)
{
	bReplicateWithoutTracking = bNewReplicateWithoutTracking;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, bReplicateWithoutTracking, this);
#endif
}

void UGripMotionControllerComponent::SetControllerNetUpdateRate(float NewControllerNetUpdateRate)
{
	ControllerNetUpdateRate = NewControllerNetUpdateRate;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, ControllerNetUpdateRate, this);
#endif
}

void UGripMotionControllerComponent::DIRTY_GRIPPED_OBJECTS()
{
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, GrippedObjects, this);
#endif
}

void UGripMotionControllerComponent::DIRTY_LOCALLY_GRIPPED_OBJECTS()
{
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UGripMotionControllerComponent, LocallyGrippedObjects, this);
#endif
}

/////////////////////////////////////////////////
//- End Push networking getter / setter functions
/////////////////////////////////////////////////