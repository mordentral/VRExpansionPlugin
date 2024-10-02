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
struct OPENXREXPANSIONPLUGIN_API FBPXRSkeletalRepContainer
{
	GENERATED_BODY()
public:

	UPROPERTY(Transient, NotReplicated)
		EVRSkeletalHandIndex TargetHand;

	UPROPERTY(Transient, NotReplicated)
		bool bAllowDeformingMesh;

	// If true we will skip sending the 4 metacarpal bones that ue4 doesn't need, (STEAMVR skeletons need this disabled!)
	// Only really used for the older UE4 skeleton
	UPROPERTY(Transient, NotReplicated)
		bool bEnableUE4HandRepSavings;

	UPROPERTY(Transient, NotReplicated)
		TArray<FTransform> SkeletalTransforms;

	UPROPERTY(Transient, NotReplicated)
		uint8 BoneCount;


	FBPXRSkeletalRepContainer()
	{
		TargetHand = EVRSkeletalHandIndex::EActionHandIndex_Left;
		bAllowDeformingMesh = false;
		bEnableUE4HandRepSavings = false;
		BoneCount = 0;
	}

	bool bHasValidData()
	{
		return SkeletalTransforms.Num() > 0;
	}

	void CopyForReplication(FBPOpenXRActionSkeletalData& Other);
	static void CopyReplicatedTo(const FBPXRSkeletalRepContainer& Container, FBPOpenXRActionSkeletalData& Other);

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits< FBPXRSkeletalRepContainer > : public TStructOpsTypeTraitsBase2<FBPXRSkeletalRepContainer>
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
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRGesture", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0"))
		float Threshold;

	FOpenXRGestureFingerPosition(FVector TipLoc, EXRHandJointType Type)
	{
		IndexType = Type;
		Value = TipLoc;
		Threshold = 5.0f;
	}
	FOpenXRGestureFingerPosition()
	{
		IndexType = EXRHandJointType::OXR_HAND_JOINT_INDEX_TIP_EXT;
		Value = FVector(0.f);
		Threshold = 5.0f;
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOpenXRGestureDetected, const FName &, GestureDetected, int32, GestureIndex, EVRSkeletalHandIndex, ActionHandType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOpenXRGestureEnded, const FName &, GestureEnded, int32, GestureIndex, EVRSkeletalHandIndex, ActionHandType);

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
		bool SaveCurrentPose(FName RecordingName, EVRSkeletalHandIndex HandToSave = EVRSkeletalHandIndex::EActionHandIndex_Right);

	UFUNCTION(BlueprintCallable, Category = "VRGestures", meta = (DisplayName = "DetectCurrentPose"))
		bool K2_DetectCurrentPose(UPARAM(ref) FBPOpenXRActionSkeletalData& SkeletalAction, FOpenXRGesture & GestureOut);

	// This version throws events
	bool DetectCurrentPose(FBPOpenXRActionSkeletalData& SkeletalAction);

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	inline bool IsLocallyControlled() const
	{
//#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 22)
		const AActor* MyOwner = GetOwner();
		return MyOwner->HasLocalNetOwner();
//#else
		// I like epics new authority check more than mine
	/*	const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);

		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner && MyOwner->GetLocalRole() == ENetRole::ROLE_Authority);*/
//#endif
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
		FBPXRSkeletalRepContainer LeftHandRep;

	UPROPERTY(Replicated, Transient, ReplicatedUsing = OnRep_SkeletalTransformRight)
		FBPXRSkeletalRepContainer RightHandRep;

	UFUNCTION(Unreliable, Server, WithValidation)
		void Server_SendSkeletalTransforms(const FBPXRSkeletalRepContainer& SkeletalInfo);

	bool bLerpingPositionLeft;
	bool bLerpingPositionRight;

	struct FTransformLerpManager
	{
		bool bReplicatedOnce;
		bool bLerping;
		float UpdateCount;
		float UpdateRate;
		TArray<FTransform> OldTransforms;
		TArray<FTransform> NewTransforms;

		FTransformLerpManager();
		void PreCopyNewData(FBPOpenXRActionSkeletalData& ActionInfo, int NetUpdateRate, bool bExponentialSmoothing);
		void NotifyNewData(FBPOpenXRActionSkeletalData& ActionInfo, int NetUpdateRate, bool bExponentialSmoothing);

		FORCEINLINE void BlendBone(uint8 BoneToBlend, FBPOpenXRActionSkeletalData& ActionInfo, float& LerpVal, bool bExponentialSmoothing)
		{
			ActionInfo.SkeletalTransforms[BoneToBlend].Blend(OldTransforms[BoneToBlend], NewTransforms[BoneToBlend], LerpVal);

			if (bExponentialSmoothing)
			{
				// Saving base back out for exponential
				OldTransforms[BoneToBlend] = ActionInfo.SkeletalTransforms[BoneToBlend];
			}
		}

		void UpdateManager(float DeltaTime, FBPOpenXRActionSkeletalData& ActionInfo, UOpenXRHandPoseComponent * ParentComp);

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
				if (bSmoothReplicatedSkeletalData)
				{
					LeftHandRepManager.PreCopyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}

				FBPXRSkeletalRepContainer::CopyReplicatedTo(LeftHandRep, HandSkeletalActions[i]);
				
				if (bSmoothReplicatedSkeletalData)
				{
					LeftHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}

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
				if (bSmoothReplicatedSkeletalData)
				{
					RightHandRepManager.PreCopyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}

				FBPXRSkeletalRepContainer::CopyReplicatedTo(RightHandRep, HandSkeletalActions[i]);
				
				if (bSmoothReplicatedSkeletalData)
				{
					RightHandRepManager.NotifyNewData(HandSkeletalActions[i], ReplicationRateForSkeletalAnimations, bUseExponentialSmoothing);
				}
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

	// If true then we will use exponential smoothing with buffered correction
	UPROPERTY(EditAnywhere, Category = "SkeletalData", meta = (editcondition = "bSmoothReplicatedSkeletalData"))
		bool bUseExponentialSmoothing = false;

	// Timestep of smoothing translation
	UPROPERTY(EditAnywhere, Category = "SkeletalData", meta = (editcondition = "bUseExponentialSmoothing"))
		float InterpolationSpeed = 25.0f;
	
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

	EVRSkeletalHandIndex TargetHand;
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

	virtual void NativeBeginPlay() override;

	//virtual void NativeInitializeAnimation() override;

	UFUNCTION(BlueprintCallable, Category = "BoneMappings")
	void InitializeCustomBoneMapping(UPARAM(ref) FBPOpenXRSkeletalMappingData & SkeletalMappingData);


};
