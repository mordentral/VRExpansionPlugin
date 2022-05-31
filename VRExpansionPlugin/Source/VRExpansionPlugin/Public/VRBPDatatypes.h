// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
//#include "EngineMinimal.h"
#include "Components/PrimitiveComponent.h"

#include "PhysicsPublic.h"
#include "PhysicsEngine/ConstraintDrives.h"

#if PHYSICS_INTERFACE_PHYSX
//#include "PhysXPublic.h"
//#include "PhysicsEngine/PhysXSupport.h"
#endif // WITH_PHYSX

#include "VRBPDatatypes.generated.h"

class UGripMotionControllerComponent;
class UVRGripScriptBase;

// Custom movement modes for the characters
UENUM(BlueprintType)
enum class EVRCustomMovementMode : uint8
{
	VRMOVE_Climbing UMETA(DisplayName = "Climbing"),
	VRMOVE_LowGrav  UMETA(DisplayName = "LowGrav"),
	VRMOVE_Seated UMETA(DisplayName = "Seated"),
	VRMOVE_SplineFollow UMETA(DisplayName = "SplineFollow")
//	VRMove_Spider UMETA(DisplayName = "Spider")
};

// We use 6 bits for this so a maximum of 64 elements
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
	C_VRMOVE_SplineFollow = 0x0B UMETA(DisplayName = "SplineFollow"), // 
	// 0x0C
	// 0x0D
	// 0x0E
	// 0x0F
	// 0x10
	// 0x11
	// 0x12
	// 0x13
	// 0x14
	// 0x15
	// 0x16
	// 0x17
	// 0x18
	// 0x19
	C_VRMOVE_Custom1 = 0x1A UMETA(DisplayName = "Custom1"),
	C_VRMOVE_Custom2 = 0x1B UMETA(DisplayName = "Custom2"),
	C_VRMOVE_Custom3 = 0x1C UMETA(DisplayName = "Custom3"),
	C_VRMOVE_Custom4 = 0x1D UMETA(DisplayName = "Custom4"),
	C_VRMOVE_Custom5 = 0x1E UMETA(DisplayName = "Custom5"),
	C_VRMOVE_Custom6 = 0x1F UMETA(DisplayName = "Custom6"),
	C_VRMOVE_Custom7 = 0x20 UMETA(DisplayName = "Custom7"),
	C_VRMOVE_Custom8 = 0x21 UMETA(DisplayName = "Custom8"),
	C_VRMOVE_Custom9 = 0x22 UMETA(DisplayName = "Custom9"),
	C_VRMOVE_Custom10 = 0x23 UMETA(DisplayName = "Custom10")
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


/** Different methods for interpolating rotation between transforms */
UENUM(BlueprintType)
enum class EVRLerpInterpolationMode : uint8
{
	/** Shortest Path or Quaternion interpolation for the rotation. */
	QuatInterp,

	/** Rotor or Euler Angle interpolation. */
	EulerInterp,

	/** Dual quaternion interpolation, follows helix or screw-motion path between keyframes.   */
	DualQuatInterp
};

template<class filterType>
class FBasicLowPassFilter
{
public:

	/** Default constructor */
	FBasicLowPassFilter(filterType EmptyValueSet)
	{
		EmptyValue = EmptyValueSet;
		Previous = EmptyValue;
		PreviousRaw = EmptyValue;
		bFirstTime = true;
	}

	/** Calculate */
	filterType Filter(const filterType& InValue, const filterType& InAlpha)
	{

		filterType Result = InValue;
		if (!bFirstTime)
		{
			// This is unsafe in non float / float array data types, but I am not going to be using any like that
			for (int i = 0; i < sizeof(filterType)/sizeof(float); i++)
			{
				((float*)&Result)[i] = ((float*)&InAlpha)[i] * ((float*)&InValue)[i] + (1.0f - ((float*)&InAlpha)[i]) * ((float*)&Previous)[i];
			}
		}

		bFirstTime = false;
		Previous = Result;
		PreviousRaw = InValue;
		return Result;
	}

	filterType EmptyValue;

	/** The previous filtered value */
	filterType Previous;

	/** The previous raw value */
	filterType PreviousRaw;

	/** If this is the first time doing a filter */
	bool bFirstTime;

//private:

	const filterType CalculateCutoff(const filterType& InValue, float& MinCutoff, float& CutoffSlope)
	{
		filterType Result;
		// This is unsafe in non float / float array data types, but I am not going to be using any like that
		for (int i = 0; i < sizeof(filterType)/sizeof(float); i++)
		{
			((float*)&Result)[i] = MinCutoff + CutoffSlope * FMath::Abs(((float*)&InValue)[i]);
		}
		return Result;
	}

	const filterType CalculateAlpha(const filterType& InCutoff, const double InDeltaTime)
	{
		filterType Result;
		// This is unsafe in non float / float array data types, but I am not going to be using any like that
		for (int i = 0; i < sizeof(filterType)/sizeof(float); i++)
		{
			((float*)&Result)[i] = CalculateAlphaTau(((float*)&InCutoff)[i], InDeltaTime);
		}
		return Result;
	}

	inline const float CalculateAlphaTau(const float InCutoff, const double InDeltaTime)
	{
		const float tau = 1.0 / (2.0f * PI * InCutoff);
		return 1.0f / (1.0f + tau / InDeltaTime);
	}
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
		DeltaCutoff(1.0f),
		CutoffSlope(0.007f),
		RawFilter(FVector::ZeroVector),
		DeltaFilter(FVector::ZeroVector)
	{}

	FBPEuroLowPassFilter(const float InMinCutoff, const float InCutoffSlope, const float InDeltaCutoff) :
		MinCutoff(InMinCutoff),
		DeltaCutoff(InDeltaCutoff),
		CutoffSlope(InCutoffSlope),
		RawFilter(FVector::ZeroVector),
		DeltaFilter(FVector::ZeroVector)
	{}

	// The smaller the value the less jitter and the more lag with micro movements
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float MinCutoff;

	// If latency is too high with fast movements increase this value
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float DeltaCutoff;

	// This is the magnitude of adjustment
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float CutoffSlope;


	void ResetSmoothingFilter();

	/** Smooth vector */
	FVector RunFilterSmoothing(const FVector &InRawValue, const float &InDeltaTime);

private:

	FBasicLowPassFilter<FVector> RawFilter;
	FBasicLowPassFilter<FVector> DeltaFilter;

};

