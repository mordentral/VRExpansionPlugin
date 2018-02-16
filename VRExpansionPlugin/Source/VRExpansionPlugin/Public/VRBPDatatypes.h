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

UENUM(Blueprintable)
enum class EVRInteractibleAxis : uint8
{
	Axis_X,
	Axis_Y,
	Axis_Z
};


// Custom movement modes for the characters
UENUM(BlueprintType)
enum class EVRCustomMovementMode : uint8
{
	VRMOVE_Climbing UMETA(DisplayName = "Climbing"),
	VRMOVE_LowGrav  UMETA(DisplayName = "LowGrav"),
	VRMOVE_Seated UMETA(DisplayName = "Seated")
//	VRMove_Spider UMETA(DisplayName = "Spider")
};

// We use 4 bits for this so a maximum of 16 elements
UENUM(BlueprintType)
enum class EVRConjoinedMovementModes : uint8
{
	C_MOVE_None	= 0x00	UMETA(DisplayName = "None"),
	C_MOVE_Walking = 0x01	UMETA(DisplayName = "Walking"),
	C_MOVE_NavWalking = 0x02	UMETA(DisplayName = "Navmesh Walking"),
	C_MOVE_Falling = 0x03	UMETA(DisplayName = "Falling"),
	C_MOVE_Swimming = 0x04	UMETA(DisplayName = "Swimming"),
	C_MOVE_Flying = 0x05		UMETA(DisplayName = "Flying"),
	//C_MOVE_Custom = 0x06	UMETA(DisplayName = "Custom"), // Skip this, could technically get a Custom7 out of using this slot but who needs 7?
	C_MOVE_MAX = 0x07		UMETA(Hidden),
	C_VRMOVE_Climbing = 0x08 UMETA(DisplayName = "Climbing"),
	C_VRMOVE_LowGrav = 0x09 UMETA(DisplayName = "LowGrav"),
	//C_VRMOVE_Spider = 0x0A UMETA(DisplayName = "Spider"),
	C_VRMOVE_Seated = 0x0A UMETA(DisplayName = "Seated"),
	C_VRMOVE_Custom1 = 0x0B UMETA(DisplayName = "Custom1"),
	C_VRMOVE_Custom2 = 0x0C UMETA(DisplayName = "Custom2"),
	C_VRMOVE_Custom3 = 0x0D UMETA(DisplayName = "Custom3"),
	C_VRMOVE_Custom4 = 0x0E UMETA(DisplayName = "Custom4"),
	C_VRMOVE_Custom5 = 0x0F UMETA(DisplayName = "Custom5")
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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		FRotator RestingRotation;

	// Distance to offset to get center of waist from tracked parent location
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		float WaistRadius;

	// Controls forward vector
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
	EBPVRWaistTrackingMode TrackingMode;

	// Tracked parent reference
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		UPrimitiveComponent * TrackedDevice;

	bool IsValid()
	{
		return TrackedDevice != nullptr;
	}

	void Clear()
	{
		TrackedDevice = nullptr;
	}

	FBPVRWaistTracking_Info():
		RestingRotation(FRotator::ZeroRotator),
		WaistRadius(0.0f),
		TrackingMode(EBPVRWaistTrackingMode::VRWaist_Tracked_Rear),
		TrackedDevice(nullptr)
	{}

};

class FBasicLowPassFilter
{
public:

	/** Default constructor */
	FBasicLowPassFilter() :
		Previous(FVector::ZeroVector),
		bFirstTime(true)
	{}

	/** Calculate */
	FVector Filter(const FVector& InValue, const FVector& InAlpha)
	{
		FVector Result = InValue;
		if (!bFirstTime)
		{
			for (int i = 0; i < 3; i++)
			{
				Result[i] = InAlpha[i] * InValue[i] + (1 - InAlpha[i]) * Previous[i];
			}
		}

		bFirstTime = false;
		Previous = Result;
		return Result;
	}

	/** The previous filtered value */
	FVector Previous;

	/** If this is the first time doing a filter */
	bool bFirstTime;
};


