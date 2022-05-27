// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"

#include "OpenXRExpansionTypes.generated.h"

// This makes a lot of the blueprint functions cleaner
UENUM()
enum class EBPXRResultSwitch : uint8
{
	// On Success
	OnSucceeded,
	// On Failure
	OnFailed
};

UENUM(BlueprintType)
enum class EVRSkeletalHandIndex : uint8
{
	EActionHandIndex_Left = 0,
	EActionHandIndex_Right
};

UENUM(BlueprintType)
enum class EXRHandJointType : uint8
{
	OXR_HAND_JOINT_PALM_EXT = 0,
	OXR_HAND_JOINT_WRIST_EXT = 1,
	OXR_HAND_JOINT_THUMB_METACARPAL_EXT = 2,
	OXR_HAND_JOINT_THUMB_PROXIMAL_EXT = 3,
	OXR_HAND_JOINT_THUMB_DISTAL_EXT = 4,
	OXR_HAND_JOINT_THUMB_TIP_EXT = 5,
	OXR_HAND_JOINT_INDEX_METACARPAL_EXT = 6,
	OXR_HAND_JOINT_INDEX_PROXIMAL_EXT = 7,
	OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT = 8,
	OXR_HAND_JOINT_INDEX_DISTAL_EXT = 9,
	OXR_HAND_JOINT_INDEX_TIP_EXT = 10,
	OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT = 11,
	OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT = 12,
	OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT = 13,
	OXR_HAND_JOINT_MIDDLE_DISTAL_EXT = 14,
	OXR_HAND_JOINT_MIDDLE_TIP_EXT = 15,
	OXR_HAND_JOINT_RING_METACARPAL_EXT = 16,
	OXR_HAND_JOINT_RING_PROXIMAL_EXT = 17,
	OXR_HAND_JOINT_RING_INTERMEDIATE_EXT = 18,
	OXR_HAND_JOINT_RING_DISTAL_EXT = 19,
	OXR_HAND_JOINT_RING_TIP_EXT = 20,
	OXR_HAND_JOINT_LITTLE_METACARPAL_EXT = 21,
	OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT = 22,
	OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT = 23,
	OXR_HAND_JOINT_LITTLE_DISTAL_EXT = 24,
	OXR_HAND_JOINT_LITTLE_TIP_EXT = 25,
	OXR_HAND_JOINT_MAX_ENUM_EXT = 0xFF
};

UENUM(BlueprintType)
enum class EVROpenXRSkeletonType : uint8
{
	OXR_SkeletonType_UE4Default_Right,
	OXR_SkeletonType_UE4Default_Left,
	OXR_SkeletonType_OpenVRDefault_Right,
	OXR_SkeletonType_OpenVRDefault_Left,
	OXR_SkeletonType_Custom
};



USTRUCT(BlueprintType, Category = "VRExpansionFunctions|OpenXR|HandSkeleton")
struct OPENXREXPANSIONPLUGIN_API FBPOpenXRActionSkeletalData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadWrite, Category = Default)
		EVRSkeletalHandIndex TargetHand;

	// A world scale override that will replace the engines current value and force into the tracked data if non zero
	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadWrite, Category = Default)
		float WorldScaleOverride;

	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadWrite, Category = Default)
		bool bAllowDeformingMesh;

	// If true then the bones will be mirrored from left/right, to allow you to swap a hand mesh or apply to a full body mesh
	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadWrite, Category = Default)
		bool bMirrorLeftRight;

	UPROPERTY(BlueprintReadOnly, NotReplicated, Transient, Category = Default)
		TArray<FTransform> SkeletalTransforms;

	// If true we will assume that the target skeleton does not have the metacarpal bones and we will not replicate them
	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadWrite, Category = Default)
		bool bEnableUE4HandRepSavings;

	UPROPERTY(BlueprintReadOnly, NotReplicated, Transient, Category = Default)
		TArray<FTransform> OldSkeletalTransforms;

	// The rotation required to rotate the finger bones back to X+
	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadWrite, Category = Default)
		FTransform AdditionTransform;

	UPROPERTY(NotReplicated, BlueprintReadOnly, Category = Default)
		bool bHasValidData;

	FName LastHandGesture;
	int32 LastHandGestureIndex;

	FBPOpenXRActionSkeletalData()
	{
		//bGetTransformsInParentSpace = false;
		AdditionTransform = FTransform(FRotator(180.f, 0.f, -90.f), FVector::ZeroVector, FVector(1.f));//FTransform(FRotator(0.f, 90.f, 90.f), FVector::ZeroVector, FVector(1.f));
		WorldScaleOverride = 0.0f;
		bAllowDeformingMesh = true;
		bMirrorLeftRight = false;
		bEnableUE4HandRepSavings = true;
		TargetHand = EVRSkeletalHandIndex::EActionHandIndex_Right;
		bHasValidData = false;
		LastHandGestureIndex = INDEX_NONE;
		LastHandGesture = NAME_None;
	}
};

