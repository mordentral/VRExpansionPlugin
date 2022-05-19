// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "VRBPDatatypes.h"
#include "VRGripInterface.h"
#include "VRExpansionFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/PoseSnapshot.h"
#include "HandSocketComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVRHandSocketComponent, Log, All);


UENUM()
namespace EVRAxis
{
	enum Type
	{
		X,
		Y,
		Z
	};
}

/**
* A base class for custom hand socket objects
* Not directly blueprint spawnable as you are supposed to subclass this to add on top your own custom data
*/

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPVRHandPoseBonePair
{
	GENERATED_BODY()
public:

	// Distance to offset to get center of waist from tracked parent location
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		FName BoneName;

	// Initial "Resting" location of the tracker parent, assumed to be the calibration zero
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		FQuat DeltaPose;

	FBoneReference ReferenceToConstruct;

	FBPVRHandPoseBonePair()
	{
		BoneName = NAME_None;
		DeltaPose = FQuat::Identity;
	}

	FORCEINLINE bool operator==(const FName& Other) const
	{
		return (BoneName == Other);
	}
};

UCLASS(Blueprintable, ClassGroup = (VRExpansionPlugin), hideCategories = ("Component Tick", Events, Physics, Lod, "Asset User Data", Collision))
class VREXPANSIONPLUGIN_API UHandSocketComponent : public USceneComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:

	UHandSocketComponent(const FObjectInitializer& ObjectInitializer);
	~UHandSocketComponent();

	//static get socket compoonent

	//Axis to mirror on for this socket
	UPROPERTY(EditDefaultsOnly, Category = "Hand Socket Data|Mirroring|Advanced")
		TEnumAsByte<EVRAxis::Type> MirrorAxis;

	// Axis to flip on when mirroring this socket
	UPROPERTY(VisibleDefaultsOnly, Category = "Hand Socket Data|Mirroring|Advanced")
		TEnumAsByte<EVRAxis::Type> FlipAxis;

	// Relative placement of the hand to this socket
	UPROPERTY(EditAnywhere, BlueprintReadWrite, /*DuplicateTransient,*/ Category = "Hand Socket Data")
		FTransform HandRelativePlacement;

	// Target Slot Prefix
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		FName SlotPrefix;

	// If true the hand meshes relative transform will be de-coupled from the hand socket
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hand Socket Data")
		bool bDecoupleMeshPlacement;

	// If true we should only be used to snap mesh to us, not for the actual socket transform
	// Will act like free gripping but the mesh will snap into position
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		bool bOnlySnapMesh;

	// If true then this socket is left hand dominant and will flip for the right hand instead
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hand Socket Data")
		bool bLeftHandDominant;

	// If true we will mirror ourselves automatically for the off hand
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Mirroring", meta = (DisplayName = "Flip For Off Hand"))
		bool bFlipForLeftHand;

	// If true, when we mirror the hand socket it will only mirror rotation, not position
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Mirroring", meta = (editcondition = "bFlipForLeftHand"))
		bool bOnlyFlipRotation;

	// If true then this hand socket will always be considered "in range" and checked against others for lowest distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		bool bAlwaysInRange;

	// If true and there are multiple hand socket components in range with this setting
	// Then the default behavior will compare closest rotation on them all to pick one
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		bool bMatchRotation;

	// If true then the hand socket will not be considered for search operations
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		bool bDisabled;

	// Snap distance to use if you want to override the defaults.
	// Will be ignored if == 0.0f or bAlwaysInRange is true
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		float OverrideDistance;

	// If true we are expected to have a list of custom deltas for bones to overlay onto our base pose
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation")
		bool bUseCustomPoseDeltas;

	// Custom rotations that are added on top of an animations bone rotation to make a final transform
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation")
		TArray<FBPVRHandPoseBonePair> CustomPoseDeltas;

	// Primary hand animation, for both hands if they share animations, right hand if they don't
	// If using a custom pose delta this is expected to be the base pose
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation")
		UAnimSequence* HandTargetAnimation;

	// Scale to apply when mirroring the hand, adjust to visualize your off hand correctly
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
		FVector MirroredScale;

	FTransform GetBoneTransformAtTime(UAnimSequence* MyAnimSequence, /*float AnimTime,*/ int BoneIdx, bool bUseRawDataOnly);

	// Returns the base target animation of the hand (if there is one)
	UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
		UAnimSequence* GetTargetAnimation();

	/** 
	* Returns the target animation of the hand blended with the delta rotations if there are any
	* @param PoseSnapShot - Snapshot generated by this function
	* @param TargetMesh - Targetmesh to check the skeleton of
	* @param bSkipRootBone - If true we will skip the root bone (IE: Hand_r) and only apply the children poses (Full body)
	* @param bFlipHand - If true we will mirror the pose, this is primarily to apply to a left hand from a right
	*/
	UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
		bool GetBlendedPoseSnapShot(FPoseSnapshot& PoseSnapShot, USkeletalMeshComponent* TargetMesh = nullptr, bool bSkipRootBone = false, bool bFlipHand = false);

	/**
	* Converts an animation sequence into a pose snapshot
	* @param InAnimationSequence - Sequence to convert to a pose snapshot
	* @param OutPoseSnapShot - Snapshot returned by this function
	* @param TargetMesh - Targetmesh to check the skeleton of
	* @param bSkipRootBone - If true we will skip the root bone (IE: Hand_r) and only apply the children poses (Full body)
	* @param bFlipHand - If true we will mirror the pose, this is primarily to apply to a left hand from a right
	*/
	UFUNCTION(BlueprintCallable, Category = "Hand Socket Data", meta = (bIgnoreSelf = "true"))
		static bool GetAnimationSequenceAsPoseSnapShot(UAnimSequence * InAnimationSequence, FPoseSnapshot& OutPoseSnapShot, USkeletalMeshComponent* TargetMesh = nullptr, bool bSkipRootBone = false, bool bFlipHand = false);

	// Returns the target relative transform of the hand
	//UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
	FTransform GetHandRelativePlacement();

	inline void MirrorHandTransform(FTransform& ReturnTrans, FTransform& relTrans)
	{
		if (bOnlyFlipRotation)
		{
			ReturnTrans.SetTranslation(ReturnTrans.GetTranslation() - relTrans.GetTranslation());
			ReturnTrans.Mirror(GetAsEAxis(MirrorAxis), GetCrossAxis());
			ReturnTrans.SetTranslation(ReturnTrans.GetTranslation() + relTrans.GetTranslation());
		}
		else
		{
			ReturnTrans.Mirror(GetAsEAxis(MirrorAxis), GetCrossAxis());
		}
	}

	inline TEnumAsByte<EAxis::Type> GetAsEAxis(TEnumAsByte<EVRAxis::Type> InAxis)
	{
		switch (InAxis)
		{
		case EVRAxis::X:
		{
			return EAxis::X;
		}break;
		case EVRAxis::Y:
		{
			return EAxis::Y;
		}break;
		case EVRAxis::Z:
		{
			return EAxis::Z;
		}break;
		}

		return EAxis::X;
	}


	inline FVector GetMirrorVector()
	{
		switch (MirrorAxis)
		{
		case EVRAxis::Y:
		{
			return FVector::RightVector;
		}break;
		case EVRAxis::Z:
		{
			return FVector::UpVector;
		}break;
		case EVRAxis::X:
		default:
		{
			return FVector::ForwardVector;
		}break;
		}
	}

	inline FVector GetFlipVector()
	{
		switch (FlipAxis)
		{
		case EVRAxis::Y:
		{
			return FVector::RightVector;
		}break;
		case EVRAxis::Z:
		{
			return FVector::UpVector;
		}break;
		case EVRAxis::X:
		default:
		{
			return FVector::ForwardVector;
		}break;
		}
	}

	inline TEnumAsByte<EAxis::Type> GetCrossAxis()
	{
		// Checking against the sign now to avoid possible mobile precision issues
		FVector SignVec = MirroredScale.GetSignVector();

		if (SignVec.X < 0)
		{
			return EAxis::X;
		}
		else if (SignVec.Z < 0)
		{
			return EAxis::Z;
		}
		else if (SignVec.Y < 0)
		{
			return EAxis::Y;
		}

		return GetAsEAxis(FlipAxis);

		/*if (FlipAxis == EVRAxis::Y)
		{
			return EAxis::Z;
		}
		else if (FlipAxis == EVRAxis::Z)
		{
			return EAxis::X;
		}
		else if (FlipAxis == EVRAxis::X)
		{
			return EAxis::Y;
		}*/

		//return EAxis::None;
	}
	// Returns the target relative transform of the hand to the gripped object
	// If you want the transform mirrored you need to pass in which hand is requesting the information
	// If UseParentScale is true then we will scale the value by the parent scale (generally only for when not using absolute hand scale)
	UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
	FTransform GetMeshRelativeTransform(bool bIsRightHand, bool bUseParentScale = false);

	// Returns the defined hand socket component (if it exists, you need to valid check the return!
	// If it is a valid return you can then cast to your projects base socket class and handle whatever logic you want
	UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
	static UHandSocketComponent *  GetHandSocketComponentFromObject(UObject * ObjectToCheck, FName SocketName)
	{
		if (AActor* OwningActor = Cast<AActor>(ObjectToCheck))
		{
			if (USceneComponent* OwningRoot = Cast<USceneComponent>(OwningActor->GetRootComponent()))
			{
				TArray<USceneComponent*> AttachChildren = OwningRoot->GetAttachChildren();
				for (USceneComponent* AttachChild : AttachChildren)
				{
					if (AttachChild && AttachChild->IsA<UHandSocketComponent>() && AttachChild->GetFName() == SocketName)
					{
						return Cast<UHandSocketComponent>(AttachChild);
					}
				}
			}
		}
		else if (USceneComponent* OwningRoot = Cast<USceneComponent>(ObjectToCheck))
		{
			TArray<USceneComponent*> AttachChildren = OwningRoot->GetAttachChildren();
			for (USceneComponent* AttachChild : AttachChildren)
			{
				if (AttachChild && AttachChild->IsA<UHandSocketComponent>() && AttachChild->GetFName() == SocketName)
				{
					return Cast<UHandSocketComponent>(AttachChild);
				}
			}
		}

		return nullptr;
	}

	virtual FTransform GetHandSocketTransform(UGripMotionControllerComponent* QueryController, bool bIgnoreOnlySnapMesh = false);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