/************************************************************************/
/* 1 Euro filter smoothing algorithm									*/
/* http://cristal.univ-lille.fr/~casiez/1euro/							*/
/************************************************************************/
// A re-implementation of the Euro Low Pass Filter that epic uses for the VR Editor, but for blueprints
USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPEuroLowPassFilter
{
	GENERATED_BODY()
public:

	/** Default constructor */
	FBPEuroLowPassFilter() :
		MinCutoff(0.9f),
		CutoffSlope(0.007f),
		DeltaCutoff(1.0f)
	{}

	FBPEuroLowPassFilter(const float InMinCutoff, const float InCutoffSlope, const float InDeltaCutoff) :
		MinCutoff(InMinCutoff),
		CutoffSlope(InCutoffSlope),
		DeltaCutoff(InDeltaCutoff)
	{}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float MinCutoff;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float CutoffSlope;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float DeltaCutoff;

	void ResetSmoothingFilter()
	{
		RawFilter.bFirstTime = true;
		DeltaFilter.bFirstTime = true;
	}

	/** Smooth vector */
	FVector RunFilterSmoothing(const FVector &InRawValue, const float &InDeltaTime)
	{
		// Calculate the delta, if this is the first time then there is no delta
		const FVector Delta = RawFilter.bFirstTime == true ? FVector::ZeroVector : (InRawValue - RawFilter.Previous) * InDeltaTime;

		// Filter the delta to get the estimated
		const FVector Estimated = DeltaFilter.Filter(Delta, FVector(CalculateAlpha(DeltaCutoff, InDeltaTime)));

		// Use the estimated to calculate the cutoff
		const FVector Cutoff = CalculateCutoff(Estimated);

		// Filter passed value 
		return RawFilter.Filter(InRawValue, CalculateAlpha(Cutoff, InDeltaTime));
	}

private:

	const FVector CalculateCutoff(const FVector& InValue)
	{
		FVector Result;
		for (int i = 0; i < 3; i++)
		{
			Result[i] = MinCutoff + CutoffSlope * FMath::Abs(InValue[i]);
		}
		return Result;
	}

	const FVector CalculateAlpha(const FVector& InCutoff, const double InDeltaTime) const
	{
		FVector Result;
		for (int i = 0; i < 3; i++)
		{
			Result[i] = CalculateAlpha(InCutoff[i], InDeltaTime);
		}
		return Result;
	}

	const float CalculateAlpha(const float InCutoff, const double InDeltaTime) const
	{
		const float tau = 1.0 / (2 * PI * InCutoff);
		return 1.0 / (1.0 + tau / InDeltaTime);
	}

	FBasicLowPassFilter RawFilter;
	FBasicLowPassFilter DeltaFilter;

};

//USTRUCT(BlueprintType, Category = "VRExpansionLibrary|Transform")

USTRUCT(/*noexport, */BlueprintType, Category = "VRExpansionLibrary|Transform", meta = (HasNativeMake = "VRExpansionPlugin.VRExpansionPluginFunctionLibrary.MakeTransform_NetQuantize", HasNativeBreak = "VRExpansionPlugin.VRExpansionPluginFunctionLibrary.BreakTransform_NetQuantize"))
struct FTransform_NetQuantize : public FTransform
{
	GENERATED_USTRUCT_BODY()

	FORCEINLINE FTransform_NetQuantize() : FTransform()
	{}

	FORCEINLINE explicit FTransform_NetQuantize(ENoInit Init) : FTransform(Init)
	{}

	FORCEINLINE explicit FTransform_NetQuantize(const FVector& InTranslation) : FTransform(InTranslation)
	{}

	FORCEINLINE explicit FTransform_NetQuantize(const FQuat& InRotation) : FTransform(InRotation)
	{}

	FORCEINLINE explicit FTransform_NetQuantize(const FRotator& InRotation) : FTransform(InRotation)
	{}

	FORCEINLINE FTransform_NetQuantize(const FQuat& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector::OneVector)
		: FTransform(InRotation, InTranslation, InScale3D)
	{}

	FORCEINLINE FTransform_NetQuantize(const FRotator& InRotation, const FVector& InTranslation, const FVector& InScale3D = FVector::OneVector) 
		: FTransform(InRotation, InTranslation, InScale3D)
	{}

	FORCEINLINE FTransform_NetQuantize(const FTransform& InTransform) : FTransform(InTransform)
	{}

	FORCEINLINE explicit FTransform_NetQuantize(const FMatrix& InMatrix) : FTransform(InMatrix)
	{}

	FORCEINLINE FTransform_NetQuantize(const FVector& InX, const FVector& InY, const FVector& InZ, const FVector& InTranslation) 
		: FTransform(InX, InY, InZ, InTranslation)
	{}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		FVector rTranslation;
		FVector rScale3D;
		//FQuat rRotation;
		FRotator rRotation;

		uint16 ShortPitch = 0;
		uint16 ShortYaw = 0;
		uint16 ShortRoll = 0;

