// Fill out your copyright notice in the Description page of Project Settings.
#include "Grippables/AnimNode_ApplyHandDeltas.h"
//#include "EngineMinimal.h"
//#include "Engine/Engine.h"
//#include "CoreMinimal.h"
#include "AnimationRuntime.h"
#include "Runtime/Engine/Public/Animation/AnimInstanceProxy.h"

FAnimNode_ApplyHandDeltas::FAnimNode_ApplyHandDeltas()
	//: FAnimNode_SkeletalControlBase()
{
	bAlreadyInitialized = false;
	LastAssetName = NAME_None;
	bAlreadyCachedPose = false;
}

void FAnimNode_ApplyHandDeltas::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	if (!Context.AnimInstanceProxy)
		return;

	const FBoneContainer &RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

	UObject* OwningAsset = RequiredBones.GetAsset();
	if (!OwningAsset)
		return;

	if (!bAlreadyInitialized || OwningAsset->GetFName() != LastAssetName)
	{
		USkeleton* AssetSkeleton = RequiredBones.GetSkeletonAsset();
		if (AssetSkeleton)
		{
			for (FBPVRHandPoseBonePair& BonePair : CustomPoseDeltas)
			{
				BonePair.ReferenceToConstruct.BoneName = BonePair.BoneName;
				BonePair.ReferenceToConstruct.Initialize(AssetSkeleton);
				BonePair.ReferenceToConstruct.CachedCompactPoseIndex = BonePair.ReferenceToConstruct.GetCompactPoseIndex(RequiredBones);
			}

			bAlreadyInitialized = true;
			LastAssetName = OwningAsset->GetFName();
		}
		else
		{
			bAlreadyInitialized = false;
		}
	}
}

void FAnimNode_ApplyHandDeltas::Evaluate_AnyThread(FPoseContext& Output)
{
	if (!bAlreadyInitialized)
	{
		return;
	}

	FCompactPose& OutPose = Output.Pose;
	OutPose.ResetToRefPose();
	if (CustomPoseDeltas.Num() > 0)
	{
		if (bAlreadyCachedPose)
		{
			OutPose = CachedPose;
		}
		else
		{
			//const FBoneContainer& BoneContainer = OutPose.GetBoneContainer();
			FCSPose<FCompactPose> MeshPoses;
			MeshPoses.InitPose(OutPose);

			for (FBPVRHandPoseBonePair& HandPair : CustomPoseDeltas)
			{
				FTransform CompTrans = MeshPoses.GetComponentSpaceTransform(HandPair.ReferenceToConstruct.CachedCompactPoseIndex);
				CompTrans.ConcatenateRotation(HandPair.DeltaPose);
				MeshPoses.SetComponentSpaceTransform(HandPair.ReferenceToConstruct.CachedCompactPoseIndex, CompTrans);
			}

			FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(MeshPoses, OutPose);

			CachedPose = OutPose;
		}
	}
}