#if WITH_EDITORONLY_DATA
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	void PoseVisualizationToAnimation(bool bForceRefresh = false);
	bool bTickedPose;
#endif
	virtual void OnRegister() override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;

	// ------------------------------------------------
	// Gameplay tag interface
	// ------------------------------------------------

	/** Overridden to return requirements tags */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
	{
		TagContainer = GameplayTags;
	}

	/** Tags that are set on this object */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "GameplayTags")
		FGameplayTagContainer GameplayTags;

	// End Gameplay Tag Interface

	// Requires bReplicates to be true for the component
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
		bool bRepGameplayTags;

	// Overrides the default of : true and allows for controlling it like in an actor, should be default of off normally with grippable components
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
		bool bReplicateMovement;

	/** mesh component to indicate hand placement */
#if WITH_EDITORONLY_DATA
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = "Hand Visualization")
		//class USkeletalMeshComponent* HandVisualizerComponent;
	class UPoseableMeshComponent* HandVisualizerComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Transient, Category = "Hand Visualization")
		class USkeletalMesh* VisualizationMesh;

	// If we should show the visualization mesh
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
		bool bShowVisualizationMesh;

	// Show the visualization mirrored
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
		bool bMirrorVisualizationMesh;

	// If we should show the grip range of this socket (shows text if always in range)
	// If override distance is zero then it attempts to infer the value from the parent architecture
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
		bool bShowRangeVisualization;

	void PositionVisualizationMesh();
	void HideVisualizationMesh();

#endif

#if WITH_EDITORONLY_DATA
	// Material to apply to the hand
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visualization")
		UMaterial* HandPreviewMaterial;

#endif
};

UCLASS(transient, Blueprintable, hideCategories = AnimInstance, BlueprintType)
class VREXPANSIONPLUGIN_API UHandSocketAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, transient, Category = "Socket Data")
		UHandSocketComponent* OwningSocket;

	virtual void NativeInitializeAnimation() override
	{
		Super::NativeInitializeAnimation();

		OwningSocket = Cast<UHandSocketComponent>(GetOwningComponent()->GetAttachParent());
	}
};