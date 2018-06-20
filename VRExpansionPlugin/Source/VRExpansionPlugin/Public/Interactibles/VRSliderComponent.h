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

UENUM(Blueprintable)
enum class EVRInteractibleSliderDropBehavior : uint8
{
	/** Stays in place on drop */
	Stay,

	/** Retains momentum on release*/
	RetainMomentum
};

/** Delegate for notification when the slider state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRSliderHitPointSignature, float, SliderProgressPoint);

/**
* A slider component, can act like a scroll bar, or gun bolt, or spline following component
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRSliderComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_UCLASS_BODY()

	~UVRSliderComponent();

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRLeverComponent")
		FVRSliderHitPointSignature OnSliderHitPoint;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Lever State Changed"))
		void ReceiveSliderHitPoint(float SliderProgressPoint);

	float LastSliderProgressState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	FVector MaxSlideDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	FVector MinSlideDistance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		EVRInteractibleSliderDropBehavior SliderBehaviorWhenReleased;

	// Number of frames to average momentum across for the release momentum (avoids quick waggles)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent|Momentum Settings", meta = (ClampMin = "0", ClampMax = "12", UIMin = "0", UIMax = "12"))
		int FramesToAverage;

	// Units in degrees per second to slow a momentum lerp down
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent|Momentum Settings", meta = (ClampMin = "0.0", ClampMax = "10.0", UIMin = "0.0", UIMax = "10.0"))
		float SliderMomentumFriction;

	// Maximum momentum of the slider in units of the total distance per second (0.0 - 1.0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent|Momentum Settings", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float MaxSliderMomentum;

	UPROPERTY(BlueprintReadOnly, Category = "VRSliderComponent")
		bool bIsLerping;

	// For momentum retention
	float MomentumAtDrop;
	float LastSliderProgress;

	// Gets filled in with the current slider location progress
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRSliderComponent")
	float CurrentSliderProgress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	bool bSlideDistanceIsInParentSpace;

	// How far away from an event state before the slider allows throwing the same state again, default of 1.0 means it takes a full toggle
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRSliderComponent", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float EventThrowThreshold;
	bool bHitEventThreshold;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
	int GripPriority;

	// Set this to assign a spline component to the slider
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Replicated/* ReplicatedUsing = OnRep_SplineComponentToFollow*/, Category = "VRSliderComponent")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent")
		bool bSliderUsesSnapPoints;

	// Portion of the slider that the slider snaps to on release and when within the threshold distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float SnapIncrement;

	// Threshold distance that when within the slider will stay snapped to its current snap increment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRSliderComponent", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
		float SnapThreshold;


	//FTransform InitialRelativeTransform;

	// Now replicating this so that it works correctly over the network
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InitialRelativeTransform, Category = "VRSliderComponent")
		FTransform_NetQuantize InitialRelativeTransform;

	UFUNCTION()
	virtual void OnRep_InitialRelativeTransform()
	{
		CalculateSliderProgress();
	}

	FVector InitialInteractorLocation;
	FVector InitialGripLoc;
	FVector InitialDropLocation;

	// Calculates the current slider progress
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
	float CalculateSliderProgress()
	{
		if (this->SplineComponentToFollow != nullptr)
		{
			CurrentSliderProgress = GetCurrentSliderProgress(this->GetComponentLocation());
		}
		else
		{
			FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
			FTransform CurrentRelativeTransform = InitialRelativeTransform * ParentTransform;
			FVector CalculatedLocation = CurrentRelativeTransform.InverseTransformPosition(this->GetComponentLocation());

			//if (bSlideDistanceIsInParentSpace)
				//CalculatedLocation *= FVector(1.0f) / InitialRelativeTransform.GetScale3D();
	
			CurrentSliderProgress = GetCurrentSliderProgress(CalculatedLocation);
		}

		return CurrentSliderProgress;
	}

	// Forcefully sets the slider progress to the defined value
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
		void SetSliderProgress(float NewSliderProgress)
	{
		NewSliderProgress = FMath::Clamp(NewSliderProgress, 0.0f, 1.0f);

		if (SplineComponentToFollow != nullptr)
		{
			FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);
			float splineProgress = SplineComponentToFollow->GetSplineLength() * NewSliderProgress;
			
			if (bFollowSplineRotationAndScale)
			{
				FTransform trans = SplineComponentToFollow->GetTransformAtDistanceAlongSpline(splineProgress, ESplineCoordinateSpace::World, true);
				trans.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
				trans = trans * ParentTransform.Inverse();
				this->SetRelativeTransform(trans);
			}
			else
			{
				this->SetRelativeLocation(ParentTransform.InverseTransformPosition(SplineComponentToFollow->GetLocationAtDistanceAlongSpline(splineProgress, ESplineCoordinateSpace::World)));
			}
		}
		else // Not a spline follow
		{
			// Doing it min+max because the clamp value subtracts the min value
			FVector CalculatedLocation = FMath::Lerp(-MinSlideDistance, MaxSlideDistance, NewSliderProgress);

			if (bSlideDistanceIsInParentSpace)
				CalculatedLocation *= FVector(1.0f) / InitialRelativeTransform.GetScale3D();

			FVector ClampedLocation = ClampSlideVector(CalculatedLocation);

			//if (bSlideDistanceIsInParentSpace)
			//	this->SetRelativeLocation(InitialRelativeTransform.TransformPositionNoScale(ClampedLocation));
			//else
			this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(ClampedLocation));
		}

		CurrentSliderProgress = NewSliderProgress;
	}

	// Should be called after the slider is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRSliderComponent")
	void ResetInitialSliderLocation()
	{
		// Get our initial relative transform to our parent (or not if un-parented).
		InitialRelativeTransform = this->GetRelativeTransform();
		FTransform ParentTransform = UVRInteractibleFunctionLibrary::Interactible_GetCurrentParentTransform(this);

		if (SplineComponentToFollow != nullptr)
		{
			FTransform WorldTransform = SplineComponentToFollow->FindTransformClosestToWorldLocation(this->GetComponentLocation(), ESplineCoordinateSpace::World, true);
			if (bFollowSplineRotationAndScale)
			{
				WorldTransform.MultiplyScale3D(InitialRelativeTransform.GetScale3D());
				WorldTransform = WorldTransform * ParentTransform.Inverse();
				this->SetRelativeTransform(WorldTransform);
			}
			else
			{
				this->SetWorldLocation(WorldTransform.GetLocation());
			}

			CurrentSliderProgress = GetCurrentSliderProgress(WorldTransform.GetLocation());
		}

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

	float GetCurrentSliderProgress(FVector CurLocation, bool bUseKeyInstead = false, float CurKey = 0.f)
	{
		if (SplineComponentToFollow != nullptr)
		{
			// In this case it is a world location
			float ClosestKey = CurKey;
			
			if (!bUseKeyInstead)
				ClosestKey = SplineComponentToFollow->FindInputKeyClosestToWorldLocation(CurLocation);

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
		FVector fScaleFactor = FVector(1.0f);

		if (bSlideDistanceIsInParentSpace)
			fScaleFactor = fScaleFactor / InitialRelativeTransform.GetScale3D();

		FVector MinScale = MinSlideDistance * fScaleFactor;

		FVector Dist = (MinSlideDistance + MaxSlideDistance) * fScaleFactor;
		FVector Progress = (ValueToClamp - (-MinScale)) / Dist;
			
		if (bSliderUsesSnapPoints)
		{
			Progress.X = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.X, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
			Progress.Y = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.Y, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
			Progress.Z = FMath::Clamp(UVRInteractibleFunctionLibrary::Interactible_GetThresholdSnappedValue(Progress.Z, SnapIncrement, SnapThreshold), 0.0f, 1.0f);
		}
		else
		{
			Progress.X = FMath::Clamp(Progress.X, 0.f, 1.f);
			Progress.Y = FMath::Clamp(Progress.Y, 0.f, 1.f);
			Progress.Z = FMath::Clamp(Progress.Z, 0.f, 1.f);
		}
		
		return (Progress * Dist) - (MinScale);
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
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface", meta = (DisplayName = "IsDenyingGrips"))
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

	// Returns if the object is socketed currently
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool RequestsSocketing(USceneComponent *& ParentToSocketTo, FName & OptionalSocketName, FTransform_NetQuantize & RelativeTransform);

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
		void OnGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);

	// Event triggered on the interfaced object when child component is gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);

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