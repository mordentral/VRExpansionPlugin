// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "IMotionController.h"
#include "SceneViewExtension.h"
#include "VRBPDatatypes.h"
#include "VRGripInterface.h"

#include "GripMotionControllerComponent.generated.h"

/**
*
*/

DECLARE_LOG_CATEGORY_EXTERN(LogVRMotionController, Log, All);
//For UE4 Profiler ~ Stat Group
DECLARE_STATS_GROUP(TEXT("TICKGrip"), STATGROUP_TickGrip, STATCAT_Advanced);



UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = MotionController)
class VREXPANSIONPLUGIN_API UGripMotionControllerComponent : public UPrimitiveComponent
{

public:
	bool bOffsetByHMD;
	FVector LastLocationForLateUpdate;
private:

	GENERATED_UCLASS_BODY()
	~UGripMotionControllerComponent();

	/** Which player index this motion controller should automatically follow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
		int32 PlayerIndex;

	/** Which hand this component should automatically follow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
		EControllerHand Hand;

	/** If false, render transforms within the motion controller hierarchy will be updated a second time immediately before rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
		uint32 bDisableLowLatencyUpdate:1;

	/** The tracking status for the device (e.g. full tracking, inertial tracking only, no tracking) */
	UPROPERTY(BlueprintReadOnly, Category = "MotionController")
		ETrackingStatus CurrentTrackingStatus;

	/** Whether or not this component had a valid tracked device this frame */
	UFUNCTION(BlueprintPure, Category = "MotionController")
	bool IsTracked() const
	{
		return bTracked;
	}

	// Used to set the difference since last tick for TickGrip()
	FVector LastControllerLocation; 

