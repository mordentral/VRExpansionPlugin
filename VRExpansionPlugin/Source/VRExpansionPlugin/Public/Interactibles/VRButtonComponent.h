// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "MotionControllerComponent.h"
#include "VRGripInterface.h"
//#include "VRBPDatatypes.h"
//#include "VRExpansionFunctionLibrary.h"
#include "VRButtonComponent.generated.h"

/**
*
*/

// VR Button Types
UENUM(Blueprintable)
enum class EVRButtonType : uint8
{
	Btn_Press,
	Btn_Toggle_Return,
	Btn_Toggle_Stay
};

// VR Button Press Axis
UENUM(Blueprintable)
enum class EVRButtonDepressAxis : uint8
{
	Btn_Axis_X,
	Btn_Axis_Y,
	Btn_Axis_Z
};

/** Delegate for notification when the button state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRButtonStateChangedSignature, bool, ButtonState);

UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRButtonComponent : public UStaticMeshComponent
{
	GENERATED_UCLASS_BODY()

	~UVRButtonComponent();

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);	

	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "VRButtonComponent")
	bool IsButtonInUse()
	{
		return InteractingComponent.IsValid();
	}

	// Should be called after the button is moved post begin play
	UFUNCTION(BlueprintCallable, Category = "VRButtonComponent")
	void ResetInitialButtonLocation()
	{
		// Get our initial relative transform to our parent (or not if un-parented).
		InitialRelativeTransform = this->GetRelativeTransform();
	}

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VRButtonComponent")
		FVRButtonStateChangedSignature OnButtonStateChanged;

	UPROPERTY(BlueprintReadOnly, Category = "VRButtonComponent")
		TWeakObjectPtr<UPrimitiveComponent> InteractingComponent;


	// Whether the button is enabled or not (can be interacted with)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRButtonComponent")
	bool bIsEnabled;

	// Current state of the button, writable to set initial value
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "VRButtonComponent")
	bool bButtonState;

	// #TODO: Offer to autocalulate depress distance / speed based on mesh size?

	// Speed that the button de-presses when no longer interacted with
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRButtonComponent")
	float DepressSpeed;

	// Distance that the button depresses
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRButtonComponent")
	float DepressDistance;

	// Type of button this is
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRButtonComponent")
	EVRButtonType ButtonType;

	// Negative on this axis is the depress direction
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRButtonComponent")
	EVRButtonDepressAxis ButtonAxis;

	// Depth at which the button engages (switches)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRButtonComponent")
	float ButtonEngageDepth;

	// Minimum time before the button can be switched again
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRButtonComponent")
	float MinTimeBetweenEngaging;


	virtual bool IsValidOverlap(UPrimitiveComponent * OverlapComponent)
	{

		// Early out on the simple checks
		if (!OverlapComponent || OverlapComponent == GetAttachParent() || OverlapComponent->GetAttachParent() == GetAttachParent())
			return false;

		// Should return faster checking for owning character
		AActor * OverlapOwner = OverlapComponent->GetOwner();
		if (OverlapOwner && OverlapOwner->IsA(ACharacter::StaticClass()))
			return true;

		// Because epic motion controllers are not owned by characters have to check here too in case someone implements it like that
		USceneComponent * OurAttachParent = OverlapComponent->GetAttachParent();
		if (OurAttachParent && (OurAttachParent->IsA(UGripMotionControllerComponent::StaticClass()) || OurAttachParent->IsA(UMotionControllerComponent::StaticClass())))
			return true;

		// Now check for if it is a grippable object and if it is currently held
		if (OverlapComponent->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
		{
			UGripMotionControllerComponent *Controller;
			bool bIsHeld;
			IVRGripInterface::Execute_IsHeld(OverlapComponent, Controller, bIsHeld);

			if (bIsHeld)
				return true;
		}

		return false;
	}

	virtual FVector GetTargetRelativeLocation()
	{
		// If target is the half pressed
		if (ButtonType == EVRButtonType::Btn_Toggle_Stay && bButtonState)
		{
			// 1.e-2f = MORE_KINDA_SMALL_NUMBER
			return InitialRelativeTransform.TransformPosition(SetAxisValue(-(ButtonEngageDepth + (1.e-2f))));
		}
		
		// Else return going all the way back
		return InitialRelativeTransform.GetTranslation();

	}

protected:

	// Control variables
	FVector InitialLocation;
	bool bToggledThisTouch;
	FVector InitialComponentLoc;
	float LastToggleTime;
	FTransform InitialRelativeTransform;

	FORCEINLINE float GetAxisValue(FVector CheckLocation)
	{
		switch (ButtonAxis)
		{
		case EVRButtonDepressAxis::Btn_Axis_X:
			return CheckLocation.X; break;
		case EVRButtonDepressAxis::Btn_Axis_Y:
			return CheckLocation.Y; break;
		case EVRButtonDepressAxis::Btn_Axis_Z:
			return CheckLocation.Z; break;
		default:return 0.0f; break;
		}
	}

	FORCEINLINE FVector SetAxisValue(float SetValue)
	{
		FVector vec = FVector::ZeroVector;

		switch (ButtonAxis)
		{
		case EVRButtonDepressAxis::Btn_Axis_X:
			vec.X = SetValue; break;
		case EVRButtonDepressAxis::Btn_Axis_Y:
			vec.Y = SetValue; break;
		case EVRButtonDepressAxis::Btn_Axis_Z:
			vec.Z = SetValue; break;
		}

		return vec;
	}

};