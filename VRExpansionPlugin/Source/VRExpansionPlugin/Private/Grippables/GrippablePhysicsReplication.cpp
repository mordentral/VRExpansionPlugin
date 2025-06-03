
/*=============================================================================
	PhysicsReplication.cpp: Code for keeping replicated physics objects in sync with the server based on replicated server state data.
	Copy / override of this class for VR client authority
=============================================================================*/


#include "Grippables/GrippablePhysicsReplication.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GrippablePhysicsReplication)

#include "CoreMinimal.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsReplicationLOD.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "DrawDebugHelpers.h"
#include "Chaos/ChaosMarshallingManager.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PhysicsObjectInternalInterface.h"

#include "PBDRigidsSolver.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "VRGlobalSettings.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Particles.h"
//#include "Components/SkeletalMeshComponent.h"
#include "Misc/ScopeRWLock.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/Player.h"
//#include "PhysicsInterfaceTypesCore.h"

// I cannot dynamic cast without RTTI so I am using a static var as a declarative in case the user removed our custom replicator
// We don't want our casts to cause issues.
namespace VRPhysicsReplicationStatics
{
	static bool bHasVRPhysicsReplication = false;
}

// Hacky work around for them not exporting these....
#if WITH_EDITOR

namespace RenderInterpolationCVars
{
	bool bRenderInterpDebugDrawResimTrigger = false;
}

namespace PhysicsReplicationCVars
{
	int32 SkipSkeletalRepOptimization = 1;
#if !UE_BUILD_SHIPPING
	//int32 LogPhysicsReplicationHardSnaps = 0;
#endif

	int32 EnableDefaultReplication = 0;
	int32 DebugDrawShowRepMode = 0;
	float DebugDrawLifeTime = 3.0f;


	namespace DefaultReplicationCVars
	{
		bool bHardsnapLegacyInPT = false;
		bool bCorrectConnectedBodies = false;
		bool bCorrectConnectedBodiesFriction = true;
	}

	namespace ResimulationCVars
	{
		//extern bool bApplyPredictiveInterpolationWhenBehindServer;

		bool bRuntimeCorrectionEnabled = false;
		bool bRuntimeVelocityCorrection = false;
		bool bRuntimeCorrectConnectedBodies = true;
		float PosStabilityMultiplier = 0.5f;
		float RotStabilityMultiplier = 1.0f;
		float VelStabilityMultiplier = 0.5f;
		float AngVelStabilityMultiplier = 0.5f;
		bool bDrawDebug = false;

		// Inside of NetworkPhysicsComponent - UPDATE AS CHANGE
		int32 RedundantInputs = 2;
		int32 RedundantRemoteInputs = 1;
		int32 RedundantStates = 0;
		bool bAllowRewindToClosestState = true;
		bool bCompareStateToTriggerRewind = false;
		bool bCompareStateToTriggerRewindIncludeSimProxies = false;
		bool bCompareInputToTriggerRewind = false;
		bool bEnableUnreliableFlow = true;
		bool bEnableReliableFlow = false;
		bool bApplyDataInsteadOfMergeData = false;
		bool bAllowInputExtrapolation = true;
		bool bValidateDataOnGameThread = false;
		bool bApplySimProxyStateAtRuntime = false;
		bool bApplySimProxyInputAtRuntime = true;
		bool bApplyPredictiveInterpolationWhenBehindServer = true;
	}

	namespace PredictiveInterpolationCVars
	{
		float PosCorrectionTimeBase = 0.0f;
		float PosCorrectionTimeMin = 0.1f;
		float PosCorrectionTimeMultiplier = 1.0f;
		float RotCorrectionTimeBase = 0.0f;
		float RotCorrectionTimeMin = 0.1f;
		float RotCorrectionTimeMultiplier = 1.0f;
		float PosInterpolationTimeMultiplier = 1.1f;
		float RotInterpolationTimeMultiplier = 1.25f;
		float AverageReceiveIntervalSmoothing = 3.0f;
		float ExtrapolationTimeMultiplier = 3.0f;
		float ExtrapolationMinTime = 0.75f;
		float MinExpectedDistanceCovered = 0.5f;
		float ErrorAccumulationDecreaseMultiplier = 0.5f;
		float ErrorAccumulationSeconds = 3.0f;
		bool bDisableErrorVelocityLimits = false;
		float ErrorAccLinVelMaxLimit = 50.0f;
		float ErrorAccAngVelMaxLimit = 1.5f;
		float SoftSnapPosStrength = 0.5f;
		float SoftSnapRotStrength = 0.5f;
		bool bSoftSnapToSource = false;
		float EarlyOutDistanceSqr = 1.0f;
		float EarlyOutAngle = 1.5f;
		bool bEarlyOutWithVelocity = true;
		bool bSkipVelocityRepOnPosEarlyOut = true;
		bool bPostResimWaitForUpdate = false;
		bool bVelocityBased = true;

		// New or re-named 5.5
		bool bCorrectionAsVelocity = false;
		bool bCorrectConnectedBodies = false;
		bool bCorrectConnectedBodiesFriction = true;
		bool bSleepConnectedBodies = true;
		bool bKinematicPrediction = true;
		bool bKinematicHardSnap = false;

		bool bDisableSoftSnap = false;
		bool bAlwaysHardSnap = false;
		bool bSkipReplication = false;
		bool bDontClearTarget = false;
		bool bDrawDebugTargets = false;
		bool bDrawDebugVectors = false;
		float DrawDebugZOffset = 50.0f;
		float SleepSecondsClearTarget = 15.0f;
		int32 TargetTickAlignmentClampMultiplier = 2;
		int32 TeleportDetectionEnabled = 1;
		float TeleportDetectionMinDistance = 200.0f;
		float TeleportDetectionVelocityMultiplier = 1.3f;
	}

}
#endif

/*struct FAsyncPhysicsRepCallbackDataVR : public Chaos::FSimCallbackInput
{
	TArray<FAsyncPhysicsDesiredState> Buffer;
	ErrorCorrectionData ErrorCorrection;

	void Reset()
	{
		Buffer.Reset();
	}
};*/

/*class FPhysicsReplicationAsyncCallbackVR final : public Chaos::TSimCallbackObject<FAsyncPhysicsRepCallbackDataVR>
{
	virtual void OnPreSimulate_Internal() override
	{
		FPhysicsReplicationVR::ApplyAsyncDesiredStateVR(GetDeltaTime_Internal(), GetConsumerInput_Internal());
	}
};*/

FPhysicsReplicationVR::~FPhysicsReplicationVR()
{
	if (PhysicsReplicationAsyncVR)
	{
		if (auto* Solver = PhysSceneVR->GetSolver())
		{
			Solver->UnregisterAndFreeSimCallbackObject_External(PhysicsReplicationAsyncVR);
		}
	}
}


void ComputeDeltasVR(const FVector& CurrentPos, const FQuat& CurrentQuat, const FVector& TargetPos, const FQuat& TargetQuat, FVector& OutLinDiff, float& OutLinDiffSize,
	FVector& OutAngDiffAxis, float& OutAngDiff, float& OutAngDiffSize)
{
	OutLinDiff = TargetPos - CurrentPos;
	OutLinDiffSize = OutLinDiff.Size();
	const FQuat InvCurrentQuat = CurrentQuat.Inverse();
	const FQuat DeltaQuat = InvCurrentQuat * TargetQuat;
	DeltaQuat.ToAxisAndAngle(OutAngDiffAxis, OutAngDiff);
	OutAngDiff = FMath::RadiansToDegrees(FMath::UnwindRadians(OutAngDiff));
	OutAngDiffSize = FMath::Abs(OutAngDiff);
}

FPhysicsReplicationVR::FPhysicsReplicationVR(FPhysScene* PhysScene) :
	FPhysicsReplication(PhysScene)
{
	PhysSceneVR = PhysScene;
	AsyncInputVR = nullptr;
	PhysicsReplicationAsyncVR = nullptr;
	if (auto* Solver = PhysSceneVR->GetSolver())
	{
		PhysicsReplicationAsyncVR = Solver->CreateAndRegisterSimCallbackObject_External<FPhysicsReplicationAsyncVR>();
		PhysicsReplicationAsyncVR->Setup(UPhysicsSettings::Get()->PhysicErrorCorrection);
	}

	//CurAsyncDataVR = nullptr;
	//AsyncCallbackServer = nullptr;

	VRPhysicsReplicationStatics::bHasVRPhysicsReplication = true;
}

void FPhysicsReplicationVR::PrepareAsyncData_ExternalVR(const FRigidBodyErrorCorrection& ErrorCorrection)
{
	//todo move this logic into a common function?
	static const auto CVarLinSet = IConsoleManager::Get().FindConsoleVariable(TEXT("p.PositionLerp"));
	const float PositionLerp = CVarLinSet->GetFloat() >= 0.0f ? CVarLinSet->GetFloat() : ErrorCorrection.PositionLerp;

	static const auto CVarLinLerp = IConsoleManager::Get().FindConsoleVariable(TEXT("p.LinearVelocityCoefficient"));
	const float LinearVelocityCoefficient = CVarLinLerp->GetFloat() >= 0.0f ? CVarLinLerp->GetFloat() : ErrorCorrection.LinearVelocityCoefficient;

	static const auto CVarAngSet = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AngleLerp"));
	const float AngleLerp = CVarAngSet->GetFloat() >= 0.0f ? CVarAngSet->GetFloat() : ErrorCorrection.AngleLerp;

	static const auto CVarAngLerp = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AngularVelocityCoefficient"));
	const float AngularVelocityCoefficient = CVarAngLerp->GetFloat() >= 0.0f ? CVarAngLerp->GetFloat() : ErrorCorrection.AngularVelocityCoefficient;

	AsyncInputVR = PhysicsReplicationAsyncVR->GetProducerInputData_External();
	AsyncInputVR->ErrorCorrection.PositionLerp = PositionLerp;
	AsyncInputVR->ErrorCorrection.AngleLerp = AngleLerp;
	AsyncInputVR->ErrorCorrection.LinearVelocityCoefficient = LinearVelocityCoefficient;
	AsyncInputVR->ErrorCorrection.AngularVelocityCoefficient = AngularVelocityCoefficient;
}

/*void FPhysicsReplicationVR::ApplyAsyncDesiredStateVR(const float DeltaSeconds, const FAsyncPhysicsRepCallbackDataVR* AsyncData)
{
	using namespace Chaos;
	if (AsyncData)
	{
		for (const FAsyncPhysicsDesiredState& State : AsyncData->Buffer)
		{
			float LinearVelocityCoefficient = AsyncData->ErrorCorrection.LinearVelocityCoefficient;
			float AngularVelocityCoefficient = AsyncData->ErrorCorrection.AngularVelocityCoefficient;
			float PositionLerp = AsyncData->ErrorCorrection.PositionLerp;
			float AngleLerp = AsyncData->ErrorCorrection.AngleLerp;
			if (State.ErrorCorrection.IsSet())
			{
				ErrorCorrectionData ECData = State.ErrorCorrection.GetValue();
				LinearVelocityCoefficient = ECData.LinearVelocityCoefficient;
				AngularVelocityCoefficient = ECData.AngularVelocityCoefficient;
				PositionLerp = ECData.PositionLerp;
				AngleLerp = ECData.AngleLerp;
			}

			//Proxy should exist because we are using latest and any pending deletes would have been enqueued after
			Chaos::FSingleParticlePhysicsProxy* Proxy = State.Proxy;
			auto* Handle = Proxy->GetPhysicsThreadAPI();


			if (Handle && Handle->CanTreatAsRigid())
			{
				const FVector TargetPos = State.WorldTM.GetLocation();
				const FQuat TargetQuat = State.WorldTM.GetRotation();

				// Get Current state
				FRigidBodyState CurrentState;
				CurrentState.Position = Handle->X();
				CurrentState.Quaternion = Handle->R();
				CurrentState.AngVel = Handle->W();
				CurrentState.LinVel = Handle->V();

				FVector LinDiff;
				float LinDiffSize;
				FVector AngDiffAxis;
				float AngDiff;
				float AngDiffSize;
				ComputeDeltasVR(CurrentState.Position, CurrentState.Quaternion, TargetPos, TargetQuat, LinDiff, LinDiffSize, AngDiffAxis, AngDiff, AngDiffSize);

				const FVector NewLinVel = FVector(State.LinearVelocity) + (LinDiff * LinearVelocityCoefficient * DeltaSeconds);
				const FVector NewAngVel = FVector(State.AngularVelocity) + (AngDiffAxis * AngDiff * AngularVelocityCoefficient * DeltaSeconds);

				const FVector NewPos = FMath::Lerp(FVector(CurrentState.Position), TargetPos, PositionLerp);
				const FQuat NewAng = FQuat::Slerp(CurrentState.Quaternion, TargetQuat, AngleLerp);

				Handle->SetX(NewPos);
				Handle->SetR(NewAng);
				Handle->SetV(NewLinVel);
				Handle->SetW(FMath::DegreesToRadians(NewAngVel));

				if (State.bShouldSleep)
				{
					// don't allow kinematic to sleeping transition
					if (Handle->ObjectState() != EObjectStateType::Kinematic)
					{
						auto* Solver = Proxy->GetSolver<FPBDRigidsSolver>();
						Solver->GetEvolution()->SetParticleObjectState(Proxy->GetHandle_LowLevel()->CastToRigidParticle(), EObjectStateType::Sleeping);	//todo: move object state into physics thread api
					}
				}
			}
		}
	}
}*/


bool FPhysicsReplicationVR::IsInitialized()
{
	return VRPhysicsReplicationStatics::bHasVRPhysicsReplication;
}


bool FPhysicsReplicationVR::ApplyRigidBodyState(float DeltaSeconds, FBodyInstance* BI, FReplicatedPhysicsTarget& PhysicsTarget, const FRigidBodyErrorCorrection& ErrorCorrection, const float InPingSecondsOneWay, int32 LocalFrame, int32 NumPredictedFrames)
{

	// Skip all of the custom logic if we aren't the server
	if (const UWorld* World = GetOwningWorld())
	{
		if (World->GetNetMode() == ENetMode::NM_Client)
		{
			return FPhysicsReplication::ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, ErrorCorrection, InPingSecondsOneWay, LocalFrame, NumPredictedFrames);
		}
	}

	// Call into the old ApplyRigidBodyState function for now,
	return ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, ErrorCorrection, InPingSecondsOneWay);
}

/*void FPhysicsReplicationVR::SetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame)
{

	// Skip all of the custom logic if we aren't the server
	if (const UWorld* World = GetOwningWorld())
	{
		if (World->GetNetMode() == ENetMode::NM_Client)
		{
			return FPhysicsReplication::SetReplicatedTarget(Component, BoneName, ReplicatedTarget, ServerFrame);
		}
	}

	static const auto CVarEnableDefaultReplication = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.EnableDefaultReplication"));

	// If networked physics prediction is enabled, enforce the new physics replication flow via SetReplicatedTarget() using PhysicsObject instead of BodyInstance from BoneName.
	AActor* Owner = Component->GetOwner();

	if (Owner && (CVarEnableDefaultReplication->GetBool() || Owner->GetPhysicsReplicationMode() != EPhysicsReplicationMode::Default)) // For now, only opt in to the PhysicsObject flow if not using Default replication or if default is allowed via CVar.
	{
		const ENetRole OwnerRole = Owner->GetLocalRole();
		const bool bIsSimulated = OwnerRole == ROLE_SimulatedProxy;
		const bool bIsReplicatedAutonomous = OwnerRole == ROLE_AutonomousProxy && Component->bReplicatePhysicsToAutonomousProxy;
		if (bIsSimulated || bIsReplicatedAutonomous)
		{
			Chaos::FConstPhysicsObjectHandle PhysicsObject = Component->GetPhysicsObjectByName(BoneName);
			SetReplicatedTargetVR(PhysicsObject, ReplicatedTarget, ServerFrame, Owner->GetPhysicsReplicationMode());
			return;
		}
	}

	return FPhysicsReplication::SetReplicatedTarget(Component, BoneName, ReplicatedTarget, ServerFrame);
}*/

