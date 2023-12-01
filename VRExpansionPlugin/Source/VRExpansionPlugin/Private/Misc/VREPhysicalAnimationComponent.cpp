#include "Misc/VREPhysicalAnimationComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VREPhysicalAnimationComponent)

#include "SceneManagement.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "ReferenceSkeleton.h"
#include "DrawDebugHelpers.h"

#if ENABLE_DRAW_DEBUG
#include "Chaos/ImplicitObject.h"
#include "Chaos/TriangleMeshImplicitObject.h"
#endif

#include "Physics/PhysicsInterfaceCore.h"
#include "Physics/PhysicsInterfaceTypes.h"

UVREPhysicalAnimationComponent::UVREPhysicalAnimationComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoSetPhysicsSleepSensitivity = true;
	SleepThresholdMultiplier = 0.0f;
}

/*void UVREPhysicalAnimationComponent::CustomPhysics(float DeltaTime, FBodyInstance* BodyInstance)
{
	//UpdateWeldedBoneDriver(DeltaTime);
}*/

/*void UVREPhysicalAnimationComponent::OnWeldedMassUpdated(FBodyInstance* BodyInstance)
{
	// If our mass changed then our body was altered, lets re-init
	SetupWeldedBoneDriver(true);
}*/

void UVREPhysicalAnimationComponent::SetWeldedBoneDriverPaused(bool bPaused)
{
	bIsPaused = bPaused;
}

bool UVREPhysicalAnimationComponent::IsWeldedBoneDriverPaused()
{
	return bIsPaused;
}

void UVREPhysicalAnimationComponent::RefreshWeldedBoneDriver()
{
	SetupWeldedBoneDriver_Implementation(true);
}

void UVREPhysicalAnimationComponent::SetupWeldedBoneDriver(TArray<FName> BaseBoneNames)
{
	if (BaseBoneNames.Num())
		BaseWeldedBoneDriverNames = BaseBoneNames;

	SetupWeldedBoneDriver_Implementation(false);
}

FTransform UVREPhysicalAnimationComponent::GetWorldSpaceRefBoneTransform(FReferenceSkeleton& RefSkel, int32 BoneIndex, int32 ParentBoneIndex)
{
	FTransform BoneTransform;

	if (BoneIndex > 0 && BoneIndex != ParentBoneIndex)
	{
		BoneTransform = RefSkel.GetRefBonePose()[BoneIndex];

		FMeshBoneInfo BoneInfo = RefSkel.GetRefBoneInfo()[BoneIndex];
		if (BoneInfo.ParentIndex != 0 && BoneInfo.ParentIndex != ParentBoneIndex)
		{
			BoneTransform *= GetWorldSpaceRefBoneTransform(RefSkel, BoneInfo.ParentIndex, ParentBoneIndex);
		}
	}

	return BoneTransform;
}

// #TODO: support off scaling
FTransform UVREPhysicalAnimationComponent::GetRefPoseBoneRelativeTransform(USkeletalMeshComponent* SkeleMesh, FName BoneName, FName ParentBoneName)
{
	FTransform BoneTransform;

	if (SkeleMesh && !BoneName.IsNone() && !ParentBoneName.IsNone())
	{
		//SkelMesh->ClearRefPoseOverride();
		FReferenceSkeleton RefSkel;
		RefSkel = SkeleMesh->GetSkinnedAsset()->GetRefSkeleton();

		BoneTransform = GetWorldSpaceRefBoneTransform(RefSkel, RefSkel.FindBoneIndex(BoneName), RefSkel.FindBoneIndex(ParentBoneName));
	}

	return BoneTransform;
}