		if (Ar.IsSaving())
		{
			// Because transforms can be vectorized or not, need to use the inline retrievers
			rTranslation = this->GetTranslation();
			rScale3D = this->GetScale3D();
			rRotation = this->Rotator();//this->GetRotation();

			// Translation set to 2 decimal precision
			bOutSuccess &= SerializePackedVector<100, 30>(rTranslation, Ar);

			// Scale set to 2 decimal precision, had it 1 but realized that I used two already even
			bOutSuccess &= SerializePackedVector<100, 30>(rScale3D, Ar);

			// Rotation converted to FRotator and short compressed, see below for conversion reason
			// FRotator already serializes compressed short by default but I can save a func call here
			rRotation.SerializeCompressedShort(Ar);

			//Ar << rRotation;

			// If I converted it to a rotator and serialized as shorts I would save 6 bytes.
			// I am unsure about a safe method of compressed serializing a quat, though I read through smallest three
			// Epic already drops W from the Quat and reconstructs it after the send.
			// Converting to rotator first may have conversion issues and has a perf cost....needs testing, epic attempts to handle
			// Singularities in their conversion but I haven't tested it in all circumstances
			//rRotation.SerializeCompressedShort(Ar);
		}
		else // If loading
		{
			bOutSuccess &= SerializePackedVector<100, 30>(rTranslation, Ar);
			bOutSuccess &= SerializePackedVector<100, 30>(rScale3D, Ar);

			rRotation.SerializeCompressedShort(Ar);

			//Ar << rRotation;

			// Set it
			this->SetComponents(rRotation.Quaternion(), rTranslation, rScale3D);
		}

		return bOutSuccess;
	}
};

template<>
struct TStructOpsTypeTraits< FTransform_NetQuantize > : public TStructOpsTypeTraitsBase2<FTransform_NetQuantize>
{
	enum
	{
		WithNetSerializer = true
	};
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

	FBPVRComponentPosRep():
		QuantizationLevel(EVRVectorQuantization::RoundTwoDecimals)
	{
		//QuantizationLevel = EVRVectorQuantization::RoundTwoDecimals;
	}

	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		// Defines the level of Quantization
		//uint8 Flags = (uint8)QuantizationLevel;
		Ar.SerializeBits(&QuantizationLevel, 1); // Only two values 0:1

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
			//QuantizationLevel = (EVRVectorQuantization)Flags;

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

UENUM(Blueprintable)
enum class EGripCollisionType : uint8
{
	/** Held items can be offset by geometry, uses physics for the offset, pushes physics simulating objects with weight taken into account. */
	InteractiveCollisionWithPhysics,

	//	InteractiveCollisionWithVelocity,

	/** Held items can be offset by geometry, uses sweep for the offset, pushes physics simulating objects, no weight. */
	InteractiveCollisionWithSweep,

	/** Uses Stiffness and damping settings on collision, on no collision uses stiffness values 10x stronger so it has less play. */
	InteractiveHybridCollisionWithPhysics,

	/** Uses Stiffness and damping settings on collision, on no collision uses stiffness values 10x stronger, no weight. */
	InteractiveHybridCollisionWithSweep,

	/** Only sweeps movement, will not be offset by geomtry, still pushes physics simulating objects, no weight. */
	SweepWithPhysics,

	/** Does not sweep at all (does not trigger OnHitEvents), still pushes physics simulating objects, no weight. */
	PhysicsOnly,

	/** Free constraint to controller base, no rotational drives. */
	ManipulationGrip,

	/** Free constraint to controller base with a twist drive. */
	ManipulationGripWithWristTwist,

