// Fill out your copyright notice in the Description page of Project Settings.
#include "VRExpansionFunctionLibrary.h"
#include "DrawDebugHelpers.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

//General Log
DEFINE_LOG_CATEGORY(VRExpansionFunctionLibraryLog);

void UVRExpansionFunctionLibrary::LowPassFilter_RollingAverage(FVector lastAverage, FVector newSample, FVector & newAverage, int32 numSamples)
{
	newAverage = lastAverage;
	newAverage -= newAverage / numSamples;
	newAverage += newSample / numSamples;
}

void UVRExpansionFunctionLibrary::LowPassFilter_Exponential(FVector lastAverage, FVector newSample, FVector & newAverage, float sampleFactor)
{
	newAverage = (newSample * sampleFactor) + ((1 - sampleFactor) * lastAverage);
}

bool UVRExpansionFunctionLibrary::GetIsActorMovable(AActor * ActorToCheck)
{
	if (!ActorToCheck)
		return false;

	if (USceneComponent * rootComp = ActorToCheck->GetRootComponent())
	{
		 return rootComp->Mobility == EComponentMobility::Movable;
	}

	return false;
}

void UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(FName SlotType, AActor * Actor, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	bHadSlotInRange = false;
	SlotWorldTransform = FTransform::Identity;

	if (!Actor)
		return;

	if (USceneComponent *rootComp = Actor->GetRootComponent())
	{
		float ClosestSlotDistance = -0.1f;

		TArray<FName> SocketNames = rootComp->GetAllSocketNames();

		FString GripIdentifier = SlotType.ToString();

		int foundIndex = 0;

		for (int i = 0; i < SocketNames.Num(); ++i)
		{
			if (SocketNames[i].ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
			{
				float vecLen = (rootComp->GetSocketLocation(SocketNames[i]) - WorldLocation).Size();

				if (MaxRange >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
				{
					ClosestSlotDistance = vecLen;
					bHadSlotInRange = true;
					foundIndex = i;
				}
			}
		}

		if (bHadSlotInRange)
		{
			SlotWorldTransform = rootComp->GetSocketTransform(SocketNames[foundIndex]);
		}
	}
}

void UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(FName SlotType, UPrimitiveComponent * Component, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform)
{
	bHadSlotInRange = false;
	SlotWorldTransform = FTransform::Identity;

	if (!Component)
		return;

	float ClosestSlotDistance = -0.1f;

	TArray<FName> SocketNames = Component->GetAllSocketNames();

	FString GripIdentifier = SlotType.ToString();

	int foundIndex = 0;

	for (int i = 0; i < SocketNames.Num(); ++i)
	{
		if (SocketNames[i].ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
		{
			float vecLen = (Component->GetSocketLocation(SocketNames[i]) - WorldLocation).Size();

			if (MaxRange >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
			{
				ClosestSlotDistance = vecLen;
				bHadSlotInRange = true;
				foundIndex = i;
			}
		}
	}

	if (bHadSlotInRange)
	{
		SlotWorldTransform = Component->GetSocketTransform(SocketNames[foundIndex]);
	}
}

FRotator UVRExpansionFunctionLibrary::GetHMDPureYaw(FRotator HMDRotation)
{
	return GetHMDPureYaw_I(HMDRotation);
}

EBPHMDWornState UVRExpansionFunctionLibrary::GetIsHMDWorn()
{
	if (GEngine && GEngine->HMDDevice.IsValid())
	{
		return ((EBPHMDWornState)GEngine->HMDDevice->GetHMDWornState());
	}

	return EBPHMDWornState::Unknown;
}

bool UVRExpansionFunctionLibrary::GetIsHMDConnected()
{
	if (GEngine && GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHMDConnected())
		return true;

	return false;
}

EBPHMDDeviceType UVRExpansionFunctionLibrary::GetHMDType()
{
	if (GEngine && GEngine->HMDDevice.IsValid())
	{
		switch (GEngine->HMDDevice->GetHMDDeviceType())
		{
		case EHMDDeviceType::DT_ES2GenericStereoMesh: return EBPHMDDeviceType::DT_ES2GenericStereoMesh; break;
		case EHMDDeviceType::DT_GearVR: return EBPHMDDeviceType::DT_GearVR; break;
		case EHMDDeviceType::DT_Morpheus: return EBPHMDDeviceType::DT_Morpheus; break;
		case EHMDDeviceType::DT_OculusRift: return EBPHMDDeviceType::DT_OculusRift; break;
		case EHMDDeviceType::DT_SteamVR: return EBPHMDDeviceType::DT_SteamVR; break;
		case EHMDDeviceType::DT_GoogleVR: return EBPHMDDeviceType::DT_GoogleVR; break;
	
		// Return unknown if not a matching enum, may need to add new entries in the copied enum if the original adds new ones in this case
		default: return EBPHMDDeviceType::DT_Unknown; break;
		}
	}

	return EBPHMDDeviceType::DT_Unknown;
}


bool UVRExpansionFunctionLibrary::IsInVREditorPreviewOrGame()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		
		UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine);
		return EdEngine->bUseVRPreviewForPlayWorld;
	}
#endif

	// Is not an editor build, default to true here
	return true;
}

void UVRExpansionFunctionLibrary::NonAuthorityMinimumAreaRectangle(class UObject* WorldContextObject, const TArray<FVector>& InVerts, const FVector& SampleSurfaceNormal, FVector& OutRectCenter, FRotator& OutRectRotation, float& OutSideLengthX, float& OutSideLengthY, bool bDebugDraw)
{
	float MinArea = -1.f;
	float CurrentArea = -1.f;
	FVector SupportVectorA, SupportVectorB;
	FVector RectSideA, RectSideB;
	float MinDotResultA, MinDotResultB, MaxDotResultA, MaxDotResultB;
	FVector TestEdge;
	float TestEdgeDot = 0.f;
	FVector PolyNormal(0.f, 0.f, 1.f);
	TArray<int32> PolyVertIndices;

	// Bail if we receive an empty InVerts array
	if (InVerts.Num() == 0)
	{
		return;
	}

	// Compute the approximate normal of the poly, using the direction of SampleSurfaceNormal for guidance
	PolyNormal = (InVerts[InVerts.Num() / 3] - InVerts[0]) ^ (InVerts[InVerts.Num() * 2 / 3] - InVerts[InVerts.Num() / 3]);
	if ((PolyNormal | SampleSurfaceNormal) < 0.f)
	{
		PolyNormal = -PolyNormal;
	}

	// Transform the sample points to 2D
	FMatrix SurfaceNormalMatrix = FRotationMatrix::MakeFromZX(PolyNormal, FVector(1.f, 0.f, 0.f));
	TArray<FVector> TransformedVerts;
	OutRectCenter = FVector(0.f);
	for (int32 Idx = 0; Idx < InVerts.Num(); ++Idx)
	{
		OutRectCenter += InVerts[Idx];
		TransformedVerts.Add(SurfaceNormalMatrix.InverseTransformVector(InVerts[Idx]));
	}
	OutRectCenter /= InVerts.Num();

	// Compute the convex hull of the sample points
	ConvexHull2D::ComputeConvexHull(TransformedVerts, PolyVertIndices);

	// Minimum area rectangle as computed by http://www.geometrictools.com/Documentation/MinimumAreaRectangle.pdf
	for (int32 Idx = 1; Idx < PolyVertIndices.Num() - 1; ++Idx)
	{
		SupportVectorA = (TransformedVerts[PolyVertIndices[Idx]] - TransformedVerts[PolyVertIndices[Idx - 1]]).GetSafeNormal();
		SupportVectorA.Z = 0.f;
		SupportVectorB.X = -SupportVectorA.Y;
		SupportVectorB.Y = SupportVectorA.X;
		SupportVectorB.Z = 0.f;
		MinDotResultA = MinDotResultB = MaxDotResultA = MaxDotResultB = 0.f;

		for (int TestVertIdx = 1; TestVertIdx < PolyVertIndices.Num(); ++TestVertIdx)
		{
			TestEdge = TransformedVerts[PolyVertIndices[TestVertIdx]] - TransformedVerts[PolyVertIndices[0]];
			TestEdgeDot = SupportVectorA | TestEdge;
			if (TestEdgeDot < MinDotResultA)
			{
				MinDotResultA = TestEdgeDot;
			}
			else if (TestEdgeDot > MaxDotResultA)
			{
				MaxDotResultA = TestEdgeDot;
			}

			TestEdgeDot = SupportVectorB | TestEdge;
			if (TestEdgeDot < MinDotResultB)
			{
				MinDotResultB = TestEdgeDot;
			}
			else if (TestEdgeDot > MaxDotResultB)
			{
				MaxDotResultB = TestEdgeDot;
			}
		}

		CurrentArea = (MaxDotResultA - MinDotResultA) * (MaxDotResultB - MinDotResultB);
		if (MinArea < 0.f || CurrentArea < MinArea)
		{
			MinArea = CurrentArea;
			RectSideA = SupportVectorA * (MaxDotResultA - MinDotResultA);
			RectSideB = SupportVectorB * (MaxDotResultB - MinDotResultB);
		}
	}

	RectSideA = SurfaceNormalMatrix.TransformVector(RectSideA);
	RectSideB = SurfaceNormalMatrix.TransformVector(RectSideB);
	OutRectRotation = FRotationMatrix::MakeFromZX(PolyNormal, RectSideA).Rotator();
	OutSideLengthX = RectSideA.Size();
	OutSideLengthY = RectSideB.Size();

#if ENABLE_DRAW_DEBUG
	if (bDebugDraw)
	{
		UWorld* World = (WorldContextObject) ? GEngine->GetWorldFromContextObject(WorldContextObject) : nullptr;
		if (World != nullptr)
		{
			DrawDebugSphere(World, OutRectCenter, 10.f, 12, FColor::Yellow, true);
			DrawDebugCoordinateSystem(World, OutRectCenter, SurfaceNormalMatrix.Rotator(), 100.f, true);
			DrawDebugLine(World, OutRectCenter - RectSideA * 0.5f + FVector(0, 0, 10.f), OutRectCenter + RectSideA * 0.5f + FVector(0, 0, 10.f), FColor::Green, true, -1, 0, 5.f);
			DrawDebugLine(World, OutRectCenter - RectSideB * 0.5f + FVector(0, 0, 10.f), OutRectCenter + RectSideB * 0.5f + FVector(0, 0, 10.f), FColor::Blue, true, -1, 0, 5.f);
		}
		else
		{
			FFrame::KismetExecutionMessage(TEXT("WorldContext required for MinimumAreaRectangle to draw a debug visualization."), ELogVerbosity::Warning);
		}
	}
#endif
}

bool UVRExpansionFunctionLibrary::EqualEqual_FBPActorGripInformation(const FBPActorGripInformation &A, const FBPActorGripInformation &B)
{
	return A == B;
}


FTransform_NetQuantize UVRExpansionFunctionLibrary::MakeTransform_NetQuantize(FVector Translation, FRotator Rotation, FVector Scale)
{
	return FTransform_NetQuantize(Rotation, Translation, Scale);
}

void UVRExpansionFunctionLibrary::BreakTransform_NetQuantize(const FTransform_NetQuantize& InTransform, FVector& Translation, FRotator& Rotation, FVector& Scale)
{
	Translation = InTransform.GetLocation();
	Rotation = InTransform.Rotator();
	Scale = InTransform.GetScale3D();
}

FTransform_NetQuantize UVRExpansionFunctionLibrary::Conv_TransformToTransformNetQuantize(const FTransform &InTransform)
{
	return FTransform_NetQuantize(InTransform);
}
