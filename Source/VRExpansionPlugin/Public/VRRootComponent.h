// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "Components/ShapeComponent.h"
#include "VRRootComponent.generated.h"

//For UE4 Profiler ~ Stat Group
DECLARE_STATS_GROUP(TEXT("VRPhysicsUpdate"), STATGROUP_VRPhysics, STATCAT_Advanced);

// EXPERIMENTAL, don't use
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UVRRootComponent : public UCapsuleComponent//UShapeComponent
{
	GENERATED_UCLASS_BODY()

public:
	friend class FDrawCylinderSceneProxy;

	void PreEditChange(UProperty* PropertyThatWillChange);

	void GenerateOffsetToWorld();

	UFUNCTION(BlueprintPure, Category = "MotionController")
	FVector GetVRForwardVector()
	{
		return OffsetComponentToWorld.GetRotation().GetForwardVector();
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

protected:

	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags, ETeleportType Teleport = ETeleportType::None) override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	void SendPhysicsTransform(ETeleportType Teleport);

	const TArray<FOverlapInfo>* ConvertRotationOverlapsToCurrentOverlaps(TArray<FOverlapInfo>& OverlapsAtEndLocation, const TArray<FOverlapInfo>& CurrentOverlaps);
	const TArray<FOverlapInfo>* ConvertSweptOverlapsToCurrentOverlaps(
	TArray<FOverlapInfo>& OverlapsAtEndLocation, const TArray<FOverlapInfo>& SweptOverlaps, int32 SweptOverlapsIndex,
	const FVector& EndLocation, const FQuat& EndRotationQuat);

public:
	void UVRRootComponent::BeginPlay() override;

	bool IsLocallyControlled() const
	{
		// Epic used a check for a player controller to control has authority, however the controllers are always attached to a pawn
		// So this check would have always failed to work in the first place.....

		APawn* Owner = Cast<APawn>(GetOwner());

		if (!Owner)
		{
			//const APlayerController* Actor = Cast<APlayerController>(GetOwner());
			//if (!Actor)
			return false;

			//return Actor->IsLocalPlayerController();
		}

		return Owner->IsLocallyControlled();
	}



	// Whether to auto size the capsule collision to the height of the head.
	UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
	USceneComponent * TargetPrimitiveComponent;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
	FTransform OffsetComponentToWorld;
	FVector DifferenceFromLastFrame;

	// Used to offset the collision (IE backwards from the player slightly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	FVector VRCapsuleOffset;

	FVector curCameraLoc;
	FRotator curCameraRot;

	FVector lastCameraLoc;
	FRotator lastCameraRot;

	bool bHadRelativeMovement;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

public:
	// Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	// End UObject interface

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
};