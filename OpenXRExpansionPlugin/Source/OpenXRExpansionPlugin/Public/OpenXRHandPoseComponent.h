// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/Texture.h"
#include "Engine/EngineTypes.h"
#include "HeadMountedDisplayTypes.h"
//#include "Runtime/Launch/Resources/Version.h"

#include "Animation/AnimInstanceProxy.h"
#include "OpenXRExpansionTypes.h"
#include "Engine/DataAsset.h"

#include "OpenXRHandPoseComponent.generated.h"

USTRUCT(BlueprintType, Category = "VRExpansionFunctions|OpenXR|HandSkeleton")
struct OPENXREXPANSIONPLUGIN_API FBPSkeletalRepContainer
{
	GENERATED_BODY()
public:

	UPROPERTY(Transient, NotReplicated)
		EVRActionHand TargetHand;

	UPROPERTY(Transient, NotReplicated)
		bool bAllowDeformingMesh;

	UPROPERTY(Transient, NotReplicated)
		TArray<FTransform> SkeletalTransforms;

	UPROPERTY(Transient, NotReplicated)
		uint8 BoneCount;


	FBPSkeletalRepContainer()
	{
		TargetHand = EVRActionHand::EActionHand_Left;
		bAllowDeformingMesh = false;
		BoneCount = 0;
	}

	bool bHasValidData()
	{
		return SkeletalTransforms.Num() > 0;
	}

	void CopyForReplication(FBPOpenXRActionSkeletalData& Other)
	{
		TargetHand = Other.TargetHand;

		if (!Other.bHasValidData)
			return;

		bAllowDeformingMesh = Other.bAllowDeformingMesh;

		// Instead of doing this, we likely need to lerp but this is for testing
		//SkeletalTransforms = Other.SkeletalData.SkeletalTransforms;

		if (Other.SkeletalTransforms.Num() < EHandKeypointCount)
		{
			SkeletalTransforms.Empty();
			return;
		}

		if (SkeletalTransforms.Num() != EHandKeypointCount - 5)
		{
			SkeletalTransforms.Reset(EHandKeypointCount - 5); // Minus bones we don't need
			SkeletalTransforms.AddUninitialized(EHandKeypointCount - 5);
		}

		// Root is always identity
		//SkeletalTransforms[0] = Other.SkeletalData.SkeletalTransforms[(uint8)EVROpenInputBones::eBone_Root]; // This has no pos right? Need to skip pos on it
		SkeletalTransforms[0] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT];
		SkeletalTransforms[1] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT];
		SkeletalTransforms[2] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT];
		SkeletalTransforms[3] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT];

		SkeletalTransforms[4] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT];
		SkeletalTransforms[5] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT];
		SkeletalTransforms[6] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT];
		SkeletalTransforms[7] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT];

		SkeletalTransforms[8] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT];
		SkeletalTransforms[9] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT];
		SkeletalTransforms[10] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT];
		SkeletalTransforms[11] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT];

		SkeletalTransforms[12] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT];
		SkeletalTransforms[13] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT];
		SkeletalTransforms[14] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT];
		SkeletalTransforms[15] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT];

		SkeletalTransforms[16] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT];
		SkeletalTransforms[17] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT];
		SkeletalTransforms[18] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT];
		SkeletalTransforms[19] = Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT];
	}

	static void CopyReplicatedTo(const FBPSkeletalRepContainer& Container, FBPOpenXRActionSkeletalData& Other)
	{
		if (Container.SkeletalTransforms.Num() < (EHandKeypointCount - 5))
		{
			Other.SkeletalTransforms.Empty();
			Other.bHasValidData = false;
			return;
		}

		Other.bAllowDeformingMesh = Container.bAllowDeformingMesh;

		// Instead of doing this, we likely need to lerp but this is for testing
		//Other.SkeletalData.SkeletalTransforms = Container.SkeletalTransforms;

		if (Other.SkeletalTransforms.Num() != EHandKeypointCount)
			Other.SkeletalTransforms.Reset(EHandKeypointCount);
		{
			Other.SkeletalTransforms.AddUninitialized(EHandKeypointCount);
		}

		// Only fill in the ones that we care about
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_PALM_EXT] = FTransform::Identity; // Always identity
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT] = Container.SkeletalTransforms[0];

		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT] = Container.SkeletalTransforms[1];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT] = Container.SkeletalTransforms[2];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT] = Container.SkeletalTransforms[3];

		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT] = Container.SkeletalTransforms[4];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT] = Container.SkeletalTransforms[5];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT] = Container.SkeletalTransforms[6];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT] = Container.SkeletalTransforms[7];

		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT] = Container.SkeletalTransforms[8];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT] = Container.SkeletalTransforms[9];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT] = Container.SkeletalTransforms[10];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT] = Container.SkeletalTransforms[11];

		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT] = Container.SkeletalTransforms[12];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT] = Container.SkeletalTransforms[13];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT] = Container.SkeletalTransforms[14];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT] = Container.SkeletalTransforms[15];

		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT] = Container.SkeletalTransforms[16];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT] = Container.SkeletalTransforms[17];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT] = Container.SkeletalTransforms[18];
		Other.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT] = Container.SkeletalTransforms[19];

		Other.bHasValidData = true;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		Ar.SerializeBits(&TargetHand, 1);
		Ar.SerializeBits(&bAllowDeformingMesh, 1);

		uint8 TransformCount = SkeletalTransforms.Num();

		Ar << TransformCount;

		if (Ar.IsLoading())
		{
			SkeletalTransforms.Reset(TransformCount);
		}

		FVector Position = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;

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

		return bOutSuccess;
	}
};

