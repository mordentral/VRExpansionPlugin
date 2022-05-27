// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/CollisionIgnoreSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Classes/Kismet/GameplayStatics.h"

#if WITH_CHAOS
#include "Chaos/ParticleHandle.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"
#endif

DEFINE_LOG_CATEGORY(VRE_CollisionIgnoreLog);


void UCollisionIgnoreSubsystem::CheckActiveFilters()
{
	for (TPair<FCollisionPrimPair, FCollisionIgnorePairArray>& KeyPair : CollisionTrackedPairs)
	{
		// First check for invalid primitives
		if (!IsValid(KeyPair.Key.Prim1) || !IsValid(KeyPair.Key.Prim2))
		{
			// If we don't have a map element for this pair, then add it now
			if (!RemovedPairs.Contains(KeyPair.Key))
			{
				RemovedPairs.Add(KeyPair.Key, KeyPair.Value);
			}

			continue; // skip remaining checks as we have invalid primitives anyway
		}

		// Now check for lost physics handles
		// Implement later


		/*for (int i = KeyPair.Value.PairArray.Num() - 1; i >= 0; i--)
		{

			FPhysicsActorHandle hand1 = KeyPair.Value.PairArray[i].Actor1;
			FPhysicsActorHandle hand2 = KeyPair.Value.PairArray[i].Actor2;
			
			if (
				(!KeyPair.Value.PairArray[i].Actor1 || !FPhysicsInterface::IsValid(KeyPair.Value.PairArray[i].Actor1)) ||
				(!KeyPair.Value.PairArray[i].Actor2 || !FPhysicsInterface::IsValid(KeyPair.Value.PairArray[i].Actor2))
				)
			{

				FName Bone1 = KeyPair.Value.PairArray[i].BoneName1;
				FName Bone2 = KeyPair.Value.PairArray[i].BoneName2;

				FBodyInstance* Inst1 = KeyPair.Key.Prim1->GetBodyInstance(Bone1);
				FBodyInstance* Inst2 = KeyPair.Key.Prim2->GetBodyInstance(Bone2);

				if (Inst1 && Inst2)
				{
					// We still have the bones available, lets go ahead and re-init for them
					KeyPair.Value.PairArray.RemoveAt(i);
					SetComponentCollisionIgnoreState(false, false, KeyPair.Key.Prim1, Bone1, KeyPair.Key.Prim2, Bone2, true, false);
				}
				else
				{
					// Its not still available, lets remove the pair.
					KeyPair.Value.PairArray.RemoveAt(i);
				}
			}
		}

		// If there are no pairs left
		if (KeyPair.Value.PairArray.Num() < 1)
		{
			// Try and remove it, chaos should be cleaning up the ignore setups
			if (!RemovedPairs.Contains(KeyPair.Key))
			{
				RemovedPairs.Add(KeyPair.Key, KeyPair.Value);
			}
		}*/
	}

/*#if WITH_CHAOS
	if (FPhysScene* PhysScene2 = GetWorld()->GetPhysicsScene())
	{
		Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = PhysScene2->GetSolver()->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();
		int32 ExternalTimestamp = PhysScene2->GetSolver()->GetMarshallingManager().GetExternalTimestamp_External();

		Chaos::FIgnoreCollisionManager::FPendingMap IgnoreSet = IgnoreCollisionManager.GetPendingActivationsForGameThread(ExternalTimestamp);
		Chaos::FIgnoreCollisionManager::FDeactivationSet DeactiveSet = IgnoreCollisionManager.GetPendingDeactivationsForGameThread(ExternalTimestamp);
		//Chaos::FIgnoreCollisionManager::FDeactivationSet IgnoreSet = 
		
		// Prints out the list of items currently being re-activated after one of their pairs died.
		// Chaos automatically cleans up here, I don't need to do anything.
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("Pending activate: %i - Pending deativate: %i"), IgnoreSet.Num(), DeactiveSet.Num()));
	}
#endif*/

	for (const TPair<FCollisionPrimPair, FCollisionIgnorePairArray>& KeyPair : RemovedPairs)
	{
		if (CollisionTrackedPairs.Contains(KeyPair.Key))
		{
			CollisionTrackedPairs[KeyPair.Key].PairArray.Empty();
			CollisionTrackedPairs.Remove(KeyPair.Key);
		}
	}

	UpdateTimer();
}

