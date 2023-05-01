// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "HeadMountedDisplayTypes.h"
#if defined(OPENXR_SUPPORTED)
#include "OpenXRCore.h"
#include "OpenXRHMD.h"
#endif
#include "OpenXRExpansionTypes.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "OpenXRExpansionFunctionLibrary.generated.h"

#if !defined(OPENXR_SUPPORTED)
class FOpenXRHMD;
#endif

DECLARE_LOG_CATEGORY_EXTERN(OpenXRExpansionFunctionLibraryLog, Log, All);

// This needs to be updated as the original gets changed, that or hope they make the original blueprint accessible.
UENUM(Blueprintable)
enum class EBPOpenXRControllerDeviceType : uint8
{
	DT_SimpleController,
	DT_ValveIndexController,
	DT_ViveController,
	DT_ViveProController,
	//DT_CosmosController,
	DT_DaydreamController,
	DT_OculusTouchController,
	DT_OculusGoController,
	DT_MicrosoftMotionController,
	DT_MicrosoftXboxController,
	DT_PicoNeo3Controller,
	DT_WMRController,
	DT_UnknownController
};

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class OPENXREXPANSIONPLUGIN_API UOpenXRExpansionFunctionLibrary : public UBlueprintFunctionLibrary
{
	//GENERATED_BODY()
	GENERATED_BODY()

public:
	UOpenXRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer);

	~UOpenXRExpansionFunctionLibrary();
public:

	static FOpenXRHMD* GetOpenXRHMD()
	{
#if defined(OPENXR_SUPPORTED)
		static FName SystemName(TEXT("OpenXR"));
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
		{
			return static_cast<FOpenXRHMD*>(GEngine->XRSystem.Get());
		}
#endif

		return nullptr;
	}

	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
		static bool GetOpenXRHandPose(FBPOpenXRActionSkeletalData& HandPoseContainer, UOpenXRHandPoseComponent* HandPoseComponent, bool bGetMockUpPose = false);

	//UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void GetFingerCurlValues(TArray<FTransform>& TransformArray, TArray<float>& CurlArray);

	// Get the estimated curl values from hand tracking
	// Will return true if it was able to get the curls, false if it could not (hand tracking not enabled or no data for the tracked index)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (WorldContext = "WorldContextObject"))
		static bool GetOpenXRFingerCurlValuesForHand(
			UObject* WorldContextObject,
			EControllerHand TargetHand,
			float& ThumbCurl,
			float& IndexCurl,
			float& MiddleCurl,
			float& RingCurl,
			float& PinkyCurl);

	static float GetCurlValueForBoneRoot(TArray<FTransform>& TransformArray, EHandKeypoint RootBone);

	//UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void ConvertHandTransformsSpaceAndBack(TArray<FTransform>& OutTransforms, const TArray<FTransform>& WorldTransforms);

	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void GetMockUpControllerData(FXRMotionControllerData& MotionControllerData, FBPOpenXRActionSkeletalData& SkeletalMappingData, bool bOpenHand = false);

	// Get a list of all currently tracked devices and their types, index in the array is their device index
	// Returns failed if the openXR query failed (no interaction profile yet or openXR is not running)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true", ExpandEnumAsExecs = "Result"))
		static void GetXRMotionControllerType(FString& TrackingSystemName, EBPOpenXRControllerDeviceType& DeviceType, EBPXRResultSwitch &Result);
};