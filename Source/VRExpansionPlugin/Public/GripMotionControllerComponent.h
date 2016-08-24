// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "IMotionController.h"
#include "SceneViewExtension.h"

#include "VRBPDatatypes.h"

#include "GripMotionControllerComponent.generated.h"

/**
*
*/

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = MotionController)
class VREXPANSIONPLUGIN_API UGripMotionControllerComponent : public UPrimitiveComponent
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

	UPROPERTY(BlueprintReadOnly, Replicated, Category = "VRGrip", ReplicatedUsing = OnRep_GrippedActors)
	TArray<FBPActorGripInformation> GrippedActors;

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
		}

		// Need to think about how best to handle the simulating flag here, don't handle for now
		// Check for removed gripped actors
		// This might actually be better left as an RPC multicast
	}

	UPROPERTY(BlueprintReadWrite, Category = "VRGrip")
	TArray<UPrimitiveComponent *> AdditionalLateUpdateComponents;

	//  Movement Replication
	// Actor needs to be replicated for this to work

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReplicatedControllerTransform, Category = "VRGrip")
	FBPVRComponentPosRep ReplicatedControllerTransform;

	UFUNCTION()
	virtual void OnRep_ReplicatedControllerTransform()
	{
		SetRelativeLocationAndRotation(ReplicatedControllerTransform.Position, ReplicatedControllerTransform.GetRotation()/*ReplicatedControllerTransform.Orientation*/);
	}

	// Rate to update the position to the server, 100htz is default (same as replication rate, should also hit every tick).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "VRGrip", meta = (ClampMin = "0", UIMin = "0"))
	float ControllerNetUpdateRate;

	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case.
	float ControllerNetUpdateCount;

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
	void Server_SendControllerTransform(FBPVRComponentPosRep NewTransform);

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	bool IsLocallyControlled() const
	{
		// I like epics new authority check more than mine
		const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
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
	bool GripActor(AActor* ActorToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, FName OptionalSnapToSocketName = NAME_None, TEnumAsByte<EGripCollisionType> GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, float GripStiffness = 1500.0f, float GripDamping = 200.0f, bool bTurnOffLateUpdateWhenColliding = true);

	// Drop a gripped actor
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool DropActor(AActor* ActorToDrop, bool bSimulate, FVector OptionalAngularVelocity = FVector::ZeroVector, FVector OptionalLinearVelocity = FVector::ZeroVector);

	// Grip a component
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool GripComponent(UPrimitiveComponent* ComponentToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, FName OptionalSnapToSocketName = NAME_None, TEnumAsByte<EGripCollisionType> GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, float GripStiffness = 1500.0f, float GripDamping = 200.0f, bool bTurnOffLateUpdateWhenColliding = true);

	// Drop a gripped component
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool DropComponent(UPrimitiveComponent* ComponentToDrop, bool bSimulate, FVector OptionalAngularVelocity = FVector::ZeroVector, FVector OptionalLinearVelocity = FVector::ZeroVector);

	// Master function for dropping a grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool DropGrip(const FBPActorGripInformation &Grip, bool bSimulate, FVector OptionalAngularVelocity = FVector::ZeroVector, FVector OptionalLinearVelocity = FVector::ZeroVector);

	// No Longer replicated, called via on rep now instead.
	//UFUNCTION(Reliable, NetMulticast)
	void NotifyGrip(const FBPActorGripInformation &NewGrip);

	UFUNCTION(Reliable, NetMulticast)
	void NotifyDrop(const FBPActorGripInformation &NewDrop, bool bSimulate);

	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity);

	// Running the gripping logic in its own function as the main tick was getting bloated
	void TickGrip();

	// Converts a worldspace transform into being relative to this motion controller
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	FTransform ConvertToControllerRelativeTransform(const FTransform & InTransform)
	{
		return InTransform.GetRelativeTransform(this->GetComponentTransform());
	}

	// Get list of all gripped actors 
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	void GetGrippedActors(TArray<AActor*> &GrippedActorsArray);

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
	bool TeleportMoveGrip(const FBPActorGripInformation &Grip);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool AddSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment, USceneComponent * SecondaryPointComponent);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool RemoveSecondaryAttachmentPoint(AActor * GrippedActorToRemoveAttachment);

	// This is for testing, setting it to true allows you to test grip with a non VR enabled pawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGrip")
	bool bUseWithoutTracking;

	//FVector OriginalPosition;
	//FRotator OriginalOrientation;

	bool CheckComponentWithSweep(UPrimitiveComponent * ComponentToCheck, FVector Move, FRotator newOrientation, bool bSkipSimulatingComponents/*, bool & bHadBlockingHitOut*/);
	
	// For physics handle operations
	bool SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip);
	bool DestroyPhysicsHandle(const FBPActorGripInformation &Grip);
	void UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform);

	TArray<FBPActorPhysicsHandleInformation> PhysicsGrips;
	FBPActorPhysicsHandleInformation * GetPhysicsGrip(const FBPActorGripInformation & GripInfo);
	int GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo);
	FBPActorPhysicsHandleInformation * CreatePhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool DestroyPhysicsHandle(int32 SceneIndex, physx::PxD6Joint** HandleData, physx::PxRigidDynamic** KinActorData);

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
		/** Primitives that need late update before rendering */
		TArray<LateUpdatePrimitiveInfo> LateUpdatePrimitives;
	};
	TSharedPtr< FViewExtension, ESPMode::ThreadSafe > ViewExtension;

};