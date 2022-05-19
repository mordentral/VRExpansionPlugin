// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/HandSocketComponent.h"
#include "Engine/CollisionProfile.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogVRHandSocketComponent);

  //=============================================================================
UHandSocketComponent::UHandSocketComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicateMovement = false;
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// Setting absolute scale so we don't have to care about our parents scale
	this->SetUsingAbsoluteScale(true); 
	//this->bReplicates = true;

	bRepGameplayTags = true;

#if WITH_EDITORONLY_DATA
	bTickedPose = false;
	bShowVisualizationMesh = true;
	bMirrorVisualizationMesh = false;
	bShowRangeVisualization = false;
#endif

	HandRelativePlacement = FTransform::Identity;
	bAlwaysInRange = false;
	bDisabled = false;
	bMatchRotation = false;
	OverrideDistance = 0.0f;
	SlotPrefix = FName("VRGripP");
	bUseCustomPoseDeltas = false;
	HandTargetAnimation = nullptr;
	MirroredScale = FVector(1.f, 1.f, -1.f);
	bOnlySnapMesh = false;
	bFlipForLeftHand = false;
	bLeftHandDominant = false;
	bOnlyFlipRotation = false;

	MirrorAxis = EVRAxis::X;
	FlipAxis = EVRAxis::Y;
}

UAnimSequence* UHandSocketComponent::GetTargetAnimation()
{
	return HandTargetAnimation;
}

bool UHandSocketComponent::GetAnimationSequenceAsPoseSnapShot(UAnimSequence* InAnimationSequence, FPoseSnapshot& OutPoseSnapShot, USkeletalMeshComponent* TargetMesh, bool bSkipRootBone, bool bFlipHand)
{
	if (InAnimationSequence)
	{
		OutPoseSnapShot.SkeletalMeshName = /*TargetMesh ? TargetMesh->SkeletalMesh->GetFName(): */InAnimationSequence->GetSkeleton()->GetFName();
		OutPoseSnapShot.SnapshotName = InAnimationSequence->GetFName();
		OutPoseSnapShot.BoneNames.Empty();
		OutPoseSnapShot.LocalTransforms.Empty();

		TArray<FName> AnimSeqNames;

		if (USkeleton* AnimationSkele = InAnimationSequence->GetSkeleton())
		{
			// pre-size the array to avoid unnecessary reallocation
			OutPoseSnapShot.BoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			for (int32 i = 0; i < AnimationSkele->GetReferenceSkeleton().GetNum(); i++)
			{
				OutPoseSnapShot.BoneNames[i] = AnimationSkele->GetReferenceSkeleton().GetBoneName(i);
				if (bFlipHand)
				{
					FString bName = OutPoseSnapShot.BoneNames[i].ToString();

					if (bName.Contains("_r"))
					{
						bName = bName.Replace(TEXT("_r"), TEXT("_l"));
					}
					else
					{
						bName = bName.Replace(TEXT("_l"), TEXT("_r"));
					}

					OutPoseSnapShot.BoneNames[i] = FName(bName);
				}
			}
		}
		else
		{
			return false;
		}

		const FReferenceSkeleton& RefSkeleton = (TargetMesh) ? TargetMesh->SkeletalMesh->GetRefSkeleton() : InAnimationSequence->GetSkeleton()->GetReferenceSkeleton();
		FTransform LocalTransform;

		const TArray<FTrackToSkeletonMap>& TrackMap = InAnimationSequence->GetCompressedTrackToSkeletonMapTable();
		int32 TrackIndex = INDEX_NONE;

		OutPoseSnapShot.LocalTransforms.Reserve(OutPoseSnapShot.BoneNames.Num());

		for (int32 BoneNameIndex = 0; BoneNameIndex < OutPoseSnapShot.BoneNames.Num(); ++BoneNameIndex)
		{

			const FName& BoneName = OutPoseSnapShot.BoneNames[BoneNameIndex];

			TrackIndex = INDEX_NONE;
			if (BoneNameIndex != INDEX_NONE && BoneNameIndex < TrackMap.Num() && TrackMap[BoneNameIndex].BoneTreeIndex == BoneNameIndex)
			{
				TrackIndex = BoneNameIndex;
			}
			else
			{
				// This shouldn't happen but I need a fallback
				// Don't currently want to reconstruct the map inversely
				for (int i = 0; i < TrackMap.Num(); ++i)
				{
					if (TrackMap[i].BoneTreeIndex == BoneNameIndex)
					{
						TrackIndex = i;
						break;
					}
				}
			}

			if (TrackIndex != INDEX_NONE && (!bSkipRootBone || TrackIndex != 0))
			{
				InAnimationSequence->GetBoneTransform(LocalTransform, TrackIndex, 0.f, false);
			}
			else
			{
				// otherwise, get ref pose if exists
				const int32 BoneIDX = RefSkeleton.FindBoneIndex(BoneName);
				if (BoneIDX != INDEX_NONE)
				{
					LocalTransform = RefSkeleton.GetRefBonePose()[BoneIDX];
				}
				else
				{
					LocalTransform = FTransform::Identity;
				}
			}

			if (bFlipHand && (!bSkipRootBone || TrackIndex != 0))
			{
				FMatrix M = LocalTransform.ToMatrixWithScale();
				M.Mirror(EAxis::X, EAxis::X);
				M.Mirror(EAxis::Y, EAxis::Y);
				M.Mirror(EAxis::Z, EAxis::Z);
				LocalTransform.SetFromMatrix(M);
			}

			OutPoseSnapShot.LocalTransforms.Add(LocalTransform);
		}

		OutPoseSnapShot.bIsValid = true;
		return true;
	}

	return false;
}

