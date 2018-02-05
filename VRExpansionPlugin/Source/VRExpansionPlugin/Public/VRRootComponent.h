// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "Components/ShapeComponent.h"
#include "VRTrackedParentInterface.h"
#include "VRBaseCharacter.h"
#include "VRExpansionFunctionLibrary.h"
#include "VRRootComponent.generated.h"

//For UE4 Profiler ~ Stat Group
//DECLARE_STATS_GROUP(TEXT("VRPhysicsUpdate"), STATGROUP_VRPhysics, STATCAT_Advanced);

//class AVRBaseCharacter;

DECLARE_STATS_GROUP(TEXT("VRRootComponent"), STATGROUP_VRRootComponent, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("VR Root Set Half Height"), STAT_VRRootSetHalfHeight, STATGROUP_VRRootComponent);
DECLARE_CYCLE_STAT(TEXT("VR Root Set Capsule Size"), STAT_VRRootSetCapsuleSize, STATGROUP_VRRootComponent);

/**
* A capsule component that repositions its physics scene and rendering location to the camera/HMD's relative position.
* Generally not to be used by itself unless on a base Pawn and not a character, the VRCharacter has been highly customized to correctly support it.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UVRRootComponent : public UCapsuleComponent, public IVRTrackedParentInterface
{
	GENERATED_UCLASS_BODY()

public:
	friend class FDrawCylinderSceneProxy;

	FORCEINLINE void GenerateOffsetToWorld(bool bUpdateBounds = true, bool bGetPureYaw = true);

	// If valid will use this as the tracked parent instead of the HMD / Parent
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRTrackedParentInterface")
		FBPVRWaistTracking_Info OptionalWaistTrackingParent;

	virtual void SetTrackedParent(UPrimitiveComponent * NewParentComponent, float WaistRadius, EBPVRWaistTrackingMode WaistTrackingMode) override
	{
		IVRTrackedParentInterface::Default_SetTrackedParent_Impl(NewParentComponent, WaistRadius, WaistTrackingMode, OptionalWaistTrackingParent, this);
	}

	/**
	* This is overidden for the VR Character to re-set physics location
	* Change the capsule size. This is the unscaled size, before component scale is applied.
	* @param	InRadius : radius of end-cap hemispheres and center cylinder.
	* @param	InHalfHeight : half-height, from capsule center to end of top or bottom hemisphere.
	* @param	bUpdateOverlaps: if true and this shape is registered and collides, updates touching array for owner actor.
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		virtual void SetCapsuleSizeVR(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps = true);

	// Used to update the capsule half height and calculate the new offset value for VR
	UFUNCTION(BlueprintCallable, Category = "Components|Capsule")
		void SetCapsuleHalfHeightVR(float HalfHeight, bool bUpdateOverlaps = true);

protected:
	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags, ETeleportType Teleport = ETeleportType::None) override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;

	void SendPhysicsTransform(ETeleportType Teleport);

	virtual void UpdateOverlaps(TArray<FOverlapInfo> const* NewPendingOverlaps = nullptr, bool bDoNotifies = true, const TArray<FOverlapInfo>* OverlapsAtEndLocation = nullptr) override;

	const TArray<FOverlapInfo>* ConvertRotationOverlapsToCurrentOverlaps(TArray<FOverlapInfo>& OverlapsAtEndLocation, const TArray<FOverlapInfo>& CurrentOverlaps);
	const TArray<FOverlapInfo>* ConvertSweptOverlapsToCurrentOverlaps(
	TArray<FOverlapInfo>& OverlapsAtEndLocation, const TArray<FOverlapInfo>& SweptOverlaps, int32 SweptOverlapsIndex,
	const FVector& EndLocation, const FQuat& EndRotationQuat);

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

	//UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
	AVRBaseCharacter * owningVRChar;

	//UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
	//UCapsuleComponent * VRCameraCollider;

	FVector DifferenceFromLastFrame;
	//UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
	FTransform OffsetComponentToWorld;

	// Used to offset the collision (IE backwards from the player slightly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	FVector VRCapsuleOffset;

	// #TODO: See if making this multiplayer compatible is viable
	// Offsets capsule to be centered on HMD - currently NOT multiplayer compatible
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bCenterCapsuleOnHMD;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bUseWalkingCollisionOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	TEnumAsByte<ECollisionChannel> WalkingCollisionOverride;

	/*ECollisionChannel GetVRCollisionObjectType()
	{
		if (bUseWalkingCollisionOverride)
			return WalkingCollisionOverride;
		else
			return GetCollisionObjectType();
	}*/

	FVector curCameraLoc;
	FRotator curCameraRot;
	FRotator StoredCameraRotOffset;

	FVector lastCameraLoc;
	FRotator lastCameraRot;

	// While misnamed, is true if we collided with a wall/obstacle due to the HMDs movement in this frame (not movement components)
	UPROPERTY(BlueprintReadOnly, Category = "VRExpansionLibrary")
	bool bHadRelativeMovement;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

public:
	// Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditChange(UProperty* PropertyThatWillChange);
#endif // WITH_EDITOR
	// End UObject interface

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	private:
		friend class FVRCharacterScopedMovementUpdate;
};


// Have to declare inlines here for blueprint
void inline UVRRootComponent::GenerateOffsetToWorld(bool bUpdateBounds, bool bGetPureYaw)
{
	FRotator CamRotOffset;

	if (bGetPureYaw)
		CamRotOffset = StoredCameraRotOffset;//UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curCameraRot);
	else
		CamRotOffset = curCameraRot;


	OffsetComponentToWorld = FTransform(CamRotOffset.Quaternion(), FVector(curCameraLoc.X, curCameraLoc.Y, bCenterCapsuleOnHMD ? curCameraLoc.Z : CapsuleHalfHeight) + CamRotOffset.RotateVector(VRCapsuleOffset), FVector(1.0f)) * GetComponentTransform();

	if (owningVRChar)
	{
		owningVRChar->OffsetComponentToWorld = OffsetComponentToWorld;
	}

	if (bUpdateBounds)
		UpdateBounds();
}


FORCEINLINE void UVRRootComponent::SetCapsuleHalfHeightVR(float HalfHeight, bool bUpdateOverlaps)
{
	SCOPE_CYCLE_COUNTER(STAT_VRRootSetHalfHeight);

	if (FMath::IsNearlyEqual(HalfHeight, CapsuleHalfHeight))
	{
		return;
	}

	SetCapsuleSizeVR(GetUnscaledCapsuleRadius(), HalfHeight, bUpdateOverlaps);
}

FORCEINLINE void UVRRootComponent::SetCapsuleSizeVR(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps)
{
	SCOPE_CYCLE_COUNTER(STAT_VRRootSetCapsuleSize);

	if (FMath::IsNearlyEqual(NewRadius, CapsuleRadius) && FMath::IsNearlyEqual(NewHalfHeight, CapsuleHalfHeight))
	{
		return;
	}

	CapsuleHalfHeight = FMath::Max3(0.f, NewHalfHeight, NewRadius);
	CapsuleRadius = FMath::Max(0.f, NewRadius);
	UpdateBounds();
	UpdateBodySetup();
	MarkRenderStateDirty();
	GenerateOffsetToWorld();

	// do this if already created
	// otherwise, it hasn't been really created yet
	if (bPhysicsStateCreated)
	{
		// Update physics engine collision shapes
		BodyInstance.UpdateBodyScale(GetComponentTransform().GetScale3D(), true);

		if (bUpdateOverlaps && IsCollisionEnabled() && GetOwner())
		{
			UpdateOverlaps();
		}
	}
}