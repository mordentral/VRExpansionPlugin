// Fill out your copyright notice in the Description page of Project Settings.

#include "Grippables/GrippablePhysicsReplication.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"

// I cannot dynamic cast without RTTI so I am using a static var as a declarative in case the user removed our custom replicator
// We don't want our casts to cause issues.
namespace VRPhysicsReplicationStatics
{
	static bool bHasVRPhysicsReplication = false;
}

FPhysicsReplicationVR::FPhysicsReplicationVR(FPhysScene* PhysScene) :
	FPhysicsReplication(PhysScene)
{
	VRPhysicsReplicationStatics::bHasVRPhysicsReplication = true;

#if PHYSICS_INTERFACE_PHYSX
	const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
	if (VRSettings.MaxCCDPasses != 1)
	{
		if (PxScene * PScene = PhysScene->GetPxScene())
		{
			PScene->setCCDMaxPasses(VRSettings.MaxCCDPasses);
		}
	}

#endif
}

bool FPhysicsReplicationVR::IsInitialized()
{
	return VRPhysicsReplicationStatics::bHasVRPhysicsReplication;
}

void FPhysicsReplicationVR::OnTick(float DeltaSeconds, TMap<TWeakObjectPtr<UPrimitiveComponent>, FReplicatedPhysicsTarget>& ComponentsToTargets)
{
	// Skip all of the custom logic if we aren't the server
	if (const UWorld* World = GetOwningWorld())
	{
		if (World->GetNetMode() == ENetMode::NM_Client)
		{
			return FPhysicsReplication::OnTick(DeltaSeconds, ComponentsToTargets);
		}
	}

	const FRigidBodyErrorCorrection& PhysicErrorCorrection = UPhysicsSettings::Get()->PhysicErrorCorrection;

	// Get the ping between this PC & the server
	const float LocalPing = 0.0f;//GetLocalPing();

	/*float CurrentTimeSeconds = 0.0f;

	if (UWorld* OwningWorld = GetOwningWorld())
	{
		CurrentTimeSeconds = OwningWorld->GetTimeSeconds();
	}*/

	for (auto Itr = ComponentsToTargets.CreateIterator(); Itr; ++Itr)
	{

		// Its been more than half a second since the last update, lets cease using the target as a failsafe
		// Clients will never update with that much latency, and if they somehow are, then they are dropping so many
		// packets that it will be useless to use their data anyway
		/*if ((CurrentTimeSeconds - Itr.Value().ArrivedTimeSeconds) > 0.5f)
		{
			OnTargetRestored(Itr.Key().Get(), Itr.Value());
			Itr.RemoveCurrent();
		}
		else */if (UPrimitiveComponent* PrimComp = Itr.Key().Get())
		{
			bool bRemoveItr = false;

			if (FBodyInstance* BI = PrimComp->GetBodyInstance(Itr.Value().BoneName))
			{
				FReplicatedPhysicsTarget& PhysicsTarget = Itr.Value();
				FRigidBodyState& UpdatedState = PhysicsTarget.TargetState;
				bool bUpdated = false;
				if (AActor* OwningActor = PrimComp->GetOwner())
				{
					// Removed as this is server sided
					/*const ENetRole OwnerRole = OwningActor->GetLocalRole();
					const bool bIsSimulated = OwnerRole == ROLE_SimulatedProxy;
					const bool bIsReplicatedAutonomous = OwnerRole == ROLE_AutonomousProxy && PrimComp->bReplicatePhysicsToAutonomousProxy;
					if (bIsSimulated || bIsReplicatedAutonomous)*/


					// Deleted everything here, we will always be the server, I already filtered out clients to default logic
					{
						/*const*/ float OwnerPing = 0.0f;// GetOwnerPing(OwningActor, PhysicsTarget);

						/*if (UPlayer* OwningPlayer = OwningActor->GetNetOwningPlayer())
						{
							if (APlayerController* PlayerController = OwningPlayer->GetPlayerController(nullptr))
							{
								if (APlayerState* PlayerState = PlayerController->PlayerState)
								{
									OwnerPing = PlayerState->ExactPing;
								}
							}
						}*/

						// Get the total ping - this approximates the time since the update was
						// actually generated on the machine that is doing the authoritative sim.
						// NOTE: We divide by 2 to approximate 1-way ping from 2-way ping.
						const float PingSecondsOneWay = 0.0f;// (LocalPing + OwnerPing) * 0.5f * 0.001f;


						if (UpdatedState.Flags & ERigidBodyFlags::NeedsUpdate)
						{
							const bool bRestoredState = ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, PhysicErrorCorrection, PingSecondsOneWay);

							// Need to update the component to match new position.
							static const auto CVarSkipSkeletalRepOptimization = IConsoleManager::Get().FindConsoleVariable(TEXT("p.SkipSkeletalRepOptimization"));
							if (/*PhysicsReplicationCVars::SkipSkeletalRepOptimization*/CVarSkipSkeletalRepOptimization->GetInt() == 0 || Cast<USkeletalMeshComponent>(PrimComp) == nullptr)	//simulated skeletal mesh does its own polling of physics results so we don't need to call this as it'll happen at the end of the physics sim
							{
								PrimComp->SyncComponentToRBPhysics();
							}

							// Added a sleeping check from the input state as well, we always want to cease activity on sleep
							if (bRestoredState || ((UpdatedState.Flags & ERigidBodyFlags::Sleeping) != 0))
							{
								bRemoveItr = true;
							}
						}
					}
				}
			}

			if (bRemoveItr)
			{
				OnTargetRestored(Itr.Key().Get(), Itr.Value());
				Itr.RemoveCurrent();
			}
		}
	}

	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Phys Rep Tick!"));
	//FPhysicsReplication::OnTick(DeltaSeconds, ComponentsToTargets);
}