bool UHandSocketComponent::GetBlendedPoseSnapShot(FPoseSnapshot& PoseSnapShot, USkeletalMeshComponent* TargetMesh, bool bSkipRootBone, bool bFlipHand)
{
	if (HandTargetAnimation)// && bUseCustomPoseDeltas && CustomPoseDeltas.Num() > 0)
	{
		PoseSnapShot.SkeletalMeshName = HandTargetAnimation->GetSkeleton()->GetFName();
		PoseSnapShot.SnapshotName = HandTargetAnimation->GetFName();
		PoseSnapShot.BoneNames.Empty();
		PoseSnapShot.LocalTransforms.Empty();
		TArray<FName> OrigBoneNames;

		if(USkeleton * AnimationSkele = HandTargetAnimation->GetSkeleton())
		{
			// pre-size the array to avoid unnecessary reallocation
			PoseSnapShot.BoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			OrigBoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			for (int32 i = 0; i < AnimationSkele->GetReferenceSkeleton().GetNum(); i++)
			{
				PoseSnapShot.BoneNames[i] = AnimationSkele->GetReferenceSkeleton().GetBoneName(i);
				OrigBoneNames[i] = PoseSnapShot.BoneNames[i];
				if (bFlipHand)
				{
					FString bName = PoseSnapShot.BoneNames[i].ToString();

					if (bName.Contains("_r"))
					{
						bName = bName.Replace(TEXT("_r"), TEXT("_l"));
					}
					else
					{
						bName = bName.Replace(TEXT("_l"), TEXT("_r"));
					}

					PoseSnapShot.BoneNames[i] = FName(bName);
				}
			}
		}
		else
		{
			return false;
		}

		const FReferenceSkeleton& RefSkeleton = (TargetMesh) ? TargetMesh->SkeletalMesh->GetRefSkeleton() : HandTargetAnimation->GetSkeleton()->GetReferenceSkeleton();
		FTransform LocalTransform;

		const TArray<FTrackToSkeletonMap>& TrackMap = HandTargetAnimation->GetCompressedTrackToSkeletonMapTable();
		int32 TrackIndex = INDEX_NONE;

		for (int32 BoneNameIndex = 0; BoneNameIndex < PoseSnapShot.BoneNames.Num(); ++BoneNameIndex)
		{
			TrackIndex = INDEX_NONE;
			if (BoneNameIndex < TrackMap.Num() && TrackMap[BoneNameIndex].BoneTreeIndex == BoneNameIndex)
			{
				TrackIndex = BoneNameIndex;
			}
			else
			{
				// This shouldn't happen but I need a fallback
				// Don't currently want to reconstruct the map inversely
				for (int i = 0; i < TrackMap.Num(); ++i)
				{
					if (TrackMap[i].BoneTreeIndex == BoneNameIndex)
					{
						TrackIndex = i;
						break;
					}
				}
			}

			const FName& BoneName = PoseSnapShot.BoneNames[BoneNameIndex];

			if (TrackIndex != INDEX_NONE && (!bSkipRootBone || TrackIndex != 0))
			{
				HandTargetAnimation->GetBoneTransform(LocalTransform, TrackIndex, 0.f, false);
			}
			else
			{
				// otherwise, get ref pose if exists
				const int32 BoneIDX = RefSkeleton.FindBoneIndex(BoneName);
				if (BoneIDX != INDEX_NONE)
				{
					LocalTransform = RefSkeleton.GetRefBonePose()[BoneIDX];
				}
				else
				{
					LocalTransform = FTransform::Identity;
				}
			}

			if (bUseCustomPoseDeltas)
			{
				FQuat DeltaQuat = FQuat::Identity;
				if (FBPVRHandPoseBonePair* HandPair = CustomPoseDeltas.FindByKey(OrigBoneNames[BoneNameIndex]))
				{
					DeltaQuat = HandPair->DeltaPose;
				}

				LocalTransform.ConcatenateRotation(DeltaQuat);
				LocalTransform.NormalizeRotation();
			}

			if (bFlipHand && (!bSkipRootBone || TrackIndex != 0))
			{
				FMatrix M = LocalTransform.ToMatrixWithScale();
				M.Mirror(EAxis::X, EAxis::X);
				M.Mirror(EAxis::Y, EAxis::Y);
				M.Mirror(EAxis::Z, EAxis::Z);
				LocalTransform.SetFromMatrix(M);
			}

			PoseSnapShot.LocalTransforms.Add(LocalTransform);
		}

		PoseSnapShot.bIsValid = true;
		return true;
	}
	else if (bUseCustomPoseDeltas && CustomPoseDeltas.Num() && TargetMesh)
	{
		PoseSnapShot.SkeletalMeshName = TargetMesh->SkeletalMesh->GetSkeleton()->GetFName();
		PoseSnapShot.SnapshotName = FName(TEXT("RawDeltaPose"));
		PoseSnapShot.BoneNames.Empty();
		PoseSnapShot.LocalTransforms.Empty();
		TargetMesh->GetBoneNames(PoseSnapShot.BoneNames);

		PoseSnapShot.LocalTransforms = TargetMesh->SkeletalMesh->GetSkeleton()->GetRefLocalPoses();

		FQuat DeltaQuat = FQuat::Identity;
		FName TargetBoneName = NAME_None;

		for (FBPVRHandPoseBonePair& HandPair : CustomPoseDeltas)
		{
			if (bFlipHand)
			{
				FString bName = HandPair.BoneName.ToString();

				if (bName.Contains("_r"))
				{
					bName = bName.Replace(TEXT("_r"), TEXT("_l"));
				}
				else
				{
					bName = bName.Replace(TEXT("_l"), TEXT("_r"));
				}

				TargetBoneName = FName(bName);
			}
			else
			{
				TargetBoneName = HandPair.BoneName;
			}

			int32 BoneIdx = TargetMesh->GetBoneIndex(TargetBoneName);
			if (BoneIdx != INDEX_NONE)
			{
				DeltaQuat = HandPair.DeltaPose;

				if (bFlipHand)
				{
					FTransform DeltaTrans(DeltaQuat);
					FMatrix M = DeltaTrans.ToMatrixWithScale();
					M.Mirror(EAxis::X, EAxis::X);
					M.Mirror(EAxis::Y, EAxis::Y);
					M.Mirror(EAxis::Z, EAxis::Z);
					DeltaTrans.SetFromMatrix(M);
					DeltaQuat = DeltaTrans.GetRotation();					
				}
			
				PoseSnapShot.LocalTransforms[BoneIdx].ConcatenateRotation(DeltaQuat);
				PoseSnapShot.LocalTransforms[BoneIdx].NormalizeRotation();

			}
		}

		PoseSnapShot.bIsValid = true;
		return true;
	}

	return false;
}

