// Fill out your copyright notice in the Description page of Project Settings.

#include "VRBPDatatypes.h"

namespace VRDataTypeCVARs
{
	// Doing it this way because I want as little rep and perf impact as possible and sampling a static var is that.
	// This is to specifically help out very rare cases, DON'T USE THIS UNLESS YOU HAVE NO CHOICE
	static int32 RepHighPrecisionTransforms = 0;
	FAutoConsoleVariableRef CVarRepHighPrecisionTransforms(
		TEXT("vrexp.RepHighPrecisionTransforms"),
		RepHighPrecisionTransforms,
		TEXT("When on, will rep Quantized transforms at full precision, WARNING use at own risk, if this isn't the same setting client & server then it will crash.\n")
		TEXT("0: Disable, 1: Enable"),
		ECVF_Default);
}

bool FTransform_NetQuantize::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;

	FVector rTranslation;
	FVector rScale3D;
	//FQuat rRotation;
	FRotator rRotation;

	uint16 ShortPitch = 0;
	uint16 ShortYaw = 0;
	uint16 ShortRoll = 0;

	bool bUseHighPrecision = VRDataTypeCVARs::RepHighPrecisionTransforms > 0;

	if (Ar.IsSaving())
	{
		// Because transforms can be vectorized or not, need to use the inline retrievers
		rTranslation = this->GetTranslation();
		rScale3D = this->GetScale3D();
		rRotation = this->Rotator();//this->GetRotation();

		if (bUseHighPrecision)
		{
			Ar << rTranslation;
			Ar << rScale3D;
			Ar << rRotation;
		}
		else
		{
			// Translation set to 2 decimal precision
			bOutSuccess &= SerializePackedVector<100, 30>(rTranslation, Ar);

			// Scale set to 2 decimal precision, had it 1 but realized that I used two already even
			bOutSuccess &= SerializePackedVector<100, 30>(rScale3D, Ar);

			// Rotation converted to FRotator and short compressed, see below for conversion reason
			// FRotator already serializes compressed short by default but I can save a func call here
			rRotation.SerializeCompressedShort(Ar);
		}


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

		if (bUseHighPrecision)
		{
			Ar << rTranslation;
			Ar << rScale3D;
			Ar << rRotation;
		}
		else
		{
			bOutSuccess &= SerializePackedVector<100, 30>(rTranslation, Ar);
			bOutSuccess &= SerializePackedVector<100, 30>(rScale3D, Ar);
			rRotation.SerializeCompressedShort(Ar);
		}

		//Ar << rRotation;

		// Set it
		this->SetComponents(rRotation.Quaternion(), rTranslation, rScale3D);
		this->NormalizeRotation();
	}

	return bOutSuccess;
}

// ** Euro Low Pass Filter ** //

void FBPEuroLowPassFilter::ResetSmoothingFilter()
{
	RawFilter.bFirstTime = true;
	DeltaFilter.bFirstTime = true;
}

FVector FBPEuroLowPassFilter::RunFilterSmoothing(const FVector &InRawValue, const float &InDeltaTime)
{
	// Calculate the delta, if this is the first time then there is no delta
	const FVector Delta = RawFilter.bFirstTime == true ? FVector::ZeroVector : (InRawValue - RawFilter.PreviousRaw) * 1.0f / InDeltaTime;

	// Filter the delta to get the estimated
	const FVector Estimated = DeltaFilter.Filter(Delta, FVector(DeltaFilter.CalculateAlphaTau(DeltaCutoff, InDeltaTime)));

	// Use the estimated to calculate the cutoff
	const FVector Cutoff = DeltaFilter.CalculateCutoff(Estimated, MinCutoff, CutoffSlope);

	// Filter passed value 
	return RawFilter.Filter(InRawValue, RawFilter.CalculateAlpha(Cutoff, InDeltaTime));
}

void FBPEuroLowPassFilterQuat::ResetSmoothingFilter()
{
	RawFilter.bFirstTime = true;
	DeltaFilter.bFirstTime = true;
}