#if PHYSICS_INTERFACE_PHYSX
void FContactModifyCallbackVR::onContactModify(PxContactModifyPair* const pairs, PxU32 count)
{
	for (uint32 PairIdx = 0; PairIdx < count; PairIdx++)
	{
		const PxActor* PActor0 = pairs[PairIdx].actor[0];
		const PxActor* PActor1 = pairs[PairIdx].actor[1];
		check(PActor0 && PActor1);

		const PxRigidBody* PRigidBody0 = PActor0->is<PxRigidBody>();
		const PxRigidBody* PRigidBody1 = PActor1->is<PxRigidBody>();

		//physx::PxRigidActor* SyncActor;

		const FBodyInstance* BodyInst0 = FPhysxUserData::Get<FBodyInstance>(PActor0->userData);
		const FBodyInstance* BodyInst1 = FPhysxUserData::Get<FBodyInstance>(PActor1->userData);
		if (BodyInst0 == nullptr || BodyInst1 == nullptr)
		{
			continue;
		}

		if (BodyInst0->bContactModification && BodyInst1->bContactModification)
		{
			FRWScopeLock(RWAccessLock, FRWScopeLockType::SLT_ReadOnly);

			const FContactModBodyInstancePair* prop = ContactsToIgnore.FindByPredicate([&](const FContactModBodyInstancePair& it) {return (it.Actor1.SyncActor == PRigidBody0 && it.Actor2.SyncActor == PRigidBody1) || (it.Actor2.SyncActor == PRigidBody0 && it.Actor1.SyncActor == PRigidBody1);  });
			if (prop)
			{
				for (uint32 ContactPt = 0; ContactPt < pairs[PairIdx].contacts.size(); ContactPt++)
				{
					pairs[PairIdx].contacts.ignore(ContactPt);
				}
			}
		}
	}
}

