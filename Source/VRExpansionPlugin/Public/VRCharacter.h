// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRBPDatatypes.h"
#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "ReplicatedVRCameraComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "VRRootComponent.h"
#include "VRCharacterMovementComponent.h"

#include "VRCharacter.generated.h"


UCLASS()
class VREXPANSIONPLUGIN_API AVRCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AVRCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Overriding teleport so that it auto calls my controllers re-positioning
	virtual bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest = false, bool bNoCheck = false) override;
	
	UPROPERTY(Category = VRCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UVRRootComponent * VRRootReference;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UVRCharacterMovementComponent * VRMovementReference;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UReplicatedVRCameraComponent * VRReplicatedCamera;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UParentRelativeAttachmentComponent * ParentRelativeAttachment;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UGripMotionControllerComponent * LeftMotionController;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UGripMotionControllerComponent * RightMotionController;


	/* 
	A helper function that offsets a given vector by the roots collision location
	pass in a teleport location and it provides the correct spot for it to be at your feet
	*/
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	FVector GetTeleportLocation(FVector OriginalLocation);


	UFUNCTION(Reliable, NetMulticast, Category = "VRGrip")
	void NotifyOfTeleport();

	// Overriding to correct some nav stuff
	FVector GetNavAgentLocation() const override;

	UFUNCTION(BlueprintImplementableEvent, Category = "VRCharacter")
	void ReceiveNavigationMoveCompleted(FAIRequestID RequestID,/* EPathFollowingResult::Type Result*/ bool Result);

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 12

	virtual void NavigationMoveCompleted(FAIRequestID RequestID, EPathFollowingResult::Type Result)
	{
		ReceiveNavigationMoveCompleted(RequestID, /*Result*/true);
	}
#else

	virtual void NavigationMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
	{
		ReceiveNavigationMoveCompleted(RequestID, /*Result*/true);
	}
#endif
};