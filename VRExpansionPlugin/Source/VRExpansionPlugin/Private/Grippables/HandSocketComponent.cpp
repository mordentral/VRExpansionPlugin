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
	MirroredScale = FVector(1.f, 1.f, -1.f);
#endif

	HandRelativePlacement = FTransform::Identity;
	OverrideDistance = 0.0f;
	SlotPrefix = FName("VRGripP");
	bUseCustomPoseDeltas = false;
	HandTargetAnimation = nullptr;
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

bool UHandSocketComponent::GetAnimationSequenceAsPoseSnapShot(UAnimSequence* InAnimationSequence, FPoseSnapshot& OutPoseSnapShot, USkeletalMeshComponent* TargetMesh)
{
	if (InAnimationSequence)
	{
		OutPoseSnapShot.SkeletalMeshName = InAnimationSequence->GetSkeleton()->GetFName();
		OutPoseSnapShot.SnapshotName = InAnimationSequence->GetFName();
		OutPoseSnapShot.BoneNames.Empty();
		OutPoseSnapShot.LocalTransforms.Empty();

		if (TargetMesh)
		{
			TargetMesh->GetBoneNames(OutPoseSnapShot.BoneNames);
		}
		else if (USkeleton* AnimationSkele = InAnimationSequence->GetSkeleton())
		{
			// pre-size the array to avoid unnecessary reallocation
			OutPoseSnapShot.BoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			for (int32 i = 0; i < AnimationSkele->GetReferenceSkeleton().GetNum(); i++)
			{
				OutPoseSnapShot.BoneNames[i] = AnimationSkele->GetReferenceSkeleton().GetBoneName(i);
			}
		}
		else
		{
			return false;
		}

		const FReferenceSkeleton& RefSkeleton = (TargetMesh) ? TargetMesh->SkeletalMesh->RefSkeleton : InAnimationSequence->GetSkeleton()->GetReferenceSkeleton();
		FTransform LocalTransform;

		const TArray<FTrackToSkeletonMap>& TrackMap = InAnimationSequence->GetCompressedTrackToSkeletonMapTable();
		int32 TrackIndex = INDEX_NONE;

		for (int32 BoneNameIndex = 0; BoneNameIndex < OutPoseSnapShot.BoneNames.Num(); ++BoneNameIndex)
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

			const FName& BoneName = OutPoseSnapShot.BoneNames[BoneNameIndex];

			if (TrackIndex != INDEX_NONE)
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

			OutPoseSnapShot.LocalTransforms.Add(LocalTransform);
		}

		return true;
	}

	return false;
}

