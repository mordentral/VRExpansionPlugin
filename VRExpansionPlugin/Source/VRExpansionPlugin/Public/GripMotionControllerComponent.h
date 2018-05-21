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

/** Delegate for notification when the controller grips a new object. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRGripControllerOnGripSignature, const FBPActorGripInformation &, GripInformation);

/** Delegate for notification when the controller drops a gripped object. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRGripControllerOnDropSignature, const FBPActorGripInformation &, GripInformation);

/** Delegate for notification when the controller grips a new object. */
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
	bool bOffsetByHMD;
	
	FVector LastLocationForLateUpdate;

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

private:

	GENERATED_UCLASS_BODY()
	~UGripMotionControllerComponent();

	// Custom version of the component sweep function to remove that aggravating warning epic is throwing about skeletal mesh components.
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void OnUnregister() override;
	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	virtual void Deactivate() override;
	virtual void BeginDestroy() override;
	virtual void BeginPlay() override;

protected:
	//~ Begin UActorComponent Interface.
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

	// Called when a object is gripped
	UPROPERTY(BlueprintAssignable, Category = "GripMotionController")
		FVRGripControllerOnGripSignature OnGrippedObject;

	// Called when a object is dropped
	UPROPERTY(BlueprintAssignable, Category = "GripMotionController")
		FVRGripControllerOnGripSignature OnDroppedObject;

	// Gets the hand enum
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "HandType", CompactNodeTitle = "HandType"))
	void GetHandType(EControllerHand& Hand)
	{
		if (!FXRMotionControllerBase::GetHandEnumForSourceName(MotionSource, Hand))
		{
			Hand = EControllerHand::Left;
		}
	}

	// Increments with each grip, wraps back to 0 after max due to modulo operation
	// I don't think that a 256 grip index is going to be used up and allow duplicates unless
	// someone does something crazy
	uint8 GripIDIncrementer;

	inline uint8 GetNextGripID(bool bIsLocalGrip)
	{
		if (bIsLocalGrip) // We need to split them between 0-127 for gripped objects server side
		{
			if (GripIDIncrementer < 127)
				GripIDIncrementer++;
			else
				GripIDIncrementer = 0;

			return GripIDIncrementer + 128;
		}
		else // And 128 - 255 for local grips client side
		{
			if (GripIDIncrementer < 127)
				GripIDIncrementer++;
			else
				GripIDIncrementer = 0;

			return GripIDIncrementer;
		}
	}

	// When possible I suggest that you use GetAllGrips/GetGrippedObjects instead of directly referencing this
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "GripMotionController", ReplicatedUsing = OnRep_GrippedObjects)
	TArray<FBPActorGripInformation> GrippedObjects;

	// When possible I suggest that you use GetAllGrips/GetGrippedObjects instead of directly referencing this
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "GripMotionController", ReplicatedUsing = OnRep_LocallyGrippedObjects)
	TArray<FBPActorGripInformation> LocallyGrippedObjects;

	// Locally Gripped Array functions

	// Notify a client that their local grip was bad
	UFUNCTION(Reliable, Client, WithValidation, Category = "GripMotionController")
	void Client_NotifyInvalidLocalGrip(UObject * LocallyGrippedObject);

	// Notify the server that we locally gripped something
	UFUNCTION(Reliable, Server, WithValidation, Category = "GripMotionController")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
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
		if (Grip.ValueCache.bWasInitiallyRepped && Grip.GripID != Grip.ValueCache.CachedGripID)
		{
			// There appears to be a bug with TArray replication where if you replace an index with another value of that
			// Index, it doesn't fully re-init the object, this is a workaround to re-zero non replicated variables
			// when that happens.
			Grip.ClearNonReppingItems();
		}
		
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
			if (Grip.ValueCache.bCachedHasSecondaryAttachment != Grip.SecondaryGripInfo.bHasSecondaryAttachment || !Grip.ValueCache.CachedSecondaryRelativeTransform.Equals(Grip.SecondaryGripInfo.SecondaryRelativeTransform))
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
				Grip.ValueCache.CachedBoneName != Grip.GrippedBoneName ||
				Grip.ValueCache.CachedPhysicsSettings.bUsePhysicsSettings != Grip.AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings
				)
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
		Grip.ValueCache.CachedSecondaryRelativeTransform = Grip.SecondaryGripInfo.SecondaryRelativeTransform;
		Grip.ValueCache.CachedGripCollisionType = Grip.GripCollisionType;
		Grip.ValueCache.CachedGripMovementReplicationSetting = Grip.GripMovementReplicationSetting;
		Grip.ValueCache.CachedStiffness = Grip.Stiffness;
		Grip.ValueCache.CachedDamping = Grip.Damping;
		Grip.ValueCache.CachedPhysicsSettings = Grip.AdvancedGripSettings.PhysicsSettings;
		Grip.ValueCache.CachedBoneName = Grip.GrippedBoneName;
		Grip.ValueCache.CachedGripID = Grip.GripID;

		return true;
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
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool DropObject(
			UObject * ObjectToDrop,
			bool bSimulate,
			FVector OptionalAngularVelocity = FVector::ZeroVector,
			FVector OptionalLinearVelocity = FVector::ZeroVector);

	// Auto grip any uobject that is/root is a primitive component
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
		bool GripObjectByInterface(UObject * ObjectToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative = false, FName OptionalBoneToGripName = NAME_None, bool bIsSlotGrip = false);

	// Auto drop any uobject that is/root is a primitive component and has the VR Grip Interface	
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
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
		FName OptionalBoneToGrip_Name = NAME_None, 
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

	// Get the physics velocities of a grip
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		void GetPhysicsVelocities(const FBPActorGripInformation &Grip, FVector &AngularVelocity, FVector &LinearVelocity);

	// Sets whether an active grip is paused or not (is not replicated by default as it is likely you will want to pass variables with this setting).
	// If you want it server authed you should RPC a bool down with any additional information (ie: attach location).
	UFUNCTION(BlueprintCallable, Category = "GripMotionController", meta = (ExpandEnumAsExecs = "Result"))
		void SetGripPaused(
			const FBPActorGripInformation &Grip,
			EBPVRResultSwitch &Result,
			bool bIsPaused = false,
			bool bNoConstraintWhenPaused = false
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

	// Returns if we have grip authority (can call drop / grip on this grip)
	// Mostly for networked games as local grips are client authed and all others are server authed
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "HasGripAuthority"))
		bool BP_HasGripAuthority(const FBPActorGripInformation &Grip);

	// Checks if we should be handling the movement of a grip based on settings for it
	inline bool HasGripMovementAuthority(const FBPActorGripInformation &Grip);

	// Returns if we have grip movement authority (we handle movement of the grip)
	// Mostly for networked games where ClientSide will be true for all and ServerSide will be true for server only
	UFUNCTION(BlueprintPure, Category = "GripMotionController", meta = (DisplayName = "HasGripMovementAuthority"))
		bool BP_HasGripMovementAuthority(const FBPActorGripInformation &Grip);

	// Running the gripping logic in its own function as the main tick was getting bloated
	void TickGrip(float DeltaTime);

	// Splitting logic into separate function
	void HandleGripArray(TArray<FBPActorGripInformation> &GrippedObjectsArray, const FTransform & ParentTransform, float DeltaTime, bool bReplicatedArray = false);

	// Gets the world transform of a grip, modified by secondary grips and interaction settings
	void GetGripWorldTransform(float DeltaTime,FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool & bRescalePhysicsGrips);

	// Handle modifying the transform per the grip interaction settings, returns final world transform
	FTransform HandleInteractionSettings(float DeltaTime, const FTransform & ParentTransform, UPrimitiveComponent * root, FBPInteractionSettings InteractionSettings, FBPActorGripInformation & GripInfo);

	// Converts a worldspace transform into being relative to this motion controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	FTransform ConvertToControllerRelativeTransform(const FTransform & InTransform)
	{
		return InTransform.GetRelativeTransform(this->GetComponentTransform());
	}

	// Creates a secondary grip relative transform
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	static FTransform ConvertToGripRelativeTransform(const FTransform& GrippedActorTransform, const FTransform & InTransform)
	{
		return InTransform.GetRelativeTransform(GrippedActorTransform);
	}

	// Gets if the given object is held by this controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	bool GetIsObjectHeld(const UObject * ObjectToCheck)
	{
		if (!ObjectToCheck)
			return false;

		return (GrippedObjects.FindByKey(ObjectToCheck) || LocallyGrippedObjects.FindByKey(ObjectToCheck));
	}

	// Gets if the given actor is held by this controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	bool GetIsHeld(const AActor * ActorToCheck)
	{
		if (!ActorToCheck)
			return false;

		return (GrippedObjects.FindByKey(ActorToCheck) || LocallyGrippedObjects.FindByKey(ActorToCheck));
	}

	// Gets if the given component is held by this controller
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	bool GetIsComponentHeld(const UPrimitiveComponent * ComponentToCheck)
	{
		if (!ComponentToCheck)
			return false;

		return (GrippedObjects.FindByKey(ComponentToCheck) || LocallyGrippedObjects.FindByKey(ComponentToCheck));

		return false;
	}

	// Gets if the given Component is a secondary attach point to a gripped actor
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
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
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	bool HasGrippedObjects()
	{
		return GrippedObjects.Num() > 0 || LocallyGrippedObjects.Num() > 0;
	}

	// Get list of all gripped objects grip info structures (local and normal both)
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
		void GetAllGrips(TArray<FBPActorGripInformation> &GripArray);

	// Get list of all gripped actors
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	void GetGrippedActors(TArray<AActor*> &GrippedActorArray);

	// Get list of all gripped objects
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	void GetGrippedObjects(TArray<UObject*> &GrippedObjectsArray);

	// Get list of all gripped components
	UFUNCTION(BlueprintPure, Category = "GripMotionController")
	void GetGrippedComponents(TArray<UPrimitiveComponent*> &GrippedComponentsArray);

	// After teleporting a pawn you NEED to call this, otherwise gripped objects will travel with a sweeped move and can get caught on geometry
	// The base Teleport() function automatically calls this already, but when you manually set location you should do it yourself.
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	void PostTeleportMoveGrippedObjects();

	bool bIsPostTeleport;

	// Move a single gripped item back into position ignoring collision in the way
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool TeleportMoveGrippedActor(AActor * GrippedActorToMove);

	// Move a single gripped item back into position ignoring collision in the way
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool TeleportMoveGrippedComponent(UPrimitiveComponent * ComponentToMove);

	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool TeleportMoveGrip(UPARAM(ref)FBPActorGripInformation &Grip, bool bIsForPostTeleport = false);
	bool TeleportMoveGrip_Impl(FBPActorGripInformation &Grip, bool bIsForPostTeleport, FTransform & OptionalTransform);

	// Adds a secondary attachment point to the grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool AddSecondaryAttachmentPoint(UObject * GrippedObjectToAddAttachment, USceneComponent * SecondaryPointComponent, const FTransform &OriginalTransform, bool bTransformIsAlreadyRelative = false, float LerpToTime = 0.25f, bool bIsSlotGrip = false);

	// Removes a secondary attachment point from a grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController")
	bool RemoveSecondaryAttachmentPoint(UObject * GrippedObjectToRemoveAttachment, float LerpToTime = 0.25f);

	// This is for testing, setting it to true allows you to test grip with a non VR enabled pawn
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripMotionController")
	bool bUseWithoutTracking;

	bool CheckComponentWithSweep(UPrimitiveComponent * ComponentToCheck, FVector Move, FRotator newOrientation, bool bSkipSimulatingComponents/*, bool & bHadBlockingHitOut*/);
	
	// For physics handle operations
	bool SetUpPhysicsHandle(const FBPActorGripInformation &NewGrip);
	bool DestroyPhysicsHandle(const FBPActorGripInformation &Grip);
	void UpdatePhysicsHandleTransform(const FBPActorGripInformation &GrippedActor, const FTransform& NewTransform);
	bool SetGripConstraintStiffnessAndDamping(const FBPActorGripInformation *Grip, bool bUseHybridMultiplier = false);
	bool GetPhysicsJointLength(const FBPActorGripInformation &GrippedActor, UPrimitiveComponent * rootComp, FVector & LocOut);

	TArray<FBPActorPhysicsHandleInformation> PhysicsGrips;
	FBPActorPhysicsHandleInformation * GetPhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool GetPhysicsGripIndex(const FBPActorGripInformation & GripInfo, int & index);
	FBPActorPhysicsHandleInformation * CreatePhysicsGrip(const FBPActorGripInformation & GripInfo);
	bool DestroyPhysicsHandle(int32 SceneIndex, physx::PxD6Joint** HandleData, physx::PxRigidDynamic** KinActorData);

	// Creates a physics handle for this grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "SetUpPhysicsHandle"))
	bool SetUpPhysicsHandle_BP(UPARAM(ref)const FBPActorGripInformation &NewGrip)
	{
		return SetUpPhysicsHandle(NewGrip);
	}

	// Destroys a physics handle for this grip
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "DestroyPhysicsHandle"))
	bool DestroyPhysicsHandle_BP(UPARAM(ref)const FBPActorGripInformation &Grip)
	{
		return DestroyPhysicsHandle(Grip);
	}

	// Update the location of the physics handle
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "UpdatePhysicsHandleTransform"))
	void UpdatePhysicsHandleTransform_BP(UPARAM(ref)const FBPActorGripInformation &GrippedActor, UPARAM(ref)const FTransform& NewTransform)
	{
		return UpdatePhysicsHandleTransform(GrippedActor, NewTransform);
	}

	// Get the grip distance of either the physics handle if there is one, or the difference from the hand to the root component if there isn't
	UFUNCTION(BlueprintCallable, Category = "GripMotionController|Custom", meta = (DisplayName = "GetGripDistance"))
	bool GetGripDistance_BP(UPARAM(ref)FBPActorGripInformation &Grip, FVector ExpectedLocation, float & CurrentDistance)
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