// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Physics/PhysicsInterfaceUtils.h"
#include "PhysicsReplication.h"



#include "GrippablePhysicsReplication.generated.h"
//#include "GrippablePhysicsReplication.generated.h"


//DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRPhysicsReplicationDelegate, void, Return);

/*static TAutoConsoleVariable<int32> CVarEnableCustomVRPhysicsReplication(
	TEXT("vr.VRExpansion.EnableCustomVRPhysicsReplication"),
	0,
	TEXT("Enable valves input controller that overrides legacy input.\n")
	TEXT(" 0: use the engines default input mapping (default), will also be default if vr.SteamVR.EnableVRInput is enabled\n")
	TEXT(" 1: use the valve input controller. You will have to define input bindings for the controllers you want to support."),
	ECVF_ReadOnly);*/

//#if PHYSICS_INTERFACE_PHYSX
struct FAsyncPhysicsRepCallbackDataVR;
class FPhysicsReplicationAsyncCallbackVR;

class FPhysicsReplicationVR : public FPhysicsReplication
{
public:

	FPhysScene* PhysSceneVR;

	FPhysicsReplicationVR(FPhysScene* PhysScene);
	~FPhysicsReplicationVR();
	static bool IsInitialized();

	virtual void OnTick(float DeltaSeconds, TMap<TWeakObjectPtr<UPrimitiveComponent>, FReplicatedPhysicsTarget>& ComponentsToTargets) override;
	
	virtual bool ApplyRigidBodyState(float DeltaSeconds, FBodyInstance* BI, FReplicatedPhysicsTarget& PhysicsTarget, const FRigidBodyErrorCorrection& ErrorCorrection, const float PingSecondsOneWay, int32 LocalFrame, int32 NumPredictedFrames) override;
	virtual bool ApplyRigidBodyState(float DeltaSeconds, FBodyInstance* BI, FReplicatedPhysicsTarget& PhysicsTarget, const FRigidBodyErrorCorrection& ErrorCorrection, const float PingSecondsOneWay, bool* bDidHardSnap = nullptr) override;

	static void ApplyAsyncDesiredStateVR(float DeltaSeconds, const FAsyncPhysicsRepCallbackDataVR* Input);

	FPhysicsReplicationAsyncCallbackVR* AsyncCallbackServer;

	void PrepareAsyncData_ExternalVR(const FRigidBodyErrorCorrection& ErrorCorrection);	//prepare async data for writing. Call on external thread (i.e. game thread)
	FAsyncPhysicsRepCallbackDataVR* CurAsyncDataVR;	//async data being written into before we push into callback
	friend FPhysicsReplicationAsyncCallback;
};

class IPhysicsReplicationFactoryVR : public IPhysicsReplicationFactory
{
public:

	virtual FPhysicsReplication* Create(FPhysScene* OwningPhysScene)
	{
		return new FPhysicsReplicationVR(OwningPhysScene);
	}

	virtual void Destroy(FPhysicsReplication* PhysicsReplication)
	{
		if (PhysicsReplication)
			delete PhysicsReplication;
	}
};

//#endif

USTRUCT()
struct VREXPANSIONPLUGIN_API FRepMovementVR : public FRepMovement
{
	GENERATED_USTRUCT_BODY()
public:

	FRepMovementVR();
	FRepMovementVR(FRepMovement& other);	
	void CopyTo(FRepMovement& other) const;
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
	bool GatherActorsMovement(AActor* OwningActor);
};

template<>
struct TStructOpsTypeTraits<FRepMovementVR> : public TStructOpsTypeTraitsBase2<FRepMovementVR>
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
	};
};

USTRUCT(BlueprintType)
struct VREXPANSIONPLUGIN_API FVRClientAuthReplicationData
{
	GENERATED_BODY()
public:

	// If True and we are using a client auth grip type then we will replicate our throws on release
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRReplication")
		bool bUseClientAuthThrowing;

	// Rate that we will be sending throwing events to the server, not replicated, only serialized
	UPROPERTY(EditAnywhere, NotReplicated, BlueprintReadOnly, Category = "VRReplication", meta = (ClampMin = "0", UIMin = "0", ClampMax = "100", UIMax = "100"))
		int32 UpdateRate;

	FTimerHandle ResetReplicationHandle;
	FTransform LastActorTransform;
	float TimeAtInitialThrow;
	bool bIsCurrentlyClientAuth;

	FVRClientAuthReplicationData() :
		bUseClientAuthThrowing(false),
		UpdateRate(30),
		LastActorTransform(FTransform::Identity),
		TimeAtInitialThrow(0.0f),
		bIsCurrentlyClientAuth(false)
	{

	}
};
