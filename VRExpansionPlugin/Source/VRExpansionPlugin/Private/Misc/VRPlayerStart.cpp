// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/VRPlayerStart.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VRPlayerStart)

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/BillboardComponent.h"


AVRPlayerStart::AVRPlayerStart(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VRRootComp = CreateDefaultSubobject<USceneComponent>(TEXT("VRRootComp"));
	VRRootComp->Mobility = EComponentMobility::Static;
	RootComponent = VRRootComp;

	UCapsuleComponent * CapsuleComp = GetCapsuleComponent();
	if (CapsuleComp && VRRootComp)
	{
		CapsuleComp->SetupAttachment(VRRootComp);
		CapsuleComp->SetRelativeLocation(FVector(0.f,0.f,CapsuleComp->GetScaledCapsuleHalfHeight()));
	}
}

void AVRPlayerStart::GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const
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
void AVRPlayerStart::FindBase()
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
void AVRPlayerStart::Validate()
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