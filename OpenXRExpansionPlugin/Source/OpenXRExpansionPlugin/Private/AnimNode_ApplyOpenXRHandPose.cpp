// Fill out your copyright notice in the Description page of Project Settings.
#include "AnimNode_ApplyOpenXRHandPose.h"
//#include "EngineMinimal.h"
//#include "Engine/Engine.h"
//#include "CoreMinimal.h"
#include "OpenXRExpansionFunctionLibrary.h"
#include "AnimNode_ApplyOpenXRHandPose.h"
#include "AnimationRuntime.h"
#include "DrawDebugHelpers.h"
#include "OpenXRHandPoseComponent.h"
#include "Runtime/Engine/Public/Animation/AnimInstanceProxy.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
	
FAnimNode_ApplyOpenXRHandPose::FAnimNode_ApplyOpenXRHandPose()
	: FAnimNode_SkeletalControlBase()
{
	WorldIsGame = false;
	Alpha = 1.f;
	SkeletonType = EVROpenXRSkeletonType::OXR_SkeletonType_UE4Default_Right;
	bIsOpenInputAnimationInstance = false;
	bSkipRootBone = false;
	bOnlyApplyWristTransform = false;
	//WristAdjustment = FQuat::Identity;
}

void FAnimNode_ApplyOpenXRHandPose::OnInitializeAnimInstance(const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	Super::OnInitializeAnimInstance(InProxy, InAnimInstance);

	if (const UOpenXRAnimInstance * animInst = Cast<UOpenXRAnimInstance>(InAnimInstance))
	{
		bIsOpenInputAnimationInstance = true;
	}
}

void FAnimNode_ApplyOpenXRHandPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);
}

void FAnimNode_ApplyOpenXRHandPose::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);
}

void FAnimNode_ApplyOpenXRHandPose::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	UObject* OwningAsset = RequiredBones.GetAsset();
	if (!OwningAsset)
		return;

	if (!MappedBonePairs.bInitialized || OwningAsset->GetFName() != MappedBonePairs.LastInitializedName)
	{
		MappedBonePairs.LastInitializedName = OwningAsset->GetFName();
		MappedBonePairs.bInitialized = false;
		
		USkeleton* AssetSkeleton = RequiredBones.GetSkeletonAsset();
		if (AssetSkeleton)
		{
			// If our bone pairs are empty, then setup our sane defaults
			if(!MappedBonePairs.BonePairs.Num())
				MappedBonePairs.ConstructDefaultMappings(SkeletonType, bSkipRootBone);

			FBPOpenXRSkeletalPair WristPair;
			FBPOpenXRSkeletalPair IndexPair;
			FBPOpenXRSkeletalPair PinkyPair;

			TArray<FTransform> RefBones = AssetSkeleton->GetReferenceSkeleton().GetRefBonePose();
			TArray<FMeshBoneInfo> RefBonesInfo = AssetSkeleton->GetReferenceSkeleton().GetRefBoneInfo();

			for (FBPOpenXRSkeletalPair& BonePair : MappedBonePairs.BonePairs)
			{
				// Fill in the bone name for the reference
				BonePair.ReferenceToConstruct.BoneName = BonePair.BoneToTarget;

				// Init the reference
				BonePair.ReferenceToConstruct.Initialize(AssetSkeleton);

				BonePair.ReferenceToConstruct.CachedCompactPoseIndex = BonePair.ReferenceToConstruct.GetCompactPoseIndex(RequiredBones);

				if ((BonePair.ReferenceToConstruct.CachedCompactPoseIndex != INDEX_NONE))
				{
					// Get our parent bones index
					BonePair.ParentReference = RequiredBones.GetParentBoneIndex(BonePair.ReferenceToConstruct.CachedCompactPoseIndex);

					/*FTransform WristPose = GetRefBoneInCS(RefBones, RefBonesInfo, BonePair.ReferenceToConstruct.BoneIndex);

					FVector WristForward = WristPose.GetRotation().GetForwardVector();
					FVector WristUpward = WristPose.GetRotation().GetForwardVector();
					FQuat ForwardFixup = FQuat::FindBetweenNormals(FVector::ForwardVector, WristForward);
					FQuat UpFixup = FQuat::FindBetweenNormals(ForwardFixup.RotateVector(FVector::UpVector), WristUpward);

					FQuat rotFix = UpFixup * ForwardFixup;
					rotFix.Normalize();
					//MappedBonePairs.AdjustmentQuat = rotFix;
					BonePair.RetargetRot = rotFix;*/
				}
				
				if (BonePair.OpenXRBone == EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT)
				{
					WristPair = BonePair;
				}
				else if (BonePair.OpenXRBone == EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT)
				{
					IndexPair = BonePair;
				}
				else if (BonePair.OpenXRBone == EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT)
				{
					PinkyPair = BonePair;
				}
			}

			MappedBonePairs.bInitialized = true;

			if (WristPair.ReferenceToConstruct.HasValidSetup() && IndexPair.ReferenceToConstruct.HasValidSetup() && PinkyPair.ReferenceToConstruct.HasValidSetup())
			{
				//TArray<FTransform> RefBones = AssetSkeleton->GetReferenceSkeleton().GetRefBonePose();
				//TArray<FMeshBoneInfo> RefBonesInfo = AssetSkeleton->GetReferenceSkeleton().GetRefBoneInfo();

				FTransform WristPose = GetRefBoneInCS(RefBones, RefBonesInfo, WristPair.ReferenceToConstruct.BoneIndex);
				FTransform MiddleFingerPose = GetRefBoneInCS(RefBones, RefBonesInfo, PinkyPair.ReferenceToConstruct.BoneIndex);

				FVector BoneForwardVector = MiddleFingerPose.GetTranslation() - WristPose.GetTranslation();
				SetVectorToMaxElement(BoneForwardVector);
				BoneForwardVector.Normalize();

				FTransform IndexFingerPose = GetRefBoneInCS(RefBones, RefBonesInfo, IndexPair.ReferenceToConstruct.BoneIndex);
				FTransform PinkyFingerPose = GetRefBoneInCS(RefBones, RefBonesInfo, PinkyPair.ReferenceToConstruct.BoneIndex);
				FVector BoneUpVector = IndexFingerPose.GetTranslation() - PinkyFingerPose.GetTranslation();
				SetVectorToMaxElement(BoneUpVector);
				BoneUpVector.Normalize();

				FVector BoneRightVector = FVector::CrossProduct(BoneUpVector, BoneForwardVector);
				BoneRightVector.Normalize();

				FQuat ForwardAdjustment = FQuat::FindBetweenNormals(FVector::ForwardVector, BoneForwardVector);

				FVector NewRightVector = ForwardAdjustment * FVector::RightVector;
				NewRightVector.Normalize();

				FQuat TwistAdjustment = FQuat::FindBetweenNormals(NewRightVector, BoneRightVector);
				MappedBonePairs.AdjustmentQuat = TwistAdjustment * ForwardAdjustment;
				MappedBonePairs.AdjustmentQuat.Normalize();
			}
			
		}
	}
}

