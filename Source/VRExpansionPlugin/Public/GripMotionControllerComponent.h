// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "IMotionController.h"
#include "SceneViewExtension.h"
#include "Runtime/Launch/Resources/Version.h"

#include "VRBPDatatypes.h"

#include "GripMotionControllerComponent.generated.h"

/**
*
*/

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = MotionController)
class UGripMotionControllerComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()
	~UGripMotionControllerComponent();

	/** Which player index this motion controller should automatically follow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
		int32 PlayerIndex;

	/** Which hand this component should automatically follow */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
		TEnumAsByte<EControllerHand> Hand;

	/** If false, render transforms within the motion controller hierarchy will be updated a second time immediately before rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
		uint32 bDisableLowLatencyUpdate:1;

	/** The tracking status for the device (e.g. full tracking, inertial tracking only, no tracking) */
	UPROPERTY(BlueprintReadOnly, Category = "MotionController")
		TEnumAsByte<ETrackingStatus> CurrentTrackingStatus;

	/** Whether or not this component had a valid tracked device this frame */
	UFUNCTION(BlueprintPure, Category = "MotionController")
	bool IsTracked() const
	{
		return bTracked;
	}

	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void OnUnregister() override;

public:

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "VRGrip")
	TArray<FBPActorGripInformation> GrippedActors;

	//  Movement Replication
	// Actor needs to be replicated for this to work

	// Whether to ever replicate position
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "VRGrip")
	bool bReplicateControllerTransform;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReplicatedControllerTransform, Category = "VRGrip")
	FBPVRComponentPosRep ReplicatedControllerTransform;

	UFUNCTION()
	virtual void OnRep_ReplicatedControllerTransform()
	{
		SetRelativeLocationAndRotation(ReplicatedControllerTransform.Position, ReplicatedControllerTransform.Orientation);
	}

	// Rate to update the position to the server, 100htz is default (same as replication rate, should also hit every tick).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "VRGrip")
	float ControllerNetUpdateRate;

	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case.
	float ControllerNetUpdateCount;

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
	void Server_SendControllerTransform(FBPVRComponentPosRep NewTransform);

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	bool IsLocallyControlled() const
	{
		// Epic used a check for a player controller to control has authority, however the controllers are always attached to a pawn
		// So this check would have always failed to work in the first place.....
		
		APawn* Owner = Cast<APawn>(GetOwner());

		if (!Owner)
		{
			//const APlayerController* Actor = Cast<APlayerController>(GetOwner());
			//if (!Actor)
				return false;

			//return Actor->IsLocalPlayerController();
		}
	
		return Owner->IsLocallyControlled();
	}


	bool IsServer() const
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

	/* Grip an actor, these are stored in a Tarray that will prevent destruction of the object, you MUST ungrip an actor if you want to kill it
	   The WorldOffset is the transform that it will remain away from the controller, if you use the world position of the actor then it will grab
	   at the point of intersection. 

	   If WorldOffsetIsRelative is true then it will not convert the transform from world space but will instead use that offset directly.
	   You could pass in a socket relative transform with this set for snapping or an empty transform to snap the object at its 0,0,0 point.
	   
	   If you declare a valid OptionSnapToSocketName then it will instead snap the actor to the relative offset
	   location that the socket is to its parent actor.
	*/
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool GripActor(AActor* ActorToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, FName OptionalSnapToSocketName = NAME_None, TEnumAsByte<EGripCollisionType> GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, /* bool bSweepCollision = true, bool bInteractiveCollision = true,*/ bool bAllowSetMobility = true);

	// Drop a gripped actor
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool DropActor(AActor* ActorToDrop, bool bSimulate);

	UFUNCTION(Reliable, NetMulticast)
	void NotifyGrip(const FBPActorGripInformation &NewGrip);

	UFUNCTION(Reliable, NetMulticast)
	void NotifyDrop(const FBPActorGripInformation &NewDrop, bool bSimulate);

	// After teleporting a pawn you NEED to call this, otherwise gripped objects will travel with a sweeped move and can get caught on geometry
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	void PostTeleportMoveGrippedActors();

	// Move a single gripped item back into position ignoring collision in the way
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool TeleportMoveGrippedActor(AActor * GrippedActorToMove);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool AddSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment, USceneComponent * SecondaryPointComponent);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool RemoveSecondaryAttachmentPoint(AActor * GrippedActorToRemoveAttachment);

	// This is for testing, setting it to true allows you to test grip with a non VR enabled pawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGrip")
	bool bUseWithoutTracking;

	FVector OriginalPosition;
	FRotator OriginalOrientation;

	bool CheckActorWithSweep(AActor * ActorToCheck, FVector Move, FRotator newOrientation);
	
	// For physics handle operations
	bool SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip);
	bool DestroyPhysicsHandle(const FBPActorGripInformation &Grip);
	void UpdatePhysicsHandleTransform(const FTransform& NewTransform);

	UPROPERTY(EditAnywhere, Category = "VRGrip")
		float Damping;
	UPROPERTY(EditAnywhere, Category = "VRGrip")
		float Stiffness;
protected: 

		/** Physics scene index of the body we are grabbing. */
		int32 SceneIndex;
		/** Pointer to PhysX joint used by the handle*/
		physx::PxD6Joint* HandleData;
		/** Pointer to kinematic actor jointed to grabbed object */
		physx::PxRigidDynamic* KinActorData;

private:
	/** Whether or not this component had a valid tracked controller associated with it this frame*/
	bool bTracked;

	/** Whether or not this component has authority within the frame*/
	bool bHasAuthority;

	/** Whether or not this component is currently on the network server*/
	bool bIsServer;

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

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 11
#else
		virtual int32 GetPriority() const override { return -10; }
#endif

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
		void GatherLateGripUpdatePrimitives(USceneComponent* Component, TArray<LateUpdatePrimitiveInfo>& Primitives, UPrimitiveComponent * root, FBPActorGripInformation * actor);
		/** Primitives that need late update before rendering */
		TArray<LateUpdatePrimitiveInfo> LateUpdatePrimitives;
	};
	TSharedPtr< FViewExtension, ESPMode::ThreadSafe > ViewExtension;

};