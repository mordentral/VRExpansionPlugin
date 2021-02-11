// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRExpansionFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "Components/ShapeComponent.h"
#include "VRTrackedParentInterface.h"
#include "ParentRelativeAttachmentComponent.generated.h"

class AVRBaseCharacter;
class AVRCharacter;

// Type of rotation sampling to use
UENUM(BlueprintType)
enum class EVR_PRC_RotationMethod : uint8
{
	// Rotate purely to the HMD yaw, default mode
	PRC_ROT_HMD UMETA(DisplayName = "HMD rotation"),

	// Rotate to a blend between the HMD and Controller facing
	PRC_ROT_HMDControllerBlend UMETA(DisplayName = "ROT HMD Controller Blend"),

	// Rotate to the controllers with behind the back detection, clamp within neck limit
	PRC_ROT_ControllerHMDClamped UMETA(DisplayName = "Controller Clamped to HMD")
};


/**
* A component that will track the HMD/Cameras location and YAW rotation to allow for chest/waist attachements.
* This is intended to be parented to the root component of a pawn, it will then either find and track the camera
* or use the HMD's position if one is connected. This allows it to work in multiplayer since the camera will
* have its position replicated.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UParentRelativeAttachmentComponent : public USceneComponent, public IVRTrackedParentInterface
{
	GENERATED_BODY()

public:
	UParentRelativeAttachmentComponent(const FObjectInitializer& ObjectInitializer);
	virtual void InitializeComponent() override;

	// Rotation method to use for facing calulations
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		//EVR_PRC_RotationMethod YawRotationMethod;

	// Angle tolerance before we lerp / snap to the new rotation
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary", meta = (ClampMin = "0", UIMin = "0"))
		float YawTolerance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary", meta = (ClampMin = "0", UIMin = "0"))
		float LerpSpeed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		bool bLerpTransition;

	float LastRot;
	float LastLerpVal;
	float LerpTarget;
	bool bWasSetOnce;
	FTransform LeftControllerTrans;
	FTransform RightControllerTrans;

	// If true uses feet/bottom of the capsule as the base Z position for this component instead of the HMD/Camera Z position
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bUseFeetLocation;

	// An additional value added to the relative position of the PRC 
	// Can be used to offset the floor, or component heights if needed
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		FVector CustomOffset;
	
	// If true will subtract the HMD's location from the position, useful for if the actors base is set to the HMD location always (simple character).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bOffsetByHMD;

	// If true, will not set rotation every frame, only position
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bIgnoreRotationFromParent;

	// If true, this component will not perform logic in its tick, it will instead allow the character movement component to move it (unless the CMC is inactive, then it will go back to self managing)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		bool bUpdateInCharacterMovement;

	// If valid will use this as the tracked parent instead of the HMD / Parent
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRTrackedParentInterface")
	FBPVRWaistTracking_Info OptionalWaistTrackingParent;

	virtual void SetTrackedParent(UPrimitiveComponent * NewParentComponent, float WaistRadius, EBPVRWaistTrackingMode WaistTrackingMode) override
	{
		IVRTrackedParentInterface::Default_SetTrackedParent_Impl(NewParentComponent, WaistRadius, WaistTrackingMode, OptionalWaistTrackingParent, this);
	}

	UPROPERTY()
		TWeakObjectPtr<AVRCharacter> AttachChar;
	UPROPERTY()
		TWeakObjectPtr<AVRBaseCharacter> AttachBaseChar;

	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	virtual void OnAttachmentChanged() override;
	void UpdateTracking(float DeltaTime);

	bool IsLocallyControlled() const
	{
		// I like epics implementation better than my own
		const AActor* MyOwner = GetOwner();
		return MyOwner->HasLocalNetOwner();
		//const APawn* MyPawn = Cast<APawn>(MyOwner);
		//return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
	}

	// Sets the rotation and location depending on the control variables. Trying to remove some code duplication here
	inline void SetRelativeRotAndLoc(FVector NewRelativeLocation, FRotator NewRelativeRotation, float DeltaTime)
	{

		RunSampling(NewRelativeRotation, NewRelativeLocation);

		if (bUseFeetLocation)
		{
			if (!bIgnoreRotationFromParent)
			{
				SetRelativeLocationAndRotation(
					FVector(NewRelativeLocation.X, NewRelativeLocation.Y, 0.0f) + CustomOffset,
					GetCalculatedRotation(NewRelativeRotation, DeltaTime)
				);
			}
			else
			{
				SetRelativeLocation(FVector(NewRelativeLocation.X, NewRelativeLocation.Y, 0.0f) + CustomOffset);
			}
		}
		else
		{
			if (!bIgnoreRotationFromParent)
			{
				SetRelativeLocationAndRotation(
					NewRelativeLocation + CustomOffset,
					GetCalculatedRotation(NewRelativeRotation, DeltaTime)
				); // Use the HMD height instead
			}
			else
			{
				SetRelativeLocation(NewRelativeLocation + CustomOffset); // Use the HMD height instead
			}
		}
	}

	FQuat GetCalculatedRotation(FRotator InverseRot, float DeltaTime)
	{
		FRotator FinalRot = FRotator::ZeroRotator;

		if (FPlatformMath::Abs(FRotator::ClampAxis(InverseRot.Yaw) - LastRot) < YawTolerance)	// This is never true with the default value of 0.0f
		{
			if (!bWasSetOnce)
			{
				LastRot = FRotator::ClampAxis(InverseRot.Yaw);
				LastLerpVal = LastRot;
				LerpTarget = LastRot;
				bWasSetOnce = true;
			}
			
			if (bLerpTransition && !FMath::IsNearlyEqual(LastLerpVal, LerpTarget))
			{
				LastLerpVal = FMath::FixedTurn(LastLerpVal, LerpTarget, LerpSpeed * DeltaTime);
				FinalRot = FRotator(0, LastLerpVal, 0);
			}
			else
			{
				FinalRot = FRotator(0, LastRot, 0);
				LastLerpVal = LastRot;
			}
		}
		else
		{
			// If we are using a snap threshold
			if (!FMath::IsNearlyZero(YawTolerance))
			{
				LerpTarget = FRotator::ClampAxis(InverseRot.Yaw);
				LastLerpVal = FMath::FixedTurn(/*LastRot*/LastLerpVal, LerpTarget, LerpSpeed * DeltaTime);
				FinalRot = FRotator(0, LastLerpVal, 0);
			}
			else // If we aren't then just directly set it to the correct rotation
			{
				FinalRot = FRotator(0, FRotator::ClampAxis(InverseRot.Yaw), 0);
			}

			LastRot = FRotator::ClampAxis(InverseRot.Yaw);
		}

		return FinalRot.Quaternion();
	}



	void RunSampling(FRotator &HMDRotation, FVector &HMDLocation)
	{
		/*switch(YawRotationMethod)
		{
			case EVR_PRC_RotationMethod::PRC_ROT_HMD:
			{
				return;
			}break;

			case EVR_PRC_RotationMethod::PRC_ROT_HMDControllerBlend:
			{
				return;
			}break;

			case EVR_PRC_RotationMethod::PRC_ROT_ControllerHMDClamped:
			{
				GetEstShoulderRotation(HMDRotation, HMDLocation);
				return;
			}break;
		}*/

	}

	// Get combined direction angle up
	void GetEstShoulderRotation(FRotator &InputHMDRotation, FVector &InputHMDLocation)
	{
		float WorldToMeters = GetWorld() ? GetWorld()->GetWorldSettings()->WorldToMeters : 100.0f;

		// Position shoulder (neck)
		FTransform shoulder = FTransform::Identity;
		FVector headNeckDirectionVector = FVector(-.05f, 0.f, -1.f);
		FVector neckShoulderDistance = FVector(-0.02f, 0.f, -.15f) * WorldToMeters; // World To Meters idealy
		float headNeckDistance = 0.03f * WorldToMeters; // World To Meters idealy

		FVector headNeckOffset = InputHMDRotation.RotateVector(headNeckDirectionVector);
		FVector targetPosition = InputHMDLocation + headNeckOffset * headNeckDistance;
		shoulder.SetLocation(targetPosition + InputHMDRotation.RotateVector(neckShoulderDistance));

		//DrawDebugSphere(GetWorld(), (shoulder * GetAttachParent()->GetComponentTransform()).GetLocation(), 4.0f, 32, FColor::White);
		return;


		/*if (IsLocallyControlled() && GEngine->XRSystem.IsValid())
		{
			FVector Position = GetRelativeLocation();
			FRotator Orientation = GetRelativeRotation();

			if (AttachBaseChar.IsValid())
			{
				if (AttachBaseChar->LeftMotionController)
				{
					const bool bNewTrackedState = AttachBaseChar->LeftMotionController->GripPollControllerState(Position, Orientation, WorldToMeters);
					bool bTracked = bNewTrackedState && CurrentTrackingStatus != ETrackingStatus::NotTracked;

					if (bTracked)
					{
						LeftControllerTrans = FTransform(Position, Orientation);
					}
				}

				if (AttachBaseChar->RightMotionController)
				{
					const bool bNewTrackedState = AttachBaseChar->RightMotionController->GripPollControllerState(Position, Orientation, WorldToMeters);
					bool bTracked = bNewTrackedState && CurrentTrackingStatus != ETrackingStatus::NotTracked;

					if (bTracked)
					{
						RightControllerTrans = FTransform(Position, Orientation);
					}
				}
			}
		}
		else if (AttachBaseChar.IsValid())
		{
			LeftControllerTrans = AttachBaseChar->LeftMotionController->GetRelativeTransform();
			RightControllerTrans = AttachBaseChar->RightMotionController->GetRelativeTransform();
		}

		FVector LeftHand = LeftControllerTrans.GetLocation();
		FVector RightHand = RightControllerTrans.GetLocation();

		//FRotator LeveledRel = CurrentTransforms.PureCameraYaw;

		FVector DistLeft = LeftHand - shoulder.Transform.GetLocation();
		FVector DistRight = RightHand - shoulder.Transform.GetLocation();

		if (bIgnoreZPos)
		{
			DistLeft.Z = 0.0f;
			DistRight.Z = 0.0f;
		}

		FVector DirLeft = DistLeft.GetSafeNormal();
		FVector DirRight = DistRight.GetSafeNormal();

		FVector CombinedDir = DirLeft + DirRight;
		float FinalRot = FMath::RadiansToDegrees(FMath::Atan2(CombinedDir.Y, CombinedDir.X));

		DetectHandsBehindHead(FinalRot, InputHMDRotation);
		ClampHeadRotationDelta(FinalRot, InputHMDRotation);

		return FinalRot;*/
	}

	void DetectHandsBehindHead(float& TargetRot, FRotator HMDRotation)
	{
		/*float delta = FRotator::ClampAxis(FMath::FindDeltaAngleDegrees(TargetRot, LastTargetRot));// FRotator::ClampAxis(TargetRot - LastTargetRot);

		if (delta > 150.f && delta < 210.0f && !FMath::IsNearlyZero(LastTargetRot) && !bClampingHeadRotation)
		{
			bHandsBehindHead = !bHandsBehindHead;
			if (bHandsBehindHead)
			{
				BehindHeadAngle = TargetRot;
			}
			else
			{
				BehindHeadAngle = 0.f;
			}
		}
		else if (bHandsBehindHead)
		{
			float delta2 = FMath::Abs(FMath::FindDeltaAngleDegrees(TargetRot, BehindHeadAngle));

			if (delta2 > 90.f)
			{
				bHandsBehindHead = !bHandsBehindHead;
				BehindHeadAngle = 0.f;
			}
		}

		LastTargetRot = TargetRot;

		if (bHandsBehindHead)
		{
			TargetRot += 180.f;
		}*/
	}

	// Clamp head rotation delta yaw
	void ClampHeadRotationDelta(float& TargetRotation, FRotator HMDRotation)
	{
		/*float HeadRotation = FRotator::ClampAxis(CurrentTransforms.PureCameraYaw.Yaw);
		float cTargetRotation = FRotator::ClampAxis(TargetRotation);

		float delta = HeadRotation - cTargetRotation;

		if ((delta > 80.f && delta < 180.f) || (delta < -180.f && delta >= -360.f + 80))
		{
			TargetRotation = HeadRotation - 80.f;
			bClampingHeadRotation = true;
		}
		else if ((delta < -80.f && delta > -180.f) || (delta > 180.f && delta < 360.f - 80.f))
		{
			TargetRotation = HeadRotation + 80.f;
			bClampingHeadRotation = true;
		}
		else
		{
			bClampingHeadRotation = false;
		}*/
	}
};

