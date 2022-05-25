// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "IMotionController.h"
#include "SceneViewExtension.h"
#include "VRBPDatatypes.h"
#include "MotionControllerComponent.h"
#include "LateUpdateManager.h"
#include "IIdentifiableXRDevice.h" // for FXRDeviceId
#include "IXRTrackingSystem.h"
#include "VRGripInterface.h"
#include "VRGlobalSettings.h"
#include "GripScripts/VRGripScriptBase.h"
#include "Math/DualQuat.h"
#include "XRMotionControllerBase.h" // for GetHandEnumForSourceName()
#include "GripMotionControllerComponent.generated.h"

class AVRBaseCharacter;

/**
*
*/

/** Override replication control variable for inherited properties that are private. Be careful since it removes a compile-time error when the variable doesn't exist */
// This is a temp macro until epic adds their own equivilant
#define DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(c,v,active) \
{ \
	static FProperty* sp##v = GetReplicatedProperty(StaticClass(), c::StaticClass(),FName(TEXT(#v))); \
	for (int32 i = 0; i < sp##v->ArrayDim; i++) \
	{ \
		ChangedPropertyTracker.SetCustomIsActiveOverride(this, sp##v->RepIndex + i, active); \
	} \
}

#define RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(c,v,cond)  ResetReplicatedLifetimeProperty(StaticClass(), c::StaticClass(), FName(TEXT(#v)), cond, OutLifetimeProps);

DECLARE_LOG_CATEGORY_EXTERN(LogVRMotionController, Log, All);
//For UE4 Profiler ~ Stat Group
DECLARE_STATS_GROUP(TEXT("TICKGrip"), STATGROUP_TickGrip, STATCAT_Advanced);

/** Delegate for notification when the controllers tracking changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRGripControllerOnTrackingEventSignature, const ETrackingStatus &, NewTrackingStatus);

/** Delegate for notification when the controller grips a new object. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVROnControllerGripSignature, const FBPActorGripInformation &, GripInformation);

/** Delegate for notification when the controller drops a gripped object. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVROnControllerDropSignature, const FBPActorGripInformation &, GripInformation, bool, bWasSocketed);

/** Delegate for notification when the controller sockets a gripped object. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FVROnControllerSocketSignature, const FBPActorGripInformation&, GripInformation, const USceneComponent*, NewParentComp, FName, OptionalSocketName, FTransform, RelativeTransformToParent, bool, bWeldingBodies);

/** Delegate for notification when the controller teleports its grips. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVROnControllerTeleportedGripsSignature);

/** Delegate for notification when an interactive grip goes out of range and isn't set to auto handle it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVRGripControllerOnGripOutOfRange, const FBPActorGripInformation &, GripInformation, float, Distance);

/** Delegate for notification when the controller profile transform changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVRGripControllerOnProfileTransformChanged, const FTransform &, NewRelTransForProcComps, const FTransform &, NewProfileTransform);

/**
* Utility class for applying an offset to a hierarchy of components in the renderer thread.
*/
class VREXPANSIONPLUGIN_API FExpandedLateUpdateManager
{
public:
	FExpandedLateUpdateManager();

	virtual ~FExpandedLateUpdateManager() {}

	/** Setup state for applying the render thread late update */
	void Setup(const FTransform& ParentToWorld, UGripMotionControllerComponent* Component, bool bSkipLateUpdate);

	/** Apply the late update delta to the cached components */
	void Apply_RenderThread(FSceneInterface* Scene, const int32 FrameNumber, const FTransform& OldRelativeTransform, const FTransform& NewRelativeTransform);
	
	/** Returns true if the LateUpdateSetup data is stale. */
	bool GetSkipLateUpdate_RenderThread() const { return UpdateStates[LateUpdateRenderReadIndex].bSkip; }

public:

	/** A utility method that calls CacheSceneInfo on ParentComponent and all of its descendants */
	void GatherLateUpdatePrimitives(USceneComponent* ParentComponent);
	void ProcessGripArrayLateUpdatePrimitives(UGripMotionControllerComponent* MotionController, TArray<FBPActorGripInformation> & GripArray);

	/** Generates a LateUpdatePrimitiveInfo for the given component if it has a SceneProxy and appends it to the current LateUpdatePrimitives array */
	void CacheSceneInfo(USceneComponent* Component);

	struct FLateUpdateState
	{
		FLateUpdateState()
			: ParentToWorld(FTransform::Identity)
			, bSkip(false)
			, TrackingNumber(-1)
		{}

		/** Parent world transform used to reconstruct new world transforms for late update scene proxies */
		FTransform ParentToWorld;
		/** Primitives that need late update before rendering */
		TMap<FPrimitiveSceneInfo*, int32> Primitives;
		/** Late Update Info Stale, if this is found true do not late update */
		bool bSkip;
		/** Frame tracking number - used to flag if the game and render threads get badly out of sync */
		int64 TrackingNumber;
	};

	FLateUpdateState UpdateStates[2];
	int32 LateUpdateGameWriteIndex;
	int32 LateUpdateRenderReadIndex;
};

/**
* Tick function that does post physics work. This executes in EndPhysics (after physics is done)
**/
USTRUCT()
struct FGripComponentEndPhysicsTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

		UGripMotionControllerComponent* Target;

	/**
	* Abstract function to execute the tick.
	* @param DeltaTime - frame time to advance, in seconds.
	* @param TickType - kind of tick for this frame.
	* @param CurrentThread - thread we are executing on, useful to pass along as new tasks are created.
	* @param MyCompletionGraphEvent - completion event for this task. Useful for holding the completetion of this task until certain child tasks are complete.
	*/
	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	/** Abstract function to describe this tick. Used to print messages about illegal cycles in the dependency graph. */
	virtual FString DiagnosticMessage() override;
	/** Function used to describe this tick for active tick reporting. **/
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FGripComponentEndPhysicsTickFunction> : public TStructOpsTypeTraitsBase2<FGripComponentEndPhysicsTickFunction>
{
	enum
	{
		WithCopy = false
	};
};

/**
* An override of the MotionControllerComponent that implements position replication and Gripping with grip replication and controllable late updates per object.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = MotionController)
class VREXPANSIONPLUGIN_API UGripMotionControllerComponent : public UMotionControllerComponent//PrimitiveComponent
{

public:

	FGripComponentEndPhysicsTickFunction EndPhysicsTickFunction;
	friend struct FGripComponentEndPhysicsTickFunction;

	/** Update systems after physics sim is done */
	void EndPhysicsTickComponent(FGripComponentEndPhysicsTickFunction& ThisTickFunction);
	void RegisterEndPhysicsTick(bool bRegister);





	// The grip script that defines the default behaviors of grips
	// Don't edit this unless you really know what you are doing, leave it empty
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
		TSubclassOf<class UVRGripScriptBase> DefaultGripScriptClass;
	
	// This is the pointer to the default grip script
	// It is here to access so if you want to set some variables on your override then you can
	// Due to a bug with instanced variables and parent classes you can't directly edit this in subclass in the details panel
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "GripMotionController")
	UVRGripScriptBase* DefaultGripScript;

	// Lerping functions and events
	void InitializeLerpToHand(FBPActorGripInformation& GripInfo);
	void HandleGlobalLerpToHand(FBPActorGripInformation& GripInformation, FTransform& WorldTransform, float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		void CancelGlobalLerpToHand(uint8 GripID);

	//UPROPERTY(BlueprintAssignable, Category = "Grip Events")
	//	FVROnControllerGripSignature OnLerpToHandBegin;

	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerGripSignature OnLerpToHandFinished;

	// If true will subtract the HMD's location from the position, useful for if the actors base is set to the HMD location always (simple character).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
	bool bOffsetByHMD;

	// When true any physics constraints will be attached to the grip pivot instead of a new kinematic actor in the scene
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
		bool bConstrainToPivot;

	UPROPERTY()
		TWeakObjectPtr<AVRBaseCharacter> AttachChar;
	void UpdateTracking(float DeltaTime);
	virtual void OnAttachmentChanged() override;

	FVector LastLocationForLateUpdate;
	FTransform LastRelativePosition;

	// If true will smooth the hand tracking data with a TInterp function
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController|Smoothing")
		bool bSmoothHandTracking;

	bool bWasSmoothingHand;

	// If true will smooth hand tracking with the Linear and Rotational 1 Euro low pass settings instead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController|Smoothing")
		bool bSmoothWithEuroLowPassFunction;

	// The interp speed to use if smoothing is enabled and not using the 1 Euro smoothing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController|Smoothing")
		float SmoothingSpeed;

	// Smoothing parameters when using the 1 Euro low pass option
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController|Smoothing")
		FBPEuroLowPassFilterTrans EuroSmoothingParams;

	FTransform LastSmoothRelativeTransform;

	// Type of velocity calculation to use for the motion controller
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController|ComponentVelocity")
		EVRVelocityType VelocityCalculationType;

	// If we should sample the velocity in world or local space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController|ComponentVelocity")
		bool bSampleVelocityInWorldSpace;

	// If not using velocity mode "default" this is the number of sample to keep
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController|ComponentVelocity")
		int32 VelocitySamples;

	FBPLowPassPeakFilter PeakFilter;

	virtual FVector GetComponentVelocity() const override;

	// If true will offset the tracked location of the controller by the controller profile that is currently loaded.
	// Thows the event OnControllerProfileTransformChanged when it happens so that you can adjust specific components
	// Like procedural ones for the offset (procedural meshes are already correctly offset for the controller and
	// need rewound.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
		bool bOffsetByControllerProfile;

	// Stores current transform so we don't have to keep casting
	FTransform CurrentControllerProfileTransform;

	// Called when the controller profile changed and we have a new transform (only if bOffsetByControllerProfile is true)
	UPROPERTY(BlueprintAssignable, Category = "GripMotionController")
		FVRGripControllerOnProfileTransformChanged OnControllerProfileTransformChanged;

	// Called when the controller profile changed and we have a new transform (only if bOffsetByControllerProfile is true)
	UPROPERTY(BlueprintAssignable, Category = "GripMotionController")
		FVRGripControllerOnGripOutOfRange OnGripOutOfRange;

private:

	GENERATED_BODY()

public:
	UGripMotionControllerComponent(const FObjectInitializer& ObjectInitializer);

	~UGripMotionControllerComponent();

	// Custom version of the component sweep function to remove that aggravating warning epic is throwing about skeletal mesh components.
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void InitializeComponent() override;
	virtual void OnUnregister() override;
	//virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	virtual void Deactivate() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void BeginDestroy() override;
	virtual void BeginPlay() override;

	/** Post-physics tick function for this character */
	//UPROPERTY()
	//	FTickFunction PostPhysicsTickFunction;

protected:
	//~ Begin UActorComponent Interface.
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void SendRenderTransform_Concurrent() override;
	//~ End UActorComponent Interface.

	FTransform GripRenderThreadRelativeTransform;
	FVector GripRenderThreadComponentScale;
	FTransform GripRenderThreadProfileTransform;

	FDelegateHandle NewControllerProfileEvent_Handle;
	UFUNCTION()
	void NewControllerProfileLoaded();
	void GetCurrentProfileTransform(bool bBindToNoticationDelegate);

public:

	// Called when a controller first gets a valid tracked frame
	UPROPERTY(BlueprintAssignable, Category = "GripMotionController")
		FVRGripControllerOnTrackingEventSignature OnTrackingChanged;

	// Called when a object is gripped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerGripSignature OnGrippedObject;

	// Called when a object is dropped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerDropSignature OnDroppedObject;

	// Called when a object is being socketed, prior to OnDrop being called and prior to the actual socketing being performed
	// Generally an early entry to detach hands and handle pre-socketing logic
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerSocketSignature OnSocketingObject;

	// Called when a gripped object has been teleported
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerTeleportedGripsSignature OnTeleportedGrips;

	// Called when an object we hold is secondary gripped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerGripSignature OnSecondaryGripAdded;

	// Called when an object we hold is secondary dropped
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerGripSignature OnSecondaryGripRemoved;

	// Called when an object we hold has its grip transform changed
	UPROPERTY(BlueprintAssignable, Category = "Grip Events")
		FVROnControllerGripSignature OnGripTransformChanged;

	// Gets the hand enum
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "HandType", CompactNodeTitle = "HandType"))
		void GetHandType(EControllerHand& Hand);

	// The component to use for basing the grip off of instead of the motion controller
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GripMotionController|CustomPivot")
		TWeakObjectPtr<USceneComponent> CustomPivotComponent;

	// The socket for the component to use for basing the grip off of instead of the motion controller
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GripMotionController|CustomPivot")
		FName CustomPivotComponentSocketName;

	// If true then we will skip the pivot transform adjustment when gripping an object with the custom pivot
	// This is here for legacy support for anyone not using "ConvertToControllerRelativeTransform".
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GripMotionController|CustomPivot")
		bool bSkipPivotTransformAdjustment;

	// Set the custom pivot component, allows you to use remote grips easier
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|CustomPivot")
		void SetCustomPivotComponent(USceneComponent * NewCustomPivotComponent, FName PivotSocketName = NAME_None);

	// Set the custom pivot component, allows you to use remote grips easier
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "GetPivotTransform"))
		FTransform GetPivotTransform_BP();

	// Set the custom pivot component, allows you to use remote grips easier
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "GetPivotLocation"))
		FVector GetPivotLocation_BP();

	FORCEINLINE FTransform GetPivotTransform()
	{
		return CustomPivotComponent.IsValid() ? CustomPivotComponent->GetSocketTransform(CustomPivotComponentSocketName) : this->GetComponentTransform();
	}

	FORCEINLINE FVector GetPivotLocation()
	{
		return CustomPivotComponent.IsValid() ? CustomPivotComponent->GetSocketLocation(CustomPivotComponentSocketName) : this->GetComponentLocation();
	}

	// Increments with each grip, wraps back to 0 after max due to modulo operation
	// I don't think that a 254 (126 total grips) grip index is going to be used up and allow duplicates unless
	// someone does something crazy
	uint8 GripIDIncrementer;


	// INVALID_VRGRIP_ID is 0, so + 1 is 1
	inline uint8 GetNextGripID(bool bIsLocalGrip)
	{
		// We are skipping index 0 to leave it for INVALID_GRIP_ID index;

		if (!bIsLocalGrip) // We need to split them between 0-126 for gripped objects server side
		{
			if (GripIDIncrementer < 127)
				GripIDIncrementer++;
			else
				GripIDIncrementer = (INVALID_VRGRIP_ID + 1);

			return GripIDIncrementer;
		}
		else // And 128 - 254 for local grips client side, with half for server initiated and half for client
		{

			if (!IsServer())
			{
				if (GripIDIncrementer < 63)
					GripIDIncrementer++;
				else
					GripIDIncrementer = (INVALID_VRGRIP_ID + 1);

				return GripIDIncrementer + 128;
			}
			else
			{
				if (GripIDIncrementer < 63)
					GripIDIncrementer++;
				else
					GripIDIncrementer = (INVALID_VRGRIP_ID + 1);

				return GripIDIncrementer + 128 + 64;
			}
		}
	}

	// When possible I suggest that you use GetAllGrips/GetGrippedObjects instead of directly referencing this
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "GripMotionController", ReplicatedUsing = OnRep_GrippedObjects)
	TArray<FBPActorGripInformation> GrippedObjects;

	// When possible I suggest that you use GetAllGrips/GetGrippedObjects instead of directly referencing this
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "GripMotionController", ReplicatedUsing = OnRep_LocallyGrippedObjects)
	TArray<FBPActorGripInformation> LocallyGrippedObjects;

	// Local Grip TransactionalBuffer to store server sided grips that need to be emplaced into the local buffer
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "GripMotionController", ReplicatedUsing = OnRep_LocalTransaction)
		TArray<FBPActorGripInformation> LocalTransactionBuffer;

	// Locally Gripped Array functions

	// Notify a client that their local grip was bad
	UFUNCTION(Reliable, Client, Category = "GripMotionController")
	void Client_NotifyInvalidLocalGrip(UObject * LocallyGrippedObject, uint8 GripID);

	// Notify the server that we locally gripped something
	UFUNCTION(Reliable, Server, WithValidation, Category = "GripMotionController")
	void Server_NotifyLocalGripAddedOrChanged(const FBPActorGripInformation & newGrip);

	// Notify the server that we changed some secondary attachment information
	UFUNCTION(Reliable, Server, WithValidation)
		void Server_NotifySecondaryAttachmentChanged(
			uint8 GripID,
			const FBPSecondaryGripInfo& SecondaryGripInfo);

	// Notify the server that we changed some secondary attachment information
	// This one specifically sends out the new relative location for a retain secondary grip
	UFUNCTION(Reliable, Server, WithValidation)
		void Server_NotifySecondaryAttachmentChanged_Retain(
			uint8 GripID,
			const FBPSecondaryGripInfo& SecondaryGripInfo, const FTransform_NetQuantize & NewRelativeTransform);

	// Notify change on relative position editing as well, make RPCS callable in blueprint
	// Notify the server that we locally gripped something
	UFUNCTION(Reliable, Server, WithValidation)
	void Server_NotifyLocalGripRemoved(uint8 GripID, const FTransform_NetQuantize &TransformAtDrop, FVector_NetQuantize100 AngularVelocity, FVector_NetQuantize100 LinearVelocity);
	
	// Handle a server initiated grip
	UFUNCTION(Reliable, Server, WithValidation, Category = "GripMotionController")
		void Server_NotifyHandledTransaction(uint8 GripID);

	// Enable this to send the TickGrip event every tick even for non custom grip types - has a slight performance hit
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
	bool bAlwaysSendTickGrip;

	// Clean up a grip that is "bad", object is being destroyed or was a bad destructible mesh
	void CleanUpBadGrip(TArray<FBPActorGripInformation> &GrippedObjectsArray, int GripIndex, bool bReplicatedArray);
	void CleanUpBadPhysicsHandles();

	// Recreates a grip physics handle bodies
	// If FullRecreate is false then it will only set the COM and actors, otherwise will re-init the entire grip
	bool UpdatePhysicsHandle(uint8 GripID, bool bFullyRecreate = true);
	bool UpdatePhysicsHandle(const FBPActorGripInformation & GripInfo, bool bFullyRecreate = true);

	inline void NotifyGripTransformChanged(const FBPActorGripInformation & GripInfo)
	{
		if (OnGripTransformChanged.IsBound())
		{
			FBPActorGripInformation CurrentGrip;
			EBPVRResultSwitch Result;
			GetGripByID(CurrentGrip, GripInfo.GripID, Result);
			if (Result == EBPVRResultSwitch::OnSucceeded)
			{
				OnGripTransformChanged.Broadcast(CurrentGrip);
			}
		}
	}

	// Recreates a grip in situations where the collision type or movement replication type may have been changed
	inline void ReCreateGrip(FBPActorGripInformation & GripInfo)
	{
		int HandleIndex = 0;
		if (GetPhysicsGripIndex(GripInfo, HandleIndex))
		{

			DestroyPhysicsHandle(&PhysicsGrips[HandleIndex]);
			PhysicsGrips.RemoveAt(HandleIndex);
		}

		// Grip Type or replication was changed
		NotifyGrip(GripInfo, true);
	}

	// Handles variable state changes and specific actions on a grip replication
	inline bool HandleGripReplication(FBPActorGripInformation & Grip, FBPActorGripInformation * OldGripInfo = nullptr)
	{
		if (Grip.ValueCache.bWasInitiallyRepped && Grip.GripID != Grip.ValueCache.CachedGripID)
		{
			// There appears to be a bug with TArray replication where if you replace an index with another value of that
			// Index, it doesn't fully re-init the object, this is a workaround to re-zero non replicated variables
			// when that happens.
			Grip.ClearNonReppingItems();
		}

		// Ignore server down no rep grips, this is kind of unavoidable unless I make yet another list which I don't want to do
		if (Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep)
		{
			// skip init
			Grip.ValueCache.bWasInitiallyRepped = true;

			// null ptr so this doesn't block grip operations
			Grip.GrippedObject = nullptr;

			// Set to paused so iteration skips it
			Grip.bIsPaused = true;
		}

		if (!Grip.ValueCache.bWasInitiallyRepped) // Hasn't already been initialized
		{
			Grip.ValueCache.bWasInitiallyRepped = NotifyGrip(Grip); // Grip it

			// Tick will keep checking from here on out locally
			if (!Grip.ValueCache.bWasInitiallyRepped)
			{
				//UE_LOG(LogVRMotionController, Warning, TEXT("Replicated grip Notify grip failed, was grip called before the object was replicated to the client?"));
				return false;
			}

			if(Grip.SecondaryGripInfo.bHasSecondaryAttachment)
			{
				// Reset the secondary grip distance
				Grip.SecondaryGripInfo.SecondaryGripDistance = 0.0f;

				if (FMath::IsNearlyZero(Grip.SecondaryGripInfo.LerpToRate)) // Zero, could use IsNearlyZero instead
					Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::NotLerping;
				else
				{
					Grip.SecondaryGripInfo.curLerp = Grip.SecondaryGripInfo.LerpToRate;
					Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::StartLerp;
				}

				if (Grip.GrippedObject && Grip.GrippedObject->IsValidLowLevelFast() && Grip.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
				{
					IVRGripInterface::Execute_OnSecondaryGrip(Grip.GrippedObject, this, Grip.SecondaryGripInfo.SecondaryAttachment, Grip);

					TArray<UVRGripScriptBase*> GripScripts;
					if (IVRGripInterface::Execute_GetGripScripts(Grip.GrippedObject, GripScripts))
					{
						for (UVRGripScriptBase* Script : GripScripts)
						{
							if (Script)
							{
								Script->OnSecondaryGrip(this, Grip.SecondaryGripInfo.SecondaryAttachment, Grip);
							}
						}
					}
				}

				OnSecondaryGripAdded.Broadcast(Grip);	
			}
			//Grip.ValueCache.bWasInitiallyRepped = true; // Set has been initialized
		}
		else if(OldGripInfo != nullptr) // Check for changes from cached information if we aren't skipping the delta check
		{
			// Manage lerp states
			if ((OldGripInfo->SecondaryGripInfo.bHasSecondaryAttachment != Grip.SecondaryGripInfo.bHasSecondaryAttachment) ||
				(OldGripInfo->SecondaryGripInfo.SecondaryAttachment != Grip.SecondaryGripInfo.SecondaryAttachment) ||
				(!OldGripInfo->SecondaryGripInfo.SecondaryRelativeTransform.Equals(Grip.SecondaryGripInfo.SecondaryRelativeTransform)))
			{
				// Reset the secondary grip distance
				Grip.SecondaryGripInfo.SecondaryGripDistance = 0.0f;

				if (FMath::IsNearlyZero(Grip.SecondaryGripInfo.LerpToRate)) // Zero, could use IsNearlyZero instead
					Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::NotLerping;
				else
				{
					// New lerp
					if (Grip.SecondaryGripInfo.bHasSecondaryAttachment)
					{
						Grip.SecondaryGripInfo.curLerp = Grip.SecondaryGripInfo.LerpToRate;
						Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::StartLerp;
					}
					else // Post Lerp
					{
						Grip.SecondaryGripInfo.curLerp = Grip.SecondaryGripInfo.LerpToRate;
						Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::EndLerp;
					}
				}

				bool bSendReleaseEvent = ((!Grip.SecondaryGripInfo.bHasSecondaryAttachment && OldGripInfo->SecondaryGripInfo.bHasSecondaryAttachment) ||
										((Grip.SecondaryGripInfo.bHasSecondaryAttachment && OldGripInfo->SecondaryGripInfo.bHasSecondaryAttachment) &&
										(OldGripInfo->SecondaryGripInfo.SecondaryAttachment != Grip.SecondaryGripInfo.SecondaryAttachment)));

				bool bSendGripEvent =	(Grip.SecondaryGripInfo.bHasSecondaryAttachment && 
										(!OldGripInfo->SecondaryGripInfo.bHasSecondaryAttachment || (OldGripInfo->SecondaryGripInfo.SecondaryAttachment != Grip.SecondaryGripInfo.SecondaryAttachment)));

				if (bSendReleaseEvent)
				{
					if (Grip.GrippedObject && Grip.GrippedObject->IsValidLowLevelFast() && Grip.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
					{
						IVRGripInterface::Execute_OnSecondaryGripRelease(Grip.GrippedObject, this, OldGripInfo->SecondaryGripInfo.SecondaryAttachment, Grip);

						TArray<UVRGripScriptBase*> GripScripts;
						if (IVRGripInterface::Execute_GetGripScripts(Grip.GrippedObject, GripScripts))
						{
							for (UVRGripScriptBase* Script : GripScripts)
							{
								if (Script)
								{
									Script->OnSecondaryGripRelease(this, OldGripInfo->SecondaryGripInfo.SecondaryAttachment, Grip);
								}
							}
						}
					}

					OnSecondaryGripRemoved.Broadcast(Grip);
				}

				if (bSendGripEvent)
				{
					if (Grip.GrippedObject && Grip.GrippedObject->IsValidLowLevelFast() && Grip.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
					{
						IVRGripInterface::Execute_OnSecondaryGrip(Grip.GrippedObject, this, Grip.SecondaryGripInfo.SecondaryAttachment, Grip);

						TArray<UVRGripScriptBase*> GripScripts;
						if (IVRGripInterface::Execute_GetGripScripts(Grip.GrippedObject, GripScripts))
						{
							for (UVRGripScriptBase* Script : GripScripts)
							{
								if (Script)
								{
									Script->OnSecondaryGrip(this, Grip.SecondaryGripInfo.SecondaryAttachment, Grip);
								}
							}
						}
					}

					OnSecondaryGripAdded.Broadcast(Grip);
				}
			}

			if (OldGripInfo->GripCollisionType != Grip.GripCollisionType ||
				OldGripInfo->GripMovementReplicationSetting != Grip.GripMovementReplicationSetting ||
				OldGripInfo->GrippedBoneName != Grip.GrippedBoneName ||
				OldGripInfo->AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings != Grip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings
				)
			{
				ReCreateGrip(Grip); // Need to re-create grip
			}
			else // If re-creating the grip anyway we don't need to do the below
			{
				bool bTransformChanged = !OldGripInfo->RelativeTransform.Equals(Grip.RelativeTransform);

				// If physics settings got changed server side
				if (!FMath::IsNearlyEqual(OldGripInfo->Stiffness, Grip.Stiffness) ||
					!FMath::IsNearlyEqual(OldGripInfo->Damping, Grip.Damping) ||
					OldGripInfo->AdvancedGripSettings.PhysicsSettings != Grip.AdvancedGripSettings.PhysicsSettings ||
					bTransformChanged
					)
				{
					UpdatePhysicsHandle(Grip);

					if (bTransformChanged)
					{
						NotifyGripTransformChanged(Grip);
					}
				}
			}
		}

		// Set caches now for next rep
		Grip.ValueCache.CachedGripID = Grip.GripID;

		return true;
	}

	inline void CheckTransactionBuffer()
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

					if (Index != INDEX_NONE)
					{
						NotifyGrip(LocallyGrippedObjects[Index]);
					}

					Server_NotifyHandledTransaction(LocalTransactionBuffer[i].GripID);				
				}
			}
		}
	}

	UFUNCTION()
	virtual void OnRep_LocalTransaction(TArray<FBPActorGripInformation> OriginalArrayState) // Original array state is useless without full serialize, it just hold last delta
	{
		CheckTransactionBuffer();
	}

	UFUNCTION()
	virtual void OnRep_GrippedObjects(TArray<FBPActorGripInformation> OriginalArrayState) // Original array state is useless without full serialize, it just hold last delta
	{
		// Need to think about how best to handle the simulating flag here, don't handle for now
		// Check for removed gripped actors
		// This might actually be better left as an RPC multicast

		for (int i = GrippedObjects.Num() - 1; i >= 0; --i)
		{
			HandleGripReplication(GrippedObjects[i], OriginalArrayState.FindByKey(GrippedObjects[i].GripID));
		}
	}

	UFUNCTION()
	virtual void OnRep_LocallyGrippedObjects(TArray<FBPActorGripInformation> OriginalArrayState)
	{
		for (int i = LocallyGrippedObjects.Num() - 1; i >= 0; --i)
		{
			HandleGripReplication(LocallyGrippedObjects[i], OriginalArrayState.FindByKey(LocallyGrippedObjects[i].GripID));
		}
	}

	UPROPERTY(BlueprintReadWrite, Category = "GripMotionController")
	TArray<UPrimitiveComponent *> AdditionalLateUpdateComponents;

	//  Movement Replication
	// Actor needs to be replicated for this to work

	UPROPERTY(EditDefaultsOnly, ReplicatedUsing = OnRep_ReplicatedControllerTransform, Category = "GripMotionController|Networking")
	FBPVRComponentPosRep ReplicatedControllerTransform;

	FVector LastUpdatesRelativePosition;
	FRotator LastUpdatesRelativeRotation;

	bool bLerpingPosition;
	bool bReppedOnce;

	UFUNCTION()
	virtual void OnRep_ReplicatedControllerTransform()
	{
		//ReplicatedControllerTransform.Unpack();

		if (bSmoothReplicatedMotion)
		{
			if (bReppedOnce)
			{
				bLerpingPosition = true;
				ControllerNetUpdateCount = 0.0f;
				LastUpdatesRelativePosition = this->GetRelativeLocation();
				LastUpdatesRelativeRotation = this->GetRelativeRotation();
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

	// Rate to update the position to the server, 100htz is default (same as replication rate, should also hit every tick).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "GripMotionController|Networking", meta = (ClampMin = "0", UIMin = "0"))
	float ControllerNetUpdateRate;
	
	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case, also used for remotes to lerp position
	float ControllerNetUpdateCount;

	// Whether to smooth (lerp) between ticks for the replicated motion, DOES NOTHING if update rate is larger than FPS!
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "GripMotionController|Networking")
		bool bSmoothReplicatedMotion;

	// Whether to replicate even if no tracking (FPS or test characters)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "GripMotionController|Networking")
		bool bReplicateWithoutTracking;

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
	void Server_SendControllerTransform(FBPVRComponentPosRep NewTransform);

	// Pointer to an override to call from the owning character - this saves 7 bits a rep avoiding component IDs on the RPC
	typedef void (AVRBaseCharacter::*VRBaseCharTransformRPC_Pointer)(FBPVRComponentPosRep NewTransform);
	VRBaseCharTransformRPC_Pointer OverrideSendTransform;

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	inline bool IsLocallyControlled() const
	{
		// I like epics new authority check more than mine
		const AActor* MyOwner = GetOwner();
		return MyOwner->HasLocalNetOwner();
		//const APawn* MyPawn = Cast<APawn>(MyOwner);
		//return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner && MyOwner->Role == ENetRole::ROLE_Authority);
	}

	// Returns if this is the owning connection for the motion controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "IsLocallyControlled"))
		bool BP_IsLocallyControlled();

	// Checks if the controllers own is torn off on the network, used to skip some RPCS
	inline bool IsTornOff() const
	{
		const AActor* MyOwner = GetOwner();
		return MyOwner ? MyOwner->GetTearOff() : false;
	}


	inline bool IsServer() const
	{
		if (GEngine != nullptr && GWorld != nullptr)
		{
			return GEngine->GetNetMode(GWorld) < NM_Client;
		}

		return false;
	}

	// Drops a gripped object and sockets it to the given component at the given relative transform.
	// *Note*: If both the parent and the child are simulating it also delays a single tick and then re-applies the relative transform.
	// This is to avoid a race condition where we need to wait for the next physics update.
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool DropAndSocketObject(const FTransform_NetQuantize & RelativeTransformToParent, UObject * ObjectToDrop = nullptr, uint8 GripIDToDrop = 0, USceneComponent * SocketingParent = nullptr, FName OptionalSocketName = NAME_None, bool bWeldBodies = true);
	
	// Drops a grip and sockets it to the given component at the given relative transform.
	// *Note*: If both the parent and the child are simulating it also delays a single tick and then re-applies the relative transform.
	// This is to avoid a race condition where we need to wait for the next physics update.
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool DropAndSocketGrip(const FBPActorGripInformation &GripToDrop, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent, bool bWeldBodies = true);
		bool DropAndSocketGrip_Implementation(const FBPActorGripInformation& GripToDrop, USceneComponent* SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize& RelativeTransformToParent, bool bWeldBodies = true, bool bSkipServerNotify = false);
	
		// Notify the server about a new drop and socket
	UFUNCTION(Reliable, Server, WithValidation, Category = "GripMotionController")
		void Server_NotifyDropAndSocketGrip(uint8 GripID, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent, bool bWeldBodies = true);

	UFUNCTION(Reliable, NetMulticast)
		void NotifyDropAndSocket(const FBPActorGripInformation &NewDrop, USceneComponent* SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize& RelativeTransformToParent, bool bWeldBodies = true);

	void DropAndSocket_Implementation(const FBPActorGripInformation &NewDrop);
	void Socket_Implementation(UObject * ObjectToSocket, bool bWasSimulating, USceneComponent * SocketingParent, FName OptionalSocketName, const FTransform_NetQuantize & RelativeTransformToParent, bool bWeldBodies = true);

	// Keep a hard reference to the drop to avoid deletion errors
	UPROPERTY()
	TArray<UObject*> ObjectsWaitingForSocketUpdate;

	// Resets the transform of a socketed grip 1 tick later, this is to avoid a race condition with simulating grips.
	// Their constraint can change the transform before or after the attachment happens if the parent and the child are both simulating.
	UFUNCTION()
		void SetSocketTransform(UObject* ObjectToSocket, /*USceneComponent * SocketingParent,*/ const FTransform_NetQuantize RelativeTransformToParent/*, FName OptionalSocketName, bool bWeldBodies*/);

	/* Auto grip any uobject that is/root is a primitive component and has the VR Grip Interface
	these are stored in a Tarray that will prevent destruction of the object, you MUST ungrip an actor if you want to kill it
	The WorldOffset is the transform that it will remain away from the controller, if you use the world position of the actor then it will grab
	at the point of intersection.

	If WorldOffsetIsRelative is true then it will not convert the transform from world space but will instead use that offset directly.
	You could pass in a socket relative transform with this set for snapping or an empty transform to snap the object at its 0,0,0 point.

	If you declare a valid OptionSnapToSocketName then it will instead snap the actor to the relative offset
	location that the socket is to its parent actor. 
	
	It will only do this if the WorldOffset value is left default, if it is not, then it will treat this as the name of the slot
	that you already have the transform for.

	If you declare a valid OptionalBoneToGripName then it will grip that physics body with physics grips (It will expect a bone worldspace transform then,
	if you pass in the normal actor/root component world space transform then the grip will not be positioned correctly).
	*/
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool GripObject(
			UObject * ObjectToGrip,
			const FTransform &WorldOffset,
			bool bWorldOffsetIsRelative = false,
			FName OptionalSnapToSocketName = NAME_None,
			FName OptionalBoneToGripName = NAME_None,
			EGripCollisionType GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics,
			EGripLateUpdateSettings GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping,
			EGripMovementReplicationSettings GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement,
			float GripStiffness = 1500.0f,
			float GripDamping = 200.0f, bool bIsSlotGrip = false);


	// Auto drop any uobject that is/root is a primitive component and has the VR Grip Interface	
	// If an object is passed in it will attempt to drop it, otherwise it will attempt to find and drop the given grip id
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool DropObject(
			UObject * ObjectToDrop = nullptr,
			uint8 GripIdToDrop = 0,
			bool bSimulate = false,
			FVector OptionalAngularVelocity = FVector::ZeroVector,
			FVector OptionalLinearVelocity = FVector::ZeroVector);

	// Auto grip any uobject that is/root is a primitive component
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool GripObjectByInterface(UObject * ObjectToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, FName OptionalBoneToGripName = NAME_None, FName OptionalSnapToSocketName = NAME_None, bool bIsSlotGrip = false);

	// Auto drop any uobject that is/root is a primitive component and has the VR Grip Interface
	// If an object is passed in it will attempt to drop it, otherwise it will attempt to find and drop the given grip id
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool DropObjectByInterface(UObject * ObjectToDrop = nullptr, uint8 GripIDToDrop = 0, FVector OptionalAngularVelocity = FVector::ZeroVector, FVector OptionalLinearVelocity = FVector::ZeroVector);

	/* Grip an actor, these are stored in a Tarray that will prevent destruction of the object, you MUST ungrip an actor if you want to kill it
	   The WorldOffset is the transform that it will remain away from the controller, if you use the world position of the actor then it will grab
	   at the point of intersection. 

	   If WorldOffsetIsRelative is true then it will not convert the transform from world space but will instead use that offset directly.
	   You could pass in a socket relative transform with this set for snapping or an empty transform to snap the object at its 0,0,0 point.

	   If you declare a valid OptionSnapToSocketName then it will instead snap the actor to the relative offset
	   location that the socket is to its parent actor.

		It will only do this if the WorldOffset value is left default, if it is not, then it will treat this as the name of the slot
		that you already have the transform for.

	   If you declare a valid OptionalBoneToGripName then it will grip that physics body with physics grips (It will expect a bone worldspace transform then, 
	   if you pass in the normal actor/root component world space transform then the grip will not be positioned correctly).
	*/
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool GripActor(
		AActor* ActorToGrip, 
		const FTransform &WorldOffset, 
		bool bWorldOffsetIsRelative = false, 
		FName OptionalSnapToSocketName = NAME_None,
		FName OptionalBoneToGripName = NAME_None,
		EGripCollisionType GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, 
		EGripLateUpdateSettings GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping, 
		EGripMovementReplicationSettings GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement,
		float GripStiffness = 1500.0f, 
		float GripDamping = 200.0f,
		bool bIsSlotGrip = false);

	// Drop a gripped actor
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool DropActor(
		AActor* ActorToDrop, 
		bool bSimulate, 
		FVector OptionalAngularVelocity = FVector::ZeroVector, 
		FVector OptionalLinearVelocity = FVector::ZeroVector
		);

	// Grip a component
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool GripComponent(
		UPrimitiveComponent* ComponentToGrip, 
		const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, 
		FName OptionalsnapToSocketName = NAME_None, 
		FName OptionalBoneToGripName = NAME_None,
		EGripCollisionType GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, 
		EGripLateUpdateSettings GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping,
		EGripMovementReplicationSettings GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement,
		float GripStiffness = 1500.0f, 
		float GripDamping = 200.0f,
		bool bIsSlotGrip = false);

	// Drop a gripped component
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool DropComponent(
		UPrimitiveComponent* ComponentToDrop,
		bool bSimulate, 
		FVector OptionalAngularVelocity = FVector::ZeroVector, 
		FVector OptionalLinearVelocity = FVector::ZeroVector
		);

	// Master function for dropping a grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool DropGrip(
		const FBPActorGripInformation &Grip, 
		bool bSimulate = false, 
		FVector OptionalAngularVelocity = FVector::ZeroVector, 
		FVector OptionalLinearVelocity = FVector::ZeroVector);

	bool DropGrip_Implementation(
		const FBPActorGripInformation& Grip,
		bool bSimulate = false,
		FVector OptionalAngularVelocity = FVector::ZeroVector,
		FVector OptionalLinearVelocity = FVector::ZeroVector,
		bool bSkipNotify = false);

	// No Longer replicated, called via on rep now instead.
	//UFUNCTION(Reliable, NetMulticast)
	bool NotifyGrip(FBPActorGripInformation &NewGrip, bool bIsReInit = false);

	UFUNCTION(Reliable, NetMulticast)
	void NotifyDrop(const FBPActorGripInformation &NewDrop, bool bSimulate);

	// Used so drop logic can be filtered
	void Drop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate);

	// Get a grip by actor
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void GetGripByActor(FBPActorGripInformation &Grip, AActor * ActorToLookForGrip, EBPVRResultSwitch &Result);

	// Get a grip by component
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void GetGripByComponent(FBPActorGripInformation &Grip, UPrimitiveComponent * ComponentToLookForGrip, EBPVRResultSwitch &Result);

	// Gets a grip by object, will auto use ByComponent or ByActor
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
	void GetGripByObject(FBPActorGripInformation &Grip, UObject * ObjectToLookForGrip, EBPVRResultSwitch &Result);

	// Gets a grip by its grip ID *NOTE*: Grip IDs are only unique to their controller, do NOT use them as cross controller identifiers
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
	void GetGripByID(FBPActorGripInformation &Grip, uint8 IDToLookForGrip, EBPVRResultSwitch &Result);

	// Gets a grip by its grip ID *NOTE*: Grip IDs are only unique to their controller, do NOT use them as cross controller identifiers
	FBPActorGripInformation * GetGripPtrByID(uint8 IDToLookForGrip);

	// Get the physics velocities of a grip
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		void GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity);

	// Get the physics constraint force of a simulating grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool GetPhysicsConstraintForce(const FBPActorGripInformation& Grip, FVector& AngularForce, FVector& LinearForce);

	// Get the root components mass of a grip
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		void GetGripMass(const FBPActorGripInformation& Grip, float& Mass);

	// Sets whether an active grip is paused or not (is not replicated by default as it is likely you will want to pass variables with this setting).
	// If you want it server authed you should RPC a bool down with any additional information (ie: attach location).
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripPaused(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			bool bIsPaused = false,
			bool bNoConstraintWhenPaused = false
		);

	// Sets whether an active hybrid grip is locked to its soft setting (is not replicated by default as it is likely you will want to pass variables with this setting).
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripHybridLock(
			const FBPActorGripInformation& Grip,
			EBPVRResultSwitch& Result,
			bool bIsLocked = false
		);

	// Sets the transform to stay at during pause
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		void SetPausedTransform(
			const FBPActorGripInformation &Grip,
			const FTransform & PausedTransform,
			bool bTeleport = false
		);

	// Set the Grip Collision Type of a grip, call server side if not a local grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripCollisionType(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			EGripCollisionType NewGripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics
			);


	// Set the late update setting of a grip, call server side if not a local grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripLateUpdateSetting(
			const FBPActorGripInformation &Grip, 
			EBPVRResultSwitch &Result,
			EGripLateUpdateSettings NewGripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping	
			);

	// Set the relative transform of a grip, call server side if not a local grip
	// Can check HasGripAuthority to decide if callable locally
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripRelativeTransform(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			const FTransform & NewRelativeTransform
			);

	// Set the addition transform of a grip, CALL LOCALLY, not server side, Addition transform is not replicated
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripAdditionTransform(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			const FTransform & NewAdditionTransform, bool bMakeGripRelative = false
			);

	// Set the constraint stiffness and dampening of a grip, call server side if not a local grip
	// Can check HasGripAuthority to decide if callable locally
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripStiffnessAndDamping(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			float NewStiffness, float NewDamping, bool bAlsoSetAngularValues = false, float OptionalAngularStiffness = 0.0f, float OptionalAngularDamping = 0.0f
		);

	// Used to convert an offset transform to grip relative, useful for storing an initial offset and then lerping back to 0 without re-calculating every tick
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "CreateGripRelativeAdditionTransform"))
		FTransform CreateGripRelativeAdditionTransform_BP(
			const FBPActorGripInformation &GripToSample,
			const FTransform & AdditionTransform,
			bool bGripRelative = false
			);

	inline FTransform CreateGripRelativeAdditionTransform(
		const FBPActorGripInformation &GripToSample,
		const FTransform & AdditionTransform,
		bool bGripRelative = false
		);


	// Checks if we have grip authority
	inline bool HasGripAuthority(const FBPActorGripInformation &Grip);

	// Checks if we have grip authority over a given object
	inline bool HasGripAuthority(const UObject * ObjToCheck);

	// Returns if we have grip authority (can call drop / grip on this grip)
	// Mostly for networked games as local grips are client authed and all others are server authed
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "HasGripAuthority"))
		bool BP_HasGripAuthority(const FBPActorGripInformation & Grip);

	// Returns if we have grip authority (can call drop / grip on this grip)
	// Mostly for networked games as local grips are client authed and all others are server authed
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "HasGripAuthorityForObject"))
		bool BP_HasGripAuthorityForObject(const UObject* ObjToCheck);

	// Checks if we should be handling the movement of a grip based on settings for it
	inline bool HasGripMovementAuthority(const FBPActorGripInformation & Grip);

	// Returns if we have grip movement authority (we handle movement of the grip)
	// Mostly for networked games where ClientSide will be true for all and ServerSide will be true for server only
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "HasGripMovementAuthority"))
		bool BP_HasGripMovementAuthority(const FBPActorGripInformation & Grip);

	// Running the gripping logic in its own function as the main tick was getting bloated
	void TickGrip(float DeltaTime);

	// Splitting logic into separate function
	void HandleGripArray(TArray<FBPActorGripInformation> &GrippedObjectsArray, const FTransform & ParentTransform, float DeltaTime, bool bReplicatedArray = false);

	// Gets the world transform of a grip, modified by secondary grips, returns if it has a valid transform, if not then this tick will be skipped for the object
	bool GetGripWorldTransform(TArray<UVRGripScriptBase*>& GripScripts, float DeltaTime,FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport, bool &bForceADrop);

	// Calculate component to world without the protected tag, doesn't set it, just returns it
	inline FTransform CalcControllerComponentToWorld(FRotator Orientation, FVector Position)
	{
		return this->CalcNewComponentToWorld(FTransform(Orientation, Position));
	}

	// Converts a worldspace transform into being relative to this motion controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		FTransform ConvertToControllerRelativeTransform(const FTransform & InTransform);

	// Creates a secondary grip relative transform
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		static FTransform ConvertToGripRelativeTransform(const FTransform& GrippedActorTransform, const FTransform & InTransform);

	// Gets if the given object is held by this controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		bool GetIsObjectHeld(const UObject * ObjectToCheck);

	// Gets if the given actor is held by this controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		bool GetIsHeld(const AActor * ActorToCheck);

	// Gets if the given component is held by this controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		bool GetIsComponentHeld(const UPrimitiveComponent * ComponentToCheck);

	// Gets if the given Component is a secondary attach point to a gripped actor
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool GetIsSecondaryAttachment(const USceneComponent * ComponentToCheck, FBPActorGripInformation & Grip);

	// Get if we have gripped objects, local or replicated
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		bool HasGrippedObjects();

	// Get the first active and valid grip (local and remote auth both, priority remote)
	// Returns nullptr if there is none
	UFUNCTION(BlueprintCallable, meta = (Keywords = "Grip", DisplayName = "GetFirstActiveGrip", ScriptName = "GetFirstActiveGrip"), Category = "GripMotionController")
		bool K2_GetFirstActiveGrip(FBPActorGripInformation& FirstActiveGrip);
		FBPActorGripInformation* GetFirstActiveGrip();

	// Get list of all gripped objects grip info structures (local and normal both)
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		void GetAllGrips(TArray<FBPActorGripInformation> &GripArray);

	// Get list of all gripped actors
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	void GetGrippedActors(TArray<AActor*> &GrippedActorArray);

	// Get list of all gripped objects
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	void GetGrippedObjects(TArray<UObject*> &GrippedObjectsArray);

	// Get list of all gripped components
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	void GetGrippedComponents(TArray<UPrimitiveComponent*> &GrippedComponentsArray);

	// After teleporting a pawn you NEED to call this, otherwise gripped objects will travel with a sweeped move and can get caught on geometry
	// The base Teleport() function automatically calls this already, but when you manually set location you should do it yourself.
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	void PostTeleportMoveGrippedObjects();

	bool bIsPostTeleport;

	// Move a single gripped item back into position ignoring collision in the way
	// bTeleportPhysicsGrips says whether we should teleport any physics grips as well
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool TeleportMoveGrippedActor(AActor * GrippedActorToMove, bool bTeleportPhysicsGrips = true);

	// Move a single gripped item back into position ignoring collision in the way
	// bTeleportPhysicsGrips says whether we should teleport any physics grips as well
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool TeleportMoveGrippedComponent(UPrimitiveComponent * ComponentToMove, bool bTeleportPhysicsGrips = true);

	// Move a single grip back into position ignoring collision in the way
	// bTeleportPhysicsGrips says whether we should teleport any physics grips as well
	// bIsForPostTeleport says whether we shuld allow the DropOnTeleport logic to apply or not
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool TeleportMoveGrip(UPARAM(ref)FBPActorGripInformation &Grip, bool bTeleportPhysicsGrips = true, bool bIsForPostTeleport = false);
	bool TeleportMoveGrip_Impl(FBPActorGripInformation &Grip, bool bTeleportPhysicsGrips, bool bIsForPostTeleport, FTransform & OptionalTransform);

	// Moves all grips back into position immediately
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		void TeleportMoveGrips(bool bTeleportPhysicsGrips = true, bool bIsForPostTeleport = false);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool AddSecondaryAttachmentPoint(UObject * GrippedObjectToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform &OriginalTransform, bool bTransformIsAlreadyRelative = false, float LerpToTime = 0.25f, bool bIsSlotGrip = false, FName SecondarySlotName = NAME_None);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool AddSecondaryAttachmentToGrip(const FBPActorGripInformation & GripToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform &OriginalTransform, bool bTransformIsAlreadyRelative = false, float LerpToTime = 0.25f, bool bIsSlotGrip = false, FName SecondarySlotName = NAME_None);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool AddSecondaryAttachmentToGripByID(const uint8 GripID, USceneComponent* SecondaryPointComponent, const FTransform& OriginalTransform, bool bTransformIsAlreadyRelative = false, float LerpToTime = 0.25f, bool bIsSlotGrip = false, FName SecondarySlotName = NAME_None);

	// Removes a secondary attachment point from a grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool RemoveSecondaryAttachmentPoint(UObject * GrippedObjectToRemoveAttachment, float LerpToTime = 0.25f);

	// Removes a secondary attachment point from a grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool RemoveSecondaryAttachmentFromGrip(const FBPActorGripInformation & GripToRemoveAttachment, float LerpToTime = 0.25f);

	// Removes a secondary attachment point from a grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool RemoveSecondaryAttachmentFromGripByID(const uint8 GripID = 0, float LerpToTime = 0.25f);

	// This is for testing, setting it to true allows you to test grip with a non VR enabled pawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
	bool bUseWithoutTracking;

	bool CheckComponentWithSweep(UPrimitiveComponent * ComponentToCheck, FVector Move, FRotator newOrientation, bool bSkipSimulatingComponents/*, bool & bHadBlockingHitOut*/);
	
	// For physics handle operations
	void OnGripMassUpdated(FBodyInstance* GripBodyInstance);
	bool SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip, TArray<UVRGripScriptBase*> * GripScripts = nullptr);
	bool DestroyPhysicsHandle(const FBPActorGripInformation &Grip, bool bSkipUnregistering = false);
	void UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform);
	bool SetGripConstraintStiffnessAndDamping(const FBPActorGripInformation *Grip, bool bUseHybridMultiplier = false);	
	bool GetPhysicsJointLength(const FBPActorGripInformation &GrippedActor, UPrimitiveComponent * rootComp, FVector & LocOut);

	TArray<FBPActorPhysicsHandleInformation> PhysicsGrips;
	FBPActorPhysicsHandleInformation * GetPhysicsGrip(const FBPActorGripInformation & GripInfo);
	FBPActorPhysicsHandleInformation * GetPhysicsGrip(const uint8 GripID);
	bool GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo, int & index);
	FBPActorPhysicsHandleInformation * CreatePhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool DestroyPhysicsHandle(FBPActorPhysicsHandleInformation * HandleInfo);
	bool PausePhysicsHandle(FBPActorPhysicsHandleInformation* HandleInfo);
	bool UnPausePhysicsHandle(FBPActorGripInformation& GripInfo, FBPActorPhysicsHandleInformation* HandleInfo);
	
	// Gets the advanced physics handle settings
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "GetPhysicsHandleSettings"))
		bool GetPhysicsHandleSettings(UPARAM(ref)const FBPActorGripInformation & Grip, FBPAdvancedPhysicsHandleSettings& PhysicsHandleSettingsOut);

	// Sets the advanced physics handle settings, also automatically updates it
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "SetPhysicsHandleSettings"))
		bool SetPhysicsHandleSettings(UPARAM(ref)const FBPActorGripInformation& Grip, UPARAM(ref) const FBPAdvancedPhysicsHandleSettings& PhysicsHandleSettingsIn);

	// Creates a physics handle for this grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "SetUpPhysicsHandle"))
		bool SetUpPhysicsHandle_BP(UPARAM(ref)const FBPActorGripInformation &Grip);

	// Destroys a physics handle for this grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "DestroyPhysicsHandle"))
		bool DestroyPhysicsHandle_BP(UPARAM(ref)const FBPActorGripInformation &Grip);

	// Re-creates a physics handle for this grip
	// If bFullyRecreate is true then it will set all of the handle properties, if not then it will only reset the physics actors and COM positions
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "UpdatePhysicsHandle"))
		bool UpdatePhysicsHandle_BP(UPARAM(ref)const FBPActorGripInformation& Grip, bool bFullyRecreate = true);

	// Update the location of the physics handle
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "UpdatePhysicsHandleTransform"))
		void UpdatePhysicsHandleTransform_BP(UPARAM(ref)const FBPActorGripInformation &GrippedActor, UPARAM(ref)const FTransform& NewTransform);

	// Get the grip distance of either the physics handle if there is one, or the difference from the hand to the root component if there isn't
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "GetGripDistance"))
		bool GetGripDistance_BP(UPARAM(ref)FBPActorGripInformation &Grip, FVector ExpectedLocation, float & CurrentDistance);

	/** If true, the Position and Orientation args will contain the most recent controller state */
	virtual bool GripPollControllerState(FVector& Position, FRotator& Orientation, float WorldToMetersScale);

	/** Whether or not this component had a valid tracked controller associated with it this frame*/
	bool bTracked;

	/** Whether or not this component had a valid tracked device this frame
	*
	* Use this instead of the normal IsTracked() for the motion controller which will not return the correct information.
	* This is messy but I have no access to the various private members of the motion controller.
	*/
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		bool GripControllerIsTracked() const;

	/** Returns the first valid device ID for this motion controller (across enabled XR systems)
	*
	* If bCheckOpenVROnly is true, then it only checks for OpenVR IDs (for use with my openVR nodes).
	*/
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void GetControllerDeviceID(FXRDeviceId & DeviceID, EBPVRResultSwitch &Result, bool bCheckOpenVROnly = false);

	/** Whether or not this component has authority within the frame*/
	bool bHasAuthority;