	/** Custom grip is to be handled by the object itself, it just sends the TickGrip event every frame but doesn't move the object. */
	CustomGrip
};

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum class EBPHMDDeviceType : uint8
{
	DT_OculusHMD,//Rift,
	DT_PSVR,
	//DT_Morpheus,
	DT_ES2GenericStereoMesh,
	DT_SteamVR,
	DT_GearVR,
	DT_GoogleVR,
	DT_OSVR,
	DT_AppleARKit,
	DT_GoogleARCore,
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

// Secondary Grip Type
UENUM(Blueprintable)
enum class ESecondaryGripType : uint8
{
	SG_None, // No secondary grip
	SG_Free, // Free secondary grip
	SG_SlotOnly, // Only secondary grip at a slot
	SG_Free_Retain, // Retain pos on drop
	SG_SlotOnly_Retain, 
	SG_FreeWithScaling_Retain, // Scaling with retain pos on drop
	SG_SlotOnlyWithScaling_Retain,
	SG_Custom, // Does nothing, just provides the events for personal use
	SG_ScalingOnly, // Does not track the hand, only scales the mesh with it
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
// ClientSide_Authoritive will grip on the client instantly without server intervention and then send a notice to the server
// ClientSide_Authoritive_NoRep will grip on the client instantly without server intervention but will not rep the grip to the server
// that the grip was made
UENUM(Blueprintable)
enum class EGripMovementReplicationSettings : uint8
{
	KeepOriginalMovement,
	ForceServerSideMovement,
	ForceClientSideMovement,
	ClientSide_Authoritive,
	ClientSide_Authoritive_NoRep
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

// Type of physics constraint to use
UENUM(Blueprintable)
enum class EPhysicsGripConstraintType : uint8
{
	AccelerationConstraint = 0,
	ForceConstraint = 1
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPAdvGripPhysicsSettings
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings")
		bool bUsePhysicsSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		EPhysicsGripConstraintType PhysicsConstraintType;

	// Do not set the Center Of Mass to the grip location, use this if the default is buggy or you want a custom COM
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		bool bDoNotSetCOMToGripLocation;

	// Turn off gravity during the grip, resolves the slight downward offset of the object with normal constraint strengths.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		bool bTurnOffGravityDuringGrip;

	// Use the custom angular values on this grip
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		bool bUseCustomAngularValues;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUseCustomAngularValues", ClampMin = "0.000", UIMin = "0.000"))
		float AngularStiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUseCustomAngularValues", ClampMin = "0.000", UIMin = "0.000"))
		float AngularDamping;

	FBPAdvGripPhysicsSettings():
		bUsePhysicsSettings(false),
		PhysicsConstraintType(EPhysicsGripConstraintType::AccelerationConstraint),
		bDoNotSetCOMToGripLocation(false),
		bTurnOffGravityDuringGrip(false),
		bUseCustomAngularValues(false),
		AngularStiffness(0.0f),
		AngularDamping(0.0f)
	{}

	FORCEINLINE bool operator==(const FBPAdvGripPhysicsSettings &Other) const
	{
		return (bUsePhysicsSettings == Other.bUsePhysicsSettings &&
			bDoNotSetCOMToGripLocation == Other.bDoNotSetCOMToGripLocation &&
			bTurnOffGravityDuringGrip == Other.bTurnOffGravityDuringGrip &&
			bUseCustomAngularValues == Other.bUseCustomAngularValues &&
			FMath::IsNearlyEqual(AngularStiffness, Other.AngularStiffness) &&
			FMath::IsNearlyEqual(AngularDamping, Other.AngularDamping) &&
			PhysicsConstraintType == Other.PhysicsConstraintType);
	}

	FORCEINLINE bool operator!=(const FBPAdvGripPhysicsSettings &Other) const
	{
		return (bUsePhysicsSettings != Other.bUsePhysicsSettings ||
			bDoNotSetCOMToGripLocation != Other.bDoNotSetCOMToGripLocation ||
			bTurnOffGravityDuringGrip != Other.bTurnOffGravityDuringGrip ||
			bUseCustomAngularValues != Other.bUseCustomAngularValues ||
			!FMath::IsNearlyEqual(AngularStiffness, Other.AngularStiffness) ||
			!FMath::IsNearlyEqual(AngularDamping, Other.AngularDamping) ||
			PhysicsConstraintType != Other.PhysicsConstraintType);
	}

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		//Ar << bUsePhysicsSettings;
		Ar.SerializeBits(&bUsePhysicsSettings, 1);

		if (bUsePhysicsSettings)
		{
			//Ar << bDoNotSetCOMToGripLocation;
			Ar.SerializeBits(&bDoNotSetCOMToGripLocation, 1);
			
			//Ar << PhysicsConstraintType;
			Ar.SerializeBits(&PhysicsConstraintType, 1); // This only has two elements

			//Ar << bTurnOffGravityDuringGrip;
			Ar.SerializeBits(&bTurnOffGravityDuringGrip, 1);
			//Ar << bUseCustomAngularValues;
			Ar.SerializeBits(&bUseCustomAngularValues, 1);

			if (bUseCustomAngularValues)
			{
				Ar << AngularStiffness;
				Ar << AngularDamping;
			}
		}

		bOutSuccess = true;
		return bOutSuccess;
	}
};

template<>
struct TStructOpsTypeTraits< FBPAdvGripPhysicsSettings > : public TStructOpsTypeTraitsBase2<FBPAdvGripPhysicsSettings>
{
	enum
	{
		WithNetSerializer = true
	};
};


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPAdvSecondaryGripSettings
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings")
		bool bUseSecondaryGripSettings;

	// Scaler used for handling the smoothing amount, 0.0f is full smoothing, 1.0f is smoothing off
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bUseSecondaryGripSettings", ClampMin = "0.00", UIMin = "0.00", ClampMax = "1.00", UIMax = "1.00"))
		float SecondaryGripScaler;

	// Whether to scale the secondary hand influence off of distance from grip point
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bUseSecondaryGripSettings"))
		bool bUseSecondaryGripDistanceInfluence;

	// If true, will use the GripInfluenceDeadZone as a constant value instead of calculating the distance and lerping, lets you define a static influence amount.
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bUseSecondaryGripDistanceInfluence"))
	//	bool bUseGripInfluenceDeadZoneAsConstant;

	// Distance from grip point in local space where there is 100% influence
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bUseSecondaryGripDistanceInfluence", ClampMin = "0.00", UIMin = "0.00", ClampMax = "256.00", UIMax = "256.00"))
		float GripInfluenceDeadZone;

	// Distance from grip point in local space before all influence is lost on the secondary grip (1.0f - 0.0f influence over this range)
	// this comes into effect outside of the deadzone
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bUseSecondaryGripDistanceInfluence", ClampMin = "1.00", UIMin = "1.00", ClampMax = "256.00", UIMax = "256.00"))
		float GripInfluenceDistanceToZero;

	// Whether clamp the grip scaling in scaling grips
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bUseSecondaryGripSettings"))
		bool bLimitGripScaling;

	// Minimum size to allow scaling in double grip to reach
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bLimitGripScaling"))
		FVector_NetQuantize100 MinimumGripScaling;

	// Maximum size to allow scaling in double grip to reach
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bLimitGripScaling"))
		FVector_NetQuantize100 MaximumGripScaling;

	// Used to smooth filter the secondary influence
	FBPEuroLowPassFilter SmoothingOneEuro;

	FBPAdvSecondaryGripSettings() :
		bUseSecondaryGripSettings(false),
		SecondaryGripScaler(1.0f),
		bUseSecondaryGripDistanceInfluence(false),
		//bUseGripInfluenceDeadZoneAsConstant(false),
		GripInfluenceDeadZone(50.0f),
		GripInfluenceDistanceToZero(100.0f),
		bLimitGripScaling(false),
		MinimumGripScaling(FVector(0.1f)),
		MaximumGripScaling(FVector(10.0f))
	{}

	// Skip passing euro filter in
	FORCEINLINE FBPAdvSecondaryGripSettings& operator=(const FBPAdvSecondaryGripSettings& Other)
	{
		this->bUseSecondaryGripSettings = Other.bUseSecondaryGripSettings;
		this->SecondaryGripScaler = Other.SecondaryGripScaler;
		this->bUseSecondaryGripDistanceInfluence = Other.bUseSecondaryGripDistanceInfluence;
		this->GripInfluenceDeadZone = Other.GripInfluenceDeadZone;
		this->GripInfluenceDistanceToZero = Other.GripInfluenceDistanceToZero;
		this->bLimitGripScaling = Other.bLimitGripScaling;
		this->MinimumGripScaling = Other.MinimumGripScaling;
		this->MaximumGripScaling = Other.MaximumGripScaling;
		return *this;
	}

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		//Ar << bUseSecondaryGripSettings;
		Ar.SerializeBits(&bUseSecondaryGripSettings, 1);

		if (bUseSecondaryGripSettings)
		{
			bOutSuccess = true;

			//Ar << SecondaryGripScaler;

			// This is 0.0-1.0, using normalized compression to get it smaller, 9 bits = 1 bit + 1 bit sign+value and 7 bits precision for 128 / full 2 digit precision
			if (Ar.IsSaving())
				bOutSuccess &= WriteFixedCompressedFloat<1, 9>(SecondaryGripScaler, Ar);
			else
				bOutSuccess &= ReadFixedCompressedFloat<1, 9>(SecondaryGripScaler, Ar);
			
			//Ar << bUseSecondaryGripDistanceInfluence;
			Ar.SerializeBits(&bUseSecondaryGripDistanceInfluence, 1);
			
			if (bUseSecondaryGripDistanceInfluence)
			{
				//Ar << GripInfluenceDeadZone;
				//Ar << GripInfluenceDistanceToZero;
				//Ar.SerializeBits(&bUseGripInfluenceDeadZoneAsConstant, 1);

				// Forcing a maximum value here so that we can compress it by making assumptions
				// 256 max value = 8 bits + 1 bit for sign + 7 bits for precision (up to 128 on precision, so full range 2 digit precision).
				if (Ar.IsSaving())
				{
					bOutSuccess &= WriteFixedCompressedFloat<256, 16>(GripInfluenceDeadZone, Ar);
					//if(!bUseGripInfluenceDeadZoneAsConstant)
					bOutSuccess &= WriteFixedCompressedFloat<256, 16>(GripInfluenceDistanceToZero, Ar);
				}
				else
				{
					bOutSuccess &= ReadFixedCompressedFloat<256, 16>(GripInfluenceDeadZone, Ar);
					//if (!bUseGripInfluenceDeadZoneAsConstant)
					bOutSuccess &= ReadFixedCompressedFloat<256, 16>(GripInfluenceDistanceToZero, Ar);
				}
			}

			//Ar << bLimitGripScaling;
			Ar.SerializeBits(&bLimitGripScaling, 1);

			if (bLimitGripScaling)
			{
				//Ar << MinimumGripScaling;
				MinimumGripScaling.NetSerialize(Ar, Map, bOutSuccess);
				//Ar << MaximumGripScaling;
				MaximumGripScaling.NetSerialize(Ar, Map, bOutSuccess);
			}
		}

		return bOutSuccess;
	}
};

template<>
struct TStructOpsTypeTraits< FBPAdvSecondaryGripSettings > : public TStructOpsTypeTraitsBase2<FBPAdvSecondaryGripSettings>
{
	enum
	{
		WithNetSerializer = true
	};
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPAdvGripSettings
{
	GENERATED_BODY()
public:

	// Priority of this item when being gripped, (Higher is more priority)
	// This lets you prioritize whether an object should be gripped over another one when both
	// collide with traces or overlaps. #Note: Currently not implemented in the plugin, here for your use.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvancedGripSettings")
		uint8 GripPriority;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvancedGripSettings")
		FBPAdvGripPhysicsSettings PhysicsSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvancedGripSettings")
		FBPAdvSecondaryGripSettings SecondaryGripSettings;


	FBPAdvGripSettings() :
		GripPriority(1)
	{}

	FBPAdvGripSettings(int GripPrio) :
		GripPriority(GripPrio)
	{}
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPSecondaryGripInfo
{
	GENERATED_BODY()
public:

	// For multi grip situations
	UPROPERTY(BlueprintReadOnly, Category = "SecondaryGripInfo")
		bool bHasSecondaryAttachment;

	UPROPERTY(BlueprintReadOnly, Category = "SecondaryGripInfo")
		USceneComponent * SecondaryAttachment;

	UPROPERTY()
		FVector_NetQuantize100 SecondaryRelativeLocation;

	UPROPERTY(BlueprintReadWrite, Category = "SecondaryGripInfo")
		bool bIsSlotGrip;

	// Lerp transitions
	// Max value is 16 seconds with two decimal precision, this is to reduce replication overhead
	UPROPERTY()
		float LerpToRate;

	// Filled in from the tick code so users can activate and deactivate grips based on this
	UPROPERTY(BlueprintReadOnly, NotReplicated, Category = "SecondaryGripInfo")
		float SecondaryGripDistance;

	// These are not replicated, they don't need to be
	EGripLerpState GripLerpState;
	float curLerp;

	// Store values for frame by frame changes of secondary grips
	FVector LastRelativeLocation;

	FBPSecondaryGripInfo():
		bHasSecondaryAttachment(false),
		SecondaryAttachment(nullptr),
		SecondaryRelativeLocation(FVector::ZeroVector),
		bIsSlotGrip(false),
		LerpToRate(0.0f),
		SecondaryGripDistance(0.0f),
		GripLerpState(EGripLerpState::NotLerping),
		curLerp(0.0f),
		LastRelativeLocation(FVector::ZeroVector)
	{}

	// Adding this override to handle the fact that repped versions don't send relative loc and slot grip
	// We don't want to override relative loc with 0,0,0 when it is in end lerp as otherwise it lerps wrong
	FORCEINLINE FBPSecondaryGripInfo& RepCopy(const FBPSecondaryGripInfo& Other)
	{
		this->bHasSecondaryAttachment = Other.bHasSecondaryAttachment;
		this->SecondaryAttachment = Other.SecondaryAttachment;
		
		if (bHasSecondaryAttachment)
		{
			this->SecondaryRelativeLocation = Other.SecondaryRelativeLocation;
			this->bIsSlotGrip = Other.bIsSlotGrip;
		}

		this->LerpToRate = Other.LerpToRate;
		return *this;
	}

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		//Ar << bHasSecondaryAttachment;
		Ar.SerializeBits(&bHasSecondaryAttachment, 1);

		if (bHasSecondaryAttachment)
		{
			Ar << SecondaryAttachment;
			//Ar << SecondaryRelativeLocation;
			SecondaryRelativeLocation.NetSerialize(Ar, Map, bOutSuccess);

			//Ar << bIsSlotGrip;
			Ar.SerializeBits(&bIsSlotGrip, 1);
		}

		// This is 0.0 - 16.0, using compression to get it smaller, 4 bits = max 16 + 1 bit for sign and 7 bits precision for 128 / full 2 digit precision
		if (Ar.IsSaving())
			bOutSuccess &= WriteFixedCompressedFloat<16, 12>(LerpToRate, Ar);
		else
			bOutSuccess &= ReadFixedCompressedFloat<16, 12>(LerpToRate, Ar);

		//Ar << LerpToRate;
		return true;
	}
};

template<>
struct TStructOpsTypeTraits< FBPSecondaryGripInfo > : public TStructOpsTypeTraitsBase2<FBPSecondaryGripInfo>
{
	enum
	{
		WithNetSerializer = true
	};
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPInteractionSettings
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		uint32 bLimitsInLocalSpace:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		uint32 bLimitX:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		uint32 bLimitY:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		uint32 bLimitZ:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bLimitPitch:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bLimitYaw:1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bLimitRoll:1;