/************************************************************************/
/* 1 Euro filter smoothing algorithm									*/
/* http://cristal.univ-lille.fr/~casiez/1euro/							*/
/************************************************************************/
// A re-implementation of the Euro Low Pass Filter that epic uses for the VR Editor, but for blueprints
// This version is for Quaternions
USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPEuroLowPassFilterQuat
{
	GENERATED_BODY()
public:

	/** Default constructor */
	FBPEuroLowPassFilterQuat() :
		MinCutoff(0.9f),
		DeltaCutoff(1.0f),
		CutoffSlope(0.007f),
		RawFilter(FQuat::Identity),
		DeltaFilter(FQuat::Identity)
	{}

	FBPEuroLowPassFilterQuat(const float InMinCutoff, const float InCutoffSlope, const float InDeltaCutoff) :
		MinCutoff(InMinCutoff),
		DeltaCutoff(InDeltaCutoff),
		CutoffSlope(InCutoffSlope),
		RawFilter(FQuat::Identity),
		DeltaFilter(FQuat::Identity)
	{}

	// The smaller the value the less jitter and the more lag with micro movements
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float MinCutoff;

	// If latency is too high with fast movements increase this value
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float DeltaCutoff;

	// This is the magnitude of adjustment
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float CutoffSlope;

	void ResetSmoothingFilter();

	/** Smooth vector */
	FQuat RunFilterSmoothing(const FQuat& InRawValue, const float& InDeltaTime);

private:

	FBasicLowPassFilter<FQuat> RawFilter;
	FBasicLowPassFilter<FQuat> DeltaFilter;

};

/************************************************************************/
/* 1 Euro filter smoothing algorithm									*/
/* http://cristal.univ-lille.fr/~casiez/1euro/							*/
/************************************************************************/
// A re-implementation of the Euro Low Pass Filter that epic uses for the VR Editor, but for blueprints
// This version is for Transforms
USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPEuroLowPassFilterTrans
{
	GENERATED_BODY()
public:

	/** Default constructor */
	FBPEuroLowPassFilterTrans() :
		MinCutoff(0.1f),
		DeltaCutoff(10.0f),
		CutoffSlope(10.0f),
		RawFilter(FTransform::Identity),
		DeltaFilter(FTransform::Identity)
	{}

	FBPEuroLowPassFilterTrans(const float InMinCutoff, const float InCutoffSlope, const float InDeltaCutoff) :
		MinCutoff(InMinCutoff),
		DeltaCutoff(InDeltaCutoff),
		CutoffSlope(InCutoffSlope),
		RawFilter(FTransform::Identity),
		DeltaFilter(FTransform::Identity)
	{}

	// The smaller the value the less jitter and the more lag with micro movements
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float MinCutoff;

	// If latency is too high with fast movements increase this value
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float DeltaCutoff;

	// This is the magnitude of adjustment
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "FilterSettings")
		float CutoffSlope;

	void ResetSmoothingFilter();

	/** Smooth vector */
	FTransform RunFilterSmoothing(const FTransform& InRawValue, const float& InDeltaTime);

private:

	FBasicLowPassFilter<FTransform> RawFilter;
	FBasicLowPassFilter<FTransform> DeltaFilter;

};

// The type of velocity tracking to perform on the motion controllers
UENUM(BlueprintType)
enum class EVRVelocityType : uint8
{
	// Gets the frame by frame velocity
	VRLOCITY_Default UMETA(DisplayName = "Default"),

	// Gets a running average velocity across a sample duration
	VRLOCITY_RunningAverage  UMETA(DisplayName = "Running Average"),

	// Gets the peak velocity across a sample duration
	VRLOCITY_SamplePeak UMETA(DisplayName = "Sampled Peak")
};

// A structure used to store and calculate velocities in different ways
USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPLowPassPeakFilter
{
	GENERATED_BODY()
public:

	/** Default constructor */
	FBPLowPassPeakFilter() :
		VelocitySamples(30),
		VelocitySampleLogCounter(0)
	{}

	// This is the number of samples to keep active
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Samples")
		int32 VelocitySamples;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Samples")
	TArray<FVector>VelocitySampleLog;
	
	int32 VelocitySampleLogCounter;

	void Reset()
	{
		VelocitySampleLog.Reset(VelocitySamples);
	}

	void AddSample(FVector NewSample)
	{
		if (VelocitySamples <= 0)
			return;

		if (VelocitySampleLog.Num() != VelocitySamples)
		{
			VelocitySampleLog.Reset(VelocitySamples);
			VelocitySampleLog.AddZeroed(VelocitySamples);
			VelocitySampleLogCounter = 0;
		}

		VelocitySampleLog[VelocitySampleLogCounter] = NewSample;
		++VelocitySampleLogCounter;

		if (VelocitySampleLogCounter >= VelocitySamples)
			VelocitySampleLogCounter = 0;
	}

	FVector GetPeak() const
	{
		FVector MaxValue = FVector::ZeroVector;
		float ValueSizeSq = 0.f;
		float CurSizeSq = 0.f;

		for (int i = 0; i < VelocitySampleLog.Num(); i++)
		{
			CurSizeSq = VelocitySampleLog[i].SizeSquared();
			if (CurSizeSq > ValueSizeSq)
			{
				MaxValue = VelocitySampleLog[i];
				ValueSizeSq = CurSizeSq;
			}
		}

		return MaxValue;
	}
};

// Some static vars so we don't have to keep calculating these for our Smallest Three compression
namespace TransNetQuant
{
	static const float MinimumQ = -1.0f / 1.414214f;
	static const float MaximumQ = +1.0f / 1.414214f;
	static const float MinMaxQDiff = TransNetQuant::MaximumQ - TransNetQuant::MinimumQ;
}

