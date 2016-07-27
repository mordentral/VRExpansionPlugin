// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "VRRootcomponent.h"
//#include "Character.h"
#include "VRCharacter.generated.h"

// EXPERIMENTAL, don't use
UCLASS(Blueprintable, BlueprintType, ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API AVRCharacter : public ACharacter
{
	GENERATED_BODY()

	AVRCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UPROPERTY(Category = Character, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UReplicatedVRCameraComponent* VRReplicatedCamera;

	UPROPERTY(Category = Character, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UParentRelativeAttachmentComponent* VRRelativeComponent;

	UPROPERTY(Category = Character, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UGripMotionControllerComponent* LeftMotionController;

	UPROPERTY(Category = Character, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	class UGripMotionControllerComponent* RightMotionController;
};