void FPhysicsReplicationVR::SetReplicatedTargetVR(Chaos::FConstPhysicsObjectHandle PhysicsObject, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame, EPhysicsReplicationMode ReplicationMode)
{
	if (!PhysicsObject)
	{
		return;
	}

	UWorld* OwningWorld = GetOwningWorld();
	if (OwningWorld == nullptr)
	{
		return;
	}

	// TODO, Check if owning actor is ROLE_SimulatedProxy or ROLE_AutonomousProxy ?

	FReplicatedPhysicsTarget Target(PhysicsObject);

	Target.ReplicationMode = ReplicationMode;
	Target.ServerFrame = ServerFrame;
	Target.TargetState = ReplicatedTarget;
	Target.ArrivedTimeSeconds = OwningWorld->GetTimeSeconds();

	ensure(!Target.TargetState.Position.ContainsNaN());

	ReplicatedTargetsQueueVR.Add(Target);
}

void FPhysicsReplicationVR::RemoveReplicatedTarget(UPrimitiveComponent* Component)
{

	// Skip all of the custom logic if we aren't the server
	/*if (const UWorld* World = GetOwningWorld())
	{
		if (World->GetNetMode() == ENetMode::NM_Client)
		{
			return FPhysicsReplication::RemoveReplicatedTarget(Component);
		}
	}*/

	if (Component == nullptr)
	{
		return;
	}
	
	// Call super version to ensure its removed from the inaccessible deprecated targets list
	FPhysicsReplication::RemoveReplicatedTarget(Component);

	// Remove from legacy flow
	//ComponentToTargets_DEPRECATED.Remove(Component);

	// Remove from FPhysicsObject flow
	Chaos::FConstPhysicsObjectHandle PhysicsObject = Component->GetPhysicsObjectByName(NAME_None);
	FReplicatedPhysicsTarget Target(PhysicsObject); // This creates a new but empty target and when it tries to update the current target in the async flow it will remove it from replication since it's empty.
	ReplicatedTargetsQueueVR.Add(Target);
}

bool FPhysicsReplicationVR::ApplyRigidBodyState(float DeltaSeconds, FBodyInstance* BI, FReplicatedPhysicsTarget& PhysicsTarget, const FRigidBodyErrorCorrection& ErrorCorrection, const float PingSecondsOneWay, bool* bDidHardSnap)
{
	// Skip all of the custom logic if we aren't the server
	if (const UWorld* World = GetOwningWorld())
	{
		if (World->GetNetMode() == ENetMode::NM_Client)
		{
			return FPhysicsReplication::ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, ErrorCorrection, PingSecondsOneWay);
		}
	}

	static const auto CVarSkipPhysicsReplication = IConsoleManager::Get().FindConsoleVariable(TEXT("p.SkipPhysicsReplication"));
	if (CVarSkipPhysicsReplication->GetInt())
	{
		return false;
	}

	if (!BI->IsInstanceSimulatingPhysics())
	{
		return false;
	}

	//
	// NOTES:
	//
	// The operation of this method has changed since 4.18.
	//
	// When a new remote physics state is received, this method will
	// be called on tick until the local state is within an adequate
	// tolerance of the new state.
	//
	// The received state is extrapolated based on ping, by some
	// adjustable amount.
	//
	// A correction velocity is added new state's velocity, and assigned
	// to the body. The correction velocity scales with the positional
	// difference, so without the interference of external forces, this
	// will result in an exponentially decaying correction.
	//
	// Generally it is not needed and will interrupt smoothness of
	// the replication, but stronger corrections can be obtained by
	// adjusting position lerping.
	//
	// If progress is not being made towards equilibrium, due to some
	// divergence in physics states between the owning and local sims,
	// an error value is accumulated, representing the amount of time
	// spent in an unresolvable state.
	//
	// Once the error value has exceeded some threshold (0.5 seconds
	// by default), a hard snap to the target physics state is applied.
	//

	bool bRestoredState = true;
	const FRigidBodyState NewState = PhysicsTarget.TargetState;
	const float NewQuatSizeSqr = NewState.Quaternion.SizeSquared();

	// failure cases
	if (!BI->IsInstanceSimulatingPhysics())
	{
		UE_LOG(LogPhysics, Warning, TEXT("Physics replicating on non-simulated body. (%s)"), *BI->GetBodyDebugName());
		return bRestoredState;
	}
	else if (NewQuatSizeSqr < UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogPhysics, Warning, TEXT("Invalid zero quaternion set for body. (%s)"), *BI->GetBodyDebugName());
		return bRestoredState;
	}
	else if (FMath::Abs(NewQuatSizeSqr - 1.f) > UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogPhysics, Warning, TEXT("Quaternion (%f %f %f %f) with non-unit magnitude detected. (%s)"),
			NewState.Quaternion.X, NewState.Quaternion.Y, NewState.Quaternion.Z, NewState.Quaternion.W, *BI->GetBodyDebugName());
		return bRestoredState;
	}

	// Grab configuration variables from engine config or from CVars if overriding is turned on.
	static const auto CVarNetPingExtrapolation = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetPingExtrapolation"));
	const float NetPingExtrapolation = CVarNetPingExtrapolation->GetFloat() >= 0.0f ? CVarNetPingExtrapolation->GetFloat() : ErrorCorrection.PingExtrapolation;

	static const auto CVarNetPingLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetPingLimit"));
	const float NetPingLimit = CVarNetPingLimit->GetFloat() > 0.0f ? CVarNetPingLimit->GetFloat() : ErrorCorrection.PingLimit;

	static const auto CVarErrorPerLinearDifference = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorPerLinearDifference"));
	const float ErrorPerLinearDiff = CVarErrorPerLinearDifference->GetFloat() >= 0.0f ? CVarErrorPerLinearDifference->GetFloat() : ErrorCorrection.ErrorPerLinearDifference;
	
	static const auto CVarErrorPerAngularDifference = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorPerAngularDifference"));
	const float ErrorPerAngularDiff = CVarErrorPerAngularDifference->GetFloat() >= 0.0f ? CVarErrorPerAngularDifference->GetFloat() : ErrorCorrection.ErrorPerAngularDifference;
	
	static const auto CVarMaxRestoredStateError = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxRestoredStateError"));
	const float MaxRestoredStateError = CVarMaxRestoredStateError->GetFloat() >= 0.0f ? CVarMaxRestoredStateError->GetFloat() : ErrorCorrection.MaxRestoredStateError;
	
	static const auto CVarErrorAccumulation = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationSeconds"));
	const float ErrorAccumulationSeconds = CVarErrorAccumulation->GetFloat() >= 0.0f ? CVarErrorAccumulation->GetFloat() : ErrorCorrection.ErrorAccumulationSeconds;
	
	static const auto CVarErrorAccumulationDistanceSq = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationDistanceSq"));
	const float ErrorAccumulationDistanceSq = CVarErrorAccumulationDistanceSq->GetFloat() >= 0.0f ? CVarErrorAccumulationDistanceSq->GetFloat() : ErrorCorrection.ErrorAccumulationDistanceSq;
	
	static const auto CVarErrorAccumulationSimilarity = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationSimilarity"));
	const float ErrorAccumulationSimilarity = CVarErrorAccumulationSimilarity->GetFloat() >= 0.0f ? CVarErrorAccumulationSimilarity->GetFloat() : ErrorCorrection.ErrorAccumulationSimilarity;
	
	static const auto CVarLinSet = IConsoleManager::Get().FindConsoleVariable(TEXT("p.PositionLerp"));
	const float PositionLerp = CVarLinSet->GetFloat() >= 0.0f ? CVarLinSet->GetFloat() : ErrorCorrection.PositionLerp;

	static const auto CVarLinLerp = IConsoleManager::Get().FindConsoleVariable(TEXT("p.LinearVelocityCoefficient"));
	const float LinearVelocityCoefficient = CVarLinLerp->GetFloat() >= 0.0f ? CVarLinLerp->GetFloat() : ErrorCorrection.LinearVelocityCoefficient;

	static const auto CVarAngSet = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AngleLerp"));
	const float AngleLerp = CVarAngSet->GetFloat() >= 0.0f ? CVarAngSet->GetFloat() : ErrorCorrection.AngleLerp;

	static const auto CVarAngLerp = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AngularVelocityCoefficient"));
	const float AngularVelocityCoefficient = CVarAngLerp->GetFloat() >= 0.0f ? CVarAngLerp->GetFloat() : ErrorCorrection.AngularVelocityCoefficient;
	
	static const auto CVarMaxLinearHardSnapDistance = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxLinearHardSnapDistance"));
	float MaxLinearHardSnapDistance = CVarMaxLinearHardSnapDistance->GetFloat() >= 0.f ? CVarMaxLinearHardSnapDistance->GetFloat() : ErrorCorrection.MaxLinearHardSnapDistance;

	static const auto CVarHardsnapLegacyInPT = IConsoleManager::Get().FindConsoleVariable(TEXT("p.DefaultReplication.Legacy.HardsnapInPT"));
	bool bHardsnapLegacyInPT = CVarHardsnapLegacyInPT->GetBool();

	static const auto CVarCorrectConnectedBodies = IConsoleManager::Get().FindConsoleVariable(TEXT("p.DefaultReplication.CorrectConnectedBodies"));
	bool bCorrectConnectedBodies = CVarCorrectConnectedBodies->GetBool();

	static const auto CVarCorrectConnectedBodiesFriction = IConsoleManager::Get().FindConsoleVariable(TEXT("p.DefaultReplication.CorrectConnectedBodiesFriction"));
	bool bCorrectConnectedBodiesFriction = CVarCorrectConnectedBodiesFriction->GetBool();

	// Assign per-actor settings from NetworkPhysicSettingsComponent if this actor has one
	if (SettingsCurrent.Get())
	{
		MaxLinearHardSnapDistance = SettingsCurrent.Get()->DefaultReplicationSettings.GetMaxLinearHardSnapDistance(MaxLinearHardSnapDistance);
		bHardsnapLegacyInPT = SettingsCurrent.Get()->DefaultReplicationSettings.GetHardsnapDefaultLegacyInPT();
		bCorrectConnectedBodies = SettingsCurrent.Get()->DefaultReplicationSettings.GetCorrectConnectedBodies();
		bCorrectConnectedBodiesFriction = SettingsCurrent.Get()->DefaultReplicationSettings.GetCorrectConnectedBodiesFriction();
	}

	// Get Current state
	FRigidBodyState CurrentState;
	BI->GetRigidBodyState(CurrentState);

	/////// EXTRAPOLATE APPROXIMATE TARGET VALUES ///////

	// Starting from the last known authoritative position, and
	// extrapolate an approximation using the last known velocity
	// and ping.
	const float PingSeconds = FMath::Clamp(PingSecondsOneWay, 0.f, NetPingLimit);
	const float ExtrapolationDeltaSeconds = PingSeconds * NetPingExtrapolation;
	const FVector ExtrapolationDeltaPos = NewState.LinVel * ExtrapolationDeltaSeconds;
	const FVector_NetQuantize100 TargetPos = NewState.Position + ExtrapolationDeltaPos;
	float NewStateAngVel;
	FVector NewStateAngVelAxis;
	NewState.AngVel.FVector::ToDirectionAndLength(NewStateAngVelAxis, NewStateAngVel);
	NewStateAngVel = FMath::DegreesToRadians(NewStateAngVel);
	const FQuat ExtrapolationDeltaQuaternion = FQuat(NewStateAngVelAxis, NewStateAngVel * ExtrapolationDeltaSeconds);
	FQuat TargetQuat = ExtrapolationDeltaQuaternion * NewState.Quaternion;

	/////// COMPUTE DIFFERENCES ///////

	FVector LinDiff;
	float LinDiffSize;
	FVector AngDiffAxis;
	float AngDiff;

	float AngDiffSize;

	ComputeDeltasVR(CurrentState.Position, CurrentState.Quaternion, TargetPos, TargetQuat, LinDiff, LinDiffSize, AngDiffAxis, AngDiff, AngDiffSize);

	/////// ACCUMULATE ERROR IF NOT APPROACHING SOLUTION ///////

	// Store sleeping state
	const bool bShouldSleep = (NewState.Flags & ERigidBodyFlags::Sleeping) != 0;
	const bool bWasAwake = BI->IsInstanceAwake();
	const bool bAutoWake = false;

	const float Error = (LinDiffSize * ErrorPerLinearDiff) + (AngDiffSize * ErrorPerAngularDiff);
	bRestoredState = Error < MaxRestoredStateError;
	if (bRestoredState)
	{
		PhysicsTarget.AccumulatedErrorSeconds = 0.0f;
	}
	else
	{
		//
		// The heuristic for error accumulation here is:
		// 1. Did the physics tick from the previous step fail to
		//    move the body towards a resolved position?
		// 2. Was the linear error in the same direction as the
		//    previous frame?
		// 3. Is the linear error large enough to accumulate error?
		//
		// If these conditions are met, then "error" time will accumulate.
		// Once error has accumulated for a certain number of seconds,
		// a hard-snap to the target will be performed.
		//
		// TODO: Rotation while moving linearly can still mess up this
		// heuristic. We need to account for it.
		//

		// Project the change in position from the previous tick onto the
		// linear error from the previous tick. This value roughly represents
		// how much correction was performed over the previous physics tick.
		const float PrevProgress = FVector::DotProduct(
			FVector(CurrentState.Position) - PhysicsTarget.PrevPos,
			(PhysicsTarget.PrevPosTarget - PhysicsTarget.PrevPos).GetSafeNormal());

		// Project the current linear error onto the linear error from the
		// previous tick. This value roughly represents how little the direction
		// of the linear error state has changed, and how big the error is.
		const float PrevSimilarity = FVector::DotProduct(
			TargetPos - FVector(CurrentState.Position),
			PhysicsTarget.PrevPosTarget - PhysicsTarget.PrevPos);

		// If the conditions from the heuristic outlined above are met, accumulate
		// error. Otherwise, reduce it.
		if (PrevProgress < ErrorAccumulationDistanceSq &&
			PrevSimilarity > ErrorAccumulationSimilarity)
		{
			PhysicsTarget.AccumulatedErrorSeconds += DeltaSeconds;
		}
		else
		{
			PhysicsTarget.AccumulatedErrorSeconds = FMath::Max(PhysicsTarget.AccumulatedErrorSeconds - DeltaSeconds, 0.0f);
		}

		// Hard snap if error accumulation or linear error is big enough, and clear the error accumulator.
		static const auto CVarAlwaysHardSnap = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AlwaysHardSnap"));
		const bool bHardSnap =
			LinDiffSize > MaxLinearHardSnapDistance ||
			PhysicsTarget.AccumulatedErrorSeconds > ErrorAccumulationSeconds ||
			CVarAlwaysHardSnap->GetInt();

		const FTransform IdealWorldTM(TargetQuat, TargetPos);

		if (bHardSnap)
		{
#if !UE_BUILD_SHIPPING
			if (PhysicsReplicationCVars::LogPhysicsReplicationHardSnaps && GetOwningWorld())
			{
				UE_LOG(LogTemp, Warning, TEXT("Simulated HARD SNAP - \nCurrent Pos - %s, Target Pos - %s\n CurrentState.LinVel - %s, New Lin Vel - %s\nTarget Extrapolation Delta - %s, Is Replay? - %d, Is Asleep - %d, Prev Progress - %f, Prev Similarity - %f"),
					*CurrentState.Position.ToString(), *TargetPos.ToString(), *CurrentState.LinVel.ToString(), *NewState.LinVel.ToString(),
					*ExtrapolationDeltaPos.ToString(), GetOwningWorld()->IsPlayingReplay(), !BI->IsInstanceAwake(), PrevProgress, PrevSimilarity);
				if (bDidHardSnap)
				{
					*bDidHardSnap = true;
				}
				if (LinDiffSize > MaxLinearHardSnapDistance)
				{
					UE_LOG(LogTemp, Warning, TEXT("Hard snap due to linear difference error"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Hard snap due to accumulated error"))
				}
			}
#endif
			// Too much error so just snap state here and be done with it
			PhysicsTarget.AccumulatedErrorSeconds = 0.0f;
			bRestoredState = true;
			// Hardsnap in physics thread
			bool bPTHardSnapSuccess = false;

			if (PhysicsReplicationAsyncVR != nullptr)
			{
				if (bHardsnapLegacyInPT)
				{
					if (Chaos::FSingleParticlePhysicsProxy* Proxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(BI->GetPhysicsActor()))
					{
						if (Chaos::FPBDRigidsSolver* Solver = Proxy->GetSolver<Chaos::FPBDRigidsSolver>())
						{
							Solver->EnqueueCommandImmediate([Solver, Proxy, IdealWorldTM, NewState, bCorrectConnectedBodies, bCorrectConnectedBodiesFriction]()
								{
									Chaos::FRigidBodyHandle_Internal* Handle = Proxy->GetPhysicsThreadAPI();

									// Set XRVW to hard snap dynamic object and force recalculation of friction
									Solver->GetEvolution()->ApplyParticleTransformCorrection(Proxy->GetHandle_LowLevel(), IdealWorldTM.GetLocation(), IdealWorldTM.GetRotation(), bCorrectConnectedBodies, bCorrectConnectedBodiesFriction);

									Handle->SetV(NewState.LinVel);
									Handle->SetW(FMath::DegreesToRadians(NewState.AngVel));
								});

							bPTHardSnapSuccess = true;
						}
					}
				}
			}

			if (!bPTHardSnapSuccess)
			{
				BI->SetBodyTransform(IdealWorldTM, ETeleportType::ResetPhysics, bAutoWake);

				// Set the new velocities
				BI->SetLinearVelocity(NewState.LinVel, false, bAutoWake);
				BI->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewState.AngVel), false, bAutoWake);
			}

			// Set the new velocities
			BI->SetLinearVelocity(NewState.LinVel, false, bAutoWake);
			BI->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewState.AngVel), false, bAutoWake);
		}
		else
		{
			// Small enough error to interpolate
			if (PhysicsReplicationAsyncVR == nullptr)	//sync case
			{
				const FVector NewLinVel = FVector(NewState.LinVel) + (LinDiff * LinearVelocityCoefficient * DeltaSeconds);
				const FVector NewAngVel = FVector(NewState.AngVel) + (AngDiffAxis * AngDiff * AngularVelocityCoefficient * DeltaSeconds);

				const FVector NewPos = FMath::Lerp(FVector(CurrentState.Position), FVector(TargetPos), PositionLerp);
				const FQuat NewAng = FQuat::Slerp(CurrentState.Quaternion, TargetQuat, AngleLerp);

				BI->SetBodyTransform(FTransform(NewAng, NewPos), ETeleportType::ResetPhysics);
				BI->SetLinearVelocity(NewLinVel, false);
				BI->SetAngularVelocityInRadians(FMath::DegreesToRadians(NewAngVel), false);
			}
			else
			{
				//If async is used, enqueue for callback
				FPhysicsRepAsyncInputData AsyncInputData(nullptr);
				AsyncInputData.TargetState = NewState;
				AsyncInputData.TargetState.Position = IdealWorldTM.GetLocation();
				AsyncInputData.TargetState.Quaternion = IdealWorldTM.GetRotation();
				AsyncInputData.Proxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(BI->GetPhysicsActor());
				AsyncInputData.ErrorCorrection = { ErrorCorrection.LinearVelocityCoefficient, ErrorCorrection.AngularVelocityCoefficient, ErrorCorrection.PositionLerp, ErrorCorrection.AngleLerp };

				AsyncInputData.LatencyOneWay = PingSeconds;


				AsyncInputVR->InputData.Add(AsyncInputData);
				/*FAsyncPhysicsDesiredState AsyncDesiredState;
				AsyncDesiredState.WorldTM = IdealWorldTM;
				AsyncDesiredState.LinearVelocity = NewState.LinVel;
				AsyncDesiredState.AngularVelocity = NewState.AngVel;
				AsyncDesiredState.Proxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(BI->GetPhysicsActorHandle());
				AsyncDesiredState.ErrorCorrection = { ErrorCorrection.LinearVelocityCoefficient, ErrorCorrection.AngularVelocityCoefficient, ErrorCorrection.PositionLerp, ErrorCorrection.AngleLerp };
				AsyncDesiredState.bShouldSleep = bShouldSleep;
				CurAsyncDataVR->Buffer.Add(AsyncDesiredState);*/
			}
		}

		// Should we show the async part?