FTransform UHandSocketComponent::GetHandRelativePlacement()
{
	// Optionally mirror for left hand

	if (bDecoupleMeshPlacement)
	{
		if (USceneComponent* ParentComp = GetAttachParent())
		{
			return HandRelativePlacement.GetRelativeTransform(this->GetRelativeTransform());
			//FTransform curTrans = HandRelativePlacement * ParentComp->GetComponentTransform();
			//return curTrans.GetRelativeTransform(this->GetComponentTransform());
		}
	}

	return HandRelativePlacement;
}

FTransform UHandSocketComponent::GetHandSocketTransform(UGripMotionControllerComponent* QueryController, bool bIgnoreOnlySnapMesh)
{
	// Optionally mirror for left hand

	if (!bIgnoreOnlySnapMesh && bOnlySnapMesh)
	{
		if (!QueryController)
		{
			// No controller input
			UE_LOG(LogVRMotionController, Warning, TEXT("HandSocketComponent::GetHandSocketTransform was missing required motion controller for bOnlySnapMesh! Check that you are passing a controller into GetClosestSocketInRange!"));
		}
		else
		{
			return QueryController->GetPivotTransform();
		}
	}

	if (bFlipForLeftHand)
	{
		if (!QueryController)
		{
			// No controller input
			UE_LOG(LogVRMotionController, Warning, TEXT("HandSocketComponent::GetHandSocketTransform was missing required motion controller for bFlipForLeftand! Check that you are passing a controller into GetClosestSocketInRange!"));
		}
		else
		{
			EControllerHand HandType;
			QueryController->GetHandType(HandType);
			bool bIsRightHand = HandType == EControllerHand::Right;
			if (bLeftHandDominant == bIsRightHand)
			{
				FTransform ReturnTrans = this->GetRelativeTransform();
				ReturnTrans.Mirror(GetAsEAxis(MirrorAxis), GetAsEAxis(FlipAxis));
				if (bOnlyFlipRotation)
				{
					ReturnTrans.SetTranslation(this->GetRelativeLocation());
				}

				if (USceneComponent* AttParent = this->GetAttachParent())
				{
					ReturnTrans = ReturnTrans * AttParent->GetComponentTransform();
				}
				return ReturnTrans;
			}
		}
	}

	return this->GetComponentTransform();
}

