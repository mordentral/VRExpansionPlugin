// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Engine.h"

#include "VRBPDatatypes.generated.h"


// Temp until full conversion past 4.13, breaks 4.14 / 4.13 cross conversion
UENUM(BlueprintType)
enum class EBPTrackingStatus : uint8 //  ETrackingStatus
{
	NotTracked,
	InertialOnly,
	Tracked
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
namespace EBPVRResultSwitch
{
	enum Type
	{
		// On Success
		OnSucceeded,

		// On Failure
		OnFailed
	};
}


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
		uint8 RollBYTE;
//		FRotator Orientation;


	FORCEINLINE void SetRotation(FRotator NewRot)
	{
		YawPitchINT = (FRotator::CompressAxisToShort(NewRot.Yaw) << 16) | FRotator::CompressAxisToShort(NewRot.Pitch);
		RollBYTE = FRotator::CompressAxisToByte(NewRot.Roll);
	}

	FORCEINLINE FRotator GetRotation()
	{
		const uint16 nPitch = (YawPitchINT & 65535);
		const uint16 nYaw = (YawPitchINT >> 16);

		return FRotator(FRotator::DecompressAxisFromShort(nPitch), FRotator::DecompressAxisFromShort(nYaw), FRotator::DecompressAxisFromByte(RollBYTE));
	}
};

/*
Interactive Collision With Physics = Held items can be offset by geometry, uses physics for the offset, pushes physics simulating objects with weight taken into account
Interactive Collision With Sweep = Held items can be offset by geometry, uses sweep for the offset, pushes physics simulating objects, no weight
Sweep With Physics = Only sweeps movement, will not be offset by geomtry, still pushes physics simulating objects, no weight
Physics Only = Does not sweep at all (does not trigger OnHitEvents), still pushes physics simulating objects, no weight
*/
UENUM(Blueprintable)
enum EGripCollisionType
{
	InteractiveCollisionWithPhysics,
	InteractiveCollisionWithVelocity,
	InteractiveCollisionWithSweep,
	InteractiveHybridCollisionWithSweep,
	SweepWithPhysics,
	PhysicsOnly,
	ManipulationGrip
};

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum EBPHMDDeviceType
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
enum EGripLerpState
{
	StartLerp,
	EndLerp,
	ConstantLerp,
	NotLerping
};

// Grip Late Update informaiton
UENUM(Blueprintable)
enum EGripLateUpdateSettings
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
enum EGripMovementReplicationSettings
{
	KeepOriginalMovement,
	ForceServerSideMovement,
	ForceClientSideMovement
};

// Grip Target Type
UENUM(Blueprintable)
enum EGripTargetType
{
	ActorGrip,
	ComponentGrip,
	InteractibleActorGrip,
	InteractibleComponentGrip
};