#if !UE_BUILD_SHIPPING
		static const auto CVarNetShowCorrections = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetShowCorrections"));
		if (CVarNetShowCorrections->GetInt() != 0)
		{
			PhysicsTarget.ErrorHistory.bAutoAdjustMinMax = false;
			PhysicsTarget.ErrorHistory.MinValue = 0.0f;
			PhysicsTarget.ErrorHistory.MaxValue = 1.0f;
			PhysicsTarget.ErrorHistory.AddSample(PhysicsTarget.AccumulatedErrorSeconds / ErrorAccumulationSeconds);
			if (UWorld* OwningWorld = GetOwningWorld())
			{
				FColor Color = FColor::White;
				static const auto CVarNetCorrectionLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetCorrectionLifetime"));
				DrawDebugDirectionalArrow(OwningWorld, CurrentState.Position, TargetPos, 5.0f, Color, false, CVarNetCorrectionLifetime->GetFloat(), 0, 1.5f);
#if 0
				//todo: do we show this in async mode?
				DrawDebugFloatHistory(*OwningWorld, PhysicsTarget.ErrorHistory, NewPos + FVector(0.0f, 0.0f, 100.0f), FVector2D(100.0f, 50.0f), FColor::White);
#endif
			}
		}
#endif
	}

	/////// SLEEP UPDATE ///////

	if (bShouldSleep)
	{
		// In the async case, we apply sleep state in ApplyAsyncDesiredState
		if (PhysicsReplicationAsyncVR == nullptr)
		{
			BI->PutInstanceToSleep();
		}
	}

	PhysicsTarget.PrevPosTarget = TargetPos;
	PhysicsTarget.PrevPos = FVector(CurrentState.Position);

	return bRestoredState;
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

	using namespace Chaos;

	if (ShouldSkipPhysicsReplication())
	{
		return;
	}

	// Don't tick unless we have data to process
	if (ComponentsToTargets.Num() == 0 && ReplicatedTargetsQueueVR.Num() == 0)
	{
		return;
	}

	using namespace Chaos;


	int32 LocalFrameOffset = 0; // LocalFrame = ServerFrame + LocalFrameOffset;
	bool LocalFrameOffsetAssigned = false;
	if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction)
	{
		if (UWorld* World = GetOwningWorld())
		{
			if (World->GetNetMode() == NM_Client)
			{
				if (APlayerController* PlayerController = World->GetFirstPlayerController())
				{
					LocalFrameOffsetAssigned = PlayerController->GetNetworkPhysicsTickOffsetAssigned();
					LocalFrameOffset = PlayerController->GetNetworkPhysicsTickOffset();
				}
			}
		}
	}

	const FRigidBodyErrorCorrection& PhysicErrorCorrection = UPhysicsSettings::Get()->PhysicErrorCorrection;
	if (PhysicsReplicationAsyncVR)
	{
		PrepareAsyncData_ExternalVR(PhysicErrorCorrection);
	}

	// Get the ping between this PC & the server
	const float LocalPing = 0.0f;//GetLocalPing();

	for (auto Itr = ComponentsToTargets.CreateIterator(); Itr; ++Itr)
	{
		bool bRemoveItr = false;
		if (UPrimitiveComponent* PrimComp = Itr.Key().Get())
		{		
			if (PrimComp->GetAttachParent() == nullptr)
			{
				if (FBodyInstance* BI = PrimComp->GetBodyInstance(Itr.Value().BoneName))
				{
					FReplicatedPhysicsTarget& PhysicsTarget = Itr.Value();
					FRigidBodyState& UpdatedState = PhysicsTarget.TargetState;
					bool bUpdated = false;
					if (AActor* OwningActor = PrimComp->GetOwner())
					{
						// Removed as this is server sided
						/*
						// Update actor replication settings overrides
						SettingsCurrent = UNetworkPhysicsSettingsComponent::GetSettingsForActor(OwningActor);

						const ENetRole OwnerRole = OwningActor->GetLocalRole();
						const bool bIsSimulated = OwnerRole == ROLE_SimulatedProxy;
						const bool bIsReplicatedAutonomous = OwnerRole == ROLE_AutonomousProxy && PrimComp->bReplicatePhysicsToAutonomousProxy;
						if (bIsSimulated || bIsReplicatedAutonomous)*/

						// Deleted everything here, we will always be the server, I already filtered out clients to default logic
						{
							// Get the ping of this thing's owner. If nobody owns it,
							// then it's server authoritative.
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
								const int32 LocalFrame = PhysicsTarget.ServerFrame - LocalFrameOffset;
								const bool bRestoredState = ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, PhysicErrorCorrection, PingSecondsOneWay, LocalFrame, 0);

								// Need to update the component to match new position.
								static const auto CVarSkipSkeletalRepOptimization = IConsoleManager::Get().FindConsoleVariable(TEXT("p.SkipSkeletalRepOptimization"));
								if (/*PhysicsReplicationCVars::SkipSkeletalRepOptimization*/CVarSkipSkeletalRepOptimization->GetInt() == 0 || Cast<USkeletalMeshComponent>(PrimComp) == nullptr)	//simulated skeletal mesh does its own polling of physics results so we don't need to call this as it'll happen at the end of the physics sim
								{
									PrimComp->SyncComponentToRBPhysics();
								}
								if (bRestoredState)
								{
									bRemoveItr = true;
								}
							}
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

	// PhysicsObject replication flow
	for (FReplicatedPhysicsTarget& PhysicsTarget : ReplicatedTargetsQueueVR)
	{
		const float PingSecondsOneWay = LocalPing * 0.5f * 0.001f;

		// Queue up the target state for async replication
		FPhysicsRepAsyncInputData AsyncInputData(PhysicsTarget.PhysicsObject);
		AsyncInputData.TargetState = PhysicsTarget.TargetState;
		AsyncInputData.Proxy = nullptr;

		AsyncInputData.RepMode = PhysicsTarget.ReplicationMode;
		AsyncInputData.ServerFrame = PhysicsTarget.ServerFrame;

		AsyncInputData.LatencyOneWay = PingSecondsOneWay;

		if (LocalFrameOffsetAssigned)
		{
			AsyncInputData.FrameOffset = LocalFrameOffset;
		}

		AsyncInputVR->InputData.Add(AsyncInputData);
	}
	ReplicatedTargetsQueueVR.Reset();

	AsyncInputVR = nullptr;
}

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


#pragma region FPhysicsReplicationAsync

void FPhysicsReplicationAsyncVR::OnPhysicsObjectUnregistered_Internal(Chaos::FConstPhysicsObjectHandle PhysicsObject)
{
	RemoveObjectFromReplication(PhysicsObject);

	// Only clear Settings when PhysicsObject unregister (not when it stops replicating, hence why it's not baked into RemoveObjectFromReplication())
	ObjectToSettings.Remove(PhysicsObject);
}

void FPhysicsReplicationAsyncVR::RegisterSettings(Chaos::FConstPhysicsObjectHandle PhysicsObject, FNetworkPhysicsSettingsAsync InSettings)
{
	if (PhysicsObject != nullptr)
	{
		FNetworkPhysicsSettingsAsync& Settings = ObjectToSettings.FindOrAdd(PhysicsObject);
		Settings = InSettings;
	}
}

void FPhysicsReplicationAsyncVR::FetchObjectSettings(Chaos::FConstPhysicsObjectHandle PhysicsObject)
{
	FNetworkPhysicsSettingsAsync* CustomSettings = ObjectToSettings.Find(PhysicsObject);
	SettingsCurrent = (CustomSettings != nullptr) ? *CustomSettings : SettingsDefault;
}

void FPhysicsReplicationAsyncVR::OnPostInitialize_Internal()
{
	Chaos::FPBDRigidsSolver& RigidsSolver = GetSolver()->CastChecked();
	RigidsSolver.SetPhysicsReplication_Internal(this);
	//Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());

	/*if (ensure(RigidsSolver))
	{
		// This doesn't even do anything currently, nothing gets it #TODO: CHECK BACK ON THIS
		//RigidsSolver->SetPhysicsReplication(this);
	}*/
}

void FPhysicsReplicationAsyncVR::OnPreSimulate_Internal()
{
	if (FPhysicsReplication::ShouldSkipPhysicsReplication())
	{
		return;
	}

	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	check(RigidsSolver);

	// Early out if this is a resim frame
	Chaos::FRewindData* RewindData = RigidsSolver->GetRewindData();
	const bool bRewindDataExist = RewindData != nullptr;
	if (bRewindDataExist && RewindData->IsResim())
	{
		//static const auto CVarPostResimWaitForUpdate = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.PostResimWaitForUpdate"));
		// TODO, Handle the transition from post-resim to interpolation better (disabled by default, resim vs replication interaction is handled via FPhysicsReplicationAsync::CacheResimInteractions)
		if (SettingsCurrent.PredictiveInterpolationSettings.GetPostResimWaitForUpdate() && RewindData->IsFinalResim())
		{
			for (auto Itr = ObjectToTarget.CreateIterator(); Itr; ++Itr)
			{
				FReplicatedPhysicsTargetAsync& Target = Itr.Value();

				// If final resim frame, mark interpolated targets as waiting for up to date data from the server.
				if (Target.RepMode == EPhysicsReplicationMode::PredictiveInterpolation)
				{
					Target.SetWaiting(RigidsSolver->GetCurrentFrame() + Target.FrameOffset, Target.RepModeOverride);
				}
			}
		}
		return;
	}

	if (const FPhysicsReplicationAsyncInput* AsyncInput = GetConsumerInput_Internal())
	{
		// Update async targets with target input
		for (const FPhysicsRepAsyncInputData& Input : AsyncInput->InputData)
		{
			if (Input.TargetState.Flags == ERigidBodyFlags::None)
			{
				// Remove replication target
				RemoveObjectFromReplication(Input.PhysicsObject);
				continue;
			}

			if (!bRewindDataExist && Input.RepMode == EPhysicsReplicationMode::Resimulation)
			{
				// We don't have rewind data but an actor is set to replicate using resimulation; we need to enable rewind capture.
				if (ensure(Chaos::FPBDRigidsSolver::IsNetworkPhysicsPredictionEnabled() && RigidsSolver->IsUsingFixedDt()))
				{

					RigidsSolver->EnableRewindCapture();
				}
			}

			UpdateRewindDataTarget(Input);
			UpdateAsyncTarget(Input, RigidsSolver);

			DebugDrawReplicationMode(Input);

			// Deprecated, legacy BodyInstance flow for Default Replication
			if (Input.Proxy != nullptr)
			{
				Chaos::FSingleParticlePhysicsProxy* Proxy = Input.Proxy;
				Chaos::FRigidBodyHandle_Internal* Handle = Proxy->GetPhysicsThreadAPI();

				const FPhysicsRepErrorCorrectionData& UsedErrorCorrection = Input.ErrorCorrection.IsSet() ? Input.ErrorCorrection.GetValue() : AsyncInput->ErrorCorrection;
				DefaultReplication_DEPRECATED(Handle, Input, GetDeltaTime_Internal(), UsedErrorCorrection);
			}
		}
	}

	if (Chaos::FPBDRigidsSolver::IsNetworkPhysicsPredictionEnabled())
	{
		CacheResimInteractions();
	}

	ApplyTargetStatesAsync(GetDeltaTime_Internal());
}

FReplicatedPhysicsTargetAsync* FPhysicsReplicationAsyncVR::AddObjectToReplication(Chaos::FConstPhysicsObjectHandle PhysicsObject)
{
	if (ensure(PhysicsObject))
	{
		// Cache ParticleID in array of replicated objects
		Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
		if (Chaos::FGeometryParticleHandle* Handle = Interface.GetParticle(PhysicsObject))
		{
			ReplicatedParticleIDs.Add(Handle->ParticleID());
		}

		// Add to Object-Target map
		return &ObjectToTarget.Add(PhysicsObject, FReplicatedPhysicsTargetAsync());
	}
	return nullptr;
}

void FPhysicsReplicationAsyncVR::RemoveObjectFromReplication(Chaos::FConstPhysicsObjectHandle PhysicsObject)
{
	if (PhysicsObject == nullptr)
	{
		return;
	}

	// Remove from Object-Target map
	ObjectToTarget.Remove(PhysicsObject);

	// Remove cached replicated ParticleID
	Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
	if (Chaos::FGeometryParticleHandle* Handle = Interface.GetParticle(PhysicsObject))
	{
		ReplicatedParticleIDs.Remove(Handle->ParticleID());
	}
}

void FPhysicsReplicationAsyncVR::UpdateRewindDataTarget(const FPhysicsRepAsyncInputData& Input)
{
	if (Input.PhysicsObject == nullptr)
	{
		return;
	}

	// If there is no FrameOffset set then we have not synced up physics ticks with the server yet so don't cache this data
	if (Input.FrameOffset.IsSet() == false)
	{
		return;
	}

	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	if (RigidsSolver == nullptr)
	{
		return;
	}

	Chaos::FRewindData* RewindData = RigidsSolver->GetRewindData();
	if (RewindData == nullptr)
	{
		return;
	}

	Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
	if (Chaos::FGeometryParticleHandle* Handle = Interface.GetParticle(Input.PhysicsObject))
	{
		// Cache all target states inside RewindData
		const int32 LocalFrame = Input.ServerFrame - *Input.FrameOffset;
		RewindData->SetTargetStateAtFrame(*Handle, LocalFrame, Chaos::FFrameAndPhase::EParticleHistoryPhase::PostPushData,
			Input.TargetState.Position, Input.TargetState.Quaternion,
			Input.TargetState.LinVel, FMath::DegreesToRadians(Input.TargetState.AngVel), (Input.TargetState.Flags & ERigidBodyFlags::Sleeping));
	}
}

void FPhysicsReplicationAsyncVR::UpdateAsyncTarget(const FPhysicsRepAsyncInputData& Input, Chaos::FPBDRigidsSolver* RigidsSolver)
{
	if (Input.PhysicsObject == nullptr)
	{
		return;
	}

	FReplicatedPhysicsTargetAsync* Target = ObjectToTarget.Find(Input.PhysicsObject);
	bool bFirstTarget = Target == nullptr;
	if (bFirstTarget)
	{
		// First time we add a target, set previous state to current input
		Target = AddObjectToReplication(Input.PhysicsObject);
		Target->PrevPos = Input.TargetState.Position;
		Target->PrevPosTarget = Input.TargetState.Position;
		Target->PrevRotTarget = Input.TargetState.Quaternion;
		Target->PrevLinVel = Input.TargetState.LinVel;
		Target->RepModeOverride = Input.RepMode;
	}
	check(Target);

	/** Target Update Description
	* @param Input = incoming state target for replication.
	*
	* Input comes mainly from the server but can be a faked state produced by the client for example if the client object wakes up from sleeping.
	* Fake inputs should have a ServerFrame of -1 (bool bIsFake = Input.ServerFrame == -1)
	* Server inputs can have ServerFrame values of either 0 or an incrementing integer value.
	*	If the ServerFrame is 0 it should always be 0. If it's incrementing it will always increment.
	*
	* @local Target = The current state target used for replication, to be updated with data from Input.
	* Read about the different target properties in FReplicatedPhysicsTargetAsync
	*
	* IMPORTANT:
	* Target.ServerFrame can be -1 if the target is newly created or if it has data from a fake input.
	*
	* SendInterval is calculated by taking Input.ServerFrame - Target.ServerFrame
	*	Note, can only be calculated if the server is sending incrementing SendIntervals and if we have received a valid input previously so we have the previous ServerFrame cached in Target.
	*
	* ReceiveInterval is calculated by taking RigidsSolver->GetCurrentFrame() - Target.ReceiveFrame
	*	Note that ReceiveInterval is only used if SendInterval is 0
	*
	* Target.TickCount starts at 0 and is incremented each tick that the target is used for, TickCount is reset back to 0 each time Target is updated with new Input.
	*
	* NOTE: With perfect network conditions SendInterval, ReceiveInterval and Target.TickCount will be the same value.
	*/

	if ((bFirstTarget || Input.ServerFrame == 0 || Input.ServerFrame > Target->ServerFrame))
	{
		// Get the current physics frame
		const int32 CurrentFrame = RigidsSolver->GetCurrentFrame();

		// Cache TickCount before updating it, force to 0 if ServerFrame is -1
		const int32 PrevTickCount = (Target->ServerFrame < 0) ? 0 : Target->TickCount;

		// Cache SendInterval, only calculate if we have a valid Target->ServerFrame, else leave at 0.
		const int32 SendInterval = (Target->ServerFrame <= 0) ? 0 : Input.ServerFrame - Target->ServerFrame;

		// Cache if this target was previously allowed to be altered, before this update
		const bool bPrevAllowTargetAltering = Target->bAllowTargetAltering;

		// Cache if the physics frame offset has changed since last target
		const bool bFrameOffsetCorrected = Target->FrameOffset != Input.FrameOffset;

		// Set if the target is allowed to be altered after this update
		Target->bAllowTargetAltering = !(Target->TargetState.Flags & ERigidBodyFlags::Sleeping) && !(Input.TargetState.Flags & ERigidBodyFlags::Sleeping);

		// Cache previous linear velocity
		const FVector PrevLinVel = Target->TargetState.LinVel;

		// Set Target->ReceiveInterval from either SendInterval or the number of physics ticks between receiving input states
		if (SendInterval > 0)
		{
			Target->ReceiveInterval = SendInterval;
		}
		else
		{
			const int32 PrevReceiveFrame = Target->ReceiveFrame < 0 ? (CurrentFrame - 1) : Target->ReceiveFrame;
			Target->ReceiveInterval = (CurrentFrame - PrevReceiveFrame);
		}

		// Update target from input and reset properties
		Target->ServerFrame = Input.ServerFrame;
		Target->ReceiveFrame = CurrentFrame;
		Target->TargetState = Input.TargetState;
		Target->RepMode = Input.RepMode;
		Target->FrameOffset = Input.FrameOffset.IsSet() ? *Input.FrameOffset : 0;
		Target->TickCount = 0;
		Target->AccumulatedSleepSeconds = 0.0f;

		// Update waiting state
		Target->UpdateWaiting(Input.ServerFrame);

		// Apply full Replication LOD on received target
		ApplyPhysicsReplicationLOD(Input.PhysicsObject, *Target, EPhysicsReplicationLODFlags::LODFlag_All);

		// Check if target is valid to use for resimulation and perform actions if not
		CheckTargetResimValidity(*Target);

		if (Target->RepMode == EPhysicsReplicationMode::PredictiveInterpolation)
		{

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			static const auto CVarDrawDebugTargets = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DrawDebugTargets"));
			if (CVarDrawDebugTargets->GetBool())
			{
				const FVector Offset = FVector(0.0f, 0.0f, 50.0f);
				// Port this?
				//const FVector Offset = FVector(0.0f, 0.0f, PhysicsReplicationCVars::PredictiveInterpolationCVars::DrawDebugZOffset);
				static const auto CVarNetCorrectionLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Net.DebugDraw.LifeTime"));
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Input.TargetState.Position + Offset, FVector(15.0f, 15.0f, 15.0f), Input.TargetState.Quaternion, FColor::MakeRandomSeededColor(Input.ServerFrame), false, CVarNetCorrectionLifetime->GetFloat(), 0, 1.0f);
			}
#endif

			// TickCount is 0 by default at this point and when LOD is used, TickCount will be 0 if no LOD alignment was performed, in this case perform the normal target alignment
			if (Target->TickCount == 0)
			{
				/** Target Alignment Feature
				* With variable network conditions state inputs from the server can arrive both later or earlier than expected.
				* Target Alignment can adjust for this to make replication act on a target in the timeline that the client is currently replicating in.
				*
				* If SendInterval is 4 we expect TickCount to be 4. TickCount - SendInterval = 0, meaning the client and server has ticked physics the same amount between the target states.
				*
				* If SendInterval is 4 and TickCount is 2 we have only simulated physics for 2 ticks with the previous target while the server had simulated physics 4 ticks between previous target and new target
				*	TickCount - SendInterval = -2
				*	To align this we need to adjust the new target by predicting backwards by 2 ticks, else the replication will start replicating towards a state that is 2 ticks further ahead than expected, making replication speed up.
				*
				* Same goes for vice-versa:
				* If SendInterval is 4 and TickCount is 6 we have simulated physics for 6 ticks with the previous target while the server had simulated physics 4 ticks between previous target and new target
				*	TickCount - SendInterval = 2
				*	To align this we need to adjust the new target by predicting forwards by 2 ticks, else the replication will start replicating towards a state that is 2 ticks behind than expected, making replication slow down.
				*
				* Note that state inputs from the server can arrive fluctuating between above examples, but over time the alignment is evened out to 0.
				* If the clients latency is raised or lowered since replication started there might be a consistent offset in the TickCount which is handled by TimeDilation of client physics through APlayerController::UpdateServerAsyncPhysicsTickOffset()
				*/

				// Run target alignment if we have been allowed to alter the target during the last two target updates
				if (!bFirstTarget && bPrevAllowTargetAltering && Target->bAllowTargetAltering && !bFrameOffsetCorrected)
				{
					static const auto CVarTargetTickAlignmentClampMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.TargetTickAlignmentClampMultiplier"));
					const int32 AdjustedAverageReceiveInterval = FMath::CeilToInt(Target->AverageReceiveInterval) * CVarTargetTickAlignmentClampMultiplier->GetInt();

					// Set the TickCount to the physics tick offset value from where we expected this target to arrive.
					// If the client has ticked 2 times ahead from the last target and this target is 3 ticks in front of the previous target then the TickOffset should be -1
					Target->TickCount = FMath::Clamp(PrevTickCount - Target->ReceiveInterval, -AdjustedAverageReceiveInterval, AdjustedAverageReceiveInterval);

					// Apply target alignment if we aren't waiting for a newer state from the server
					if (!Target->IsWaiting())
					{
						FPhysicsReplicationAsyncVR::ExtrapolateTarget(*Target, Target->TickCount, GetDeltaTime_Internal());
					}
				}
			}

			static const auto CVarTeleportDetectionEnabled = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.TeleportDetection.Enabled"));
			// Teleport detection, we don't have specific data that tells us a teleport has happened on the server, so try to detect it by examining the previous and next state
			if (CVarTeleportDetectionEnabled->GetBool() == 1 && !bFirstTarget && SendInterval > 0 && RigidsSolver->IsUsingFixedDt())
			{
				const FVector PosOffset = (Input.TargetState.Position - Target->PrevPosTarget);
				static const auto CVarTeleportDetectionMinDistance = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.TeleportDetection.MinDistance"));
				if (PosOffset.SizeSquared() > (CVarTeleportDetectionMinDistance->GetFloat() * CVarTeleportDetectionMinDistance->GetFloat()))
				{
					const FVector Velocity = Input.TargetState.LinVel.SizeSquared() > PrevLinVel.SizeSquared() ? Input.TargetState.LinVel : PrevLinVel;
					const float DeltaSeconds = (SendInterval * RigidsSolver->GetAsyncDeltaTime());
					static const auto CVarTeleportDetectionVelocityMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.TeleportDetection.VelocityMultiplier"));
					const float PossibleDistanceSquared = (Velocity * (DeltaSeconds * CVarTeleportDetectionVelocityMultiplier->GetFloat())).SizeSquared();

					if (PossibleDistanceSquared < PosOffset.SizeSquared())
					{
						// A teleport has most likely happened, set accumulated error seconds to above limit for hard snapping
						// TODO: Don't piggyback on AccumulatedErrorSeconds (potentially implement ERigidBodyFlags::Teleported)

						static const auto CVarErrorAcumulationSeconds = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationSeconds"));
						Target->AccumulatedErrorSeconds = CVarErrorAcumulationSeconds->GetFloat() + 1.0f;
					}
				}
			}

			// Cache the position we received this target at, Predictive Interpolation will alter the target state but use this as the source position for reconciliation.
			Target->PrevPosTarget = Input.TargetState.Position;
			Target->PrevRotTarget = Input.TargetState.Quaternion;
		}
	}

	/** Cache the latest ping time */
	LatencyOneWay = Input.LatencyOneWay;
}


void FPhysicsReplicationAsyncVR::CacheResimInteractions()
{
	static const auto CVarResimDisableReplicationOnInteraction = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.DisableReplicationOnInteraction"));
	if (!CVarResimDisableReplicationOnInteraction->GetBool())
	{
		ParticlesInResimIslands.Empty();
		return;
	}

	if (UsePhysicsReplicationLOD())
	{
		// This will be handled by the LOD system
		ParticlesInResimIslands.Empty();
		return;
	}

	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	if (RigidsSolver == nullptr)
	{
		return;
	}

	ResimIslands.Reset();
	ResimIslandsParticles.Reset();
	ParticlesInResimIslands.Reset();

	Chaos::Private::FPBDIslandManager& IslandManager = RigidsSolver->GetEvolution()->GetIslandManager();
	Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
	for (auto Itr = ObjectToTarget.CreateIterator(); Itr; ++Itr)
	{
		FReplicatedPhysicsTargetAsync& Target = Itr.Value();
		if (Target.RepMode == EPhysicsReplicationMode::Resimulation)
		{
			Chaos::FConstPhysicsObjectHandle& POHandle = Itr.Key();
			if (Chaos::FGeometryParticleHandle* Handle = Interface.GetParticle(POHandle))
			{
				// Get a list of particles from the same island as a resim particle is in, i.e. particles interacting with a resim particle
				IslandManager.FindParticleIslands(Handle, OUT ResimIslands);
				IslandManager.FindParticlesInIslands(ResimIslands, OUT ResimIslandsParticles);
				for (const Chaos::FGeometryParticleHandle* InteractParticle : ResimIslandsParticles)
				{
					ParticlesInResimIslands.Add(InteractParticle->GetHandleIdx());
				}
			}
		}
	}
}

void FPhysicsReplicationAsyncVR::ApplyTargetStatesAsync(const float DeltaSeconds)
{
	using namespace Chaos;

	/** Helper function to remove replicated target*/
	auto RemoveTargetHelper = [this](TMap<Chaos::FConstPhysicsObjectHandle, FReplicatedPhysicsTargetAsync>::TIterator Itr, FGeometryParticleHandle* Handle)
		{
			if (Handle)
			{
				ReplicatedParticleIDs.Remove(Handle->ParticleID());
			}
			Itr.RemoveCurrent();
		};

	// PhysicsObject flow
	Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
	for (TMap<Chaos::FConstPhysicsObjectHandle, FReplicatedPhysicsTargetAsync>::TIterator Itr = ObjectToTarget.CreateIterator(); Itr; ++Itr)
	{
		bool bRemoveItr = true; // Remove current cached replication target unless replication logic tells us to store it for next tick

		Chaos::FConstPhysicsObjectHandle& POHandle = Itr.Key();
		FGeometryParticleHandle* Handle = Interface.GetParticle(POHandle);
		if (!Handle)
		{
			RemoveTargetHelper(Itr, nullptr);
			continue;
		}

		FPBDRigidParticleHandle* RigidHandle = Handle->CastToRigidParticle();
		if (!RigidHandle)
		{
			RemoveTargetHelper(Itr, Handle);
			continue;
		}

		FReplicatedPhysicsTargetAsync& Target = Itr.Value();

		// Cache custom settings for this object if there are any
		FetchObjectSettings(POHandle);

		// Apply limited Replication LOD
		ApplyPhysicsReplicationLOD(POHandle, Target, EPhysicsReplicationLODFlags::LODFlag_IslandCheck);

		const EPhysicsReplicationMode RepMode = Target.IsWaiting() ? Target.RepModeOverride : Target.RepMode;
		switch (RepMode)
		{
		case EPhysicsReplicationMode::Default:
			bRemoveItr = DefaultReplication(RigidHandle, Target, DeltaSeconds);
			break;

		case EPhysicsReplicationMode::PredictiveInterpolation:
			bRemoveItr = PredictiveInterpolation(RigidHandle, Target, DeltaSeconds);
			break;

		case EPhysicsReplicationMode::Resimulation:
			bRemoveItr = ResimulationReplication(RigidHandle, Target, DeltaSeconds);
			break;
		}
		Target.TickCount++;

		if (bRemoveItr)
		{
			RemoveTargetHelper(Itr, RigidHandle);

		}
	}
}

void FPhysicsReplicationAsyncVR::CheckTargetResimValidity(FReplicatedPhysicsTargetAsync& Target)
{
	if (Target.RepMode != EPhysicsReplicationMode::Resimulation)
	{
		return;
	}

	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	if (RigidsSolver == nullptr)
	{
		return;
	}

	Chaos::FRewindData* RewindData = RigidsSolver->GetRewindData();
	if (RewindData == nullptr)
	{
		return;
	}

	const int32 LocalFrame = Target.ServerFrame - Target.FrameOffset;
	if (!RewindData->IsFrameWithinRewindHistory(LocalFrame))
	{

		static const auto CVarResimApplyPredictiveInterpolationWhenBehindServer = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.ApplyPredictiveInterpolationWhenBehindServer"));

		if (LocalFrame < RewindData->GetEarliestFrame_Internal())
		{
			// Client is far ahead of the server, switch over to Predictive Interpolation since it can't use incoming target states from the server to perform resimulations with

			Target.RepMode = EPhysicsReplicationMode::PredictiveInterpolation;
		}
		else if (CVarResimApplyPredictiveInterpolationWhenBehindServer->GetBool())
		{
			/** NOTE: If the server is ahead of the client we receive target states for frames we have not yet simulated on the client, target states are stored in FRewindData still though.
			* If PhysicsReplicationCVars::ResimulationCVars::bApplyPredictiveInterpolationWhenBehindServer is true switch over to using PredictiveInterpolation temporarily.
			* else FRewindData::CompareTargetsToLastFrame will check for already cached targets to resim with when the server has simulated the corresponding frame */

			Target.RepMode = EPhysicsReplicationMode::PredictiveInterpolation;
		}

		UE_LOG(LogPhysics, Warning, TEXT("FPhysicsReplication received target frame (%d) out of rewind data bounds (%d, %d) - %s - Target will use EPhysicsReplicationMode: %s"),
			LocalFrame, RewindData->GetEarliestFrame_Internal(), RewindData->CurrentFrame(), (LocalFrame < RewindData->GetEarliestFrame_Internal()) ? TEXT("Client is far ahead of the server, server might be dropping frames.") : TEXT("Client is behind the server, client might be dropping frames."), *UEnum::GetValueAsString(Target.RepMode));
	}
}

void FPhysicsReplicationAsyncVR::ApplyPhysicsReplicationLOD(Chaos::FConstPhysicsObjectHandle PhysicsObjectHandle, FReplicatedPhysicsTargetAsync& Target, const uint32 LODFLags)
{
	Chaos::FPBDRigidsSolver& RigidsSolver = GetSolver()->CastChecked();

	IPhysicsReplicationLODAsync* PhysRepLod = RigidsSolver.GetPhysicsReplicationLOD_Internal();
	if (!PhysRepLod || !PhysRepLod->IsEnabled())
	{
		return;
	}

	FPhysicsRepLodData* LodData = PhysRepLod->GetLODData_Internal(PhysicsObjectHandle, LODFLags);
	if (LodData && LodData->DataAssigned)
	{
		// Apply recommended replication mode
		Target.RepMode = LodData->ReplicationMode;

		if (Target.RepMode == EPhysicsReplicationMode::PredictiveInterpolation)
		{
			const bool bShouldSleep = (Target.TargetState.Flags & ERigidBodyFlags::Sleeping) != 0;
			int32 TargetClientFrame = (Target.ServerFrame - Target.FrameOffset);

			// If we use Predicitve Interpolation and we should not sleep and the aligned frame from LOD is ahead of the target, perform LOD aligment extrapolation
			if (!bShouldSleep && LodData->AlignedFrame > TargetClientFrame)
			{
				// Calculate how far to forward predict and extrapolate target by that amount
				const int32 FullPredictionFrames = RigidsSolver.GetCurrentFrame() - TargetClientFrame;
				const float FullPredictionTime = (FullPredictionFrames * GetDeltaTime_Internal());
				const float AlignedPredictionTime = FullPredictionTime - LodData->AlignedTime;
				FPhysicsReplicationAsyncVR::ExtrapolateTarget(Target, AlignedPredictionTime);

				// Update tick count based on LOD alignment
				Target.TickCount = LodData->AlignedFrame - TargetClientFrame;
			}
		}
	}
}

//** Async function for legacy replication flow that goes partially through GT to then finishes in PT in this function. */
void FPhysicsReplicationAsyncVR::DefaultReplication_DEPRECATED(Chaos::FRigidBodyHandle_Internal* Handle, const FPhysicsRepAsyncInputData& State, const float DeltaSeconds, const FPhysicsRepErrorCorrectionData& ErrorCorrection)
{
	if (Handle && Handle->CanTreatAsRigid())
	{
		const float LinearVelocityCoefficient = ErrorCorrection.LinearVelocityCoefficient;
		const float AngularVelocityCoefficient = ErrorCorrection.AngularVelocityCoefficient;
		const float PositionLerp = ErrorCorrection.PositionLerp;
		const float AngleLerp = ErrorCorrection.AngleLerp;

		const FVector TargetPos = State.TargetState.Position;
		const FQuat TargetQuat = State.TargetState.Quaternion;

		// Get Current state
		FRigidBodyState CurrentState;
		CurrentState.Position = Handle->X();
		CurrentState.Quaternion = Handle->R();
		CurrentState.AngVel = Handle->W();
		CurrentState.LinVel = Handle->V();

		FVector LinDiff;
		float LinDiffSize;
		FVector AngDiffAxis;
		float AngDiff;
		float AngDiffSize;
		ComputeDeltasVR(CurrentState.Position, CurrentState.Quaternion, TargetPos, TargetQuat, LinDiff, LinDiffSize, AngDiffAxis, AngDiff, AngDiffSize);

		const FVector NewLinVel = FVector(State.TargetState.LinVel) + (LinDiff * LinearVelocityCoefficient * DeltaSeconds);
		const FVector NewAngVel = FVector(State.TargetState.AngVel) + (AngDiffAxis * AngDiff * AngularVelocityCoefficient * DeltaSeconds);

		const FVector NewPos = FMath::Lerp(FVector(CurrentState.Position), TargetPos, PositionLerp);
		const FQuat NewAng = FQuat::Slerp(CurrentState.Quaternion, TargetQuat, AngleLerp);

		Handle->SetX(NewPos);
		Handle->SetR(NewAng);
		Handle->SetV(NewLinVel);
		Handle->SetW(FMath::DegreesToRadians(NewAngVel));

		if (State.TargetState.Flags & ERigidBodyFlags::Sleeping)
		{
			// don't allow kinematic to sleeping transition
			if (Handle->ObjectState() != Chaos::EObjectStateType::Kinematic)
			{
				Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
				if (RigidsSolver)
				{
					RigidsSolver->GetEvolution()->SetParticleObjectState(Handle->GetProxy()->GetHandle_LowLevel()->CastToRigidParticle(), Chaos::EObjectStateType::Sleeping);	//todo: move object state into physics thread api
				}
			}
		}
	}
}


/** Default replication, run in simulation tick */
bool FPhysicsReplicationAsyncVR::DefaultReplication(Chaos::FPBDRigidParticleHandle* Handle, FReplicatedPhysicsTargetAsync& Target, const float DeltaSeconds)
{
	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	if (RigidsSolver == nullptr)
	{
		return true;
	}

	static const auto CVarResimDisableReplicationOnInteraction = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.DisableReplicationOnInteraction"));
	if (CVarResimDisableReplicationOnInteraction->GetBool() && ParticlesInResimIslands.Contains(Handle->GetHandleIdx()))
	{
		return false;
	}

	//
	// NOTES:
	//
	// The operation of this method has changed since 4.18.
	//
	// When a new remote physics state is received, this method will
	// be called on tick until the local state is within an adequate
	// tolerance of the new state.
	//
	// The received state is extrapolated based on ping, by some
	// adjustable amount.
	//
	// A correction velocity is added new state's velocity, and assigned
	// to the body. The correction velocity scales with the positional
	// difference, so without the interference of external forces, this
	// will result in an exponentially decaying correction.
	//
	// Generally it is not needed and will interrupt smoothness of
	// the replication, but stronger corrections can be obtained by
	// adjusting position lerping.
	//
	// If progress is not being made towards equilibrium, due to some
	// divergence in physics states between the owning and local sims,
	// an error value is accumulated, representing the amount of time
	// spent in an unresolvable state.
	//
	// Once the error value has exceeded some threshold (0.5 seconds
	// by default), a hard snap to the target physics state is applied.
	//


	bool bRestoredState = true;
	const FRigidBodyState NewState = Target.TargetState;
	const float NewQuatSizeSqr = NewState.Quaternion.SizeSquared();


	const FString ObjectName
#if CHAOS_DEBUG_NAME
		= Handle && Handle->DebugName() ? *Handle->DebugName() : FString(TEXT(""));
#else
		= FString(TEXT(""));
#endif

	// failure cases
	if (Handle == nullptr)
	{
		UE_LOG(LogPhysics, Warning, TEXT("Trying to replicate rigid state for non-rigid particle. (%s)"), *ObjectName);
		return bRestoredState;
	}
	else if (NewQuatSizeSqr < UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogPhysics, Warning, TEXT("Invalid zero quaternion set for body. (%s)"), *ObjectName);
		return bRestoredState;
	}
	else if (FMath::Abs(NewQuatSizeSqr - 1.f) > UE_KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogPhysics, Warning, TEXT("Quaternion (%f %f %f %f) with non-unit magnitude detected. (%s)"),
			NewState.Quaternion.X, NewState.Quaternion.Y, NewState.Quaternion.Z, NewState.Quaternion.W, *ObjectName);
		return bRestoredState;
	}

	// Grab configuration variables from engine config or from CVars if overriding is turned on.
	static const auto CVarNetPingExtrapolation = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetPingExtrapolation"));
	const float NetPingExtrapolation = CVarNetPingExtrapolation->GetFloat() >= 0.0f ? CVarNetPingExtrapolation->GetFloat() : ErrorCorrectionDefault.PingExtrapolation;

	static const auto CVarNetPingLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetPingLimit"));
	const float NetPingLimit = CVarNetPingLimit->GetFloat() > 0.0f ? CVarNetPingLimit->GetFloat() : ErrorCorrectionDefault.PingLimit;

	static const auto CVarErrorPerLinearDifference = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorPerLinearDifference"));
	const float ErrorPerLinearDiff = CVarErrorPerLinearDifference->GetFloat() >= 0.0f ? CVarErrorPerLinearDifference->GetFloat() : ErrorCorrectionDefault.ErrorPerLinearDifference;

	static const auto CVarErrorPerAngularDifference = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorPerAngularDifference"));
	const float ErrorPerAngularDiff = CVarErrorPerAngularDifference->GetFloat() >= 0.0f ? CVarErrorPerAngularDifference->GetFloat() : ErrorCorrectionDefault.ErrorPerAngularDifference;

	static const auto CVarMaxRestoredStateError = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxRestoredStateError"));
	const float MaxRestoredStateError = CVarMaxRestoredStateError->GetFloat() >= 0.0f ? CVarMaxRestoredStateError->GetFloat() : ErrorCorrectionDefault.MaxRestoredStateError;

	static const auto CVarErrorAccumulation = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationSeconds"));
	const float ErrorAccumulationSeconds = CVarErrorAccumulation->GetFloat() >= 0.0f ? CVarErrorAccumulation->GetFloat() : ErrorCorrectionDefault.ErrorAccumulationSeconds;

	static const auto CVarErrorAccumulationDistanceSq = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationDistanceSq"));
	const float ErrorAccumulationDistanceSq = CVarErrorAccumulationDistanceSq->GetFloat() >= 0.0f ? CVarErrorAccumulationDistanceSq->GetFloat() : ErrorCorrectionDefault.ErrorAccumulationDistanceSq;

	static const auto CVarErrorAccumulationSimilarity = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationSimilarity"));
	const float ErrorAccumulationSimilarity = CVarErrorAccumulationSimilarity->GetFloat() >= 0.0f ? CVarErrorAccumulationSimilarity->GetFloat() : ErrorCorrectionDefault.ErrorAccumulationSimilarity;

	static const auto CVarLinSet = IConsoleManager::Get().FindConsoleVariable(TEXT("p.PositionLerp"));
	const float PositionLerp = CVarLinSet->GetFloat() >= 0.0f ? CVarLinSet->GetFloat() : ErrorCorrectionDefault.PositionLerp;

	static const auto CVarLinLerp = IConsoleManager::Get().FindConsoleVariable(TEXT("p.LinearVelocityCoefficient"));
	const float LinearVelocityCoefficient = CVarLinLerp->GetFloat() >= 0.0f ? CVarLinLerp->GetFloat() : ErrorCorrectionDefault.LinearVelocityCoefficient;

	static const auto CVarAngSet = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AngleLerp"));
	const float AngleLerp = CVarAngSet->GetFloat() >= 0.0f ? CVarAngSet->GetFloat() : ErrorCorrectionDefault.AngleLerp;

	static const auto CVarAngLerp = IConsoleManager::Get().FindConsoleVariable(TEXT("p.AngularVelocityCoefficient"));
	const float AngularVelocityCoefficient = CVarAngLerp->GetFloat() >= 0.0f ? CVarAngLerp->GetFloat() : ErrorCorrectionDefault.AngularVelocityCoefficient;

	static const auto CVarMaxLinearHardSnapDistance = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxLinearHardSnapDistance"));
	float MaxLinearHardSnapDistance = CVarMaxLinearHardSnapDistance->GetFloat() >= 0.f ? CVarMaxLinearHardSnapDistance->GetFloat() : ErrorCorrectionDefault.MaxLinearHardSnapDistance;
	MaxLinearHardSnapDistance = SettingsCurrent.DefaultReplicationSettings.GetMaxLinearHardSnapDistance(MaxLinearHardSnapDistance);

	// Get Current state
	FRigidBodyState CurrentState;
	CurrentState.Position = Handle->GetX();
	CurrentState.Quaternion = Handle->GetR();
	CurrentState.AngVel = Handle->GetW();
	CurrentState.LinVel = Handle->GetV();


	// Starting from the last known authoritative position, and
	// extrapolate an approximation using the last known velocity
	// and ping.
	const float PingSeconds = FMath::Clamp(LatencyOneWay, 0.f, NetPingLimit);
	const float ExtrapolationDeltaSeconds = PingSeconds * NetPingExtrapolation;
	const FVector ExtrapolationDeltaPos = NewState.LinVel * ExtrapolationDeltaSeconds;
	const FVector_NetQuantize100 TargetPos = NewState.Position + ExtrapolationDeltaPos;
	float NewStateAngVel;
	FVector NewStateAngVelAxis;
	NewState.AngVel.FVector::ToDirectionAndLength(NewStateAngVelAxis, NewStateAngVel);
	NewStateAngVel = FMath::DegreesToRadians(NewStateAngVel);
	const FQuat ExtrapolationDeltaQuaternion = FQuat(NewStateAngVelAxis, NewStateAngVel * ExtrapolationDeltaSeconds);
	FQuat TargetQuat = ExtrapolationDeltaQuaternion * NewState.Quaternion;


	FVector LinDiff;
	float LinDiffSize;
	FVector AngDiffAxis;
	float AngDiff;
	float AngDiffSize;
	ComputeDeltasVR(CurrentState.Position, CurrentState.Quaternion, TargetPos, TargetQuat, LinDiff, LinDiffSize, AngDiffAxis, AngDiff, AngDiffSize);

	/////// ACCUMULATE ERROR IF NOT APPROACHING SOLUTION ///////

	// Store sleeping state
	const bool bShouldSleep = (NewState.Flags & ERigidBodyFlags::Sleeping) != 0;
	const bool bWasAwake = !Handle->Sleeping();
	const bool bAutoWake = false;

	const float Error = (LinDiffSize * ErrorPerLinearDiff) + (AngDiffSize * ErrorPerAngularDiff);

	bRestoredState = Error < MaxRestoredStateError;
	if (bRestoredState)
	{
		Target.AccumulatedErrorSeconds = 0.0f;
	}
	else
	{
		//
		// The heuristic for error accumulation here is:

		// 1. Did the physics tick from the previous step fail to
		//    move the body towards a resolved position?
		// 2. Was the linear error in the same direction as the
		//    previous frame?
		// 3. Is the linear error large enough to accumulate error?
		//
		// If these conditions are met, then "error" time will accumulate.
		// Once error has accumulated for a certain number of seconds,
		// a hard-snap to the target will be performed.
		//
		// TODO: Rotation while moving linearly can still mess up this
		// heuristic. We need to account for it.
		//

		// Project the change in position from the previous tick onto the
		// linear error from the previous tick. This value roughly represents
		// how much correction was performed over the previous physics tick.
		const float PrevProgress = FVector::DotProduct(
			FVector(CurrentState.Position) - Target.PrevPos,
			(Target.PrevPosTarget - Target.PrevPos).GetSafeNormal());

		// Project the current linear error onto the linear error from the
		// previous tick. This value roughly represents how little the direction
		// of the linear error state has changed, and how big the error is.
		const float PrevSimilarity = FVector::DotProduct(
			TargetPos - FVector(CurrentState.Position),
			Target.PrevPosTarget - Target.PrevPos);

		// If the conditions from the heuristic outlined above are met, accumulate
		// error. Otherwise, reduce it.
		if (PrevProgress < ErrorAccumulationDistanceSq &&
			PrevSimilarity > ErrorAccumulationSimilarity)
		{
			Target.AccumulatedErrorSeconds += DeltaSeconds;
		}
		else
		{
			Target.AccumulatedErrorSeconds = FMath::Max(Target.AccumulatedErrorSeconds - DeltaSeconds, 0.0f);
		}

		// Hard snap if error accumulation or linear error is big enough, and clear the error accumulator.
		const bool bHardSnap =
			LinDiffSize > MaxLinearHardSnapDistance ||
			Target.AccumulatedErrorSeconds > ErrorAccumulationSeconds ||
			CharacterMovementCVars::AlwaysHardSnap;

		if (bHardSnap)
		{
#if !UE_BUILD_SHIPPING
			if (PhysicsReplicationCVars::LogPhysicsReplicationHardSnaps)
			{
				UE_LOG(LogTemp, Warning, TEXT("Simulated HARD SNAP - \nCurrent Pos - %s, Target Pos - %s\n CurrentState.LinVel - %s, New Lin Vel - %s\nTarget Extrapolation Delta - %s, Is Asleep - %d, Prev Progress - %f, Prev Similarity - %f"),
					*CurrentState.Position.ToString(), *TargetPos.ToString(), *CurrentState.LinVel.ToString(), *NewState.LinVel.ToString(),
					*ExtrapolationDeltaPos.ToString(), Handle->Sleeping(), PrevProgress, PrevSimilarity);

				if (LinDiffSize > MaxLinearHardSnapDistance)
				{
					UE_LOG(LogTemp, Warning, TEXT("Hard snap due to linear difference error"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Hard snap due to accumulated error"))
				}
			}
#endif
			// Too much error so just snap state here and be done with it
			Target.AccumulatedErrorSeconds = 0.0f;
			bRestoredState = true;

			// Set XRVW to hard snap dynamic object and force recalculation of friction
			const bool bCorrectConnectedBodies = SettingsCurrent.DefaultReplicationSettings.GetCorrectConnectedBodies();
			const bool bCorrectConnectedBodiesFriction = SettingsCurrent.DefaultReplicationSettings.GetCorrectConnectedBodiesFriction();
			RigidsSolver->GetEvolution()->ApplyParticleTransformCorrection(Handle, TargetPos, TargetQuat, bCorrectConnectedBodies, bCorrectConnectedBodiesFriction, ReplicatedParticleIDs);
			Handle->SetV(NewState.LinVel);
			Handle->SetW(FMath::DegreesToRadians(NewState.AngVel));
		}
		else
		{
			const FVector NewLinVel = FVector(Target.TargetState.LinVel) + (LinDiff * LinearVelocityCoefficient * DeltaSeconds);
			const FVector NewAngVel = FVector(Target.TargetState.AngVel) + (AngDiffAxis * AngDiff * AngularVelocityCoefficient * DeltaSeconds);

			const FVector NewPos = FMath::Lerp(FVector(CurrentState.Position), TargetPos, PositionLerp);
			const FQuat NewAng = FQuat::Slerp(CurrentState.Quaternion, TargetQuat, AngleLerp);

			Handle->SetX(NewPos);
			Handle->SetR(NewAng);
			Handle->SetV(NewLinVel);
			Handle->SetW(FMath::DegreesToRadians(NewAngVel));
		}
	}

	if (bShouldSleep)
	{
		// don't allow kinematic to sleeping transition
		if (Handle->ObjectState() != Chaos::EObjectStateType::Kinematic)
		{
			RigidsSolver->GetEvolution()->SetParticleObjectState(Handle, Chaos::EObjectStateType::Sleeping);
		}
	}

	Target.PrevPosTarget = TargetPos;
	Target.PrevPos = FVector(CurrentState.Position);

	return bRestoredState;
}

/** Interpolating towards replicated states from the server while predicting local physics
* TODO, detailed description
*/
bool FPhysicsReplicationAsyncVR::PredictiveInterpolation(Chaos::FPBDRigidParticleHandle* Handle, FReplicatedPhysicsTargetAsync& Target, const float DeltaSeconds)
{
	static const auto CVarSkipReplication = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.SkipReplication"));
	if (CVarSkipReplication->GetBool())
	{
		return true;
	}

	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	if (RigidsSolver == nullptr)
	{
		return true;
	}

	static const auto CVarResimDisableReplicationOnInteraction = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.DisableReplicationOnInteraction"));
	if (CVarResimDisableReplicationOnInteraction->GetBool() && ParticlesInResimIslands.Contains(Handle->GetHandleIdx()))
	{
		// If particle is in an island with a resim object, don't run replication and wait for an up to date target (after leaving the island)
		Target.SetWaiting(RigidsSolver->GetCurrentFrame() + Target.FrameOffset, EPhysicsReplicationMode::Resimulation);
		return false;
	}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	static const auto CVarDrawDebugTargets = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DrawDebugTargets"));
	if (CVarDrawDebugTargets->GetBool())
	{
		// Needs updated post 5.5 for DrawDebugZ CVAR
		 
		const FVector Offset = FVector(0.0f, 0.0f, 50.0f);
		const FVector StartPos = Target.TargetState.Position + Offset;
		const int32 SizeMultiplier = FMath::Clamp(Target.TickCount, -4, 30);
		static const auto CVarDebugdrawLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Net.DebugDraw.LifeTime"));
		Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(StartPos, FVector(5.0f + SizeMultiplier * 0.75f, 5.0f + SizeMultiplier * 0.75f, 5.0f + SizeMultiplier * 0.75f), Target.TargetState.Quaternion, FColor::MakeRandomSeededColor(Target.ServerFrame), false, CVarDebugdrawLifetime->GetFloat(), 0, 1.0f);
	}
#endif

	const bool bIsSleeping = Handle->IsSleeping();
	const bool bCanSimulate = Handle->IsDynamic() || bIsSleeping;

	// Accumulate sleep time or reset back to 0s if not sleeping
	Target.AccumulatedSleepSeconds = bIsSleeping ? (Target.AccumulatedSleepSeconds + DeltaSeconds) : 0.0f;

	// Helper for sleep and target clearing at replication end
	auto EndReplicationHelper = [RigidsSolver, Handle, bCanSimulate, bIsSleeping, DeltaSeconds](FReplicatedPhysicsTargetAsync& Target, bool bOkToClear) -> bool
	{
		const bool bShouldSleep = (Target.TargetState.Flags & ERigidBodyFlags::Sleeping) != 0;
		const bool bReplicatingPhysics = (Target.TargetState.Flags & ERigidBodyFlags::RepPhysics) != 0;

		// --- Set Sleep State ---
		if (bOkToClear && bShouldSleep && bCanSimulate)
		{
			RigidsSolver->GetEvolution()->SetParticleObjectState(Handle, Chaos::EObjectStateType::Sleeping);

			static const auto CVarSleepConnectedBodies = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.SleepConnectedBodies"));
			if (CVarSleepConnectedBodies->GetBool())
			{
				RigidsSolver->GetEvolution()->ApplySleepOnConnectedParticles(Handle);
			}
		}

		static const auto CVarSleepSecondsClearTarget = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.SleepSecondsClearTarget"));
		static const auto CVarDontClearTarget = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DontClearTarget"));
		// --- Should replication stop? ---
		const bool bClearTarget =
			((bOkToClear && bShouldSleep && Target.AccumulatedSleepSeconds >= CVarSleepSecondsClearTarget->GetFloat()) // Allow clearing the target due to sleeping after the object has been sleeping for n seconds
				|| (bOkToClear && !bReplicatingPhysics) // If replication say it's okay to clear the target and the object shouldn't replicate physics anymore, clear the target
				|| (bOkToClear && !bCanSimulate)) // If replication say it's okay to clear the target and the object can't simulate, clear the target
			&& !CVarDontClearTarget->GetBool();

		// --- Target Prediction ---
		if (!bClearTarget && Target.bAllowTargetAltering)
		{
			static const auto CVarExtrapolationTimeMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.ExtrapolationTimeMultiplier"));
			static const auto CVarExtrapolationMinTime = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.ExtrapolationMinTime"));
			const int32 ExtrapolationTickLimit = FMath::Max(
				FMath::CeilToInt(Target.AverageReceiveInterval * CVarExtrapolationTimeMultiplier->GetFloat()), // Extrapolate time based on receive interval * multiplier
				FMath::CeilToInt(CVarExtrapolationMinTime->GetFloat() / DeltaSeconds)); // At least extrapolate for N seconds
			
			if (Target.TickCount <= ExtrapolationTickLimit)
			{
				FPhysicsReplicationAsyncVR::ExtrapolateTarget(Target, 1, DeltaSeconds);
			}
			else
			{
				// If we reach the extrapolation limit, disable target from being altered
				Target.bAllowTargetAltering = false;
			}
		}

		return bClearTarget;
	};

	// If waiting on an up to date state, early out but allow target clearing since we might not receive a new state if target is already set to sleep for example
	if (Target.IsWaiting())
	{
		return EndReplicationHelper(Target, true);
	}

	static const auto CVarEarlyOutWithVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.EarlyOutWithVelocity"));
	static const auto CVarEarlyOutDistanceSqr = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.EarlyOutDistanceSqr"));
	// If target velocity is low enough, check the distance from the current position to the source position of our target to see if it's low enough to early out of replication
	const bool bXCanEarlyOut = (CVarEarlyOutWithVelocity->GetBool() || Target.TargetState.LinVel.SizeSquared() < UE_KINDA_SMALL_NUMBER) &&
		(Target.PrevPosTarget - Handle->GetX()).SizeSquared() < CVarEarlyOutDistanceSqr->GetFloat();

	// Early out if we are within range of target, also apply target sleep state
	if (bXCanEarlyOut)
	{
		// Get the rotational offset between the blended rotation target and the current rotation
		const FQuat TargetRotDelta = Target.TargetState.Quaternion * Handle->GetR().Inverse();

		// Convert to angle and axis
		float Angle;
		FVector Axis;
		TargetRotDelta.ToAxisAndAngle(Axis, Angle);
		Angle = FMath::RadiansToDegrees(FMath::UnwindRadians(Angle));
		Angle = FMath::Abs(Angle);

		static const auto CVarEarlyOutAngle = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.EarlyOutAngle"));
		if (Angle < CVarEarlyOutAngle->GetFloat())
		{
			// Early Out
			return EndReplicationHelper(Target, true);
		}
	}

	static const auto CVarAverageReceiveIntervalSmoothing = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.AverageReceiveIntervalSmoothing"));
	// Update the AverageReceiveInterval if Target.ReceiveInterval has a valid value to update from
	Target.AverageReceiveInterval = Target.ReceiveInterval == 0 ? Target.AverageReceiveInterval : FMath::Lerp(Target.AverageReceiveInterval, Target.ReceiveInterval, FMath::Clamp((1.0f / (Target.ReceiveInterval * CVarAverageReceiveIntervalSmoothing->GetFloat())), 0.0f, 1.0f));

	// CurrentState
	FRigidBodyState CurrentState;
	CurrentState.Position = Handle->GetX();
	CurrentState.Quaternion = Handle->GetR();
	CurrentState.LinVel = Handle->GetV();
	CurrentState.AngVel = Handle->GetW(); // Radians

	// NewState
	const FVector TargetPos = FVector(Target.TargetState.Position);
	const FQuat TargetRot = Target.TargetState.Quaternion;
	const FVector TargetLinVel = FVector(Target.TargetState.LinVel);
	const FVector TargetAngVel = FVector(FMath::DegreesToRadians(Target.TargetState.AngVel)); // Radians

	/** --- Reconciliation --- */
	static const auto CVarKinematicHardSnap = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.KinematicHardSnap"));
	static const auto CVarErrorAccumulationSeconds = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationSeconds"));
	static const auto CVarAlwaysHardSnap = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.AlwaysHardSnap"));
	const bool bHardSnap = (!bCanSimulate && CVarKinematicHardSnap->GetBool())
		|| Target.AccumulatedErrorSeconds > CVarErrorAccumulationSeconds->GetFloat()
		|| CVarAlwaysHardSnap->GetBool();

	if (bHardSnap)
	{
		Target.AccumulatedErrorSeconds = 0.0f;

		if (Handle->IsKinematic())
		{
			// Set a FKinematicTarget to hard snap kinematic object
			const Chaos::FKinematicTarget KinTarget = Chaos::FKinematicTarget::MakePositionTarget(Target.PrevPosTarget, Target.PrevRotTarget); // Uses EKinematicTargetMode::Position
			RigidsSolver->GetEvolution()->SetParticleKinematicTarget(Handle, KinTarget);
		}
		else
		{
			// Set XRVW to hard snap dynamic object and force recalculation of friction
			const bool bCorrectConnectedBodies = SettingsCurrent.PredictiveInterpolationSettings.GetCorrectConnectedBodies();
			RigidsSolver->GetEvolution()->ApplyParticleTransformCorrection(Handle, Target.PrevPosTarget, Target.PrevRotTarget, bCorrectConnectedBodies, /*bInRecalculateFrictionOnConnectedBodies*/ true, ReplicatedParticleIDs);
			Handle->SetV(TargetLinVel);
			Handle->SetW(TargetAngVel);
		}

		// Cache data for next replication
		Target.PrevLinVel = FVector(Target.TargetState.LinVel);

		// End replication and go to sleep if that's requested
		return EndReplicationHelper(Target, true);
	}

	/** If target velocities are low enough, check the traveled direction and distance from previous frame and compare with replicated linear velocity.
	* If the object isn't moving enough along the replicated velocity it's considered stuck and needs reconciliation.
	* SoftSnap is performed each tick while there is a registered error, if enough time pass HardSnap forces the object into the correct state. */
	static const auto CVarVelocityBased = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.VelocityBased"));
	bool bSoftSnap = !CVarVelocityBased->GetBool();

	static const auto CVarDisableErrorVelocityLimits = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DisableErrorVelocityLimits"));
	static const auto CVarErrorAccLinVelMaxLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.ErrorAccLinVelMaxLimit"));
	static const auto CVarErrorAccAngVelMaxLimit = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.ErrorAccAngVelMaxLimit"));
	if ( CVarDisableErrorVelocityLimits->GetBool() ||
		(TargetLinVel.Size() < CVarErrorAccLinVelMaxLimit->GetFloat() && TargetAngVel.Size() < CVarErrorAccAngVelMaxLimit->GetFloat()))
	{
		const FVector PrevDiff = CurrentState.Position - Target.PrevPos;
		const float ExpectedDistance = (Target.PrevLinVel * DeltaSeconds).Size();
		const float CoveredDistance = FVector::DotProduct(PrevDiff, Target.PrevLinVel.GetSafeNormal());
		const float CoveredAplha = FMath::Clamp(CoveredDistance / ExpectedDistance, 0.0f, 1.0f);

		// If the object is moving less than X% of the expected distance, accumulate error seconds
		static const auto CVarMinExpectedDistanceCovered = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.MinExpectedDistanceCovered"));
		if (CoveredAplha < CVarMinExpectedDistanceCovered->GetFloat())
		{
			Target.AccumulatedErrorSeconds += DeltaSeconds;
			bSoftSnap = true;
		}
		else if (Target.AccumulatedErrorSeconds > 0.f)
		{
			static const auto CVarErrorAccumulationDecreaseMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.ErrorAccumulationDecreaseMultiplier"));
			const float DecreaseTime = DeltaSeconds * CVarErrorAccumulationDecreaseMultiplier->GetFloat();
			Target.AccumulatedErrorSeconds = FMath::Max(Target.AccumulatedErrorSeconds - DecreaseTime, 0.0f);
			bSoftSnap = true;
		}
	}
	else
	{
		Target.AccumulatedErrorSeconds = 0;
	}

	static const auto CVarVelocityBasedSetting = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.VelocityBased"));
	if (SettingsCurrent.PredictiveInterpolationSettings.GetDisableSoftSnap() && CVarVelocityBasedSetting->GetBool())
	{
		bSoftSnap = false;
	}
	
	if (Handle->IsKinematic()) // Smooth Kinematic Replication
	{
		static const auto CVarKinematicPrediction = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.KinematicPrediction"));
		const bool bKinematicPrediction = CVarKinematicPrediction->GetBool();
		const float InterpolationTicks = FMath::CeilToInt(Target.AverageReceiveInterval) - (RigidsSolver->GetCurrentFrame() - Target.ReceiveFrame);

		if ((bKinematicPrediction && Target.bAllowTargetAltering) || InterpolationTicks > 0)
		{
			/* Calculate the Lerp value for a smooth interpolation
			* ------------------------------------------------------------------------------
			* bKinematicPrediction is True :: Interpolate towards the target that gets forward predicted each tick
			*	1 / 4 = 0.25 = 25% interpolation each time (if AverageReceiveInterval is 4)
			* ------------------------------------------------------------------------------
			* bKinematicPrediction is False :: Interpolate from current position to the static source for the current target, we need to cover the same amount of distance but from a decaying distance
			*	| ---> | ------------------ |
			*	0%    25%				   100%		(1 / 4 = 0.25)
			*		   | ---> | ----------- |
			*		   0%	 33%		   100%		(1 / 3 = 0.33)
			*				  | ---> | ---- |
			*				  0%    50%    100%		(1 / 2 = 0.5)
			*						 | ---> |
			*						 0%    100%		(1 / 1 = 1.0)
			* ------------------------------------------------------------------------------
			*/
			const float Lerp = 1.f / (bKinematicPrediction ? Target.AverageReceiveInterval : InterpolationTicks);

			// Interpolate position and rotation from current position towards target position based on either predicted target or source target
			const FVector KinTargetPos = FMath::Lerp(CurrentState.Position,
				(bKinematicPrediction ? Target.TargetState.Position : Target.PrevPosTarget),
				Lerp);
			const FQuat KinTargetRot = FQuat::Slerp(CurrentState.Quaternion,
				(bKinematicPrediction ? Target.TargetState.Quaternion : Target.PrevRotTarget),
				Lerp);

			// Apply kinematic target
			const Chaos::FKinematicTarget KinTarget = Chaos::FKinematicTarget::MakePositionTarget(KinTargetPos, KinTargetRot); // Uses EKinematicTargetMode::Position
			RigidsSolver->GetEvolution()->SetParticleKinematicTarget(Handle, KinTarget);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			//static const auto CVarDrawDebugTargets = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DrawDebugTargets"));
			if (CVarDrawDebugTargets->GetBool())
			{
				
				static const auto CVarDrawDebugZOffset = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DrawDebugZOffset"));
				const FVector Offset = FVector(0.0f, 0.0f, CVarDrawDebugZOffset->GetFloat());
				const FVector Pos = KinTargetPos + Offset;
				const int32 SizeMultiplier = FMath::Clamp(Target.TickCount, -4, 30);
				static const auto CVarDebugdrawLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Net.DebugDraw.LifeTime"));
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugSphere(Pos, 3.0f + SizeMultiplier * 0.75f, 8, FColor::MakeRandomSeededColor(Target.ServerFrame), false, CVarDebugdrawLifetime->GetFloat(), 0, 1.0f);
			}
#endif
		}
		else
		{
			// End replication and allow to clear target
			return EndReplicationHelper(Target, true);
		}
	}
	else // Velocity-based Replication
	{
		// Wake up if sleeping
		if (bIsSleeping)
		{
			RigidsSolver->GetEvolution()->SetParticleObjectState(Handle, Chaos::EObjectStateType::Dynamic);
		}

		// Calculate interpolation time based on current average receive rate
		const float AverageReceiveIntervalSeconds = Target.AverageReceiveInterval * DeltaSeconds;
		const float InterpolationTime = AverageReceiveIntervalSeconds * SettingsCurrent.PredictiveInterpolationSettings.GetPosInterpolationTimeMultiplier();

		// Calculate position correction time based on current Round Trip Time
		const float RTT = LatencyOneWay * 2.f;
		const float PosCorrectionTime = FMath::Max(SettingsCurrent.PredictiveInterpolationSettings.GetPosCorrectionTimeBase() + AverageReceiveIntervalSeconds + RTT * SettingsCurrent.PredictiveInterpolationSettings.GetPosCorrectionTimeMultiplier(),
			DeltaSeconds + SettingsCurrent.PredictiveInterpolationSettings.GetPosCorrectionTimeMin());
		const float RotCorrectionTime = FMath::Max(SettingsCurrent.PredictiveInterpolationSettings.GetRotCorrectionTimeBase() + AverageReceiveIntervalSeconds + RTT * SettingsCurrent.PredictiveInterpolationSettings.GetRotCorrectionTimeMultiplier(),
			DeltaSeconds + SettingsCurrent.PredictiveInterpolationSettings.GetRotCorrectionTimeMin());

		FVector CorrectionX = CurrentState.Position;
		if ((bXCanEarlyOut && SettingsCurrent.PredictiveInterpolationSettings.GetSkipVelocityRepOnPosEarlyOut()) == false)
		{	// --- Velocity Replication ---

			// Get PosDiff
			const FVector PosDiff = TargetPos - CurrentState.Position;

			// Get LinVelDiff by adding inverted CurrentState.LinVel to TargetLinVel
			const FVector LinVelDiff = -CurrentState.LinVel + TargetLinVel;

			// Calculate velocity blend amount for this tick as an alpha value
			const float VelocityAlpha = FMath::Clamp(DeltaSeconds / InterpolationTime, 0.0f, 1.0f);

			FVector RepLinVel;
			static const auto CVarPosCorrectionAsVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.CorrectionAsVelocity"));
			if (CVarPosCorrectionAsVelocity->GetBool())
			{
				// Convert PosDiff to a velocity
				const FVector PosDiffVelocity = PosDiff / PosCorrectionTime;

				// Add PosDiffVelocity to LinVelDiff to get BlendedTargetVelocity
				const FVector BlendedTargetVelocity = LinVelDiff + PosDiffVelocity;

				// Add BlendedTargetVelocity onto current velocity
				RepLinVel = CurrentState.LinVel + (BlendedTargetVelocity * VelocityAlpha); // Same as (BlendedTargetVelocity / InterpolationTime) * DeltaSeconds
			}
			else // Positional correction as transform shift
			{
				// Add velocity diff onto current velocity
				RepLinVel = CurrentState.LinVel + (LinVelDiff * VelocityAlpha); // Same as (LinVelDiff / InterpolationTime) * DeltaSeconds

				// Calculate correction blend amount for this tick as an alpha value
				const float CorrectionAlpha = FMath::Clamp(DeltaSeconds / PosCorrectionTime, 0.0f, 1.0f);

				// Calculate the PosDiff amount to correct this tick
				const FVector PosDiffVelocityDelta = PosDiff * CorrectionAlpha; // Same as (PosDiff / PosCorrectionTime) * DeltaSeconds

				// The new position after correction
				CorrectionX = Handle->GetX() + PosDiffVelocityDelta;
			}

			// Apply velocity replication

			Handle->SetV(RepLinVel);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			static const auto CVarDrawDebugVectors = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DrawDebugVectors"));
			if (CVarDrawDebugVectors->GetBool())
			{
				static const auto CVarDrawDebugZOffset = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.DrawDebugZOffset"));
				const FVector Offset = FVector(0.0f, 0.0f, CVarDrawDebugZOffset->GetFloat());
				const FVector OffsetAdd = FVector(0.0f, 0.0f, 10.0f);
				const FVector StartPos = TargetPos + Offset;
				FVector Direction = TargetLinVel;
				Direction.Normalize();
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow((StartPos + OffsetAdd * 0), (StartPos + OffsetAdd * 0) + TargetLinVel * 0.5f, 5.0f, FColor::Green, false, -1.0f, 0, 2.0f);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow((StartPos + OffsetAdd * 1), (StartPos + OffsetAdd * 1) + CurrentState.LinVel * 0.5f, 5.0f, FColor::Blue, false, -1.0f, 0, 2.0f);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow((StartPos + OffsetAdd * 2), (StartPos + OffsetAdd * 2) + (Target.PrevLinVel - CurrentState.LinVel) * 0.5f, 5.0f, FColor::Red, false, -1.0f, 0, 2.0f);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow((StartPos + OffsetAdd * 3), (StartPos + OffsetAdd * 3) + RepLinVel * 0.5f, 5.0f, FColor::Magenta, false, -1.0f, 0, 2.0f);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow((StartPos + OffsetAdd * 4), (StartPos + OffsetAdd * 4) + (Target.PrevLinVel - RepLinVel) * 0.5f, 5.0f, FColor::Orange, false, -1.0f, 0, 2.0f);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow((StartPos + OffsetAdd * 5), (StartPos + OffsetAdd * 5) + Direction * RTT, 5.0f, FColor::White, false, -1.0f, 0, 2.0f);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow((StartPos + OffsetAdd * 6), (StartPos + OffsetAdd * 6) + Direction * InterpolationTime, 5.0f, FColor::Yellow, false, -1.0f, 0, 2.0f);
			}
#endif
			// Cache data for next replication
			Target.PrevLinVel = FVector(RepLinVel);
		}

		FQuat CorrectionR = CurrentState.Quaternion;
		{	// --- Angular Velocity Replication ---

			// Get AngVelDiff by adding inverted CurrentState.AngVel to TargetAngVel
			const FVector AngVelDiff = -CurrentState.AngVel + TargetAngVel;

			// Calculate velocity blend amount for this tick as an alpha value
			const float VelocityAlpha = FMath::Clamp(DeltaSeconds / InterpolationTime, 0.0f, 1.0f);

			FVector RepAngVel;
			static const auto CVarCorrectionAsVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.CorrectionAsVelocity"));
			if (CVarCorrectionAsVelocity->GetBool())
			{
				// Get RotDiff
				const FQuat RotDiff = TargetRot * CurrentState.Quaternion.Inverse();

				// Convert RotDiff to a velocity
				float WAngle;
				FVector WAxis;
				RotDiff.ToAxisAndAngle(WAxis, WAngle);
				WAngle = FMath::UnwindRadians(WAngle);
				const FVector RotDiffVelocity = FVector(WAxis * (WAngle / RotCorrectionTime));

				// Add RotDiffVelocity to AngVelDiff to get BlendedTargetVelocity
				const FVector BlendedTargetVelocity = AngVelDiff + RotDiffVelocity;

				// Add BlendedTargetVelocity to CurrentState.AngVel
				RepAngVel = CurrentState.AngVel + (BlendedTargetVelocity * VelocityAlpha); // Same as (BlendedTargetVelocity / InterpolationTime) * DeltaSeconds
			}
			else // Positional correction as transform shift
			{
				// Add velocity diff onto current velocity
				RepAngVel = CurrentState.AngVel + (AngVelDiff * VelocityAlpha); // Same as (AngVelDiff / InterpolationTime) * DeltaSeconds

				// Calculate correction blend amount for this tick as an alpha value
				const float CorrectionAlpha = FMath::Clamp(DeltaSeconds / RotCorrectionTime, 0.0f, 1.0f);

				// The new position after correction
				CorrectionR = FQuat::Slerp(Handle->GetR(), TargetRot, CorrectionAlpha);
			}

			// Apply velocity replication
			Handle->SetW(RepAngVel);
		}

		// Cache data for next replication
		Target.PrevPos = FVector(CurrentState.Position);

		// Apply correction as a transform shift
		static const auto CVarCorrectionAsVelocity = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.CorrectionAsVelocity"));
		if (!CVarCorrectionAsVelocity->GetBool())
		{
			const bool bCorrectConnectedBodies = SettingsCurrent.PredictiveInterpolationSettings.GetCorrectConnectedBodies();
			const bool bCorrectConnectedBodiesFriction = SettingsCurrent.PredictiveInterpolationSettings.GetCorrectConnectedBodiesFriction();
			RigidsSolver->GetEvolution()->ApplyParticleTransformCorrection(Handle, CorrectionX, CorrectionR, bCorrectConnectedBodies, bCorrectConnectedBodiesFriction, ReplicatedParticleIDs);
		}

		if (bSoftSnap)
		{
			const FVector SoftSnapPos = FMath::Lerp(FVector(CurrentState.Position),
				SettingsCurrent.PredictiveInterpolationSettings.GetSoftSnapToSource() ? Target.PrevPosTarget : Target.TargetState.Position,
				FMath::Clamp(SettingsCurrent.PredictiveInterpolationSettings.GetSoftSnapPosStrength(), 0.0f, 1.0f));

			const FQuat SoftSnapRot = FQuat::Slerp(CurrentState.Quaternion,
				SettingsCurrent.PredictiveInterpolationSettings.GetSoftSnapToSource() ? Target.PrevRotTarget : Target.TargetState.Quaternion,
				FMath::Clamp(SettingsCurrent.PredictiveInterpolationSettings.GetSoftSnapRotStrength(), 0.0f, 1.0f));

			// Apply correction as a transform shift
			const bool bCorrectConnectedBodies = SettingsCurrent.PredictiveInterpolationSettings.GetCorrectConnectedBodies();
			const bool bCorrectConnectedBodiesFriction = SettingsCurrent.PredictiveInterpolationSettings.GetCorrectConnectedBodiesFriction();
			RigidsSolver->GetEvolution()->ApplyParticleTransformCorrection(Handle, SoftSnapPos, SoftSnapRot, bCorrectConnectedBodies, bCorrectConnectedBodiesFriction, ReplicatedParticleIDs);
		}
	}

	return EndReplicationHelper(Target, false);
}

/** Static function to extrapolate a target for N ticks using X DeltaSeconds */
void FPhysicsReplicationAsyncVR::ExtrapolateTarget(FReplicatedPhysicsTargetAsync& Target, const int32 ExtrapolateFrames, const float DeltaSeconds)
{
	const float ExtrapolationTime = DeltaSeconds * static_cast<float>(ExtrapolateFrames);
	FPhysicsReplicationAsyncVR::ExtrapolateTarget(Target, ExtrapolationTime);
}

/** Static function to extrapolate a target for N Seconds */
void FPhysicsReplicationAsyncVR::ExtrapolateTarget(FReplicatedPhysicsTargetAsync& Target, const float ExtrapolationTime)
{
	// Extrapolate target position
	Target.TargetState.Position = Target.TargetState.Position + Target.TargetState.LinVel * ExtrapolationTime;

	// Extrapolate target rotation
	float TargetAngVelSize;
	FVector TargetAngVelAxis;
	Target.TargetState.AngVel.FVector::ToDirectionAndLength(TargetAngVelAxis, TargetAngVelSize);
	TargetAngVelSize = FMath::DegreesToRadians(TargetAngVelSize);
	const FQuat TargetRotExtrapDelta = FQuat(TargetAngVelAxis, TargetAngVelSize * ExtrapolationTime);
	Target.TargetState.Quaternion = TargetRotExtrapDelta * Target.TargetState.Quaternion;
}

/** Compare states and trigger resimulation if needed */
bool FPhysicsReplicationAsyncVR::ResimulationReplication(Chaos::FPBDRigidParticleHandle* Handle, FReplicatedPhysicsTargetAsync& Target, const float DeltaSeconds)
{
	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	if (RigidsSolver == nullptr)
	{
		return true;
	}

	Chaos::FRewindData* RewindData = RigidsSolver->GetRewindData();
	if (RewindData == nullptr)
	{
		return true;
	}

	if (Target.ServerFrame <= 0)
	{
		return true;
	}

	const int32 LocalFrame = Target.ServerFrame - Target.FrameOffset;

	if (!RewindData->IsFrameWithinRewindHistory(LocalFrame))
	{
		return true;
	}

	const bool bShouldSleep = (Target.TargetState.Flags & ERigidBodyFlags::Sleeping) != 0;
	bool bClearTarget = true;

	static constexpr Chaos::FFrameAndPhase::EParticleHistoryPhase RewindPhase = Chaos::FFrameAndPhase::EParticleHistoryPhase::PostPushData;

	// Get state from locally cached history for frame corresponding to received data
	const Chaos::FGeometryParticleState PastState = RewindData->GetPastStateAtFrame(*Handle, LocalFrame, RewindPhase);

	// Check which comparisons to perform to trigger resimulation from
	const bool bCompareX = Chaos::FPhysicsSolverBase::GetResimulationErrorPositionThresholdEnabled() || SettingsCurrent.ResimulationSettings.bOverrideResimulationErrorPositionThreshold;
	const bool bCompareR = Chaos::FPhysicsSolverBase::GetResimulationErrorRotationThresholdEnabled() || SettingsCurrent.ResimulationSettings.bOverrideResimulationErrorRotationThreshold;
	const bool bCompareV = Chaos::FPhysicsSolverBase::GetResimulationErrorLinearVelocityThresholdEnabled() || SettingsCurrent.ResimulationSettings.bOverrideResimulationErrorLinearVelocityThreshold;
	const bool bCompareW = Chaos::FPhysicsSolverBase::GetResimulationErrorAngularVelocityThresholdEnabled() || SettingsCurrent.ResimulationSettings.bOverrideResimulationErrorAngularVelocityThreshold;
	bool bShouldTriggerResim = false;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Debugging
	FColor DebugColor = FColor::Black;
	bool bResimV = false;
	bool bResimW = false;
#endif

	// Check for positional discrepancy in Distance between client and server
	if (bCompareX)
	{
		const float ResimPositionErrorThreshold = SettingsCurrent.ResimulationSettings.GetResimulationErrorPositionThreshold(Chaos::FPhysicsSolverBase::GetResimulationErrorPositionThreshold());
		bShouldTriggerResim = Chaos::FRewindData::CheckVectorThreshold(Target.TargetState.Position, PastState.GetX(), ResimPositionErrorThreshold);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (bShouldTriggerResim)
		{
			DebugColor = FColor::Orange;
		}
#endif
	}

	// Check for linear velocity discrepancy in Distance / s between client and server
	if (!bShouldTriggerResim && bCompareV)
	{
		const float ResimLinVelocityErrorThreshold = SettingsCurrent.ResimulationSettings.GetResimulationErrorLinearVelocityThreshold(Chaos::FPhysicsSolverBase::GetResimulationErrorLinearVelocityThreshold());
		bShouldTriggerResim = Chaos::FRewindData::CheckVectorThreshold(Target.TargetState.LinVel, PastState.GetV(), ResimLinVelocityErrorThreshold);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (bShouldTriggerResim)
		{
			bResimV = true;
		}
#endif
	}

	// Check for angular velocity discrepancy in Degrees / s between client and server
	if (!bShouldTriggerResim && bCompareW)
	{
		const float ResimAngVelocityErrorThreshold = SettingsCurrent.ResimulationSettings.GetResimulationErrorAngularVelocityThreshold(Chaos::FPhysicsSolverBase::GetResimulationErrorAngularVelocityThreshold());
		bShouldTriggerResim = Chaos::FRewindData::CheckVectorThreshold(Target.TargetState.AngVel, FMath::RadiansToDegrees(PastState.GetW()), ResimAngVelocityErrorThreshold);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (bShouldTriggerResim)
		{
			bResimW = true;
		}
#endif
	}

	// Check for rotational discrepancy in Degrees between client and server
	if (!bShouldTriggerResim && bCompareR)
	{
		const float ResimRotationErrorThreshold = SettingsCurrent.ResimulationSettings.GetResimulationErrorRotationThreshold(Chaos::FPhysicsSolverBase::GetResimulationErrorRotationThreshold());
		bShouldTriggerResim = Chaos::FRewindData::CheckQuaternionThreshold(Target.TargetState.Quaternion, PastState.GetR(), ResimRotationErrorThreshold);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (bShouldTriggerResim)
		{
			DebugColor = FColor::Magenta;
		}
#endif
	}


#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	if (Chaos::FPhysicsSolverBase::CanDebugNetworkPhysicsPrediction())
	{
		UE_LOG(LogTemp, Log, TEXT("Apply Rigid body state at local frame %d with offset = %d"), LocalFrame, Target.FrameOffset);
		UE_LOG(LogTemp, Log, TEXT("Should Trigger Resim = %s | Server Frame = %d | Client Frame = %d"), (bShouldTriggerResim ? TEXT("True") : TEXT("False")), Target.ServerFrame, LocalFrame);
		UE_LOG(LogTemp, Log, TEXT("Particle Target Position = %s | Current Position = %s"), *Target.TargetState.Position.ToString(), *PastState.GetX().ToString());
		UE_LOG(LogTemp, Log, TEXT("Particle Target Velocity = %s | Current Velocity = %s"), *Target.TargetState.LinVel.ToString(), *PastState.GetV().ToString());
		UE_LOG(LogTemp, Log, TEXT("Particle Target Quaternion = %s | Current Quaternion = %s"), *Target.TargetState.Quaternion.ToString(), *PastState.GetR().ToString());
		UE_LOG(LogTemp, Log, TEXT("Particle Target Omega = %s | Current Omega= %s"), *Target.TargetState.AngVel.ToString(), *PastState.GetW().ToString());
	}

	static const auto CVarResimDrawDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.DrawDebug"));
	static const auto CVarRenderInterpDebugDrawResimTrigger = IConsoleManager::Get().FindConsoleVariable(TEXT("p.RenderInterp.DebugDraw.ResimTrigger"));
	if (CVarResimDrawDebug->GetBool() || CVarRenderInterpDebugDrawResimTrigger->GetBool())
	{
		if (bShouldTriggerResim)
		{
			FVector Box = CVarRenderInterpDebugDrawResimTrigger->GetBool() ? FVector(6, 3, 2) : FVector(40, 20, 10);
			const float DrawThickness = CVarRenderInterpDebugDrawResimTrigger->GetBool() ? 0.5f : 1.5f;

			static const auto CVarDebugdrawLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Net.DebugDraw.LifeTime"));
			if (CVarRenderInterpDebugDrawResimTrigger->GetBool()) // Resim debug draw extension for render interpolation 
			{
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(PastState.GetX(), Box, PastState.GetR(), FColor::White, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Target.TargetState.Position, Box, Target.TargetState.Quaternion, DebugColor, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);

				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Handle->GetX(), PastState.GetX(), 5.0f, FColor::White, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PastState.GetX(), Target.TargetState.Position, 5.0f, FColor::Black, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);

				if (bResimV)
				{
					const FVector DiffV = Target.TargetState.LinVel - PastState.GetV();
					Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Target.TargetState.Position, Target.TargetState.Position + DiffV, 5.0f, FColor::Orange, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);
				}
				if (bResimW)
				{
					const FVector DiffW = Target.TargetState.AngVel - FMath::RadiansToDegrees(PastState.GetW());
					Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Target.TargetState.Position + DiffW, Target.TargetState.Position, 5.0f, FColor::Magenta, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);
				}
			}
			else // Resim trigger debug draw
			{
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Handle->GetX(), Box, PastState.GetR(), FColor::White, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Handle->GetX() + (Target.TargetState.Position - PastState.GetX()), Box, Target.TargetState.Quaternion, DebugColor, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);

				if (bResimV)
				{
					const FVector DiffV = Target.TargetState.LinVel - PastState.GetV();
					Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Handle->GetX(), Handle->GetX() + DiffV, 5.0f, FColor::Orange, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);
				}
				if (bResimW)
				{
					const FVector DiffW = Target.TargetState.AngVel - FMath::RadiansToDegrees(PastState.GetW());
					Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Handle->GetX() + DiffW, Handle->GetX(), 5.0f, FColor::Magenta, false, CVarDebugdrawLifetime->GetFloat(), 0, DrawThickness);
				}
			}
		}
	}
