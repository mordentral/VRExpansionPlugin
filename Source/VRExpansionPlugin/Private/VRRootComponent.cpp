// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
//#include "Runtime/Engine/Private/EnginePrivate.h"

#include "PhysicsPublic.h"

#if WITH_PHYSX
#include "PhysXSupport.h"
#endif // WITH_PHYSX

#include "Components/PrimitiveComponent.h"

#include "VRRootComponent.h"

#define LOCTEXT_NAMESPACE "VRRootComponent"

static int32 bEnableFastOverlapCheck = 1;

static const auto CVarInitialOverlapTolerance = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("p.InitialOverlapTolerance"));


// LOOKING_FOR_PERF_ISSUES
#define PERF_MOVECOMPONENT_STATS 0

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
static const auto CVarShowInitialOverlaps = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("p.ShowInitialOverlaps"));
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

static const auto CVarAllowCachedOverlaps = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AllowCachedOverlaps"));

namespace PrimitiveComponentStatics
{
	static const FName MoveComponentName(TEXT("MoveComponent"));
}

// Predicate to determine if an overlap is with a certain AActor.
struct FPredicateOverlapHasSameActor
{
	FPredicateOverlapHasSameActor(const AActor& Owner)
		: MyOwner(Owner)
	{
	}

	bool operator() (const FOverlapInfo& Info)
	{
		return Info.OverlapInfo.Actor == &MyOwner;
	}

private:
	const AActor& MyOwner;
};

// Predicate to determine if an overlap is *NOT* with a certain AActor.
struct FPredicateOverlapHasDifferentActor
{
	FPredicateOverlapHasDifferentActor(const AActor& Owner)
		: MyOwner(Owner)
	{
	}

	bool operator() (const FOverlapInfo& Info)
	{
		return Info.OverlapInfo.Actor != &MyOwner;
	}

private:
	const AActor& MyOwner;
};


static void PullBackHit(FHitResult& Hit, const FVector& Start, const FVector& End, const float Dist)
{
	const float DesiredTimeBack = FMath::Clamp(0.1f, 0.1f / Dist, 1.f / Dist) + 0.001f;
	Hit.Time = FMath::Clamp(Hit.Time - DesiredTimeBack, 0.f, 1.f);
}

static bool ShouldIgnoreHitResult(const UWorld* InWorld, FHitResult const& TestHit, FVector const& MovementDirDenormalized, const AActor* MovingActor, EMoveComponentFlags MoveFlags)
{
	if (TestHit.bBlockingHit)
	{
		// VR Pawns need to totally ignore simulating components with movement to prevent sickness
		if (TestHit.Component.IsValid() && TestHit.Component->IsSimulatingPhysics())
			return true;

		// check "ignore bases" functionality
		if ((MoveFlags & MOVECOMP_IgnoreBases) && MovingActor)	//we let overlap components go through because their overlap is still needed and will cause beginOverlap/endOverlap events
		{
			// ignore if there's a base relationship between moving actor and hit actor
			AActor const* const HitActor = TestHit.GetActor();
			if (HitActor)
			{
				if (MovingActor->IsBasedOnActor(HitActor) || HitActor->IsBasedOnActor(MovingActor))
				{
					return true;
				}
			}
		}

		// If we started penetrating, we may want to ignore it if we are moving out of penetration.
		// This helps prevent getting stuck in walls.
		if (TestHit.bStartPenetrating && !(MoveFlags & MOVECOMP_NeverIgnoreBlockingOverlaps))
		{
			const float DotTolerance = CVarInitialOverlapTolerance->GetValueOnGameThread();

			// Dot product of movement direction against 'exit' direction
			const FVector MovementDir = MovementDirDenormalized.GetSafeNormal();
			const float MoveDot = (TestHit.ImpactNormal | MovementDir);

			const bool bMovingOut = MoveDot > DotTolerance;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

				if (CVarShowInitialOverlaps->GetValueOnGameThread() != 0)
				{
					UE_LOG(LogTemp, Log, TEXT("Overlapping %s Dir %s Dot %f Normal %s Depth %f"), *GetNameSafe(TestHit.Component.Get()), *MovementDir.ToString(), MoveDot, *TestHit.ImpactNormal.ToString(), TestHit.PenetrationDepth);
					DrawDebugDirectionalArrow(InWorld, TestHit.TraceStart, TestHit.TraceStart + 30.f * TestHit.ImpactNormal, 5.f, bMovingOut ? FColor(64, 128, 255) : FColor(255, 64, 64), true, 4.f);
					if (TestHit.PenetrationDepth > KINDA_SMALL_NUMBER)
					{
						DrawDebugDirectionalArrow(InWorld, TestHit.TraceStart, TestHit.TraceStart + TestHit.PenetrationDepth * TestHit.Normal, 5.f, FColor(64, 255, 64), true, 4.f);
					}
				}
		//	}
#endif

			// If we are moving out, ignore this result!
			if (bMovingOut)
			{
				return true;
			}
		}
	}

	return false;
}