	// Doesn't work totally correctly without using the ConvertToControllerRelativeTransform node in the motion controller
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bIgnoreHandRotation:1;

	// #TODO: Net quantize the initial and min/max values.
	// I wanted to do it already but the editor treats it like a different type
	// and reinitializes ALL values, which obviously is bad as it would force people
	// to re-enter their offsets all over again......

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector/*_NetQuantize100*/ InitialLinearTranslation;

	// To use property, set value as -Distance
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector/*_NetQuantize100*/ MinLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector/*_NetQuantize100*/ MaxLinearTranslation;

	// FRotators already by default NetSerialize as shorts
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		FRotator InitialAngularTranslation;

	// To use property, set value as -Rotation
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		FRotator MinAngularTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		FRotator MaxAngularTranslation;

	FBPInteractionSettings():
		bLimitsInLocalSpace(true),
		bLimitX(false),
		bLimitY(false),
		bLimitZ(false),
		bLimitPitch(false),
		bLimitYaw(false),
		bLimitRoll(false),
		bIgnoreHandRotation(false),
		InitialLinearTranslation(FVector::ZeroVector),
		MinLinearTranslation(FVector::ZeroVector),
		MaxLinearTranslation(FVector::ZeroVector),
		InitialAngularTranslation(FRotator::ZeroRotator),
		MinAngularTranslation(FRotator::ZeroRotator),
		MaxAngularTranslation(FRotator::ZeroRotator)
	{}
};


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPActorGripInformation
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		EGripTargetType GripTargetType;
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		UObject * GrippedObject;
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		EGripCollisionType GripCollisionType;
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
		EGripLateUpdateSettings GripLateUpdateSetting;
	UPROPERTY(BlueprintReadOnly, NotReplicated, Category = "Settings")
		bool bColliding;
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
		FTransform_NetQuantize RelativeTransform;
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
		bool bIsSlotGrip;
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
		FName GrippedBoneName;
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		EGripMovementReplicationSettings GripMovementReplicationSetting;