#endif

	// Wake up if is sleeping and should not sleep
	if (Handle->IsSleeping() && !bShouldSleep)
	{
		RigidsSolver->GetEvolution()->SetParticleObjectState(Handle, Chaos::EObjectStateType::Dynamic);
	}

	if (bShouldTriggerResim && Target.TickCount == 0 && LocalFrame > RewindData->GetBlockedResimFrame())
	{
		// Request resimulation
		RewindData->RequestResimulation(LocalFrame, Handle);
	}
	else if (SettingsCurrent.ResimulationSettings.GetRuntimeCorrectionEnabled())
	{
		const int32 NumPredictedFrames = RigidsSolver->GetCurrentFrame() - LocalFrame - Target.TickCount;

		if (Target.TickCount <= NumPredictedFrames && NumPredictedFrames > 0)
		{
			const FVector ErrorOffset = (Target.TargetState.Position - PastState.GetX());

			// Positional Correction
			const float CorrectionAmountX = SettingsCurrent.ResimulationSettings.GetPosStabilityMultiplier() / NumPredictedFrames;
			const FVector PosDiffCorrection = ErrorOffset * CorrectionAmountX; // Same result as (ErrorOffset / NumPredictedFrames) * PosStabilityMultiplier
			const FVector CorrectedX = Handle->GetX() + PosDiffCorrection;

			// Rotational Correction
			const float CorrectionAmountR = SettingsCurrent.ResimulationSettings.GetRotStabilityMultiplier() / NumPredictedFrames;
			const FQuat DeltaQuat = PastState.GetR().Inverse() * Target.TargetState.Quaternion;
			const FQuat TargetCorrectionR = Handle->GetR() * DeltaQuat;
			const FQuat CorrectedR = FQuat::Slerp(Handle->GetR(), TargetCorrectionR, CorrectionAmountR);

			if (SettingsCurrent.ResimulationSettings.GetRuntimeVelocityCorrectionEnabled())
			{
				// Linear Velocity Correction
				const FVector LinVelDiff = Target.TargetState.LinVel - PastState.GetV(); // Velocity vector that the server covers but the client doesn't
				const float CorrectionAmountV = SettingsCurrent.ResimulationSettings.GetVelStabilityMultiplier() / NumPredictedFrames;
				const FVector VelCorrection = LinVelDiff * CorrectionAmountV; // Same result as (LinVelDiff / NumPredictedFrames) * VelStabilityMultiplier
				const FVector CorrectedV = Handle->GetV() + VelCorrection;

				// Angular Velocity Correction
				const FVector AngVelDiff = FMath::DegreesToRadians(Target.TargetState.AngVel) - PastState.GetW(); // Angular velocity vector that the server covers but the client doesn't
				const float CorrectionAmountW = SettingsCurrent.ResimulationSettings.GetAngVelStabilityMultiplier() / NumPredictedFrames;
				const FVector AngVelCorrection = AngVelDiff * CorrectionAmountW; // Same result as (AngVelDiff / NumPredictedFrames) * VelStabilityMultiplier
				const FVector CorrectedW = Handle->GetW() + AngVelCorrection;

				// Apply correction to velocities
				Handle->SetV(CorrectedV);
				Handle->SetW(CorrectedW);
			}


#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (CVarResimDrawDebug->GetBool())
				{
					static const auto CVarDebugdrawLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Net.DebugDraw.LifeTime"));
					Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Handle->GetX(), CorrectedX, 5.0f, FColor::MakeRandomSeededColor(LocalFrame), true, CVarDebugdrawLifetime->GetFloat(), 0, 0.5f);
				}
#endif
				// Apply correction to position and rotation
				RigidsSolver->GetEvolution()->ApplyParticleTransformCorrection(Handle, CorrectedX, CorrectedR, SettingsCurrent.ResimulationSettings.GetRuntimeCorrectConnectedBodies(), /*bInRecalculateFrictionOnConnectedBodies*/true, ReplicatedParticleIDs);
		}

		// Keep target for NumPredictedFrames time to perform runtime corrections with until a new target is received
		bClearTarget = Target.TickCount >= NumPredictedFrames;
	}

	// Set sleep state if we are about to clear the target from memory and the target is set to sleep
	if (bClearTarget && bShouldSleep)
	{
		// Snap object into correct state, it should already be at that state or very close to it
		RigidsSolver->GetEvolution()->ApplyParticleTransformCorrection(Handle, Target.TargetState.Position, Target.TargetState.Quaternion, /*bApplyToConnectedBodies*/true, /*bInRecalculateFrictionOnConnectedBodies*/true, ReplicatedParticleIDs);

		RigidsSolver->GetEvolution()->SetParticleObjectState(Handle, Chaos::EObjectStateType::Sleeping);
		static const auto CVarSleepConnectedBodies = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.SleepConnectedBodies"));
		if (CVarSleepConnectedBodies->GetBool())
		{
			RigidsSolver->GetEvolution()->ApplySleepOnConnectedParticles(Handle);
		}
	}
	else if (Target.IsWaiting())
	{
		// Don't clear the target if we are waiting for a specific target frame and not sleeping
		bClearTarget = false;
	}


	return bClearTarget;
}