// Lerp states
UENUM(Blueprintable)
enum EGripInterfaceTeleportBehavior
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
		bool bCanUse;

	UPROPERTY(BlueprintReadWrite, EditAnywhere)
		bool bLimitsInLocalSpace;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		bool bLimitX;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		bool bLimitY;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "LinearSettings")
		bool bLimitZ;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		bool bLimitPitch;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		bool bLimitYaw;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		bool bLimitRoll;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		bool bRotateLeverToFaceController;

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
		bCanUse = false;
		bLimitsInLocalSpace = true;

		bLimitX = false;
		bLimitY = false;
		bLimitZ = false;

		bLimitPitch = false;
		bLimitYaw = false;
		bLimitRoll = false;

		bRotateLeverToFaceController = false;

		InitialLinearTranslation = FVector::ZeroVector;
		MinLinearTranslation = FVector::ZeroVector;
		MaxLinearTranslation = FVector::ZeroVector;

		InitialAngularTranslation = FRotator::ZeroRotator;
		MinAngularTranslation = FRotator::ZeroRotator;
		MaxAngularTranslation = FRotator::ZeroRotator;
	}

};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPActorGripInformation
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadOnly)
		TEnumAsByte<EGripTargetType> GripTargetType;
	UPROPERTY(BlueprintReadOnly)
		AActor * Actor;
	UPROPERTY(BlueprintReadOnly)
		UPrimitiveComponent * Component;
	UPROPERTY(BlueprintReadOnly)
		TEnumAsByte<EGripCollisionType> GripCollisionType;
	UPROPERTY(BlueprintReadWrite)
		TEnumAsByte<EGripLateUpdateSettings> GripLateUpdateSetting;
	//UPROPERTY(BlueprintReadOnly)
		bool bColliding;
	UPROPERTY(BlueprintReadWrite)
		FTransform RelativeTransform;

	UPROPERTY(BlueprintReadOnly)
		TEnumAsByte<EGripMovementReplicationSettings> GripMovementReplicationSetting;
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
	TEnumAsByte<EGripLerpState> GripLerpState;
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
		Ar << Actor;
		Ar << Component;
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

		// Don't bother replicated physics grip types if the grip type doesn't support it.
		if (GripCollisionType == EGripCollisionType::InteractiveCollisionWithPhysics || GripCollisionType == EGripCollisionType::InteractiveHybridCollisionWithSweep || GripCollisionType == EGripCollisionType::InteractiveCollisionWithVelocity)
		{
			Ar << Damping;
			Ar << Stiffness;
		}

		bOutSuccess = true;
		return true;
	}

	//Check if a grip is the same as another, the only things I check for are the actor / component
	//This is here for the Find() function from TArray
	FORCEINLINE bool operator==(const FBPActorGripInformation &Other) const
	{
		if (Actor && Actor == Other.Actor)
			return true;

		if (Component && Component == Other.Component)
			return true;

		return false;
	}

	FORCEINLINE bool operator==(const AActor * Other) const
	{
		if (Other && Actor && Actor == Other)
			return true;

		return false;
	}

	FORCEINLINE bool operator==(const UPrimitiveComponent * Other) const
	{
		if (Other && Component && Component == Other)
			return true;

		return false;
	}

	FBPActorGripInformation()
	{
		GripTargetType = EGripTargetType::ActorGrip;
		Damping = 200.0f;
		Stiffness = 1500.0f;
		Component = nullptr;
		Actor = nullptr;
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
		TEnumAsByte<EGripInterfaceTeleportBehavior> OnTeleportBehavior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bSimulateOnDrop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		uint8 EnumObjectType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		TEnumAsByte<EGripCollisionType> SlotDefaultGripType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		TEnumAsByte<EGripCollisionType> FreeDefaultGripType;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bCanHaveDoubleGrip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		TEnumAsByte<EGripTargetType> GripTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		TEnumAsByte<EGripMovementReplicationSettings> MovementReplicationType;

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
		GripTarget = EGripTargetType::ComponentGrip;
		MovementReplicationType = EGripMovementReplicationSettings::ForceClientSideMovement;
		ConstraintStiffness = 1500.0f;
		ConstraintDamping = 200.0f;
		ConstraintBreakDistance = 100.0f;
		SecondarySlotRange = 20.0f;
		PrimarySlotRange = 20.0f;
		bIsInteractible = false;
	}
};


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPActorPhysicsHandleInformation
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly)
		AActor * Actor;
	UPROPERTY(BlueprintReadOnly)
		UPrimitiveComponent * Component;

	/** Physics scene index of the body we are grabbing. */
	int32 SceneIndex;
	/** Pointer to PhysX joint used by the handle*/
	physx::PxD6Joint* HandleData;
	/** Pointer to kinematic actor jointed to grabbed object */
	physx::PxRigidDynamic* KinActorData;

	FBPActorPhysicsHandleInformation()
	{
		HandleData = NULL;
		KinActorData = NULL;
		Actor = nullptr;
		Component = nullptr;
	}

	FORCEINLINE bool operator==(const FBPActorGripInformation & Other) const
	{
		if (Actor && Actor == Other.Actor || Component && Component == Other.Component)
			return true;

		return false;
	}

};