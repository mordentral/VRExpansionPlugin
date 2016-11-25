// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRBPDatatypes.h"
#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "ReplicatedVRSimpleCameraComponent.h"
#include "VRSimpleCharacterMovementComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "VRSimpleRootComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRSimpleCharacter.generated.h"


UCLASS()
class VREXPANSIONPLUGIN_API AVRSimpleCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AVRSimpleCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());


	UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
		FTransform OffsetComponentToWorld;

	/*FVector GetVROffsetFromLocationAndRotation(FVector Location, const FQuat &Rotation)
	{
	FRotator CamRotOffset(0.0f, curCameraRot.Yaw, 0.0f);
	FTransform testComponentToWorld = FTransform(Rotation, Location, RelativeScale3D);

	return testComponentToWorld.TransformPosition(FVector(curCameraLoc.X, curCameraLoc.Y, CapsuleHalfHeight) + CamRotOffset.RotateVector(VRCapsuleOffset));
	}*/

	UFUNCTION(BlueprintPure, Category = "MotionController")
		FVector GetVRForwardVector()
	{
		return UVRExpansionFunctionLibrary::GetHMDPureYaw_I(VRReplicatedCamera->GetComponentRotation()).Quaternion().GetForwardVector();
		//return OffsetComponentToWorld.GetRotation().GetForwardVector();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
		FVector GetVRRightVector()
	{
		return OffsetComponentToWorld.GetRotation().GetRightVector();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
		FVector GetVRUpVector()
	{
		return OffsetComponentToWorld.GetRotation().GetUpVector();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
		FVector GetVRLocation()
	{
		return OffsetComponentToWorld.GetLocation();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
		FRotator GetVRRotation()
	{
		return OffsetComponentToWorld.GetRotation().Rotator();
	}




	// Overriding teleport so that it auto calls my controllers re-positioning
	virtual bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest = false, bool bNoCheck = false) override;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))	
	UVRSimpleRootComponent * VRRootReference;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UVRSimpleCharacterMovementComponent * VRMovementReference;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	USceneComponent * VRSceneComponent;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UReplicatedVRSimpleCameraComponent * VRReplicatedCamera;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UParentRelativeAttachmentComponent * ParentRelativeAttachment;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UGripMotionControllerComponent * LeftMotionController;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UGripMotionControllerComponent * RightMotionController;

	/* 
	A helper function that offsets a given vector by the roots collision location
	pass in a teleport location and it provides the correct spot for it to be at your feet
	*/
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	FVector GetTeleportLocation(FVector OriginalLocation);

	UFUNCTION(Reliable, NetMulticast, Category = "VRGrip")
	void NotifyOfTeleport();


	// Event when a navigation pathing operation has completed, auto calls stop movement for VR characters
	UFUNCTION(BlueprintImplementableEvent, Category = "VRSimpleCharacter")
	void ReceiveNavigationMoveCompleted(EPathFollowingResult::Type PathingResult);

	virtual void NavigationMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
	{
		this->Controller->StopMovement();
		ReceiveNavigationMoveCompleted(Result.Code);
	}

	/** Returns status of path following */
	UFUNCTION(BlueprintCallable, Category = "VRSimpleCharacter")
	EPathFollowingStatus::Type GetMoveStatus() const
	{
		if (!Controller)
			return EPathFollowingStatus::Idle;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			pathComp->GetStatus();
		}
	
		return EPathFollowingStatus::Idle;
	}

	/** Returns true if the current PathFollowingComponent's path is partial (does not reach desired destination). */
	UFUNCTION(BlueprintCallable, Category = "VRSimpleCharacter")
	bool HasPartialPath() const
	{
		if (!Controller)
			return false;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			return pathComp->HasPartialPath();
		}

		return false;
	}

	// Instantly stops pathing
	UFUNCTION(BlueprintCallable, Category = "VRSimpleCharacter")
	void StopNavigationMovement()
	{
		if (!Controller)
			return;

		if (UPathFollowingComponent* pathComp = Controller->FindComponentByClass<UPathFollowingComponent>())
		{
			// @note FPathFollowingResultFlags::ForcedScript added to make AITask_MoveTo instances 
			// not ignore OnRequestFinished notify that's going to be sent out due to this call
			pathComp->AbortMove(*this, FPathFollowingResultFlags::MovementStop | FPathFollowingResultFlags::ForcedScript);
		}
	}

	UPROPERTY(BlueprintReadWrite, Category = AI)
	TSubclassOf<UNavigationQueryFilter> DefaultNavigationFilterClass;

	// An extended simple move to location with additional parameters
	UFUNCTION(BlueprintCallable, Category = "VRSimpleCharacter", Meta = (AdvancedDisplay = "bStopOnOverlap,bCanStrafe,bAllowPartialPath"))
	void ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius = -1, bool bStopOnOverlap = false, 
		bool bUsePathfinding = true, bool bProjectDestinationToNavigation = true, bool bCanStrafe = false,
		TSubclassOf<UNavigationQueryFilter> FilterClass = NULL, bool bAllowPartialPath = true);
};