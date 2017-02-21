// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Engine.h"

#include "PhysicsPublic.h"
#if WITH_PHYSX
#include "PhysXPublic.h"
#include "PhysXSupport.h"
#endif // WITH_PHYSX

#include "VRBPDatatypes.generated.h"

class UGripMotionControllerComponent;


// Custom movement modes for the characters
UENUM(BlueprintType)
enum class EVRCustomMovementMode : uint8
{
	VRMOVE_Climbing UMETA(DisplayName = "Climbing")
};


// Redefined here so that non windows packages can compile
/** Defines the class of tracked devices in SteamVR*/
UENUM(BlueprintType)
enum class EBPSteamVRTrackedDeviceType : uint8
{
	/** Represents a Steam VR Controller */
	Controller,

	/** Represents a static tracking reference device, such as a Lighthouse or tracking camera */
	TrackingReference,

	/** Misc. device types, for future expansion */
	Other,

	/** DeviceId is invalid */
	Invalid
};


// This makes a lot of the blueprint functions cleaner
UENUM()
enum class EBPVRResultSwitch : uint8
{
	// On Success
	OnSucceeded,
	// On Failure
	OnFailed
};


USTRUCT()
struct VREXPANSIONPLUGIN_API FBPVRComponentPosRep
{
	GENERATED_BODY()
public:
	UPROPERTY()
		FVector_NetQuantize100 Position;
	UPROPERTY()
		uint32 YawPitchINT;
	UPROPERTY()
		uint16 RollSHORT;

	// Removed roll BYTE, it was too inaccurate, using a short now
	//FRotator Orientation;

	// This removes processing time from lerping
	FRotator UnpackedRotation;
	FVector UnpackedLocation;

	FORCEINLINE void Unpack()
	{
		UnpackedLocation = (FVector)Position;
		UnpackedRotation = GetRotation();
	}

	FORCEINLINE void SetRotation(FRotator NewRot)
	{
		//Orientation = NewRot;
		YawPitchINT = (FRotator::CompressAxisToShort(NewRot.Yaw) << 16) | FRotator::CompressAxisToShort(NewRot.Pitch);
		RollSHORT = FRotator::CompressAxisToShort(NewRot.Roll);
	}

	FORCEINLINE FRotator GetRotation()
	{
		//return Orientation;
		const uint16 nPitch = (YawPitchINT & 65535);
		const uint16 nYaw = (YawPitchINT >> 16);

		return FRotator(FRotator::DecompressAxisFromShort(nPitch), FRotator::DecompressAxisFromShort(nYaw), FRotator::DecompressAxisFromShort(RollSHORT)/*DecompressAxisFromByte(RollBYTE)*/);
	}
};

/*
Interactive Collision With Physics = Held items can be offset by geometry, uses physics for the offset, pushes physics simulating objects with weight taken into account
Interactive Collision With Sweep = Held items can be offset by geometry, uses sweep for the offset, pushes physics simulating objects, no weight
Sweep With Physics = Only sweeps movement, will not be offset by geomtry, still pushes physics simulating objects, no weight
Physics Only = Does not sweep at all (does not trigger OnHitEvents), still pushes physics simulating objects, no weight
Custom grip is to be handled by the object itself, it just sends the TickGrip event every frame but doesn't move the object.
*/
UENUM(Blueprintable)
enum class EGripCollisionType : uint8
{
	InteractiveCollisionWithPhysics,
//	InteractiveCollisionWithVelocity,
	InteractiveCollisionWithSweep,
	InteractiveHybridCollisionWithSweep,
	SweepWithPhysics,
	PhysicsOnly,
	ManipulationGrip,
	CustomGrip
};

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum class EBPHMDDeviceType : uint8
{
	DT_OculusRift,
	DT_Morpheus,
	DT_ES2GenericStereoMesh,
	DT_SteamVR,
	DT_GearVR,
	DT_Unknown
};

// Lerp states
UENUM(Blueprintable)
enum class EGripLerpState : uint8
{
	StartLerp,
	EndLerp,
	ConstantLerp,
	NotLerping
};