FQuat FBPEuroLowPassFilterQuat::RunFilterSmoothing(const FQuat& InRawValue, const float& InDeltaTime)
{

	FQuat NewInVal = InRawValue;
	if (!RawFilter.bFirstTime)
	{
		// fix axial flipping, from unity open 1 Euro implementation
		FVector4 PrevQuatAsVector(RawFilter.Previous.X, RawFilter.Previous.Y, RawFilter.Previous.Z, RawFilter.Previous.W);
		FVector4 CurrQuatAsVector(InRawValue.X, InRawValue.Y, InRawValue.Z, InRawValue.W);
		if ((PrevQuatAsVector - CurrQuatAsVector).SizeSquared() > 2)
			NewInVal = FQuat(-InRawValue.X, -InRawValue.Y, -InRawValue.Z, -InRawValue.W);
	}

	// Calculate the delta, if this is the first time then there is no delta
	FQuat Delta = FQuat::Identity;

	if (!RawFilter.bFirstTime)
	{
		Delta = (NewInVal - RawFilter.PreviousRaw) * (1.0f / InDeltaTime);
	}


	float AlphaTau = DeltaFilter.CalculateAlphaTau(DeltaCutoff, InDeltaTime);
	FQuat AlphaTauQ(AlphaTau, AlphaTau, AlphaTau, AlphaTau);
	const FQuat Estimated = DeltaFilter.Filter(Delta, AlphaTauQ);

	// Use the estimated to calculate the cutoff
	const FQuat Cutoff = DeltaFilter.CalculateCutoff(Estimated, MinCutoff, CutoffSlope);

	// Filter passed value 
	return RawFilter.Filter(NewInVal, RawFilter.CalculateAlpha(Cutoff, InDeltaTime)).GetNormalized();// .GetNormalized();
}

void FBPEuroLowPassFilterTrans::ResetSmoothingFilter()
{
	RawFilter.bFirstTime = true;
	DeltaFilter.bFirstTime = true;
}

FTransform FBPEuroLowPassFilterTrans::RunFilterSmoothing(const FTransform& InRawValue, const float& InDeltaTime)
{

	FTransform NewInVal = InRawValue;
	if (!RawFilter.bFirstTime)
	{
		FQuat TransQ = NewInVal.GetRotation();
		FQuat PrevQ = RawFilter.Previous.GetRotation();
		// fix axial flipping, from unity open 1 Euro implementation
		FVector4 PrevQuatAsVector(PrevQ.X, PrevQ.Y, PrevQ.Z, PrevQ.W);
		FVector4 CurrQuatAsVector(TransQ.X, TransQ.Y, TransQ.Z, TransQ.W);
		if ((PrevQuatAsVector - CurrQuatAsVector).SizeSquared() > 2)
			NewInVal.SetRotation(FQuat(-TransQ.X, -TransQ.Y, -TransQ.Z, -TransQ.W));
	}

	// Calculate the delta, if this is the first time then there is no delta
	FTransform Delta = FTransform::Identity;

	float Frequency = 1.0f / InDeltaTime;
	if (!RawFilter.bFirstTime)
	{
		Delta.SetLocation((NewInVal.GetLocation() - RawFilter.PreviousRaw.GetLocation()) * Frequency);
		Delta.SetRotation((NewInVal.GetRotation() - RawFilter.PreviousRaw.GetRotation()) * Frequency);
		Delta.SetScale3D((NewInVal.GetScale3D() - RawFilter.PreviousRaw.GetScale3D()) * Frequency);
	}


	float AlphaTau = DeltaFilter.CalculateAlphaTau(DeltaCutoff, InDeltaTime);
	FTransform AlphaTauQ(FQuat(AlphaTau, AlphaTau, AlphaTau, AlphaTau), FVector(AlphaTau), FVector(AlphaTau));
	const FTransform Estimated = DeltaFilter.Filter(Delta, AlphaTauQ);

	// Use the estimated to calculate the cutoff
	const FTransform Cutoff = DeltaFilter.CalculateCutoff(Estimated, MinCutoff, CutoffSlope);

	FTransform NewTrans = RawFilter.Filter(NewInVal, RawFilter.CalculateAlpha(Cutoff, InDeltaTime));
	NewTrans.NormalizeRotation();
	// Filter passed value 
	return NewTrans;
}