void UVREPhysicalAnimationComponent::SetupWeldedBoneDriver_Implementation(bool bReInit)
{
	TArray<FWeldedBoneDriverData> OriginalData;
	if (bReInit)
	{
		OriginalData = BoneDriverMap;
	}

	BoneDriverMap.Empty();

	USkeletalMeshComponent* SkeleMesh = GetSkeletalMesh();

	if (!SkeleMesh || !SkeleMesh->Bodies.Num())
		return;

	// Get ref pose position and walk up the tree to the welded root to get our relative base pose.
	//SkeleMesh->GetRefPosePosition()

	UPhysicsAsset* PhysAsset = SkeleMesh ? SkeleMesh->GetPhysicsAsset() : nullptr;
	if (PhysAsset && SkeleMesh->GetSkinnedAsset())
	{

		for (FName BaseWeldedBoneDriverName : BaseWeldedBoneDriverNames)
		{
			int32 ParentBodyIdx = PhysAsset->FindBodyIndex(BaseWeldedBoneDriverName);

			if (FBodyInstance* ParentBody = (ParentBodyIdx == INDEX_NONE ? nullptr : SkeleMesh->Bodies[ParentBodyIdx]))
			{
				// Build map of bodies that we want to control.
				FPhysicsActorHandle& ActorHandle = ParentBody->WeldParent ? ParentBody->WeldParent->GetPhysicsActorHandle() : ParentBody->GetPhysicsActorHandle();

				if (FPhysicsInterface::IsValid(ActorHandle) /*&& FPhysicsInterface::IsRigidBody(ActorHandle)*/)
				{
					FPhysicsCommand::ExecuteWrite(ActorHandle, [&](FPhysicsActorHandle& Actor)
					{
						//TArray<FPhysicsShapeHandle> Shapes;
						PhysicsInterfaceTypes::FInlineShapeArray Shapes;
						FPhysicsInterface::GetAllShapes_AssumedLocked(Actor, Shapes);

						for (FPhysicsShapeHandle& Shape : Shapes)
						{
							if (ParentBody->WeldParent)
							{
								const FBodyInstance* OriginalBI = ParentBody->WeldParent->GetOriginalBodyInstance(Shape);

								if (OriginalBI != ParentBody)
								{
									// Not originally our shape
									continue;
								}
							}

							FKShapeElem* ShapeElem = FChaosUserData::Get<FKShapeElem>(FPhysicsInterface::GetUserData(Shape));
							if (ShapeElem)
							{
								FName TargetBoneName = ShapeElem->GetName();
								int32 BoneIdx = SkeleMesh->GetBoneIndex(TargetBoneName);

								if (BoneIdx != INDEX_NONE)
								{
									FWeldedBoneDriverData DriverData;
									DriverData.BoneName = TargetBoneName;
									//DriverData.ShapeHandle = Shape;

									if (bReInit && OriginalData.Num() - 1 >= BoneDriverMap.Num())
									{
										DriverData.RelativeTransform = OriginalData[BoneDriverMap.Num()].RelativeTransform;
									}
									else
									{
										FTransform BoneTransform = FTransform::Identity;
										if (SkeleMesh->GetBoneIndex(TargetBoneName) != INDEX_NONE)
											BoneTransform = GetRefPoseBoneRelativeTransform(SkeleMesh, TargetBoneName, BaseWeldedBoneDriverName).Inverse();

										//FTransform BoneTransform = SkeleMesh->GetSocketTransform(TargetBoneName, ERelativeTransformSpace::RTS_World);

										// Calc shape global pose
										//FTransform RelativeTM = FPhysicsInterface::GetLocalTransform(Shape) * FPhysicsInterface::GetGlobalPose_AssumesLocked(ActorHandle);

										//RelativeTM = RelativeTM * BoneTransform.Inverse();
										//BoneTransform.SetScale3D(FVector(1.0f));
										DriverData.RelativeTransform = FPhysicsInterface::GetLocalTransform(Shape) * BoneTransform;
									}

									BoneDriverMap.Add(DriverData);
								}
							}
						}

						if (bAutoSetPhysicsSleepSensitivity && !ParentBody->WeldParent && BoneDriverMap.Num() > 0)
						{
							ParentBody->SleepFamily = ESleepFamily::Custom;
							ParentBody->CustomSleepThresholdMultiplier = SleepThresholdMultiplier;
							float SleepEnergyThresh = FPhysicsInterface::GetSleepEnergyThreshold_AssumesLocked(Actor);
							SleepEnergyThresh *= ParentBody->GetSleepThresholdMultiplier();
							FPhysicsInterface::SetSleepEnergyThreshold_AssumesLocked(Actor, SleepEnergyThresh);
						}
					});
				}
			}
		}
	}
}

void UVREPhysicalAnimationComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Make sure base physical animation component runs its logic
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateWeldedBoneDriver(DeltaTime);
}

