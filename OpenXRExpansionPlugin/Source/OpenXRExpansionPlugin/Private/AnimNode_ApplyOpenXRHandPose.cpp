// Fill out your copyright notice in the Description page of Project Settings.
#include "AnimNode_ApplyOpenXRHandPose.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNode_ApplyOpenXRHandPose)

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

	if (const UOpenXRAnimInstance * OpenXRAnimInstance = Cast<UOpenXRAnimInstance>(InAnimInstance))
	{
		bIsOpenInputAnimationInstance = true;

		if (OpenXRAnimInstance->AnimInstanceProxy.HandSkeletalActionData.Num())
		{
			for (int i = 0; i < OpenXRAnimInstance->AnimInstanceProxy.HandSkeletalActionData.Num(); ++i)
			{
				EVRSkeletalHandIndex TargetHand = OpenXRAnimInstance->AnimInstanceProxy.HandSkeletalActionData[i].TargetHand;

				if (OpenXRAnimInstance->AnimInstanceProxy.HandSkeletalActionData[i].bMirrorLeftRight)
				{
					TargetHand = (TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left) ? EVRSkeletalHandIndex::EActionHandIndex_Right : EVRSkeletalHandIndex::EActionHandIndex_Left;
				}

				if (TargetHand == MappedBonePairs.TargetHand)
				{
					bIsMirroringHand = OpenXRAnimInstance->AnimInstanceProxy.HandSkeletalActionData[i].bMirrorLeftRight;
					break;
				}
			}
		}
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

	USkeleton* AssetSkeleton = RequiredBones.GetSkeletonAsset();

	if (!AssetSkeleton)
		return;

	if (!MappedBonePairs.bInitialized || OwningAsset->GetFName() != MappedBonePairs.LastInitializedName || SkeletonType != MappedBonePairs.LastInitializedSkeleton)
	{

		// Trigger a full re-build if our asset changed
		if (MappedBonePairs.bInitialized && (OwningAsset->GetFName() != MappedBonePairs.LastInitializedName || SkeletonType != MappedBonePairs.LastInitializedSkeleton))
		{
			MappedBonePairs.ClearMapping();
		}

		MappedBonePairs.LastInitializedName = OwningAsset->GetFName();
		MappedBonePairs.LastInitializedSkeleton = SkeletonType;
		MappedBonePairs.bInitialized = false;
		
		if (AssetSkeleton)
		{
			// If our bone pairs are empty, then setup our sane defaults
			if (!MappedBonePairs.BonePairs.Num())
			{

				MappedBonePairs.ConstructDefaultMappings(SkeletonType, bSkipRootBone);
			}

			// Construct a reverse map of our joints
			MappedBonePairs.ConstructReverseMapping();

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
				}
			}

			MappedBonePairs.bInitialized = true;

			if (SkeletonType == EVROpenXRSkeletonType::OXR_SkeletonType_OpenVRDefault_Left || SkeletonType == EVROpenXRSkeletonType::OXR_SkeletonType_OpenVRDefault_Right)
			{
				// We hard code this for now because I don't like their wrist being a different transform
				MappedBonePairs.AdjustmentQuat = FRotator(0.f, 90.f, 180.f).Quaternion(); // Current one is incorrect without wrist
				// Maybe do it in relative space?
			}
			else
			{
				CalculateSkeletalAdjustment(AssetSkeleton);
			}
			
		}
	}
}

