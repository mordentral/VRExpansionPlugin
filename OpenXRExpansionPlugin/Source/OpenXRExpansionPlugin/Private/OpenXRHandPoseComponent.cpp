// Fill out your copyright notice in the Description page of Project Settings.
#include "OpenXRHandPoseComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(OpenXRHandPoseComponent)

#include "Net/UnrealNetwork.h"
#include "MotionControllerComponent.h"
#include "OpenXRExpansionFunctionLibrary.h"
#include "Engine/NetSerialization.h"
#include "Net/Core/PushModel/PushModel.h"

#include "XRMotionControllerBase.h" // for GetHandEnumForSourceName()
//#include "EngineMinimal.h"

UOpenXRHandPoseComponent::UOpenXRHandPoseComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	ReplicationRateForSkeletalAnimations = 10.f;
	bReplicateSkeletalData = false;
	bSmoothReplicatedSkeletalData = true;
	SkeletalNetUpdateCount = 0.f;
	bDetectGestures = true;
	SetIsReplicatedByDefault(true);
	bGetMockUpPoseForDebugging = false;
}

void UOpenXRHandPoseComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams SkipOwnerParams;
	SkipOwnerParams.Condition = COND_SkipOwner;
	SkipOwnerParams.bIsPushBased = true;

	// Skipping the owner with this as the owner will use the controllers location directly
	DOREPLIFETIME_WITH_PARAMS_FAST(UOpenXRHandPoseComponent, LeftHandRep, SkipOwnerParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UOpenXRHandPoseComponent, RightHandRep, SkipOwnerParams);
}

void UOpenXRHandPoseComponent::Server_SendSkeletalTransforms_Implementation(const FBPXRSkeletalRepContainer& SkeletalInfo)
{
	for (int i = 0; i < HandSkeletalActions.Num(); i++)
	{
		if (HandSkeletalActions[i].TargetHand == SkeletalInfo.TargetHand)
		{
			if (SkeletalInfo.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left)
			{
				if (bSmoothReplicatedSkeletalData)
				{
					LeftHandRepManager.PreCopyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}

				FBPXRSkeletalRepContainer::CopyReplicatedTo(SkeletalInfo, HandSkeletalActions[i]);
				LeftHandRep = SkeletalInfo;
#if WITH_PUSH_MODEL
				MARK_PROPERTY_DIRTY_FROM_NAME(UOpenXRHandPoseComponent, LeftHandRep, this);
#endif

				if (bSmoothReplicatedSkeletalData)
				{
					LeftHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}
			}
			else
			{
				if (bSmoothReplicatedSkeletalData)
				{
					RightHandRepManager.PreCopyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}

				FBPXRSkeletalRepContainer::CopyReplicatedTo(SkeletalInfo, HandSkeletalActions[i]);
				RightHandRep = SkeletalInfo;
#if WITH_PUSH_MODEL
				MARK_PROPERTY_DIRTY_FROM_NAME(UOpenXRHandPoseComponent, RightHandRep, this);
#endif

				if (bSmoothReplicatedSkeletalData)
				{
					RightHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}
			}

			break;
		}
	}
}

bool UOpenXRHandPoseComponent::Server_SendSkeletalTransforms_Validate(const FBPXRSkeletalRepContainer& SkeletalInfo)
{
	return true;
}

void FOpenXRAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	Super::PreUpdate(InAnimInstance, DeltaSeconds);

	if (UOpenXRAnimInstance* OwningInstance = Cast<UOpenXRAnimInstance>(InAnimInstance))
	{
		if (OwningInstance->OwningPoseComp)
		{
			if (HandSkeletalActionData.Num() != OwningInstance->OwningPoseComp->HandSkeletalActions.Num())
			{
				HandSkeletalActionData.Empty(OwningInstance->OwningPoseComp->HandSkeletalActions.Num());

				for(FBPOpenXRActionSkeletalData& actionInfo : OwningInstance->OwningPoseComp->HandSkeletalActions)
				{
					HandSkeletalActionData.Add(actionInfo);
				}
			}
			else
			{
				for (int i = 0; i < OwningInstance->OwningPoseComp->HandSkeletalActions.Num(); ++i)
				{
					HandSkeletalActionData[i] = OwningInstance->OwningPoseComp->HandSkeletalActions[i];
				}
			}
		}
	}
}