template<>
struct TStructOpsTypeTraits< FBPSkeletalRepContainer > : public TStructOpsTypeTraitsBase2<FBPSkeletalRepContainer>
{
	enum
	{
		WithNetSerializer = true
	};
};

USTRUCT(BlueprintType, Category = "VRGestures")
struct OPENXREXPANSIONPLUGIN_API FOpenXRGestureFingerPosition
{
	GENERATED_BODY()
public:

	// The Finger index, not editable
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = "VRGesture")
		EXRHandJointType IndexType;

	// The locational value of this element 0.f - 1.f
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture")
	FVector Value;

	// The threshold within which this finger value will be detected as matching (1.0 would be always matching, IE: finger doesn't count)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Threshold;

	FOpenXRGestureFingerPosition(FVector TipLoc, EXRHandJointType Type)
	{
		IndexType = Type;
		Value = TipLoc;
		Threshold = 0.1f;
	}
	FOpenXRGestureFingerPosition()
	{
		IndexType = EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT;
		Value = FVector(0.f);
		Threshold = 0.1f;
	}
};

USTRUCT(BlueprintType, Category = "VRGestures")
struct OPENXREXPANSIONPLUGIN_API FOpenXRGesture
{
	GENERATED_BODY()
public:

	// Name of the recorded gesture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture")
		FName Name;

	// Samples in the recorded gesture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture")
		TArray<FOpenXRGestureFingerPosition> FingerValues;

	FOpenXRGesture()
	{
		InitPoseValues();
		Name = NAME_None;
	}

	void InitPoseValues()
	{
		FingerValues.Add(FOpenXRGestureFingerPosition(FVector::ZeroVector, EXRHandJointType::OXR_HAND_JOINT_THUMB_TIP_EXT));
		FingerValues.Add(FOpenXRGestureFingerPosition(FVector::ZeroVector, EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT));
		FingerValues.Add(FOpenXRGestureFingerPosition(FVector::ZeroVector, EXRHandJointType::OXR_HAND_JOINT_MIDDLE_TIP_EXT));
		FingerValues.Add(FOpenXRGestureFingerPosition(FVector::ZeroVector, EXRHandJointType::OXR_HAND_JOINT_RING_TIP_EXT));
		FingerValues.Add(FOpenXRGestureFingerPosition(FVector::ZeroVector, EXRHandJointType::OXR_HAND_JOINT_LITTLE_TIP_EXT));
	}
};

/**
* Items Database DataAsset, here we can save all of our game items
*/
UCLASS(BlueprintType, Category = "VRGestures")
class OPENXREXPANSIONPLUGIN_API UOpenXRGestureDatabase : public UDataAsset
{
	GENERATED_BODY()
public:

	// Gestures in this database
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		TArray <FOpenXRGesture> Gestures;

	UOpenXRGestureDatabase()
	{
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOpenXRGestureDetected, const FName &, GestureDetected, int32, GestureIndex, EVRActionHand, ActionHandType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOpenXRGestureEnded, const FName &, GestureEnded, int32, GestureIndex, EVRActionHand, ActionHandType);

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class OPENXREXPANSIONPLUGIN_API UOpenXRHandPoseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOpenXRHandPoseComponent(const FObjectInitializer& ObjectInitializer);

	// Says whether we should run gesture detection
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		bool bDetectGestures;

	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void SetDetectGestures(bool bNewDetectGestures)
	{
		bDetectGestures = bNewDetectGestures;
	}

	UPROPERTY(BlueprintAssignable, Category = "VRGestures")
		FOpenXRGestureDetected OnNewGestureDetected;

	UPROPERTY(BlueprintAssignable, Category = "VRGestures")
		FOpenXRGestureEnded OnGestureEnded;

	// Known sequences
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGestures")
		UOpenXRGestureDatabase *GesturesDB;
	 