void FAnimNode_ApplyOpenXRHandPose::CalculateSkeletalAdjustment(USkeleton* AssetSkeleton)
{

	TArray<FTransform> RefBones = AssetSkeleton->GetReferenceSkeleton().GetRefBonePose();
	TArray<FMeshBoneInfo> RefBonesInfo = AssetSkeleton->GetReferenceSkeleton().GetRefBoneInfo();

	if (!MappedBonePairs.bInitialized || MappedBonePairs.BonePairs.Num() < 4 || !RefBones.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("Empty or incorrect mapping or skeleton data when calculating skeletal adjustment!"));
		return;
	}

	FBPOpenXRSkeletalPair KnuckleIndexPair = MappedBonePairs.BonePairs[MappedBonePairs.ReverseBonePairMap[(int8)EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT]];
	FBPOpenXRSkeletalPair KnuckleMiddlePair = MappedBonePairs.BonePairs[MappedBonePairs.ReverseBonePairMap[(int8)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT]];
	FBPOpenXRSkeletalPair KnuckleRingPair = MappedBonePairs.BonePairs[MappedBonePairs.ReverseBonePairMap[(int8)EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT]];
	FBPOpenXRSkeletalPair KnucklePinkyPair = MappedBonePairs.BonePairs[MappedBonePairs.ReverseBonePairMap[(int8)EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT]];

	FBPOpenXRSkeletalPair WristPair = MappedBonePairs.BonePairs[MappedBonePairs.ReverseBonePairMap[(int8)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT]];

	FVector KnuckleAverage = GetRefBoneInCS(RefBones, RefBonesInfo, KnuckleIndexPair.ReferenceToConstruct.BoneIndex).GetTranslation();
	KnuckleAverage += GetRefBoneInCS(RefBones, RefBonesInfo, KnuckleMiddlePair.ReferenceToConstruct.BoneIndex).GetTranslation();
	KnuckleAverage += GetRefBoneInCS(RefBones, RefBonesInfo, KnuckleRingPair.ReferenceToConstruct.BoneIndex).GetTranslation();
	KnuckleAverage += GetRefBoneInCS(RefBones, RefBonesInfo, KnucklePinkyPair.ReferenceToConstruct.BoneIndex).GetTranslation();

	// Get our average across the knuckles
	KnuckleAverage /= 4.f;

	// Obtain the UE4 wrist Side & Forward directions from first animation frame and place in cache 
	FTransform WristTransform_UE = GetRefBoneInCS(RefBones, RefBonesInfo, WristPair.ReferenceToConstruct.BoneIndex);
	FVector ToKnuckleAverage_UE = KnuckleAverage - WristTransform_UE.GetTranslation();
	ToKnuckleAverage_UE.Normalize();


	WristForwardLS_UE = WristTransform_UE.GetRotation().UnrotateVector(ToKnuckleAverage_UE);
	SetVectorToMaxElement(WristForwardLS_UE);
	WristSideDirectionLS = FVector::CrossProduct(WristForwardLS_UE, FVector::RightVector);
	SetVectorToMaxElement(WristSideDirectionLS);

	CalculateOpenXRAdjustment();
}

void FAnimNode_ApplyOpenXRHandPose::CalculateOpenXRAdjustment()
{
	// Base implementation is like valves

	// Forward direction
	static FVector OpenXRForwardDirection = FVector(1.0f, 0.f, 0.f);

	// Side direction
	// Do I need to flip this for left hand?

	bool bUseLeftHandOffsets = false;
	if ((!bIsMirroringHand && MappedBonePairs.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left) ||
		(bIsMirroringHand && MappedBonePairs.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Right))
	{
		bUseLeftHandOffsets = true;
	}

	//static FVector OpenXRSideDirection = FVector(0.f, 1.f, 0.f);
	FVector OpenXRSideDirection = bUseLeftHandOffsets ? FVector(0.f, -1.f, 0.f) : FVector(0.f, 1.f, 0.f);

	// Align forward vectors, openXR once in engine is X+ forward
	FQuat AlignmentRot = FQuat::FindBetweenNormals(WristForwardLS_UE, OpenXRForwardDirection);

	// Rotate about the aligned forward direction to make the side directions align
	FVector WristSideDirectionMS_UE = AlignmentRot * WristSideDirectionLS;

	// Rotate around, should the Side direction flip for openXR if its the left hand?
	FQuat TwistRotation = CalcRotationAboutAxis(WristSideDirectionMS_UE, OpenXRSideDirection, OpenXRForwardDirection);

	FRotator Difference = (TwistRotation * AlignmentRot).Rotator();

	MappedBonePairs.AdjustmentQuat = (TwistRotation * AlignmentRot).GetNormalized();

}