FOpenXRAnimInstanceProxy::FOpenXRAnimInstanceProxy(UAnimInstance* InAnimInstance)
	: FAnimInstanceProxy(InAnimInstance)
{
}

void UOpenXRHandPoseComponent::BeginPlay()
{
	/*if (UMotionControllerComponent * MotionParent = Cast<UMotionControllerComponent>(GetAttachParent()))
	{
		EControllerHand HandType;
		if (!FXRMotionControllerBase::GetHandEnumForSourceName(MotionParent->MotionSource, HandType))
		{
			HandType = EControllerHand::Left;
		}

		for (int i = 0; i < HandSkeletalActions.Num(); i++)
		{
			if (HandType == EControllerHand::Left || HandType == EControllerHand::AnyHand)
				HandSkeletalActions[i].SkeletalData.TargetHand = EVRSkeletalHandIndex::EActionHandIndex_Left;
			else
				HandSkeletalActions[i].SkeletalData.TargetHand = EVRSkeletalHandIndex::EActionHandIndex_Right;
		}

	}*/

	Super::BeginPlay();
}

void UOpenXRHandPoseComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if (!IsLocallyControlled())
	{
		if (bReplicateSkeletalData)
		{
			// Handle bone lerping here if we are replicating
			for (FBPOpenXRActionSkeletalData& actionInfo : HandSkeletalActions)
			{
				if (bSmoothReplicatedSkeletalData)
				{
					if (actionInfo.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left)
					{
						LeftHandRepManager.UpdateManager(DeltaTime, actionInfo, this);
					}
					else
					{
						RightHandRepManager.UpdateManager(DeltaTime, actionInfo, this);
					}
				}
			}			
		}
	}
	else // Get data and process
	{
		bool bGetCompressedTransforms = false;
		if (bReplicateSkeletalData && HandSkeletalActions.Num() > 0)
		{
			SkeletalNetUpdateCount += DeltaTime;
			if (SkeletalNetUpdateCount >= (1.0f / ReplicationRateForSkeletalAnimations))
			{
				SkeletalNetUpdateCount = 0.0f;
				bGetCompressedTransforms = true;
			}
		}

		for (FBPOpenXRActionSkeletalData& actionInfo : HandSkeletalActions)
		{
			if (UOpenXRExpansionFunctionLibrary::GetOpenXRHandPose(actionInfo, this, bGetMockUpPoseForDebugging))
			{
				if (bGetCompressedTransforms)
				{
					if (GetNetMode() == NM_Client)
					{
						if (actionInfo.bHasValidData)
						{
							FBPXRSkeletalRepContainer ContainerSend;
							ContainerSend.CopyForReplication(actionInfo);
							Server_SendSkeletalTransforms(ContainerSend);
						}
					}
					else
					{
						if (actionInfo.bHasValidData)
						{
							if (actionInfo.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left)
								LeftHandRep.CopyForReplication(actionInfo);
							else
								RightHandRep.CopyForReplication(actionInfo);
						}
					}
				}
			}

			if (bDetectGestures && actionInfo.bHasValidData && actionInfo.SkeletalTransforms.Num() > 0 && GesturesDB != nullptr && GesturesDB->Gestures.Num() > 0)
			{
				DetectCurrentPose(actionInfo);
			}
		}
	}
	
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