void UVREPhysicalAnimationComponent::UpdateWeldedBoneDriver(float DeltaTime)
{
	if (!BoneDriverMap.Num())
		return;

	USkeletalMeshComponent* SkeleMesh = GetSkeletalMesh();

	if (!SkeleMesh || !SkeleMesh->Bodies.Num())// || (!SkeleMesh->IsSimulatingPhysics(BaseWeldedBoneDriverNames) && !SkeleMesh->IsWelded()))
		return;

	UPhysicsAsset* PhysAsset = SkeleMesh ? SkeleMesh->GetPhysicsAsset() : nullptr;
	if(PhysAsset && SkeleMesh->GetSkinnedAsset())
	{
		for (FName BaseWeldedBoneDriverName : BaseWeldedBoneDriverNames)
		{
			int32 ParentBodyIdx = PhysAsset->FindBodyIndex(BaseWeldedBoneDriverName);

			if (FBodyInstance* ParentBody = (ParentBodyIdx == INDEX_NONE ? nullptr : SkeleMesh->Bodies[ParentBodyIdx]))
			{
				// Allow it to run even when not simulating physics, if we have a welded root then it needs to animate anyway
				//if (!ParentBody->IsInstanceSimulatingPhysics() && !ParentBody->WeldParent)
				//	return;

				FPhysicsActorHandle& ActorHandle = ParentBody->WeldParent ? ParentBody->WeldParent->GetPhysicsActorHandle() : ParentBody->GetPhysicsActorHandle();

				if (FPhysicsInterface::IsValid(ActorHandle) /*&& FPhysicsInterface::IsRigidBody(ActorHandle)*/)
				{

#if ENABLE_DRAW_DEBUG
					if (bDebugDrawCollision)
					{
						Chaos::FDebugDrawQueue::GetInstance().SetConsumerActive(this, true); // Need to deactivate this later as well
						Chaos::FDebugDrawQueue::GetInstance().SetMaxCost(20000);
						//Chaos::FDebugDrawQueue::GetInstance().SetRegionOfInterest(SkeleMesh->GetComponentLocation(), 1000.0f);
						Chaos::FDebugDrawQueue::GetInstance().SetEnabled(true);
					}
#endif

					bool bModifiedBody = false;
					FPhysicsCommand::ExecuteWrite(ActorHandle, [&](FPhysicsActorHandle& Actor)
					{
						PhysicsInterfaceTypes::FInlineShapeArray Shapes;
						FPhysicsInterface::GetAllShapes_AssumedLocked(Actor, Shapes);

						FTransform GlobalPose = FPhysicsInterface::GetGlobalPose_AssumesLocked(ActorHandle);
						FTransform GlobalPoseInv = GlobalPose.Inverse();


#if ENABLE_DRAW_DEBUG
						if (bDebugDrawCollision)
						{
							Chaos::FDebugDrawQueue::GetInstance().SetRegionOfInterest(GlobalPose.GetLocation(), 100.0f);
						}

#endif

						for (FPhysicsShapeHandle& Shape : Shapes)
						{
							if (ParentBody->WeldParent)
							{
								const FBodyInstance* OriginalBI = ParentBody->WeldParent->GetOriginalBodyInstance(Shape);

								if (OriginalBI != ParentBody)
								{
									// Not originally our shape
									continue;
								}
							}

							// Log the shapes name to match to the bone
							FName TargetBoneName = NAME_None;
							if (FKShapeElem* ShapeElem = FChaosUserData::Get<FKShapeElem>(FPhysicsInterface::GetUserData(Shape)))
							{
								TargetBoneName = ShapeElem->GetName();
							}
							else
							{
								// Cant find the matching shape
								continue;
							}

							if (FWeldedBoneDriverData* WeldedData = BoneDriverMap.FindByKey(TargetBoneName/*Shape*/))
							{
								bModifiedBody = true;

								FTransform Trans = SkeleMesh->GetSocketTransform(WeldedData->BoneName, ERelativeTransformSpace::RTS_World);

								// This fixes a bug with simulating inverse scaled meshes
								//Trans.SetScale3D(FVector(1.f) * Trans.GetScale3D().GetSignVector());
								FTransform GlobalTransform = WeldedData->RelativeTransform * Trans;
								FTransform RelativeTM = GlobalTransform * GlobalPoseInv;

								// Fix chaos ensure
								RelativeTM.RemoveScaling();

								if (!WeldedData->LastLocal.Equals(RelativeTM))
								{
									FPhysicsInterface::SetLocalTransform(Shape, RelativeTM);
									WeldedData->LastLocal = RelativeTM;
								}
							}

#if ENABLE_DRAW_DEBUG
							if (bDebugDrawCollision)
							{
								const Chaos::FImplicitObject* ShapeImplicit = Shape.Shape->GetGeometry().Get();
								Chaos::EImplicitObjectType Type = ShapeImplicit->GetType();

								FTransform shapeTransform = FPhysicsInterface::GetLocalTransform(Shape);
								FTransform FinalTransform = shapeTransform * GlobalPose;
								Chaos::FRigidTransform3 RigTransform(FinalTransform);
								Chaos::DebugDraw::DrawShape(RigTransform, ShapeImplicit, Chaos::FShapeOrShapesArray(), FColor::White);
							}
#endif
						}
					});

#if ENABLE_DRAW_DEBUG
					if (bDebugDrawCollision)
					{
						// Get the latest commands
						TArray<Chaos::FLatentDrawCommand> DrawCommands;
						Chaos::FDebugDrawQueue::GetInstance().ExtractAllElements(DrawCommands);
						if (DrawCommands.Num())
						{
							DebugDrawMesh(DrawCommands);
						}
						Chaos::FDebugDrawQueue::GetInstance().SetConsumerActive(this, false); // Need to deactivate this later as well
						Chaos::FDebugDrawQueue::GetInstance().SetEnabled(false);
					}
#endif
				}

			}
		}
	}
}

