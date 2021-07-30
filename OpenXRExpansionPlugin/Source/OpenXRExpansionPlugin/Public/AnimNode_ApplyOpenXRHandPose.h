// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "Runtime/AnimGraphRuntime/Public/BoneControllers/AnimNode_SkeletalControlBase.h"
#include "OpenXRExpansionTypes.h"
//#include "Skeleton/BodyStateSkeleton.h"
//#include "BodyStateAnimInstance.h"

#include "AnimNode_ApplyOpenXRHandPose.generated.h"


USTRUCT()
struct OPENXREXPANSIONPLUGIN_API FAnimNode_ApplyOpenXRHandPose : public FAnimNode_SkeletalControlBase
{
	GENERATED_USTRUCT_BODY()

public:

	// Generally used when not passing in custom bone mappings, defines the auto mapping style
	UPROPERTY(EditAnywhere, Category = Skeletal, meta = (PinShownByDefault))
		EVROpenXRSkeletonType SkeletonType;

	// If your hand is part of a full body or arm skeleton and you don't have a proxy bone to retain the position enable this
	UPROPERTY(EditAnywhere, Category = Skeletal, meta = (PinShownByDefault))
		bool bSkipRootBone;

	// If you only want to use the wrist transform part of this
	// This will also automatically add the deform to the wrist as it doesn't make much sense without it
	UPROPERTY(EditAnywhere, Category = Skeletal, meta = (PinShownByDefault))
		bool bOnlyApplyWristTransform;
	
	// Generally used when not passing in custom bone mappings, defines the auto mapping style
	UPROPERTY(EditAnywhere, Category = Skeletal, meta = (PinShownByDefault))
		FBPOpenXRActionSkeletalData OptionalStoredActionInfo;

	// MappedBonePairs, if you leave it blank then they will auto generate based off of the SkeletonType
	// Otherwise, fill out yourself.
	UPROPERTY(EditAnywhere, Category = Skeletal, meta = (PinHiddenByDefault))
		FBPOpenXRSkeletalMappingData MappedBonePairs;

	bool bIsOpenInputAnimationInstance;

	void ConvertHandTransformsSpace(TArray<FTransform>& OutTransforms, TArray<FTransform>& WorldTransforms, FTransform AddTrans, bool bMirrorLeftRight, bool bMergeMissingUE4Bones);

	FTransform GetRefBoneInCS(TArray<FTransform>& RefBones, TArray<FMeshBoneInfo>& RefBonesInfo, int32 BoneIndex)
	{
		FTransform BoneTransform;

		if (BoneIndex >= 0)
		{
			BoneTransform = RefBones[BoneIndex];
			if (RefBonesInfo[BoneIndex].ParentIndex >= 0)
			{
				BoneTransform *= GetRefBoneInCS(RefBones, RefBonesInfo, RefBonesInfo[BoneIndex].ParentIndex);
			}
		}

		return BoneTransform;
	}

	void SetVectorToMaxElement(FVector& vec)
	{
		float aX = FMath::Abs(vec.X);
		float aY = FMath::Abs(vec.Y);

		if (aY < aX)
		{
			vec.Y = 0.f;
			if (FMath::Abs(vec.Z) < aX)
				vec.Z = 0.f;
			else
				vec.X = 0.f;
		}
		else
		{
			vec.X = 0.f;
			if (FMath::Abs(vec.Z) < aY)
				vec.Z = 0;
			else
				vec.Y = 0;
		}
	}

	// FAnimNode_SkeletalControlBase interface
	//virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface
	virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	virtual bool NeedsOnInitializeAnimInstance() const override { return true; }
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)  override;

	// Constructor 
	FAnimNode_ApplyOpenXRHandPose();

protected:
	bool WorldIsGame;
	AActor* OwningActor;

private:
};