void FAnimNode_ApplyOpenXRHandPose::ConvertHandTransformsSpace(TArray<FTransform>& OutTransforms, const TArray<FTransform>& WorldTransforms, FTransform AddTrans, bool bMirrorLeftRight, bool bMergeMissingUE4Bones)
{
	// Fail if the count is too low
	if (WorldTransforms.Num() < EHandKeypointCount)
		return;

	if (OutTransforms.Num() < WorldTransforms.Num())
	{
		OutTransforms.Empty(WorldTransforms.Num());
		OutTransforms.AddUninitialized(WorldTransforms.Num());
	}

	TArray<FTransform> TempWorldTransforms = WorldTransforms;

	// Ensure add trans is normalized
	AddTrans.NormalizeRotation();

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

	bool bUseAutoCalculatedRetarget = AddTrans.Equals(FTransform::Identity);

	// Convert transforms to parent space
	// The hand tracking transforms are in world space.

	for (int32 Index = 0; Index < EHandKeypointCount; ++Index)
	{
		if (TempWorldTransforms[Index].ContainsNaN() || TempWorldTransforms[Index].Equals(FTransform::Identity))
		{
			OutTransforms[Index] = FTransform::Identity;
			//continue;
		}

		// Ensure normalization
		TempWorldTransforms[Index].NormalizeRotation();

		if (bMirrorLeftRight)
		{
			TempWorldTransforms[Index].Mirror(EAxis::Y, EAxis::Y);
		}

		if (bUseAutoCalculatedRetarget)
		{
			TempWorldTransforms[Index].ConcatenateRotation(MappedBonePairs.AdjustmentQuat);
			//WorldTransforms[Index].ConcatenateRotation(MappedBonePairs.BonePairs[0].RetargetRot);
		}
		else
		{
			TempWorldTransforms[Index].ConcatenateRotation(AddTrans.GetRotation());
		}
	}

	// Make this into a single loop, their structure always has children after parent
	for (int32 Index = 0; Index < EHandKeypointCount; ++Index)
	{
		FTransform& BoneTransform = TempWorldTransforms[Index];
		//BoneTransform.NormalizeRotation();

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
			FTransform ParentTransform = FTransform::Identity;

			// Merging missing metacarpal bone into the transform
			if (bMergeMissingUE4Bones && ParentParent == 1) // Wrist
			{
				ParentTransform = TempWorldTransforms[ParentParent];
			}
			else
			{
				ParentTransform = TempWorldTransforms[ParentIndex];
			}

			//ParentTransform.NormalizeRotation();
			OutTransforms[Index] = BoneTransform.GetRelativeTransform(ParentTransform);
		}
	}
}

void FAnimNode_ApplyOpenXRHandPose::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	if (!MappedBonePairs.bInitialized)
		return;


	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();

	UObject* OwningAsset = BoneContainer.GetAsset();
	if (!OwningAsset)
		return;

	// Trigger a full re-build if our asset or target skeleton changed, do it up here before finding the correct hand
	if ((OwningAsset->GetFName() != MappedBonePairs.LastInitializedName || SkeletonType != MappedBonePairs.LastInitializedSkeleton))
	{
		InitializeBoneReferences(BoneContainer);
	}


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
	if (StoredActionInfoPtr == nullptr || !StoredActionInfoPtr->SkeletalTransforms.Num())
	{
		StoredActionInfoPtr = &OptionalStoredActionInfo;
	}

	if (!StoredActionInfoPtr->bHasValidData)
	{
		return;
	}

	//MappedBonePairs.AdjustmentQuat = WristAdjustment;

	// Currently not blending correctly
	const float BlendWeight = FMath::Clamp<float>(ActualAlpha, 0.f, 1.f);
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

		if (bSkipRootBone && BonePair.OpenXRBone == EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT)
			continue;

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
		//TempTrans.ConcatenateRotation(BonePair.RetargetRot);

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