bool UOpenXRHandPoseComponent::SaveCurrentPose(FName RecordingName, EVRSkeletalHandIndex HandToSave)
{

	if (!HandSkeletalActions.Num())
		return false;

	// Default to the first hand element so that single length arrays work as is.
	FBPOpenXRActionSkeletalData* HandSkeletalAction = nullptr;

	// Now check for the specific passed in hand if this is a multi hand
	for (int i = 0; i < HandSkeletalActions.Num(); ++i)
	{
		if (HandSkeletalActions[i].TargetHand == HandToSave)
		{
			HandSkeletalAction = &HandSkeletalActions[i];
			break;
		}
	}

	if (!HandSkeletalAction || !HandSkeletalAction->bHasValidData || HandSkeletalAction->SkeletalTransforms.Num() < EHandKeypointCount)
		return false;

	if (GesturesDB)
	{
		FOpenXRGesture NewGesture;

		int32 FingerMap[5] =
		{
			(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_TIP_EXT,
			(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT,
			(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_TIP_EXT,
			(int32)EXRHandJointType::OXR_HAND_JOINT_RING_TIP_EXT,
			(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_TIP_EXT
		};

		FVector WristLoc = FVector::ZeroVector;

		if (HandToSave == EVRSkeletalHandIndex::EActionHandIndex_Left)
		{
			WristLoc = HandSkeletalAction->SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation().MirrorByVector(FVector::RightVector);
		}
		else
		{
			WristLoc = HandSkeletalAction->SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation();
		}

		for (int i = 0; i < 5; ++i)
		{
			if (HandToSave == EVRSkeletalHandIndex::EActionHandIndex_Left)
			{				
				NewGesture.FingerValues[i] = FOpenXRGestureFingerPosition(HandSkeletalAction->SkeletalTransforms[FingerMap[i]].GetLocation().MirrorByVector(FVector::RightVector) - WristLoc, (EXRHandJointType)FingerMap[i]);
			}
			else
			{
				NewGesture.FingerValues[i] = FOpenXRGestureFingerPosition(HandSkeletalAction->SkeletalTransforms[FingerMap[i]].GetLocation() - WristLoc, (EXRHandJointType)FingerMap[i]);
			}
		}

		NewGesture.Name = RecordingName;
		GesturesDB->Gestures.Add(NewGesture);

		return true;
	}

	return false;
}


bool UOpenXRHandPoseComponent::K2_DetectCurrentPose(UPARAM(ref) FBPOpenXRActionSkeletalData& SkeletalAction, FOpenXRGesture & GestureOut)
{
	if (!GesturesDB || GesturesDB->Gestures.Num() < 1)
		return false;

	int32 FingerMap[5] =
	{
		(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_RING_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_TIP_EXT
	};

	FVector WristLoc = FVector::ZeroVector;

	if (SkeletalAction.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left)
	{
		WristLoc = SkeletalAction.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation().MirrorByVector(FVector::RightVector);
	}
	else
	{
		WristLoc = SkeletalAction.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation();
	}

	// Early fill in an array to keep from performing math for each gesture
	TArray<FVector> CurrentTips;
	CurrentTips.AddUninitialized(5);
	for (int i = 0; i < 5; ++i)
	{
		if (SkeletalAction.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left)
		{
			CurrentTips[i] = SkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation().MirrorByVector(FVector::RightVector) - WristLoc;
		}
		else
		{
			CurrentTips[i] = SkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation() - WristLoc;
		}
	}

	for (const FOpenXRGesture& Gesture : GesturesDB->Gestures)
	{
		// If not enough indexs to match curl values, or if this gesture requires finger splay and the controller can't do it
		if (Gesture.FingerValues.Num() < 5 || SkeletalAction.SkeletalTransforms.Num() < EHandKeypointCount)
			continue;

		bool bDetectedPose = true;
		for (int i = 0; i < 5; ++i)
		{
			FVector GestureV = Gesture.FingerValues[i].Value;
			FVector CurrentV = CurrentTips[i];
			FVector Difference = GestureV - CurrentV;

			if (!Gesture.FingerValues[i].Value.Equals(CurrentTips[i], Gesture.FingerValues[i].Threshold))
			{
				bDetectedPose = false;
				break;
			}
		}

		if (bDetectedPose)
		{
			GestureOut = Gesture;
			return true;
		}
	}

	return false;
}

bool UOpenXRHandPoseComponent::DetectCurrentPose(FBPOpenXRActionSkeletalData &SkeletalAction)
{
	if (!GesturesDB || GesturesDB->Gestures.Num() < 1 || SkeletalAction.SkeletalTransforms.Num() < EHandKeypointCount)
		return false;

	FTransform BoneTransform = FTransform::Identity;

	int32 FingerMap[5] =
	{
		(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_RING_TIP_EXT,
		(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_TIP_EXT
	};

	FVector WristLoc = FVector::ZeroVector;

	if (SkeletalAction.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left)
	{
		WristLoc = SkeletalAction.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation().MirrorByVector(FVector::RightVector);
	}
	else
	{
		WristLoc = SkeletalAction.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation();
	}

	// Early fill in an array to keep from performing math for each gesture
	TArray<FVector> CurrentTips;
	CurrentTips.AddUninitialized(5);
	for (int i = 0; i < 5; ++i)
	{
		if (SkeletalAction.TargetHand == EVRSkeletalHandIndex::EActionHandIndex_Left)
		{
			CurrentTips[i] = SkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation().MirrorByVector(FVector::RightVector) - WristLoc;
		}
		else
		{
			CurrentTips[i] = SkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation() - WristLoc;
		}
	}

	for (auto GestureIterator = GesturesDB->Gestures.CreateConstIterator(); GestureIterator; ++GestureIterator)
	{
		const FOpenXRGesture &Gesture = *GestureIterator;

		// If not enough indexs to match curl values, or if this gesture requires finger splay and the controller can't do it
		if (Gesture.FingerValues.Num() < 5 || SkeletalAction.SkeletalTransforms.Num() < EHandKeypointCount)
			continue;

		bool bDetectedPose = true;
		for (int i = 0; i < 5; ++i)
		{
			if (Gesture.FingerValues[i].Threshold <= 0.0f)
				continue;

			if (!Gesture.FingerValues[i].Value.Equals(CurrentTips[i], Gesture.FingerValues[i].Threshold))
			{
				bDetectedPose = false;
				break;
			}
		}

		if (bDetectedPose)
		{
			if (SkeletalAction.LastHandGesture != Gesture.Name)
			{
				if (SkeletalAction.LastHandGesture != NAME_None)
					OnGestureEnded.Broadcast(SkeletalAction.LastHandGesture, SkeletalAction.LastHandGestureIndex, SkeletalAction.TargetHand);

				SkeletalAction.LastHandGesture = Gesture.Name;
				SkeletalAction.LastHandGestureIndex = GestureIterator.GetIndex();
				OnNewGestureDetected.Broadcast(SkeletalAction.LastHandGesture, SkeletalAction.LastHandGestureIndex, SkeletalAction.TargetHand);

				return true;
			}
			else
				return false; // Same gesture
		}
	}

	if (SkeletalAction.LastHandGesture != NAME_None)
	{
		OnGestureEnded.Broadcast(SkeletalAction.LastHandGesture, SkeletalAction.LastHandGestureIndex, SkeletalAction.TargetHand);
		SkeletalAction.LastHandGesture = NAME_None;
		SkeletalAction.LastHandGestureIndex = INDEX_NONE;
	}

	return false;
}

UOpenXRHandPoseComponent::FTransformLerpManager::FTransformLerpManager()
{
	bReplicatedOnce = false;
	bLerping = false;
	UpdateCount = 0.0f;
	UpdateRate = 0.0f;
}

void UOpenXRHandPoseComponent::FTransformLerpManager::PreCopyNewData(FBPOpenXRActionSkeletalData& ActionInfo, int NetUpdateRate, bool bExponentialSmoothing)
{
	if (!bExponentialSmoothing)
	{
		if (ActionInfo.SkeletalTransforms.Num())
		{
			OldTransforms = ActionInfo.SkeletalTransforms;
		}
	}
}

void UOpenXRHandPoseComponent::FTransformLerpManager::NotifyNewData(FBPOpenXRActionSkeletalData& ActionInfo, int NetUpdateRate, bool bExponentialSmoothing)
{
	UpdateRate = (1.0f / NetUpdateRate);

	if (bReplicatedOnce)
	{
		bLerping = true;
		UpdateCount = 0.0f;
		NewTransforms = ActionInfo.SkeletalTransforms;

		ActionInfo.SkeletalTransforms = OldTransforms;

	}
	else
	{
		if (bExponentialSmoothing)
		{
			OldTransforms = ActionInfo.SkeletalTransforms;
		}

		bReplicatedOnce = true;
	}
}

void UOpenXRHandPoseComponent::FTransformLerpManager::UpdateManager(float DeltaTime, FBPOpenXRActionSkeletalData& ActionInfo, UOpenXRHandPoseComponent* ParentComp)
{
	if (!ActionInfo.bHasValidData || !OldTransforms.Num())
		return;

	if (bLerping)
	{	
		bool bExponentialSmoothing = ParentComp->bUseExponentialSmoothing;
		float LerpVal = 0.0f;

		if (!bExponentialSmoothing || ParentComp->InterpolationSpeed <= 0.f)
		{
			UpdateCount += DeltaTime;

			// Keep LerpVal
			LerpVal = FMath::Clamp(UpdateCount / UpdateRate, 0.0f, 1.0f);
		}
		else
		{
			LerpVal = FMath::Clamp(DeltaTime * ParentComp->InterpolationSpeed, 0.f, 1.f);
		}

		if (!bExponentialSmoothing && LerpVal >= 1.0f)
		{
			bLerping = false;
			UpdateCount = 0.0f;
			ActionInfo.SkeletalTransforms = NewTransforms;
		}
		else
		{
			int32 BoneCountAdjustment = 6 + (ActionInfo.bEnableUE4HandRepSavings ? 4 : 0);
			if ((NewTransforms.Num() < (EHandKeypointCount - BoneCountAdjustment)) || (NewTransforms.Num() != ActionInfo.SkeletalTransforms.Num()))
			{
				return;
			}

			ActionInfo.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_PALM_EXT] = FTransform::Identity;
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT, ActionInfo, LerpVal, bExponentialSmoothing);

			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			//BlendBone((uint8)EVROpenXRBones::eBone_Thumb3, ActionInfo, LerpVal); // Technically can be projected instead of blended

			if (!ActionInfo.bEnableUE4HandRepSavings)
			{
				BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			}
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			//BlendBone((uint8)EVROpenXRBones::eBone_IndexFinger4, ActionInfo, LerpVal); // Technically can be projected instead of blended

			if (!ActionInfo.bEnableUE4HandRepSavings)
			{
				BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			}
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			//BlendBone((uint8)EVROpenXRBones::eBone_IndexFinger4, ActionInfo, LerpVal); // Technically can be projected instead of blended

			if (!ActionInfo.bEnableUE4HandRepSavings)
			{
				BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			}
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			//BlendBone((uint8)EVROpenXRBones::eBone_IndexFinger4, ActionInfo, LerpVal); // Technically can be projected instead of blended

			if (!ActionInfo.bEnableUE4HandRepSavings)
			{
				BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			}
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT, ActionInfo, LerpVal, bExponentialSmoothing);
			//BlendBone((uint8)EVROpenXRBones::eBone_IndexFinger4, ActionInfo, LerpVal); // Technically can be projected instead of blended

			// These are copied from the 3rd joints as they use the same transform but a different root
			// Don't want to waste cpu time blending these
			//ActionInfo.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_Aux_Thumb] = ActionInfo.SkeletalData.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_Thumb2];
			//ActionInfo.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_Aux_IndexFinger] = ActionInfo.SkeletalData.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_IndexFinger3];
			//ActionInfo.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_Aux_MiddleFinger] = ActionInfo.SkeletalData.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_MiddleFinger3];
			//ActionInfo.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_Aux_RingFinger] = ActionInfo.SkeletalData.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_RingFinger3];
			//ActionInfo.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_Aux_PinkyFinger] = ActionInfo.SkeletalData.SkeletalTransforms[(uint8)EVROpenXRBones::eBone_PinkyFinger3];
		}
	}
}

void FBPXRSkeletalRepContainer::CopyForReplication(FBPOpenXRActionSkeletalData& Other)
{
	TargetHand = Other.TargetHand;

	if (!Other.bHasValidData)
		return;

	bAllowDeformingMesh = Other.bAllowDeformingMesh;
	bEnableUE4HandRepSavings = Other.bEnableUE4HandRepSavings;

	// Instead of doing this, we likely need to lerp but this is for testing
	//SkeletalTransforms = Other.SkeletalData.SkeletalTransforms;

	if (Other.SkeletalTransforms.Num() < EHandKeypointCount)
	{
		SkeletalTransforms.Empty();
		return;
	}

	int32 BoneCountAdjustment = 6 + (bEnableUE4HandRepSavings ? 4 : 0);

	if (SkeletalTransforms.Num() != EHandKeypointCount - BoneCountAdjustment)
	{
		SkeletalTransforms.Reset(EHandKeypointCount - BoneCountAdjustment); // Minus bones we don't need
		SkeletalTransforms.AddUninitialized(EHandKeypointCount - BoneCountAdjustment);
	}

	int32 idx = 0;
	// Root is always identity
	//SkeletalTransforms[0] = Other.SkeletalData.SkeletalTransforms[(uint8)EVROpenInputBones::eBone_Root]; // This has no pos right? Need to skip pos on it
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT];

	if (!bEnableUE4HandRepSavings)
	{
		SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT];
	}
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT];

	if (!bEnableUE4HandRepSavings)
	{
		SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT];
	}
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT];

	if (!bEnableUE4HandRepSavings)
	{
		SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT];
	}
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT];

	if (!bEnableUE4HandRepSavings)
	{
		SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT];
	}
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT];
	SkeletalTransforms[idx++] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT];
}

