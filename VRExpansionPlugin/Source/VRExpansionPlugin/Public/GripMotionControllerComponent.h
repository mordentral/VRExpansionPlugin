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
#include "XRMotionControllerBase.h" // for GetHandEnumForSourceName()
#include "GripMotionControllerComponent.generated.h"

class AVRBaseCharacter;

/**
*
*/

DECLARE_LOG_CATEGORY_EXTERN(LogVRMotionController, Log, All);
//For UE4 Profiler ~ Stat Group
DECLARE_STATS_GROUP(TEXT("TICKGrip"), STATGROUP_TickGrip, STATCAT_Advanced);

/**
* Utility class for applying an offset to a hierarchy of components in the renderer thread.
*/
class VREXPANSIONPLUGIN_API FExpandedLateUpdateManager
{
public:
	FExpandedLateUpdateManager();

	virtual ~FExpandedLateUpdateManager() {}
	/** Setup state for applying the render thread late update */
	void Setup(const FTransform& ParentToWorld, UGripMotionControllerComponent* Component);

	/** Apply the late update delta to the cached components */
	void Apply_RenderThread(FSceneInterface* Scene, const FTransform& OldRelativeTransform, const FTransform& NewRelativeTransform);

	/** Increments the double buffered read index, etc. - in prep for the next render frame (read: MUST be called for each frame Setup() was called on). */
	void PostRender_RenderThread();

public:

	/*
	*  Late update primitive info for accessing valid scene proxy info. From the time the info is gathered
	*  to the time it is later accessed the render proxy can be deleted. To ensure we only access a proxy that is
	*  still valid we cache the primitive's scene info AND a pointer to its own cached index. If the primitive
	*  is deleted or removed from the scene then attempting to access it via its index will result in a different
	*  scene info than the cached scene info.
	*/
	struct LateUpdatePrimitiveInfo
	{
		const int32*			IndexAddress;
		FPrimitiveSceneInfo*	SceneInfo;
	};

	/** A utility method that calls CacheSceneInfo on ParentComponent and all of its descendants */
	void GatherLateUpdatePrimitives(USceneComponent* ParentComponent);
	void ProcessGripArrayLateUpdatePrimitives(UGripMotionControllerComponent* MotionController, TArray<FBPActorGripInformation> & GripArray);

	/** Generates a LateUpdatePrimitiveInfo for the given component if it has a SceneProxy and appends it to the current LateUpdatePrimitives array */
	void CacheSceneInfo(USceneComponent* Component);

	/** Parent world transform used to reconstruct new world transforms for late update scene proxies */
	FTransform LateUpdateParentToWorld[2];
	/** Primitives that need late update before rendering */
	TArray<LateUpdatePrimitiveInfo> LateUpdatePrimitives[2];

	int32 LateUpdateGameWriteIndex;
	int32 LateUpdateRenderReadIndex;

};