FTransform UHandSocketComponent::GetMeshRelativeTransform(bool bIsRightHand, bool bUseParentScale)
{
	// Optionally mirror for left hand

	// Failsafe
	if (!this->GetAttachParent())
		return FTransform::Identity;

	FTransform relTrans = this->GetRelativeTransform();
	FTransform HandTrans = GetHandRelativePlacement();
	FTransform ReturnTrans = FTransform::Identity;

	// Fix the scale
	if (!bUseParentScale && this->IsUsingAbsoluteScale() /*&& !bDecoupleMeshPlacement*/)
	{
		FVector ParentScale = this->GetAttachParent()->GetComponentScale();
		// Take parent scale out of our relative transform early
		relTrans.ScaleTranslation(ParentScale);
		ReturnTrans = HandTrans * relTrans;
		// We add in the inverse of the parent scale to adjust the hand mesh
		ReturnTrans.ScaleTranslation((FVector(1.0f) / ParentScale));
		ReturnTrans.SetScale3D(FVector(1.0f));
	}
	else
	{
		ReturnTrans = HandTrans * relTrans;
	}

	// If we should mirror the transform, do it now that it is in our parent relative space
	if ((bFlipForLeftHand && (bLeftHandDominant == bIsRightHand)))
	{
		//FTransform relTrans = this->GetRelativeTransform();
		MirrorHandTransform(ReturnTrans, relTrans);
	}


	return ReturnTrans;
}