static bool ShouldIgnoreOverlapResult(const UWorld* World, const AActor* ThisActor, const UPrimitiveComponent& ThisComponent, const AActor* OtherActor, const UPrimitiveComponent& OtherComponent)
{
	// Don't overlap with self
	if (&ThisComponent == &OtherComponent)
	{
		return true;
	}

	// Both components must set bGenerateOverlapEvents
	if (!ThisComponent.bGenerateOverlapEvents || !OtherComponent.bGenerateOverlapEvents)
	{
		return true;
	}

	if (!ThisActor || !OtherActor)
	{
		return true;
	}

	if (!World || OtherActor == World->GetWorldSettings() || !OtherActor->IsActorInitialized())
	{
		return true;
	}

	return false;
}

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
		, VRCapsuleOffset(InComponent->VRCapsuleOffset)
		//, OffsetComponentToWorld(InComponent->OffsetComponentToWorld)
		, LocalToWorld(InComponent->OffsetComponentToWorld.ToMatrixWithScale())
	{
		bWillEverBeLit = false;
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_GetDynamicMeshElements_DrawDynamicElements);

		//const FMatrix& LocalToWorld = OffsetComponentToWorld.ToMatrixWithScale();//GetLocalToWorld();
		const int32 CapsuleSides = FMath::Clamp<int32>(CapsuleRadius / 4.f, 16, 64);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{

			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				const FLinearColor DrawCapsuleColor = GetViewSelectionColor(ShapeColor, *View, IsSelected(), IsHovered(), false, IsIndividuallySelected());

				FVector Base = LocalToWorld.GetOrigin();

				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				DrawWireCapsule(PDI, Base, LocalToWorld.GetScaledAxis(EAxis::X), LocalToWorld.GetScaledAxis(EAxis::Y), LocalToWorld.GetScaledAxis(EAxis::Z), DrawCapsuleColor, CapsuleRadius, CapsuleHalfHeight, CapsuleSides, SDPG_World, 1.25f);
			}
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateTransform_RenderThread(const FTransform &NewTransform, float NewHalfHeight)
	{
		check(IsInRenderingThread());
		LocalToWorld = NewTransform.ToMatrixWithScale();
		//OffsetComponentToWorld = NewTransform;
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
	const FVector VRCapsuleOffset;
	//FTransform OffsetComponentToWorld;
	FMatrix LocalToWorld;
};


UVRRootComponent::UVRRootComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);

	VRCapsuleOffset = FVector(0.0f, 0.0f, 0.0f);
	
	ShapeColor = FColor(223, 149, 157, 255);

	CapsuleRadius = 20.0f;
	CapsuleHalfHeight = 96.0f;
	bUseEditorCompositing = true;
	OffsetComponentToWorld = FTransform(FQuat(0.0f,0.0f,0.0f,1.0f), FVector::ZeroVector, FVector(1.0f));
	
	// Fixes a problem where headset stays at 0,0,0
	lastCameraLoc = FVector::ZeroVector;
	lastCameraRot = FRotator::ZeroRotator;
	curCameraRot = FRotator::ZeroRotator;
	curCameraLoc = FVector::ZeroVector;
	TargetPrimitiveComponent = NULL;
	owningVRChar = NULL;
	//VRCameraCollider = NULL;

	bUseWalkingCollisionOverride = false;
	WalkingCollisionOverride = ECollisionChannel::ECC_Pawn;

	CanCharacterStepUpOn = ECB_No;
	bShouldUpdatePhysicsVolume = true;
	bCheckAsyncSceneOnMove = false;
	SetCanEverAffectNavigation(false);
	bDynamicObstacle = true;
}


FPrimitiveSceneProxy* UVRRootComponent::CreateSceneProxy()
{
	return new FDrawCylinderSceneProxy(this);
}

void UVRRootComponent::BeginPlay()
{
	Super::BeginPlay();


	if(AVRBaseCharacter * vrOwner = Cast<AVRBaseCharacter>(this->GetOwner()))
	{ 
		TargetPrimitiveComponent = vrOwner->VRReplicatedCamera;
		owningVRChar = vrOwner;
		//VRCameraCollider = vrOwner->VRCameraCollider;
		return;
	}
	else
	{
		TArray<USceneComponent*> children = this->GetAttachChildren();

		for (int i = 0; i < children.Num(); i++)
		{
			if (children[i]->IsA(UCameraComponent::StaticClass()))
			{
				TargetPrimitiveComponent = children[i];
				owningVRChar = NULL;
				return;
			}
		}
	}

	//VRCameraCollider = NULL;
	TargetPrimitiveComponent = NULL;
	owningVRChar = NULL;
}


void UVRRootComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{		
	if (IsLocallyControlled())
	{
		if (IsLocallyControlled() && GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed())
		{
			FQuat curRot;
			GEngine->HMDDevice->GetCurrentOrientationAndPosition(curRot, curCameraLoc);
			curCameraRot = curRot.Rotator();
		}
		else if (TargetPrimitiveComponent)
		{
			curCameraRot = TargetPrimitiveComponent->RelativeRotation;
			curCameraLoc = TargetPrimitiveComponent->RelativeLocation;
		}
		else
		{
			curCameraRot = FRotator::ZeroRotator;
			curCameraLoc = FVector::ZeroVector;
		}

		// Can adjust the relative tolerances to remove jitter and some update processing
		if (!(curCameraLoc - lastCameraLoc).IsNearlyZero(0.01f) || !(curCameraRot - lastCameraRot).IsNearlyZero(0.01f))
		{
			// Also calculate vector of movement for the movement component
			FVector LastPosition = OffsetComponentToWorld.GetLocation();

			GenerateOffsetToWorld(false);

			FHitResult OutHit;
			FCollisionQueryParams Params("RelativeMovementSweep", false, GetOwner());
			FCollisionResponseParams ResponseParam;

			InitSweepCollisionParams(Params, ResponseParam);
			Params.bFindInitialOverlaps = true;

			bool bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, LastPosition, OffsetComponentToWorld.GetLocation(), FQuat(0.0f, 0.0f, 0.0f, 1.0f), GetVRCollisionObjectType(), GetCollisionShape(), Params, ResponseParam);
			// If we had a valid blocking hit

			if (bBlockingHit && OutHit.Component.IsValid() && !OutHit.Component->IsSimulatingPhysics())
			{
				bHadRelativeMovement = true;
			}
			else
				bHadRelativeMovement = false;

			lastCameraLoc = curCameraLoc;
			lastCameraRot = curCameraRot;

			OnUpdateTransform(EUpdateTransformFlags::None, ETeleportType::None);
			DifferenceFromLastFrame = (OffsetComponentToWorld.GetLocation() - LastPosition);// .GetSafeNormal2D();
		}
		else
			bHadRelativeMovement = false;
	}
	else
	{
		if (TargetPrimitiveComponent)
		{
			curCameraRot = TargetPrimitiveComponent->RelativeRotation;
			curCameraLoc = TargetPrimitiveComponent->RelativeLocation;
		}
		else
		{
			curCameraRot = FRotator(0.0f, 0.0f, 0.0f);// = FRotator::ZeroRotator;
			curCameraLoc = FVector(0.0f, 0.0f, 0.0f);//FVector::ZeroVector;
		}

		OnUpdateTransform(EUpdateTransformFlags::None, ETeleportType::None);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}


void UVRRootComponent::GenerateOffsetToWorld(bool bUpdateBounds)
{
	FRotator CamRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curCameraRot);

	OffsetComponentToWorld = FTransform(CamRotOffset.Quaternion(), FVector(curCameraLoc.X, curCameraLoc.Y, CapsuleHalfHeight) + CamRotOffset.RotateVector(VRCapsuleOffset), FVector(1.0f)) * ComponentToWorld;

	if (owningVRChar)
	{
		owningVRChar->OffsetComponentToWorld = OffsetComponentToWorld;
	}

	if (bUpdateBounds)
		UpdateBounds();
}

void UVRRootComponent::SendPhysicsTransform(ETeleportType Teleport)
{
	BodyInstance.SetBodyTransform(OffsetComponentToWorld, Teleport);
	BodyInstance.UpdateBodyScale(OffsetComponentToWorld.GetScale3D());
}

// Override this so that the physics representation is in the correct location
void UVRRootComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	GenerateOffsetToWorld();

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
	//FRotator CamRotOffset(0.0f, curCameraRot.Yaw, 0.0f);
	FRotator CamRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw(curCameraRot);

	return FBoxSphereBounds(FVector(curCameraLoc.X, curCameraLoc.Y, CapsuleHalfHeight) + CamRotOffset.RotateVector(VRCapsuleOffset), BoxPoint, BoxPoint.Size()).TransformBy(LocalToWorld);
		
}

