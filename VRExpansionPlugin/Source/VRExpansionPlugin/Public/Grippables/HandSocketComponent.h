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
#include "Animation/AnimSequence.h"
#include "HandSocketComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVRHandSocketComponent, Log, All);

/**
* A base class for custom hand socket objects
* Not directly blueprint spawnable as you are supposed to subclass this to add on top your own custom data
*/

UCLASS(Blueprintable, /*meta = (BlueprintSpawnableComponent, ChildCanTick),*/ ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UHandSocketComponent : public USceneComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:

	UHandSocketComponent(const FObjectInitializer& ObjectInitializer);
	~UHandSocketComponent();

	//static get socket compoonent

	UPROPERTY(/*Category = "Hand Socket Data"*/)
		TEnumAsByte<EAxis::Type> MirrorAxis;
	UPROPERTY(/*Category = "Hand Socket Data"*/)
		TEnumAsByte<EAxis::Type> FlipAxis;

	// Relative placement of the hand to this socket
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		FTransform HandRelativePlacement;

	// Target Slot Prefix
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		FName SlotPrefix;

	// If true we should only be used to snap mesh to us, not for the actual socket transform
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		bool bOnlySnapMesh;

	// If true we will mirror ourselves automatically for the left hand
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		bool bFlipForLeftHand;

	// Snap distance to use if you want to override the defaults.
	// Will be ignored if == 0.0f
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		float OverrideDistance;

	// Primary hand animation, for both hands if they share animations, right hand if they don't
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		UAnimSequence* HandTargetAnimation;

	// If we have a seperate left hand animation then set it here
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
		UAnimSequence* HandTargetAnimationLeft;

	// Returns the target animation of the hand
	UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
		UAnimSequence* GetTargetAnimation(bool bIsRightHand);

	// Returns the target relative transform of the hand
	UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
		FTransform GetHandRelativePlacement(bool bIsRightHand);

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

	virtual FTransform GetHandSocketTransform(UGripMotionControllerComponent* QueryController);
	virtual FTransform GetMeshRelativeTransform(UGripMotionControllerComponent* QueryController);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
#if WITH_EDITORONLY_DATA
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

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
		class USkeletalMeshComponent* HandVisualizerComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
		class USkeletalMesh* VisualizationMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
		bool bShowVisualizationMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visualization")
		UMaterial* HandPreviewMaterial;
#endif
};