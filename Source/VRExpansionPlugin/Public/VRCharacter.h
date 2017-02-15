// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "VRBPDatatypes.h"
#include "VRBaseCharacter.h"
#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "ReplicatedVRCameraComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "VRRootComponent.h"
#include "VRCharacterMovementComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRCharacter.generated.h"


UCLASS()
class VREXPANSIONPLUGIN_API AVRCharacter : public AVRBaseCharacter
{
	GENERATED_BODY()

public:
	AVRCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Overriding teleport so that it auto calls my controllers re-positioning
	virtual bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest = false, bool bNoCheck = false) override;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))	
	UVRRootComponent * VRRootReference;

	/* 
	A helper function that offsets a given vector by the roots collision location
	pass in a teleport location and it provides the correct spot for it to be at your feet
	*/
	UFUNCTION(BlueprintPure, Category = "VRGrip")
	virtual FVector GetTeleportLocation(FVector OriginalLocation) override;
	

	UFUNCTION(Reliable, NetMulticast, Category = "VRGrip")
	virtual void NotifyOfTeleport() override;
	
	// Overriding to correct some nav stuff
	FVector GetNavAgentLocation() const override;

	// An extended simple move to location with additional parameters
	UFUNCTION(BlueprintCallable, Category = "VRCharacter", Meta = (AdvancedDisplay = "bStopOnOverlap,bCanStrafe,bAllowPartialPath"))
		void ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius = -1, bool bStopOnOverlap = false,
			bool bUsePathfinding = true, bool bProjectDestinationToNavigation = true, bool bCanStrafe = false,
			TSubclassOf<UNavigationQueryFilter> FilterClass = NULL, bool bAllowPartialPath = true) override;
};