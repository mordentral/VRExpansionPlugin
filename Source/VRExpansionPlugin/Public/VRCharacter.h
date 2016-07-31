// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

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

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UReplicatedVRCameraComponent * VRReplicatedCamera;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UParentRelativeAttachmentComponent * ParentRelativeAttachment;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UGripMotionControllerComponent * LeftMotionController;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UGripMotionControllerComponent * RightMotionController;

};