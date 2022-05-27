// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "HeadMountedDisplayTypes.h"
#include "OpenXRCore.h"
#include "OpenXRHMD.h"
#include "OpenXRExpansionTypes.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "OpenXRExpansionFunctionLibrary.generated.h"


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
		static FName SystemName(TEXT("OpenXR"));
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
		{
			return static_cast<FOpenXRHMD*>(GEngine->XRSystem.Get());
		}

		return nullptr;
	}

	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
		static bool GetOpenXRHandPose(FBPOpenXRActionSkeletalData& HandPoseContainer, UOpenXRHandPoseComponent* HandPoseComponent, bool bGetMockUpPose = false)
	{
		FXRMotionControllerData MotionControllerData;

		if (bGetMockUpPose)
		{
			GetMockUpControllerData(MotionControllerData, HandPoseContainer);
			return true;
		}

		UHeadMountedDisplayFunctionLibrary::GetMotionControllerData((UObject*)HandPoseComponent, HandPoseContainer.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left ? EControllerHand::Left : EControllerHand::Right, MotionControllerData);

		if (MotionControllerData.bValid)
		{
			HandPoseContainer.SkeletalTransforms.Empty(MotionControllerData.HandKeyPositions.Num());
			FTransform ParentTrans = FTransform::Identity;

			if (MotionControllerData.DeviceVisualType == EXRVisualType::Controller)
			{
				ParentTrans = FTransform(MotionControllerData.GripRotation, MotionControllerData.GripPosition, FVector(1.f));
			}
			else // EXRVisualType::Hand visual type
			{
				ParentTrans = FTransform(MotionControllerData.HandKeyRotations[(uint8)EHandKeypoint::Palm], MotionControllerData.HandKeyPositions[(uint8)EHandKeypoint::Palm], FVector(1.f));
			}

			for (int i = 0; i < MotionControllerData.HandKeyPositions.Num(); i++)
			{
				// Convert to component space, we convert then to parent space later when applying it
				HandPoseContainer.SkeletalTransforms.Add(FTransform(MotionControllerData.HandKeyRotations[i].GetNormalized(), MotionControllerData.HandKeyPositions[i], FVector(1.f)).GetRelativeTransform(ParentTrans));
			}

			HandPoseContainer.bHasValidData = true;
			return true;
		}

		HandPoseContainer.bHasValidData = false;
		return false;
	}

	//UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void ConvertHandTransformsSpaceAndBack(TArray<FTransform>& OutTransforms, const TArray<FTransform>& WorldTransforms)
	{
		// Fail if the count is too low
		if (WorldTransforms.Num() < EHandKeypointCount)
			return;

		if (OutTransforms.Num() < WorldTransforms.Num())
		{
			OutTransforms.Empty(WorldTransforms.Num());
			OutTransforms.AddUninitialized(WorldTransforms.Num());
		}

		// Bone/Parent map
		int32 BoneParents[26] =
		{
			// Manually build the parent hierarchy starting at the wrist which has no parent (-1)
			1,	// Palm -> Wrist
			-1,	// Wrist -> None
			1,	// ThumbMetacarpal -> Wrist
			2,	// ThumbProximal -> ThumbMetacarpal
			3,	// ThumbDistal -> ThumbProximal
			4,	// ThumbTip -> ThumbDistal

			1,	// IndexMetacarpal -> Wrist
			6,	// IndexProximal -> IndexMetacarpal
			7,	// IndexIntermediate -> IndexProximal
			8,	// IndexDistal -> IndexIntermediate
			9,	// IndexTip -> IndexDistal

			1,	// MiddleMetacarpal -> Wrist
			11,	// MiddleProximal -> MiddleMetacarpal
			12,	// MiddleIntermediate -> MiddleProximal
			13,	// MiddleDistal -> MiddleIntermediate
			14,	// MiddleTip -> MiddleDistal

			1,	// RingMetacarpal -> Wrist
			16,	// RingProximal -> RingMetacarpal
			17,	// RingIntermediate -> RingProximal
			18,	// RingDistal -> RingIntermediate
			19,	// RingTip -> RingDistal

			1,	// LittleMetacarpal -> Wrist
			21,	// LittleProximal -> LittleMetacarpal
			22,	// LittleIntermediate -> LittleProximal
			23,	// LittleDistal -> LittleIntermediate
			24,	// LittleTip -> LittleDistal
		};

		// Convert transforms to parent space
		// The hand tracking transforms are in world space.
		for (int32 Index = 0; Index < EHandKeypointCount; ++Index)
		{
			const FTransform& BoneTransform = WorldTransforms[Index];
			int32 ParentIndex = BoneParents[Index];
			int32 ParentParent = -1;

			if (ParentIndex > 0)
			{
				ParentParent = BoneParents[ParentIndex];
			}

			if (ParentIndex < 0)
			{
				// We are at the root, so use it.
				OutTransforms[Index] = BoneTransform;
			}
			else
			{
				// Merging missing metacarpal bone into the transform
				if (ParentParent == 1) // Wrist
				{
					OutTransforms[Index] = BoneTransform.GetRelativeTransform(WorldTransforms[ParentParent]);
				}
				else
				{
					OutTransforms[Index] = BoneTransform.GetRelativeTransform(WorldTransforms[ParentIndex]);
				}
			}
		}

		// Check on the easy component space conversion first
		{
			for (int32 Index = 0; Index < EHandKeypointCount; ++Index)
			{
				const FTransform& BoneTransform = WorldTransforms[Index];
				int32 ParentIndex = BoneParents[Index];
				int32 ParentParent = -1;

				if (ParentIndex > 0)
				{
					ParentParent = BoneParents[ParentIndex];
				}

				if (ParentIndex > 0)
				{
					if (ParentParent == 1)
					{
						OutTransforms[Index] = OutTransforms[Index] * OutTransforms[ParentParent];
					}
					else
					{
						OutTransforms[Index] = OutTransforms[Index] * OutTransforms[ParentIndex];
					}
				}
			}
		}
	}

	//UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true"))
	static void GetMockUpControllerData(FXRMotionControllerData& MotionControllerData, FBPOpenXRActionSkeletalData& SkeletalMappingData, bool bOpenHand = false)
	{


		TArray<FQuat> HandRotationsClosed = {
			// Closed palm
			FQuat(0.419283837f,-0.547548413f,-0.704691410f,-0.166739136f),
			FQuat(-0.494952023f,0.567746222f,0.647768855f,0.114382625f),
			FQuat(0.105539620f,-0.079821065f,-0.934957743f,0.329157114f),
			FQuat(0.309810340f,0.005135804f,-0.887668610f,0.340640306f),
			FQuat(-0.292820454f,0.003018156f,0.892185807f,-0.343877673f),
			FQuat(-0.292820454f,0.003018156f,0.892185807f,-0.343877673f),
			FQuat(0.351302803f,-0.693441451f,-0.594165504f,-0.206626847f),
			FQuat(0.510899961f,-0.668595433f,-0.531049490f,-0.099752367f),
			FQuat(0.582462251f,-0.656170130f,-0.478819191f,0.030230284f),
			FQuat(0.631917894f,-0.637212157f,-0.435243726f,0.072160020f),
			FQuat(0.631917894f,-0.637212157f,-0.435243726f,0.072160020f),
			FQuat(0.419282734f,-0.547549129f,-0.704691410f,-0.166739583f),
			FQuat(0.514688492f,-0.678421855f,-0.501057625f,-0.154207960f),
			FQuat(0.603475809f,-0.696219206f,-0.388458073f,0.014001161f),
			FQuat(0.665590405f,-0.681470037f,-0.267755210f,0.144555300f),
			FQuat(0.670854926f,-0.691665173f,-0.240195125f,0.117731705f),
			FQuat(0.468501151f,-0.566464663f,-0.638139248f,-0.228914157f),
			FQuat(0.541896224f,-0.702563524f,-0.417507589f,-0.196058303f),
			FQuat(0.619513929f,-0.728625834f,-0.289327621f,-0.039927930f),
			FQuat(0.676032484f,-0.712538362f,-0.151063532f,0.111561134f),
			FQuat(0.676032484f,-0.712538362f,-0.151063532f,0.111561134f),
			FQuat(0.541268349f,-0.622995615f,-0.511540473f,-0.239230901f),
			FQuat(0.545463741f,-0.719190478f,-0.361613214f,-0.233387768f),
			FQuat(0.613924086f,-0.754614115f,-0.223099023f,-0.062293097f),
			FQuat(0.682628751f,-0.717284977f,-0.112533726f,0.082796305f),
			FQuat(0.682628751f,-0.717284977f,-0.112533726f,0.082796305f)
		};
		TArray<FQuat> HandRotationsOpen = {
			// Open Hand
			FQuat(0.167000905f,-0.670308471f,0.304047525f,-0.656011939f),
			FQuat(-0.129862994f,0.736467659f,-0.315623045f,0.584065497f),
			FQuat(-0.030090153f,0.121532254f,-0.840178490f,0.527659237f),
			FQuat(-0.126470163f,-0.262596279f,0.816956878f,-0.497623593f),
			FQuat(-0.102322638f,-0.249194950f,0.821705163f,-0.502227187f),
			FQuat(-0.102322638f,-0.249194950f,0.821705163f,-0.502227187f),
			FQuat(-0.277370781f,0.686735749f,-0.258646101f,0.620130479f),
			FQuat(0.193366051f,-0.808131576f,0.260262072f,-0.491728455f),
			FQuat(0.145547777f,-0.854364336f,0.317562312f,-0.384749293f),
			FQuat(0.107193023f,-0.879853010f,0.321882188f,-0.332806766f),
			FQuat(0.107193023f,-0.879853010f,0.321882188f,-0.332806766f),
			FQuat(0.166999936f,-0.670307159f,0.304047883f,-0.656013489f),
			FQuat(0.206125781f,-0.815250278f,0.203173012f,-0.501596987f),
			FQuat(0.164493740f,-0.890369833f,0.257293820f,-0.337613612f),
			FQuat(0.114019498f,-0.937856555f,0.283619940f,-0.164267495f),
			FQuat(0.107336335f,-0.925720870f,0.321023613f,-0.168710276f),
			FQuat(0.156629071f,-0.719596088f,0.210793152f,-0.642817795f),
			FQuat(0.194258988f,-0.858762920f,0.127883837f,-0.456546605f),
			FQuat(0.166189745f,-0.930981100f,0.161785051f,-0.281922638f),
			FQuat(0.119936436f,-0.970744252f,0.189072192f,-0.086731322f),
			FQuat(0.119936436f,-0.970744252f,0.189072192f,-0.086731322f),
			FQuat(0.160288095f,-0.812664807f,0.100923792f,-0.551087677f),
			FQuat(0.207311243f,-0.870556056f,0.056644741f,-0.442656904f),
			FQuat(0.191506147f,-0.944826961f,0.096772499f,-0.247511998f),
			FQuat(0.116890728f,-0.981477261f,0.138804480f,-0.061412390f),
			FQuat(0.116890728f,-0.981477261f,0.138804480f,-0.061412390f)
		};

		MotionControllerData.HandKeyRotations = SkeletalMappingData.TargetHand != EVRSkeletalHandIndex::EActionHandIndex_Left ? HandRotationsOpen : HandRotationsClosed;

		TArray<FVector> HandPositionsClosed = {
			// Closed palm - Left
			FVector(-1203.819f,-516.869f,286.028f),
			FVector(-1201.705f,-515.876f,287.796),
			FVector(-1204.748f,-519.489f,288.397),
			FVector(-1207.823f,-522.044f,287.811),
			FVector(-1209.696f,-524.000f,286.012),
			FVector(-1210.690f,-525.034f,285.137),
			FVector(-1204.366f,-517.319f,288.299),
			FVector(-1209.332f,-519.116f,283.063),
			FVector(-1211.344f,-521.651f,280.094),
			FVector(-1212.261f,-523.927f,278.608),
			FVector(-1212.474f,-524.895f,278.097),
			FVector(-1203.111f,-516.601f,286.951),
			FVector(-1207.318f,-518.192f,281.462),
			FVector(-1209.140f,-520.536f,278.337),
			FVector(-1210.042f,-523.368f,276.842),
			FVector(-1210.136f,-524.640f,276.636),
			FVector(-1202.057f,-516.294f,286.121),
			FVector(-1205.064f,-517.866f,280.471),
			FVector(-1206.431f,-520.299f,277.509),
			FVector(-1207.089f,-522.828f,276.311),
			FVector(-1207.154f,-523.888f,276.263),
			FVector(-1200.966f,-516.092f,285.475),
			FVector(-1202.852f,-518.796f,280.113),
			FVector(-1203.746f,-520.657f,277.907),
			FVector(-1204.180f,-522.291f,277.237),
			FVector(-1204.222f,-523.061f,277.211)
		};

		// Open Hand
		TArray<FVector> HandPositionsOpen = {
			FVector(-1014.001f,-478.278f,212.902f),
			FVector(-1013.516f,-476.006f,214.688f),
			FVector(-1016.362f,-479.642f,215.119f),
			FVector(-1018.145f,-483.254f,214.805f),
			FVector(-1019.682f,-485.682f,213.284f),
			FVector(-1020.480f,-486.982f,212.581f),
			FVector(-1014.360f,-478.927f,215.169f),
			FVector(-1014.932f,-484.146f,209.902f),
			FVector(-1016.872f,-486.643f,206.852f),
			FVector(-1018.771f,-488.058f,205.231f),
			FVector(-1019.613f,-488.507f,204.655f),
			FVector(-1013.901f,-477.534f,213.831f),
			FVector(-1014.494f,-481.954f,208.310f),
			FVector(-1016.269f,-484.282f,205.146f),
			FVector(-1018.657f,-485.834f,203.427f),
			FVector(-1019.846f,-486.231f,203.113f),
			FVector(-1013.816f,-476.436f,213.006f),
			FVector(-1014.637f,-479.707f,207.344f),
			FVector(-1016.703f,-481.540f,204.355f),
			FVector(-1018.962f,-482.692f,203.000f),
			FVector(-1019.978f,-482.975f,202.870f),
			FVector(-1013.845f,-475.325f,212.363f),
			FVector(-1015.993f,-477.665f,206.928f),
			FVector(-1017.571f,-478.907f,204.670f),
			FVector(-1019.033f,-479.652f,203.887f),
			FVector(-1019.778f,-479.842f,203.819f)
		};

		MotionControllerData.HandKeyPositions = SkeletalMappingData.TargetHand != EVRSkeletalHandIndex::EActionHandIndex_Left ? HandPositionsOpen : HandPositionsClosed;

		if (SkeletalMappingData.TargetHand != EVRSkeletalHandIndex::EActionHandIndex_Left)
		{
			MotionControllerData.GripPosition = FVector(-1018.305f, -478.019f, 209.872f);
			MotionControllerData.GripRotation = FQuat(-0.116352126f, 0.039430488f, -0.757644236f, 0.641001403f);
		}
		else
		{
			MotionControllerData.GripPosition = FVector(-1202.619f, -521.077f, 283.076f);
			MotionControllerData.GripRotation = FQuat(0.040843058f, 0.116659224f, 0.980030060f, -0.155767411f);
		}

		MotionControllerData.DeviceName = TEXT("OpenXR");

		SkeletalMappingData.bHasValidData = true;
		SkeletalMappingData.SkeletalTransforms.Empty(SkeletalMappingData.SkeletalTransforms.Num());
		FTransform ParentTrans = FTransform(MotionControllerData.GripRotation, MotionControllerData.GripPosition, FVector(1.f));
		for (int i = 0; i < MotionControllerData.HandKeyPositions.Num(); i++)
		{
			SkeletalMappingData.SkeletalTransforms.Add(FTransform(MotionControllerData.HandKeyRotations[i], MotionControllerData.HandKeyPositions[i], FVector(1.f)).GetRelativeTransform(ParentTrans));
		}
	}

	// Get a list of all currently tracked devices and their types, index in the array is their device index
	// Returns failed if the openXR query failed (no interaction profile yet or openXR is not running)
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|OpenXR", meta = (bIgnoreSelf = "true", ExpandEnumAsExecs = "Result"))
		static void GetXRMotionControllerType(FString& TrackingSystemName, EBPOpenXRControllerDeviceType& DeviceType, EBPXRResultSwitch &Result);
};