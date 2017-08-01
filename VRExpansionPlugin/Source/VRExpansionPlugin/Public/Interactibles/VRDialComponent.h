// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "MotionControllerComponent.h"
#include "VRGripInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"


#include "VRDialComponent.generated.h"


/** Delegate for notification when the lever state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRDialStateChangedSignature, float, DialMilestoneAngle);

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRDialComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_UCLASS_BODY()

	~UVRDialComponent();

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRDialComponent")
		FVRDialStateChangedSignature OnDialStateChanged;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VRDialComponent")
	float CurrentDialAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	bool bDialUsesAngleSnap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float SnapAngleIncrement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	EVRInteractibleAxis DialRotationAxis;

	FTransform InitialRelativeTransform;
	float CurRotBackEnd;
	FRotator LastRotation;

	// Should be called after the moved is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
	void ResetInitialDialLocation()
	{
		// Get our initial relative transform to our parent (or not if un-parented).
		InitialRelativeTransform = this->GetRelativeTransform();
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
		

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripMovementReplicationSettings MovementReplicationSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float BreakDistance;

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
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

	// Grip type to use when gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType SlotGripType();

	// Grip type to use when not gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType FreeGripType();

	// Secondary grip type
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		ESecondaryGripType SecondaryGripType();

	// Define which movement repliation setting to use
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripMovementReplicationSettings GripMovementReplicationType();

	// Define the late update setting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripLateUpdateSettings GripLateUpdateSetting();

	// What grip stiffness to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripStiffness();

	// What grip damping to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripDamping();

	// Get the advanced physics settings for this grip
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		FBPAdvGripPhysicsSettings AdvancedPhysicsSettings();

	// What distance to break a grip at (only relevent with physics enabled grips
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripBreakDistance();

	// Get closest secondary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestSecondarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);

	// Get closest primary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestPrimarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);

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

	protected:

		inline float GetAxisValue(FRotator CheckRotation)
		{
			switch (DialRotationAxis)
			{
			case EVRInteractibleAxis::Axis_X:
				return CheckRotation.Roll; break;
			case EVRInteractibleAxis::Axis_Y:
				return CheckRotation.Pitch; break;
			case EVRInteractibleAxis::Axis_Z:
				return CheckRotation.Yaw; break;
			default:return 0.0f; break;
			}
		}

		inline FRotator SetAxisValue(float SetValue)
		{
			FRotator vec = FRotator::ZeroRotator;

			switch (DialRotationAxis)
			{
			case EVRInteractibleAxis::Axis_X:
				vec.Roll = SetValue; break;
			case EVRInteractibleAxis::Axis_Y:
				vec.Pitch = SetValue; break;
			case EVRInteractibleAxis::Axis_Z:
				vec.Yaw = SetValue; break;
			default:break;
			}

			return vec;
		}

		inline FRotator SetAxisValue(float SetValue, FRotator Var)
		{
			FRotator vec = Var;
			switch (DialRotationAxis)
			{
			case EVRInteractibleAxis::Axis_X:
				vec.Roll = SetValue; break;
			case EVRInteractibleAxis::Axis_Y:
				vec.Pitch = SetValue; break;
			case EVRInteractibleAxis::Axis_Z:
				vec.Yaw = SetValue; break;
			default:break;
			}

			return vec;
		}
};