void FBPXRSkeletalRepContainer::CopyReplicatedTo(const FBPXRSkeletalRepContainer& Container, FBPOpenXRActionSkeletalData& Other)
{
	int32 BoneCountAdjustment = 6 + (Container.bEnableUE4HandRepSavings ? 4 : 0);
	if (Container.SkeletalTransforms.Num() < (EHandKeypointCount - BoneCountAdjustment))
	{
		Other.SkeletalTransforms.Empty();
		Other.bHasValidData = false;
		return;
	}

	Other.bAllowDeformingMesh = Container.bAllowDeformingMesh;
	Other.bEnableUE4HandRepSavings = Container.bEnableUE4HandRepSavings;

	// Instead of doing this, we likely need to lerp but this is for testing
	//Other.SkeletalData.SkeletalTransforms = Container.SkeletalTransforms;

	if (Other.SkeletalTransforms.Num() != EHandKeypointCount)
	{ 
		Other.SkeletalTransforms.Reset(EHandKeypointCount);
		Other.SkeletalTransforms.AddUninitialized(EHandKeypointCount);
	}

	int32 idx = 0;

	// Only fill in the ones that we care about
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_PALM_EXT] = FTransform::Identity; // Always identity
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT] = Container.SkeletalTransforms[idx++];

	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_TIP_EXT] = FTransform::Identity;

	if (!Container.bEnableUE4HandRepSavings)
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT] = Container.SkeletalTransforms[idx++];
	}
	else
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT] = FTransform::Identity;
	}
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT] = FTransform::Identity;

	if (!Container.bEnableUE4HandRepSavings)
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT] = Container.SkeletalTransforms[idx++];
	}
	else
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT] = FTransform::Identity;
	}
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_TIP_EXT] = FTransform::Identity;

	if (!Container.bEnableUE4HandRepSavings)
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT] = Container.SkeletalTransforms[idx++];
	}
	else
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT] = FTransform::Identity;
	}
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_TIP_EXT] = FTransform::Identity;

	if (!Container.bEnableUE4HandRepSavings)
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT] = Container.SkeletalTransforms[idx++];
	}
	else
	{
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT] = FTransform::Identity;
	}
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT] = Container.SkeletalTransforms[idx++];
	Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_TIP_EXT] = FTransform::Identity;

	Other.bHasValidData = true;
}

