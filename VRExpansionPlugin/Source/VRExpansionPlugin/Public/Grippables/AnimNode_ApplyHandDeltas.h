// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "Grippables/HandSocketComponent.h"
#include "Animation/AnimNodeBase.h"
//#include "Skeleton/BodyStateSkeleton.h"
//#include "BodyStateAnimInstance.h"

#include "AnimNode_ApplyHandDeltas.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct VREXPANSIONPLUGIN_API FAnimNode_ApplyHandDeltas : public FAnimNode_Base
{
	GENERATED_USTRUCT_BODY()

public:
	
	// The stored custom bone deltas
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandAnimation", meta = (PinShownByDefault))
		TArray<FBPVRHandPoseBonePair> CustomPoseDeltas;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandAnimation", meta = (PinShownByDefault))
		FBPVRHandPoseBonePair CustomPoseDelta;

	bool bAlreadyInitialized;
	FName LastAssetName;
	FCompactPose CachedPose;
	bool bAlreadyCachedPose;

	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;

	// FAnimNode_SkeletalControlBase interface
	//virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
//	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	//virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface
	//virtual void OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance) override;
	//virtual bool NeedsOnInitializeAnimInstance() const override { return false; }
	//virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

	// Constructor 
	FAnimNode_ApplyHandDeltas();

protected:

private:
};