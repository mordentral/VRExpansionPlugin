// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"
#include "VRRootComponent.h"

//For UE4 Profiler ~ Stat
//DECLARE_CYCLE_STAT(TEXT("VR Create Physics Meshes"), STAT_CreatePhysicsMeshes, STATGROUP_VRPhysics);

/** Represents a UVRRootComponent to the scene manager. */
class FDrawCylinderSceneProxy : public FPrimitiveSceneProxy
{
public:
	FDrawCylinderSceneProxy(const UVRRootComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
		, bDrawOnlyIfSelected(InComponent->bDrawOnlyIfSelected)
		, CapsuleRadius(InComponent->CapsuleRadius)
		, CapsuleHalfHeight(InComponent->CapsuleHalfHeight)
		, ShapeColor(InComponent->ShapeColor)
		, AutoSizeCapsuleOffset(InComponent->AutoSizeCapsuleOffset)
		, OffsetComponentToWorld(InComponent->OffsetComponentToWorld)
	{
		bWillEverBeLit = false;
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_GetDynamicMeshElements_DrawDynamicElements);

		const FMatrix& LocalToWorld = OffsetComponentToWorld.ToMatrixWithScale();//GetLocalToWorld();
		const int32 CapsuleSides = FMath::Clamp<int32>(CapsuleRadius / 4.f, 16, 64);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{

			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				const FLinearColor DrawCapsuleColor = GetViewSelectionColor(ShapeColor, *View, IsSelected(), IsHovered(), false, IsIndividuallySelected());

				// This is a quick hack and doesn't really show the location, it only works in viewport where the character has no rotation
				FVector Base = LocalToWorld.GetOrigin();
				//FVector Offset = AutoSizeCapsuleOffset;
				//Offset.X += LocationOffset.X;
				//Offset.Y += LocationOffset.Y;

				//Base += LocalToWorld.TransformVector(Offset);
				//Base.Z += CapsuleHalfHeight;

				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				DrawWireCapsule(PDI, Base, LocalToWorld.GetScaledAxis(EAxis::X), LocalToWorld.GetScaledAxis(EAxis::Y), LocalToWorld.GetScaledAxis(EAxis::Z), DrawCapsuleColor, CapsuleRadius, CapsuleHalfHeight, CapsuleSides, SDPG_World, 1.25f);
			}
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateTransform_RenderThread(FTransform NewTransform, float NewHalfHeight)
	{
		check(IsInRenderingThread());
		OffsetComponentToWorld = NewTransform;
		CapsuleHalfHeight = NewHalfHeight;
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		const bool bVisible = !bDrawOnlyIfSelected || IsSelected();
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View) && bVisible;
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
		return Result;
	}
	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }
	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

private:
	const uint32	bDrawOnlyIfSelected : 1;
	const float		CapsuleRadius;
	float		CapsuleHalfHeight;
	FColor	ShapeColor;
	const FVector AutoSizeCapsuleOffset;
	FTransform OffsetComponentToWorld;
};


UVRRootComponent::UVRRootComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);

	//ShapeBodySetup = NULL;

	bAutoSizeCapsuleHeight = false;
	AutoSizeCapsuleOffset = FVector(-5.0f, 0.0f, 0.0f);
	AutoCapsuleUpdateRate = 60;
	AutoCapsuleUpdateCount = 0.0f;
	bAutoCapsuleUpdateEveryFrame = false;

	ShapeColor = FColor(223, 149, 157, 255);

	CapsuleRadius = 9.0f;
	CapsuleHalfHeight = 9.0f;
	bUseEditorCompositing = true;

	curCameraRot = FQuat(0.0f, 0.0f, 0.0f, 1.0f);// = FRotator::ZeroRotator;
	curCameraLoc = FVector(0.0f, 0.0f, 0.0f);//FVector::ZeroVector;
	TargetPrimitiveComponent = NULL;
}