// Grip Late Update informaiton
UENUM(Blueprintable)
enum class EGripLateUpdateSettings : uint8
{
	LateUpdatesAlwaysOn,
	LateUpdatesAlwaysOff,
	NotWhenColliding,
	NotWhenDoubleGripping,
	NotWhenCollidingOrDoubleGripping
};

// Grip movement replication settings
// ServerSideMovementOnlyWhenColliding is not InteractivePhysicsGripCompatible
UENUM(Blueprintable)
enum class EGripMovementReplicationSettings : uint8
{
	KeepOriginalMovement,
	ForceServerSideMovement,
	ForceClientSideMovement,
	LocalOnly_Not_Replicated
};

// Grip Target Type
UENUM(Blueprintable)
enum class EGripTargetType : uint8
{
	ActorGrip,
	ComponentGrip
	//InteractibleActorGrip,
	//InteractibleComponentGrip
};

// Lerp states
UENUM(Blueprintable)
enum class EGripInterfaceTeleportBehavior : uint8
{
	TeleportAllComponents,
	OnlyTeleportRootComponent,
	DropOnTeleport,
	DontTeleport
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPInteractionSettings
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
		uint32 bLimitsInLocalSpace:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		uint32 bLimitX:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		uint32 bLimitY:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		uint32 bLimitZ:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		uint32 bLimitPitch:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		uint32 bLimitYaw:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		uint32 bLimitRoll:1;

	// Doesn't work totally correctly without using the ConvertToControllerRelativeTransform node in the motion controller
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		uint32 bIgnoreHandRotation:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		FVector InitialLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		FVector MinLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		FVector MaxLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		FRotator InitialAngularTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		FRotator MinAngularTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		FRotator MaxAngularTranslation;

	FBPInteractionSettings()
	{
		bLimitsInLocalSpace = true;

		bLimitX = false;
		bLimitY = false;
		bLimitZ = false;

		bLimitPitch = false;
		bLimitYaw = false;
		bLimitRoll = false;

		bIgnoreHandRotation = false;

		InitialLinearTranslation = FVector::ZeroVector;
		MinLinearTranslation = FVector::ZeroVector;
		MaxLinearTranslation = FVector::ZeroVector;

		InitialAngularTranslation = FRotator::ZeroRotator;
		MinAngularTranslation = FRotator::ZeroRotator;
		MaxAngularTranslation = FRotator::ZeroRotator;
	}

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		// pack bitfield with flags
		uint8 Flags;
		if (Ar.IsSaving())
		{
			Flags =
				(bLimitsInLocalSpace << 0) |
				(bLimitX << 1) |
				(bLimitY << 2) |
				(bLimitZ << 3) |
				(bLimitPitch << 4) |
				(bLimitYaw << 5) |
				(bLimitRoll << 6) |
				(bIgnoreHandRotation << 7);
		}

		Ar.SerializeBits(&Flags, 8);

		if (Ar.IsLoading())
		{
			// We should technically be safe just bit shifting
			// to the right as these are :1 bitfield values.
			// however to be totally sure I am enforcing only first
			// bit being set with the &0x01
			// I didn't like epics ternary version as it should technically have more overhead
			// But what do I know
			bLimitsInLocalSpace = (Flags >> 0) & 0x01;
			bLimitX = (Flags >> 1) & 0x01;
			bLimitY = (Flags >> 2) & 0x01;
			bLimitZ = (Flags >> 3) & 0x01;
			bLimitPitch = (Flags >> 4) & 0x01;
			bLimitYaw = (Flags >> 5) & 0x01;
			bLimitRoll = (Flags >> 6) & 0x01;	
			bIgnoreHandRotation = (Flags >> 7) & 0x01;
		}

		// Shouldn't need full precision for these, but letting them rep as it anyway to not cause issues if someone ever wants it
		//bOutSuccess &= SerializeQuantizedVector(Ar, InitialLinearTranslation, EVectorQuantization::RoundTwoDecimals);
		//bOutSuccess &= SerializeQuantizedVector(Ar, MinLinearTranslation, EVectorQuantization::RoundTwoDecimals);
		//bOutSuccess &= SerializeQuantizedVector(Ar, MaxLinearTranslation, EVectorQuantization::RoundTwoDecimals);

