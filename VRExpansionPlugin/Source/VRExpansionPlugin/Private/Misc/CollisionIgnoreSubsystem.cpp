// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/CollisionIgnoreSubsystem.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(CollisionIgnoreSubsystem)

#include "Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Classes/Kismet/GameplayStatics.h"
#include "VRGlobalSettings.h"

//#include "Chaos/ParticleHandle.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"

#include "Chaos/ContactModification.h"

DEFINE_LOG_CATEGORY(VRE_CollisionIgnoreLog);


void FCollisionIgnoreSubsystemAsyncCallback::OnContactModification_Internal(Chaos::FCollisionContactModifier& Modifier)
{
	const FSimCallbackInputVR* Input = GetConsumerInput_Internal();

	if (Input && Input->bIsInitialized)
	{
		for (Chaos::FContactPairModifierIterator ContactIterator = Modifier.Begin(); ContactIterator; ++ContactIterator)
		{
			if (ContactIterator.IsValid())
			{
				Chaos::TVec2<Chaos::FGeometryParticleHandle*> Pair = ContactIterator->GetParticlePair();

				Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>* ParticleHandle0 = Pair[0]->CastToRigidParticle();
				Chaos::TPBDRigidParticleHandle<Chaos::FReal, 3>* ParticleHandle1 = Pair[1]->CastToRigidParticle();


				// Chaos has some sort of bug here, the constraint flags are cleared, and for that matter the 
				// ID0 and ID1 pairs losing collision ignore shouldn't happen as they are still valid always
				// Don't use contact modification until after we see if the incoming chaos fixes for constraint collision ignore
				// is resolved.
				if (ParticleHandle0 && ParticleHandle1)
				{
					// This lets us pull the transform at time of collision, collision events use the first contact
					// for the information to throw out so we should be able to pull it here and keep it for that pair for the frame
					//FTransform particleHandle = Chaos::FRigidTransform3(ParticleHandle0->X(), ParticleHandle0->R());
					//FTransform particleHandle2 = Chaos::FRigidTransform3(ParticleHandle0->X(), ParticleHandle1->R());


					//bool bHasCollisionFlag = ParticleHandle0->HasCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
					//bool bHadCollisionFlag2 = ParticleHandle1->HasCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);

					//if (bHasCollisionFlag && bHadCollisionFlag2)
					{
						FChaosParticlePair SearchPair(ParticleHandle0, ParticleHandle1);

						if (Input->ParticlePairs.Contains(SearchPair))
						{
							ContactIterator->Disable();
						}
					}
				}
			}
		}
	}
}

void UCollisionIgnoreSubsystem::ConstructInput()
{
	if (ContactModifierCallback)
	{
		FSimCallbackInputVR* Input = ContactModifierCallback->GetProducerInputData_External();
		if (Input->bIsInitialized == false)
		{
			Input->bIsInitialized = true;
		}

		// Clear out the pair array
		Input->Reset();

		for (TPair<FCollisionPrimPair, FCollisionIgnorePairArray>& CollisionPairArray : CollisionTrackedPairs)
		{
			for (FCollisionIgnorePair& IgnorePair : CollisionPairArray.Value.PairArray)
			{
				if (IgnorePair.Actor1 && IgnorePair.Actor2)
				{
					Input->ParticlePairs.Add(FChaosParticlePair(IgnorePair.Actor1->GetHandle_LowLevel()->CastToRigidParticle(), IgnorePair.Actor2->GetHandle_LowLevel()->CastToRigidParticle()));
				}
			}
		}
	}
}