/*FBodyInstance* UVRRootComponent::GetBodyInstance(FName BoneName, bool bGetWelded) const
{
//	if (TargetPrimitiveComponent)
	//	return TargetPrimitiveComponent->GetBodyInstance();
	//else
	//	return Super::GetBodyInstance(BoneName, bGetWelded);
}*/

class UBodySetup* UVRRootComponent::GetBodySetup()
{
	UpdateBodySetup();
	return ShapeBodySetup;
}

FPrimitiveSceneProxy* UVRRootComponent::CreateSceneProxy()
{

	return new FDrawCylinderSceneProxy(this);
}

void UVRRootComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{		
	//SCOPE_CYCLE_COUNTER(STAT_CreatePhysicsMeshes);

	bool bUpdate = bAutoCapsuleUpdateEveryFrame;

	if (!bAutoCapsuleUpdateEveryFrame)
	{
		AutoCapsuleUpdateCount += DeltaTime;

		if (AutoCapsuleUpdateCount >= (1.0f / AutoCapsuleUpdateRate))
		{
			AutoCapsuleUpdateCount = 0.0f;

			bUpdate = true;
		}
	}

	if (bUpdate)
	{
		if (GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed())
		{
			GEngine->HMDDevice->GetCurrentOrientationAndPosition(curCameraRot, curCameraLoc);
		}
		else
		{
			
			curCameraRot = FQuat(0.0f, 0.0f, 0.0f, 1.0f);// = FRotator::ZeroRotator;
			curCameraLoc = FVector(0.0f, 0.0f, 160.0f);//FVector::ZeroVector;
		}

	//	if(bAutoSizeCapsuleHeight)
		//	CapsuleHalfHeight = FMath::Clamp(curCameraLoc.Z / 2, CapsuleRadius, 300.0f);

		OffsetComponentToWorld = FTransform(FQuat(0,0,0,1), curCameraLoc + AutoSizeCapsuleOffset /*+ FVector(0, 0, -CapsuleHalfHeight)*/, FVector(1.0f)) * ComponentToWorld;
		FTransform OffsetComponentToWorld2 = FTransform(FQuat(0, 0, 0, 1), curCameraLoc + AutoSizeCapsuleOffset, FVector(1.0f)) * ComponentToWorld;
		

		//UpdateBounds();

	//	FBodyInstance * bodyInstance = GetBodyInstance();
	//	bodyInstance->SetBodyTransform(OffsetComponentToWorld2, ETeleportType::TeleportPhysics);
		//BodyInstance.UpdateBodyScale(OffsetComponentToWorld.GetScale3D());

		//UpdateBodySetup();

		//RecreatePhysicsState();


		if (this->ShouldRender() && this->SceneProxy)
		{
			ENQUEUE_UNIQUE_RENDER_COMMAND_THREEPARAMETER(
				FDrawCylinderTransformUpdate,
				FDrawCylinderSceneProxy*, CylinderSceneProxy, (FDrawCylinderSceneProxy*)SceneProxy,
				FTransform, OffsetComponentToWorld, OffsetComponentToWorld, float, CapsuleHalfHeight, CapsuleHalfHeight,
				{
					CylinderSceneProxy->UpdateTransform_RenderThread(OffsetComponentToWorld, CapsuleHalfHeight);
				}
			);
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UVRRootComponent::SendPhysicsTransform(ETeleportType Teleport)
{
	BodyInstance.SetBodyTransform(OffsetComponentToWorld, Teleport);
	BodyInstance.UpdateBodyScale(OffsetComponentToWorld.GetScale3D());
}

void UVRRootComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	// Don't want to call primitives version, and the scenecomponents version does nothing
	//Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

	// Always send new transform to physics
	if (bPhysicsStateCreated && !(UpdateTransformFlags & EUpdateTransformFlags::SkipPhysicsUpdate))
	{
		//If we update transform of welded bodies directly (i.e. on the actual component) we need to update the shape transforms of the parent.
		//If the parent is updated, any welded shapes are automatically updated so we don't need to do this physx update.
		//If the parent is updated and we are NOT welded, the child still needs to update physx
		const bool bTransformSetDirectly = !(UpdateTransformFlags & EUpdateTransformFlags::PropagateFromParent);
		if (bTransformSetDirectly || !IsWelded())
		{
			SendPhysicsTransform(Teleport);
		}
	}
}

FBoxSphereBounds UVRRootComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FVector BoxPoint = FVector(CapsuleRadius, CapsuleRadius, CapsuleHalfHeight);
	//return FBoxSphereBounds(FVector::ZeroVector, BoxPoint, BoxPoint.Size()).TransformBy(LocalToWorld);
	return FBoxSphereBounds(/*FVector::ZeroVectorFVector*/FVector(curCameraLoc.X,curCameraLoc.Y,/*CapsuleHalfHeight*/curCameraLoc.Z /*- CapsuleHalfHeight*/) + AutoSizeCapsuleOffset, BoxPoint, BoxPoint.Size()).TransformBy(LocalToWorld);
}

void UVRRootComponent::CalcBoundingCylinder(float& CylinderRadius, float& CylinderHalfHeight) const
{
	const float Scale = ComponentToWorld.GetMaximumAxisScale();
	const float CapsuleEndCapCenter = FMath::Max(CapsuleHalfHeight - CapsuleRadius, 0.f);
	const FVector ZAxis = ComponentToWorld.TransformVectorNoScale(FVector(0.f, 0.f, CapsuleEndCapCenter * Scale));

	const float ScaledRadius = CapsuleRadius * Scale;

	CylinderRadius = ScaledRadius + FMath::Sqrt(FMath::Square(ZAxis.X) + FMath::Square(ZAxis.Y));
	CylinderHalfHeight = ScaledRadius + ZAxis.Z;
}

void UVRRootComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading() && (Ar.UE4Ver() < VER_UE4_AFTER_CAPSULE_HALF_HEIGHT_CHANGE))
	{
		if ((CapsuleHeight_DEPRECATED != 0.0f) || (Ar.UE4Ver() < VER_UE4_BLUEPRINT_VARS_NOT_READ_ONLY))
		{
			CapsuleHalfHeight = CapsuleHeight_DEPRECATED;
			CapsuleHeight_DEPRECATED = 0.0f;
		}
	}

	CapsuleHalfHeight = FMath::Max3(0.f, CapsuleHalfHeight, CapsuleRadius);
}