USTRUCT(BlueprintType, Category = "VRExpansionFunctions|SteamVR|HandSkeleton")
struct OPENXREXPANSIONPLUGIN_API FBPOpenXRSkeletalPair
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		EXRHandJointType OpenXRBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		FName BoneToTarget;

	FBoneReference ReferenceToConstruct;
	FCompactPoseBoneIndex ParentReference;
	FQuat RetargetRot;

	FBPOpenXRSkeletalPair() :
		ParentReference(INDEX_NONE)
	{
		OpenXRBone = EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT;
		BoneToTarget = NAME_None;
		RetargetRot = FQuat::Identity;
	}

	FBPOpenXRSkeletalPair(EXRHandJointType Bone, FString TargetBone) :
		ParentReference(INDEX_NONE)
	{
		OpenXRBone = Bone;
		BoneToTarget = FName(*TargetBone);
		ReferenceToConstruct.BoneName = BoneToTarget;
		RetargetRot = FQuat::Identity;
	}

	FORCEINLINE bool operator==(const int32& Other) const
	{
		return ReferenceToConstruct.CachedCompactPoseIndex.GetInt() == Other;
		//return ReferenceToConstruct.BoneIndex == Other;
	}
};

USTRUCT(BlueprintType, Category = "VRExpansionFunctions|SteamVR|HandSkeleton")
struct OPENXREXPANSIONPLUGIN_API FBPOpenXRSkeletalMappingData
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		TArray<FBPOpenXRSkeletalPair> BonePairs;

	// Merge the transforms of bones that are missing from the OpenVR skeleton to the UE4 one.
	// This should be always enabled for UE4 skeletons generally.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		bool bMergeMissingBonesUE4;

	// The hand data to get, if not using a custom bone mapping then this value will be auto filled
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Default")
		EVRSkeletalHandIndex TargetHand;

	FQuat AdjustmentQuat;
	bool bInitialized;

	FName LastInitializedName;

	void ConstructDefaultMappings(EVROpenXRSkeletonType SkeletonType, bool bSkipRootBone)
	{
		switch (SkeletonType)
		{

		case EVROpenXRSkeletonType::OXR_SkeletonType_OpenVRDefault_Left:
		case EVROpenXRSkeletonType::OXR_SkeletonType_OpenVRDefault_Right:
		{
			bMergeMissingBonesUE4 = false;
			SetDefaultOpenVRInputs(SkeletonType, bSkipRootBone);
		}break;
		case EVROpenXRSkeletonType::OXR_SkeletonType_UE4Default_Left:
		case EVROpenXRSkeletonType::OXR_SkeletonType_UE4Default_Right:
		{
			bMergeMissingBonesUE4 = true;
			SetDefaultUE4Inputs(SkeletonType, bSkipRootBone);
		}break;
		}
	}

	void SetDefaultOpenVRInputs(EVROpenXRSkeletonType cSkeletonType, bool bSkipRootBone)
	{
		// Don't map anything if the end user already has
		if (BonePairs.Num())
			return;

		bool bIsRightHand = cSkeletonType != EVROpenXRSkeletonType::OXR_SkeletonType_OpenVRDefault_Left;
		FString HandDelimiterS = !bIsRightHand ? "l" : "r";
		const TCHAR* HandDelimiter = *HandDelimiterS;

		TargetHand = bIsRightHand ? EVRSkeletalHandIndex::EActionHandIndex_Right : EVRSkeletalHandIndex::EActionHandIndex_Left;

		// Default OpenVR bones mapping
		//if (!bSkipRootBone)
		//{
			//BonePairs.Add(FBPOpenVRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_PALM_EXT, FString::Printf(TEXT("Root"), HandDelimiter)));
		//}

		if (!bSkipRootBone)
		{
			BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT, FString::Printf(TEXT("wrist_%s"), HandDelimiter)));
		}


		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT, FString::Printf(TEXT("finger_thumb_0_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT, FString::Printf(TEXT("finger_thumb_1_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT, FString::Printf(TEXT("finger_thumb_2_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_TIP_EXT, FString::Printf(TEXT("finger_thumb_%s_end"), HandDelimiter)));


		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT, FString::Printf(TEXT("finger_index_meta_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT, FString::Printf(TEXT("finger_index_0_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, FString::Printf(TEXT("finger_index_1_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT, FString::Printf(TEXT("finger_index_2_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT, FString::Printf(TEXT("finger_index_%s_end"), HandDelimiter)));


		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT, FString::Printf(TEXT("finger_middle_meta_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, FString::Printf(TEXT("finger_middle_0_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, FString::Printf(TEXT("finger_middle_1_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT, FString::Printf(TEXT("finger_middle_2_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_TIP_EXT, FString::Printf(TEXT("finger_middle_%s_end"), HandDelimiter)));


		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT, FString::Printf(TEXT("finger_ring_meta_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT, FString::Printf(TEXT("finger_ring_0_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT, FString::Printf(TEXT("finger_ring_1_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT, FString::Printf(TEXT("finger_ring_2_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_TIP_EXT, FString::Printf(TEXT("finger_ring_%s_end"), HandDelimiter)));


		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT, FString::Printf(TEXT("finger_pinky_meta_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT, FString::Printf(TEXT("finger_pinky_0_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, FString::Printf(TEXT("finger_pinky_1_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT, FString::Printf(TEXT("finger_pinky_2_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_TIP_EXT, FString::Printf(TEXT("finger_pinky_%s_end"), HandDelimiter)));

		// Aux bones share the final knuckles location / rotation
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT, FString::Printf(TEXT("finger_thumb_%s_aux"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT, FString::Printf(TEXT("finger_index_%s_aux"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT, FString::Printf(TEXT("finger_middle_%s_aux"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT, FString::Printf(TEXT("finger_ring_%s_aux"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT, FString::Printf(TEXT("finger_pinky_%s_aux"), HandDelimiter)));
	}

	void SetDefaultUE4Inputs(EVROpenXRSkeletonType cSkeletonType, bool bSkipRootBone)
	{
		// Don't map anything if the end user already has
		if (BonePairs.Num())
			return;

		bool bIsRightHand = cSkeletonType != EVROpenXRSkeletonType::OXR_SkeletonType_UE4Default_Left;
		FString HandDelimiterS = !bIsRightHand ? "l" : "r";
		const TCHAR* HandDelimiter = *HandDelimiterS;

		TargetHand = bIsRightHand ? EVRSkeletalHandIndex::EActionHandIndex_Right : EVRSkeletalHandIndex::EActionHandIndex_Left;

		// Default ue4 skeleton hand to the OpenVR bones, skipping the extra joint and the aux joints
		if (!bSkipRootBone)
		{
			BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT, FString::Printf(TEXT("hand_%s"), HandDelimiter)));
		}

		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT, FString::Printf(TEXT("index_01_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, FString::Printf(TEXT("index_02_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT, FString::Printf(TEXT("index_03_%s"), HandDelimiter)));

		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, FString::Printf(TEXT("middle_01_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, FString::Printf(TEXT("middle_02_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT, FString::Printf(TEXT("middle_03_%s"), HandDelimiter)));

		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT, FString::Printf(TEXT("pinky_01_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, FString::Printf(TEXT("pinky_02_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT, FString::Printf(TEXT("pinky_03_%s"), HandDelimiter)));

		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT, FString::Printf(TEXT("ring_01_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT, FString::Printf(TEXT("ring_02_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT, FString::Printf(TEXT("ring_03_%s"), HandDelimiter)));

		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT, FString::Printf(TEXT("thumb_01_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT, FString::Printf(TEXT("thumb_02_%s"), HandDelimiter)));
		BonePairs.Add(FBPOpenXRSkeletalPair(EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT, FString::Printf(TEXT("thumb_03_%s"), HandDelimiter)));

	}

	FBPOpenXRSkeletalMappingData()
	{
		AdjustmentQuat = FQuat::Identity;
		bInitialized = false;
		bMergeMissingBonesUE4 = false;
		TargetHand = EVRSkeletalHandIndex::EActionHandIndex_Right;
		LastInitializedName = NAME_None;
	}
};