	// I would have loved to have both of these not be replicated (and in normal grips they wouldn't have to be)
	// However for serialization purposes and Client_Authority grips they need to be....
	UPROPERTY()
		bool bOriginalReplicatesMovement;
	UPROPERTY()
		bool bOriginalGravity;

	UPROPERTY()
		float Damping;
	UPROPERTY()
		float Stiffness;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		FBPAdvGripSettings AdvancedGripSettings;


	// When true the grips movement logic will not be performed until it is false again
	//UPROPERTY(BlueprintReadWrite)
		//bool bPauseGrip;

	// For multi grip situations
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		FBPSecondaryGripInfo SecondaryGripInfo;

	// Optional Additive Transform for programmatic animation
	UPROPERTY(BlueprintReadWrite, NotReplicated, Category = "Settings")
	FTransform AdditionTransform;

	// Distance from the target point for the grip
	UPROPERTY(BlueprintReadOnly, NotReplicated, Category = "Settings")
		float GripDistance;

	// Locked transitions for swept movement so they don't just rotate in place on contact
	bool bIsLocked;
	FQuat LastLockedRotation;

	// Need to skip one frame of length check post teleport with constrained objects, the constraint may have not been updated yet.
	bool bSkipNextConstraintLengthCheck;

	// Cached values - since not using a full serialize now the old array state may not contain what i need to diff
	// I set these in On_Rep now and check against them when new replications happen to control some actions.
	struct FGripValueCache
	{
		bool bWasInitiallyRepped;
		bool bCachedHasSecondaryAttachment;
		FVector CachedSecondaryRelativeLocation;
		EGripCollisionType CachedGripCollisionType;
		EGripMovementReplicationSettings CachedGripMovementReplicationSetting;
		float CachedStiffness;
		float CachedDamping;
		FBPAdvGripPhysicsSettings CachedPhysicsSettings;
		FName CachedBoneName;