void FPhysicsReplicationAsyncVR::DebugDrawReplicationMode(const FPhysicsRepAsyncInputData& Input)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

	static const auto CVarDebugDrawShowRepMode = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Net.DebugDraw.ShowRepMode"));
	if (!CVarDebugDrawShowRepMode->GetBool())
	{
		return;
	}

	if (Input.PhysicsObject == nullptr && Input.Proxy == nullptr)
	{
		return;
	}

	FColor DebugColor = FColor::White;
	FVector BoxExtent = FVector(10.0f, 10.0f, 10.0f);
	FQuat Rotation = FQuat::Identity;

	if (Input.PhysicsObject)
	{
		if (FReplicatedPhysicsTargetAsync* Target = ObjectToTarget.Find(Input.PhysicsObject))
		{
			Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
			if (Chaos::FGeometryParticleHandle* Handle = Interface.GetParticle(Input.PhysicsObject))
			{
				BoxExtent = Handle->LocalBounds().Extents() * 0.5f;
				Rotation = Handle->GetR();
			}

			const EPhysicsReplicationMode RepMode = Target->IsWaiting() ? Target->RepModeOverride : Target->RepMode;
			switch (RepMode)
			{
			case EPhysicsReplicationMode::PredictiveInterpolation:
				DebugColor = FColor::Yellow;
				break;
			case EPhysicsReplicationMode::Resimulation:
				DebugColor = FColor::Red;
				break;
			case EPhysicsReplicationMode::Default:
			default:
				DebugColor = FColor::Cyan;
				break;
			}
		}
	}
	else if (Input.Proxy != nullptr)
	{
		// Legacy Default physics replication

		Chaos::FSingleParticlePhysicsProxy* Proxy = Input.Proxy;
		Chaos::FRigidBodyHandle_Internal* Handle = Proxy->GetPhysicsThreadAPI();

		Rotation = Handle->GetR();
		DebugColor = FColor::Green;
	}

	static const auto CVarDebugdrawLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.Net.DebugDraw.LifeTime"));
	Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Input.TargetState.Position, BoxExtent, Rotation, DebugColor, false, CVarDebugdrawLifetime->GetFloat(), 0, 1.0f);
#endif
}

FName FPhysicsReplicationAsyncVR::GetFNameForStatId() const
{
	const static FLazyName StaticName("FPhysicsReplicationAsyncCallback");
	return StaticName;
}

bool FPhysicsReplicationAsyncVR::UsePhysicsReplicationLOD()
{
	Chaos::FPBDRigidsSolver& RigidsSolver = GetSolver()->CastChecked();

	IPhysicsReplicationLODAsync* PhysRepLod = RigidsSolver.GetPhysicsReplicationLOD_Internal();
	return PhysRepLod && PhysRepLod->IsEnabled();
}

#pragma endregion // FPhysicsReplicationAsync