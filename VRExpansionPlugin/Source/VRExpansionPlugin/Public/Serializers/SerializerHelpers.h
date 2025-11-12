#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"

// Helpers for Iris serialization




namespace UE::Net
{
	// -----------------------------------------------------------------------------
	// Fixed-compression helpers for 0–MaxValue floats with BitCount precision
	// Epic doesn't have per float compression helpers yet for Iris serializers
	// -----------------------------------------------------------------------------

	// Based on Epics FixedCompressionFloat functions for std archives

	template<int32 MaxValue, uint32 NumBits>
	uint32 GetCompressedFloat(const float Value)
	{
		using Details = TFixedCompressedFloatDetails<MaxValue, NumBits>;

		bool clamp = false;
		int64 ScaledValue;
		if (MaxValue > Details::MaxBitValue)
		{
			// We have to scale this down
			const float Scale = float(Details::MaxBitValue) / MaxValue;
			ScaledValue = FMath::TruncToInt(Scale * Value);
		}
		else
		{
			// We will scale up to get extra precision. But keep is a whole number preserve whole values
			constexpr int32 Scale = Details::MaxBitValue / MaxValue;
			ScaledValue = FMath::RoundToInt(Scale * Value);
		}

		uint32 Delta = static_cast<uint32>(ScaledValue + Details::Bias);

		if (Delta > Details::MaxDelta)
		{
			clamp = true;
			Delta = static_cast<int32>(Delta) > 0 ? Details::MaxDelta : 0;
		}

		return Delta;
	}

	template<int32 MaxValue, uint32 NumBits>
	float GetDecompressedFloat(uint32 Delta)
	{
		using Details = TFixedCompressedFloatDetails<MaxValue, NumBits>;

		float Value = 0.0f;

		float UnscaledValue = static_cast<float>(static_cast<int32>(Delta) - Details::Bias);

		if constexpr (MaxValue > Details::MaxBitValue)
		{
			// We have to scale down, scale needs to be a float:
			constexpr float InvScale = MaxValue / (float)Details::MaxBitValue;
			Value = UnscaledValue * InvScale;
		}
		else
		{
			constexpr int32 Scale = Details::MaxBitValue / MaxValue;
			constexpr float InvScale = float(1) / (float)Scale;

			Value = UnscaledValue * InvScale;
		}

		return Value;
	}

	template<int32 ScaleFactor, int32 MaxBitsPerComponent>
	bool WritePackedIrisFloat2(float X, float Y, FNetBitStreamWriter* Ar)	// Note Value is intended to not be a reference since we are scaling it before serializing!
	{
		FVector2D Value(X, Y);

		// Scale vector by quant factor first
		Value[0] *= ScaleFactor;
		Value[1] *= ScaleFactor;

		float MinV = -1073741824.0f;
		float MaxV = 1073741760.0f;

		// Some platforms have RoundToInt implementations that essentially reduces the allowed inputs to 2^31.
		const FVector2D ClampedValue = FVector2D(FMath::Clamp(Value.X, MinV, MaxV), FMath::Clamp(Value.Y, MinV, MaxV));
		bool bClamp = ClampedValue != Value;

		// Do basically FVector::SerializeCompressed
		int32 IntX = FMath::RoundToInt(ClampedValue.X);
		int32 IntY = FMath::RoundToInt(ClampedValue.Y);

		uint32 Bits = FMath::Clamp<uint32>(FMath::CeilLogTwo(1 + FMath::Max(FMath::Abs(IntX), FMath::Abs(IntY))), 1, MaxBitsPerComponent) - 1;

		// Serialize how many bits each component will have
		Ar->WriteBits(Bits, MaxBitsPerComponent);
		//Ar.SerializeInt(Bits, MaxBitsPerComponent);

		int32  Bias = 1 << (Bits + 1);
		uint32 Max = 1 << (Bits + 2);
		uint32 DX = IntX + Bias;
		uint32 DY = IntY + Bias;

		if (DX >= Max) { bClamp = true; DX = static_cast<int32>(DX) > 0 ? Max - 1 : 0; }
		if (DY >= Max) { bClamp = true; DY = static_cast<int32>(DY) > 0 ? Max - 1 : 0; }

		Ar->WriteBits(DX, Max);
		Ar->WriteBits(DY, Max);

		return !bClamp;
	}

	template<uint32 ScaleFactor, int32 MaxBitsPerComponent>
	bool ReadPackedIrisFloat2(float &X, float &Y, FNetBitStreamReader* Ar)
	{
		uint32 Bits = 0;

		// Serialize how many bits each component will have
		Bits = Ar->ReadBits(MaxBitsPerComponent);
		//Ar.SerializeInt(Bits, MaxBitsPerComponent);

		int32  Bias = 1 << (Bits + 1);
		uint32 Max = 1 << (Bits + 2);
		uint32 DX = 0;
		uint32 DY = 0;

		DX = Ar->ReadBits(Max);
		DY = Ar->ReadBits(Max);
		//Ar.SerializeInt(DX, Max);
		//Ar.SerializeInt(DY, Max);


		float fact = (float)ScaleFactor;

		X = (float)(static_cast<int32>(DX) - Bias) / fact;
		Y = (float)(static_cast<int32>(DY) - Bias) / fact;

		return true;
	}
}