	UFUNCTION(BlueprintCallable, Category = "VRGestures")
		void SaveCurrentPose(FName RecordingName, EVRActionHand HandToSave = EVRActionHand::EActionHand_Right);

	UFUNCTION(BlueprintCallable, Category = "VRGestures", meta = (DisplayName = "DetectCurrentPose"))
		bool K2_DetectCurrentPose(FBPOpenXRActionSkeletalData& SkeletalAction, FOpenXRGesture & GestureOut);

	// This version throws events
	bool DetectCurrentPose(FBPOpenXRActionSkeletalData& SkeletalAction);

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	inline bool IsLocallyControlled() const
	{
#if ENGINE_MAJOR_VERSION >= 4 && ENGINE_MINOR_VERSION >= 22
		const AActor* MyOwner = GetOwner();
		return MyOwner->HasLocalNetOwner();
#else
		// I like epics new authority check more than mine
		const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner && MyOwner->Role == ENetRole::ROLE_Authority);
#endif
	}

	// Using tick and not timers because skeletal components tick anyway, kind of a waste to make another tick by adding a timer over that
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;


	//virtual void OnUnregister() override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkeletalData|Actions")
		bool bGetMockUpPoseForDebugging;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SkeletalData|Actions")
		TArray<FBPOpenXRActionSkeletalData> HandSkeletalActions;

	UPROPERTY(Replicated, Transient, ReplicatedUsing = OnRep_SkeletalTransformLeft)
		FBPSkeletalRepContainer LeftHandRep;

	UPROPERTY(Replicated, Transient, ReplicatedUsing = OnRep_SkeletalTransformRight)
		FBPSkeletalRepContainer RightHandRep;

	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendSkeletalTransforms(const FBPSkeletalRepContainer& SkeletalInfo);

	bool bLerpingPositionLeft;
	bool bReppedOnceLeft;

	bool bLerpingPositionRight;
	bool bReppedOnceRight;

	struct FTransformLerpManager
	{
		bool bReplicatedOnce;
		bool bLerping;
		float UpdateCount;
		float UpdateRate;
		TArray<FTransform> NewTransforms;

		FTransformLerpManager()
		{
			bReplicatedOnce = false;
			bLerping = false;
			UpdateCount = 0.0f;
			UpdateRate = 0.0f;
		}

		void NotifyNewData(FBPOpenXRActionSkeletalData& ActionInfo, int NetUpdateRate)
		{
			UpdateRate = (1.0f / NetUpdateRate);
			if (bReplicatedOnce)
			{
				bLerping = true;
				UpdateCount = 0.0f;
				NewTransforms = ActionInfo.SkeletalTransforms;
			}
			else
			{
				bReplicatedOnce = true;
			}
		}

		FORCEINLINE void BlendBone(uint8 BoneToBlend, FBPOpenXRActionSkeletalData& ActionInfo, float & LerpVal)
		{
			ActionInfo.SkeletalTransforms[BoneToBlend].Blend(ActionInfo.OldSkeletalTransforms[BoneToBlend], NewTransforms[BoneToBlend], LerpVal);
		}

		void UpdateManager(float DeltaTime, FBPOpenXRActionSkeletalData& ActionInfo)
		{
			if (!ActionInfo.bHasValidData)
				return;

			if (bLerping)
			{
				UpdateCount += DeltaTime;
				float LerpVal = FMath::Clamp(UpdateCount / UpdateRate, 0.0f, 1.0f);

				if (LerpVal >= 1.0f)
				{
					bLerping = false;
					UpdateCount = 0.0f;
					ActionInfo.SkeletalTransforms = NewTransforms;
				}
				else
				{
					if ((NewTransforms.Num() < (EHandKeypointCount - 5)) || (NewTransforms.Num() != ActionInfo.SkeletalTransforms.Num() || NewTransforms.Num() != ActionInfo.OldSkeletalTransforms.Num()))
					{
						return;
					}

					ActionInfo.SkeletalTransforms[(int32)EXRHandJointType::OXR_HAND_JOINT_PALM_EXT] = FTransform::Identity;
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_WRIST_EXT, ActionInfo, LerpVal);

					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_METACARPAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_PROXIMAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_THUMB_DISTAL_EXT, ActionInfo, LerpVal);
					//BlendBone((uint8)EVROpenXRBones::eBone_Thumb3, ActionInfo, LerpVal); // Technically can be projected instead of blended

					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_METACARPAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_PROXIMAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_INDEX_DISTAL_EXT, ActionInfo, LerpVal);
					//BlendBone((uint8)EVROpenXRBones::eBone_IndexFinger4, ActionInfo, LerpVal); // Technically can be projected instead of blended

					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_METACARPAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_PROXIMAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_MIDDLE_DISTAL_EXT, ActionInfo, LerpVal);
					//BlendBone((uint8)EVROpenXRBones::eBone_IndexFinger4, ActionInfo, LerpVal); // Technically can be projected instead of blended

					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_METACARPAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_PROXIMAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_INTERMEDIATE_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_RING_DISTAL_EXT, ActionInfo, LerpVal);
					//BlendBone((uint8)EVROpenXRBones::eBone_IndexFinger4, ActionInfo, LerpVal); // Technically can be projected instead of blended

					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_METACARPAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_PROXIMAL_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, ActionInfo, LerpVal);
					BlendBone((int32)EXRHandJointType::OXR_HAND_JOINT_LITTLE_DISTAL_EXT, ActionInfo, LerpVal);
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

	}; 
	
	FTransformLerpManager LeftHandRepManager;
	FTransformLerpManager RightHandRepManager;

	UFUNCTION()
	virtual void OnRep_SkeletalTransformLeft()
	{
		for (int i = 0; i < HandSkeletalActions.Num(); i++)
		{
			if (HandSkeletalActions[i].TargetHand == LeftHandRep.TargetHand)
			{
				HandSkeletalActions[i].OldSkeletalTransforms = HandSkeletalActions[i].SkeletalTransforms;

				FBPSkeletalRepContainer::CopyReplicatedTo(LeftHandRep, HandSkeletalActions[i]);
				
				if(bSmoothReplicatedSkeletalData)
					LeftHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations);
				break;
			}
		}
	}

	UFUNCTION()
	virtual void OnRep_SkeletalTransformRight()
	{
		for (int i = 0; i < HandSkeletalActions.Num(); i++)
		{
			if (HandSkeletalActions[i].TargetHand == RightHandRep.TargetHand)
			{
				HandSkeletalActions[i].OldSkeletalTransforms = HandSkeletalActions[i].SkeletalTransforms;

				FBPSkeletalRepContainer::CopyReplicatedTo(RightHandRep, HandSkeletalActions[i]);
				
				if (bSmoothReplicatedSkeletalData)
					RightHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations);
				break;
			}
		}
	}

	// If we should replicate the skeletal transform data
	UPROPERTY(EditAnywhere, Category = SkeletalData)
		bool bReplicateSkeletalData;

	// If true we will lerp between updates of the skeletal mesh transforms and smooth the result
	UPROPERTY(EditAnywhere, Category = SkeletalData)
		bool bSmoothReplicatedSkeletalData;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SkeletalData)
		float ReplicationRateForSkeletalAnimations;

	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case, also used for remotes to lerp position
	float SkeletalNetUpdateCount;
	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case, also used for remotes to lerp position
	float SkeletalUpdateCount;
};	