bool FBPXRSkeletalRepContainer::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	bOutSuccess = true;

	Ar.SerializeBits(&TargetHand, 1);
	Ar.SerializeBits(&bAllowDeformingMesh, 1);
	Ar.SerializeBits(&bEnableUE4HandRepSavings, 1);

	int32 BoneCountAdjustment = 6 + (bEnableUE4HandRepSavings ? 4 : 0);
	uint8 TransformCount = EHandKeypointCount - BoneCountAdjustment;

	bool bHasValidData = SkeletalTransforms.Num() >= TransformCount;
	Ar.SerializeBits(&bHasValidData, 1);

	//Ar << TransformCount;

	if (Ar.IsLoading())
	{
		SkeletalTransforms.Reset(TransformCount);
	}

	FVector Position = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;

	if (bHasValidData)
	{
		for (int i = 0; i < TransformCount; i++)
		{
			if (Ar.IsSaving())
			{
				if (bAllowDeformingMesh)
					Position = SkeletalTransforms[i].GetLocation();

				Rot = SkeletalTransforms[i].Rotator();
			}

			if (bAllowDeformingMesh)
				bOutSuccess &= SerializePackedVector<10, 11>(Position, Ar);

			Rot.SerializeCompressed(Ar); // Short? 10 bit?

			if (Ar.IsLoading())
			{
				if (bAllowDeformingMesh)
					SkeletalTransforms.Add(FTransform(Rot, Position));
				else
					SkeletalTransforms.Add(FTransform(Rot));
			}
		}
	}

	return bOutSuccess;
}

