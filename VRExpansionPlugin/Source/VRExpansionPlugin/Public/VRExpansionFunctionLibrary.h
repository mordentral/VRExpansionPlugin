// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VRBPDatatypes.h"
#include "VRExpansionFunctionLibrary.generated.h"

class USplineComponent;
class UPrimitiveComponent;
class UGripMotioncontroller;
struct FGameplayTag;
struct FGameplayTagContainer;


enum class EControllerHand : uint8;
enum class EBPHMDDeviceType : uint8;

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

	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (WorldContext = "WorldContextObject", CallableWithoutWorldContext))
		static UGameViewportClient * GetGameViewportClient(UObject* WorldContextObject);


	// Sets two primitive components to ignore collision between two specific bodies
	// If bAddChildBones is true then it will also add all child bones of the given bone (or the entire skeleton if no name is given)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|Collision", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", CallableWithoutWorldContext))
		static void SetObjectsIgnoreCollision(UObject* WorldContextObject, UPrimitiveComponent* Prim1 = nullptr, FName OptionalBoneName1 = NAME_None, bool bAddChildBones1 = false, UPrimitiveComponent* Prim2 = nullptr, FName OptionalBoneName2 = NAME_None, bool bAddChildBones2 = false, bool bIgnoreCollision = true);

	// Sets two actors to entirely ignore collision between them
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|Collision", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", CallableWithoutWorldContext))
		static void SetActorsIgnoreAllCollision(UObject* WorldContextObject, AActor* Actor1 = nullptr, AActor* Actor2 = nullptr, bool bIgnoreCollision = true);

	// Removes all collision ignore matches for the given primitive object
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|Collision", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", CallableWithoutWorldContext))
		static void RemoveObjectCollisionIgnore(UObject* WorldContextObject, UPrimitiveComponent* Prim1);

	// Removes all collision ignore matches for the given actor
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|Collision", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", CallableWithoutWorldContext))
		static void RemoveActorCollisionIgnore(UObject* WorldContextObject, AActor* Actor1);

	// Returns if the component is ignoring any collisions
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|Collision", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", CallableWithoutWorldContext))
		static bool IsComponentIgnoringCollision(UObject* WorldContextObject, UPrimitiveComponent* Prim1);

	// Returns if the component is ignoring collisions with the specific component
	// This does not check per bone, but rather at scale if any part of them are ignoring each other
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|Collision", meta = (bIgnoreSelf = "true", WorldContext = "WorldContextObject", CallableWithoutWorldContext))
		static bool AreComponentsIgnoringCollisions(UObject* WorldContextObject, UPrimitiveComponent* Prim1, UPrimitiveComponent* Prim2);

	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetHandFromMotionSourceName"))
	static bool GetHandFromMotionSourceName(FName MotionSource, EControllerHand& Hand);

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

	// Gets whether an HMD device is connected
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions", meta = (bIgnoreSelf = "true", DisplayName = "GetHMDType"))
	static EBPHMDDeviceType GetHMDType();

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

	// Gets if an actors root component contains a grip slot within range
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (bIgnoreSelf = "true", DisplayName = "GetGripSlotInRangeByTypeName"))
	static void GetGripSlotInRangeByTypeName(FName SlotType, AActor * Actor, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent* QueryController = nullptr);

	// Gets if an actors root component contains a grip slot within range
	UFUNCTION(BlueprintPure, Category = "VRGrip", meta = (bIgnoreSelf = "true", DisplayName = "GetGripSlotInRangeByTypeName_Component"))
	static void GetGripSlotInRangeByTypeName_Component(FName SlotType, USceneComponent * Component, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent* QueryController = nullptr);

	/* Returns true if the values are equal (A == B) */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "Equal VR Grip", CompactNodeTitle = "==", Keywords = "== equal"), Category = "VRExpansionFunctions")
	static bool EqualEqual_FBPActorGripInformation(const FBPActorGripInformation &A, const FBPActorGripInformation &B);

	/* Returns true if the grip is active (both valid and not paused) */
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions")
		static bool IsActiveGrip(const FBPActorGripInformation& Grip);

	/* Returns the target primitive component if the grip has one (it should always), otherwise it will return invalid */
	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions")
		static UPrimitiveComponent* GetGripPrimitiveTarget(const FBPActorGripInformation& Grip);

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

	/** Converts a FBPGripPair into a MotionController */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToController (FBPGripPair)", CompactNodeTitle = "->", BlueprintAutocast), Category = "VRExpansionLibrary")
		static UGripMotionControllerComponent * Conv_GripPairToMotionController(const FBPGripPair &GripPair);

	/** Converts a FBPGripPair into a GripID */
	UFUNCTION(BlueprintPure, meta = (DisplayName = "ToGripID (FBPGripPair)", CompactNodeTitle = "->", BlueprintAutocast), Category = "VRExpansionLibrary")
		static uint8 Conv_GripPairToGripID(const FBPGripPair &GripPair);

	// Adds a USceneComponent Subclass, that is based on the passed in Class, and added to the Outer(Actor) object
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Add Scene Component By Class"), Category = "VRExpansionLibrary")
		static USceneComponent* AddSceneComponentByClass(UObject* Outer, TSubclassOf<USceneComponent> Class, const FTransform & ComponentRelativeTransform);
	

	/** Resets a Filter so that the first time it is used again it is clean */
	UFUNCTION(BlueprintCallable, Category = "LowPassFilter_Peak")
		static void ResetPeakLowPassFilter(UPARAM(ref) FBPLowPassPeakFilter& TargetPeakFilter);

	/** Adds an entry to the Peak low pass filter */
	UFUNCTION(BlueprintCallable, Category = "LowPassFilter_Peak")
		static void UpdatePeakLowPassFilter(UPARAM(ref) FBPLowPassPeakFilter& TargetPeakFilter, FVector NewSample);

	/** Gets the peak value of the Peak Low Pass Filter */
	UFUNCTION(BlueprintCallable, Category = "LowPassFilter_Peak")
		static FVector GetPeak_PeakLowPassFilter(UPARAM(ref) FBPLowPassPeakFilter& TargetPeakFilter);


	/** Resets a Euro Low Pass Filter so that the first time it is used again it is clean */
	UFUNCTION(BlueprintCallable, Category = "EuroLowPassFilter")
	static void ResetEuroSmoothingFilter(UPARAM(ref) FBPEuroLowPassFilter& TargetEuroFilter);

	/** Runs the smoothing function of a Euro Low Pass Filter */
	UFUNCTION(BlueprintCallable, Category = "EuroLowPassFilter")
	static void RunEuroSmoothingFilter(UPARAM(ref) FBPEuroLowPassFilter& TargetEuroFilter, FVector InRawValue, const float DeltaTime, FVector & SmoothedValue);

	// Applies the same laser smoothing that the vr editor uses to an array of points
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Smooth Update Laser Spline"), Category = "VRExpansionLibrary")
	static void SmoothUpdateLaserSpline(USplineComponent * LaserSplineComponent, TArray<USplineMeshComponent *> LaserSplineMeshComponents, FVector InStartLocation, FVector InEndLocation, FVector InForward, float LaserRadius);

	/**
	* Determine if any tag in the BaseContainer matches against any tag in OtherContainer with a required direct parent for both
	*
	* @param TagParent		Required direct parent tag
	* @param BaseContainer	Container containing values to check
	* @param OtherContainer	Container to check against.
	*
	* @return True if any tag was found that matches any tags explicitly present in OtherContainer with the same DirectParent
	*/
	UFUNCTION(BlueprintPure, Category = "GameplayTags")
	static bool MatchesAnyTagsWithDirectParentTag(FGameplayTag DirectParentTag,const FGameplayTagContainer& BaseContainer, const FGameplayTagContainer& OtherContainer);

	/**
	* Determine if any tag in the BaseContainer has the exact same direct parent tag and returns the first one
	* @param TagParent		Required direct parent tag
	* @param BaseContainer	Container containing values to check

	* @return True if any tag was found and also returns the tag
	*/
	UFUNCTION(BlueprintPure, Category = "GameplayTags")
	static bool GetFirstGameplayTagWithExactParent(FGameplayTag DirectParentTag, const FGameplayTagContainer& BaseContainer, FGameplayTag& FoundTag);

	// #TODO: probably need to implement this some day
	// This doesn't work for the web browser widget but it does for the normal widgets like text boxes
	// Just have to SetKeyboardFocus or SetUserFocus for the input widget first
	// Main problem is it takes focus from the player then....
	/*UFUNCTION(BlueprintCallable, Category = "KeyboardSimulation")
	static void SimulateCharacterEntry(UPARAM(ref) const FString& InChar)
	{

		for (int32 CharIndex = 0; CharIndex < InChar.Len(); CharIndex++)
		{
			TCHAR CharKey = InChar[CharIndex];
			const bool bRepeat = false;
			FCharacterEvent CharacterEvent(CharKey, FModifierKeysState(), 0, bRepeat);
			FSlateApplication::Get().ProcessKeyCharEvent(CharacterEvent);
		}

	}*/
	/*
	void FVREditorActionCallbacks::SimulateBackspace()
	{
		// Slate editable text fields handle backspace as a character entry
		FString BackspaceString = FString(TEXT("\b"));
		bool bRepeat = false;
		SimulateCharacterEntry(BackspaceString);
	}

	void FVREditorActionCallbacks::SimulateKeyDown(const FKey Key, const bool bRepeat)
	{
		const uint32* KeyCodePtr;
		const uint32* CharCodePtr;
		FInputKeyManager::Get().GetCodesFromKey(Key, KeyCodePtr, CharCodePtr);

		uint32 KeyCode = KeyCodePtr ? *KeyCodePtr : 0;
		uint32 CharCode = CharCodePtr ? *CharCodePtr : 0;

		FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, bRepeat, KeyCode, CharCode);
		bool DownResult = FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);

		if (CharCodePtr)
		{
			FCharacterEvent CharacterEvent(CharCode, FModifierKeysState(), 0, bRepeat);
			FSlateApplication::Get().ProcessKeyCharEvent(CharacterEvent);
		}
	}

	void FVREditorActionCallbacks::SimulateKeyUp(const FKey Key)
	{
		const uint32* KeyCodePtr;
		const uint32* CharCodePtr;
		FInputKeyManager::Get().GetCodesFromKey(Key, KeyCodePtr, CharCodePtr);

		uint32 KeyCode = KeyCodePtr ? *KeyCodePtr : 0;
		uint32 CharCode = CharCodePtr ? *CharCodePtr : 0;

		FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, false, KeyCode, CharCode);
		FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent);
	}*/
};	