FTransform UHandSocketComponent::GetBoneTransformAtTime(UAnimSequence* MyAnimSequence, /*float AnimTime,*/ int BoneIdx, bool bUseRawDataOnly)
{
	float tracklen = MyAnimSequence->GetPlayLength();
	FTransform BoneTransform = FTransform::Identity;
	const TArray<FTrackToSkeletonMap>& TrackToSkeletonMap = bUseRawDataOnly ? MyAnimSequence->GetRawTrackToSkeletonMapTable() : MyAnimSequence->GetCompressedTrackToSkeletonMapTable();

	if ((TrackToSkeletonMap.Num() > BoneIdx) && (TrackToSkeletonMap[0].BoneTreeIndex == 0))
	{
		MyAnimSequence->GetBoneTransform(BoneTransform, BoneIdx, /*AnimTime*/ tracklen, bUseRawDataOnly);
		return BoneTransform;
	}
	return FTransform::Identity;
}

void UHandSocketComponent::OnRegister()
{
#if WITH_EDITORONLY_DATA
	AActor* MyOwner = GetOwner();
	if (bShowVisualizationMesh && (MyOwner != nullptr) && !IsRunningCommandlet())
	{
		if (HandVisualizerComponent == nullptr && bShowVisualizationMesh)
		{
			HandVisualizerComponent = NewObject<UPoseableMeshComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			HandVisualizerComponent->SetupAttachment(this);
			HandVisualizerComponent->SetIsVisualizationComponent(true);
			HandVisualizerComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			HandVisualizerComponent->CastShadow = false;
			HandVisualizerComponent->CreationMethod = CreationMethod;
			//HandVisualizerComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HandVisualizerComponent->SetComponentTickEnabled(false);
			HandVisualizerComponent->SetHiddenInGame(true);
			HandVisualizerComponent->RegisterComponentWithWorld(GetWorld());
			//HandVisualizerComponent->SetUsingAbsoluteScale(true);
		}
		else if (!bShowVisualizationMesh && HandVisualizerComponent)
		{
			HideVisualizationMesh();
		}

		if (HandVisualizerComponent)
		{
			bTickedPose = false;

			if (VisualizationMesh)
			{
				HandVisualizerComponent->SetSkeletalMesh(VisualizationMesh);
				if (HandPreviewMaterial)
				{
					HandVisualizerComponent->SetMaterial(0, HandPreviewMaterial);
				}
			}

			PositionVisualizationMesh();
			PoseVisualizationToAnimation(true);
		}
	}

#endif	// WITH_EDITORONLY_DATA

	Super::OnRegister();
}

#if WITH_EDITORONLY_DATA

void UHandSocketComponent::PositionVisualizationMesh()
{
	if (!HandVisualizerComponent)
	{
		return;
	}

	if (USceneComponent* ParentAttach = this->GetAttachParent())
	{
		FTransform relTrans = this->GetRelativeTransform();
		FTransform HandPlacement = GetHandRelativePlacement();
		FTransform ReturnTrans = (HandPlacement * relTrans);

		if (bMirrorVisualizationMesh)//(bFlipForLeftHand && !bIsRightHand))
		{
			MirrorHandTransform(ReturnTrans, relTrans);
		}

		if ((bLeftHandDominant && !bMirrorVisualizationMesh) || (!bLeftHandDominant && bMirrorVisualizationMesh))
		{
			ReturnTrans.SetScale3D(ReturnTrans.GetScale3D() * MirroredScale);
		}

		HandVisualizerComponent->SetRelativeTransform(ReturnTrans.GetRelativeTransform(relTrans)/*newRel*/);
	}
}

void UHandSocketComponent::HideVisualizationMesh()
{
	if (!bShowVisualizationMesh && HandVisualizerComponent)
	{
		HandVisualizerComponent->SetVisibility(false);
		HandVisualizerComponent->DestroyComponent();
		HandVisualizerComponent = nullptr;
	}
}

#endif