	// Custom version of the component sweep function to remove that aggravating warning epic is throwing about skeletal mesh components.
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void OnUnregister() override;

public:

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "VRGrip", ReplicatedUsing = OnRep_GrippedActors)
	TArray<FBPActorGripInformation> GrippedActors;

	UPROPERTY(BlueprintReadOnly, Category = "VRGrip")
	TArray<FBPActorGripInformation> LocallyGrippedActors;

	// Enable this to send the TickGrip event every tick even for non custom grip types - has a slight performance hit
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGrip")
	bool bAlwaysSendTickGrip;

	// Recreates a grip in situations where a relevant variable may have been changed
	FORCEINLINE void ReCreateGrip(FBPActorGripInformation & GripInfo)
	{
		int HandleIndex = 0;

		if (GetPhysicsGripIndex(GripInfo, HandleIndex))
		{
			DestroyPhysicsHandle(PhysicsGrips[HandleIndex].SceneIndex, &PhysicsGrips[HandleIndex].HandleData, &PhysicsGrips[HandleIndex].KinActorData);
			PhysicsGrips.RemoveAt(HandleIndex);
		}

		// Grip Type or replication was changed
		NotifyGrip(GripInfo);
	}

	UFUNCTION()
	virtual void OnRep_GrippedActors(TArray<FBPActorGripInformation> OriginalArrayState)
	{
		// Check for new gripped actors
		for (int i = 0; i < GrippedActors.Num(); i++)
		{
			int FoundIndex = 0;
			if (!OriginalArrayState.Find(GrippedActors[i], FoundIndex))
			{
				// Is a new grip entry
				NotifyGrip(GrippedActors[i]);
			}
			else // Check to see if the important bits got changed (instant drop / pickup can cause this)
			{
				if (OriginalArrayState[FoundIndex].GripCollisionType != GrippedActors[i].GripCollisionType ||
					OriginalArrayState[FoundIndex].GripMovementReplicationSetting != GrippedActors[i].GripMovementReplicationSetting)
				{
					ReCreateGrip(GrippedActors[i]);
				}
			}
		}

		// Need to think about how best to handle the simulating flag here, don't handle for now
		// Check for removed gripped actors
		// This might actually be better left as an RPC multicast
	}

	UPROPERTY(BlueprintReadWrite, Category = "VRGrip")
	TArray<UPrimitiveComponent *> AdditionalLateUpdateComponents;

	//  Movement Replication
	// Actor needs to be replicated for this to work

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReplicatedControllerTransform, Category = "GripMotionController|Networking")
	FBPVRComponentPosRep ReplicatedControllerTransform;

	FVector LastUpdatesRelativePosition;
	FRotator LastUpdatesRelativeRotation;
	float LerpTimeFromLastUpdate;
	bool bLerpingPosition;

	UFUNCTION()
	virtual void OnRep_ReplicatedControllerTransform()
	{
		ReplicatedControllerTransform.Unpack();

		if (bSmoothReplicatedMotion)
		{
			bLerpingPosition = true;
			ControllerNetUpdateCount = 0.0f;
			LastUpdatesRelativePosition = this->RelativeLocation;
			LastUpdatesRelativeRotation =  this->RelativeRotation;
		}
		else
			SetRelativeLocationAndRotation(ReplicatedControllerTransform.UnpackedLocation, ReplicatedControllerTransform.UnpackedRotation);
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

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	FORCEINLINE bool IsLocallyControlled() const
	{
		// I like epics new authority check more than mine
		const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
	}


	FORCEINLINE bool IsServer() const
	{
		if (GEngine != nullptr && GWorld != nullptr)
		{
			switch (GEngine->GetNetMode(GWorld))
			{
			case NM_Client: 
			{return false;} break;
			case NM_DedicatedServer:
			case NM_ListenServer:
			default: 
			{return true; } break;
			}
		}

		return false;
	}

	// Auto grip any uobject that is/root is a primitive component and has the VR Grip Interface	
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
		bool GripObject(
			UObject * ObjectToGrip, 
			const FTransform &WorldOffset,
			bool bWorldOffsetIsRelative = false,
			FName OptionalSnapToSocketName = NAME_None,
			EGripCollisionType GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics,
			EGripLateUpdateSettings GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping,
			EGripMovementReplicationSettings GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement,
			float GripStiffness = 1500.0f,
			float GripDamping = 200.0f);

	// Auto drop any uobject that is/root is a primitive component and has the VR Grip Interface	
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
		bool DropObject(
			UObject * ObjectToDrop, 
			bool bSimulate,
			FVector OptionalAngularVelocity = FVector::ZeroVector,
			FVector OptionalLinearVelocity = FVector::ZeroVector);

	// Auto grip any uobject that is/root is a primitive component
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
		bool GripObjectByInterface(UObject * ObjectToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, bool bIsSlotGrip = false);

	// Auto drop any uobject that is/root is a primitive component and has the VR Grip Interface	
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
		bool DropObjectByInterface(UObject * ObjectToDrop, FVector OptionalAngularVelocity = FVector::ZeroVector, FVector OptionalLinearVelocity = FVector::ZeroVector);

	/* Grip an actor, these are stored in a Tarray that will prevent destruction of the object, you MUST ungrip an actor if you want to kill it
	   The WorldOffset is the transform that it will remain away from the controller, if you use the world position of the actor then it will grab
	   at the point of intersection. 

	   If WorldOffsetIsRelative is true then it will not convert the transform from world space but will instead use that offset directly.
	   You could pass in a socket relative transform with this set for snapping or an empty transform to snap the object at its 0,0,0 point.
	   
	   If you declare a valid OptionSnapToSocketName then it will instead snap the actor to the relative offset
	   location that the socket is to its parent actor.
	*/
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool GripActor(
		AActor* ActorToGrip, 
		const FTransform &WorldOffset, 
		bool bWorldOffsetIsRelative = false, 
		FName OptionalSnapToSocketName = NAME_None, 
		EGripCollisionType GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, 
		EGripLateUpdateSettings GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping, 
		EGripMovementReplicationSettings GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement,
		float GripStiffness = 1500.0f, 
		float GripDamping = 200.0f
		);

	// Drop a gripped actor
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool DropActor(
		AActor* ActorToDrop, 
		bool bSimulate, 
		FVector OptionalAngularVelocity = FVector::ZeroVector, 
		FVector OptionalLinearVelocity = FVector::ZeroVector
		);

	// Grip a component
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool GripComponent(
		UPrimitiveComponent* ComponentToGrip, 
		const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, 
		FName OptionalSnapToSocketName = NAME_None, 
		EGripCollisionType GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, 
		EGripLateUpdateSettings GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping,
		EGripMovementReplicationSettings GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement,
		float GripStiffness = 1500.0f, 
		float GripDamping = 200.0f
		);

	// Drop a gripped component
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool DropComponent(
		UPrimitiveComponent* ComponentToDrop, 
		bool bSimulate, 
		FVector OptionalAngularVelocity = FVector::ZeroVector, 
		FVector OptionalLinearVelocity = FVector::ZeroVector
		);

	// Master function for dropping a grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool DropGrip(
		const FBPActorGripInformation &Grip, 
		bool bSimulate, 
		FVector OptionalAngularVelocity = FVector::ZeroVector, 
		FVector OptionalLinearVelocity = FVector::ZeroVector);

	// No Longer replicated, called via on rep now instead.
	//UFUNCTION(Reliable, NetMulticast)
	void NotifyGrip(const FBPActorGripInformation &NewGrip);

	UFUNCTION(Reliable, NetMulticast)
	void NotifyDrop(const FBPActorGripInformation &NewDrop, bool bSimulate);

	// Get a grip by actor
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void GetGripByActor(FBPActorGripInformation &Grip, AActor * ActorToLookForGrip, EBPVRResultSwitch &Result);

	// Get a grip by component
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void GetGripByComponent(FBPActorGripInformation &Grip, UPrimitiveComponent * ComponentToLookForGrip, EBPVRResultSwitch &Result);

	// Get the physics velocities of a grip
	UFUNCTION(BlueprintPure, Category = "VRGrip")
		void GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity);

	// Set the Grip Collision Type of a grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripCollisionType(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			EGripCollisionType NewGripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics
			);


	// Set the late update setting of a grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripLateUpdateSetting(
			const FBPActorGripInformation &Grip, 
			EBPVRResultSwitch &Result,
			EGripLateUpdateSettings NewGripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping	
			);

	// Set the relative transform of a grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripRelativeTransform(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			const FTransform & NewRelativeTransform
			);

	// Set the addition transform of a grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripAdditionTransform(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			const FTransform & NewAdditionTransform, bool bMakeGripRelative = false
			);

	// Used to convert an offset transform to grip relative, useful for storing an initial offset and then lerping back to 0 without re-calculating every tick
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (DisplayName = "CreateGripRelativeAdditionTransform"))
		FTransform CreateGripRelativeAdditionTransform_BP(
			const FBPActorGripInformation &GripToSample,
			const FTransform & AdditionTransform,
			bool bGripRelative = false
			);

	FORCEINLINE FTransform CreateGripRelativeAdditionTransform(
		const FBPActorGripInformation &GripToSample,
		const FTransform & AdditionTransform,
		bool bGripRelative = false
		);


	// Checks if we should be handling the movement of a grip based on settings for it
	FORCEINLINE bool HasGripMovementAuthority(const FBPActorGripInformation &Grip);

	// Running the gripping logic in its own function as the main tick was getting bloated
	FORCEINLINE void TickGrip(float DeltaTime);

	// Splitting logic into seperate function
	void HandleGripArray(TArray<FBPActorGripInformation> &GrippedObjects, const FTransform & ParentTransform, const FVector &MotionControllerLocDelta, float DeltaTime, bool bReplicatedArray = false);

	// Gets the world transform of a grip, modified by secondary grips and interaction settings
	FORCEINLINE void GetGripWorldTransform(float DeltaTime,FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface);

	// Handle modifying the transform per the grip interaction settings, returns final world transform
	FORCEINLINE FTransform HandleInteractionSettings(float DeltaTime, const FTransform & ParentTransform, UPrimitiveComponent * root, FBPInteractionSettings InteractionSettings, FBPActorGripInformation & GripInfo);

	// Converts a worldspace transform into being relative to this motion controller, optionally can check interface settings for a given object as well to modify the given transform
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	FTransform ConvertToControllerRelativeTransform(const FTransform & InTransform, UObject * OptionalObjectToCheck = NULL)
	{
		if (OptionalObjectToCheck)
		{
			if (OptionalObjectToCheck->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
			{
				if (IVRGripInterface::Execute_IsInteractible(OptionalObjectToCheck) && IVRGripInterface::Execute_GetInteractionSettings(OptionalObjectToCheck).bIgnoreHandRotation)
				{
					// Remove the rotation portion of the transform, this interaction doesn't use it
					FTransform ModifiedTransform = this->GetComponentTransform();
					ModifiedTransform.SetRotation(FQuat::Identity);

					return InTransform.GetRelativeTransform(ModifiedTransform);
				}
			}
		}

		return InTransform.GetRelativeTransform(this->GetComponentTransform());
	}

	// Creates a secondary grip relative transform
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	static FTransform ConvertToGripRelativeTransform(const FTransform& GrippedActorTransform, const FTransform & InTransform)
	{
		return InTransform.GetRelativeTransform(GrippedActorTransform);
	}

	// Gets if the given Component is a secondary attach point to a gripped actor
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetIsObjectHeld(const UObject * ObjectToCheck)
	{
		if (!ObjectToCheck)
			return false;

		for (int i = 0; i < GrippedActors.Num(); ++i)
		{
			if (GrippedActors[i] == ObjectToCheck)
			{
				return true;
			}
		}

		for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
		{
			if (LocallyGrippedActors[i] == ObjectToCheck)
			{
				return true;
			}
		}

		return false;
	}

	// Gets if the given Component is a secondary attach point to a gripped actor
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetIsHeld(const AActor * ActorToCheck)
	{
		if (!ActorToCheck)
			return false;

		for (int i = 0; i < GrippedActors.Num(); ++i)
		{
			if (GrippedActors[i] == ActorToCheck)
			{
				return true;
			}
		}

		for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
		{
			if (LocallyGrippedActors[i] == ActorToCheck)
			{
				return true;
			}
		}

		return false;
	}

	// Gets if the given Component is a secondary attach point to a gripped actor
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetIsComponentHeld(const UPrimitiveComponent * ComponentToCheck)
	{
		if (!ComponentToCheck)
			return false;

		for (int i = 0; i < GrippedActors.Num(); ++i)
		{
			if (GrippedActors[i] == ComponentToCheck)
			{
				return true;
			}
		}

		for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
		{
			if (LocallyGrippedActors[i] == ComponentToCheck)
			{
				return true;
			}
		}

		return false;
	}

	// Gets if the given Component is a secondary attach point to a gripped actor
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetIsSecondaryAttachment(const USceneComponent * ComponentToCheck, FBPActorGripInformation & Grip)
	{
		if (!ComponentToCheck)
			return false;

		for (int i = 0; i < GrippedActors.Num(); ++i)
		{
			if(GrippedActors[i].bHasSecondaryAttachment && GrippedActors[i].SecondaryAttachment == ComponentToCheck)
			{
				Grip = GrippedActors[i];
				return true;
			}
		}

		for (int i = 0; i < LocallyGrippedActors.Num(); ++i)
		{
			if (LocallyGrippedActors[i].bHasSecondaryAttachment && LocallyGrippedActors[i].SecondaryAttachment == ComponentToCheck)
			{
				Grip = LocallyGrippedActors[i];
				return true;
			}
		}

		return false;
	}

	// Get list of all gripped actors 
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	void GetGrippedActors(TArray<AActor*> &GrippedActorsArray);

	// Get list of all gripped components
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	void GetGrippedComponents(TArray<UPrimitiveComponent*> &GrippedComponentsArray);

	// After teleporting a pawn you NEED to call this, otherwise gripped objects will travel with a sweeped move and can get caught on geometry
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	void PostTeleportMoveGrippedActors();

	// Move a single gripped item back into position ignoring collision in the way
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool TeleportMoveGrippedActor(AActor * GrippedActorToMove);

	// Move a single gripped item back into position ignoring collision in the way
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool TeleportMoveGrippedComponent(UPrimitiveComponent * ComponentToMove);

	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool TeleportMoveGrip(const FBPActorGripInformation &Grip, bool bIsPostTeleport = false);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool AddSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform &OriginalTransform, bool bTransformIsAlreadyRelative = false, float LerpToTime = 0.25f, float SecondarySmoothingScaler = 1.0f);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool RemoveSecondaryAttachmentPoint(AActor * GrippedActorToRemoveAttachment, float LerpToTime = 0.25f);

	// This is for testing, setting it to true allows you to test grip with a non VR enabled pawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGrip")
	bool bUseWithoutTracking;

	bool CheckComponentWithSweep(UPrimitiveComponent * ComponentToCheck, FVector Move, FRotator newOrientation, bool bSkipSimulatingComponents/*, bool & bHadBlockingHitOut*/);
	
	// For physics handle operations
	bool SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip);
	bool DestroyPhysicsHandle(const FBPActorGripInformation &Grip);
	void UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform);
	bool GetPhysicsJointLength(const FBPActorGripInformation &GrippedActor, UPrimitiveComponent * rootComp, FVector & LocOut);

	TArray<FBPActorPhysicsHandleInformation> PhysicsGrips;
	FBPActorPhysicsHandleInformation * GetPhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo, int & index);
	FBPActorPhysicsHandleInformation * CreatePhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool DestroyPhysicsHandle(int32 SceneIndex, physx::PxD6Joint** HandleData, physx::PxRigidDynamic** KinActorData);