USTRUCT(/*noexport, */BlueprintType, Category = "VRExpansionLibrary|TransformNetQuantize", meta = (HasNativeMake = "VRExpansionPlugin.VRExpansionFunctionLibrary.MakeTransform_NetQuantize", HasNativeBreak = "VRExpansionPlugin.VRExpansionFunctionLibrary.BreakTransform_NetQuantize"))
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
public:

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	// Serializes a quaternion with the Smallest Three alg
	// Referencing the implementation from https://gafferongames.com/post/snapshot_compression/
	// Which appears to be the mostly widely referenced method
	// Template variable is number of bits per element (IE: precision), lowest suggested is 9
	// While I wouldn't go to 16 as at that point it is 2 bits more expensive than FRotator::SerializeShort
	// Due to the overhead 2 bits of sending out the largest index, a good default is likely 9-10 bits.
	template <uint32 bits>
	static bool SerializeQuat_SmallestThree(FArchive& Ar, FQuat &InQuat)
	{
		check(bits > 1 && bits <= 32);

		uint32 IntegerA = 0, IntegerB = 0, IntegerC = 0, LargestIndex = 0;

		// Get our scaler to not chop off the values
		const float scale = float((1 << bits) - 1);

		if (Ar.IsSaving())
		{
			InQuat.Normalize();
			const float abs_x = FMath::Abs(InQuat.X);
			const float abs_y = FMath::Abs(InQuat.Y);
			const float abs_z = FMath::Abs(InQuat.Z);
			const float abs_w = FMath::Abs(InQuat.W);

			LargestIndex = 0;
			float largest_value = abs_x;

			if (abs_y > largest_value)
			{
				LargestIndex = 1;
				largest_value = abs_y;
			}

			if (abs_z > largest_value)
			{
				LargestIndex = 2;
				largest_value = abs_z;
			}

			if (abs_w > largest_value)
			{
				LargestIndex = 3;
				largest_value = abs_w;
			}

			float a = 0.f;
			float b = 0.f;
			float c = 0.f;

			switch (LargestIndex)
			{
			case 0:
				if (InQuat.X >= 0)
				{
					a = InQuat.Y;
					b = InQuat.Z;
					c = InQuat.W;
				}
				else
				{
					a = -InQuat.Y;
					b = -InQuat.Z;
					c = -InQuat.W;
				}
				break;

			case 1:
				if (InQuat.Y >= 0)
				{
					a = InQuat.X;
					b = InQuat.Z;
					c = InQuat.W;
				}
				else
				{
					a = -InQuat.X;
					b = -InQuat.Z;
					c = -InQuat.W;
				}
				break;

			case 2:
				if (InQuat.Z >= 0)
				{
					a = InQuat.X;
					b = InQuat.Y;
					c = InQuat.W;
				}
				else
				{
					a = -InQuat.X;
					b = -InQuat.Y;
					c = -InQuat.W;
				}
				break;

			case 3:
				if (InQuat.W >= 0)
				{
					a = InQuat.X;
					b = InQuat.Y;
					c = InQuat.Z;
				}
				else
				{
					a = -InQuat.X;
					b = -InQuat.Y;
					c = -InQuat.Z;
				}
				break;

			default:break;
			}

			const float normal_a = (a - TransNetQuant::MinimumQ) / (TransNetQuant::MinMaxQDiff);
			const float normal_b = (b - TransNetQuant::MinimumQ) / (TransNetQuant::MinMaxQDiff);
			const float normal_c = (c - TransNetQuant::MinimumQ) / (TransNetQuant::MinMaxQDiff);

			IntegerA = FMath::FloorToInt(normal_a * scale + 0.5f);
			IntegerB = FMath::FloorToInt(normal_b * scale + 0.5f);
			IntegerC = FMath::FloorToInt(normal_c * scale + 0.5f);
		}

		// Serialize the bits
		Ar.SerializeBits(&LargestIndex, 2);
		Ar.SerializeBits(&IntegerA, bits);
		Ar.SerializeBits(&IntegerB, bits);
		Ar.SerializeBits(&IntegerC, bits);

		if (Ar.IsLoading())
		{
			const float inverse_scale = 1.0f / scale;

			const float a = IntegerA * inverse_scale * (TransNetQuant::MinMaxQDiff) + TransNetQuant::MinimumQ;
			const float b = IntegerB * inverse_scale * (TransNetQuant::MinMaxQDiff) + TransNetQuant::MinimumQ;
			const float c = IntegerC * inverse_scale * (TransNetQuant::MinMaxQDiff) + TransNetQuant::MinimumQ;

			switch (LargestIndex)
			{
			case 0:
			{
				InQuat.X = FMath::Sqrt(1.f - a * a - b * b - c * c);
				InQuat.Y = a;
				InQuat.Z = b;
				InQuat.W = c;
			}
			break;

			case 1:
			{
				InQuat.X = a;
				InQuat.Y = FMath::Sqrt(1.f - a * a - b * b - c * c);
				InQuat.Z = b;
				InQuat.W = c;
			}
			break;

			case 2:
			{
				InQuat.X = a;
				InQuat.Y = b;
				InQuat.Z = FMath::Sqrt(1.f - a * a - b * b - c * c);
				InQuat.W = c;
			}
			break;

			case 3:
			{
				InQuat.X = a;
				InQuat.Y = b;
				InQuat.Z = c;
				InQuat.W = FMath::Sqrt(1.f - a * a - b * b - c * c);
			}
			break;

			default:
			{
				InQuat.X = 0.f;
				InQuat.Y = 0.f;
				InQuat.Z = 0.f;
				InQuat.W = 1.f;
			}
			}

			InQuat.Normalize();
		}

		return true;
	}
};

template<>
struct TStructOpsTypeTraits< FTransform_NetQuantize > : public TStructOpsTypeTraitsBase2<FTransform_NetQuantize>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
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

UENUM()
enum class EVRRotationQuantization : uint8
{
	/** Each rotation component will be rounded to 10 bits (1024 values). */
	RoundTo10Bits = 0,
	/** Each rotation component will be rounded to a short. */
	RoundToShort = 1
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

	// The quantization level to use for the vector components
	UPROPERTY(EditDefaultsOnly, Category = Replication, AdvancedDisplay)
		EVRVectorQuantization QuantizationLevel;

	// The quantization level to use for the rotation components
	// Using 10 bits mode saves approx 2.25 bytes per replication.
	UPROPERTY(EditDefaultsOnly, Category = Replication, AdvancedDisplay)
		EVRRotationQuantization RotationQuantizationLevel;

	FORCEINLINE uint16 CompressAxisTo10BitShort(float Angle)
	{
		// map [0->360) to [0->1024) and mask off any winding
		return FMath::RoundToInt(Angle * 1024.f / 360.f) & 0xFFFF;
	}


	FORCEINLINE float DecompressAxisFrom10BitShort(uint16 Angle)
	{
		// map [0->1024) to [0->360)
		return (Angle * 360.f / 1024.f);
	}