		Ar << InitialLinearTranslation;
		Ar << MinLinearTranslation;
		Ar << MaxLinearTranslation;
		Ar << InitialAngularTranslation;
		Ar << MinAngularTranslation;
		Ar << MaxAngularTranslation;

		bOutSuccess = true;
		return true;
	}

};

template<>
struct TStructOpsTypeTraits< FBPInteractionSettings > : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithNetSerializer = true
	};
};


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPActorGripInformation
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadOnly)
		EGripTargetType GripTargetType;
	UPROPERTY(BlueprintReadOnly)
		UObject * GrippedObject;
	UPROPERTY(BlueprintReadOnly)
		EGripCollisionType GripCollisionType;
	UPROPERTY(BlueprintReadWrite)
		EGripLateUpdateSettings GripLateUpdateSetting;
	//UPROPERTY(BlueprintReadOnly)
		bool bColliding;
	UPROPERTY(BlueprintReadWrite)
		FTransform RelativeTransform;

	UPROPERTY(BlueprintReadOnly)
		EGripMovementReplicationSettings GripMovementReplicationSetting;
	UPROPERTY(BlueprintReadOnly)
		bool bOriginalReplicatesMovement;

	UPROPERTY()
		float Damping;
	UPROPERTY()
		float Stiffness;

	// For multi grip situations
	UPROPERTY(BlueprintReadOnly)
		bool bHasSecondaryAttachment;
	UPROPERTY(BlueprintReadOnly)
		USceneComponent * SecondaryAttachment;
	UPROPERTY(BlueprintReadWrite)
		float SecondarySmoothingScaler;
	UPROPERTY()
		FVector SecondaryRelativeLocation;

	// Lerp transitions
	UPROPERTY()
		float LerpToRate;
	
	// These are not replicated, they don't need to be
	EGripLerpState GripLerpState;
	FVector LastRelativeLocation;
	float curLerp;

	// Optional Additive Transform for programatic animation
	FTransform AdditionTransform;

	// Locked transitions
	bool bIsLocked;
	FQuat LastLockedRotation;

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		Ar << GripTargetType;
		Ar << GrippedObject;
		Ar << GripCollisionType;
		Ar << GripLateUpdateSetting;

		Ar << RelativeTransform;

		Ar << GripMovementReplicationSetting;

		// If on colliding server, otherwise doesn't matter to client
		//	Ar << bColliding;

		// This doesn't matter to clients
		//Ar << bOriginalReplicatesMovement;
		
		bool bHadAttachment = bHasSecondaryAttachment;
	
		Ar << bHasSecondaryAttachment;
		Ar << LerpToRate;

		// If this grip has a secondary attachment
		if (bHasSecondaryAttachment)
		{
			Ar << SecondaryAttachment;
			Ar << SecondaryRelativeLocation;
			Ar << SecondarySmoothingScaler;
		}

		// Manage lerp states
		if (Ar.IsLoading())
		{
			if (bHadAttachment != bHasSecondaryAttachment)
			{
				if (LerpToRate < 0.01f)
					GripLerpState = EGripLerpState::NotLerping;
				else
				{
					// New lerp
					if (bHasSecondaryAttachment)
					{
						curLerp = LerpToRate;
						GripLerpState = EGripLerpState::StartLerp;
					}
					else // Post Lerp
					{
						curLerp = LerpToRate;
						GripLerpState = EGripLerpState::EndLerp;
					}
				}
			}
		}

		// Now always replicating these two, in case people want to pass in custom values using it
		Ar << Damping;
		Ar << Stiffness;

		bOutSuccess = true;
		return true;
	}

	FORCEINLINE AActor * GetGrippedActor() const
	{
		return Cast<AActor>(GrippedObject);
	}

	FORCEINLINE UPrimitiveComponent * GetGrippedComponent() const
	{
		return Cast<UPrimitiveComponent>(GrippedObject);
	}

	//Check if a grip is the same as another, the only things I check for are the actor / component
	//This is here for the Find() function from TArray
	FORCEINLINE bool operator==(const FBPActorGripInformation &Other) const
	{
		if (GrippedObject && GrippedObject == Other.GrippedObject)
			return true;

		return false;
	}

	FORCEINLINE bool operator==(const AActor * Other) const
	{
		if (Other && GrippedObject && GrippedObject == Other)
			return true;

		return false;
	}

	FORCEINLINE bool operator==(const UPrimitiveComponent * Other) const
	{
		if (Other && GrippedObject && GrippedObject == Other)
			return true;

		return false;
	}


	FORCEINLINE bool operator==(const UObject * Other) const
	{
		if (Other && GrippedObject == Other)
			return true;

		return false;
	}

	FBPActorGripInformation()
	{
		GripTargetType = EGripTargetType::ActorGrip;
		Damping = 200.0f;
		Stiffness = 1500.0f;
		GrippedObject = nullptr;
		bColliding = false;
		GripCollisionType = EGripCollisionType::InteractiveCollisionWithSweep;
		GripLateUpdateSetting = EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping;
		bIsLocked = false;
		curLerp = 0.0f;
		LerpToRate = 0.0f;
		GripLerpState = EGripLerpState::NotLerping;
		SecondarySmoothingScaler = 1.0f;

		SecondaryAttachment = nullptr;
		bHasSecondaryAttachment = false;

		AdditionTransform = FTransform::Identity;
	}
};

