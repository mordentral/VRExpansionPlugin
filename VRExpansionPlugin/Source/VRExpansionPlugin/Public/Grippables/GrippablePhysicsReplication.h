// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Physics/PhysicsInterfaceUtils.h"
#include "PhysicsReplication.h"

#include "Engine/ReplicatedState.h"
#include "PhysicsReplicationInterface.h"
#include "Physics/PhysicsInterfaceDeclares.h"
#include "PhysicsProxy/SingleParticlePhysicsProxyFwd.h"
#include "Chaos/PhysicsObject.h"
#include "Chaos/SimCallbackObject.h"
#include "Physics/NetworkPhysicsSettingsComponent.h"


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
//struct FAsyncPhysicsRepCallbackDataVR;
//class FPhysicsReplicationAsyncCallbackVR;

#pragma region FPhysicsReplicationAsync

class FPhysicsReplicationAsyncVR : public IPhysicsReplicationAsync,
	public Chaos::TSimCallbackObject<
	FPhysicsReplicationAsyncInput,
	Chaos::FSimCallbackNoOutput,
	Chaos::ESimCallbackOptions::Presimulate | Chaos::ESimCallbackOptions::PhysicsObjectUnregister>
{
	virtual FName GetFNameForStatId() const override;
	virtual void OnPostInitialize_Internal() override;
	virtual void OnPreSimulate_Internal() override;
	virtual void OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle PhysicsObject) override;

	virtual void ApplyTargetStatesAsync(const float DeltaSeconds);
	UE_DEPRECATED(5.6, "Deprecated, call the function with just @param DeltaSeconds instead.")
		virtual void ApplyTargetStatesAsync(const float DeltaSeconds, const FPhysicsRepErrorCorrectionData& ErrorCorrection, const TArray<FPhysicsRepAsyncInputData>& TargetStates) { ApplyTargetStatesAsync(DeltaSeconds); };

	// Replication functions
	virtual void DefaultReplication_DEPRECATED(Chaos::FRigidBodyHandle_Internal* Handle, const FPhysicsRepAsyncInputData& State, const float DeltaSeconds, const FPhysicsRepErrorCorrectionData& ErrorCorrection);
	virtual bool DefaultReplication(Chaos::FPBDRigidParticleHandle* Handle, FReplicatedPhysicsTargetAsync& Target, const float DeltaSeconds);
	virtual bool PredictiveInterpolation(Chaos::FPBDRigidParticleHandle* Handle, FReplicatedPhysicsTargetAsync& Target, const float DeltaSeconds);
	virtual bool ResimulationReplication(Chaos::FPBDRigidParticleHandle* Handle, FReplicatedPhysicsTargetAsync& Target, const float DeltaSeconds);

public:
	virtual void RegisterSettings(Chaos::FConstPhysicsObjectHandle PhysicsObject, FNetworkPhysicsSettingsAsync InSettings) override;

private:
	float LatencyOneWay;
	FRigidBodyErrorCorrection ErrorCorrectionDefault;
	FNetworkPhysicsSettingsAsync SettingsCurrent;
	FNetworkPhysicsSettingsAsync SettingsDefault;
	TMap<Chaos::FConstPhysicsObjectHandle, FReplicatedPhysicsTargetAsync> ObjectToTarget;
	TMap<Chaos::FConstPhysicsObjectHandle, FNetworkPhysicsSettingsAsync> ObjectToSettings;
	TArray<const Chaos::Private::FPBDIsland*> ResimIslands;
	TArray<const Chaos::FGeometryParticleHandle*> ResimIslandsParticles;
	TArray<int32> ParticlesInResimIslands;
	TArray<Chaos::FParticleID> ReplicatedParticleIDs;

private:
	FReplicatedPhysicsTargetAsync* AddObjectToReplication(Chaos::FConstPhysicsObjectHandle PhysicsObject);
	void RemoveObjectFromReplication(Chaos::FConstPhysicsObjectHandle PhysicsObject);
	void UpdateAsyncTarget(const FPhysicsRepAsyncInputData& Input, Chaos::FPBDRigidsSolver* RigidsSolver);
	void UpdateRewindDataTarget(const FPhysicsRepAsyncInputData& Input);
	void CacheResimInteractions();
	// Sets SettingsCurrent to either the objects custom settings or to the default settings
	void FetchObjectSettings(Chaos::FConstPhysicsObjectHandle PhysicsObject);
	bool UsePhysicsReplicationLOD();
	void CheckTargetResimValidity(FReplicatedPhysicsTargetAsync& Target);
	void ApplyPhysicsReplicationLOD(Chaos::FConstPhysicsObjectHandle PhysicsObjectHandle, FReplicatedPhysicsTargetAsync& Target, const uint32 LODFlags);
	void DebugDrawReplicationMode(const FPhysicsRepAsyncInputData& Input);

	/** Static function to extrapolate a target for N ticks using X DeltaSeconds */
	static void ExtrapolateTarget(FReplicatedPhysicsTargetAsync& Target, const int32 ExtrapolateFrames, const float DeltaSeconds);
	/** Static function to extrapolate a target for N Seconds */
	static void ExtrapolateTarget(FReplicatedPhysicsTargetAsync& Target, const float ExtrapolationTime);

public:
	void Setup(FRigidBodyErrorCorrection ErrorCorrection)
	{
		ErrorCorrectionDefault = ErrorCorrection;
	}
};

#pragma endregion // FPhysicsReplicationAsync

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

	void SetReplicatedTargetVR(Chaos::FConstPhysicsObjectHandle PhysicsObject, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame, EPhysicsReplicationMode ReplicationMode = EPhysicsReplicationMode::Default);

	
	virtual void RemoveReplicatedTarget(UPrimitiveComponent* Component) override;

	TArray<FReplicatedPhysicsTarget> ReplicatedTargetsQueueVR;
	FPhysicsReplicationAsyncVR* PhysicsReplicationAsyncVR;
	FPhysicsReplicationAsyncInput* AsyncInputVR;	//async data being written into before we push into callback
	TWeakObjectPtr<UNetworkPhysicsSettingsComponent> SettingsCurrent;

	void PrepareAsyncData_ExternalVR(const FRigidBodyErrorCorrection& ErrorCorrection);	//prepare async data for writing. Call on external thread (i.e. game thread)
};


class IPhysicsReplicationFactoryVR : public IPhysicsReplicationFactory
{
public:

	virtual TUniquePtr<IPhysicsReplication> CreatePhysicsReplication(FPhysScene* OwningPhysScene) override
	{
		return TUniquePtr<IPhysicsReplication>(new FPhysicsReplicationVR(OwningPhysScene));
	}

	/*virtual FPhysicsReplication* Create(FPhysScene* OwningPhysScene)
	{
		return new FPhysicsReplicationVR(OwningPhysScene);
	}

	virtual void Destroy(FPhysicsReplication* PhysicsReplication)
	{
		if (PhysicsReplication)
			delete PhysicsReplication;
	}*/
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