private:
	/** Whether or not this component had a valid tracked controller associated with it this frame*/
	bool bTracked;

	/** Whether or not this component has authority within the frame*/
	bool bHasAuthority;

	/** Whether or not this component is currently on the network server*/
	//bool bIsServer;

	/** If true, the Position and Orientation args will contain the most recent controller state */
	bool PollControllerState(FVector& Position, FRotator& Orientation);

	/** View extension object that can persist on the render thread without the motion controller component */
	class FViewExtension : public ISceneViewExtension, public TSharedFromThis<FViewExtension, ESPMode::ThreadSafe>
	{
	public:
		FViewExtension(UGripMotionControllerComponent* InMotionControllerComponent) { MotionControllerComponent = InMotionControllerComponent; }
		virtual ~FViewExtension() {}

		/** ISceneViewExtension interface */
		virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
		virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
		virtual void PreRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView) override {}
		virtual void PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;

		virtual int32 GetPriority() const override { return -10; }

	private:
		friend class UGripMotionControllerComponent;

		/** Motion controller component associated with this view extension */
		UGripMotionControllerComponent* MotionControllerComponent;

		/*
		*	Late update primitive info for accessing valid scene proxy info. From the time the info is gathered
		*  to the time it is later accessed the render proxy can be deleted. To ensure we only access a proxy that is
		*  still valid we cache the primitive's scene info AND a pointer to it's own cached index. If the primitive
		*  is deleted or removed from the scene then attempting to access it via it's index will result in a different
		*  scene info than the cached scene info.
		*/
		struct LateUpdatePrimitiveInfo
		{
			const int32*			IndexAddress;
			FPrimitiveSceneInfo*	SceneInfo;
		};


		/** Walks the component hierarchy gathering scene proxies */
		void GatherLateUpdatePrimitives(USceneComponent* Component, TArray<LateUpdatePrimitiveInfo>& Primitives);
		FORCEINLINE void ProcessGripArrayLateUpdatePrimitives(TArray<FBPActorGripInformation> & GripArray);

		/** Primitives that need late update before rendering */
		TArray<LateUpdatePrimitiveInfo> LateUpdatePrimitives;
	};
	TSharedPtr< FViewExtension, ESPMode::ThreadSafe > ViewExtension;

};