// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "IMotionController.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"

//#include "HeadMountedDisplay.h" 
#include "HeadMountedDisplayFunctionLibrary.h"
//#include "HeadMountedDisplayFunctionLibrary.h"
#include "IHeadMountedDisplay.h"

#include "VRBPDatatypes.h"
#include "XRMotionControllerBase.h" // for GetHandEnumForSourceName()

#include "VRExpansionFunctionLibrary.generated.h"

//General Advanced Sessions Log
DECLARE_LOG_CATEGORY_EXTERN(VRExpansionFunctionLibraryLog, Log, All);

/**
* Redefining this for blueprint as it wasn't declared as BlueprintType
* Stores if the user is wearing the HMD or not. For HMDs without a sensor to detect the user wearing it, the state defaults to Unknown.
*/
UENUM(BlueprintType)
enum class EBPHMDWornState : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	Worn UMETA(DisplayName = "Worn"),
	NotWorn UMETA(DisplayName = "Not Worn"),
};

UCLASS()//, meta = (BlueprintSpawnableComponent))
class VREXPANSIONPLUGIN_API UVRExpansionFunctionLibrary : public UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_BODY()
	//~UVRExpansionFunctionLibrary();
public:

	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetHandFromMotionSourceName"))
	static bool GetHandFromMotionSourceName(FName MotionSource, EControllerHand& Hand)
	{
		Hand = EControllerHand::Left;
		if (FXRMotionControllerBase::GetHandEnumForSourceName(MotionSource, Hand))
		{
			return true;
		}

		return false;
	}

	// Gets the unwound yaw of the HMD
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetHMDPureYaw"))
	static FRotator GetHMDPureYaw(FRotator HMDRotation);

	inline static FRotator GetHMDPureYaw_I(FRotator HMDRotation)
	{
		// Took this from UnityVRToolkit, no shame, I liked it
		FRotationMatrix HeadMat(HMDRotation);
		FVector forward = HeadMat.GetScaledAxis(EAxis::X);
		FVector forwardLeveled = forward;
		forwardLeveled.Z = 0;
		forwardLeveled.Normalize();
		FVector mixedInLocalForward = HeadMat.GetScaledAxis(EAxis::Z);

		if (forward.Z > 0)
		{
			mixedInLocalForward = -mixedInLocalForward;
		}

		mixedInLocalForward.Z = 0;
		mixedInLocalForward.Normalize();
		float dot = FMath::Clamp(FVector::DotProduct(forwardLeveled, forward), 0.0f, 1.0f);
		FVector finalForward = FMath::Lerp(mixedInLocalForward, forwardLeveled, dot * dot);

		return FRotationMatrix::MakeFromXZ(finalForward, FVector::UpVector).Rotator();
	}

	// Applies a delta rotation around a pivot point, if bUseOriginalYawOnly is true then it only takes the original Yaw into account (characters)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "RotateAroundPivot"))
	static void RotateAroundPivot(FRotator RotationDelta, FVector OriginalLocation, FRotator OriginalRotation, FVector PivotPoint, FVector & NewLocation, FRotator & NewRotation,bool bUseOriginalYawOnly = true)
	{		
		if (bUseOriginalYawOnly)
		{
			// Keep original pitch/roll
			NewRotation.Pitch = OriginalRotation.Pitch;
			NewRotation.Roll = OriginalRotation.Roll;

			// Throw out pitch/roll before calculating offset
			OriginalRotation.Roll = 0;
			OriginalRotation.Pitch = 0;

			// Offset to pivot point
			NewLocation = OriginalLocation + OriginalRotation.RotateVector(PivotPoint);

			// Combine rotations
			OriginalRotation.Yaw = (OriginalRotation.Quaternion() * RotationDelta.Quaternion()).Rotator().Yaw;
			NewRotation.Yaw = OriginalRotation.Yaw;

			// Remove pivot point offset
			NewLocation -= OriginalRotation.RotateVector(PivotPoint);

		}
		else
		{
			NewLocation = OriginalLocation + OriginalRotation.RotateVector(PivotPoint);
			NewRotation = (OriginalRotation.Quaternion() * RotationDelta.Quaternion()).Rotator();
			NewLocation -= NewRotation.RotateVector(PivotPoint);
		}
	}

	// Returns a delta rotation to have Vec1 point towards Vec2, assumes that the v
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "FindBetween"))
		static FRotator BPQuat_FindBetween(FVector Vec1, FVector Vec2)
	{
		return FQuat::FindBetween(Vec1, Vec2).Rotator();
	}

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetIsHMDConnected"))
	static bool GetIsHMDConnected();

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetIsHMDWorn"))
	static EBPHMDWornState GetIsHMDWorn();

	// Gets whether the game is running in VRPreview or is a non editor build game (returns true for either).
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "IsInVREditorPreviewOrGame"))
	static bool IsInVREditorPreviewOrGame();

	// Gets whether the game is running in VRPreview
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "IsInVREditorPreview"))
	static bool IsInVREditorPreview();

	/**
	* Finds the minimum area rectangle that encloses all of the points in InVerts
	* Engine default version is server only for some reason
	* Uses algorithm found in http://www.geometrictools.com/Documentation/MinimumAreaRectangle.pdf
	*
	* @param		InVerts	- Points to enclose in the rectangle
	* @outparam	OutRectCenter - Center of the enclosing rectangle
	* @outparam	OutRectSideA - Vector oriented and sized to represent one edge of the enclosing rectangle, orthogonal to OutRectSideB
	* @outparam	OutRectSideB - Vector oriented and sized to represent one edge of the enclosing rectangle, orthogonal to OutRectSideA
	*/
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions", meta = (WorldContext = "WorldContextObject", CallableWithoutWorldContext))
	static void NonAuthorityMinimumAreaRectangle(UObject* WorldContextObject, const TArray<FVector>& InVerts, const FVector& SampleSurfaceNormal, FVector& OutRectCenter, FRotator& OutRectRotation, float& OutSideLengthX, float& OutSideLengthY, bool bDebugDraw = false);

	// A Rolling average low pass filter
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "LowPassFilter_RollingAverage"))
	static void LowPassFilter_RollingAverage(FVector lastAverage, FVector newSample, FVector & newAverage, int32 numSamples = 10);

	// A exponential low pass filter
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "LowPassFilter_Exponential"))
	static void LowPassFilter_Exponential(FVector lastAverage, FVector newSample, FVector & newAverage, float sampleFactor = 0.25f);

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetIsActorMovable"))
	static bool GetIsActorMovable(AActor * ActorToCheck);

	/** Make a transform net quantize from location, rotation and scale */
	UFUNCTION(BlueprintPure, meta = (Scale = "1,1,1", Keywords = "construct build", NativeMakeFunc), Category = "VRExpansionLibrary|TransformNetQuantize")
		static FTransform_NetQuantize MakeTransform_NetQuantize(FVector Location, FRotator Rotation, FVector Scale);

	/** Breaks apart a transform net quantize into location, rotation and scale */
	UFUNCTION(BlueprintPure, Category = "VRExpansionLibrary|TransformNetQuantize", meta = (NativeBreakFunc))
		static void BreakTransform_NetQuantize(const FTransform_NetQuantize& InTransform, FVector& Location, FRotator& Rotation, FVector& Scale);

	/** Converts a FTransform into a FTransform_NetQuantize */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToTransform_NetQuantize (Transform)", CompactNodeTitle = "->", BlueprintAutocast), Category = "VRExpansionLibrary|TransformNetQuantize")
		static FTransform_NetQuantize Conv_TransformToTransformNetQuantize(const FTransform &InTransform);

	/** Converts a vector into a FVector_NetQuantize */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToVector_NetQuantize (Vector)", CompactNodeTitle = "->", BlueprintAutocast), Category = "VRExpansionLibrary|FVectorNetQuantize")
		static FVector_NetQuantize Conv_FVectorToFVectorNetQuantize(const FVector &InVector);

	/** Make a transform net quantize from location, rotation and scale */
	UFUNCTION(BlueprintPure, meta = (Scale = "1,1,1", Keywords = "construct build", NativeMakeFunc), Category = "VRExpansionLibrary|FVectorNetQuantize")
		static FVector_NetQuantize MakeVector_NetQuantize(FVector InVector);

	/** Converts a vector into a FVector_NetQuantize10 */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToVector_NetQuantize10 (Vector)", CompactNodeTitle = "->", BlueprintAutocast), Category = "VRExpansionLibrary|FVectorNetQuantize")
		static FVector_NetQuantize10 Conv_FVectorToFVectorNetQuantize10(const FVector &InVector);

	/** Make a transform net quantize10 from location, rotation and scale */
	UFUNCTION(BlueprintPure, meta = (Scale = "1,1,1", Keywords = "construct build", NativeMakeFunc), Category = "VRExpansionLibrary|FVectorNetQuantize")
		static FVector_NetQuantize10 MakeVector_NetQuantize10(FVector InVector);

	/** Converts a vector into a FVector_NetQuantize100 */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToVector_NetQuantize100 (Vector)", CompactNodeTitle = "->", BlueprintAutocast), Category = "VRExpansionLibrary|FVectorNetQuantize")
		static FVector_NetQuantize100 Conv_FVectorToFVectorNetQuantize100(const FVector &InVector);

	/** Make a transform net quantize100 from location, rotation and scale */
	UFUNCTION(BlueprintPure, meta = (Scale = "1,1,1", Keywords = "construct build", NativeMakeFunc), Category = "VRExpansionLibrary|FVectorNetQuantize")
		static FVector_NetQuantize100 MakeVector_NetQuantize100(FVector InVector);

	// Adds a USceneComponent Subclass, that is based on the passed in Class, and added to the Outer(Actor) object
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Scene Component By Class"), Category = "VRExpansionLibrary")
		static USceneComponent* AddSceneComponentByClass(UObject* Outer, TSubclassOf<USceneComponent> Class, const FTransform & ComponentRelativeTransform);
	
	/** Resets a Filter so that the first time it is used again it is clean */
	UFUNCTION(BlueprintCallable, Category = "LowPassFilter_Peak")
		static void ResetPeakLowPassFilter(UPARAM(ref) FBPLowPassPeakFilter& TargetPeakFilter)
	{
		TargetPeakFilter.Reset();
	}

	/** Adds an entry to the Peak low pass filter */
	UFUNCTION(BlueprintCallable, Category = "LowPassFilter_Peak")
		static void UpdatePeakLowPassFilter(UPARAM(ref) FBPLowPassPeakFilter& TargetPeakFilter, FVector NewSample)
	{
		TargetPeakFilter.AddSample(NewSample);
	}

	/** Gets the peak value of the Peak Low Pass Filter */
	UFUNCTION(BlueprintCallable, Category = "LowPassFilter_Peak")
		static FVector GetPeak_PeakLowPassFilter(UPARAM(ref) FBPLowPassPeakFilter& TargetPeakFilter)
	{
		return TargetPeakFilter.GetPeak();
	}


	/** Resets a Euro Low Pass Filter so that the first time it is used again it is clean */
	UFUNCTION(BlueprintCallable, Category = "EuroLowPassFilter")
	static void ResetEuroSmoothingFilter(UPARAM(ref) FBPEuroLowPassFilter& TargetEuroFilter)
	{
		TargetEuroFilter.ResetSmoothingFilter();
	}

	/** Runs the smoothing function of a Euro Low Pass Filter */
	UFUNCTION(BlueprintCallable, Category = "EuroLowPassFilter")
	static void RunEuroSmoothingFilter(UPARAM(ref) FBPEuroLowPassFilter& TargetEuroFilter, FVector InRawValue, const float DeltaTime, FVector & SmoothedValue)
	{
		SmoothedValue = TargetEuroFilter.RunFilterSmoothing(InRawValue, DeltaTime);
	}

	// Applies the same laser smoothing that the vr editor uses to an array of points
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Smooth Update Laser Spline"), Category = "VRExpansionLibrary")
	static void SmoothUpdateLaserSpline(USplineComponent * LaserSplineComponent, TArray<USplineMeshComponent *> LaserSplineMeshComponents, FVector InStartLocation, FVector InEndLocation, FVector InForward, float LaserRadius)
	{
		if (LaserSplineComponent == nullptr)
			return;

		LaserSplineComponent->ClearSplinePoints();

		const FVector SmoothLaserDirection = InEndLocation - InStartLocation;
		float Distance = SmoothLaserDirection.Size();
		const FVector StraightLaserEndLocation = InStartLocation + (InForward * Distance);
		const int32 NumLaserSplinePoints = LaserSplineMeshComponents.Num();

		LaserSplineComponent->AddSplinePoint(InStartLocation, ESplineCoordinateSpace::World, false);
		for (int32 Index = 1; Index < NumLaserSplinePoints; Index++)
		{
			float Alpha = (float)Index / (float)NumLaserSplinePoints;
			Alpha = FMath::Sin(Alpha * PI * 0.5f);
			const FVector PointOnStraightLaser = FMath::Lerp(InStartLocation, StraightLaserEndLocation, Alpha);
			const FVector PointOnSmoothLaser = FMath::Lerp(InStartLocation, InEndLocation, Alpha);
			const FVector PointBetweenLasers = FMath::Lerp(PointOnStraightLaser, PointOnSmoothLaser, Alpha);
			LaserSplineComponent->AddSplinePoint(PointBetweenLasers, ESplineCoordinateSpace::World, false);
		}
		LaserSplineComponent->AddSplinePoint(InEndLocation, ESplineCoordinateSpace::World, false);
		
		// Update all the segments of the spline
		LaserSplineComponent->UpdateSpline();

		const float LaserPointerRadius = LaserRadius;
		Distance *= 0.0001f;
		for (int32 Index = 0; Index < NumLaserSplinePoints; Index++)
		{
			USplineMeshComponent* SplineMeshComponent = LaserSplineMeshComponents[Index];
			check(SplineMeshComponent != nullptr);

			FVector StartLoc, StartTangent, EndLoc, EndTangent;
			LaserSplineComponent->GetLocationAndTangentAtSplinePoint(Index, StartLoc, StartTangent, ESplineCoordinateSpace::Local);
			LaserSplineComponent->GetLocationAndTangentAtSplinePoint(Index + 1, EndLoc, EndTangent, ESplineCoordinateSpace::Local);

			const float AlphaIndex = (float)Index / (float)NumLaserSplinePoints;
			const float AlphaDistance = Distance * AlphaIndex;
			float Radius = LaserPointerRadius * ((AlphaIndex * AlphaDistance) + 1);
			FVector2D LaserScale(Radius, Radius);
			SplineMeshComponent->SetStartScale(LaserScale, false);

			const float NextAlphaIndex = (float)(Index + 1) / (float)NumLaserSplinePoints;
			const float NextAlphaDistance = Distance * NextAlphaIndex;
			Radius = LaserPointerRadius * ((NextAlphaIndex * NextAlphaDistance) + 1);
			LaserScale = FVector2D(Radius, Radius);
			SplineMeshComponent->SetEndScale(LaserScale, false);

			SplineMeshComponent->SetStartAndEnd(StartLoc, StartTangent, EndLoc, EndTangent, true);
		}

	}

};	