void FAnimNode_ApplyOpenXRHandPose::ConvertHandTransformsSpace(TArray<FTransform>& OutTransforms, TArray<FTransform>& WorldTransforms, FTransform AddTrans, bool bMirrorLeftRight, bool bMergeMissingUE4Bones)
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
		WorldTransforms[Index].NormalizeRotation();

		if (bMirrorLeftRight)
		{
			WorldTransforms[Index].Mirror(EAxis::Y, EAxis::Y);
		}

		WorldTransforms[Index].ConcatenateRotation(AddTrans.GetRotation());
	}

	for (int32 Index = 0; Index < EHandKeypointCount; ++Index)
	{
		FTransform& BoneTransform = WorldTransforms[Index];
		int32 ParentIndex = BoneParents[Index];
		int32 ParentParent = -1;

		// Thumb keeps the metacarpal intact, we don't skip it
		if (bMergeMissingUE4Bones)
		{
			if (Index != (int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT && ParentIndex > 0)
			{
				ParentParent = BoneParents[ParentIndex];
			}
		}

		if (ParentIndex < 0)
		{
			// We are at the root, so use it.
			OutTransforms[Index] = BoneTransform;
		}
		else
		{
			// Merging missing metacarpal bone into the transform
			if (bMergeMissingUE4Bones && ParentParent == 1) // Wrist
			{
				OutTransforms[Index] = BoneTransform.GetRelativeTransform(WorldTransforms[ParentParent]);
			}
			else
			{
				OutTransforms[Index] = BoneTransform.GetRelativeTransform(WorldTransforms[ParentIndex]);
			}
		}
	}
}