void UCollisionIgnoreSubsystem::UpdateTimer(bool bChangesWereMade)
{
	RemovedPairs.Reset();
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();

	if (CollisionTrackedPairs.Num() > 0)
	{
		if (!UpdateHandle.IsValid())
		{
			// Setup the heartbeat on 1htz checks
			GetWorld()->GetTimerManager().SetTimer(UpdateHandle, this, &UCollisionIgnoreSubsystem::CheckActiveFilters, VRSettings.CollisionIgnoreSubsystemUpdateRate, true, VRSettings.CollisionIgnoreSubsystemUpdateRate);

			if (VRSettings.bUseCollisionModificationForCollisionIgnore && !ContactModifierCallback)
			{
				if (UWorld* World = GetWorld())
				{
					if (FPhysScene* PhysScene = World->GetPhysicsScene())
					{
						// Register a callback
						ContactModifierCallback = PhysScene->GetSolver()->CreateAndRegisterSimCallbackObject_External<FCollisionIgnoreSubsystemAsyncCallback>(/*true*/);
					}
				}
			}
		}

		// Need to only add input when changes are made
		if (VRSettings.bUseCollisionModificationForCollisionIgnore && ContactModifierCallback && bChangesWereMade)
		{
			ConstructInput();
		}
	}
	else if (UpdateHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(UpdateHandle);

		if (VRSettings.bUseCollisionModificationForCollisionIgnore && ContactModifierCallback)
		{
			//FSimCallbackInputVR* Input = ContactModifierCallback->GetProducerInputData_External();
			//Input->bIsInitialized = false;

			if (UWorld* World = GetWorld())
			{
				if (FPhysScene* PhysScene = World->GetPhysicsScene())
				{
					// UnRegister a callback
					PhysScene->GetSolver()->UnregisterAndFreeSimCallbackObject_External(ContactModifierCallback);
					ContactModifierCallback = nullptr;
				}
			}
		}
	}
}