void UCollisionIgnoreSubsystem::RemoveComponentCollisionIgnoreState(UPrimitiveComponent* Prim1)
{

	if (!Prim1)
		return;
	

	TMap<FCollisionPrimPair, FCollisionIgnorePairArray> PairsToRemove;

	for (const TPair<FCollisionPrimPair, FCollisionIgnorePairArray>& KeyPair : CollisionTrackedPairs)
	{
		// First check for invalid primitives
		if (KeyPair.Key.Prim1 == Prim1 || KeyPair.Key.Prim2 == Prim1)
		{
			// If we don't have a map element for this pair, then add it now
			if (!PairsToRemove.Contains(KeyPair.Key))
			{
				PairsToRemove.Add(KeyPair.Key, KeyPair.Value);
			}
		}
	}
	

	for (const TPair<FCollisionPrimPair, FCollisionIgnorePairArray>& KeyPair : PairsToRemove)
	{
		if (CollisionTrackedPairs.Contains(KeyPair.Key))
		{
			for (const FCollisionIgnorePair& newIgnorePair : KeyPair.Value.PairArray)
			{
				// Clear out current ignores
				SetComponentCollisionIgnoreState(false, false, KeyPair.Key.Prim1, newIgnorePair.BoneName1, KeyPair.Key.Prim2, newIgnorePair.BoneName2, false, false);
			}

			/*if (CollisionTrackedPairs.Contains(KeyPair.Key))
			{
				// Ensure we are empty
				CollisionTrackedPairs[KeyPair.Key].PairArray.Empty();
				CollisionTrackedPairs.Remove(KeyPair.Key);
			}*/
		}
	}

	UpdateTimer();
}

bool UCollisionIgnoreSubsystem::IsComponentIgnoringCollision(UPrimitiveComponent* Prim1)
{

	if (!Prim1)
		return false;

	for (const TPair<FCollisionPrimPair, FCollisionIgnorePairArray>& KeyPair : CollisionTrackedPairs)
	{
		// First check for invalid primitives
		if (KeyPair.Key.Prim1 == Prim1 || KeyPair.Key.Prim2 == Prim1)
		{
			return true;
		}
	}

	return false;
}

void UCollisionIgnoreSubsystem::InitiateIgnore()
{

}

