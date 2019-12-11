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

#if WITH_PHYSX
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