#if WITH_EDITORONLY_DATA
void UHandSocketComponent::PoseVisualizationToAnimation(bool bForceRefresh)
{

	if (!HandVisualizerComponent || !HandVisualizerComponent->SkeletalMesh)
		return;

	TArray<FTransform> LocalPoses;
	if (!HandTargetAnimation)
	{
		// Store local poses for posing
		LocalPoses = HandVisualizerComponent->SkeletalMesh->GetSkeleton()->GetRefLocalPoses();
	}

	TArray<FName> BonesNames;
	HandVisualizerComponent->GetBoneNames(BonesNames);
	int32 Bones = HandVisualizerComponent->GetNumBones();

	for (int32 i = 0; i < Bones; i++)
	{
		if (!HandTargetAnimation && !bUseCustomPoseDeltas)
		{
			HandVisualizerComponent->ResetBoneTransformByName(BonesNames[i]);
			continue;
		}

		FName ParentBone = HandVisualizerComponent->GetParentBone(BonesNames[i]);
		FTransform ParentTrans = FTransform::Identity;
		if (ParentBone != NAME_None)
		{
			ParentTrans = HandVisualizerComponent->GetBoneTransformByName(ParentBone, EBoneSpaces::ComponentSpace);
		}


		FQuat DeltaQuat = FQuat::Identity;
		if (bUseCustomPoseDeltas)
		{
			for (FBPVRHandPoseBonePair BonePairC : CustomPoseDeltas)
			{
				if (BonePairC.BoneName == BonesNames[i])
				{
					DeltaQuat = BonePairC.DeltaPose;
					DeltaQuat.Normalize();
					break;
				}
			}
		}

		FTransform BoneTrans = FTransform::Identity;

		if (HandTargetAnimation)
		{
			BoneTrans = GetBoneTransformAtTime(HandTargetAnimation, /*FLT_MAX,*/ i, false); // true;
		}
		else
		{
			BoneTrans = LocalPoses[i];
		}

		BoneTrans = BoneTrans * ParentTrans;// *HandVisualizerComponent->GetComponentTransform();
		BoneTrans.NormalizeRotation();

		//DeltaQuat *= HandVisualizerComponent->GetComponentTransform().GetRotation().Inverse();

		BoneTrans.ConcatenateRotation(DeltaQuat);
		BoneTrans.NormalizeRotation();
		HandVisualizerComponent->SetBoneTransformByName(BonesNames[i], BoneTrans, EBoneSpaces::ComponentSpace);

	}

	if (HandVisualizerComponent && (!bTickedPose || bForceRefresh))
	{
		// Tick Pose first
		if (HandVisualizerComponent->IsRegistered())
		{
			bTickedPose = true;
			HandVisualizerComponent->TickPose(1.0f, false);
			if (HandVisualizerComponent->MasterPoseComponent.IsValid())
			{
				HandVisualizerComponent->UpdateSlaveComponent();
			}
			else
			{
				HandVisualizerComponent->RefreshBoneTransforms(&HandVisualizerComponent->PrimaryComponentTick);
			}
		}
	}
}

void UHandSocketComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UHandSocketComponent* This = CastChecked<UHandSocketComponent>(InThis);
	Collector.AddReferencedObject(This->HandVisualizerComponent);

	Super::AddReferencedObjects(InThis, Collector);
}

void UHandSocketComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);

	if (HandVisualizerComponent)
	{
		HandVisualizerComponent->DestroyComponent();
	}
}

#endif

void UHandSocketComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(UHandSocketComponent, bRepGameplayTags);
	DOREPLIFETIME(UHandSocketComponent, bReplicateMovement);
	DOREPLIFETIME_CONDITION(UHandSocketComponent, GameplayTags, COND_Custom);
}

void UHandSocketComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	DOREPLIFETIME_ACTIVE_OVERRIDE(UHandSocketComponent, GameplayTags, bRepGameplayTags);

	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_PRIVATE_PROPERTY(USceneComponent, RelativeScale3D, bReplicateMovement);
}

//=============================================================================
UHandSocketComponent::~UHandSocketComponent()
{
}

#if WITH_EDITOR

void UHandSocketComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.Property;

	if (PropertyThatChanged != nullptr)
	{
#if WITH_EDITORONLY_DATA
		if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandTargetAnimation) ||
			PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, VisualizationMesh)
			)
		{
			PositionVisualizationMesh();
			PoseVisualizationToAnimation(true);
		}
		else if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, CustomPoseDeltas))
		{
			PoseVisualizationToAnimation(true);
		}
		else if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement))
		{
			PositionVisualizationMesh();
		}
		else if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bShowVisualizationMesh))
		{
			HideVisualizationMesh();
		}
#endif
	}
}
#endif

