// Fill out your copyright notice in the Description page of Project Settings.

#include "GrippablePhysicsReplication.h"
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
}

UVRReplicationInterface::UVRReplicationInterface(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool IVRReplicationInterface::AddObjectToReplicationManager(uint32 UpdateHTZ, UObject * ObjectToAdd)
{
	if (!VRPhysicsReplicationStatics::bHasVRPhysicsReplication || !ObjectToAdd)
		return false;

	if (UWorld * OurWorld = ObjectToAdd->GetWorld())
	{
		if (FPhysScene *  PhysicsScene = OurWorld->GetPhysicsScene())
		{
			FPhysicsReplicationVR * PhysRep = ((FPhysicsReplicationVR *)PhysicsScene->GetPhysicsReplication());
			return PhysRep->BucketContainer.AddReplicatingObject(UpdateHTZ, ObjectToAdd);
		}
	}

	return false;
}

bool IVRReplicationInterface::RemoveObjectFromReplicationManager(UObject * ObjectToRemove)
{
	if (!VRPhysicsReplicationStatics::bHasVRPhysicsReplication || !ObjectToRemove)
		return false;
	
	if (UWorld * OurWorld = ObjectToRemove->GetWorld())
	{
		if (FPhysScene *  PhysicsScene = OurWorld->GetPhysicsScene())
		{
			FPhysicsReplicationVR * PhysRep = ((FPhysicsReplicationVR *)PhysicsScene->GetPhysicsReplication());
			return PhysRep->BucketContainer.RemoveReplicatingObject(ObjectToRemove);
		}
	}

	return false;
}