template<>
struct TStructOpsTypeTraits< FBPActorGripInformation > : public TStructOpsTypeTraitsBase
{
	enum
	{
		WithNetSerializer = true
	};
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPInterfaceProperties
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bDenyGripping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripInterfaceTeleportBehavior OnTeleportBehavior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bSimulateOnDrop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		uint8 EnumObjectType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripCollisionType SlotDefaultGripType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripCollisionType FreeDefaultGripType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bCanHaveDoubleGrip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripMovementReplicationSettings MovementReplicationType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripLateUpdateSettings LateUpdateSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float ConstraintStiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float ConstraintDamping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float ConstraintBreakDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float SecondarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float PrimarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bIsInteractible;

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
		bool bIsHeld;

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
		UGripMotionControllerComponent * HoldingController;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		FBPInteractionSettings InteractionSettings;

	FBPInterfaceProperties()
	{
		bDenyGripping = false;
		OnTeleportBehavior = EGripInterfaceTeleportBehavior::DropOnTeleport;
		bSimulateOnDrop = true;
		EnumObjectType = 0;
		SlotDefaultGripType = EGripCollisionType::ManipulationGrip;
		FreeDefaultGripType = EGripCollisionType::ManipulationGrip;
		bCanHaveDoubleGrip = false;
		//GripTarget = EGripTargetType::ComponentGrip;
		MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
		LateUpdateSetting = EGripLateUpdateSettings::LateUpdatesAlwaysOff;
		ConstraintStiffness = 1500.0f;
		ConstraintDamping = 200.0f;
		ConstraintBreakDistance = 0.0f;
		SecondarySlotRange = 20.0f;
		PrimarySlotRange = 20.0f;
		bIsInteractible = false;

		bIsHeld = false;
		HoldingController = nullptr;
	}
};


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPActorPhysicsHandleInformation
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly)
		UObject * HandledObject;

	/** Physics scene index of the body we are grabbing. */
	int32 SceneIndex;
	/** Pointer to PhysX joint used by the handle*/
	physx::PxD6Joint* HandleData;
	/** Pointer to kinematic actor jointed to grabbed object */
	physx::PxRigidDynamic* KinActorData;

	physx::PxTransform COMPosition;

	FBPActorPhysicsHandleInformation()
	{
		HandleData = NULL;
		KinActorData = NULL;		
		HandledObject = nullptr;
		//Actor = nullptr;
		//Component = nullptr;
		COMPosition = physx::PxTransform(U2PVector(FVector::ZeroVector));
	}

	FORCEINLINE bool operator==(const FBPActorGripInformation & Other) const
	{
		if (HandledObject && HandledObject == Other.GrippedObject)
			return true;

		return false;
	}

};