		FGripValueCache():
			bWasInitiallyRepped(false),
			bCachedHasSecondaryAttachment(false),
			CachedSecondaryRelativeLocation(FVector::ZeroVector),
			CachedGripCollisionType(EGripCollisionType::InteractiveCollisionWithSweep),
			CachedGripMovementReplicationSetting(EGripMovementReplicationSettings::ForceClientSideMovement),
			CachedStiffness(1500.0f),
			CachedDamping(200.0f),
			CachedBoneName(NAME_None)
		{}

	}ValueCache;

	// Adding this override to keep un-repped variables from repping over from Client Auth grips
	FORCEINLINE FBPActorGripInformation& RepCopy(const FBPActorGripInformation& Other)
	{
		this->GripTargetType = Other.GripTargetType;
		this->GrippedObject = Other.GrippedObject;
		this->GripCollisionType = Other.GripCollisionType;
		this->GripLateUpdateSetting = Other.GripLateUpdateSetting;
		this->RelativeTransform = Other.RelativeTransform;
		this->bIsSlotGrip = Other.bIsSlotGrip;
		this->GrippedBoneName = Other.GrippedBoneName;
		this->GripMovementReplicationSetting = Other.GripMovementReplicationSetting;
		this->bOriginalReplicatesMovement = Other.bOriginalReplicatesMovement;
		this->bOriginalGravity = Other.bOriginalGravity;
		this->Damping = Other.Damping;
		this->Stiffness = Other.Stiffness;
		this->AdvancedGripSettings = Other.AdvancedGripSettings;
		this->SecondaryGripInfo = Other.SecondaryGripInfo;

		return *this;
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

	FBPActorGripInformation() :
		GripTargetType(EGripTargetType::ActorGrip),
		GrippedObject(nullptr),
		GripCollisionType(EGripCollisionType::InteractiveCollisionWithPhysics),
		GripLateUpdateSetting(EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping),
		bColliding(false),
		RelativeTransform(FTransform::Identity),
		bIsSlotGrip(false),
		GrippedBoneName(NAME_None),
		GripMovementReplicationSetting(EGripMovementReplicationSettings::ForceClientSideMovement),
		bOriginalReplicatesMovement(false),
		bOriginalGravity(false),
		Damping(200.0f),
		Stiffness(1500.0f),
		AdditionTransform(FTransform::Identity),
		GripDistance(0.0f),
		bIsLocked(false),
		LastLockedRotation(FRotator::ZeroRotator),
		bSkipNextConstraintLengthCheck(false)
	{
	}	

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

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
	//	uint8 EnumObjectType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripCollisionType SlotDefaultGripType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripCollisionType FreeDefaultGripType;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
	//	bool bCanHaveDoubleGrip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		ESecondaryGripType SecondaryGripType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripMovementReplicationSettings MovementReplicationType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripLateUpdateSettings LateUpdateSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float ConstraintStiffness;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float ConstraintDamping;

	/*UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		FBPAdvGripPhysicsSettings PhysicsSettings;*/

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float ConstraintBreakDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float SecondarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float PrimarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface|AdvancedGripSettings")
		FBPAdvGripSettings AdvancedGripSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bIsInteractible;

	UPROPERTY(BlueprintReadWrite, NotReplicated, Category = "VRGripInterface")
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadWrite, NotReplicated, Category = "VRGripInterface")
		UGripMotionControllerComponent * HoldingController; // Set on grip notify, not net serializing

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface", meta = (editcondition = "bIsInteractible"))
		FBPInteractionSettings InteractionSettings;