private:
	/** Whether or not this component is currently on the network server*/
	//bool bIsServer;

	/** View extension object that can persist on the render thread without the motion controller component */
	class FGripViewExtension : public FSceneViewExtensionBase
	{

		// #TODO: 4.18 - Uses an auto register base now, revise declaration and implementation
	public:
		FGripViewExtension(const FAutoRegister& AutoRegister, UGripMotionControllerComponent* InMotionControllerComponent);

		virtual ~FGripViewExtension() {}

		/** ISceneViewExtension interface */
		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
		virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
		virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {}
		virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;
		virtual void LateLatchingViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;

		virtual int32 GetPriority() const override { return -10; }

	private:
		friend class UGripMotionControllerComponent;

		/** Motion controller component associated with this view extension */
		UGripMotionControllerComponent* MotionControllerComponent;

		FExpandedLateUpdateManager LateUpdate;
	};
	TSharedPtr< FGripViewExtension, ESPMode::ThreadSafe > GripViewExtension;

};

FTransform inline UGripMotionControllerComponent::CreateGripRelativeAdditionTransform(
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

bool inline UGripMotionControllerComponent::HasGripAuthority(const FBPActorGripInformation &Grip)
{
	if (((Grip.GripMovementReplicationSetting != EGripMovementReplicationSettings::ClientSide_Authoritive &&
		Grip.GripMovementReplicationSetting != EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) && IsServer()) ||
		((Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive ||
			Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) && bHasAuthority))
	{
		return true;
	}

	return false;
}

bool inline UGripMotionControllerComponent::HasGripAuthority(const UObject * ObjToCheck)
{
	if (!ObjToCheck)
		return false;

	// If it isn't interfaced and we are the server, then allow gripping it
	if (!ObjToCheck->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()) && IsServer())
		return true;

	// I know that it is bad practice to const_cast here, but I want the object to be passed in const
	EGripMovementReplicationSettings MovementRepType = IVRGripInterface::Execute_GripMovementReplicationType(const_cast<UObject*>(ObjToCheck));

	if (((MovementRepType != EGripMovementReplicationSettings::ClientSide_Authoritive &&
		MovementRepType != EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) && IsServer()) ||
		((MovementRepType == EGripMovementReplicationSettings::ClientSide_Authoritive ||
			MovementRepType == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep) && bHasAuthority))
	{
		return true;
	}

	return false;
}

bool inline UGripMotionControllerComponent::HasGripMovementAuthority(const FBPActorGripInformation &Grip)
{
	if (IsServer())
	{
		return true;
	}
	else
	{
		if (Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ForceClientSideMovement ||
			Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive ||
			Grip.GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep)
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