void UOpenXRAnimInstance::NativeBeginPlay()
{
	Super::NativeBeginPlay();

	AActor* Owner = GetOwningComponent()->GetOwner();
	UActorComponent* HandPoseComp = nullptr;

	if (Owner)
	{
		HandPoseComp = Owner->GetComponentByClass(UOpenXRHandPoseComponent::StaticClass());

		if (!HandPoseComp)
		{
			// We are also checking owner->owner in case hand mesh is in a sub actor
			if (Owner->GetOwner())
			{
				HandPoseComp = Owner->GetOwner()->GetComponentByClass(UOpenXRHandPoseComponent::StaticClass());
			}
		}
	}

	if (!HandPoseComp)
	{
		return;
	}

	if (UOpenXRHandPoseComponent* HandComp = Cast<UOpenXRHandPoseComponent>(HandPoseComp))
	{
		OwningPoseComp = HandComp;
	}
}

/*void UOpenXRAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	AActor* Owner = GetOwningComponent()->GetOwner();
	UActorComponent* HandPoseComp = nullptr;

	if (Owner)
	{
		HandPoseComp = Owner->GetComponentByClass(UOpenXRHandPoseComponent::StaticClass());

		if (!HandPoseComp)
		{
			// We are also checking owner->owner in case hand mesh is in a sub actor
			if (Owner->GetOwner())
			{
				HandPoseComp = Owner->GetOwner()->GetComponentByClass(UOpenXRHandPoseComponent::StaticClass());
			}
		}
	}

	if (!HandPoseComp)
	{
		return;
	}

	if (UOpenXRHandPoseComponent* HandComp = Cast<UOpenXRHandPoseComponent>(HandPoseComp))
	{
		OwningPoseComp = HandComp;
	}
}*/

void UOpenXRAnimInstance::InitializeCustomBoneMapping(UPARAM(ref) FBPOpenXRSkeletalMappingData& SkeletalMappingData)
{
	USkeleton* AssetSkeleton = this->CurrentSkeleton;//RequiredBones.GetSkeletonAsset();

	if (AssetSkeleton)
	{
		FBoneContainer& RequiredBones = this->GetRequiredBones();
		for (FBPOpenXRSkeletalPair& BonePair : SkeletalMappingData.BonePairs)
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

		if (UObject* OwningAsset = RequiredBones.GetAsset())
		{
			SkeletalMappingData.LastInitializedName = OwningAsset->GetFName();
		}

		SkeletalMappingData.bInitialized = true;
		return;
	}

	SkeletalMappingData.bInitialized = false;
}