void FAnimNode_ApplyOpenXRHandPose::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	if (!MappedBonePairs.bInitialized)
		return;

	/*const */FBPOpenXRActionSkeletalData *StoredActionInfoPtr = nullptr;
	if (bIsOpenInputAnimationInstance)
	{
		/*const*/ FOpenXRAnimInstanceProxy* OpenXRAnimInstance = (FOpenXRAnimInstanceProxy*)Output.AnimInstanceProxy;
		if (OpenXRAnimInstance->HandSkeletalActionData.Num())
		{
			for (int i = 0; i <OpenXRAnimInstance->HandSkeletalActionData.Num(); ++i)
			{
				EVRSkeletalHandIndex TargetHand = OpenXRAnimInstance->HandSkeletalActionData[i].TargetHand;

				if (OpenXRAnimInstance->HandSkeletalActionData[i].bMirrorLeftRight)
				{
					TargetHand = (TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left) ? EVRSkeletalHandIndex::EActionHandIndex_Right : EVRSkeletalHandIndex::EActionHandIndex_Left;
				}

				if (TargetHand == MappedBonePairs.TargetHand)
				{
					StoredActionInfoPtr = &OpenXRAnimInstance->HandSkeletalActionData[i];
					break;
				}
			}
		}
	}

	// If we have an empty hand pose but have a passed in custom one then use that
	if ((StoredActionInfoPtr == nullptr || !StoredActionInfoPtr->SkeletalTransforms.Num()) && OptionalStoredActionInfo.SkeletalTransforms.Num())
	{
		StoredActionInfoPtr = &OptionalStoredActionInfo;
	}

	//MappedBonePairs.AdjustmentQuat = WristAdjustment;

	// Currently not blending correctly
	const float BlendWeight = FMath::Clamp<float>(ActualAlpha, 0.f, 1.f);
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	uint8 BoneTransIndex = 0;
	uint8 NumBones = StoredActionInfoPtr ? StoredActionInfoPtr->SkeletalTransforms.Num() : 0;

	if (NumBones < 1)
	{
		// Early out, we don't have a valid data to work with
		return;
	}

	FTransform trans = FTransform::Identity;
	OutBoneTransforms.Reserve(MappedBonePairs.BonePairs.Num());
	TArray<FBoneTransform> TransBones;
	FTransform AdditionTransform = StoredActionInfoPtr->AdditionTransform;

	FTransform TempTrans = FTransform::Identity;
	FTransform ParentTrans = FTransform::Identity;
	FTransform * ParentTransPtr = nullptr;

	//AdditionTransform.SetRotation(MappedBonePairs.AdjustmentQuat);

	TArray<FTransform> HandTransforms;
	ConvertHandTransformsSpace(HandTransforms, StoredActionInfoPtr->SkeletalTransforms, AdditionTransform, StoredActionInfoPtr->bMirrorLeftRight, MappedBonePairs.bMergeMissingBonesUE4);

	for (const FBPOpenXRSkeletalPair& BonePair : MappedBonePairs.BonePairs)
	{
		BoneTransIndex = (int8)BonePair.OpenXRBone;
		ParentTrans = FTransform::Identity;

		if (BoneTransIndex >= NumBones || BonePair.ReferenceToConstruct.CachedCompactPoseIndex == INDEX_NONE)
			continue;		

		if (!BonePair.ReferenceToConstruct.IsValidToEvaluate(BoneContainer))
		{
			continue;
		}

		trans = Output.Pose.GetComponentSpaceTransform(BonePair.ReferenceToConstruct.CachedCompactPoseIndex);

		if (BonePair.ParentReference != INDEX_NONE)
		{
			ParentTrans = Output.Pose.GetComponentSpaceTransform(BonePair.ParentReference);
			ParentTrans.SetScale3D(FVector(1.f));
		}

		EXRHandJointType CurrentBone = (EXRHandJointType)BoneTransIndex;
		TempTrans = (HandTransforms[BoneTransIndex]);

		/*if (StoredActionInfoPtr->bMirrorHand)
		{
			FMatrix M = TempTrans.ToMatrixWithScale();
			M.Mirror(EAxis::Z, EAxis::X);
			M.Mirror(EAxis::X, EAxis::Z);
			TempTrans.SetFromMatrix(M);
		}*/

		TempTrans = TempTrans * ParentTrans;

		if (StoredActionInfoPtr->bAllowDeformingMesh || bOnlyApplyWristTransform)
			trans.SetTranslation(TempTrans.GetTranslation());

		trans.SetRotation(TempTrans.GetRotation());
		
		TransBones.Add(FBoneTransform(BonePair.ReferenceToConstruct.CachedCompactPoseIndex, trans));

		// Need to do it per bone so future bones are correct
		// Only if in parent space though, can do it all at the end in component space
		if (TransBones.Num())
		{
			Output.Pose.LocalBlendCSBoneTransforms(TransBones, BlendWeight);
			TransBones.Reset();
		}

		if (bOnlyApplyWristTransform && CurrentBone == EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT)
		{
			break; // Early out of the loop, we only wanted to apply the wrist
		}
	}
}

bool FAnimNode_ApplyOpenXRHandPose::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return(/*MappedBonePairs.bInitialized && */MappedBonePairs.BonePairs.Num() > 0);
}
