// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "WorldCollision.h"
#include "VRMovementComponent.generated.h"


UCLASS(ClassGroup = Movement, meta = (BlueprintSpawnableComponent))
class VREXPANSIONPLUGIN_API UVRMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()
public:

	/**
	 * Default UObject constructor.
	 */
	UVRMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	//Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//End UActorComponent Interface

	//void ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult) const override;
	void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;

	UPROPERTY(BlueprintReadOnly, Transient, DuplicateTransient, Category = VRMovement)
	UVRRootComponent* VRRootComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = VRMovement)
	bool bEnableGravity;

	FVector GravityVelocity;

	/** Custom gravity scale. Gravity is multiplied by this amount for the character. */
	UPROPERTY(Category = "VRMovement", EditAnywhere, BlueprintReadWrite)
	float GravityScale;

	/** Maximum velocity magnitude allowed for the controlled Pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = VRMovement)
	float MoveSpeed;

	/**
	* Actor's current movement mode (walking, falling, etc).
	*    - walking:  Walking on a surface, under the effects of friction, and able to "step up" barriers. Vertical velocity is zero.
	*    - falling:  Falling under the effects of gravity, after jumping or walking off the edge of a surface.
	*    - flying:   Flying, ignoring the effects of gravity.
	*    - swimming: Swimming through a fluid volume, under the effects of gravity and buoyancy.
	*    - custom:   User-defined custom movement mode, including many possible sub-modes.
	* This is automatically replicated through the Character owner and for client-server movement functions.
	* @see SetMovementMode(), CustomMovementMode
	*/
	UPROPERTY(Category = "Character Movement: MovementMode", BlueprintReadOnly)
	TEnumAsByte<enum EMovementMode> MovementMode;

	/** Information about the floor the Character is standing on (updated only during walking movement). */
	UPROPERTY(Category = "VRMovement", VisibleInstanceOnly, BlueprintReadOnly)
	FFindFloorResult CurrentFloor;

	UFUNCTION(BlueprintCallable, Category = "VRMovement|Input", meta = (Keywords = "MoveForward"))
	void MoveForward(float ScaleValue = 1.0f, bool bForce = false);

	UFUNCTION(BlueprintCallable, Category = "VRMovement|Input", meta = (Keywords = "MoveRight"))
	void MoveRight(float ScaleValue = 1.0f, bool bForce = false);
	
	UFUNCTION(Reliable, Server, WithValidation)
	void Server_AddInputVector(FVector WorldAccel, bool bForce);


	//Begin UMovementComponent Interface
	//virtual float GetMaxSpeed() const override { return MaxSpeed; }

protected:
	virtual bool ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& NewRotation) override;
public:
	//End UMovementComponent Interface
protected:

	/** Prevent Pawn from leaving the world bounds (if that restriction is enabled in WorldSettings) */
	virtual bool LimitWorldBounds();

	/** Set to true when a position correction is applied. Used to avoid recalculating velocity when this occurs. */
	UPROPERTY(Transient)
		uint32 bPositionCorrected : 1;
};