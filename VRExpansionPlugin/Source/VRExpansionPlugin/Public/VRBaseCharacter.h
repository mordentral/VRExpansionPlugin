// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "VRBaseCharacterMovementComponent.h"
#include "ReplicatedVRCameraComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "GripMotionControllerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Components/CapsuleComponent.h"
#include "VRBaseCharacter.generated.h"

class AVRPlayerController;

DECLARE_LOG_CATEGORY_EXTERN(LogBaseVRCharacter, Log, All);

/** Delegate for notification when the lever state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVRSeatThresholdChangedSignature, bool, bIsWithinThreshold, float, ToThresholdScaler);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRPlayerStateReplicatedSignature, const APlayerState *, NewPlayerState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRPlayerTeleportedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRPlayerNetworkCorrectedSignature);

USTRUCT()
struct VREXPANSIONPLUGIN_API FRepMovementVRCharacter : public FRepMovement
{
	GENERATED_BODY()

public:
	
	FRepMovementVRCharacter();

	UPROPERTY(Transient)
		bool bJustTeleported;

	UPROPERTY(Transient)
		bool bJustTeleportedGrips;

	UPROPERTY(Transient)
		bool bPausedTracking;

	UPROPERTY(Transient)
		FVector_NetQuantize100 PausedTrackingLoc;

	UPROPERTY(Transient)
		float PausedTrackingRot;

	UPROPERTY(Transient)
		AActor* Owner;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		FRepMovement BaseSettings = Owner ? Owner->GetReplicatedMovement() : FRepMovement();

		// pack bitfield with flags
		uint8 Flags = (bSimulatedPhysicSleep << 0) | (bRepPhysics << 1) | (bJustTeleported << 2) | (bJustTeleportedGrips << 3) | (bPausedTracking << 4);
		Ar.SerializeBits(&Flags, 5);
		bSimulatedPhysicSleep = (Flags & (1 << 0)) ? 1 : 0;
		bRepPhysics = (Flags & (1 << 1)) ? 1 : 0;
		bJustTeleported = (Flags & (1 << 2)) ? 1 : 0;
		bJustTeleportedGrips = (Flags & (1 << 3)) ? 1 : 0;
		bPausedTracking = (Flags & (1 << 4)) ? 1 : 0;

		bOutSuccess = true;

		if (bPausedTracking)
		{
			bOutSuccess &= PausedTrackingLoc.NetSerialize(Ar, Map, bOutSuccess);

			uint16 Yaw = 0;
			if (Ar.IsSaving())
			{
				Yaw = FRotator::CompressAxisToShort(PausedTrackingRot);
				Ar << Yaw;
			}
			else
			{
				Ar << Yaw;
				PausedTrackingRot = Yaw;
			}

		}

		// update location, rotation, linear velocity
		bOutSuccess &= SerializeQuantizedVector(Ar, Location, BaseSettings.LocationQuantizationLevel);

		switch (BaseSettings.RotationQuantizationLevel)
		{
		case ERotatorQuantization::ByteComponents:
		{
			Rotation.SerializeCompressed(Ar);
			break;
		}

		case ERotatorQuantization::ShortComponents:
		{
			Rotation.SerializeCompressedShort(Ar);
			break;
		}
		}

		bOutSuccess &= SerializeQuantizedVector(Ar, LinearVelocity, BaseSettings.VelocityQuantizationLevel);

		// update angular velocity if required
		if (bRepPhysics)
		{
			bOutSuccess &= SerializeQuantizedVector(Ar, AngularVelocity, BaseSettings.VelocityQuantizationLevel);
		}

		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FRepMovementVRCharacter> : public TStructOpsTypeTraitsBase2<FRepMovementVRCharacter>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

USTRUCT(Blueprintable)
struct VREXPANSIONPLUGIN_API FVRSeatedCharacterInfo
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "CharacterSeatInfo")
		bool bSitting;
	UPROPERTY(BlueprintReadOnly, Category = "CharacterSeatInfo")
		bool bZeroToHead;
	UPROPERTY(BlueprintReadOnly, Category = "CharacterSeatInfo")
		FTransform_NetQuantize StoredTargetTransform;
	UPROPERTY(BlueprintReadOnly, Category = "CharacterSeatInfo")
		FTransform_NetQuantize InitialRelCameraTransform;
	UPROPERTY(BlueprintReadOnly, Category = "CharacterSeatInfo")
		USceneComponent* SeatParent;
	UPROPERTY(BlueprintReadOnly, Category = "CharacterSeatInfo")
		EVRConjoinedMovementModes PostSeatedMovementMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, NotReplicated, Category = "CharacterSeatInfo", meta = (ClampMin = "1.000", UIMin = "1.000", ClampMax = "256.000", UIMax = "256.000"))
		float AllowedRadius;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, NotReplicated, Category = "CharacterSeatInfo", meta = (ClampMin = "1.000", UIMin = "1.000", ClampMax = "256.000", UIMax = "256.000"))
		float AllowedRadiusThreshold;
	UPROPERTY(BlueprintReadOnly, NotReplicated, Category = "CharacterSeatInfo")
		float CurrentThresholdScaler;
	UPROPERTY(BlueprintReadOnly, NotReplicated, Category = "CharacterSeatInfo")
		bool bIsOverThreshold;

	bool bWasSeated;
	bool bOriginalControlRotation;
	bool bWasOverLimit;

	FVRSeatedCharacterInfo()
	{
		Clear();
	}

	void Clear()
	{
		bSitting = false;
		bIsOverThreshold = false;
		bWasOverLimit = false;
		bZeroToHead = true;
		StoredTargetTransform = FTransform::Identity;
		InitialRelCameraTransform = FTransform::Identity;
		bWasSeated = false;
		bOriginalControlRotation = false;
		AllowedRadius = 40.0f;
		AllowedRadiusThreshold = 20.0f;
		CurrentThresholdScaler = 0.0f;
		SeatParent = nullptr;
		PostSeatedMovementMode = EVRConjoinedMovementModes::C_MOVE_Walking;
	}

	void ClearTempVals()
	{
		bWasOverLimit = false;
		bWasSeated = false;
		bOriginalControlRotation = false;
		CurrentThresholdScaler = 0.0f;
	}


	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		Ar.SerializeBits(&bSitting, 1);
		Ar.SerializeBits(&bZeroToHead, 1);

		if (bSitting)
		{
			InitialRelCameraTransform.NetSerialize(Ar, Map, bOutSuccess);

			// Forcing a maximum value here so that we can compress it by making assumptions
			// 256 max value = 8 bits + 1 bit for sign + 7 bits for precision (up to 128 on precision, so full range 2 digit precision).
			if (Ar.IsSaving())
			{
				bOutSuccess &= WriteFixedCompressedFloat<256, 16>(AllowedRadius, Ar);
				bOutSuccess &= WriteFixedCompressedFloat<256, 16>(AllowedRadiusThreshold, Ar);
			}
			else
			{
				bOutSuccess &= ReadFixedCompressedFloat<256, 16>(AllowedRadius, Ar);
				bOutSuccess &= ReadFixedCompressedFloat<256, 16>(AllowedRadiusThreshold, Ar);
			}
		}

		StoredTargetTransform.NetSerialize(Ar, Map, bOutSuccess);
		Ar << SeatParent;
		Ar << PostSeatedMovementMode;	
		return bOutSuccess;
	}
};
template<>
struct TStructOpsTypeTraits< FVRSeatedCharacterInfo > : public TStructOpsTypeTraitsBase2<FVRSeatedCharacterInfo>
{
	enum
	{
		WithNetSerializer = true
	};
};

USTRUCT()
struct VREXPANSIONPLUGIN_API FVRReplicatedCapsuleHeight
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY()
		float CapsuleHeight;

	FVRReplicatedCapsuleHeight() :
		CapsuleHeight(0.0f)
	{}

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;
		// Forcing a maximum value here so that we can compress it by making assumptions
		// 1024 max value = 10 bits + 1 bit for sign + 7 bits for precision (up to 128 on precision, so full range 2 digit precision).
		if (Ar.IsSaving())
		{
			bOutSuccess &= WriteFixedCompressedFloat<1024, 18>(CapsuleHeight, Ar);
		}
		else
		{
			bOutSuccess &= ReadFixedCompressedFloat<1024, 18>(CapsuleHeight, Ar);
		}

		return bOutSuccess;
	}
};
template<>
struct TStructOpsTypeTraits< FVRReplicatedCapsuleHeight > : public TStructOpsTypeTraitsBase2<FVRReplicatedCapsuleHeight>
{
	enum
	{
		WithNetSerializer = true
	};
};

UCLASS()
class VREXPANSIONPLUGIN_API AVRBaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AVRBaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** BaseVR Character movement component belongs to */
	UPROPERTY(Transient, DuplicateTransient)
		AVRPlayerController* OwningVRPlayerController;

	//virtual void CacheInitialMeshOffset(FVector MeshRelativeLocation, FRotator MeshRelativeRotation) override;
	virtual void PostInitializeComponents() override;

	virtual void PossessedBy(AController* NewController);
	virtual void OnRep_Controller() override;
	virtual void OnRep_PlayerState() override;

	/** Used for replication of our RootComponent's position and velocity */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedMovement)
		struct FRepMovementVRCharacter ReplicatedMovementVR;

	bool bFlagTeleported;
	bool bFlagTeleportedGrips;
	bool bTrackingPaused;
	FVector PausedTrackingLoc;
	float PausedTrackingRot;

	// Injecting our custom teleport notification
	virtual void OnRep_ReplicatedMovement() override;
	virtual void GatherCurrentMovement() override;

	// Give my users direct access to an event for when the player has teleported
	UPROPERTY(BlueprintAssignable, Category = "VRMovement")
		FVRPlayerTeleportedSignature OnCharacterTeleported_Bind;

	// Give my users direct access to an event for when the player has been network corrected
	UPROPERTY(BlueprintAssignable, Category = "VRMovement")
		FVRPlayerNetworkCorrectedSignature OnCharacterNetworkCorrected_Bind;

	// Give my users direct access to an event for when the player state has changed
	UPROPERTY(BlueprintAssignable, Category = "VRMovement")
		FVRPlayerStateReplicatedSignature OnPlayerStateReplicated_Bind;

	//These functions are now housed in the base character and used when possible, it saves about 7 bits of packet header overhead per send.

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendTransformCamera(FBPVRComponentPosRep NewTransform);

	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendTransformLeftController(FBPVRComponentPosRep NewTransform);

	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendTransformRightController(FBPVRComponentPosRep NewTransform);

	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;

	// If true will replicate the capsule height on to clients, allows for dynamic capsule height changes in multiplayer
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRBaseCharacter")
		bool VRReplicateCapsuleHeight;

	// OnlyReplicated to simulated clients
	UPROPERTY(Replicated, ReplicatedUsing = OnRep_CapsuleHeight)
		FVRReplicatedCapsuleHeight ReplicatedCapsuleHeight;

	UFUNCTION()
	void OnRep_CapsuleHeight()
	{
		if (!VRReplicateCapsuleHeight)
			return;

		if (UCapsuleComponent * Capsule = Cast<UCapsuleComponent>(GetRootComponent()))
		{
			if (ReplicatedCapsuleHeight.CapsuleHeight > 0.0f && !FMath::IsNearlyEqual(ReplicatedCapsuleHeight.CapsuleHeight, Capsule->GetUnscaledCapsuleHalfHeight()))
			{
				SetCharacterHalfHeightVR(ReplicatedCapsuleHeight.CapsuleHeight, false);
			}
		}
	}

	// Override this in c++ or blueprints to pass in an IK mesh to be used in some optimizations
	// May be extended in the future
	//UFUNCTION(BlueprintNativeEvent, Category = "BaseVRCharacter")
	//USkeletalMeshComponent * GetIKMesh() const;
	//virtual USkeletalMeshComponent *  GetIKMesh_Implementation() const;1
	// #TODO: Work with the above, can do multiple things with it


	// Called when the client is in climbing mode and is stepped up onto a platform
	// Generally you should drop the climbing at this point and go into falling movement.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRMovement")
		void OnClimbingSteppedUp();
	virtual void OnClimbingSteppedUp_Implementation();

	/**
	* Event for adding to the climbing movement mode. Called by CharacterMovement if MovementMode is set to Climbing.
	* @note C++ code should override PhysCustom_Climbing() instead.
	*/
	UFUNCTION(BlueprintNativeEvent, meta = (DisplayName = "UpdateLowGravMovement", ScriptName = "UpdateLowGravMovement"))
		void UpdateLowGravMovement(float DeltaTime);
	virtual void UpdateLowGravMovement_Implementation(float DeltaTime) {} // Do nothing by default

	/**
	 * Event for adding to the climbing movement mode. Called by CharacterMovement if MovementMode is set to Climbing.
	 * @note C++ code should override PhysCustom_Climbing() instead.
	 */
	UFUNCTION(BlueprintNativeEvent, meta = (DisplayName = "UpdateClimbingMovement", ScriptName = "UpdateClimbingMovement"))
		void UpdateClimbingMovement(float DeltaTime);
	virtual void UpdateClimbingMovement_Implementation(float DeltaTime){} // Do nothing by default

	// This is the offset location of the player, use this for when checking against player transform instead of the actors transform
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
	FTransform OffsetComponentToWorld;

	// Gets the forward vector of the HMD offset capsule
	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
	FVector GetVRForwardVector() const
	{
		return OffsetComponentToWorld.GetRotation().GetForwardVector();
	}

	// Gets the right vector of the HMD offset capsule
	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
		FVector GetVRRightVector() const
	{
		return OffsetComponentToWorld.GetRotation().GetRightVector();
	}

	// Gets the upvector of the HMD offset capsule
	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
		FVector GetVRUpVector() const
	{
		return OffsetComponentToWorld.GetRotation().GetUpVector();
	}

	// Gets the location of the HMD offset capsule (this retains the Capsule HalfHeight offset)
	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
	FVector GetVRLocation() const
	{
		return OffsetComponentToWorld.GetLocation();
	}

	inline FVector GetVRLocation_Inline() const
	{
		return OffsetComponentToWorld.GetLocation();
	}

	// Gets the rotation of the HMD offset capsule
	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations")
		FRotator GetVRRotation() const
	{
		return OffsetComponentToWorld.GetRotation().Rotator();
	}
	// Gets the location of the HMD, if the camera is missing then it just returns waist location instead
	UFUNCTION(BlueprintPure, Category = "BaseVRCharacter|VRLocations", meta = (DisplayName = "GetVRHeadLocation", ScriptName = "GetVRHeadLocation", Keywords = "position"))
		FVector K2_GetVRHeadLocation() const
	{
		return GetVRHeadLocation();
	}

	inline FVector GetVRHeadLocation() const
	{
		return VRReplicatedCamera != nullptr ? VRReplicatedCamera->GetComponentLocation() : OffsetComponentToWorld.GetLocation();
	}


	virtual FVector GetTargetLocation(AActor* RequestedBy) const override
	{
		return GetVRLocation_Inline();
	}

	// If true will use the experimental method of unseating that clears some movement replication options.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRBaseCharacter")
		bool bUseExperimentalUnseatModeFix;

	UPROPERTY(BlueprintReadOnly, Replicated, EditAnywhere, Category = "Seating", ReplicatedUsing = OnRep_SeatedCharInfo)
	FVRSeatedCharacterInfo SeatInformation;

	// Called when the seated mode is changed
	UFUNCTION(BlueprintNativeEvent, Category = "Seating")
		void OnSeatedModeChanged(bool bNewSeatedMode, bool bWasAlreadySeated);
	virtual void OnSeatedModeChanged_Implementation(bool bNewSeatedMode, bool bWasAlreadySeated) {}

	// Called when the the player either transitions to/from the threshold boundry or the scaler value of being outside the boundry changes
	// Can be used for warnings or screen darkening, ect
	UFUNCTION(BlueprintNativeEvent, Category = "Seating")
		void OnSeatThreshholdChanged(bool bIsWithinThreshold, float ToThresholdScaler);
	virtual void OnSeatThreshholdChanged_Implementation(bool bIsWithinThreshold, float ToThresholdScaler) {}
	
	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "Seating")
		FVRSeatThresholdChangedSignature OnSeatThreshholdChanged_Bind;
	
	void ZeroToSeatInformation()
	{
		SetSeatRelativeLocationAndRotationVR(FVector::ZeroVector);
		NotifyOfTeleport();
		//LeftMotionController->PostTeleportMoveGrippedObjects();
		//RightMotionController->PostTeleportMoveGrippedObjects();
	}
	
	// Called from the movement component
	void TickSeatInformation(float DeltaTime);

	UFUNCTION()
		virtual void OnRep_SeatedCharInfo();

	void InitSeatedModeTransition();

	// Re-zeros the seated settings
	UFUNCTION(BlueprintCallable, Server, Reliable, WithValidation, Category = "BaseVRCharacter", meta = (DisplayName = "ReZeroSeating"))
		void Server_ReZeroSeating(FTransform_NetQuantize NewTargetTransform, FTransform_NetQuantize NewInitialRelCameraTransform, bool bZeroToHead = true);


	// Sets seated mode on the character and then fires off an event to handle any special setup
	// Target Transform is for teleport location if standing up, or relative camera location when sitting down.
	// InitialRelCameraTransform is generally the relative transform of the camera at the time of requesting to sit.
	// ZeroToHead places central point on head, if false it will use foot location and ignore Z values instead.
	// Post Seated movement mode is the movement mode to switch too after seating is canceled, defaults to Walking and only uses it when un-seating.
	UFUNCTION(BlueprintCallable, Server, Reliable, WithValidation, Category = "BaseVRCharacter", meta = (DisplayName = "SetSeatedMode"))
		void Server_SetSeatedMode(USceneComponent * SeatParent, bool bSetSeatedMode, FTransform_NetQuantize TargetTransform, FTransform_NetQuantize InitialRelCameraTransform, float AllowedRadius = 40.0f, float AllowedRadiusThreshold = 20.0f, bool bZeroToHead = true, EVRConjoinedMovementModes PostSeatedMovementMode = EVRConjoinedMovementModes::C_MOVE_Walking);

	// Sets seated mode on the character and then fires off an event to handle any special setup
	// Should only be called on the server / net authority
	// If allowed radius is 0.0f then the seated mode does not check for radial distance anymore.
	bool SetSeatedMode(USceneComponent * SeatParent, bool bSetSeatedMode, FTransform TargetTransform, FTransform InitialRelCameraTransform, float AllowedRadius = 40.0f, float AllowedRadiusThreshold = 20.0f, bool bZeroToHead = true, EVRConjoinedMovementModes PostSeatedMovementMode = EVRConjoinedMovementModes::C_MOVE_Walking);

	void SetSeatRelativeLocationAndRotationVR(FVector LocDelta);

	// Adds a rotation delta taking into account the HMD as a pivot point (also moves the actor), returns final location difference
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
		FVector AddActorWorldRotationVR(FRotator DeltaRot, bool bUseYawOnly = true);

	// Sets the actors rotation taking into account the HMD as a pivot point (also moves the actor), returns the location difference
	// bAccountForHMDRotation sets the rot to have the HMD face the given rot, if it is false it ignores the HMD rotation
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
		FVector SetActorRotationVR(FRotator NewRot, bool bUseYawOnly = true, bool bAccountForHMDRotation = true);
	
	// Sets the actors rotation and location taking into account the HMD as a pivot point (also moves the actor), returns the location difference from the rotation
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
		FVector SetActorLocationAndRotationVR(FVector NewLoc, FRotator NewRot, bool bUseYawOnly = true, bool bAccountForHMDRotation = true, bool bTeleport = false);

	// Regenerates the base offsetcomponenttoworld that VR uses
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
	virtual void RegenerateOffsetComponentToWorld(bool bUpdateBounds, bool bCalculatePureYaw)
	{}

	// This sets the capsules height, but also regenerates the offset transform instantly
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter")
		virtual void SetCharacterSizeVR(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps = true);

	// This sets the capsules half height, but also regenerates the offset transform instantly
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter")
		virtual void SetCharacterHalfHeightVR(float HalfHeight, bool bUpdateOverlaps = true);

	// This component is used with the normal character SkeletalMesh network smoothing system for simulated proxies
	// It will lerp the characters components back to zero on simulated proxies after a move is complete.
	// The simplest method of doing this was applying the exact same offset as the mesh gets to a base component that
	// tracked objects are attached to.
	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		USceneComponent * NetSmoother;

	// This is just a helper proxy component after the net smoother to make it easier to move tracking around for people
	// but still maintain the netsmoothers functionality
	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		USceneComponent* VRProxyComponent;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UVRBaseCharacterMovementComponent * VRMovementReference;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UReplicatedVRCameraComponent * VRReplicatedCamera;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UParentRelativeAttachmentComponent * ParentRelativeAttachment;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UGripMotionControllerComponent * LeftMotionController;

	UPROPERTY(Category = VRBaseCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UGripMotionControllerComponent * RightMotionController;

	/** Name of the LeftMotionController component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName LeftMotionControllerComponentName;

	/** Name of the RightMotionController component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName RightMotionControllerComponentName;

	/** Name of the VRReplicatedCamera component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName ReplicatedCameraComponentName;

	/** Name of the ParentRelativeAttachment component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName ParentRelativeAttachmentComponentName;
	
	/** Name of the net smoother component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName SmoothingSceneParentComponentName;

	/** Name of the vr proxy component. Use this name if you want to use a different class (with ObjectInitializer.SetDefaultSubobjectClass). */
	static FName VRProxyComponentName;

	/*
	A helper function that offsets a given vector by the roots collision location
	pass in a teleport location and it provides the correct spot for it to be at your feet
	*/
	UFUNCTION(BlueprintPure, Category = "VRGrip")
		virtual FVector GetTeleportLocation(FVector OriginalLocation);

	// Notifies that we should teleport the both hand grips on next tick
	// When called server side will automatically apply to remote clients as well.
	// Owning clients get it on server correction automatically already.
	UFUNCTION(BlueprintCallable, Category = "VRGrip")
		virtual void NotifyOfTeleport(bool bRegisterAsTeleport = true);


	// Event triggered when a move action is performed, this is ran just prior to PerformMovement in the character tick
	UFUNCTION(BlueprintNativeEvent, Category = "VRMovement")
		void OnCustomMoveActionPerformed(EVRMoveAction MoveActionType, FVector MoveActionVector, FRotator MoveActionRotator, uint8 MoveActionFlags);
	virtual void OnCustomMoveActionPerformed_Implementation(EVRMoveAction MoveActionType, FVector MoveActionVector, FRotator MoveActionRotator, uint8 MoveActionFlags);

	// Event triggered when beginning to be pushed back from a wall
	// bHadLocomotionInput means that the character was moving itself
	// HmdInput is how much the HMD moved in that tick so you can compare sizes to decide what to do
	UFUNCTION(BlueprintNativeEvent, Category = "VRMovement")
		void OnBeginWallPushback(FHitResult HitResultOfImpact, bool bHadLocomotionInput, FVector HmdInput);
	virtual void OnBeginWallPushback_Implementation(FHitResult HitResultOfImpact, bool bHadLocomotionInput, FVector HmdInput);

	// Event triggered when beginning to be pushed back from a wall
	UFUNCTION(BlueprintNativeEvent, Category = "VRMovement")
		void OnEndWallPushback();
	virtual void OnEndWallPushback_Implementation();

	// Event when a navigation pathing operation has completed, auto calls stop movement for VR characters
	UFUNCTION(BlueprintImplementableEvent, Category = "VRBaseCharacter|Navigation")
		void ReceiveNavigationMoveCompleted(EPathFollowingResult::Type PathingResult);

	virtual void NavigationMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
	{
		this->Controller->StopMovement();
		ReceiveNavigationMoveCompleted(Result.Code);
	}

	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter|Navigation")
	EPathFollowingStatus::Type GetMoveStatus() const
	{
		if (!Controller)
			return EPathFollowingStatus::Idle;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			pathComp->GetStatus();
		}

		return EPathFollowingStatus::Idle;
	}

	/** Returns true if the current PathFollowingComponent's path is partial (does not reach desired destination). */
	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter|Navigation")
	bool HasPartialPath() const
	{
		if (!Controller)
			return false;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			return pathComp->HasPartialPath();
		}

		return false;
	}

	// Instantly stops pathing
	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter|Navigation")
	void StopNavigationMovement()
	{
		if (!Controller)
			return;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			// @note FPathFollowingResultFlags::ForcedScript added to make AITask_MoveTo instances 
			// not ignore OnRequestFinished notify that's going to be sent out due to this call
			pathComp->AbortMove(*this, FPathFollowingResultFlags::MovementStop | FPathFollowingResultFlags::ForcedScript);
		}
	}

	UPROPERTY(BlueprintReadWrite, Category = AI)
		TSubclassOf<UNavigationQueryFilter> DefaultNavigationFilterClass;

	// An extended simple move to location with additional parameters
	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter|Navigation", Meta = (AdvancedDisplay = "bStopOnOverlap,bCanStrafe,bAllowPartialPath"))
		virtual void ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius = -1, bool bStopOnOverlap = false,
			bool bUsePathfinding = true, bool bProjectDestinationToNavigation = true, bool bCanStrafe = false,
			TSubclassOf<UNavigationQueryFilter> FilterClass = NULL, bool bAllowPartialPath = true);

	// Returns the current path points on the active navigation path
	// Will return false / an empty result if the path following component is not active yet or the path is empty
	UFUNCTION(BlueprintCallable, Category = "VRBaseCharacter|Navigation")
		bool GetCurrentNavigationPathPoints(TArray<FVector>& NavigationPointList);

};