	FBPVRComponentPosRep():
		QuantizationLevel(EVRVectorQuantization::RoundTwoDecimals),
		RotationQuantizationLevel(EVRRotationQuantization::RoundToShort)
	{
		//QuantizationLevel = EVRVectorQuantization::RoundTwoDecimals;
		Position = FVector::ZeroVector;
		Rotation = FRotator::ZeroRotator;
	}

	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		// Defines the level of Quantization
		//uint8 Flags = (uint8)QuantizationLevel;
		Ar.SerializeBits(&QuantizationLevel, 1); // Only two values 0:1
		Ar.SerializeBits(&RotationQuantizationLevel, 1); // Only two values 0:1

		// No longer using their built in rotation rep, as controllers will rarely if ever be at 0 rot on an axis and 
		// so the 1 bit overhead per axis is just that, overhead
		//Rotation.SerializeCompressedShort(Ar);

		uint16 ShortPitch = 0;
		uint16 ShortYaw = 0;
		uint16 ShortRoll = 0;
		
		/**
		*	Valid range 100: 2^22 / 100 = +/- 41,943.04 (419.43 meters)
		*	Valid range 10: 2^18 / 10 = +/- 26,214.4 (262.144 meters)
		*	Pos rep is assumed to be in relative space for a tracked component, these numbers should be fine
		*/
		if (Ar.IsSaving())
		{		
			switch (QuantizationLevel)
			{
			case EVRVectorQuantization::RoundTwoDecimals: bOutSuccess &= SerializePackedVector<100, 22/*30*/>(Position, Ar); break;
			case EVRVectorQuantization::RoundOneDecimal: bOutSuccess &= SerializePackedVector<10, 18/*24*/>(Position, Ar); break;
			}

			switch (RotationQuantizationLevel)
			{
			case EVRRotationQuantization::RoundTo10Bits:
			{
				ShortPitch = CompressAxisTo10BitShort(Rotation.Pitch);
				ShortYaw = CompressAxisTo10BitShort(Rotation.Yaw);
				ShortRoll = CompressAxisTo10BitShort(Rotation.Roll);

				Ar.SerializeBits(&ShortPitch, 10);
				Ar.SerializeBits(&ShortYaw, 10);
				Ar.SerializeBits(&ShortRoll, 10);
			}break;

			case EVRRotationQuantization::RoundToShort:
			{
				ShortPitch = FRotator::CompressAxisToShort(Rotation.Pitch);
				ShortYaw = FRotator::CompressAxisToShort(Rotation.Yaw);
				ShortRoll = FRotator::CompressAxisToShort(Rotation.Roll);

				Ar << ShortPitch;
				Ar << ShortYaw;
				Ar << ShortRoll;
			}break;
			}
		}
		else // If loading
		{
			//QuantizationLevel = (EVRVectorQuantization)Flags;

			switch (QuantizationLevel)
			{
			case EVRVectorQuantization::RoundTwoDecimals: bOutSuccess &= SerializePackedVector<100, 22/*30*/>(Position, Ar); break;
			case EVRVectorQuantization::RoundOneDecimal: bOutSuccess &= SerializePackedVector<10, 18/*24*/>(Position, Ar); break;
			}

			switch (RotationQuantizationLevel)
			{
			case EVRRotationQuantization::RoundTo10Bits:
			{
				Ar.SerializeBits(&ShortPitch, 10);
				Ar.SerializeBits(&ShortYaw, 10);
				Ar.SerializeBits(&ShortRoll, 10);

				Rotation.Pitch = DecompressAxisFrom10BitShort(ShortPitch);
				Rotation.Yaw = DecompressAxisFrom10BitShort(ShortYaw);
				Rotation.Roll = DecompressAxisFrom10BitShort(ShortRoll);
			}break;

			case EVRRotationQuantization::RoundToShort:
			{
				Ar << ShortPitch;
				Ar << ShortYaw;
				Ar << ShortRoll;

				Rotation.Pitch = FRotator::DecompressAxisFromShort(ShortPitch);
				Rotation.Yaw = FRotator::DecompressAxisFromShort(ShortYaw);
				Rotation.Roll = FRotator::DecompressAxisFromShort(ShortRoll);
			}break;
			}
		}

		return bOutSuccess;
	}

};

template<>
struct TStructOpsTypeTraits< FBPVRComponentPosRep > : public TStructOpsTypeTraitsBase2<FBPVRComponentPosRep>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
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

	/** Swaps back and forth between physx grip and a sweep type grip depending on if the held object will be colliding this frame or not. */
	InteractiveHybridCollisionWithSweep,

	/** Only sweeps movement, will not be offset by geomtry, still pushes physics simulating objects, no weight. */
	SweepWithPhysics,

	/** Does not sweep at all (does not trigger OnHitEvents), still pushes physics simulating objects, no weight. */
	PhysicsOnly,

	/** Free constraint to controller base, no rotational drives. */
	ManipulationGrip,

	/** Free constraint to controller base with a twist drive. */
	ManipulationGripWithWristTwist,
	
	/** Attachment grips use native attachment and only sets location / rotation if they differ, this grip always late updates*/
	AttachmentGrip,

	/** Custom grip is to be handled by the object itself, it just sends the TickGrip event every frame but doesn't move the object. */
	CustomGrip,

	/** A grip that does not tick or move, used for drop / grip events only and uses least amount of processing. */
	EventsOnly,

	/** Uses a hard constraint with no softness to lock them together, best used with ConstrainToPivot enabled and a bone chain. */
	LockedConstraint

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
	//ConstantLerp_DEPRECATED,
	NotLerping
};

// Secondary Grip Type
UENUM(Blueprintable)
enum class ESecondaryGripType : uint8
{
	// No secondary grip
	SG_None, 
	// Free secondary grip
	SG_Free, 
	// Only secondary grip at a slot
	SG_SlotOnly, 
	// Retain pos on drop
	SG_Free_Retain, 
	// Retain pos on drop, slot only
	SG_SlotOnly_Retain, 
	// Scaling with retain pos on drop
	SG_FreeWithScaling_Retain, 
	// Scaling with retain pos on drop, slot only
	SG_SlotOnlyWithScaling_Retain,
	// Does nothing, just provides the events for personal use
	SG_Custom, 
	// Does not track the hand, only scales the mesh with it
	SG_ScalingOnly, 
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
	/* Teleports entire actor */
	TeleportAllComponents,
	/* Teleports by the location delta and not the calculated new position of the grip, useful for rag dolls*/
	DeltaTeleportation,
	/* Only teleports an actor if the root component is held */
	OnlyTeleportRootComponent,
	/* Just drop the grip on teleport */
	DropOnTeleport,
	/* Teleporting is not allowed */
	DontTeleport
};

