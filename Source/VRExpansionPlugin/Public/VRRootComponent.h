// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "Components/ShapeComponent.h"
#include "VRRootComponent.generated.h"

//For UE4 Profiler ~ Stat Group
//DECLARE_STATS_GROUP(TEXT("VRPhysicsUpdate"), STATGROUP_VRPhysics, STATCAT_Advanced);

// EXPERIMENTAL, don't use
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UVRRootComponent : public UShapeComponent//USceneComponent
{
	GENERATED_UCLASS_BODY()

	// Whether to auto size the capsule collision to the height of the head.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary|AutoCapsule")
	bool bAutoSizeCapsuleHeight;

	// Used to offset the collision (IE backwards from the player slightly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary|AutoCapsule")
	FVector AutoSizeCapsuleOffset;

	// If true will update position every frame, if false will use the AutoCapsuleUpdateRate instead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary|AutoCapsule")
	bool bAutoCapsuleUpdateEveryFrame;

	// Rate to update the height of the collision capsule relative to the headset
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary|AutoCapsule")
	float AutoCapsuleUpdateRate;

	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case.
	float AutoCapsuleUpdateCount;

	FVector curCameraLoc;
	FQuat curCameraRot;


	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
//};

protected:
	/**
	*	Half-height, from center of capsule to the end of top or bottom hemisphere.
	*	This cannot be less than CapsuleRadius.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = Shape, meta = (ClampMin = "0", UIMin = "0"))
		float CapsuleHalfHeight;

	/**
	*	Radius of cap hemispheres and center cylinder.
	*	This cannot be more than CapsuleHalfHeight.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = Shape, meta = (ClampMin = "0", UIMin = "0"))
		float CapsuleRadius;

protected:
	UPROPERTY()
		float CapsuleHeight_DEPRECATED;

public:
	/**
	* Change the capsule size. This is the unscaled size, before component scale is applied.
	* @param	InRadius : radius of end-cap hemispheres and center cylinder.
	* @param	InHalfHeight : half-height, from capsule center to end of top or bottom hemisphere.
	* @param	bUpdateOverlaps: if true and this shape is registered and collides, updates touching array for owner actor.
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		void SetCapsuleSize(float InRadius, float InHalfHeight, bool bUpdateOverlaps = true);

	/**
	* Set the capsule radius. This is the unscaled radius, before component scale is applied.
	* If this capsule collides, updates touching array for owner actor.
	* @param	Radius : radius of end-cap hemispheres and center cylinder.
	* @param	bUpdateOverlaps: if true and this shape is registered and collides, updates touching array for owner actor.
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		void SetCapsuleRadius(float Radius, bool bUpdateOverlaps = true);

	/**
	* Set the capsule half-height. This is the unscaled half-height, before component scale is applied.
	* If this capsule collides, updates touching array for owner actor.
	* @param	HalfHeight : half-height, from capsule center to end of top or bottom hemisphere.
	* @param	bUpdateOverlaps: if true and this shape is registered and collides, updates touching array for owner actor.
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		void SetCapsuleHalfHeight(float HalfHeight, bool bUpdateOverlaps = true);

	// Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	// End UObject interface

	// Begin USceneComponent interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void CalcBoundingCylinder(float& CylinderRadius, float& CylinderHalfHeight) const override;
	// End USceneComponent interface

	// Begin UPrimitiveComponent interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual bool IsZeroExtent() const override;
	virtual struct FCollisionShape GetCollisionShape(float Inflation = 0.0f) const override;
	virtual bool AreSymmetricRotations(const FQuat& A, const FQuat& B, const FVector& Scale3D) const override;
	// End UPrimitiveComponent interface.

	// Begin UShapeComponent interface
	virtual void UpdateBodySetup() override;
	// End UShapeComponent interface

	// @return the capsule radius scaled by the component scale.
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		float GetScaledCapsuleRadius() const;

	// @return the capsule half height scaled by the component scale.
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		float GetScaledCapsuleHalfHeight() const;

	// @return the capsule radius and half height scaled by the component scale.
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		void GetScaledCapsuleSize(float& OutRadius, float& OutHalfHeight) const;


	// @return the capsule radius, ignoring component scaling.
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		float GetUnscaledCapsuleRadius() const;

	// @return the capsule half height, ignoring component scaling.
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		float GetUnscaledCapsuleHalfHeight() const;

	// @return the capsule radius and half height, ignoring component scaling.
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		void GetUnscaledCapsuleSize(float& OutRadius, float& OutHalfHeight) const;

	// Get the scale used by this shape. This is a uniform scale that is the minimum of any non-uniform scaling.
	// @return the scale used by this shape.
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		float GetShapeScale() const;

	// Sets the capsule size without triggering a render or physics update. This is the preferred method when initializing a component in a class constructor.
	FORCEINLINE void InitCapsuleSize(float InRadius, float InHalfHeight)
	{
		CapsuleRadius = FMath::Max(0.f, InRadius);
		CapsuleHalfHeight = FMath::Max3(0.f, InHalfHeight, InRadius);
	}
};


// ----------------- INLINES ---------------

FORCEINLINE void UVRRootComponent::SetCapsuleRadius(float Radius, bool bUpdateOverlaps)
{
	SetCapsuleSize(Radius, GetUnscaledCapsuleHalfHeight(), bUpdateOverlaps);
}

FORCEINLINE void UVRRootComponent::SetCapsuleHalfHeight(float HalfHeight, bool bUpdateOverlaps)
{
	SetCapsuleSize(GetUnscaledCapsuleRadius(), HalfHeight, bUpdateOverlaps);
}

FORCEINLINE float UVRRootComponent::GetScaledCapsuleRadius() const
{
	return CapsuleRadius * GetShapeScale();
}

FORCEINLINE float UVRRootComponent::GetScaledCapsuleHalfHeight() const
{
	return CapsuleHalfHeight * GetShapeScale();
}

FORCEINLINE void UVRRootComponent::GetScaledCapsuleSize(float& OutRadius, float& OutHalfHeight) const
{
	const float Scale = GetShapeScale();
	OutRadius = CapsuleRadius * Scale;
	OutHalfHeight = CapsuleHalfHeight * Scale;
}


FORCEINLINE float UVRRootComponent::GetUnscaledCapsuleRadius() const
{
	return CapsuleRadius;
}

FORCEINLINE float UVRRootComponent::GetUnscaledCapsuleHalfHeight() const
{
	return CapsuleHalfHeight;
}

FORCEINLINE void UVRRootComponent::GetUnscaledCapsuleSize(float& OutRadius, float& OutHalfHeight) const
{
	OutRadius = CapsuleRadius;
	OutHalfHeight = CapsuleHalfHeight;
}

FORCEINLINE float UVRRootComponent::GetShapeScale() const
{
	return ComponentToWorld.GetMinimumAxisScale();
}