void UCollisionIgnoreSubsystem::CheckActiveFilters()
{
	bool bMadeChanges = false;

	for (TPair<FCollisionPrimPair, FCollisionIgnorePairArray>& KeyPair : CollisionTrackedPairs)
	{
		// First check for invalid primitives
		if (!IsValid(KeyPair.Key.Prim1) || !IsValid(KeyPair.Key.Prim2))
		{
			// If we don't have a map element for this pair, then add it now
			if (!RemovedPairs.Contains(KeyPair.Key))
			{
				RemovedPairs.Add(KeyPair.Key, KeyPair.Value);
				bMadeChanges = true;
			}

			continue; // skip remaining checks as we have invalid primitives anyway
		}

		// Skip this section but leave the code intact in case i need it eventually
#if false
		// Now check for lost physics handles
		// Implement later
		if (FPhysScene* PhysScene = KeyPair.Key.Prim1->GetWorld()->GetPhysicsScene())
		{
			Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = PhysScene->GetSolver()->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();
			FPhysicsCommand::ExecuteWrite(PhysScene, [&]()
				{
					using namespace Chaos;
					for (int i = KeyPair.Value.PairArray.Num() - 1; i >= 0; i--)
					{
						FPhysicsActorHandle hand1 = KeyPair.Value.PairArray[i].Actor1;
						FPhysicsActorHandle hand2 = KeyPair.Value.PairArray[i].Actor2;

						/*if (bIgnoreCollision)
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

								}
							}
						}*/

						if (
							(!KeyPair.Value.PairArray[i].Actor1 || !FPhysicsInterface::IsValid(KeyPair.Value.PairArray[i].Actor1)) ||
							(!KeyPair.Value.PairArray[i].Actor2 || !FPhysicsInterface::IsValid(KeyPair.Value.PairArray[i].Actor2))
							)
						{

							// We will either be re-initing or deleting from here so it will always involve changes
							bMadeChanges = true;

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
						else
						{
							Chaos::FUniqueIdx ID0 = KeyPair.Value.PairArray[i].Actor1->GetParticle_LowLevel()->UniqueIdx();
							Chaos::FUniqueIdx ID1 = KeyPair.Value.PairArray[i].Actor2->GetParticle_LowLevel()->UniqueIdx();

							if (!IgnoreCollisionManager.IgnoresCollision(ID0, ID1))
							{
								auto* pHandle1 = KeyPair.Value.PairArray[i].Actor1->GetHandle_LowLevel();
								auto* pHandle2 = KeyPair.Value.PairArray[i].Actor2->GetHandle_LowLevel();

								if (pHandle1 && pHandle2)
								{
									TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = pHandle1->CastToRigidParticle();
									TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = pHandle2->CastToRigidParticle();

									if (ParticleHandle0 && ParticleHandle1)
									{
										ParticleHandle0->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
										IgnoreCollisionManager.AddIgnoreCollisionsFor(ID0, ID1);

										ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
										IgnoreCollisionManager.AddIgnoreCollisionsFor(ID1, ID0);
									}
								}
							}
						}
					}
				});
		}
#endif

		// If there are no pairs left
		if (KeyPair.Value.PairArray.Num() < 1)
		{
			// Try and remove it, chaos should be cleaning up the ignore setups
			if (!RemovedPairs.Contains(KeyPair.Key))
			{
				RemovedPairs.Add(KeyPair.Key, KeyPair.Value);
			}
		}
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

	UpdateTimer(bMadeChanges);
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

	UpdateTimer(true);
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

bool UCollisionIgnoreSubsystem::AreComponentsIgnoringCollisions(UPrimitiveComponent* Prim1, UPrimitiveComponent* Prim2)
{

	if (!Prim1 || !Prim2)
		return false;

	TSet<FCollisionPrimPair> CurrentKeys;
	int numKeys = CollisionTrackedPairs.GetKeys(CurrentKeys);

	FCollisionPrimPair SearchPair;
	SearchPair.Prim1 = Prim1;
	SearchPair.Prim2 = Prim2;

	// This checks if we exist already as well as provides an index
	if (FCollisionPrimPair* ExistingPair = CurrentKeys.Find(SearchPair))
	{
		// These components are ignoring collision
		return true;
	}

	return false;
}

void UCollisionIgnoreSubsystem::InitiateIgnore()
{

}

bool UCollisionIgnoreSubsystem::HasCollisionIgnorePairs()
{
	return CollisionTrackedPairs.Num() > 0;
}

void UCollisionIgnoreSubsystem::SetComponentCollisionIgnoreState(bool bIterateChildren1, bool bIterateChildren2, UPrimitiveComponent* Prim1, FName OptionalBoneName1, UPrimitiveComponent* Prim2, FName OptionalBoneName2, bool bIgnoreCollision, bool bCheckFilters)
{
	if (!Prim1 || !Prim2)
	{
		UE_LOG(VRE_CollisionIgnoreLog, Error, TEXT("Set Objects Ignore Collision called with invalid object(s)!!"));
		return;
	}

	if (Prim1->GetCollisionEnabled() == ECollisionEnabled::NoCollision || Prim2->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
	{
		UE_LOG(VRE_CollisionIgnoreLog, Error, TEXT("Set Objects Ignore Collision called with one or more objects with no collision!! %s, %s"), *Prim1->GetName(), *Prim2->GetName());
		return;
	}

	// Appears to work now with the chaos collision ignore setup
	/*if (Prim1->Mobility == EComponentMobility::Static || Prim2->Mobility == EComponentMobility::Static)
	{
		UE_LOG(VRE_CollisionIgnoreLog, Error, TEXT("Set Objects Ignore Collision called with at least one static mobility object (cannot ignore collision with it)!!"));
		if (bIgnoreCollision)
		{
			return;
		}
	}*/

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
			if (ApplicableBodies[i].BInstance && ApplicableBodies2[j].BInstance && ApplicableBodies[i].BInstance->ActorHandle && ApplicableBodies2[j].BInstance->ActorHandle)
			{
				if (FPhysScene* PhysScene = Prim1->GetWorld()->GetPhysicsScene())
				{
					FCollisionIgnorePair newIgnorePair;
					newIgnorePair.Actor1 = ApplicableBodies[i].BInstance->ActorHandle;
					newIgnorePair.BoneName1 = ApplicableBodies[i].BName;
					newIgnorePair.Actor2 = ApplicableBodies2[j].BInstance->ActorHandle;
					newIgnorePair.BoneName2 = ApplicableBodies2[j].BName;

					//Chaos::FUniqueIdx ID0 = ApplicableBodies[i].BInstance->ActorHandle->GetParticle_LowLevel()->UniqueIdx();
					//Chaos::FUniqueIdx ID1 = ApplicableBodies2[j].BInstance->ActorHandle->GetParticle_LowLevel()->UniqueIdx();

					auto* pHandle1 = ApplicableBodies[i].BInstance->ActorHandle->GetHandle_LowLevel();
					auto* pHandle2 = ApplicableBodies2[j].BInstance->ActorHandle->GetHandle_LowLevel();

					Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = PhysScene->GetSolver()->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();

					FPhysicsCommand::ExecuteWrite(PhysScene, [&]()
						{
							using namespace Chaos;

							if (bIgnoreCollision && pHandle1 && pHandle2)
							{
								if (!IgnoreCollisionManager.IgnoresCollision(pHandle1, pHandle2))
								{							
									IgnoreCollisionManager.AddIgnoreCollisions(pHandle1, pHandle2);

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
								}
							}
							else if (pHandle1 && pHandle2)
							{
								if (IgnoreCollisionManager.IgnoresCollision(pHandle1, pHandle2))
								{
									IgnoreCollisionManager.RemoveIgnoreCollisions(pHandle1, pHandle2);

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
				}
			}
		}
	}

	// Update our timer state
	UpdateTimer(true);
}