#if WITH_EDITOR
void UVRRootComponent::PreEditChange(UProperty* PropertyThatWillChange)
{
	// This is technically not correct at all to do...however when overloading a root component the preedit gets called twice for some reason.
	// Calling it twice attempts to double register it in the list and causes an assert to be thrown.
	if (this->GetOwner()->IsA(AVRCharacter::StaticClass()))
		return;	
	else
		Super::PreEditChange(PropertyThatWillChange);
}

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
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UVRRootComponent, VRCapsuleOffset))
	{
	}

	if (!IsTemplate())
	{
		//UpdateBodySetup(); // do this before reregistering components so that new values are used for collision
	}

	return;

	// Overrode the defaults for this, don't call the parent
	//Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif	// WITH_EDITOR


// This overrides the movement logic to use the offset location instead of the default location for sweeps.
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
	/*const*/ FVector TraceStart = OffsetComponentToWorld.GetLocation();// .GetLocation();//GetComponentLocation();
	/*const*/ FVector TraceEnd = TraceStart + Delta;
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
		bMoved = InternalSetWorldLocationAndRotation(/*TraceEnd*/GetComponentLocation() + Delta, NewRotationQuat, bSkipPhysicsMove, Teleport);
		GenerateOffsetToWorld();
		bRotationOnly = (DeltaSizeSq == 0);
		bIncludesOverlapsAtEnd = bRotationOnly && (AreSymmetricRotations(InitialRotationQuat, NewRotationQuat, GetComponentScale())) && IsCollisionEnabled();
	}
	else
	{
		TArray<FHitResult> Hits;
		FVector NewLocation = GetComponentLocation();//TraceStart;
		// Perform movement collision checking if needed for this actor.
		const bool bCollisionEnabled = IsCollisionEnabled();
		if (bCollisionEnabled && (DeltaSizeSq > 0.f))
		{
/*#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
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
#endif*/
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST) && PERF_MOVECOMPONENT_STATS
			MoveTimer.bDidLineCheck = true;
#endif 
			UWorld* const MyWorld = GetWorld();

			FComponentQueryParams Params(/*PrimitiveComponentStatics::MoveComponentName*/"MoveComponent", Actor);
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

			/*if(bSweepHeadWithMovement && VRCameraCollider)
			{
				FVector TraceStartC = VRCameraCollider->GetComponentLocation();
				FVector TraceEndC = TraceStartC + Delta;

				VRCameraCollider->InitSweepCollisionParams(Params, ResponseParam);

				bool bHadBlockingHitHead = MyWorld->ComponentSweepMulti(HitsHead, VRCameraCollider, TraceStartC, TraceEndC, VRCameraCollider->GetComponentQuat(), Params);

				if (!bHadBlockingHit)
					bHadBlockingHit = bHadBlockingHitHead;

				if (HitsHead.Num() > 0)
				{
					const float DeltaSize = FMath::Sqrt(DeltaSizeSq);
					///FVector impactnormal = -Delta;
					//impactnormal.Normalize();
					for (int32 HitIdx = 0; HitIdx < HitsHead.Num(); HitIdx++)
					{
						PullBackHit(HitsHead[HitIdx], TraceStartC, TraceEndC, DeltaSize);
						//HitsHead[HitIdx].ImpactNormal.Z = 0;// = impactnormal;
						//HitsHead[HitIdx].Normal.Z = 0;// = impactnormal;
					}

					Hits.Append(HitsHead);
				}
			}*/

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
				NewLocation += (TraceEnd - TraceStart);
			}
			else
			{
				NewLocation += (BlockingHit.Time * (TraceEnd - TraceStart));

				// Sanity check
				const FVector ToNewLocation = (NewLocation - GetComponentLocation()/*TraceStart*/);
				if (ToNewLocation.SizeSquared() <= MinMovementDistSq)
				{
					// We don't want really small movements to put us on or inside a surface.
					NewLocation = GetComponentLocation();//TraceStart;
					BlockingHit.Time = 0.f;

					// Remove any pending overlaps after this point, we are not going as far as we swept.
					if (FirstNonInitialOverlapIdx != INDEX_NONE)
					{
						PendingOverlaps.SetNum(FirstNonInitialOverlapIdx);
					}
				}
			}

			bIncludesOverlapsAtEnd = AreSymmetricRotations(InitialRotationQuat, NewRotationQuat, GetComponentScale());
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
		GenerateOffsetToWorld();
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
					OverlapsAtEndLocationPtr = ConvertSweptOverlapsToCurrentOverlaps(OverlapsAtEndLocation, PendingOverlaps, 0, /*GetComponentLocation()*/OffsetComponentToWorld.GetLocation(), GetComponentQuat());
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
						else if (OtherPrimitive->ComponentOverlapComponent(this, EndLocation, EndRotationQuat, UnusedQueryParams))
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
}