/**
* An override of the MotionControllerComponent that implements position replication and Gripping with grip replication and controllable late updates per object.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = MotionController)
class VREXPANSIONPLUGIN_API UGripMotionControllerComponent : public UMotionControllerComponent//PrimitiveComponent
{

public:

	// If true will subtract the HMD's location from the position, useful for if the actors base is set to the HMD location always (simple character).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionController")
	bool bOffsetByHMD;
	
	FVector LastLocationForLateUpdate;
private:

	GENERATED_UCLASS_BODY()
	~UGripMotionControllerComponent();

	// Used to set the difference since last tick for TickGrip()
	FVector LastControllerLocation; 

	// Custom version of the component sweep function to remove that aggravating warning epic is throwing about skeletal mesh components.
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void OnUnregister() override;
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	virtual void Deactivate() override;
	virtual void BeginDestroy() override;

protected:
	//~ Begin UActorComponent Interface.
	virtual void SendRenderTransform_Concurrent() override;
	//~ End UActorComponent Interface.

	FTransform GripRenderThreadRelativeTransform;
	FVector GripRenderThreadComponentScale;

public:

	// Gets the hand enum
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "HandType", CompactNodeTitle = "HandType"))
	void GetHandType(EControllerHand& Hand)
	{
		if (!FXRMotionControllerBase::GetHandEnumForSourceName(MotionSource, Hand))
		{
			Hand = EControllerHand::Left;
		}
	}

	// When possible I suggest that you use GetAllGrips/GetGrippedObjects instead of directly referencing this
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "VRGrip", ReplicatedUsing = OnRep_GrippedObjects)
	TArray<FBPActorGripInformation> GrippedObjects;

	// When possible I suggest that you use GetAllGrips/GetGrippedObjects instead of directly referencing this
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "VRGrip", ReplicatedUsing = OnRep_LocallyGrippedObjects)
	TArray<FBPActorGripInformation> LocallyGrippedObjects;

	// Locally Gripped Array functions

	// Notify a client that their local grip was bad
	UFUNCTION(Reliable, Client, WithValidation, Category = "VRGrip")
	void Client_NotifyInvalidLocalGrip(UObject * LocallyGrippedObject);

	// Notify the server that we locally gripped something
	UFUNCTION(Reliable, Server, WithValidation, Category = "VRGrip")
	void Server_NotifyLocalGripAddedOrChanged(const FBPActorGripInformation & newGrip);

	// Notify the server that we changed some secondary attachment information
	UFUNCTION(Reliable, Server, WithValidation)
		void Server_NotifySecondaryAttachmentChanged(
			UObject * GrippedObject,
			FBPSecondaryGripInfo SecondaryGripInfo);

	// Notify change on relative position editing as well, make RPCS callable in blueprint
	// Notify the server that we locally gripped something
	UFUNCTION(Reliable, Server, WithValidation)
	void Server_NotifyLocalGripRemoved(const FBPActorGripInformation & removeGrip, FVector_NetQuantize100 AngularVelocity, FVector_NetQuantize100 LinearVelocity);
	

	// Enable this to send the TickGrip event every tick even for non custom grip types - has a slight performance hit
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGrip")
	bool bAlwaysSendTickGrip;

	// Clean up a grip that is "bad", object is being destroyed or was a bad destructible mesh
	void CleanUpBadGrip(TArray<FBPActorGripInformation> &GrippedObjectsArray, int GripIndex, bool bReplicatedArray);

	// Recreates a grip in situations where the collision type or movement replication type may have been changed
	inline void ReCreateGrip(FBPActorGripInformation & GripInfo)
	{
		int HandleIndex = 0;

		if (GetPhysicsGripIndex(GripInfo, HandleIndex))
		{
			DestroyPhysicsHandle(PhysicsGrips[HandleIndex].SceneIndex, &PhysicsGrips[HandleIndex].HandleData, &PhysicsGrips[HandleIndex].KinActorData);
			PhysicsGrips.RemoveAt(HandleIndex);
		}

		// Grip Type or replication was changed
		NotifyGrip(GripInfo, true);
	}

	UFUNCTION()
	virtual void OnRep_GrippedObjects(/*TArray<FBPActorGripInformation> OriginalArrayState*/) // Original array state is useless without full serialize, it just hold last delta
	{
		// Need to think about how best to handle the simulating flag here, don't handle for now
		// Check for removed gripped actors
		// This might actually be better left as an RPC multicast

		for (FBPActorGripInformation & Grip : GrippedObjects)
		{
			HandleGripReplication(Grip);
		}
	}

	UFUNCTION()
	virtual void OnRep_LocallyGrippedObjects()
	{
		for (FBPActorGripInformation & Grip : LocallyGrippedObjects)
		{
			HandleGripReplication(Grip);
		}
	}

	inline bool HandleGripReplication(FBPActorGripInformation & Grip)
	{
		if (!Grip.ValueCache.bWasInitiallyRepped) // Hasn't already been initialized
		{
			Grip.ValueCache.bWasInitiallyRepped = NotifyGrip(Grip); // Grip it

			// Tick will keep checking from here on out locally
			if (!Grip.ValueCache.bWasInitiallyRepped)
			{
				UE_LOG(LogVRMotionController, Warning, TEXT("Replicated grip Notify grip failed, was grip called before the object was replicated to the client?"));
				return false;
			}
			//Grip.ValueCache.bWasInitiallyRepped = true; // Set has been initialized
		}
		else // Check for changes from cached information
		{
			// Manage lerp states
			if (Grip.ValueCache.bCachedHasSecondaryAttachment != Grip.SecondaryGripInfo.bHasSecondaryAttachment || Grip.ValueCache.CachedSecondaryRelativeLocation != Grip.SecondaryGripInfo.SecondaryRelativeLocation)
			{
				// Reset the secondary grip distance
				Grip.SecondaryGripInfo.SecondaryGripDistance = 0.0f;

				const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
				Grip.AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.CutoffSlope = VRSettings.OneEuroCutoffSlope;
				Grip.AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.DeltaCutoff = VRSettings.OneEuroDeltaCutoff;
				Grip.AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.MinCutoff = VRSettings.OneEuroMinCutoff;
				Grip.AdvancedGripSettings.SecondaryGripSettings.SmoothingOneEuro.ResetSmoothingFilter();
				
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

				// Now calling the on secondary grip interface function client side as well
				if (Grip.SecondaryGripInfo.bHasSecondaryAttachment)
				{
					if (Grip.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
					{
						IVRGripInterface::Execute_OnSecondaryGrip(Grip.GrippedObject, Grip.SecondaryGripInfo.SecondaryAttachment, Grip);
					}
				}
				else
				{
					if (Grip.GrippedObject->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
					{
						IVRGripInterface::Execute_OnSecondaryGripRelease(Grip.GrippedObject, Grip.SecondaryGripInfo.SecondaryAttachment, Grip);
					}
				}
			}

			if (Grip.ValueCache.CachedGripCollisionType != Grip.GripCollisionType ||
				Grip.ValueCache.CachedGripMovementReplicationSetting != Grip.GripMovementReplicationSetting ||
				Grip.ValueCache.CachedBoneName != Grip.GrippedBoneName)
			{
				ReCreateGrip(Grip); // Need to re-create grip
			}
			else // If re-creating the grip anyway we don't need to do the below
			{
				// If the stiffness and damping got changed server side
				if ( !FMath::IsNearlyEqual(Grip.ValueCache.CachedStiffness, Grip.Stiffness) || !FMath::IsNearlyEqual(Grip.ValueCache.CachedDamping, Grip.Damping) || Grip.ValueCache.CachedPhysicsSettings != Grip.AdvancedGripSettings.PhysicsSettings)
				{
					SetGripConstraintStiffnessAndDamping(&Grip);
				}
			}
		}

		// Set caches now for next rep
		Grip.ValueCache.bCachedHasSecondaryAttachment = Grip.SecondaryGripInfo.bHasSecondaryAttachment;
		Grip.ValueCache.CachedSecondaryRelativeLocation = Grip.SecondaryGripInfo.SecondaryRelativeLocation;
		Grip.ValueCache.CachedGripCollisionType = Grip.GripCollisionType;
		Grip.ValueCache.CachedGripMovementReplicationSetting = Grip.GripMovementReplicationSetting;
		Grip.ValueCache.CachedStiffness = Grip.Stiffness;
		Grip.ValueCache.CachedDamping = Grip.Damping;
		Grip.ValueCache.CachedPhysicsSettings = Grip.AdvancedGripSettings.PhysicsSettings;
		Grip.ValueCache.CachedBoneName = Grip.GrippedBoneName;

		return true;
	}

	UPROPERTY(BlueprintReadWrite, Category = "VRGrip")
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
				LastUpdatesRelativePosition = this->RelativeLocation;
				LastUpdatesRelativeRotation = this->RelativeRotation;
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
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
	}


	inline bool IsServer() const
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

	/* Auto grip any uobject that is/root is a primitive component and has the VR Grip Interface
	these are stored in a Tarray that will prevent destruction of the object, you MUST ungrip an actor if you want to kill it
	The WorldOffset is the transform that it will remain away from the controller, if you use the world position of the actor then it will grab
	at the point of intersection.

	If WorldOffsetIsRelative is true then it will not convert the transform from world space but will instead use that offset directly.
	You could pass in a socket relative transform with this set for snapping or an empty transform to snap the object at its 0,0,0 point.

	If you declare a valid OptionSnapToSocketName then it will instead snap the actor to the relative offset
	location that the socket is to its parent actor.

	If you declare a valid OptionalBoneToGripName then it will grip that physics body with physics grips (It will expect a bone worldspace transform then,
	if you pass in the normal actor/root component world space transform then the grip will not be positioned correctly).
	*/
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
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
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
		bool DropObject(
			UObject * ObjectToDrop,
			bool bSimulate,
			FVector OptionalAngularVelocity = FVector::ZeroVector,
			FVector OptionalLinearVelocity = FVector::ZeroVector);

	// Auto grip any uobject that is/root is a primitive component
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
		bool GripObjectByInterface(UObject * ObjectToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, FName OptionalBoneToGripName = NAME_None, bool bIsSlotGrip = false);

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

	   If you declare a valid OptionalBoneToGripName then it will grip that physics body with physics grips (It will expect a bone worldspace transform then, 
	   if you pass in the normal actor/root component world space transform then the grip will not be positioned correctly).
	*/
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
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
		FName OptionalBoneToGrip_Name = NAME_None, 
		FName OptionalBoneToGripName = NAME_None,
		EGripCollisionType GripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics, 
		EGripLateUpdateSettings GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping,
		EGripMovementReplicationSettings GripMovementReplicationSetting = EGripMovementReplicationSettings::ForceClientSideMovement,
		float GripStiffness = 1500.0f, 
		float GripDamping = 200.0f,
		bool bIsSlotGrip = false);

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
	bool NotifyGrip(FBPActorGripInformation &NewGrip, bool bIsReInit = false);

	UFUNCTION(Reliable, NetMulticast)
	void NotifyDrop(const FBPActorGripInformation &NewDrop, bool bSimulate);

	// Used so drop logic can be filtered
	void Drop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate);

	// Get a grip by actor
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void GetGripByActor(FBPActorGripInformation &Grip, AActor * ActorToLookForGrip, EBPVRResultSwitch &Result);

	// Get a grip by component
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void GetGripByComponent(FBPActorGripInformation &Grip, UPrimitiveComponent * ComponentToLookForGrip, EBPVRResultSwitch &Result);

	// Gets a grip by object, will auto use ByComponent or ByActor
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
	void GetGripByObject(FBPActorGripInformation &Grip, UObject * ObjectToLookForGrip, EBPVRResultSwitch &Result);

	// Get the physics velocities of a grip
	UFUNCTION(BlueprintPure, Category = "VRGrip")
		void GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity);

	// Set the Grip Collision Type of a grip, call server side if not a local grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripCollisionType(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			EGripCollisionType NewGripCollisionType = EGripCollisionType::InteractiveCollisionWithPhysics
			);


	// Set the late update setting of a grip, call server side if not a local grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripLateUpdateSetting(
			const FBPActorGripInformation &Grip, 
			EBPVRResultSwitch &Result,
			EGripLateUpdateSettings NewGripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping	
			);

	// Set the relative transform of a grip, call server side if not a local grip
	// Can check HasGripAuthority to decide if callable locally
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripRelativeTransform(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			const FTransform & NewRelativeTransform
			);

	// Set the addition transform of a grip, CALL LOCALLY, not server side, Addition transform is not replicated
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripAdditionTransform(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			const FTransform & NewAdditionTransform, bool bMakeGripRelative = false
			);

	// Set the constraint stiffness and dampening of a grip, call server side if not a local grip
	// Can check HasGripAuthority to decide if callable locally
	UFUNCTION(BlueprintCallable, Category = "VRGrip", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripStiffnessAndDamping(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			float NewStiffness, float NewDamping, bool bAlsoSetAngularValues = false, float OptionalAngularStiffness = 0.0f, float OptionalAngularDamping = 0.0f
		);

	// Used to convert an offset transform to grip relative, useful for storing an initial offset and then lerping back to 0 without re-calculating every tick
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (DisplayName = "CreateGripRelativeAdditionTransform"))
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

	// Returns if we have grip authority (can call drop / grip on this grip)
	// Mostly for networked games as local grips are client authed and all others are server authed
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (DisplayName = "HasGripAuthority"))
		bool BP_HasGripAuthority(const FBPActorGripInformation &Grip);

	// Checks if we should be handling the movement of a grip based on settings for it
	inline bool HasGripMovementAuthority(const FBPActorGripInformation &Grip);

	// Returns if we have grip movement authority (we handle movement of the grip)
	// Mostly for networked games where ClientSide will be true for all and ServerSide will be true for server only
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (DisplayName = "HasGripMovementAuthority"))
		bool BP_HasGripMovementAuthority(const FBPActorGripInformation &Grip);

	// Running the gripping logic in its own function as the main tick was getting bloated
	void TickGrip(float DeltaTime);

	// Splitting logic into separate function
	void HandleGripArray(TArray<FBPActorGripInformation> &GrippedObjectsArray, const FTransform & ParentTransform, float DeltaTime, bool bReplicatedArray = false);

	// Gets the world transform of a grip, modified by secondary grips and interaction settings
	inline void GetGripWorldTransform(float DeltaTime,FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool & bRescalePhysicsGrips);

	// Handle modifying the transform per the grip interaction settings, returns final world transform
	inline FTransform HandleInteractionSettings(float DeltaTime, const FTransform & ParentTransform, UPrimitiveComponent * root, FBPInteractionSettings InteractionSettings, FBPActorGripInformation & GripInfo);

	// Converts a worldspace transform into being relative to this motion controller
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	FTransform ConvertToControllerRelativeTransform(const FTransform & InTransform)
	{
		return InTransform.GetRelativeTransform(this->GetComponentTransform());
	}

	// Creates a secondary grip relative transform
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	static FTransform ConvertToGripRelativeTransform(const FTransform& GrippedActorTransform, const FTransform & InTransform)
	{
		return InTransform.GetRelativeTransform(GrippedActorTransform);
	}

	// Gets if the given object is held by this controller
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetIsObjectHeld(const UObject * ObjectToCheck)
	{
		if (!ObjectToCheck)
			return false;

		return (GrippedObjects.FindByKey(ObjectToCheck) || LocallyGrippedObjects.FindByKey(ObjectToCheck));
	}

	// Gets if the given actor is held by this controller
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetIsHeld(const AActor * ActorToCheck)
	{
		if (!ActorToCheck)
			return false;

		return (GrippedObjects.FindByKey(ActorToCheck) || LocallyGrippedObjects.FindByKey(ActorToCheck));
	}

	// Gets if the given component is held by this controller
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool GetIsComponentHeld(const UPrimitiveComponent * ComponentToCheck)
	{
		if (!ComponentToCheck)
			return false;

		return (GrippedObjects.FindByKey(ComponentToCheck) || LocallyGrippedObjects.FindByKey(ComponentToCheck));

		return false;
	}

	// Gets if the given Component is a secondary attach point to a gripped actor
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool GetIsSecondaryAttachment(const USceneComponent * ComponentToCheck, FBPActorGripInformation & Grip)
	{
		if (!ComponentToCheck)
			return false;

		for (int i = 0; i < GrippedObjects.Num(); ++i)
		{
			if(GrippedObjects[i].SecondaryGripInfo.bHasSecondaryAttachment && GrippedObjects[i].SecondaryGripInfo.SecondaryAttachment == ComponentToCheck)
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

	// Get if we have gripped objects, local or replicated
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	bool HasGrippedObjects()
	{
		return GrippedObjects.Num() > 0 || LocallyGrippedObjects.Num() > 0;
	}

	// Get list of all gripped objects grip info structures (local and normal both)
	UFUNCTION(BlueprintPure, Category = "VRGrip")
		void GetAllGrips(TArray<FBPActorGripInformation> &GripArray);

	// Get list of all gripped actors
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	void GetGrippedActors(TArray<AActor*> &GrippedActorArray);

	// Get list of all gripped objects
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	void GetGrippedObjects(TArray<UObject*> &GrippedObjectsArray);

	// Get list of all gripped components
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	void GetGrippedComponents(TArray<UPrimitiveComponent*> &GrippedComponentsArray);

	// After teleporting a pawn you NEED to call this, otherwise gripped objects will travel with a sweeped move and can get caught on geometry
	// The base Teleport() function automatically calls this already, but when you manually set location you should do it yourself.
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	void PostTeleportMoveGrippedObjects();

	// Move a single gripped item back into position ignoring collision in the way
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool TeleportMoveGrippedActor(AActor * GrippedActorToMove);

	// Move a single gripped item back into position ignoring collision in the way
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool TeleportMoveGrippedComponent(UPrimitiveComponent * ComponentToMove);

	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool TeleportMoveGrip(UPARAM(ref)FBPActorGripInformation &Grip, bool bIsPostTeleport = false);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool AddSecondaryAttachmentPoint(UObject * GrippedObjectToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform &OriginalTransform, bool bTransformIsAlreadyRelative = false, float LerpToTime = 0.25f, bool bIsSlotGrip = false);

	// Removes a secondary attachment point from a grip
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
	bool RemoveSecondaryAttachmentPoint(UObject * GrippedObjectToRemoveAttachment, float LerpToTime = 0.25f);

	// This is for testing, setting it to true allows you to test grip with a non VR enabled pawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGrip")
	bool bUseWithoutTracking;

	bool CheckComponentWithSweep(UPrimitiveComponent * ComponentToCheck, FVector Move, FRotator newOrientation, bool bSkipSimulatingComponents/*, bool & bHadBlockingHitOut*/);
	
	// For physics handle operations
	bool SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip);
	bool DestroyPhysicsHandle(const FBPActorGripInformation &Grip);
	void UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform);
	bool GetPhysicsJointLength(const FBPActorGripInformation &GrippedActor, UPrimitiveComponent * rootComp, FVector & LocOut);
	bool SetGripConstraintStiffnessAndDamping(const FBPActorGripInformation *Grip, bool bUseHybridMultiplier = false);

	TArray<FBPActorPhysicsHandleInformation> PhysicsGrips;
	FBPActorPhysicsHandleInformation * GetPhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo, int & index);
	FBPActorPhysicsHandleInformation * CreatePhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool DestroyPhysicsHandle(int32 SceneIndex, physx::PxD6Joint** HandleData, physx::PxRigidDynamic** KinActorData);

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
	bool GripControllerIsTracked() const
	{
		return bTracked;
	}

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
		virtual void PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily) override;

		virtual int32 GetPriority() const override { return -10; }
		virtual bool IsActiveThisFrame(class FViewport* InViewport) const;

	private:
		friend class UGripMotionControllerComponent;

		/** Motion controller component associated with this view extension */
		UGripMotionControllerComponent* MotionControllerComponent;

		FExpandedLateUpdateManager LateUpdate;
	};
	TSharedPtr< FGripViewExtension, ESPMode::ThreadSafe > GripViewExtension;

};
