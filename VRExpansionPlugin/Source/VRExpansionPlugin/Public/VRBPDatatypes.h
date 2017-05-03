// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "EngineMinimal.h"

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

// This makes a lot of the blueprint functions cleaner
UENUM()
enum class EBPVRResultSwitch : uint8
{
	// On Success
	OnSucceeded,
	// On Failure
	OnFailed
};

// Wasn't needed when final setup was realized
// Tracked device waist location
UENUM(Blueprintable)
enum class EBPVRWaistTrackingMode : uint8
{
	// Waist is tracked from the front
	VRWaist_Tracked_Front,
	// Waist is tracked from the rear
	VRWaist_Tracked_Rear,
	// Waist is tracked from the left (self perspective)
	VRWaist_Tracked_Left,
	// Waist is tracked from the right (self perspective)
	VRWaist_Tracked_Right
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPVRWaistTracking_Info
{
	GENERATED_BODY()
public:

	// Initial "Resting" location of the tracker parent, assumed to be the calibration zero
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
		FRotator RestingRotation;


	// Distance to offset to get center of waist from tracked parent location
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
		float WaistRadius;

	// Controls forward vector
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	EBPVRWaistTrackingMode TrackingMode;

	// Tracked parent reference
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
		UPrimitiveComponent * TrackedDevice;

	bool IsValid()
	{
		return TrackedDevice != nullptr;
	}

	void Clear()
	{
		TrackedDevice = nullptr;
	}

	FBPVRWaistTracking_Info()
	{
		WaistRadius = 0.0f;
		TrackedDevice = nullptr;
		TrackingMode = EBPVRWaistTrackingMode::VRWaist_Tracked_Rear;
	}

};

UENUM()
enum class EVRVectorQuantization : uint8
{
	/** Each vector component will be rounded, preserving one decimal place. */
	RoundOneDecimal = 0,
	/** Each vector component will be rounded, preserving two decimal places. */
	RoundTwoDecimals = 1
};

USTRUCT()
struct VREXPANSIONPLUGIN_API FBPVRComponentPosRep
{
	GENERATED_USTRUCT_BODY()
public:

	UPROPERTY(Transient)
		FVector Position;
	UPROPERTY(Transient)
		FRotator Rotation;

	UPROPERTY(EditDefaultsOnly, Category = Replication, AdvancedDisplay)
		EVRVectorQuantization QuantizationLevel;

	FBPVRComponentPosRep()
	{
		QuantizationLevel = EVRVectorQuantization::RoundTwoDecimals;
	}

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		// Defines the level of Quantization
		uint8 Flags = (uint8)QuantizationLevel;
		Ar.SerializeBits(&Flags, 1);
		
		// No longer using their built in rotation rep, as controllers will rarely if ever be at 0 rot on an axis and 
		// so the 1 bit overhead per axis is just that, overhead
		//Rotation.SerializeCompressedShort(Ar);

		uint16 ShortPitch = 0;
		uint16 ShortYaw = 0;
		uint16 ShortRoll = 0;
		
		if (Ar.IsSaving())
		{		
			switch (QuantizationLevel)
			{
			case EVRVectorQuantization::RoundTwoDecimals: bOutSuccess &= SerializePackedVector<100, 30>(Position, Ar); break;
			case EVRVectorQuantization::RoundOneDecimal: bOutSuccess &= SerializePackedVector<10, 24>(Position, Ar); break;
			}

			ShortPitch = FRotator::CompressAxisToShort(Rotation.Pitch);
			ShortYaw = FRotator::CompressAxisToShort(Rotation.Yaw);
			ShortRoll = FRotator::CompressAxisToShort(Rotation.Roll);

			Ar << ShortPitch;
			Ar << ShortYaw;
			Ar << ShortRoll;
		}
		else // If loading
		{
			QuantizationLevel = (EVRVectorQuantization)Flags;

			switch (QuantizationLevel)
			{
			case EVRVectorQuantization::RoundTwoDecimals: bOutSuccess &= SerializePackedVector<100, 30>(Position, Ar); break;
			case EVRVectorQuantization::RoundOneDecimal: bOutSuccess &= SerializePackedVector<10, 24>(Position, Ar); break;
			}

			Ar << ShortPitch;
			Ar << ShortYaw;
			Ar << ShortRoll;
			
			Rotation.Pitch = FRotator::DecompressAxisFromShort(ShortPitch);
			Rotation.Yaw = FRotator::DecompressAxisFromShort(ShortYaw);
			Rotation.Roll = FRotator::DecompressAxisFromShort(ShortRoll);
		}

		return bOutSuccess;
	}

};

template<>
struct TStructOpsTypeTraits< FBPVRComponentPosRep > : public TStructOpsTypeTraitsBase2<FBPVRComponentPosRep>
{
	enum
	{
		WithNetSerializer = true
	};
};