#if WITH_EDITOR
void UVRRootComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// We only want to modify the property that was changed at this point
	// things like propagation from CDO to instances don't work correctly if changing one property causes a different property to change
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UVRRootComponent, CapsuleHalfHeight))
	{
		CapsuleHalfHeight = FMath::Max3(0.f, CapsuleHalfHeight, CapsuleRadius);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UVRRootComponent, CapsuleRadius))
	{
		CapsuleRadius = FMath::Clamp(CapsuleRadius, 0.f, CapsuleHalfHeight);
	}

	if (!IsTemplate())
	{
		UpdateBodySetup(); // do this before reregistering components so that new values are used for collision
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif	// WITH_EDITOR

void UVRRootComponent::SetCapsuleSize(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps)
{
	CapsuleHalfHeight = FMath::Max3(0.f, NewHalfHeight, NewRadius);
	CapsuleRadius = FMath::Max(0.f, NewRadius);
	MarkRenderStateDirty();

	// do this if already created
	// otherwise, it hasn't been really created yet
	if (bPhysicsStateCreated)
	{
		//if (ShapeBodySetup != NULL)
		//	ShapeBodySetup->InvalidatePhysicsData();
		DestroyPhysicsState(); // Faster without
		UpdateBodySetup();
		CreatePhysicsState(); // Faster without
		//ShapeBodySetup->CreatePhysicsMeshes();
		//RecreatePhysicsState();

		if (bUpdateOverlaps && IsCollisionEnabled() && GetOwner())
		{
			UpdateOverlaps();
		}
	}
}

void UVRRootComponent::UpdateBodySetup()
{
	if (ShapeBodySetup == NULL || ShapeBodySetup->IsPendingKill())
	{
		ShapeBodySetup = NewObject<UBodySetup>(this);
		ShapeBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		ShapeBodySetup->AggGeom.SphylElems.Add(FKSphylElem());
	}

	check(ShapeBodySetup->AggGeom.SphylElems.Num() == 1);
	FKSphylElem* SE = ShapeBodySetup->AggGeom.SphylElems.GetData();

	SE->SetTransform(FTransform(FQuat(0,0,0,1), FVector(curCameraLoc.X,curCameraLoc.Y,curCameraLoc.Z /*- CapsuleHalfHeight*/) + AutoSizeCapsuleOffset, FVector(1.0f)));
	//SE->SetTransform(FTransform::Identity);
	SE->Radius = CapsuleRadius;
	SE->Length = 2 * FMath::Max(CapsuleHalfHeight - CapsuleRadius, 0.f);	//SphylElem uses height from center of capsule spheres, but UVRRootComponent uses halfHeight from end of the sphere
}

bool UVRRootComponent::IsZeroExtent() const
{
	return (CapsuleRadius == 0.f) && (CapsuleHalfHeight == 0.f);
}


FCollisionShape UVRRootComponent::GetCollisionShape(float Inflation) const
{
	const float ShapeScale = GetShapeScale();
	const float Radius = FMath::Max(0.f, (CapsuleRadius * ShapeScale) + Inflation);
	const float HalfHeight = FMath::Max(0.f, (CapsuleHalfHeight * ShapeScale) + Inflation);
	return FCollisionShape::MakeCapsule(Radius, HalfHeight);
}

bool UVRRootComponent::AreSymmetricRotations(const FQuat& A, const FQuat& B, const FVector& Scale3D) const
{
	if (Scale3D.X != Scale3D.Y)
	{
		return false;
	}

	const FVector AUp = A.GetAxisZ();
	const FVector BUp = B.GetAxisZ();
	return AUp.Equals(BUp);
}


/*
bool UVRRootComponent::MoveComponentImpl(const FVector& Delta, const FQuat& NewRotationQuat, bool bSweep, FHitResult* OutHit, EMoveComponentFlags MoveFlags, ETeleportType Teleport)
{
	if (IsPendingKill() || (this->Mobility == EComponentMobility::Static && IsRegistered()))//|| CheckStaticMobilityAndWarn(PrimitiveComponentStatics::MobilityWarnText))
	{
		if (OutHit)
		{
			*OutHit = FHitResult();
		}
		return false;
	}

	ConditionalUpdateComponentToWorld();

	// Init HitResult
	FHitResult BlockingHit(1.f);
	const FVector TraceStart = OffsetComponentToWorld.GetLocation();//GetComponentLocation();
	const FVector TraceEnd = TraceStart + Delta;
	BlockingHit.TraceStart = TraceStart;
	BlockingHit.TraceEnd = TraceEnd;

	// Update the location.  This will teleport any child components as well (not sweep).
	bool bMoved = InternalSetWorldLocationAndRotation(NewLocation, NewRotationQuat, bSkipPhysicsMove, Teleport);

	return bMoved;
}*/

	/*
	SCOPE_CYCLE_COUNTER(STAT_MoveComponentTime);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && PERF_MOVECOMPONENT_STATS
	FScopedMoveCompTimer MoveTimer(this, Delta);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && PERF_MOVECOMPONENT_STATS

#if defined(PERF_SHOW_MOVECOMPONENT_TAKING_LONG_TIME) || LOOKING_FOR_PERF_ISSUES
	uint32 MoveCompTakingLongTime = 0;
	CLOCK_CYCLES(MoveCompTakingLongTime);
#endif

	// static things can move before they are registered (e.g. immediately after streaming), but not after.
	if (IsPendingKill() || (this->Mobility == EComponentMobility::Static && IsRegistered()))//|| CheckStaticMobilityAndWarn(PrimitiveComponentStatics::MobilityWarnText))
	{
		if (OutHit)
		{
			*OutHit = FHitResult();
		}
		return false;
	}

	ConditionalUpdateComponentToWorld();

	// Init HitResult
	FHitResult BlockingHit(1.f);
	const FVector TraceStart = GetComponentLocation();
	const FVector TraceEnd = TraceStart + Delta;
	BlockingHit.TraceStart = TraceStart;
	BlockingHit.TraceEnd = TraceEnd;

	// Set up.
	float DeltaSizeSq = Delta.SizeSquared();
	const FQuat InitialRotationQuat = ComponentToWorld.GetRotation();

	// ComponentSweepMulti does nothing if moving < KINDA_SMALL_NUMBER in distance, so it's important to not try to sweep distances smaller than that. 
	const float MinMovementDistSq = (bSweep ? FMath::Square(4.f*KINDA_SMALL_NUMBER) : 0.f);
	if (DeltaSizeSq <= MinMovementDistSq)
	{
		// Skip if no vector or rotation.
		if (NewRotationQuat.Equals(InitialRotationQuat, SCENECOMPONENT_QUAT_TOLERANCE))
		{
			// copy to optional output param
			if (OutHit)
			{
				*OutHit = BlockingHit;
			}
			return true;
		}
		DeltaSizeSq = 0.f;
	}

	const bool bSkipPhysicsMove = ((MoveFlags & MOVECOMP_SkipPhysicsMove) != MOVECOMP_NoFlags);

	bool bMoved = false;
	bool bIncludesOverlapsAtEnd = false;
	bool bRotationOnly = false;
	TArray<FOverlapInfo> PendingOverlaps;
	AActor* const Actor = GetOwner();

	if (!bSweep)
	{
		// not sweeping, just go directly to the new transform
		bMoved = InternalSetWorldLocationAndRotation(TraceEnd, NewRotationQuat, bSkipPhysicsMove, Teleport);
		bRotationOnly = (DeltaSizeSq == 0);
		bIncludesOverlapsAtEnd = bRotationOnly && (AreSymmetricRotations(InitialRotationQuat, NewRotationQuat, GetComponentScale())) && IsCollisionEnabled();
	}
	else
	{
		TArray<FHitResult> Hits;
		FVector NewLocation = TraceStart;

		// Perform movement collision checking if needed for this actor.
		const bool bCollisionEnabled = IsCollisionEnabled();
		if (bCollisionEnabled && (DeltaSizeSq > 0.f))
		{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if (!IsRegistered())
			{
				if (Actor)
				{
				//	UE_LOG(LogPrimitiveComponent, Fatal, TEXT("%s MovedComponent %s not initialized deleteme %d"), *Actor->GetName(), *GetName(), Actor->IsPendingKill());
				}
				else
				{
				//	UE_LOG(LogPrimitiveComponent, Fatal, TEXT("MovedComponent %s not initialized"), *GetFullName());
				}
			}
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && PERF_MOVECOMPONENT_STATS
			MoveTimer.bDidLineCheck = true;
#endif 
			UWorld* const MyWorld = GetWorld();

			FComponentQueryParams Params(PrimitiveComponentStatics::MoveComponentName, Actor);
			FCollisionResponseParams ResponseParam;
			InitSweepCollisionParams(Params, ResponseParam);
			bool const bHadBlockingHit = MyWorld->ComponentSweepMulti(Hits, this, TraceStart, TraceEnd, InitialRotationQuat, Params);

			if (Hits.Num() > 0)
			{
				const float DeltaSize = FMath::Sqrt(DeltaSizeSq);
				for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
				{
					PullBackHit(Hits[HitIdx], TraceStart, TraceEnd, DeltaSize);
				}
			}

			// If we had a valid blocking hit, store it.
			// If we are looking for overlaps, store those as well.
			uint32 FirstNonInitialOverlapIdx = INDEX_NONE;
			if (bHadBlockingHit || bGenerateOverlapEvents)
			{
				int32 BlockingHitIndex = INDEX_NONE;
				float BlockingHitNormalDotDelta = BIG_NUMBER;
				for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
				{
					const FHitResult& TestHit = Hits[HitIdx];

					if (TestHit.bBlockingHit)
					{
						if (!ShouldIgnoreHitResult(MyWorld, TestHit, Delta, Actor, MoveFlags))
						{
							if (TestHit.Time == 0.f)
							{
								// We may have multiple initial hits, and want to choose the one with the normal most opposed to our movement.
								const float NormalDotDelta = (TestHit.ImpactNormal | Delta);
								if (NormalDotDelta < BlockingHitNormalDotDelta)
								{
									BlockingHitNormalDotDelta = NormalDotDelta;
									BlockingHitIndex = HitIdx;
								}
							}
							else if (BlockingHitIndex == INDEX_NONE)
							{
								// First non-overlapping blocking hit should be used, if an overlapping hit was not.
								// This should be the only non-overlapping blocking hit, and last in the results.
								BlockingHitIndex = HitIdx;
								break;
							}
						}
					}
					else if (bGenerateOverlapEvents)
					{
						UPrimitiveComponent* OverlapComponent = TestHit.Component.Get();
						if (OverlapComponent && OverlapComponent->bGenerateOverlapEvents)
						{
							if (!ShouldIgnoreOverlapResult(MyWorld, Actor, *this, TestHit.GetActor(), *OverlapComponent))
							{
								// don't process touch events after initial blocking hits
								if (BlockingHitIndex >= 0 && TestHit.Time > Hits[BlockingHitIndex].Time)
								{
									break;
								}

								if (FirstNonInitialOverlapIdx == INDEX_NONE && TestHit.Time > 0.f)
								{
									// We are about to add the first non-initial overlap.
									FirstNonInitialOverlapIdx = PendingOverlaps.Num();
								}

								// cache touches
								PendingOverlaps.AddUnique(FOverlapInfo(TestHit));
							}
						}
					}
				}

				// Update blocking hit, if there was a valid one.
				if (BlockingHitIndex >= 0)
				{
					BlockingHit = Hits[BlockingHitIndex];
				}
			}

			// Update NewLocation based on the hit result
			if (!BlockingHit.bBlockingHit)
			{
				NewLocation = TraceEnd;
			}
			else
			{
				NewLocation = TraceStart + (BlockingHit.Time * (TraceEnd - TraceStart));

				// Sanity check
				const FVector ToNewLocation = (NewLocation - TraceStart);
				if (ToNewLocation.SizeSquared() <= MinMovementDistSq)
				{
					// We don't want really small movements to put us on or inside a surface.
					NewLocation = TraceStart;
					BlockingHit.Time = 0.f;

					// Remove any pending overlaps after this point, we are not going as far as we swept.
					if (FirstNonInitialOverlapIdx != INDEX_NONE)
					{
						PendingOverlaps.SetNum(FirstNonInitialOverlapIdx);
					}
				}
			}

			bIncludesOverlapsAtEnd = AreSymmetricRotations(InitialRotationQuat, NewRotationQuat, GetComponentScale());

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			if ((BlockingHit.Time < 1.f) && !IsZeroExtent())
			{
				// this is sole debug purpose to find how capsule trace information was when hit 
				// to resolve stuck or improve our movement system - To turn this on, use DebugCapsuleSweepPawn
				APawn const* const ActorPawn = (Actor ? Cast<APawn>(Actor) : NULL);
				if (ActorPawn && ActorPawn->Controller && ActorPawn->Controller->IsLocalPlayerController())
				{
					APlayerController const* const PC = CastChecked<APlayerController>(ActorPawn->Controller);
					if (PC->CheatManager && PC->CheatManager->bDebugCapsuleSweepPawn)
					{
						FVector CylExtent = ActorPawn->GetSimpleCollisionCylinderExtent()*FVector(1.001f, 1.001f, 1.0f);
						FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CylExtent);
						PC->CheatManager->AddCapsuleSweepDebugInfo(TraceStart, TraceEnd, BlockingHit.ImpactPoint, BlockingHit.Normal, BlockingHit.ImpactNormal, BlockingHit.Location, CapsuleShape.GetCapsuleHalfHeight(), CapsuleShape.GetCapsuleRadius(), true, (BlockingHit.bStartPenetrating && BlockingHit.bBlockingHit) ? true : false);
					}
				}
			}
#endif
		}
		else if (DeltaSizeSq > 0.f)
		{
			// apply move delta even if components has collisions disabled
			NewLocation += Delta;
			bIncludesOverlapsAtEnd = false;
		}
		else if (DeltaSizeSq == 0.f && bCollisionEnabled)
		{
			bIncludesOverlapsAtEnd = AreSymmetricRotations(InitialRotationQuat, NewRotationQuat, GetComponentScale());
			bRotationOnly = true;
		}

		// Update the location.  This will teleport any child components as well (not sweep).
		bMoved = InternalSetWorldLocationAndRotation(NewLocation, NewRotationQuat, bSkipPhysicsMove, Teleport);
	}

	// Handle overlap notifications.
	if (bMoved)
	{
		if (IsDeferringMovementUpdates())
		{
			// Defer UpdateOverlaps until the scoped move ends.
			FScopedMovementUpdate* ScopedUpdate = GetCurrentScopedMovement();
			if (bRotationOnly && bIncludesOverlapsAtEnd)
			{
				ScopedUpdate->KeepCurrentOverlapsAfterRotation(bSweep);
			}
			else
			{
				ScopedUpdate->AppendOverlapsAfterMove(PendingOverlaps, bSweep, bIncludesOverlapsAtEnd);
			}
		}
		else
		{
			if (bIncludesOverlapsAtEnd)
			{
				TArray<FOverlapInfo> OverlapsAtEndLocation;
				const TArray<FOverlapInfo>* OverlapsAtEndLocationPtr = nullptr; // When non-null, used as optimization to avoid work in UpdateOverlaps.
				if (bRotationOnly)
				{
					OverlapsAtEndLocationPtr = ConvertRotationOverlapsToCurrentOverlaps(OverlapsAtEndLocation, GetOverlapInfos());
				}
				else
				{
					OverlapsAtEndLocationPtr = ConvertSweptOverlapsToCurrentOverlaps(OverlapsAtEndLocation, PendingOverlaps, 0, GetComponentLocation(), GetComponentQuat());
				}
				UpdateOverlaps(&PendingOverlaps, true, OverlapsAtEndLocationPtr);
			}
			else
			{
				UpdateOverlaps(&PendingOverlaps, true, nullptr);
			}
		}
	}

	// Handle blocking hit notifications. Avoid if pending kill (which could happen after overlaps).
	if (BlockingHit.bBlockingHit && !IsPendingKill())
	{
		if (IsDeferringMovementUpdates())
		{
			FScopedMovementUpdate* ScopedUpdate = GetCurrentScopedMovement();
			ScopedUpdate->AppendBlockingHitAfterMove(BlockingHit);
		}
		else
		{
			DispatchBlockingHit(*Actor, BlockingHit);
		}
	}

#if defined(PERF_SHOW_MOVECOMPONENT_TAKING_LONG_TIME) || LOOKING_FOR_PERF_ISSUES
	UNCLOCK_CYCLES(MoveCompTakingLongTime);
	const float MSec = FPlatformTime::ToMilliseconds(MoveCompTakingLongTime);
	if (MSec > PERF_SHOW_MOVECOMPONENT_TAKING_LONG_TIME_AMOUNT)
	{
		if (GetOwner())
		{
			UE_LOG(LogPrimitiveComponent, Log, TEXT("%10f executing MoveComponent for %s owned by %s"), MSec, *GetName(), *GetOwner()->GetFullName());
		}
		else
		{
			UE_LOG(LogPrimitiveComponent, Log, TEXT("%10f executing MoveComponent for %s"), MSec, *GetFullName());
		}
	}
#endif

	// copy to optional output param
	if (OutHit)
	{
		*OutHit = BlockingHit;
	}

	// Return whether we moved at all.
	return bMoved;
}

const TArray<FOverlapInfo>* UVRRootComponent::ConvertSweptOverlapsToCurrentOverlaps(
	TArray<FOverlapInfo>& OverlapsAtEndLocation, const TArray<FOverlapInfo>& SweptOverlaps, int32 SweptOverlapsIndex,
	const FVector& EndLocation, const FQuat& EndRotationQuat)
{
	checkSlow(SweptOverlapsIndex >= 0);

	const TArray<FOverlapInfo>* Result = nullptr;
	if (bGenerateOverlapEvents && CVarAllowCachedOverlaps->GetInt())
	{
		const AActor* Actor = GetOwner();
		if (Actor && Actor->GetRootComponent() == this)
		{
			// We know we are not overlapping any new components at the end location. Children are ignored here (see note below).
			if (bEnableFastOverlapCheck)
			{
				//SCOPE_CYCLE_COUNTER(STAT_MoveComponent_FastOverlap);

				// Check components we hit during the sweep, keep only those still overlapping
				const FCollisionQueryParams UnusedQueryParams;
				for (int32 Index = SweptOverlapsIndex; Index < SweptOverlaps.Num(); ++Index)
				{
					const FOverlapInfo& OtherOverlap = SweptOverlaps[Index];
					UPrimitiveComponent* OtherPrimitive = OtherOverlap.OverlapInfo.GetComponent();
					if (OtherPrimitive && OtherPrimitive->bGenerateOverlapEvents)
					{
						if (OtherPrimitive->bMultiBodyOverlap)
						{
							// Not handled yet. We could do it by checking every body explicitly and track each body index in the overlap test, but this seems like a rare need.
							return nullptr;
						}
						else if (ComponentOverlapComponent(OtherPrimitive, EndLocation, EndRotationQuat, UnusedQueryParams))
						{
							OverlapsAtEndLocation.Add(OtherOverlap);
						}
					}
				}

				// Note: we don't worry about adding any child components here, because they are not included in the sweep results.
				// Children test for their own overlaps after we update our own, and we ignore children in our own update.
				checkfSlow(OverlapsAtEndLocation.FindByPredicate(FPredicateOverlapHasSameActor(*Actor)) == nullptr,
					TEXT("Child overlaps should not be included in the SweptOverlaps() array in UPrimitiveComponent::ConvertSweptOverlapsToCurrentOverlaps()."));

				Result = &OverlapsAtEndLocation;
			}
			else
			{
				if (SweptOverlaps.Num() == 0 && AreAllCollideableDescendantsRelative())
				{
					// Add overlaps with components in this actor.
					GetOverlapsWithActor(Actor, OverlapsAtEndLocation);
					Result = &OverlapsAtEndLocation;
				}
			}
		}
	}

	return Result;
}


const TArray<FOverlapInfo>* UVRRootComponent::ConvertRotationOverlapsToCurrentOverlaps(TArray<FOverlapInfo>& OverlapsAtEndLocation, const TArray<FOverlapInfo>& CurrentOverlaps)
{
	const TArray<FOverlapInfo>* Result = nullptr;
	if (bGenerateOverlapEvents && CVarAllowCachedOverlaps->GetInt())
	{
		const AActor* Actor = GetOwner();
		if (Actor && Actor->GetRootComponent() == this)
		{
			if (bEnableFastOverlapCheck)
			{
				// Add all current overlaps that are not children. Children test for their own overlaps after we update our own, and we ignore children in our own update.
				OverlapsAtEndLocation = CurrentOverlaps.FilterByPredicate(FPredicateOverlapHasDifferentActor(*Actor));
				Result = &OverlapsAtEndLocation;
			}
		}
	}

	return Result;
}*/