// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"

#include "PhysicsPublic.h"
#include "PhysicsEngine/ConstraintDrives.h"

#include "VRBPDatatypes.generated.h"

class UGripMotionControllerComponent;
class UVRGripScriptBase;

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