// Type of physics constraint to use
UENUM(Blueprintable)
enum class EPhysicsGripConstraintType : uint8
{
	AccelerationConstraint = 0,
	// Not available when not using Physx
	ForceConstraint = 1
};

UENUM(Blueprintable)
enum class EPhysicsGripCOMType : uint8
{
	/* Use the default setting for the specified grip type */
	COM_Default = 0,
	/* Don't grip at center of mass (generally unstable as it grips at actor zero)*/
	COM_AtPivot = 1,
	/* Set center of mass to grip location and grip there (default for interactible with physics) */
	COM_SetAndGripAt = 2,
	/* Grip at center of mass but do not set it */
	COM_GripAt = 3,
	/* Just grip at the controller location, but don't set COM (default for manipulation grips)*/
	COM_GripAtControllerLoc = 4
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPAdvGripPhysicsSettings
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings")
		bool bUsePhysicsSettings;

	// Not available outside of physx, chaos has no force constraints and other plugin physics engines may not as well
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		EPhysicsGripConstraintType PhysicsConstraintType;

	// Set how the grips handle center of mass
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		EPhysicsGripCOMType PhysicsGripLocationSettings;

	// Turn off gravity during the grip, resolves the slight downward offset of the object with normal constraint strengths.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		bool bTurnOffGravityDuringGrip;

	// Don't automatically (un)simulate the component/root on grip/drop, let the end user set it up instead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"))
		bool bSkipSettingSimulating;

	// A multiplier to add to the stiffness of a grip that is then set as the MaxForce of the grip
	// It is clamped between 0.00 and 256.00 to save in replication cost, a value of 0 will mean max force is infinite as it will multiply it to zero (legacy behavior)
	// If you want an exact value you can figure it out as a factor of the stiffness, also Max force can be directly edited with SetAdvancedConstraintSettings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"), meta = (ClampMin = "0.00", UIMin = "0.00", ClampMax = "256.00", UIMax = "256.00"))
		float LinearMaxForceCoefficient;

	// A multiplier to add to the stiffness of a grip that is then set as the MaxForce of the grip
	// It is clamped between 0.00 and 256.00 to save in replication cost, a value of 0 will mean max force is infinite as it will multiply it to zero (legacy behavior)
	// If you want an exact value you can figure it out as a factor of the stiffness, also Max force can be directly edited with SetAdvancedConstraintSettings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (editcondition = "bUsePhysicsSettings"), meta = (ClampMin = "0.00", UIMin = "0.00", ClampMax = "256.00", UIMax = "256.00"))
		float AngularMaxForceCoefficient;

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
		PhysicsGripLocationSettings(EPhysicsGripCOMType::COM_Default),
		bTurnOffGravityDuringGrip(false),
		bSkipSettingSimulating(false),
		LinearMaxForceCoefficient(0.f),
		AngularMaxForceCoefficient(0.f),
		bUseCustomAngularValues(false),
		AngularStiffness(0.0f),
		AngularDamping(0.0f)//,
		//MaxForce(0.f)
	{}

	FORCEINLINE bool operator==(const FBPAdvGripPhysicsSettings &Other) const
	{
		return (bUsePhysicsSettings == Other.bUsePhysicsSettings &&
			PhysicsGripLocationSettings == Other.PhysicsGripLocationSettings &&
			bTurnOffGravityDuringGrip == Other.bTurnOffGravityDuringGrip &&
			bSkipSettingSimulating == Other.bSkipSettingSimulating &&
			bUseCustomAngularValues == Other.bUseCustomAngularValues &&
			PhysicsConstraintType == Other.PhysicsConstraintType &&
			FMath::IsNearlyEqual(LinearMaxForceCoefficient, Other.LinearMaxForceCoefficient) &&
			FMath::IsNearlyEqual(AngularMaxForceCoefficient, Other.AngularMaxForceCoefficient) &&
			FMath::IsNearlyEqual(AngularStiffness, Other.AngularStiffness) &&
			FMath::IsNearlyEqual(AngularDamping, Other.AngularDamping) //&&
			//FMath::IsNearlyEqual(MaxForce, Other.MaxForce)
			);
	}

	FORCEINLINE bool operator!=(const FBPAdvGripPhysicsSettings &Other) const
	{
		return (bUsePhysicsSettings != Other.bUsePhysicsSettings ||
			PhysicsGripLocationSettings != Other.PhysicsGripLocationSettings ||
			bTurnOffGravityDuringGrip != Other.bTurnOffGravityDuringGrip ||
			bSkipSettingSimulating != Other.bSkipSettingSimulating ||
			bUseCustomAngularValues != Other.bUseCustomAngularValues ||
			PhysicsConstraintType != Other.PhysicsConstraintType ||
			!FMath::IsNearlyEqual(LinearMaxForceCoefficient, Other.LinearMaxForceCoefficient) ||
			!FMath::IsNearlyEqual(AngularMaxForceCoefficient, Other.AngularMaxForceCoefficient) ||
			!FMath::IsNearlyEqual(AngularStiffness, Other.AngularStiffness) ||
			!FMath::IsNearlyEqual(AngularDamping, Other.AngularDamping) //||
			//!FMath::IsNearlyEqual(MaxForce, Other.MaxForce)
			);
	}

	/** Network serialization */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		//Ar << bUsePhysicsSettings;
		Ar.SerializeBits(&bUsePhysicsSettings, 1);