void UCollisionIgnoreSubsystem::SetComponentCollisionIgnoreState(bool bIterateChildren1, bool bIterateChildren2, UPrimitiveComponent* Prim1, FName OptionalBoneName1, UPrimitiveComponent* Prim2, FName OptionalBoneName2, bool bIgnoreCollision, bool bCheckFilters)
{
	if (!Prim1 || !Prim2)
	{
		UE_LOG(VRE_CollisionIgnoreLog, Error, TEXT("Set Objects Ignore Collision called with invalid object(s)!!"));
		return;
	}

	if (Prim1->Mobility == EComponentMobility::Static || Prim2->Mobility == EComponentMobility::Static)
	{
		UE_LOG(VRE_CollisionIgnoreLog, Error, TEXT("Set Objects Ignore Collision called with at least one static mobility object (cannot ignore collision with it)!!"));
		if (bIgnoreCollision)
		{
			return;
		}
	}

	USkeletalMeshComponent* SkeleMesh = nullptr;
	USkeletalMeshComponent* SkeleMesh2 = nullptr;

	if (bIterateChildren1)
	{
		SkeleMesh = Cast<USkeletalMeshComponent>(Prim1);
	}

	if (bIterateChildren2)
	{
		SkeleMesh2 = Cast<USkeletalMeshComponent>(Prim2);
	}

	struct BodyPairStore
	{
		FBodyInstance* BInstance;
		FName BName;

		BodyPairStore(FBodyInstance* BI, FName BoneName)
		{
			BInstance = BI;
			BName = BoneName;
		}
	};

	TArray<BodyPairStore> ApplicableBodies;

	if (SkeleMesh)
	{
		UPhysicsAsset* PhysAsset = SkeleMesh ? SkeleMesh->GetPhysicsAsset() : nullptr;
		if (PhysAsset)
		{
			int32 NumBodiesFound = SkeleMesh->ForEachBodyBelow(OptionalBoneName1, true, false, [PhysAsset, &ApplicableBodies](FBodyInstance* BI)
			{
				const FName IterBodyName = PhysAsset->SkeletalBodySetups[BI->InstanceBodyIndex]->BoneName;
				ApplicableBodies.Add(BodyPairStore(BI, IterBodyName));
			});
		}
	}
	else
	{
		FBodyInstance* Inst1 = Prim1->GetBodyInstance(OptionalBoneName1);
		if (Inst1)
		{
			ApplicableBodies.Add(BodyPairStore(Inst1, OptionalBoneName1));
		}
	}

	TArray<BodyPairStore> ApplicableBodies2;
	if (SkeleMesh2)
	{
		UPhysicsAsset* PhysAsset = SkeleMesh2 ? SkeleMesh2->GetPhysicsAsset() : nullptr;
		if (PhysAsset)
		{
			int32 NumBodiesFound = SkeleMesh2->ForEachBodyBelow(OptionalBoneName2, true, false, [PhysAsset, &ApplicableBodies2](FBodyInstance* BI)
				{
					const FName IterBodyName = PhysAsset->SkeletalBodySetups[BI->InstanceBodyIndex]->BoneName;
					ApplicableBodies2.Add(BodyPairStore(BI, IterBodyName));
				});
		}
	}
	else
	{
		FBodyInstance* Inst1 = Prim2->GetBodyInstance(OptionalBoneName2);
		if (Inst1)
		{
			ApplicableBodies2.Add(BodyPairStore(Inst1, OptionalBoneName2));
		}
	}


	FCollisionPrimPair newPrimPair;
	newPrimPair.Prim1 = Prim1;
	newPrimPair.Prim2 = Prim2;

	// Check our active filters and handle inconsistencies before we run the next logic
	// (This prevents cases where null ptrs get added too)
	if (bCheckFilters)
	{
		CheckActiveFilters();
	}

	// If we don't have a map element for this pair, then add it now
	if (bIgnoreCollision && !CollisionTrackedPairs.Contains(newPrimPair))
	{
		CollisionTrackedPairs.Add(newPrimPair, FCollisionIgnorePairArray());
	}
	else if (!bIgnoreCollision && !CollisionTrackedPairs.Contains(newPrimPair))
	{
		// Early out, we don't even have this pair to remove it
		return;
	}

	for (int i = 0; i < ApplicableBodies.Num(); ++i)
	{
		for (int j = 0; j < ApplicableBodies2.Num(); ++j)
		{
			if (ApplicableBodies[i].BInstance && ApplicableBodies2[j].BInstance)
			{
				if (FPhysScene* PhysScene = Prim1->GetWorld()->GetPhysicsScene())
				{
					FCollisionIgnorePair newIgnorePair;
					newIgnorePair.Actor1 = ApplicableBodies[i].BInstance->ActorHandle;
					newIgnorePair.BoneName1 = ApplicableBodies[i].BName;
					newIgnorePair.Actor2 = ApplicableBodies2[j].BInstance->ActorHandle;
					newIgnorePair.BoneName2 = ApplicableBodies2[j].BName;

#if WITH_CHAOS
					Chaos::FUniqueIdx ID0 = ApplicableBodies[i].BInstance->ActorHandle->GetParticle_LowLevel()->UniqueIdx();
					Chaos::FUniqueIdx ID1 = ApplicableBodies2[j].BInstance->ActorHandle->GetParticle_LowLevel()->UniqueIdx();

					Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = PhysScene->GetSolver()->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();

					FPhysicsCommand::ExecuteWrite(PhysScene, [&]()
						{
							using namespace Chaos;

							if (bIgnoreCollision)
							{
								if (!IgnoreCollisionManager.IgnoresCollision(ID0, ID1))
								{
									TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = ApplicableBodies[i].BInstance->ActorHandle->GetHandle_LowLevel()->CastToRigidParticle();
									TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = ApplicableBodies2[j].BInstance->ActorHandle->GetHandle_LowLevel()->CastToRigidParticle();

									if (ParticleHandle0 && ParticleHandle1)
									{
										ParticleHandle0->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
										IgnoreCollisionManager.AddIgnoreCollisionsFor(ID0, ID1);

										ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
										IgnoreCollisionManager.AddIgnoreCollisionsFor(ID1, ID0);


										TSet<FCollisionPrimPair> CurrentKeys;
										int numKeys = CollisionTrackedPairs.GetKeys(CurrentKeys);

										// This checks if we exist already as well as provides an index
										if (FCollisionPrimPair* CurrentPair = CurrentKeys.Find(newPrimPair))
										{
											// Check if the current one has the same primitive ordering as the new check
											if (CurrentPair->Prim1 != newPrimPair.Prim1)
											{
												// If not then lets flip the elements around in order to match it
												newIgnorePair.FlipElements();
											}

											CollisionTrackedPairs[newPrimPair].PairArray.AddUnique(newIgnorePair);
										}

										/*if (ApplicableBodies[i].BInstance->bContactModification != bIgnoreCollision)
											ApplicableBodies[i].BInstance->SetContactModification(true);

										if (ApplicableBodies2[j].BInstance->bContactModification != bIgnoreCollision)
											ApplicableBodies2[j].BInstance->SetContactModification(true);*/
									}
								}
							}
							else
							{
								if (IgnoreCollisionManager.IgnoresCollision(ID0, ID1))
								{
									TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = ApplicableBodies[i].BInstance->ActorHandle->GetHandle_LowLevel()->CastToRigidParticle();
									TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = ApplicableBodies2[j].BInstance->ActorHandle->GetHandle_LowLevel()->CastToRigidParticle();

									if (ParticleHandle0 && ParticleHandle1)
									{
										IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID0, ID1);
										IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID1, ID0);

										if (IgnoreCollisionManager.NumIgnoredCollision(ID0) < 1)
										{
											ParticleHandle0->RemoveCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
										}

										if (IgnoreCollisionManager.NumIgnoredCollision(ID1) < 1)
										{
											ParticleHandle1->RemoveCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
										}
									}

									CollisionTrackedPairs[newPrimPair].PairArray.Remove(newIgnorePair);
									if (CollisionTrackedPairs[newPrimPair].PairArray.Num() < 1)
									{
										CollisionTrackedPairs.Remove(newPrimPair);
									}

									// If we don't have a map element for this pair, then add it now
									if (!RemovedPairs.Contains(newPrimPair))
									{
										RemovedPairs.Add(newPrimPair, FCollisionIgnorePairArray());
									}
									RemovedPairs[newPrimPair].PairArray.AddUnique(newIgnorePair);
								}
							}
						});
#endif
				}
			}
		}
	}

	// Update our timer state
	UpdateTimer();
}