// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBaseCharacter.h"
#include "VRCharacter.generated.h"

class UVRRootComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogVRCharacter, Log, All);

UCLASS()
class VREXPANSIONPLUGIN_API AVRCharacter : public AVRBaseCharacter
{
	GENERATED_BODY()

public:
	AVRCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Overriding teleport so that it auto calls my controllers re-positioning
	virtual bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest = false, bool bNoCheck = false) override;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))	
		TObjectPtr<UVRRootComponent> VRRootReference;

	virtual FVector GetTargetHeightOffset() override;

	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;


	// Regenerates the base offsetcomponenttoworld that VR uses
	virtual void RegenerateOffsetComponentToWorld(bool bUpdateBounds, bool bCalculatePureYaw) override;
	virtual void SetCharacterSizeVR(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps = true) override;
	virtual void SetCharacterHalfHeightVR(float HalfHeight, bool bUpdateOverlaps = true) override;

	/* 
	A helper function that offsets a given vector by the roots collision location
	pass in a teleport location and it provides the correct spot for it to be at your feet
	*/
	virtual FVector GetTeleportLocation(FVector OriginalLocation) override;

	// Returns the head location projected from the world offset (if applicable)
	virtual FVector GetProjectedVRLocation() const override;
	
	
	// Overriding to correct some nav stuff
	FVector GetNavAgentLocation() const override;

	// An extended simple move to location with additional parameters
		virtual void ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius = -1, bool bStopOnOverlap = false,
			bool bUsePathfinding = true, bool bProjectDestinationToNavigation = true, bool bCanStrafe = false,
			TSubclassOf<UNavigationQueryFilter> FilterClass = NULL, bool bAllowPartialPath = true) override;

};