		if (bUsePhysicsSettings)
		{
			//Ar << bDoNotSetCOMToGripLocation;
			Ar.SerializeBits(&PhysicsGripLocationSettings, 3); // This only has four elements
			
			//Ar << PhysicsConstraintType;
			Ar.SerializeBits(&PhysicsConstraintType, 1); // This only has two elements

			//Ar << bTurnOffGravityDuringGrip;
			Ar.SerializeBits(&bTurnOffGravityDuringGrip, 1);
			Ar.SerializeBits(&bSkipSettingSimulating, 1);


			// This is 0.0 - 256.0, using compression to get it smaller, 8 bits = max 256 + 1 bit for sign and 7 bits precision for 128 / full 2 digit precision
			if (Ar.IsSaving())
			{
				bOutSuccess &= WriteFixedCompressedFloat<256, 16>(LinearMaxForceCoefficient, Ar);
				bOutSuccess &= WriteFixedCompressedFloat<256, 16>(AngularMaxForceCoefficient, Ar);
			}
			else
			{
				bOutSuccess &= ReadFixedCompressedFloat<256, 16>(LinearMaxForceCoefficient, Ar);
				bOutSuccess &= ReadFixedCompressedFloat<256, 16>(AngularMaxForceCoefficient, Ar);
			}



			//Ar << bUseCustomAngularValues;
			Ar.SerializeBits(&bUseCustomAngularValues, 1);

			if (bUseCustomAngularValues)
			{
				Ar << AngularStiffness;
				Ar << AngularDamping;
			}

			//Ar << MaxForce;
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
struct VREXPANSIONPLUGIN_API FBPAdvGripSettings
{
	GENERATED_BODY()
public:

	// Priority of this item when being gripped, (Higher is more priority)
	// This lets you prioritize whether an object should be gripped over another one when both
	// collide with traces or overlaps. #Note: Currently not implemented in the plugin, here for your use.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvancedGripSettings")
		uint8 GripPriority;

	// If true, will set the owner of actor grips on grip
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvancedGripSettings")
		bool bSetOwnerOnGrip;

	// If true, we will be bypassed on global lerp operations
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvancedGripSettings")
		bool bDisallowLerping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvancedGripSettings")
		FBPAdvGripPhysicsSettings PhysicsSettings;

	FBPAdvGripSettings() :
		GripPriority(1),
		bSetOwnerOnGrip(1),
		bDisallowLerping(0)
	{}

	FBPAdvGripSettings(int GripPrio) :
		GripPriority(GripPrio),
		bSetOwnerOnGrip(1),
		bDisallowLerping(0)
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

	UPROPERTY(BlueprintReadOnly, Category = "SecondaryGripInfo")
		FTransform_NetQuantize SecondaryRelativeTransform;

	UPROPERTY(BlueprintReadWrite, Category = "SecondaryGripInfo")
		bool bIsSlotGrip;

	UPROPERTY(BlueprintReadWrite, Category = "SecondaryGripInfo")
		FName SecondarySlotName;

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

	void ClearNonReppingItems()
	{
		SecondaryGripDistance = 0.0f;
		GripLerpState = EGripLerpState::NotLerping;
		curLerp = 0.0f;
	}

	FBPSecondaryGripInfo():
		bHasSecondaryAttachment(false),
		SecondaryAttachment(nullptr),
		SecondaryRelativeTransform(FTransform::Identity),
		bIsSlotGrip(false),
		SecondarySlotName(NAME_None),
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
			this->SecondaryRelativeTransform = Other.SecondaryRelativeTransform;
			this->bIsSlotGrip = Other.bIsSlotGrip;
			this->SecondarySlotName = Other.SecondarySlotName;
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
			SecondaryRelativeTransform.NetSerialize(Ar, Map, bOutSuccess);

			//Ar << bIsSlotGrip;
			Ar.SerializeBits(&bIsSlotGrip, 1);

			Ar << SecondarySlotName;
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

#define INVALID_VRGRIP_ID 0

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPActorGripInformation
{
	GENERATED_BODY()
public:

	// Hashed unique ID to identify this grip instance
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		uint8 GripID;
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
	UPROPERTY(BlueprintReadWrite, Category = "Settings")
		FName SlotName;
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		EGripMovementReplicationSettings GripMovementReplicationSetting;

	// Whether the grip is currently paused
	UPROPERTY(BlueprintReadWrite, NotReplicated, Category = "Settings")
		bool bIsPaused;

	// Only true in one specific circumstance, when you are a simulated client
	// and the grip has been dropped but replication on the array hasn't deleted
	// the entry yet. We cannot remove the entry as it can corrupt the array.
	// this lets end users check against the grip to ignore it.
	UPROPERTY(BlueprintReadOnly, NotReplicated, Category = "Settings")
		bool bIsPendingKill;

	// When true, will lock a hybrid grip into its collision state
	UPROPERTY(BlueprintReadWrite, NotReplicated, Category = "Settings")
		bool bLockHybridGrip;

	// I would have loved to have both of these not be replicated (and in normal grips they wouldn't have to be)
	// However for serialization purposes and Client_Authority grips they need to be....
	UPROPERTY()
		bool bOriginalReplicatesMovement;
	UPROPERTY()
		bool bOriginalGravity;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		float Damping;
	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		float Stiffness;

	UPROPERTY(BlueprintReadOnly, Category = "Settings")
		FBPAdvGripSettings AdvancedGripSettings;

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
	
	// For delta teleport and any future calculations we want to do
	FTransform LastWorldTransform;

	// Need to skip one frame of length check post teleport with constrained objects, the constraint may have not been updated yet.
	bool bSkipNextTeleportCheck;

	// Need to skip one frame of length check post teleport with constrained objects, the constraint may have not been updated yet.
	bool bSkipNextConstraintLengthCheck;

	// Lerp settings if we are using global lerping
	float CurrentLerpTime;
	float LerpSpeed;
	FTransform OnGripTransform;

	UPROPERTY(BlueprintReadOnly, NotReplicated, Category = "Settings")
	bool bIsLerping;

	bool IsLocalAuthGrip()
	{
		return GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive || GripMovementReplicationSetting == EGripMovementReplicationSettings::ClientSide_Authoritive_NoRep;
	}

	// If the grip is valid
	bool IsValid() const
	{
		return (!bIsPendingKill && GripID != INVALID_VRGRIP_ID && GrippedObject);
	}

	// Both valid and is not paused
	bool IsActive() const
	{
		return (!bIsPendingKill && GripID != INVALID_VRGRIP_ID && GrippedObject && !bIsPaused);
	}

	// Cached values - since not using a full serialize now the old array state may not contain what i need to diff
	// I set these in On_Rep now and check against them when new replications happen to control some actions.
	struct FGripValueCache
	{
		bool bWasInitiallyRepped;
		uint8 CachedGripID;

		FGripValueCache() :
			bWasInitiallyRepped(false),
			CachedGripID(INVALID_VRGRIP_ID)
		{}

	}ValueCache;

	void ClearNonReppingItems()
	{
		ValueCache = FGripValueCache();
		bColliding = false;
		bIsLocked = false;
		LastLockedRotation = FQuat::Identity;
		LastWorldTransform.SetIdentity();
		bSkipNextTeleportCheck = false;
		bSkipNextConstraintLengthCheck = false;
		bIsPaused = false;
		bIsPendingKill = false;
		bLockHybridGrip = false;
		AdditionTransform = FTransform::Identity;
		GripDistance = 0.0f;
		CurrentLerpTime = 0.f;
		LerpSpeed = 0.f;
		OnGripTransform = FTransform::Identity;
		bIsLerping = false;

		// Clear out the secondary grip
		SecondaryGripInfo.ClearNonReppingItems();
	}

	// Adding this override to keep un-repped variables from repping over from Client Auth grips
	FORCEINLINE FBPActorGripInformation& RepCopy(const FBPActorGripInformation& Other)
	{
		this->GripID = Other.GripID;
		this->GripTargetType = Other.GripTargetType;
		this->GrippedObject = Other.GrippedObject;
		this->GripCollisionType = Other.GripCollisionType;
		this->GripLateUpdateSetting = Other.GripLateUpdateSetting;
		this->RelativeTransform = Other.RelativeTransform;
		this->bIsSlotGrip = Other.bIsSlotGrip;
		this->GrippedBoneName = Other.GrippedBoneName;
		this->SlotName = Other.SlotName;
		this->GripMovementReplicationSetting = Other.GripMovementReplicationSetting;
		//this->bOriginalReplicatesMovement = Other.bOriginalReplicatesMovement;
		//this->bOriginalGravity = Other.bOriginalGravity;
		this->Damping = Other.Damping;
		this->Stiffness = Other.Stiffness;
		this->AdvancedGripSettings = Other.AdvancedGripSettings;		
		this->SecondaryGripInfo.RepCopy(Other.SecondaryGripInfo); // Run the replication copy version so we don't overwrite vars
		//this->SecondaryGripInfo = Other.SecondaryGripInfo;

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
		if ((GripID != INVALID_VRGRIP_ID) && (GripID == Other.GripID) )
			return true;
			//if (GrippedObject && GrippedObject == Other.GrippedObject)
			//return true;

		return false;
	}

	FORCEINLINE bool operator==(const AActor * Other) const
	{
		if (Other && GrippedObject && GrippedObject == (const UObject*)Other)
			return true;

		return false;
	}

	FORCEINLINE bool operator==(const UPrimitiveComponent * Other) const
	{
		if (Other && GrippedObject && GrippedObject == (const UObject*)Other)
			return true;

		return false;
	}

	FORCEINLINE bool operator==(const UObject * Other) const
	{
		if (Other && GrippedObject == Other)
			return true;

		return false;
	}

	FORCEINLINE bool operator==(const uint8& Other) const
	{
		if ((GripID != INVALID_VRGRIP_ID) && (GripID == Other))
			return true;

		return false;
	}

	FBPActorGripInformation() :
		GripID(INVALID_VRGRIP_ID),
		GripTargetType(EGripTargetType::ActorGrip),
		GrippedObject(nullptr),
		GripCollisionType(EGripCollisionType::InteractiveCollisionWithPhysics),
		GripLateUpdateSetting(EGripLateUpdateSettings::NotWhenCollidingOrDoubleGripping),
		bColliding(false),
		RelativeTransform(FTransform::Identity),
		bIsSlotGrip(false),
		GrippedBoneName(NAME_None),
		SlotName(NAME_None),
		GripMovementReplicationSetting(EGripMovementReplicationSettings::ForceClientSideMovement),
		bIsPaused(false),
		bIsPendingKill(false),
		bLockHybridGrip(false),
		bOriginalReplicatesMovement(false),
		bOriginalGravity(false),
		Damping(200.0f),
		Stiffness(1500.0f),
		AdditionTransform(FTransform::Identity),
		GripDistance(0.0f),
		bIsLocked(false),
		LastLockedRotation(FRotator::ZeroRotator),
		LastWorldTransform(FTransform::Identity),
		bSkipNextTeleportCheck(false),
		bSkipNextConstraintLengthCheck(false),
		CurrentLerpTime(0.f),
		LerpSpeed(0.f),
		OnGripTransform(FTransform::Identity),
		bIsLerping(false)
	{
	}	

};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPGripPair
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripPair")
	UGripMotionControllerComponent * HoldingController;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GripPair")
	uint8 GripID;

	FBPGripPair() :
		HoldingController(nullptr),
		GripID(INVALID_VRGRIP_ID)
	{}

	FBPGripPair(UGripMotionControllerComponent * Controller, uint8 ID) :
		HoldingController(Controller),
		GripID(ID)
	{}

	void Clear()
	{
		HoldingController = nullptr;
		GripID = INVALID_VRGRIP_ID;
	}

	bool IsValid()
	{
		return HoldingController != nullptr && GripID != INVALID_VRGRIP_ID;
	}

	FORCEINLINE bool operator==(const FBPGripPair & Other) const
	{
		return (Other.HoldingController == HoldingController && ((GripID != INVALID_VRGRIP_ID) && (GripID == Other.GripID)));
	}

	FORCEINLINE bool operator==(const UGripMotionControllerComponent * Other) const
	{
		return (Other == HoldingController);
	}

	FORCEINLINE bool operator==(const uint8 & Other) const
	{
		return GripID == Other;
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
		bool bAllowMultipleGrips;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripInterfaceTeleportBehavior OnTeleportBehavior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		bool bSimulateOnDrop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripCollisionType SlotDefaultGripType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripCollisionType FreeDefaultGripType;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float ConstraintBreakDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float SecondarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float PrimarySlotRange;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface|AdvancedGripSettings")
		FBPAdvGripSettings AdvancedGripSettings;

	UPROPERTY(BlueprintReadWrite, NotReplicated, Category = "VRGripInterface")
		bool bIsHeld; // Set on grip notify, not net serializing

	// If this grip was ever held
	bool bWasHeld;;

	UPROPERTY(BlueprintReadWrite, NotReplicated, Category = "VRGripInterface")
		TArray<FBPGripPair> HoldingControllers; // Set on grip notify, not net serializing

	FBPInterfaceProperties():
		bDenyGripping(false),
		bAllowMultipleGrips(false),
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
		bIsHeld(false),
		bWasHeld(false)
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
	uint8 GripID;
	bool bIsPaused;

	FPhysicsActorHandle KinActorData2;
	FPhysicsConstraintHandle HandleData2;
	FLinearDriveConstraint LinConstraint;
	FAngularDriveConstraint AngConstraint;

	FTransform LastPhysicsTransform;
	FTransform COMPosition;
	FTransform RootBoneRotation;

	bool bSetCOM;
	bool bSkipResettingCom;
	bool bSkipMassCheck;
	bool bSkipDeletingKinematicActor;
	bool bInitiallySetup;

	FBPActorPhysicsHandleInformation()
	{	
		HandledObject = nullptr;
		LastPhysicsTransform = FTransform::Identity;
		COMPosition = FTransform::Identity;
		GripID = INVALID_VRGRIP_ID;
		bIsPaused = false;
		RootBoneRotation = FTransform::Identity;
		bSetCOM = false;
		bSkipResettingCom = false;
		bSkipMassCheck = false;
		bSkipDeletingKinematicActor = false;
		bInitiallySetup = false;
#if WITH_CHAOS
		KinActorData2 = nullptr;
#endif
	}

	FORCEINLINE bool operator==(const FBPActorGripInformation & Other) const
	{
		return ((GripID != INVALID_VRGRIP_ID) && (GripID == Other.GripID));
	}

	FORCEINLINE bool operator==(const uint8 & Other) const
	{
		return ((GripID != INVALID_VRGRIP_ID) && (GripID == Other));
	}

};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPAdvancedPhysicsHandleAxisSettings
{
	GENERATED_BODY()
public:
	/** The spring strength of the drive. Force proportional to the position error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Constraint, meta = (ClampMin = "0.0"))
		float Stiffness;

	/** The damping strength of the drive. Force proportional to the velocity error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Constraint, meta = (ClampMin = "0.0"))
		float Damping;

	// A multiplier to add to the stiffness that is then set as the MaxForce
	// It is clamped between 0.00 and 256.00 to save in replication cost, a value of 0 will mean max force is infinite as it will multiply it to zero (legacy behavior)
	// If you want an exact value you can figure it out as a factor of the stiffness
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PhysicsSettings", meta = (ClampMin = "0.00", UIMin = "0.00", ClampMax = "256.00", UIMax = "256.00"))
		float MaxForceCoefficient;

	/** Enables/Disables position drive (orientation if using angular drive)*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Constraint)
		bool bEnablePositionDrive;

	/** Enables/Disables velocity drive (damping) (angular velocity if using angular drive) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Constraint)
		bool bEnableVelocityDrive;

	FBPAdvancedPhysicsHandleAxisSettings()
	{
		Stiffness = 0.f;
		Damping = 0.f;
		MaxForceCoefficient = 0.f;
		bEnablePositionDrive = false;
		bEnableVelocityDrive = false;
	}

	void FillFrom(FConstraintDrive& ConstraintDrive)
	{
		Damping = ConstraintDrive.Damping;
		Stiffness = ConstraintDrive.Stiffness;
		MaxForceCoefficient = ConstraintDrive.MaxForce / Stiffness;
		bEnablePositionDrive = ConstraintDrive.bEnablePositionDrive;
		bEnableVelocityDrive = ConstraintDrive.bEnableVelocityDrive;
	}

	void FillTo(FConstraintDrive& ConstraintDrive) const
	{
		ConstraintDrive.Damping = Damping;
		ConstraintDrive.Stiffness = Stiffness;
		ConstraintDrive.MaxForce = MaxForceCoefficient * Stiffness;
		ConstraintDrive.bEnablePositionDrive = bEnablePositionDrive;
		ConstraintDrive.bEnableVelocityDrive = bEnableVelocityDrive;
	}

};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPAdvancedPhysicsHandleSettings
{
	GENERATED_BODY()
public:

	// The settings for the XAxis
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Constraint Settings")
		FBPAdvancedPhysicsHandleAxisSettings XAxisSettings;

	// The settings for the YAxis
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Constraint Settings")
		FBPAdvancedPhysicsHandleAxisSettings YAxisSettings;

	// The settings for the ZAxis
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Linear Constraint Settings")
		FBPAdvancedPhysicsHandleAxisSettings ZAxisSettings;

	// The settings for the Orientation (Slerp only for now)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Angular Constraint Settings")
		FBPAdvancedPhysicsHandleAxisSettings SlerpSettings;

	// The settings for the Orientation (Slerp only for now)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Angular Constraint Settings")
		FBPAdvancedPhysicsHandleAxisSettings TwistSettings;

	// The settings for the Orientation (Slerp only for now)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Angular Constraint Settings")
		FBPAdvancedPhysicsHandleAxisSettings SwingSettings;


	// FConstraintSettings // settings for various things like distance limits
	// Add a deletegate bindable in the motion controller

	bool FillFrom(FBPActorPhysicsHandleInformation* HandleInfo)
	{
		if (!HandleInfo)
			return false;

		XAxisSettings.FillFrom(HandleInfo->LinConstraint.XDrive);
		YAxisSettings.FillFrom(HandleInfo->LinConstraint.YDrive);
		ZAxisSettings.FillFrom(HandleInfo->LinConstraint.ZDrive);

		SlerpSettings.FillFrom(HandleInfo->AngConstraint.SlerpDrive);
		TwistSettings.FillFrom(HandleInfo->AngConstraint.TwistDrive);
		SwingSettings.FillFrom(HandleInfo->AngConstraint.SwingDrive);

		return true;
	}

	bool FillTo(FBPActorPhysicsHandleInformation* HandleInfo) const
	{
		if (!HandleInfo)
			return false;

		XAxisSettings.FillTo(HandleInfo->LinConstraint.XDrive);
		YAxisSettings.FillTo(HandleInfo->LinConstraint.YDrive);
		ZAxisSettings.FillTo(HandleInfo->LinConstraint.ZDrive);

		if ((SlerpSettings.bEnablePositionDrive || SlerpSettings.bEnableVelocityDrive))
		{
			HandleInfo->AngConstraint.AngularDriveMode = EAngularDriveMode::SLERP;
			SlerpSettings.FillTo(HandleInfo->AngConstraint.SlerpDrive);
		}
		else
		{
			HandleInfo->AngConstraint.AngularDriveMode = EAngularDriveMode::TwistAndSwing;
			TwistSettings.FillTo(HandleInfo->AngConstraint.TwistDrive);
			SwingSettings.FillTo(HandleInfo->AngConstraint.SwingDrive);
		}

		return true;
	}
};