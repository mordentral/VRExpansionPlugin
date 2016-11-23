// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "Components/ShapeComponent.h"
#include "VRSimpleCharacterMovementComponent.h"
#include "VRSimpleRootComponent.generated.h"

//For UE4 Profiler ~ Stat Group
//DECLARE_STATS_GROUP(TEXT("VRPhysicsUpdate"), STATGROUP_VRPhysics, STATCAT_Advanced);

// EXPERIMENTAL, don't use
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UVRSimpleRootComponent : public UCapsuleComponent//UShapeComponent
{
	GENERATED_UCLASS_BODY()

public:

	UFUNCTION(BlueprintPure, Category = "MotionController")
	FVector GetVRForwardVector()
	{
		return this->ComponentToWorld.GetRotation().GetForwardVector();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
	FVector GetVRRightVector()
	{
		return this->ComponentToWorld.GetRotation().GetRightVector();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
	FVector GetVRUpVector()
	{
		return this->ComponentToWorld.GetRotation().GetUpVector();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
	FVector GetVRLocation()
	{
		return this->ComponentToWorld.GetLocation();
	}

	UFUNCTION(BlueprintPure, Category = "MotionController")
	FRotator GetVRRotation()
	{
		return this->ComponentToWorld.GetRotation().Rotator();
	}

protected:


public:
	void BeginPlay() override;

	bool IsLocallyControlled() const
	{
		// I like epics implementation better than my own
		const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
	}

	UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
	USceneComponent * TargetPrimitiveComponent;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
	UVRSimpleCharacterMovementComponent * MovementComponent;

	//UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
	//UCapsuleComponent * VRCameraCollider;

	FVector DifferenceFromLastFrame;
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
	FTransform OffsetComponentToWorld;

	// Used to offset the collision (IE backwards from the player slightly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	FVector VRCapsuleOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bUseWalkingCollisionOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	TEnumAsByte<ECollisionChannel> WalkingCollisionOverride;

	ECollisionChannel GetVRCollisionObjectType()
	{
		if (bUseWalkingCollisionOverride)
			return WalkingCollisionOverride;
		else
			return GetCollisionObjectType();
	}

	FVector curCameraLoc;
	FRotator curCameraRot;

	FVector lastCameraLoc;
	FRotator lastCameraRot;

	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

public:
	// Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditChange(UProperty* PropertyThatWillChange);
#endif // WITH_EDITOR
	// End UObject interface

};