	FBPInterfaceProperties():
		bDenyGripping(false),
		OnTeleportBehavior(EGripInterfaceTeleportBehavior::DropOnTeleport),
		bSimulateOnDrop(true),
		SlotDefaultGripType(EGripCollisionType::ManipulationGrip),
		FreeDefaultGripType(EGripCollisionType::ManipulationGrip),
		SecondaryGripType(ESecondaryGripType::SG_None),
		MovementReplicationType(EGripMovementReplicationSettings::ForceClientSideMovement),
		LateUpdateSetting(EGripLateUpdateSettings::LateUpdatesAlwaysOff),
		ConstraintStiffness(1500.0f),
		ConstraintDamping(200.0f),
		ConstraintBreakDistance(0.0f),
		SecondarySlotRange(20.0f),
		PrimarySlotRange(20.0f),
		bIsInteractible(false),
		bIsHeld(false),
		HoldingController(nullptr)
	{
	}
};


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPActorPhysicsHandleInformation
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		UObject * HandledObject;

	/** Physics scene index of the body we are grabbing. */
	int32 SceneIndex;
	/** Pointer to PhysX joint used by the handle*/
	physx::PxD6Joint* HandleData;
	/** Pointer to kinematic actor jointed to grabbed object */
	physx::PxRigidDynamic* KinActorData;

	physx::PxTransform COMPosition;
	FTransform RootBoneRotation;

	FBPActorPhysicsHandleInformation()
	{
		HandleData = NULL;
		KinActorData = NULL;		
		HandledObject = nullptr;
		//Actor = nullptr;
		//Component = nullptr;
		
		RootBoneRotation = FTransform::Identity;
	}

	FORCEINLINE bool operator==(const FBPActorGripInformation & Other) const
	{
		if (HandledObject && HandledObject == Other.GrippedObject)
			return true;

		return false;
	}

};