/*
Interactive Collision With Physics = Held items can be offset by geometry, uses physics for the offset, pushes physics simulating objects with weight taken into account
Interactive Collision With Sweep = Held items can be offset by geometry, uses sweep for the offset, pushes physics simulating objects, no weight
Sweep With Physics = Only sweeps movement, will not be offset by geomtry, still pushes physics simulating objects, no weight
Physics Only = Does not sweep at all (does not trigger OnHitEvents), still pushes physics simulating objects, no weight
Manipulation grip = free constraint to controller base, no rotational drives
ManipulationGripWithWristTwise = free constraint to controller base with a twist drive
Custom grip is to be handled by the object itself, it just sends the TickGrip event every frame but doesn't move the object.
InteractiveHybridCollisionWithPhysics = Uses Stiffness and damping settings on collision, on no collision uses stiffness values 10x stronger so it has less play.
*/
UENUM(Blueprintable)
enum class EGripCollisionType : uint8
{
	InteractiveCollisionWithPhysics,
//	InteractiveCollisionWithVelocity,
	InteractiveCollisionWithSweep,
	InteractiveHybridCollisionWithPhysics,
	InteractiveHybridCollisionWithSweep,
	SweepWithPhysics,
	PhysicsOnly,
	ManipulationGrip,
	ManipulationGripWithWristTwist,
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
	DT_GoogleVR,
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

// Grip Late Update information
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
// LocalOnly_Not_Replicated is useful for instant client grips 
// that can be sent to the server and everyone locally grips it (IE: inventories that don't ever leave a player)
// Objects that need to be handled possibly by multiple players should be ran
// non locally gripped instead so that the server can validate grips instead.
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
		bOutSuccess = true;

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
		
		// Might need to change this

		//Ar << InitialLinearTranslation;
		//Ar << MinLinearTranslation;
		//Ar << MaxLinearTranslation;
		bOutSuccess &= SerializePackedVector<100, 30>(InitialLinearTranslation, Ar);
		bOutSuccess &= SerializePackedVector<100, 30>(MinLinearTranslation, Ar);
		bOutSuccess &= SerializePackedVector<100, 30>(MaxLinearTranslation, Ar);

		InitialAngularTranslation.SerializeCompressedShort(Ar);
		MinAngularTranslation.SerializeCompressedShort(Ar);
		MaxAngularTranslation.SerializeCompressedShort(Ar);
		//Ar << InitialAngularTranslation;
		//Ar << MinAngularTranslation;
		//Ar << MaxAngularTranslation;


		return bOutSuccess;
	}

};

template<>
struct TStructOpsTypeTraits< FBPInteractionSettings > : public TStructOpsTypeTraitsBase2<FBPInteractionSettings>
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
	// #TODO serialize this more efficiently over the network
	UPROPERTY(BlueprintReadOnly)
		EGripTargetType GripTargetType;
	UPROPERTY(BlueprintReadOnly)
		UObject * GrippedObject;
	UPROPERTY(BlueprintReadOnly)
		EGripCollisionType GripCollisionType;
	UPROPERTY(BlueprintReadWrite)
		EGripLateUpdateSettings GripLateUpdateSetting;
	//UPROPERTY(BlueprintReadOnly) // If this was a UProperty it would trigger replication when changed
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
	// Max value is 16 seconds with two decimal precision, this is to reduce replication overhead
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
		//#TODO Start compressing Transforms / Vectors / Floats?

		bOutSuccess = true;



		Ar << GrippedObject;
		Ar << RelativeTransform;

		// #TODO
		// In the future I can compress these down to half a byte each, none go over 4 bits of data
		// Its only a 2 byte savings on something that rarely replicates so not worried about it at the moment.
		Ar << GripTargetType;
		Ar << GripCollisionType;
		Ar << GripLateUpdateSetting;
		Ar << GripMovementReplicationSetting;

		// Doesn't matter to client
		//	Ar << bColliding;

		// This doesn't matter to clients
		//Ar << bOriginalReplicatesMovement;
		
		bool bHadAttachment = bHasSecondaryAttachment;
	
		//Ar << bHasSecondaryAttachment;
		uint8 Flags = bHasSecondaryAttachment;
		Ar.SerializeBits(&Flags, 1);
		bHasSecondaryAttachment = (Flags >> 0) & 0x01;	// The compiler should optimize out bitshifts by 0, so keeping it for clarity
		
		//Ar << LerpToRate;
		if(Ar.IsSaving())
		{ 
			bOutSuccess &= WriteFixedCompressedFloat<16, 12>(LerpToRate, Ar); // 4 bits up to 16 seconds lerp time, 1 bit sign, 7 bits 2 decimal precision
		}
		else
			bOutSuccess &= ReadFixedCompressedFloat<16, 12>(LerpToRate, Ar);


		// If this grip has a secondary attachment
		if (bHasSecondaryAttachment)
		{
			Ar << SecondaryAttachment;
			Ar << SecondaryRelativeLocation;

			//Ar << SecondarySmoothingScaler;
			if (Ar.IsSaving())
			{
				bOutSuccess &= WriteFixedCompressedFloat<1, 9>(SecondarySmoothingScaler, Ar); // 1 bits up to 1 value, 1 bit sign, 7 bits 2 decimal precision
			}
			else
				bOutSuccess &= ReadFixedCompressedFloat<1, 9>(SecondarySmoothingScaler, Ar);

		}

		// Manage lerp states
		if (Ar.IsLoading())
		{
			if (bHadAttachment != bHasSecondaryAttachment)
			{
				if (LerpToRate < 0.01f) // Zero, could use IsNearlyZero instead
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

		return bOutSuccess;
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

		RelativeTransform = FTransform::Identity;
		AdditionTransform = FTransform::Identity;
	}
};

template<>
struct TStructOpsTypeTraits< FBPActorGripInformation > : public TStructOpsTypeTraitsBase2<FBPActorGripInformation>
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