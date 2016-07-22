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
		, LocationOffset(InComponent->curCameraLoc)
		, OffsetComponentToWorld(InComponent->OffsetComponentToWorld)
	{
		bWillEverBeLit = false;
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_GetDynamicMeshElements_DrawDynamicElements);

		const FMatrix& LocalToWorld = /*OffsetComponentToWorld.ToMatrixWithScale();*/GetLocalToWorld();
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

				DrawWireCapsule(PDI, Base, LocalToWorld.GetScaledAxis(EAxis::X), LocalToWorld.GetScaledAxis(EAxis::Y), LocalToWorld.GetScaledAxis(EAxis::Z), DrawCapsuleColor, CapsuleRadius, CapsuleHalfHeight, CapsuleSides, SDPG_World, 2.0f);
			}
		}
	}

	/** Called on render thread to assign new dynamic data */
	void UpdateTransform_RenderThread(FTransform NewTransform)
	{
		check(IsInRenderingThread());
		OffsetComponentToWorld = NewTransform;
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
	const float		CapsuleHalfHeight;
	FColor	ShapeColor;
	const FVector AutoSizeCapsuleOffset;
	const FVector LocationOffset;
	FTransform OffsetComponentToWorld;
	bool bExpectingCameraInput;
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
	AutoCapsuleUpdateRate = 10;
	AutoCapsuleUpdateCount = 0.0f;
	bAutoCapsuleUpdateEveryFrame = false;

	ShapeColor = FColor(223, 149, 157, 255);

	CapsuleRadius = 9.0f;
	CapsuleHalfHeight = 9.0f;
	bUseEditorCompositing = true;

	curCameraRot = FQuat(0.0f, 0.0f, 0.0f, 1.0f);// = FRotator::ZeroRotator;
	curCameraLoc = FVector(0.0f, 0.0f, 0.0f);//FVector::ZeroVector;
	TargetPrimitiveComponent = NULL;


	bTEST = false;
}

/*FBodyInstance* UVRRootComponent::GetBodyInstance(FName BoneName, bool bGetWelded) const
{
//	if (TargetPrimitiveComponent)
	//	return TargetPrimitiveComponent->GetBodyInstance();
	//else
	//	return Super::GetBodyInstance(BoneName, bGetWelded);
}*/

/*class UBodySetup* UVRRootComponent::GetBodySetup()
{
	//if (TargetPrimitiveComponent)
	//	return TargetPrimitiveComponent->GetBodySetup();
	//else
		return Super::GetBodySetup();
}*/

FPrimitiveSceneProxy* UVRRootComponent::CreateSceneProxy()
{

	return new FDrawCylinderSceneProxy(this);
}

void UVRRootComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{		
	//SCOPE_CYCLE_COUNTER(STAT_CreatePhysicsMeshes);

	if (GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed())
	{
		GEngine->HMDDevice->GetCurrentOrientationAndPosition(curCameraRot, curCameraLoc);
	}
	else
	{
		curCameraRot = FQuat(0.0f, 0.0f, 0.0f, 1.0f);// = FRotator::ZeroRotator;
		curCameraLoc = FVector(0.0f, 0.0f, 0.0f);//FVector::ZeroVector;
	}

	if (!bTEST)
	{
		SetCapsuleSize(this->CapsuleRadius, FMath::Clamp(curCameraLoc.Z / 2, CapsuleRadius, 300.0f), false);
		bTEST = true;
	}

	if (bAutoSizeCapsuleHeight)
	{
		if (bAutoCapsuleUpdateEveryFrame)
		{
			if (bAutoSizeCapsuleHeight)
				SetCapsuleSize(this->CapsuleRadius, FMath::Clamp(curCameraLoc.Z / 2, CapsuleRadius, 300.0f), false);
			else
			{
				SetCapsuleSize(this->CapsuleRadius, this->CapsuleHalfHeight, false);
			}
		}
		else
		{
			AutoCapsuleUpdateCount += DeltaTime;

			if (AutoCapsuleUpdateCount >= (1.0f / AutoCapsuleUpdateRate))
			{
				AutoCapsuleUpdateCount = 0.0f;

				// Hacky experiment with full capsule collision on the body that scales with head height
				if (bAutoSizeCapsuleHeight)
					SetCapsuleSize(this->CapsuleRadius, FMath::Clamp(curCameraLoc.Z / 2, CapsuleRadius, 300.0f), false);
				else
				{
					SetCapsuleSize(this->CapsuleRadius, this->CapsuleHalfHeight, false);
				}
			}
		}
	}

	if (TargetPrimitiveComponent != NULL)
	{
		OffsetComponentToWorld = TargetPrimitiveComponent->ComponentToWorld;

		//BodyInstance.SetBodyTransform(TargetPrimitiveComponent->GetRelativeTransform(), ETeleportType::TeleportPhysics);
		//BodyInstance.UpdateBodyScale(TargetPrimitiveComponent->GetRelativeTransform().GetScale3D());
		// Enqueue command to send to render thread

		ENQUEUE_UNIQUE_RENDER_COMMAND_TWOPARAMETER(
			FDrawCylinderTransformUpdate,
			FDrawCylinderSceneProxy*, CylinderSceneProxy, (FDrawCylinderSceneProxy*)SceneProxy,
			FTransform, OffsetComponentToWorld, OffsetComponentToWorld,
			{
				CylinderSceneProxy->UpdateTransform_RenderThread(OffsetComponentToWorld);
			}
		);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

FBoxSphereBounds UVRRootComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FVector BoxPoint = FVector(CapsuleRadius, CapsuleRadius, CapsuleHalfHeight);
	return FBoxSphereBounds(/*FVector::ZeroVectorFVector*/FVector(curCameraLoc.X,curCameraLoc.Y,CapsuleHalfHeight) + AutoSizeCapsuleOffset, BoxPoint, BoxPoint.Size()).TransformBy(LocalToWorld);
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
		if (ShapeBodySetup != NULL)
			ShapeBodySetup->InvalidatePhysicsData();
		//DestroyPhysicsState();
		UpdateBodySetup();
		//CreatePhysicsState();
		ShapeBodySetup->CreatePhysicsMeshes();
		RecreatePhysicsState();

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

	SE->SetTransform(FTransform(FQuat(0,0,0,1.0f), FVector(curCameraLoc.X, curCameraLoc.Y, CapsuleHalfHeight) + AutoSizeCapsuleOffset, FVector(1.0f)));//FTransform::Identity);
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