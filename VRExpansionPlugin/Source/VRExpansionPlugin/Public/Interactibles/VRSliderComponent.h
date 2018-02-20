// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "MotionControllerComponent.h"
#include "VRGripInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "Components/SplineComponent.h"
#include "VRInteractibleFunctionLibrary.h"

#include "VRSliderComponent.generated.h"

UENUM(Blueprintable)
enum class EVRInteractibleSliderLerpType : uint8
{
	Lerp_None,
	Lerp_Interp,
	Lerp_InterpConstantTo
};

/** Delegate for notification when the lever state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRSliderHitEndSignature, float, SliderProgress);

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRSliderComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_UCLASS_BODY()

	~UVRSliderComponent();

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRLeverComponent")
		FVRSliderHitEndSignature OnSliderHitEnd;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Lever State Changed"))
		void ReceiveSliderHitEnd(float SliderProgress);

	float LastSliderProgressState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	FVector MaxSlideDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	FVector MinSlideDistance;

	// Gets filled in with the current slider location progress
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRSliderComponent")
	float CurrentSliderProgress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	bool bSlideDistanceIsInParentSpace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	int GripPriority;

	// Set this to assign a spline component to the slider
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	USplineComponent * SplineComponentToFollow; 

	// Where the slider should follow the rotation and scale of the spline as well
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	bool bFollowSplineRotationAndScale;

	// Does not allow the slider to skip past nodes on the spline, it requires it to progress from node to node
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bEnforceSplineLinearity;
	float LastInputKey;
	float LerpedKey;

	// Type of lerp to use when following a spline
	// For lerping I would suggest using ConstantTo in general as it will be the smoothest.
	// Normal Interp will change speed based on distance, that may also have its uses.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		EVRInteractibleSliderLerpType SplineLerpType;

	// Lerp Value for the spline, when in InterpMode it is the speed of interpolation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (ClampMin = "0", UIMin = "0"))
		float SplineLerpValue;


	FTransform InitialRelativeTransform;
	FVector InitialInteractorLocation;
	FVector InitialGripLoc;
	FVector InitialDropLocation;

	// Should be called after the slider is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
	void ResetInitialSliderLocation()
	{
		if (SplineComponentToFollow != nullptr)
		{
			// Snap to start of spline
			FTransform WorldTransform = SplineComponentToFollow->GetTransformAtSplinePoint(0, ESplineCoordinateSpace::World);

			if (bFollowSplineRotationAndScale)
			{
				this->SetWorldLocationAndRotation(WorldTransform.GetLocation(), WorldTransform.GetRotation());
			}
			else
			{
				this->SetWorldLocation(WorldTransform.GetLocation());
			}

			GetCurrentSliderProgress(WorldTransform.GetLocation());
		}

		// Get our initial relative transform to our parent (or not if un-parented).
		InitialRelativeTransform = this->GetRelativeTransform();

		if(SplineComponentToFollow == nullptr)
			CurrentSliderProgress = GetCurrentSliderProgress(FVector(0, 0, 0));
	}

	// Sets the spline component to follow, if empty, just reinitializes the transform and removes the follow component
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
	void SetSplineComponentToFollow(USplineComponent * SplineToFollow)
	{
		SplineComponentToFollow = SplineToFollow;
		ResetInitialSliderLocation();
	}

	void GetLerpedKey(float &ClosestKey, float DeltaTime)
	{
		switch (SplineLerpType)
		{
		case EVRInteractibleSliderLerpType::Lerp_Interp:
		{
			ClosestKey = FMath::FInterpTo(LastInputKey, ClosestKey, DeltaTime, SplineLerpValue);
		}break;
		case EVRInteractibleSliderLerpType::Lerp_InterpConstantTo:
		{
			ClosestKey = FMath::FInterpConstantTo(LastInputKey, ClosestKey, DeltaTime, SplineLerpValue);
		}break;

		default: break;
		}
	}

	float GetCurrentSliderProgress(FVector CurLocation)
	{
		if (SplineComponentToFollow != nullptr)
		{
			// In this case it is a world location
			float ClosestKey = SplineComponentToFollow->FindInputKeyClosestToWorldLocation(CurLocation);
			int32 primaryKey = FMath::TruncToInt(ClosestKey);

			float distance1 = SplineComponentToFollow->GetDistanceAlongSplineAtSplinePoint(primaryKey);
			float distance2 = SplineComponentToFollow->GetDistanceAlongSplineAtSplinePoint(primaryKey + 1);

			float FinalDistance = ((distance2 - distance1) * (ClosestKey - (float)primaryKey)) + distance1;

			return FMath::Clamp(FinalDistance / SplineComponentToFollow->GetSplineLength(), 0.0f, 1.0f);
		}

		// Should need the clamp normally, but if someone is manually setting locations it could go out of bounds
		return FMath::Clamp(FVector::Dist(-MinSlideDistance, CurLocation) / FVector::Dist(-MinSlideDistance, MaxSlideDistance), 0.0f, 1.0f);
	}

	FVector ClampSlideVector(FVector ValueToClamp)
	{
		if (bSlideDistanceIsInParentSpace)
		{
			// Scale distance by initial slider scale
			FVector fScaleFactor = FVector(1.0f) / InitialRelativeTransform.GetScale3D();

			return FVector(
				FMath::Clamp(ValueToClamp.X, -MinSlideDistance.X * fScaleFactor.X, MaxSlideDistance.X * fScaleFactor.X),
				FMath::Clamp(ValueToClamp.Y, -MinSlideDistance.Y * fScaleFactor.Y, MaxSlideDistance.Y * fScaleFactor.Y),
				FMath::Clamp(ValueToClamp.Z, -MinSlideDistance.Z * fScaleFactor.Z, MaxSlideDistance.Z * fScaleFactor.Z)
			);
		}
		else
		{
			return FVector(
				FMath::Clamp(ValueToClamp.X, -MinSlideDistance.X, MaxSlideDistance.X),
				FMath::Clamp(ValueToClamp.Y, -MinSlideDistance.Y, MaxSlideDistance.Y),
				FMath::Clamp(ValueToClamp.Z, -MinSlideDistance.Z, MaxSlideDistance.Z)
			);
		}
	}

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

	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	

	// Requires bReplicates to be true for the component
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		bool bRepGameplayTags;
		
	// Overrides the default of : true and allows for controlling it like in an actor, should be default of off normally with grippable components
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
		bool bReplicateMovement;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripMovementReplicationSettings MovementReplicationSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float BreakDistance;

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
		bool bDenyGripping;

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		UGripMotionControllerComponent * HoldingController; // Set on grip notify, not net serializing

	// Grip interface setup

	// Set up as deny instead of allow so that default allows for gripping
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool DenyGripping();

	// How an interfaced object behaves when teleporting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripInterfaceTeleportBehavior TeleportBehavior();

	// Should this object simulate on drop
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool SimulateOnDrop();

	/*// Grip type to use when gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType SlotGripType();

	// Grip type to use when not gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType FreeGripType();
		*/

		// Grip type to use
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType GetPrimaryGripType(bool bIsSlot);

	// Secondary grip type
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		ESecondaryGripType SecondaryGripType();

	// Define which movement repliation setting to use
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripMovementReplicationSettings GripMovementReplicationType();

	// Define the late update setting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripLateUpdateSettings GripLateUpdateSetting();

	/*// What grip stiffness to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripStiffness();

	// What grip damping to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripDamping();
		*/
		// What grip stiffness and damping to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void GetGripStiffnessAndDamping(float &GripStiffnessOut, float &GripDampingOut);

	// Get the advanced physics settings for this grip
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		FBPAdvGripSettings AdvancedGripSettings();

	// What distance to break a grip at (only relevent with physics enabled grips
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripBreakDistance();

	/*// Get closest secondary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestSecondarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);

	// Get closest primary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestPrimarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);
		*/

		// Get closest primary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestGripSlotInRange(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);


	// Check if the object is an interactable
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool IsInteractible();

	// Returns if the object is held and if so, which pawn is holding it
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void IsHeld(UGripMotionControllerComponent *& CurHoldingController, bool & bCurIsHeld);

	// Sets is held, used by the plugin
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void SetHeld(UGripMotionControllerComponent * NewHoldingController, bool bNewIsHeld);

	// Get interactable settings
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		FBPInteractionSettings GetInteractionSettings();


	// Events //

	// Event triggered each tick on the interfaced object when gripped, can be used for custom movement or grip based logic
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void TickGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, float DeltaTime);

	// Event triggered on the interfaced object when gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnSecondaryGrip(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnSecondaryGripRelease(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Interaction Functions

	// Call to use an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnUsed();

	// Call to stop using an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnEndUsed();

	// Call to use an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnSecondaryUsed();

	// Call to stop using an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnEndSecondaryUsed();

	// Call to send an action event to the object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnInput(FKey Key, EInputEvent KeyEvent);

	protected:

};