#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/NavigationObjectBase.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BillboardComponent.h"
#include "VRPlayerStart.generated.h"

/**
*	A normal player start except I replaced the root component with a scene component so that the spawn
*	transform will match our VR characters.
*/
UCLASS(Blueprintable, ClassGroup = Common, hidecategories = Collision)
class VREXPANSIONPLUGIN_API AVRPlayerStart : public APlayerStart
{
	GENERATED_BODY()

private:
	UPROPERTY()
		class USceneComponent* VRRootComp;
public:

	AVRPlayerStart(const FObjectInitializer& ObjectInitializer);

	/** Returns VRRootComp subobject **/
	class USceneComponent* GetVRRootComponent() const { return VRRootComp; }

	// Override this to use capsule even if it isn't the root component
	virtual void GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const override
	{
		UCapsuleComponent * CapsuleComp = GetCapsuleComponent();
		if (CapsuleComp != nullptr && CapsuleComp->IsRegistered() && CapsuleComp->IsCollisionEnabled())
		{
			// Note: assuming vertical orientation
			CapsuleComp->GetScaledCapsuleSize(CollisionRadius, CollisionHalfHeight);
		}
		else
		{
			Super::GetSimpleCollisionCylinder(CollisionRadius, CollisionHalfHeight);
		}
	}

	void FindBase() override
	{
		if (GetWorld()->HasBegunPlay())
		{
			return;
		}

		if (ShouldBeBased())
		{
			UCapsuleComponent * CapsuleComp = GetCapsuleComponent();
			if (!CapsuleComp)
				return;

			// not using find base, because don't want to fail if LD has navigationpoint slightly interpenetrating floor
			FHitResult Hit(1.f);

			const float Radius = CapsuleComp->GetScaledCapsuleRadius();
			FVector const CollisionSlice(Radius, Radius, 1.f);

			// check for placement
			float ScaledHalfHeight = CapsuleComp->GetScaledCapsuleHalfHeight();
			const FVector TraceStart = GetActorLocation() + FVector(0.f, 0.f, ScaledHalfHeight);
			const FVector TraceEnd = GetActorLocation() - FVector(0.f, 0.f, 2.f * ScaledHalfHeight);

			GetWorld()->SweepSingleByObjectType(Hit, TraceStart, TraceEnd, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeBox(CollisionSlice), FCollisionQueryParams(SCENE_QUERY_STAT(NavFindBase), false));

			// @fixme, ensure object is on the navmesh?
			// 		if( Hit.Actor != NULL )
			// 		{
			// 			if (Hit.Normal.Z > Scout->WalkableFloorZ)
			// 			{
			// 				const FVector HitLocation = TraceStart + (TraceEnd - TraceStart) * Hit.Time;
			// 				TeleportTo(HitLocation + FVector(0.f,0.f,CapsuleComponent->GetScaledCapsuleHalfHeight()-2.f), GetActorRotation(), false, true);
			// 			}
			// 			else
			// 			{
			// 				Hit.Actor = NULL;
			// 			}
			// 		}

			if (GetGoodSprite())
			{
				GetGoodSprite()->SetVisibility(true);
			}
			if (GetBadSprite())
			{
				GetBadSprite()->SetVisibility(false);
			}
		}
	}


	void Validate() override
	{
		if (ShouldBeBased() && (GetGoodSprite() || GetBadSprite()))
		{
			UCapsuleComponent * CapsuleComp = GetCapsuleComponent();
			if (!CapsuleComp)
				return;

			FVector OrigLocation = GetActorLocation();
			const float Radius = CapsuleComp->GetScaledCapsuleRadius();
			FVector const Slice(Radius, Radius, 1.f);

			bool bResult = true;

			// Check for adjustment
			FHitResult Hit(ForceInit);
			float ScaledHalfHeight = CapsuleComp->GetScaledCapsuleHalfHeight();
			const FVector TraceStart = GetActorLocation() + FVector(0.f, 0.f, ScaledHalfHeight);
			const FVector TraceEnd = GetActorLocation() - FVector(0.f, 0.f, 4.f * ScaledHalfHeight);
			GetWorld()->SweepSingleByChannel(Hit, TraceStart, TraceEnd, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(Slice), FCollisionQueryParams(SCENE_QUERY_STAT(NavObjectBase_Validate), false, this));
			if (Hit.bBlockingHit)
			{
				const FVector HitLocation = TraceStart + (TraceEnd - TraceStart) * Hit.Time;
				FVector Dest = HitLocation - FVector(0.f, 0.f, /*CapsuleComponent->GetScaledCapsuleHalfHeight() -*/ 2.f);

				// Move actor (TEST ONLY) to see if navigation point moves
				TeleportTo(Dest, GetActorRotation(), false, true);

				// If only adjustment was down towards the floor, then it is a valid placement
				FVector NewLocation = GetActorLocation();
				bResult = (NewLocation.X == OrigLocation.X &&
					NewLocation.Y == OrigLocation.Y &&
					NewLocation.Z <= OrigLocation.Z);

				// Move actor back to original position
				TeleportTo(OrigLocation, GetActorRotation(), false, true);
			}

			// Update sprites by result
			if (GetGoodSprite())
			{
				GetGoodSprite()->SetVisibility(bResult);
			}
			if (GetBadSprite())
			{
				GetBadSprite()->SetVisibility(!bResult);
			}
		}

		// Force update of icon
		MarkComponentsRenderStateDirty();
	}
};