#if ENABLE_DRAW_DEBUG
void UVREPhysicalAnimationComponent::DebugDrawMesh(const TArray<Chaos::FLatentDrawCommand>& DrawCommands)
{
	UWorld* World = this->GetWorld();

	// Draw all the captured elements in the viewport
	for (const Chaos::FLatentDrawCommand& Command : DrawCommands)
	{
		switch (Command.Type)
		{
		case Chaos::FLatentDrawCommand::EDrawType::Point:
			DrawDebugPoint(World, Command.LineStart, Command.Thickness, Command.Color, Command.bPersistentLines, -1.f, Command.DepthPriority);
			break;
		case Chaos::FLatentDrawCommand::EDrawType::Line:
			DrawDebugLine(World, Command.LineStart, Command.LineEnd, Command.Color, Command.bPersistentLines, -1.0f, 0.f, 0.f);
			break;
		case Chaos::FLatentDrawCommand::EDrawType::DirectionalArrow:
			DrawDebugDirectionalArrow(World, Command.LineStart, Command.LineEnd, Command.ArrowSize, Command.Color, Command.bPersistentLines, -1.f, Command.DepthPriority, Command.Thickness);
			break;
		case Chaos::FLatentDrawCommand::EDrawType::Sphere:
			DrawDebugSphere(World, Command.LineStart, Command.Radius, Command.Segments, Command.Color, Command.bPersistentLines, -1.f, Command.DepthPriority, Command.Thickness);
			break;
		case Chaos::FLatentDrawCommand::EDrawType::Box:
			DrawDebugBox(World, Command.Center, Command.Extent, Command.Rotation, Command.Color, Command.bPersistentLines, -1.f, Command.DepthPriority, Command.Thickness);
			break;
		case Chaos::FLatentDrawCommand::EDrawType::String:
			DrawDebugString(World, Command.TextLocation, Command.Text, Command.TestBaseActor, Command.Color, -1.f, Command.bDrawShadow, Command.FontScale);
			break;
		case Chaos::FLatentDrawCommand::EDrawType::Circle:
		{
			FMatrix M = FRotationMatrix::MakeFromYZ(Command.YAxis, Command.ZAxis);
			M.SetOrigin(Command.Center);
			DrawDebugCircle(World, M, Command.Radius, Command.Segments, Command.Color, Command.bPersistentLines, -1.f, Command.DepthPriority, Command.Thickness, Command.bDrawAxis);
			break;
		}
		case Chaos::FLatentDrawCommand::EDrawType::Capsule:
			DrawDebugCapsule(World, Command.Center, Command.HalfHeight, Command.Radius, Command.Rotation, Command.Color, Command.bPersistentLines, -1.f, Command.DepthPriority, Command.Thickness);
		default:
			break;
		}
	}
}
#endif