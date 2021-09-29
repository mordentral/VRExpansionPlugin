#include "Misc/VREPhysicalAnimationComponent.h"
#include "SceneManagement.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#endif
#include "ReferenceSkeleton.h"
#include "DrawDebugHelpers.h"
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
		RefSkel = SkeleMesh->SkeletalMesh->GetRefSkeleton();

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
	if (PhysAsset && SkeleMesh->SkeletalMesh)
	{

//#if PHYSICS_INTERFACE_PHYSX
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
#if WITH_CHAOS 
							FKShapeElem* ShapeElem = FChaosUserData::Get<FKShapeElem>(FPhysicsInterface::GetUserData(Shape));
#elif PHYSICS_INTERFACE_PHYSX
							FKShapeElem* ShapeElem = FPhysxUserData::Get<FKShapeElem>(FPhysicsInterface::GetUserData(Shape));
#endif
							if (ShapeElem)
							{
								FName TargetBoneName = ShapeElem->GetName();
								int32 BoneIdx = SkeleMesh->GetBoneIndex(TargetBoneName);

								if (BoneIdx != INDEX_NONE)
								{
									FWeldedBoneDriverData DriverData;
									DriverData.BoneName = TargetBoneName;
									DriverData.ShapeHandle = Shape;

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
//#endif
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

	/*
	#if WITH_CHAOS || WITH_IMMEDIATE_PHYSX
		//ensure(false);
#else
#endif
	*/
	USkeletalMeshComponent* SkeleMesh = GetSkeletalMesh();

	if (!SkeleMesh || !SkeleMesh->Bodies.Num())// || (!SkeleMesh->IsSimulatingPhysics(BaseWeldedBoneDriverNames) && !SkeleMesh->IsWelded()))
		return;

	UPhysicsAsset* PhysAsset = SkeleMesh ? SkeleMesh->GetPhysicsAsset() : nullptr;
	if(PhysAsset && SkeleMesh->SkeletalMesh)
	{
		for (FName BaseWeldedBoneDriverName : BaseWeldedBoneDriverNames)
		{
			int32 ParentBodyIdx = PhysAsset->FindBodyIndex(BaseWeldedBoneDriverName);

			if (FBodyInstance* ParentBody = (ParentBodyIdx == INDEX_NONE ? nullptr : SkeleMesh->Bodies[ParentBodyIdx]))
			{
				if (!ParentBody->IsInstanceSimulatingPhysics() && !ParentBody->WeldParent)
					return;

				FPhysicsActorHandle& ActorHandle = ParentBody->WeldParent ? ParentBody->WeldParent->GetPhysicsActorHandle() : ParentBody->GetPhysicsActorHandle();

				if (FPhysicsInterface::IsValid(ActorHandle) /*&& FPhysicsInterface::IsRigidBody(ActorHandle)*/)
				{

					bool bModifiedBody = false;
					FPhysicsCommand::ExecuteWrite(ActorHandle, [&](FPhysicsActorHandle& Actor)
					{
						PhysicsInterfaceTypes::FInlineShapeArray Shapes;
						FPhysicsInterface::GetAllShapes_AssumedLocked(Actor, Shapes);

						FTransform GlobalPose = FPhysicsInterface::GetGlobalPose_AssumesLocked(ActorHandle).Inverse();
						
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

							if (FWeldedBoneDriverData* WeldedData = BoneDriverMap.FindByKey(Shape))
							{
								bModifiedBody = true;

								FTransform Trans = SkeleMesh->GetSocketTransform(WeldedData->BoneName, ERelativeTransformSpace::RTS_World);

								// This fixes a bug with simulating inverse scaled meshes
								//Trans.SetScale3D(FVector(1.f) * Trans.GetScale3D().GetSignVector());
								FTransform GlobalTransform = WeldedData->RelativeTransform * Trans;
								FTransform RelativeTM = GlobalTransform * GlobalPose;

								if (!WeldedData->LastLocal.Equals(RelativeTM))
								{
									FPhysicsInterface::SetLocalTransform(Shape, RelativeTM);
									WeldedData->LastLocal = RelativeTM;
								}
							}
						}
					});
				}

			}
		}
	}
}