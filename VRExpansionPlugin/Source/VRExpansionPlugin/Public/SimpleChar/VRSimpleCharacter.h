// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRBPDatatypes.h"
#include "VRBaseCharacter.h"
#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "VRSimpleCharacterMovementComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "ReplicatedVRCameraComponent.h"
//#include "VRSimpleRootComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRSimpleCharacter.generated.h"


UCLASS()
class VREXPANSIONPLUGIN_API AVRSimpleCharacter : public AVRBaseCharacter
{
	GENERATED_BODY()

public:
	AVRSimpleCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	FORCEINLINE void GenerateOffsetToWorld()
	{
		FRotator CamRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(VRReplicatedCamera->GetComponentRotation());
		OffsetComponentToWorld = FTransform(CamRotOffset.Quaternion(), this->GetActorLocation(), this->GetActorScale3D());
	}

	// Regenerates the base offsetcomponenttoworld that VR uses
	//UFUNCTION(BlueprintCallable, Category = "BaseVRCharacter|VRLocations")
	virtual void RegenerateOffsetComponentToWorld(bool bUpdateBounds, bool bCalculatePureYaw) override
	{
		GenerateOffsetToWorld();
	}

	// Resetting if people turned off required settings here
	virtual void BeginPlay() override;

	// Overriding teleport so that it auto calls my controllers re-positioning
	virtual bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest = false, bool bNoCheck = false) override;

	UPROPERTY(Category = VRSimpleCharacter, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	USceneComponent * VRSceneComponent;

	//A helper function that offsets a given vector by the roots collision location
	//pass in a teleport location and it provides the correct spot for it to be at your feet

	//UFUNCTION(BlueprintPure, Category = "VRGrip")
	virtual FVector GetTeleportLocation(FVector OriginalLocation) override;		
};