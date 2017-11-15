// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "AIController.h"
#include "VRBaseCharacter.h"

#include "VRAIController.generated.h"


UCLASS()
class VREXPANSIONPLUGIN_API AVRAIController : public AAIController
{
	GENERATED_BODY()

public:
	virtual FVector GetFocalPointOnActor(const AActor *Actor) const override;

	/**
	* Checks line to center and top of other actor
	* @param Other is the actor whose visibility is being checked.
	* @param ViewPoint is eye position visibility is being checked from.  If vect(0,0,0) passed in, uses current viewtarget's eye position.
	* @param bAlternateChecks used only in AIController implementation
	* @return true if controller's pawn can see Other actor.
	*/
	virtual bool LineOfSightTo(const AActor* Other, FVector ViewPoint = FVector(ForceInit), bool bAlternateChecks = false) const override;
	//~ End AController Interface
};


UCLASS()
class AVRDetourCrowdAIController : public AVRAIController
{
	GENERATED_BODY()
public:
	AVRDetourCrowdAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};