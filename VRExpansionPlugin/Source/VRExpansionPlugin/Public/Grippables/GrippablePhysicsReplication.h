// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGlobalSettings.h"
#include "Engine/Classes/GameFramework/PlayerController.h"
#include "Engine/Classes/GameFramework/PlayerState.h"
#include "Engine/Player.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Components/SkeletalMeshComponent.h"

#if PHYSICS_INTERFACE_PHYSX
#include "Physics/PhysScene_PhysX.h"
#include "PhysXPublicCore.h"
//#include "PhysXPublic.h"
#include "PhysXIncludes.h"
#endif

#include "Physics/PhysicsInterfaceUtils.h"
#include "PhysicsInterfaceTypesCore.h"
#include "PhysicsReplication.h"

#include "Misc/ScopeRWLock.h"

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
	virtual bool ApplyRigidBodyState(float DeltaSeconds, FBodyInstance* BI, FReplicatedPhysicsTarget& PhysicsTarget, const FRigidBodyErrorCorrection& ErrorCorrection, const float PingSecondsOneWay) override;
#if WITH_CHAOS

	static void ApplyAsyncDesiredStateVR(float DeltaSeconds, const FAsyncPhysicsRepCallbackDataVR* Input);

	FPhysicsReplicationAsyncCallbackVR* AsyncCallbackServer;

	void PrepareAsyncData_ExternalVR(const FRigidBodyErrorCorrection& ErrorCorrection);	//prepare async data for writing. Call on external thread (i.e. game thread)
	FAsyncPhysicsRepCallbackDataVR* CurAsyncDataVR;	//async data being written into before we push into callback
	friend FPhysicsReplicationAsyncCallback;
#endif
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

struct FContactModBodyInstancePair
{
	FPhysicsActorHandle Actor1;
	FPhysicsActorHandle Actor2;
	TWeakObjectPtr<UPrimitiveComponent> Prim1;
	TWeakObjectPtr<UPrimitiveComponent> Prim2;

	FORCEINLINE bool operator==(const FContactModBodyInstancePair &Other) const
	{
		return (
			(Actor1 == Other.Actor1 || Actor1 == Other.Actor2) &&
			(Actor2 == Other.Actor2 || Actor2 == Other.Actor1)
			);
	}
};

#if PHYSICS_INTERFACE_PHYSX
class FContactModifyCallbackVR : public FContactModifyCallback
{
public:

	TArray<FContactModBodyInstancePair> ContactsToIgnore;
	FRWLock RWAccessLock;

	void onContactModify(PxContactModifyPair* const pairs, PxU32 count) override;

	virtual ~FContactModifyCallbackVR()
	{

	}
};

class FCCDContactModifyCallbackVR : public FCCDContactModifyCallback
{
public:

	TArray<FContactModBodyInstancePair> ContactsToIgnore;
	FRWLock RWAccessLock;

	void onCCDContactModify(PxContactModifyPair* const pairs, PxU32 count) override;

	virtual ~FCCDContactModifyCallbackVR()
	{

	}
};

class IContactModifyCallbackFactoryVR : public IContactModifyCallbackFactory
{
public:

	virtual FContactModifyCallback* Create(FPhysScene* OwningPhysScene) override
	{
		return new FContactModifyCallbackVR();
	}

	virtual void Destroy(FContactModifyCallback* ContactCallback) override
	{
		if (ContactCallback)
			delete ContactCallback;
	}
};

class ICCDContactModifyCallbackFactoryVR : public ICCDContactModifyCallbackFactory
{
public:

	virtual FCCDContactModifyCallback* Create(FPhysScene* OwningPhysScene) override
	{
		return new FCCDContactModifyCallbackVR();
	}

	virtual void Destroy(FCCDContactModifyCallback* ContactCallback) override
	{
		if (ContactCallback)
			delete ContactCallback;
	}
};
#endif

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
