// Fill out your copyright notice in the Description page of Project Settings.
#include "OpenXRHandPoseComponent.h"
#include "Net/UnrealNetwork.h"
#include "MotionControllerComponent.h"
#include "OpenXRExpansionFunctionLibrary.h"

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

	// Skipping the owner with this as the owner will use the controllers location directly
	DOREPLIFETIME_CONDITION(UOpenXRHandPoseComponent, LeftHandRep, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(UOpenXRHandPoseComponent, RightHandRep, COND_SkipOwner);
}

void UOpenXRHandPoseComponent::Server_SendSkeletalTransforms_Implementation(const FBPSkeletalRepContainer& SkeletalInfo)
{
	for (int i = 0; i < HandSkeletalActions.Num(); i++)
	{
		if (HandSkeletalActions[i].TargetHand == SkeletalInfo.TargetHand)
		{
			HandSkeletalActions[i].OldSkeletalTransforms = HandSkeletalActions[i].SkeletalTransforms;

			FBPSkeletalRepContainer::CopyReplicatedTo(SkeletalInfo, HandSkeletalActions[i]);

			if (SkeletalInfo.TargetHand == EVRActionHand::EActionHand_Left)
			{
				LeftHandRep = SkeletalInfo;
				if (bSmoothReplicatedSkeletalData)
					LeftHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations);
			}
			else
			{
				RightHandRep = SkeletalInfo;
				if (bSmoothReplicatedSkeletalData)
					RightHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations);
			}

			break;
		}
	}
}

bool UOpenXRHandPoseComponent::Server_SendSkeletalTransforms_Validate(const FBPSkeletalRepContainer& SkeletalInfo)
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
				HandSkeletalActions[i].SkeletalData.TargetHand = EVRActionHand::EActionHand_Left;
			else
				HandSkeletalActions[i].SkeletalData.TargetHand = EVRActionHand::EActionHand_Right;
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
					if (actionInfo.TargetHand == EVRActionHand::EActionHand_Left)
					{
						LeftHandRepManager.UpdateManager(DeltaTime, actionInfo);
					}
					else
					{
						RightHandRepManager.UpdateManager(DeltaTime, actionInfo);
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
			if (UOpenXRExpansionFunctionLibrary::GetOpenXRHandPose(actionInfo, this))
			{
				if (bGetCompressedTransforms)
				{
					if (GetNetMode() == NM_Client)
					{
						if (actionInfo.bHasValidData)
						{
							FBPSkeletalRepContainer ContainerSend;
							ContainerSend.CopyForReplication(actionInfo);
							Server_SendSkeletalTransforms(ContainerSend);
						}
					}
					else
					{
						if (actionInfo.bHasValidData)
						{
							if (actionInfo.TargetHand == EVRActionHand::EActionHand_Left)
								LeftHandRep.CopyForReplication(actionInfo);
							else
								RightHandRep.CopyForReplication(actionInfo);
						}
					}
				}
			}

			if (bDetectGestures && actionInfo.bHasValidData && GesturesDB != nullptr && GesturesDB->Gestures.Num() > 0)
			{
				DetectCurrentPose(actionInfo);
			}
		}
	}
	
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UOpenXRHandPoseComponent::SaveCurrentPose(FName RecordingName, EVRActionHand HandToSave)
{

	if (!HandSkeletalActions.Num())
		return;

	// Default to the first hand element so that single length arrays work as is.
	FBPOpenXRActionSkeletalData& HandSkeletalAction = HandSkeletalActions[0];

	// Now check for the specific passed in hand if this is a multi hand
	for (int i = 0; i < HandSkeletalActions.Num(); ++i)
	{
		if (HandSkeletalActions[i].TargetHand == HandToSave)
		{
			HandSkeletalAction = HandSkeletalActions[i];
			break;
		}
	}

	if (!HandSkeletalAction.bHasValidData || HandSkeletalAction.SkeletalTransforms.Num() < EHandKeypointCount)
		return;

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

		if (HandToSave == EVRActionHand::EActionHand_Left)
		{
			WristLoc = HandSkeletalAction.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation().MirrorByVector(FVector::RightVector);
		}
		else
		{
			WristLoc = HandSkeletalAction.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT].GetLocation();
		}

		for (int i = 0; i < 5; ++i)
		{
			if (HandToSave == EVRActionHand::EActionHand_Left)
			{				
				NewGesture.FingerValues[i] = FOpenXRGestureFingerPosition(HandSkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation().MirrorByVector(FVector::RightVector) - WristLoc, (EXRHandJointType)FingerMap[i]);
			}
			else
			{
				NewGesture.FingerValues[i] = FOpenXRGestureFingerPosition(HandSkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation() - WristLoc, (EXRHandJointType)FingerMap[i]);
			}
		}

		NewGesture.Name = RecordingName;
		GesturesDB->Gestures.Add(NewGesture);
	}
	
}


bool UOpenXRHandPoseComponent::K2_DetectCurrentPose(FBPOpenXRActionSkeletalData& SkeletalAction, FOpenXRGesture & GestureOut)
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

	for (const FOpenXRGesture& Gesture : GesturesDB->Gestures)
	{
		// If not enough indexs to match curl values, or if this gesture requires finger splay and the controller can't do it
		if (Gesture.FingerValues.Num() < 5 || SkeletalAction.SkeletalTransforms.Num() < EHandKeypointCount)
			continue;

		bool bDetectedPose = true;
		for (int i = 0; i < 5; ++i)
		{
			if (!Gesture.FingerValues[i].Value.Equals(SkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation(), Gesture.FingerValues[i].Threshold))
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
	if (!GesturesDB || GesturesDB->Gestures.Num() < 1)
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

	for (auto GestureIterator = GesturesDB->Gestures.CreateConstIterator(); GestureIterator; ++GestureIterator)
	{
		const FOpenXRGesture &Gesture = *GestureIterator;

		// If not enough indexs to match curl values, or if this gesture requires finger splay and the controller can't do it
		if (Gesture.FingerValues.Num() < 5 || SkeletalAction.SkeletalTransforms.Num() < EHandKeypointCount)
			continue;

		bool bDetectedPose = true;
		for (int i = 0; i < 5; ++i)
		{
			if (!Gesture.FingerValues[i].Value.Equals(SkeletalAction.SkeletalTransforms[FingerMap[i]].GetLocation(), Gesture.FingerValues[i].Threshold))
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
