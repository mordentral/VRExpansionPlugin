// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "MotionControllerComponent.h"
#include "VRGripInterface.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "VRInteractibleFunctionLibrary.h"


#include "VRDialComponent.generated.h"


/** Delegate for notification when the lever state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRDialStateChangedSignature, float, DialMilestoneAngle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRDialFinishedLerpingSignature);

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRDialComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UVRDialComponent(const FObjectInitializer& ObjectInitializer);


	~UVRDialComponent();

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRDialComponent")
		FVRDialStateChangedSignature OnDialHitSnapAngle;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Dial Hit Snap Angle"))
		void ReceiveDialHitSnapAngle(float DialMilestoneAngle);

	// If true the dial will lerp back to zero on release
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent|Lerping")
		bool bLerpBackOnRelease;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent|Lerping")
		bool bSendDialEventsDuringLerp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent|Lerping")
		float DialReturnSpeed;

	UPROPERTY(BlueprintReadOnly, Category = "VRDialComponent|Lerping")
		bool bIsLerping;

	UPROPERTY(BlueprintAssignable, Category = "VRDialComponent|Lerping")
		FVRDialFinishedLerpingSignature OnDialFinishedLerping;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Dial Finished Lerping"))
		void ReceiveDialFinishedLerping();

	UPROPERTY(BlueprintReadOnly, Category = "VRDialComponent")
	float CurrentDialAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (ClampMin = "0.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0"))
	float ClockwiseMaximumDialAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (ClampMin = "0.0", ClampMax = "360.0", UIMin = "0.0", UIMax = "360.0"))
	float CClockwiseMaximumDialAngle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	bool bDialUsesAngleSnap;

	// Angle that the dial snaps to on release and when within the threshold distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float SnapAngleIncrement;

	// Threshold distance that when within the dial will stay snapped to its snap increment
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float SnapAngleThreshold;

	// Scales rotational input to speed up or slow down the rotation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float RotationScaler;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	EVRInteractibleAxis DialRotationAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
	EVRInteractibleAxis InteractorRotationAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRDialComponent")
		int GripPriority;

	FTransform InitialRelativeTransform;
	FVector InitialInteractorLocation;
	FVector InitialDropLocation;
	float CurRotBackEnd;
	FRotator LastRotation;
	float LastSnapAngle;

	// Should be called after the dial is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
	void ResetInitialDialLocation()
	{
		// Get our initial relative transform to our parent (or not if un-parented).
		InitialRelativeTransform = this->GetRelativeTransform();
		CurRotBackEnd = 0.0f;
	}

	// Can be called to recalculate the dial angle after you move it if you want different values
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
	void AddDialAngle(float DialAngleDelta, bool bCallEvents = false)
	{
		float MaxCheckValue = 360.0f - CClockwiseMaximumDialAngle;

		float DeltaRot = DialAngleDelta;
		float tempCheck = FRotator::ClampAxis(CurRotBackEnd + DeltaRot);

		// Clamp it to the boundaries
		if (FMath::IsNearlyZero(CClockwiseMaximumDialAngle))
		{
			CurRotBackEnd = FMath::Clamp(CurRotBackEnd + DeltaRot, 0.0f, ClockwiseMaximumDialAngle);
		}
		else if (FMath::IsNearlyZero(ClockwiseMaximumDialAngle))
		{
			if (CurRotBackEnd < MaxCheckValue)
				CurRotBackEnd = FMath::Clamp(360.0f + DeltaRot, MaxCheckValue, 360.0f);
			else
				CurRotBackEnd = FMath::Clamp(CurRotBackEnd + DeltaRot, MaxCheckValue, 360.0f);
		}
		else if (tempCheck > ClockwiseMaximumDialAngle && tempCheck < MaxCheckValue)
		{
			if (CurRotBackEnd < MaxCheckValue)
			{
				CurRotBackEnd = ClockwiseMaximumDialAngle;
			}
			else
			{
				CurRotBackEnd = MaxCheckValue;
			}
		}
		else
			CurRotBackEnd = tempCheck;

		if (bDialUsesAngleSnap && FMath::Abs(FMath::Fmod(CurRotBackEnd, SnapAngleIncrement)) <= FMath::Min(SnapAngleIncrement, SnapAngleThreshold))
		{
			this->SetRelativeRotation((FTransform(SetAxisValue(FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement), FRotator::ZeroRotator, DialRotationAxis)) * InitialRelativeTransform).Rotator());
			CurrentDialAngle = FMath::RoundToFloat(FMath::GridSnap(CurRotBackEnd, SnapAngleIncrement));

			if (bCallEvents && !FMath::IsNearlyEqual(LastSnapAngle, CurrentDialAngle))
			{
				ReceiveDialHitSnapAngle(CurrentDialAngle);
				OnDialHitSnapAngle.Broadcast(CurrentDialAngle);
			}

			LastSnapAngle = CurrentDialAngle;
		}
		else
		{
			this->SetRelativeRotation((FTransform(SetAxisValue(CurRotBackEnd, FRotator::ZeroRotator, DialRotationAxis)) * InitialRelativeTransform).Rotator());
			CurrentDialAngle = FMath::RoundToFloat(CurRotBackEnd);
		}

	}

	// Directly sets the dial angle, still obeys maximum limits and snapping though
	UFUNCTION(BlueprintCallable, Category = "VRLeverComponent")
		void SetDialAngle(float DialAngle, bool bCallEvents = false)
	{
		float MaxCheckValue = 360.0f - CClockwiseMaximumDialAngle;

		CurRotBackEnd = DialAngle;

		AddDialAngle(0.0f);
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

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadOnly, Category = "VRGripInterface")
		UGripMotionControllerComponent * HoldingController; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
		bool bDenyGripping;

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

	// Get grip slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestGripSlotInRange(FVector WorldLocation, bool bSecondarySlot, bool & bHadSlotInRange, FTransform & SlotWorldTransform, UGripMotionControllerComponent * CallingController = nullptr, FName OverridePrefix = NAME_None);



	// Check if the object is an interactable
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
	//	bool IsInteractible();

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
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		//FBPInteractionSettings GetInteractionSettings();

	// Get grip scripts
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool GetGripScripts(UPARAM(ref) TArray<UVRGripScriptBase*> & ArrayReference);


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

		inline float GetAxisValue(FRotator CheckRotation, EVRInteractibleAxis RotationAxis)
		{
			switch (RotationAxis)
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

		inline FRotator SetAxisValue(float SetValue, EVRInteractibleAxis RotationAxis)
		{
			FRotator vec = FRotator::ZeroRotator;

			switch (RotationAxis)
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

		inline FRotator SetAxisValue(float SetValue, FRotator Var, EVRInteractibleAxis RotationAxis)
		{
			FRotator vec = Var;
			switch (RotationAxis)
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