USTRUCT()
struct OPENXREXPANSIONPLUGIN_API FOpenXRAnimInstanceProxy : public FAnimInstanceProxy
{
public:
	GENERATED_BODY()

		FOpenXRAnimInstanceProxy() {}
		FOpenXRAnimInstanceProxy(UAnimInstance* InAnimInstance);

		/** Called before update so we can copy any data we need */
		virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;

public:

	EVRActionHand TargetHand;
	TArray<FBPOpenXRActionSkeletalData> HandSkeletalActionData;

};

UCLASS(transient, Blueprintable, hideCategories = AnimInstance, BlueprintType)
class OPENXREXPANSIONPLUGIN_API UOpenXRAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(transient)
		UOpenXRHandPoseComponent * OwningPoseComp;

	FOpenXRAnimInstanceProxy AnimInstanceProxy;

	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override
	{
		return new FOpenXRAnimInstanceProxy(this);
		//return &AnimInstanceProxy;
	}

	virtual void NativeInitializeAnimation() override
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

		if(!HandPoseComp)
		{
			return;
		}

		if (UOpenXRHandPoseComponent* HandComp = Cast<UOpenXRHandPoseComponent>(HandPoseComp))
		{
			OwningPoseComp = HandComp;
		}
	}

	UFUNCTION(BlueprintCallable, Category = "BoneMappings")
	void InitializeCustomBoneMapping(UPARAM(ref) FBPSkeletalMappingData & SkeletalMappingData)
	{
		USkeleton* AssetSkeleton = this->CurrentSkeleton;//RequiredBones.GetSkeletonAsset();

		if (AssetSkeleton)
		{
			FBoneContainer &RequiredBones = this->GetRequiredBones();
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

			if (UObject * OwningAsset = RequiredBones.GetAsset())
			{
				SkeletalMappingData.LastInitializedName = OwningAsset->GetFName();
			}

			SkeletalMappingData.bInitialized = true;
			return;
		}

		SkeletalMappingData.bInitialized = false;
	}


};
