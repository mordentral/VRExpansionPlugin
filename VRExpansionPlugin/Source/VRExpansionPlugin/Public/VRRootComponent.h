// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
//#include "Engine/Engine.h"
//#include "Components/ShapeComponent.h"
#include "VRTrackedParentInterface.h"
#include "VRBaseCharacter.h"
#include "VRExpansionFunctionLibrary.h"
#include "GameFramework/PhysicsVolume.h"
#include "Components/CapsuleComponent.h"
#include "VRRootComponent.generated.h"

//For UE4 Profiler ~ Stat Group
//DECLARE_STATS_GROUP(TEXT("VRPhysicsUpdate"), STATGROUP_VRPhysics, STATCAT_Advanced);

//class AVRBaseCharacter;

DECLARE_LOG_CATEGORY_EXTERN(LogVRRootComponent, Log, All);

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
	GENERATED_BODY()

public:
	UVRRootComponent(const FObjectInitializer& ObjectInitializer);

	friend class FDrawCylinderSceneProxy;

	bool bCalledUpdateTransform;

	// Overriding this and applying the offset to world position for the elements
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

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

	inline void OnUpdateTransform_Public(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None)
	{
		OnUpdateTransform(UpdateTransformFlags, Teleport);
		if (bNavigationRelevant && bRegistered)
		{
			UpdateNavigationData();
			PostUpdateNavigationData();
		}
	}

	virtual void SetSimulatePhysics(bool bSimulate) override;

protected:
	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* OutHit = NULL, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags, ETeleportType Teleport = ETeleportType::None) override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;

	void SendPhysicsTransform(ETeleportType Teleport);
	virtual bool UpdateOverlapsImpl(const TOverlapArrayView* NewPendingOverlaps = nullptr, bool bDoNotifies = true, const TOverlapArrayView* OverlapsAtEndLocation = nullptr) override;

	/** Convert a set of overlaps from a symmetric change in rotation to a subset that includes only those at the end location (filling in OverlapsAtEndLocation). */
	template<typename AllocatorType>
	bool ConvertRotationOverlapsToCurrentOverlaps(TArray<FOverlapInfo, AllocatorType>& OutOverlapsAtEndLocation, const TOverlapArrayView& CurrentOverlaps);

	template<typename AllocatorType>
	bool GetOverlapsWithActor_Template(const AActor* Actor, TArray<FOverlapInfo, AllocatorType>& OutOverlaps) const;

	/** Convert a set of overlaps from a sweep to a subset that includes only those at the end location (filling in OverlapsAtEndLocation). */
	template<typename AllocatorType>
	bool ConvertSweptOverlapsToCurrentOverlaps(TArray<FOverlapInfo, AllocatorType>& OutOverlapsAtEndLocation, const TOverlapArrayView& SweptOverlaps, int32 SweptOverlapsIndex, const FVector& EndLocation, const FQuat& EndRotationQuat);


public:
	void BeginPlay() override;
	virtual void InitializeComponent() override;

	bool IsLocallyControlled() const;

	UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
		TObjectPtr<USceneComponent> TargetPrimitiveComponent;

	//UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
	TObjectPtr<AVRBaseCharacter> owningVRChar;

	//UPROPERTY(BlueprintReadWrite, Transient, Category = "VRExpansionLibrary")
	//UCapsuleComponent * VRCameraCollider;

	FVector DifferenceFromLastFrame;
	//UPROPERTY(BlueprintReadOnly, Transient, Category = "VRExpansionLibrary")
	FTransform OffsetComponentToWorld;

	// Used to offset the collision (IE backwards from the player slightly.
	// The default 2.15 Z offset is to account for floor hover from the character movement component.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	FVector VRCapsuleOffset;

	// If true we will stop tracking the camera / hmd until enabled again
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		bool bPauseTracking;

	UFUNCTION(BlueprintCallable, Category = "VRExpansionLibrary")
		void SetTrackingPaused(bool bPaused);

	// #TODO: Test with 100.f rounding to make sure it isn't noticable, currently that is what it is
	// If true will subtract the HMD's location from the position, useful for if the actors base is set to the HMD location always (simple character).
	/*UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera")
		bool bOffsetByHMD;
		*/



	// #TODO: See if making this multiplayer compatible is viable
	// Offsets capsule to be centered on HMD - currently NOT multiplayer compatible
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	bool bCenterCapsuleOnHMD;

	// Allows the root component to be blocked by simulating objects (default off due to sickness inducing stuttering).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		bool bAllowSimulatingCollision;

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

	virtual void UpdatePhysicsVolume(bool bTriggerNotifiers) override;

	inline bool AreWeOverlappingVolume(APhysicsVolume* V)
	{
		bool bInsideVolume = true;
		if (!V->bPhysicsOnContact)
		{
			FVector ClosestPoint(0.f);
			// If there is no primitive component as root we consider you inside the volume. This is odd, but the behavior 
			// has existed for a long time, so keeping it this way
			UPrimitiveComponent* RootPrimitive = Cast<UPrimitiveComponent>(V->GetRootComponent());
			if (RootPrimitive)
			{
				float DistToCollisionSqr = -1.f;
				if (RootPrimitive->GetSquaredDistanceToCollision(OffsetComponentToWorld.GetTranslation(), DistToCollisionSqr, ClosestPoint))
				{
					bInsideVolume = (DistToCollisionSqr == 0.f);
				}
				else
				{
					bInsideVolume = false;
				}
			}
		}

		return bInsideVolume;
	}

public:
	// Begin UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditChange(FProperty* PropertyThatWillChange);
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

	/*if(bOffsetByHMD)
	{
		OffsetComponentToWorld = FTransform(CamRotOffset.Quaternion(), FVector(0, 0, bCenterCapsuleOnHMD ? curCameraLoc.Z : CapsuleHalfHeight) + CamRotOffset.RotateVector(VRCapsuleOffset), FVector(1.0f)) * GetComponentTransform();
	}
	else*/
	{
		OffsetComponentToWorld = FTransform(CamRotOffset.Quaternion(), FVector(curCameraLoc.X, curCameraLoc.Y, bCenterCapsuleOnHMD ? curCameraLoc.Z : CapsuleHalfHeight) + CamRotOffset.RotateVector(VRCapsuleOffset), FVector(1.0f)) * GetComponentTransform();
	}

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

	// Make sure that our character parent updates its replicated var as well
	if (AVRBaseCharacter * BaseChar = Cast<AVRBaseCharacter>(GetOwner()))
	{
		if (GetNetMode() < ENetMode::NM_Client && BaseChar->VRReplicateCapsuleHeight)
			BaseChar->ReplicatedCapsuleHeight.CapsuleHeight = CapsuleHalfHeight;
	}

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