bool UHandSocketComponent::GetBlendedPoseSnapShot(FPoseSnapshot& PoseSnapShot, USkeletalMeshComponent* TargetMesh)
{
	if (HandTargetAnimation)// && bUseCustomPoseDeltas && CustomPoseDeltas.Num() > 0)
	{
		PoseSnapShot.SkeletalMeshName = HandTargetAnimation->GetSkeleton()->GetFName();
		PoseSnapShot.SnapshotName = HandTargetAnimation->GetFName();
		PoseSnapShot.BoneNames.Empty();
		PoseSnapShot.LocalTransforms.Empty();

		if (TargetMesh)
		{
			TargetMesh->GetBoneNames(PoseSnapShot.BoneNames);
		}
		else if(USkeleton * AnimationSkele = HandTargetAnimation->GetSkeleton())
		{
			// pre-size the array to avoid unnecessary reallocation
			PoseSnapShot.BoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			for (int32 i = 0; i < AnimationSkele->GetReferenceSkeleton().GetNum(); i++)
			{
				PoseSnapShot.BoneNames[i] = AnimationSkele->GetReferenceSkeleton().GetBoneName(i);
			}
		}
		else
		{
			return false;
		}

		const FReferenceSkeleton& RefSkeleton = (TargetMesh) ? TargetMesh->SkeletalMesh->RefSkeleton : HandTargetAnimation->GetSkeleton()->GetReferenceSkeleton();
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

			if (TrackIndex != INDEX_NONE)
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

			FQuat DeltaQuat = FQuat::Identity;
			for (FBPVRHandPoseBonePair& HandPair : CustomPoseDeltas)
			{
				if (HandPair.BoneName == PoseSnapShot.BoneNames[BoneNameIndex])
				{
					DeltaQuat = HandPair.DeltaPose;
					break;
				}
			}

			LocalTransform.ConcatenateRotation(DeltaQuat);
			LocalTransform.NormalizeRotation();

			PoseSnapShot.LocalTransforms.Add(LocalTransform);
		}

		return true;
	}
	else if (CustomPoseDeltas.Num() && TargetMesh)
	{
		PoseSnapShot.SkeletalMeshName = TargetMesh->SkeletalMesh->Skeleton->GetFName();
		PoseSnapShot.SnapshotName = FName(TEXT("RawDeltaPose"));
		PoseSnapShot.BoneNames.Empty();
		PoseSnapShot.LocalTransforms.Empty();
		TargetMesh->GetBoneNames(PoseSnapShot.BoneNames);

		PoseSnapShot.LocalTransforms = TargetMesh->SkeletalMesh->Skeleton->GetRefLocalPoses();

		FQuat DeltaQuat = FQuat::Identity;
		for (FBPVRHandPoseBonePair& HandPair : CustomPoseDeltas)
		{
			int32 BoneIdx = TargetMesh->GetBoneIndex(HandPair.BoneName);
			if (BoneIdx != INDEX_NONE)
			{
				DeltaQuat = HandPair.DeltaPose;
				PoseSnapShot.LocalTransforms[BoneIdx].ConcatenateRotation(HandPair.DeltaPose);
				PoseSnapShot.LocalTransforms[BoneIdx].NormalizeRotation();
			}
		}

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

FTransform UHandSocketComponent::GetHandSocketTransform(UGripMotionControllerComponent* QueryController)
{
	// Optionally mirror for left hand

	if (bOnlySnapMesh)
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

FTransform UHandSocketComponent::GetMeshRelativeTransform(bool bIsRightHand)
{
	// Optionally mirror for left hand

	FTransform relTrans = this->GetRelativeTransform();
	FTransform HandPlacement = GetHandRelativePlacement();

	if (this->IsUsingAbsoluteScale() && !bDecoupleMeshPlacement)
	{
		if (this->GetAttachParent())
		{
			HandPlacement.ScaleTranslation(/*FVector(1.0f) / */this->GetAttachParent()->GetRelativeScale3D());
		}
	}

	FTransform ReturnTrans = (HandPlacement * relTrans);

	if ((bFlipForLeftHand && (bLeftHandDominant == bIsRightHand)))
	{

		if (!bOnlyFlipRotation)
		{
			ReturnTrans.SetTranslation(ReturnTrans.GetTranslation().MirrorByVector(GetMirrorVector()));
		}

		FRotationMatrix test(ReturnTrans.GetRotation().Rotator());
		test.Mirror(GetAsEAxis(MirrorAxis), GetCrossAxis());
		//test.Mirror(MirrorAxis, FlipAxis);
		ReturnTrans.SetRotation(test.ToQuat());
		//ReturnTrans.Mirror(MirrorAxis, FlipAxis);
	}

	FTransform CorrectTrans = ReturnTrans.GetRelativeTransform(relTrans);

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
		if (HandVisualizerComponent == nullptr)
		{
			//HandVisualizerComponent = NewObject<USkeletalMeshComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			HandVisualizerComponent = NewObject<UPoseableMeshComponent>(MyOwner, NAME_None, RF_Transactional | RF_TextExportTransient);
			if (HandVisualizerComponent)
			{
				HandVisualizerComponent->SetupAttachment(this);
				HandVisualizerComponent->SetIsVisualizationComponent(true);
				HandVisualizerComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
				HandVisualizerComponent->CastShadow = false;
				HandVisualizerComponent->CreationMethod = CreationMethod;
				//HandVisualizerComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				HandVisualizerComponent->SetComponentTickEnabled(false);
				HandVisualizerComponent->SetHiddenInGame(true);
				HandVisualizerComponent->RegisterComponentWithWorld(GetWorld());

				if (VisualizationMesh)
				{
					HandVisualizerComponent->SetSkeletalMesh(VisualizationMesh);
					if (HandPreviewMaterial)
					{
						HandVisualizerComponent->SetMaterial(0, HandPreviewMaterial);
					}
				}

				if (USceneComponent* ParentAttach = this->GetAttachParent())
				{
					FTransform relTrans = this->GetRelativeTransform();
					FTransform HandPlacement = GetHandRelativePlacement();

					if (this->IsUsingAbsoluteScale() && !bDecoupleMeshPlacement)
					{
						HandPlacement.ScaleTranslation(/*FVector(1.0f) / */ParentAttach->GetRelativeScale3D());
					}

					if ((bLeftHandDominant && !bMirrorVisualizationMesh) || (!bLeftHandDominant && bMirrorVisualizationMesh))
					{
						HandPlacement.SetScale3D(HandPlacement.GetScale3D() * MirroredScale);
					}


					FTransform ReturnTrans = (HandPlacement * relTrans);

					if (bMirrorVisualizationMesh)//(bFlipForLeftHand && !bIsRightHand))
					{
						if (!bOnlyFlipRotation)
						{
							ReturnTrans.SetTranslation(ReturnTrans.GetTranslation().MirrorByVector(GetMirrorVector()));
						}

						FRotationMatrix test(ReturnTrans.GetRotation().Rotator());
						test.Mirror(GetAsEAxis(MirrorAxis), GetCrossAxis());
						ReturnTrans.SetRotation(test.ToQuat());
						//ReturnTrans.Mirror(MirrorAxis, FlipAxis);
					}

					FTransform RelativeTrans = ReturnTrans;

					HandVisualizerComponent->SetRelativeTransform(ReturnTrans.GetRelativeTransform(relTrans)/*newRel*/);
				}

				PoseVisualizationToAnimation();
			}
		}
	}

#endif	// WITH_EDITORONLY_DATA

	Super::OnRegister();
}

#if WITH_EDITORONLY_DATA
void UHandSocketComponent::PoseVisualizationToAnimation(bool bForceRefresh)
{

	if (!HandVisualizerComponent || !HandVisualizerComponent->SkeletalMesh)
		return;

	TArray<FTransform> LocalPoses;
	if (!HandTargetAnimation)
	{
		// Store local poses for posing
		LocalPoses = HandVisualizerComponent->SkeletalMesh->Skeleton->GetRefLocalPoses();
	}

	TArray<FName> BonesNames;
	HandVisualizerComponent->GetBoneNames(BonesNames);
	int32 Bones = HandVisualizerComponent->GetNumBones();

	for (int32 i = 0; i < Bones; i++)
	{
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

	if (HandVisualizerComponent && !bTickedPose)
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
		}
		else if (PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, CustomPoseDeltas))
		{
			//PoseVisualizationToAnimation(true);
		}
#endif
	}
}
#endif