void FCCDContactModifyCallbackVR::onCCDContactModify(PxContactModifyPair* const pairs, PxU32 count)
{
	for (uint32 PairIdx = 0; PairIdx < count; PairIdx++)
	{
		const PxActor* PActor0 = pairs[PairIdx].actor[0];
		const PxActor* PActor1 = pairs[PairIdx].actor[1];
		check(PActor0 && PActor1);

		const PxRigidBody* PRigidBody0 = PActor0->is<PxRigidBody>();
		const PxRigidBody* PRigidBody1 = PActor1->is<PxRigidBody>();

		//physx::PxRigidActor* SyncActor;

		const FBodyInstance* BodyInst0 = FPhysxUserData::Get<FBodyInstance>(PActor0->userData);
		const FBodyInstance* BodyInst1 = FPhysxUserData::Get<FBodyInstance>(PActor1->userData);

		if (BodyInst0 == nullptr || BodyInst1 == nullptr)
		{
			continue;
		}

		if (BodyInst0->bContactModification && BodyInst1->bContactModification)
		{
			FRWScopeLock(RWAccessLock, FRWScopeLockType::SLT_ReadOnly);

			const FContactModBodyInstancePair* prop = ContactsToIgnore.FindByPredicate([&](const FContactModBodyInstancePair& it) {return (it.Actor1.SyncActor == PRigidBody0 && it.Actor2.SyncActor == PRigidBody1) || (it.Actor2.SyncActor == PRigidBody0 && it.Actor1.SyncActor == PRigidBody1);  });
			if (prop)
			{
				for (uint32 ContactPt = 0; ContactPt < pairs[PairIdx].contacts.size(); ContactPt++)
				{
					pairs[PairIdx].contacts.ignore(ContactPt);
				}
			}
		}
	}
}
#endif

FRepMovementVR::FRepMovementVR() : FRepMovement()
{
	LocationQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
	VelocityQuantizationLevel = EVectorQuantization::RoundTwoDecimals;
	RotationQuantizationLevel = ERotatorQuantization::ShortComponents;
}

FRepMovementVR::FRepMovementVR(FRepMovement& other) : FRepMovement()
{
	FRepMovementVR();

	LinearVelocity = other.LinearVelocity;
	AngularVelocity = other.AngularVelocity;
	Location = other.Location;
	Rotation = other.Rotation;
	bSimulatedPhysicSleep = other.bSimulatedPhysicSleep;
	bRepPhysics = other.bRepPhysics;
}

void FRepMovementVR::CopyTo(FRepMovement& other) const
{
	other.LinearVelocity = LinearVelocity;
	other.AngularVelocity = AngularVelocity;
	other.Location = Location;
	other.Rotation = Rotation;
	other.bSimulatedPhysicSleep = bSimulatedPhysicSleep;
	other.bRepPhysics = bRepPhysics;
}

bool FRepMovementVR::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	return FRepMovement::NetSerialize(Ar, Map, bOutSuccess);
}

bool FRepMovementVR::GatherActorsMovement(AActor* OwningActor)
{
	//if (/*bReplicateMovement || (RootComponent && RootComponent->GetAttachParent())*/)
	{
		UPrimitiveComponent* RootPrimComp = Cast<UPrimitiveComponent>(OwningActor->GetRootComponent());
		if (RootPrimComp && RootPrimComp->IsSimulatingPhysics())
		{
			FRigidBodyState RBState;
			RootPrimComp->GetRigidBodyState(RBState);

			FillFrom(RBState, OwningActor);
			// Don't replicate movement if we're welded to another parent actor.
			// Their replication will affect our position indirectly since we are attached.
			bRepPhysics = !RootPrimComp->IsWelded();
		}
		else if (RootPrimComp != nullptr)
		{
			// If we are attached, don't replicate absolute position, use AttachmentReplication instead.
			if (RootPrimComp->GetAttachParent() != nullptr)
			{
				return false; // We don't handle attachment rep

			}
			else
			{
				Location = FRepMovement::RebaseOntoZeroOrigin(RootPrimComp->GetComponentLocation(), OwningActor);
				Rotation = RootPrimComp->GetComponentRotation();
				LinearVelocity = OwningActor->GetVelocity();
				AngularVelocity = FVector::ZeroVector;
			}

			bRepPhysics = false;
		}
	}

	/*if (const UWorld* World = GetOwningWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (APlayerState* PlayerState = PlayerController->PlayerState)
			{
				CurrentPing = PlayerState->ExactPing;
			}
		}
	}*/

	return true;
}