// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Runtime/Engine/Private/EnginePrivate.h"
#include "ParentRelativeAttachmentComponent.h"


UParentRelativeAttachmentComponent::UParentRelativeAttachmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);
	this->RelativeLocation = FVector(0, 0, 0);

	bLockPitch = true;
	bLockYaw = false;
	bLockRoll = true;

	PitchTolerance = 1.0f;
	YawTolerance = 1.0f;
	RollTolerance = 1.0f;

	bAutoSizeCapsuleHeight = false;
	AutoSizeCapsuleOffset = FVector(-5.0f, 0.0f, 0.0f);

	ShapeColor = FColor(223, 149, 157, 255);

	CapsuleRadius = 9.0f;
	CapsuleHalfHeight = 9.0f;
	bUseEditorCompositing = true;
}

void UParentRelativeAttachmentComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	if(this->GetAttachParent())
	{
		FRotator InverseRot = GetAttachParent()->GetComponentRotation();
		FRotator CurRot = this->GetComponentRotation();

		float newYaw = CurRot.Yaw;
		float newRoll = CurRot.Roll;
		float newPitch = CurRot.Pitch;

		if (bLockYaw)
			newYaw = 0;
		else if (!bLockYaw && (FPlatformMath::Abs(InverseRot.Yaw - CurRot.Yaw)) > YawTolerance)
			newYaw = InverseRot.Yaw;
		else
			newYaw = CurRot.Yaw;

		if (bLockPitch)
			newPitch = 0;
		else if (!bLockPitch && (FPlatformMath::Abs(InverseRot.Pitch - CurRot.Pitch)) > PitchTolerance)
			newPitch = InverseRot.Pitch;

		if (bLockRoll)
			newRoll = 0;
		else if (!bLockRoll && (FPlatformMath::Abs(InverseRot.Roll - CurRot.Roll)) > RollTolerance)
			newRoll = InverseRot.Roll;

		SetWorldRotation(FRotator(newPitch, newYaw, newRoll), false);
		
		if (bAutoSizeCapsuleHeight)
		{
			// Hacky experiment with full capsule collision on the body that scales with head height
			SetCapsuleSize(this->CapsuleRadius, FMath::Clamp(GetAttachParent()->RelativeLocation.Z / 2,CapsuleRadius,300.0f), false);
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

FPrimitiveSceneProxy* UParentRelativeAttachmentComponent::CreateSceneProxy()
{
	/** Represents a UParentRelativeAttachmentComponent to the scene manager. */
	class FDrawCylinderSceneProxy : public FPrimitiveSceneProxy
	{
	public:
		FDrawCylinderSceneProxy(const UParentRelativeAttachmentComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
			, bDrawOnlyIfSelected(InComponent->bDrawOnlyIfSelected)
			, CapsuleRadius(InComponent->CapsuleRadius)
			, CapsuleHalfHeight(InComponent->CapsuleHalfHeight)
			, ShapeColor(InComponent->ShapeColor)
			, AutoSizeCapsuleOffset(InComponent->AutoSizeCapsuleOffset)
		{
			bWillEverBeLit = false;
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_GetDynamicMeshElements_DrawDynamicElements);


			const FMatrix& LocalToWorld = GetLocalToWorld();
			const int32 CapsuleSides = FMath::Clamp<int32>(CapsuleRadius / 4.f, 16, 64);

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{

				if (VisibilityMap & (1 << ViewIndex))
				{
					const FSceneView* View = Views[ViewIndex];
					const FLinearColor DrawCapsuleColor = GetViewSelectionColor(ShapeColor, *View, IsSelected(), IsHovered(), false, IsIndividuallySelected());
					
					// This is a quick hack and doesn't really show the location, it only works in viewport where the character has no rotation
					FVector Base = LocalToWorld.GetOrigin();
					Base += AutoSizeCapsuleOffset;
					Base.Z -= CapsuleHalfHeight;

					FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

					DrawWireCapsule(PDI, Base, LocalToWorld.GetScaledAxis(EAxis::X), LocalToWorld.GetScaledAxis(EAxis::Y), LocalToWorld.GetScaledAxis(EAxis::Z), DrawCapsuleColor, CapsuleRadius, CapsuleHalfHeight, CapsuleSides, SDPG_World,2.0f);
				}
			}
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
		const FColor	ShapeColor;
		const FVector AutoSizeCapsuleOffset;
	};

	return new FDrawCylinderSceneProxy(this);
}


FBoxSphereBounds UParentRelativeAttachmentComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FVector BoxPoint = FVector(CapsuleRadius, CapsuleRadius, CapsuleHalfHeight);
	return FBoxSphereBounds(/*FVector::ZeroVector*/FVector(0,0,-CapsuleHalfHeight) + AutoSizeCapsuleOffset, BoxPoint, BoxPoint.Size()).TransformBy(LocalToWorld);
}

void UParentRelativeAttachmentComponent::CalcBoundingCylinder(float& CylinderRadius, float& CylinderHalfHeight) const
{
	const float Scale = ComponentToWorld.GetMaximumAxisScale();
	const float CapsuleEndCapCenter = FMath::Max(CapsuleHalfHeight - CapsuleRadius, 0.f);
	const FVector ZAxis = ComponentToWorld.TransformVectorNoScale(FVector(0.f, 0.f, CapsuleEndCapCenter * Scale));

	const float ScaledRadius = CapsuleRadius * Scale;

	CylinderRadius = ScaledRadius + FMath::Sqrt(FMath::Square(ZAxis.X) + FMath::Square(ZAxis.Y));
	CylinderHalfHeight = ScaledRadius + ZAxis.Z;
}

void UParentRelativeAttachmentComponent::Serialize(FArchive& Ar)
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
void UParentRelativeAttachmentComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	// We only want to modify the property that was changed at this point
	// things like propagation from CDO to instances don't work correctly if changing one property causes a different property to change
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UParentRelativeAttachmentComponent, CapsuleHalfHeight))
	{
		CapsuleHalfHeight = FMath::Max3(0.f, CapsuleHalfHeight, CapsuleRadius);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UParentRelativeAttachmentComponent, CapsuleRadius))
	{
		CapsuleRadius = FMath::Clamp(CapsuleRadius, 0.f, CapsuleHalfHeight);
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif	// WITH_EDITOR

void UParentRelativeAttachmentComponent::SetCapsuleSize(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps)
{
	CapsuleHalfHeight = FMath::Max3(0.f, NewHalfHeight, NewRadius);
	CapsuleRadius = FMath::Max(0.f, NewRadius);
	MarkRenderStateDirty();

	// do this if already created
	// otherwise, it hasn't been really created yet
	if (bPhysicsStateCreated)
	{
		DestroyPhysicsState();
		UpdateBodySetup();
		CreatePhysicsState();

		if (bUpdateOverlaps && IsCollisionEnabled() && GetOwner())
		{
			UpdateOverlaps();
		}
	}
}

void UParentRelativeAttachmentComponent::UpdateBodySetup()
{
	if (ShapeBodySetup == NULL || ShapeBodySetup->IsPendingKill())
	{
		ShapeBodySetup = NewObject<UBodySetup>(this);
		ShapeBodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		ShapeBodySetup->AggGeom.SphylElems.Add(FKSphylElem());
	}

	check(ShapeBodySetup->AggGeom.SphylElems.Num() == 1);
	FKSphylElem* SE = ShapeBodySetup->AggGeom.SphylElems.GetData();

	SE->SetTransform(FTransform(FQuat(0,0,0,1.0f), FVector(0,0,-CapsuleHalfHeight) + AutoSizeCapsuleOffset, FVector(1.0f)));//FTransform::Identity);
	SE->Radius = CapsuleRadius;
	SE->Length = 2 * FMath::Max(CapsuleHalfHeight - CapsuleRadius, 0.f);	//SphylElem uses height from center of capsule spheres, but UParentRelativeAttachmentComponent uses halfHeight from end of the sphere
}

bool UParentRelativeAttachmentComponent::IsZeroExtent() const
{
	return (CapsuleRadius == 0.f) && (CapsuleHalfHeight == 0.f);
}


FCollisionShape UParentRelativeAttachmentComponent::GetCollisionShape(float Inflation) const
{
	const float ShapeScale = GetShapeScale();
	const float Radius = FMath::Max(0.f, (CapsuleRadius * ShapeScale) + Inflation);
	const float HalfHeight = FMath::Max(0.f, (CapsuleHalfHeight * ShapeScale) + Inflation);
	return FCollisionShape::MakeCapsule(Radius, HalfHeight);
}

bool UParentRelativeAttachmentComponent::AreSymmetricRotations(const FQuat& A, const FQuat& B, const FVector& Scale3D) const
{
	if (Scale3D.X != Scale3D.Y)
	{
		return false;
	}

	const FVector AUp = A.GetAxisZ();
	const FVector BUp = B.GetAxisZ();
	return AUp.Equals(BUp);
}