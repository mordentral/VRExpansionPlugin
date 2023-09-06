// Fill out your copyright notice in the Description page of Project Settings.

#include "Grippables/GrippablePhysicsReplication.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GrippablePhysicsReplication)

#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "DrawDebugHelpers.h"
#include "Chaos/ChaosMarshallingManager.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "Chaos/PhysicsObject.h"
#include "PBDRigidsSolver.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "VRGlobalSettings.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Chaos/DebugDrawQueue.h"
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

void FPhysicsReplicationVR::SetReplicatedTarget(UPrimitiveComponent* Component, FName BoneName, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame)
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
	if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction && Owner &&
		(CVarEnableDefaultReplication->GetInt() || Owner->GetPhysicsReplicationMode() != EPhysicsReplicationMode::Default)) // For now, only opt in to the PhysicsObject flow if not using Default replication or if default is allowed via CVar.
	{
		const ENetRole OwnerRole = Owner->GetLocalRole();
		const bool bIsSimulated = OwnerRole == ROLE_SimulatedProxy;
		const bool bIsReplicatedAutonomous = OwnerRole == ROLE_AutonomousProxy && Component->bReplicatePhysicsToAutonomousProxy;
		if (bIsSimulated || bIsReplicatedAutonomous)
		{
			Chaos::FPhysicsObjectHandle PhysicsObject = Component->GetPhysicsObjectByName(BoneName);
			SetReplicatedTargetVR(PhysicsObject, ReplicatedTarget, ServerFrame, Owner->GetPhysicsReplicationMode());
			return;
		}
	}

	return FPhysicsReplication::SetReplicatedTarget(Component, BoneName, ReplicatedTarget, ServerFrame);
}

void FPhysicsReplicationVR::SetReplicatedTargetVR(Chaos::FPhysicsObject* PhysicsObject, const FRigidBodyState& ReplicatedTarget, int32 ServerFrame, EPhysicsReplicationMode ReplicationMode)
{
	UWorld* OwningWorld = GetOwningWorld();
	if (OwningWorld == nullptr)
	{
		return;
	}

	// TODO, Check if owning actor is ROLE_SimulatedProxy or ROLE_AutonomousProxy ?

	FReplicatedPhysicsTarget Target;
	Target.PhysicsObject = PhysicsObject;
	Target.ReplicationMode = ReplicationMode;
	Target.ServerFrame = ServerFrame;
	Target.TargetState = ReplicatedTarget;
	Target.ArrivedTimeSeconds = OwningWorld->GetTimeSeconds();

	ensure(!Target.TargetState.Position.ContainsNaN());

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
	const float MaxLinearHardSnapDistance = CVarMaxLinearHardSnapDistance->GetFloat() >= 0.f ? CVarMaxLinearHardSnapDistance->GetFloat() : ErrorCorrection.MaxLinearHardSnapDistance;

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
			BI->SetBodyTransform(IdealWorldTM, ETeleportType::ResetPhysics, bAutoWake);

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
				FPhysicsRepAsyncInputData AsyncInputData;
				AsyncInputData.TargetState = NewState;
				AsyncInputData.TargetState.Position = IdealWorldTM.GetLocation();
				AsyncInputData.TargetState.Quaternion = IdealWorldTM.GetRotation();
				AsyncInputData.Proxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(BI->GetPhysicsActorHandle());
				AsyncInputData.PhysicsObject = nullptr;
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
				DrawDebugDirectionalArrow(OwningWorld, CurrentState.Position, TargetPos, 5.0f, Color, true, CVarNetCorrectionLifetime->GetFloat(), 0, 1.5f);
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

	using namespace Chaos;


	int32 LocalFrameOffset = 0; // LocalFrame = ServerFrame + LocalFrameOffset;
	if (FPhysicsSolverBase::IsNetworkPhysicsPredictionEnabled())
	{
		if (UWorld* World = GetOwningWorld())
		{
			if (World->GetNetMode() == NM_Client)
			{
				if (APlayerController* PlayerController = World->GetFirstPlayerController())
				{
					LocalFrameOffset = PlayerController->GetServerToLocalAsyncPhysicsTickOffset();
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
			if (FBodyInstance* BI = PrimComp->GetBodyInstance(Itr.Value().BoneName))
			{
				FReplicatedPhysicsTarget& PhysicsTarget = Itr.Value();
				FRigidBodyState& UpdatedState = PhysicsTarget.TargetState;
				bool bUpdated = false;
				if (AActor* OwningActor = PrimComp->GetOwner())
				{
					// Remove if there is no owner
					if (!OwningActor->GetNetOwningPlayer())
					{
						bRemoveItr = true;
					}
					else
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
								const int32 LocalFrame = PhysicsTarget.ServerFrame + LocalFrameOffset;
								const bool bRestoredState = ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, PhysicErrorCorrection, PingSecondsOneWay, LocalFrame, 0);
								//const bool bRestoredState = ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, PhysicErrorCorrection, PingSecondsOneWay, LocalFrame, NumPredictedFrames);

								// Need to update the component to match new position.
								static const auto CVarSkipSkeletalRepOptimization = IConsoleManager::Get().FindConsoleVariable(TEXT("p.SkipSkeletalRepOptimization"));
								if (/*PhysicsReplicationCVars::SkipSkeletalRepOptimization*/CVarSkipSkeletalRepOptimization->GetInt() == 0 || Cast<USkeletalMeshComponent>(PrimComp) == nullptr)	//simulated skeletal mesh does its own polling of physics results so we don't need to call this as it'll happen at the end of the physics sim
								{
									PrimComp->SyncComponentToRBPhysics();
								}

								// Added a sleeping check from the input state as well, we always want to cease activity on sleep
								if (bRestoredState /* || ((UpdatedState.Flags & ERigidBodyFlags::Sleeping) != 0)*/)
								{
									bRemoveItr = true;
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
	}

	// PhysicsObject replication flow
	for (FReplicatedPhysicsTarget& PhysicsTarget : ReplicatedTargetsQueueVR)
	{
		if (PhysicsTarget.TargetState.Flags & ERigidBodyFlags::NeedsUpdate)
		{
			const float PingSecondsOneWay = LocalPing * 0.5f * 0.001f;

			// Queue up the target state for async replication
			FPhysicsRepAsyncInputData AsyncInputData;
			AsyncInputData.TargetState = PhysicsTarget.TargetState;
			AsyncInputData.Proxy = nullptr;
			AsyncInputData.PhysicsObject = PhysicsTarget.PhysicsObject;
			AsyncInputData.RepMode = PhysicsTarget.ReplicationMode;
			AsyncInputData.ServerFrame = PhysicsTarget.ServerFrame;
			AsyncInputData.FrameOffset = LocalFrameOffset;
			AsyncInputData.LatencyOneWay = PingSecondsOneWay;

			AsyncInputVR->InputData.Add(AsyncInputData);
		}
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

void FPhysicsReplicationAsyncVR::OnPreSimulate_Internal()
{
	if (const FPhysicsReplicationAsyncInput* AsyncInput = GetConsumerInput_Internal())
	{
		Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
		check(RigidsSolver);

		// Early out if this is a resim frame
		Chaos::FRewindData* RewindData = RigidsSolver->GetRewindData();
		if (RewindData && RewindData->IsResim())
		{
			// TODO, Handle the transition from post-resim to interpolation better.
			static const auto CVarPostResimWaitForUpdate = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.PostResimWaitForUpdate"));
			if (CVarPostResimWaitForUpdate->GetBool()  && RewindData->IsFinalResim())
			{
				for (auto Itr = ObjectToTarget.CreateIterator(); Itr; ++Itr)
				{
					FReplicatedPhysicsTargetAsync& Target = Itr.Value();

					// If final resim frame, mark interpolated targets as waiting for up to date data from the server.
					if (Target.RepMode == EPhysicsReplicationMode::PredictiveInterpolation)
					{
						Target.bWaiting = true;
						Target.ServerFrame = RigidsSolver->GetCurrentFrame() + Target.FrameOffset;
					}
				}
			}

			return;
		}

		// Update async targets with target input
		for (const FPhysicsRepAsyncInputData& Input : AsyncInput->InputData)
		{
			UpdateRewindDataTarget(Input);
			UpdateAsyncTarget(Input);
		}

		ApplyTargetStatesAsync(GetDeltaTime_Internal(), AsyncInput->ErrorCorrection, AsyncInput->InputData);
	}
}

void FPhysicsReplicationAsyncVR::UpdateRewindDataTarget(const FPhysicsRepAsyncInputData& Input)
{
	if (Input.PhysicsObject == nullptr)
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
	Chaos::FPBDRigidParticleHandle* Handle = Interface.GetRigidParticle(Input.PhysicsObject);

	if (Handle != nullptr)
	{
		// Cache all target states inside RewindData
		const int32 LocalFrame = Input.ServerFrame - Input.FrameOffset;
		RewindData->SetTargetStateAtFrame(*Handle, LocalFrame, Chaos::FFrameAndPhase::EParticleHistoryPhase::PostPushData,
			Input.TargetState.Position, Input.TargetState.Quaternion,
			Input.TargetState.LinVel, Input.TargetState.AngVel, (Input.TargetState.Flags & ERigidBodyFlags::Sleeping));
	}
}

void FPhysicsReplicationAsyncVR::UpdateAsyncTarget(const FPhysicsRepAsyncInputData& Input)
{
	if (Input.PhysicsObject == nullptr)
	{
		return;
	}

	FReplicatedPhysicsTargetAsync* Target = ObjectToTarget.Find(Input.PhysicsObject);
	if (Target == nullptr)
	{
		// First time we add a target, set it's previous and correction
		// positions to the target position to avoid math with uninitialized
		// memory.
		Target = &ObjectToTarget.Add(Input.PhysicsObject);
		Target->PrevPos = Input.TargetState.Position;
		Target->PrevPosTarget = Input.TargetState.Position;
		Target->PrevRotTarget = Input.TargetState.Quaternion;
		Target->PrevLinVel = Input.TargetState.LinVel;
	}

	if (Input.ServerFrame > Target->ServerFrame)
	{
		Target->PhysicsObject = Input.PhysicsObject;
		Target->PrevServerFrame = Target->ServerFrame;
		Target->ServerFrame = Input.ServerFrame;
		Target->TargetState = Input.TargetState;
		Target->RepMode = Input.RepMode;
		Target->FrameOffset = Input.FrameOffset;
		Target->TickCount = 0;
		Target->bWaiting = false;

		if (Input.RepMode == EPhysicsReplicationMode::PredictiveInterpolation)
		{
			// Cache the position we received this target at, Predictive Interpolation will alter the target state but use this as the source position for reconciliation.
			Target->PrevPosTarget = Input.TargetState.Position;
			Target->PrevRotTarget = Input.TargetState.Quaternion;
		}
	}

	/** Cache the latest ping time */
	LatencyOneWay = Input.LatencyOneWay;
}

void FPhysicsReplicationAsyncVR::ApplyTargetStatesAsync(const float DeltaSeconds, const FPhysicsRepErrorCorrectionData& ErrorCorrection, const TArray<FPhysicsRepAsyncInputData>& InputData)
{
	using namespace Chaos;

	// Deprecated, BodyInstance flow
	for (const FPhysicsRepAsyncInputData& Input : InputData)
	{
		if (Input.Proxy != nullptr)
		{
			Chaos::FSingleParticlePhysicsProxy* Proxy = Input.Proxy;
			Chaos::FRigidBodyHandle_Internal* Handle = Proxy->GetPhysicsThreadAPI();

			const FPhysicsRepErrorCorrectionData& UsedErrorCorrection = Input.ErrorCorrection.IsSet() ? Input.ErrorCorrection.GetValue() : ErrorCorrection;
			DefaultReplication_DEPRECATED(Handle, Input, DeltaSeconds, UsedErrorCorrection);
		}
	}

	// PhysicsObject flow
	for (auto Itr = ObjectToTarget.CreateIterator(); Itr; ++Itr)
	{
		bool bRemoveItr = true; // Remove current cached replication target unless replication logic tells us to store it for next tick

		FReplicatedPhysicsTargetAsync& Target = Itr.Value();

		if (Target.PhysicsObject != nullptr)
		{
			Chaos::FWritePhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetWrite();
			FPBDRigidParticleHandle* Handle = Interface.GetRigidParticle(Target.PhysicsObject);

			if (Handle != nullptr)
			{
				// TODO, Remove the resim option from project settings, we only need the physics prediction one now
				EPhysicsReplicationMode RepMode = Target.RepMode;
				if (!Chaos::FPBDRigidsSolver::IsPhysicsResimulationEnabled() && RepMode == EPhysicsReplicationMode::Resimulation)
				{
					RepMode = EPhysicsReplicationMode::Default;
				}

				switch (RepMode)
				{
				case EPhysicsReplicationMode::Default:
					bRemoveItr = DefaultReplication(Handle, Target, DeltaSeconds);
					break;

				case EPhysicsReplicationMode::PredictiveInterpolation:
					bRemoveItr = PredictiveInterpolation(Handle, Target, DeltaSeconds);
					break;

				case EPhysicsReplicationMode::Resimulation:
					bRemoveItr = ResimulationReplication(Handle, Target, DeltaSeconds);
					break;
				}
			}
		}

		if (bRemoveItr)
		{
			Itr.RemoveCurrent();
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
		= Handle->DebugName() ? *Handle->DebugName() : FString(TEXT(""));
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
	const float MaxLinearHardSnapDistance = CVarMaxLinearHardSnapDistance->GetFloat() >= 0.f ? CVarMaxLinearHardSnapDistance->GetFloat() : ErrorCorrectionDefault.MaxLinearHardSnapDistance;

	// Get Current state
	FRigidBodyState CurrentState;
	CurrentState.Position = Handle->X();
	CurrentState.Quaternion = Handle->R();
	CurrentState.AngVel = Handle->W();
	CurrentState.LinVel = Handle->V();


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
			Handle->SetX(TargetPos);
			Handle->SetR(TargetQuat);
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
	if (Target.bWaiting)
	{
		return false;
	}

	Chaos::FPBDRigidsSolver* RigidsSolver = static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
	if (RigidsSolver == nullptr)
	{
		return true;
	}

	static const auto CVarErrorAccumulationSeconds = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ErrorAccumulationSeconds"));
	const float ErrorAccumulationSeconds = CVarErrorAccumulationSeconds->GetFloat() >= 0.0f ? CVarErrorAccumulationSeconds->GetFloat() : ErrorCorrectionDefault.ErrorAccumulationSeconds;
	
	static const auto CVarMaxRestoredStateError = IConsoleManager::Get().FindConsoleVariable(TEXT("p.MaxRestoredStateError"));
	const float MaxRestoredStateErrorSqr = CVarMaxRestoredStateError->GetFloat() >= 0.0f ?
		(CVarMaxRestoredStateError->GetFloat() * CVarMaxRestoredStateError->GetFloat()) :
		(ErrorCorrectionDefault.MaxRestoredStateError * ErrorCorrectionDefault.MaxRestoredStateError);

	const bool bShouldSleep = (Target.TargetState.Flags & ERigidBodyFlags::Sleeping) != 0;
	const int32 LocalFrame = Target.ServerFrame - Target.FrameOffset;
	const int32 NumPredictedFrames = RigidsSolver->GetCurrentFrame() - LocalFrame - Target.TickCount;
	const float PredictedTime = DeltaSeconds * NumPredictedFrames;
	const float SendRate = (Target.ServerFrame - Target.PrevServerFrame) * DeltaSeconds;

	static const auto CVarPosCorrectionTimeMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.PosCorrectionTimeMultiplier"));
	const float PosCorrectionTime = PredictedTime * CVarPosCorrectionTimeMultiplier->GetFloat();

	static const auto CVarInterpolationTimeMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.InterpolationTimeMultiplier"));
	const float InterpolationTime = SendRate * CVarInterpolationTimeMultiplier->GetFloat();

	// CurrentState
	FRigidBodyState CurrentState;
	CurrentState.Position = Handle->X();
	CurrentState.Quaternion = Handle->R();
	CurrentState.LinVel = Handle->V();
	CurrentState.AngVel = Handle->W();

	// NewState
	const FVector TargetPos = Target.TargetState.Position;
	const FQuat TargetRot = Target.TargetState.Quaternion;
	const FVector TargetLinVel = Target.TargetState.LinVel;
	const FVector TargetAngVel = Target.TargetState.AngVel;


	/** --- Reconciliation ---
	* Get the traveled direction and distance from previous frame and compare with replicated linear velocity.
	* If the object isn't moving enough along the replicated velocity it's considered stuck and needs a hard reconciliation.
	*/
	const FVector PrevDiff = CurrentState.Position - Target.PrevPos;
	const float	ExpectedDistance = (Target.PrevLinVel * DeltaSeconds).Size();
	const float CoveredDistance = FVector::DotProduct(PrevDiff, Target.PrevLinVel.GetSafeNormal());

	// If the object is moving less than X% of the expected distance, accumulate error seconds
	static const auto CVarMinExpectedDistanceCovered = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.MinExpectedDistanceCovered"));
	if (CoveredDistance / ExpectedDistance < CVarMinExpectedDistanceCovered->GetFloat())
	{
		Target.AccumulatedErrorSeconds += DeltaSeconds;
	}
	else
	{
		Target.AccumulatedErrorSeconds = FMath::Max(Target.AccumulatedErrorSeconds - DeltaSeconds, 0.0f);
	}

	static const auto CVarbPredictiveInterpolationAlwaysHardSnap = IConsoleManager::Get().FindConsoleVariable(TEXT("p.PredictiveInterpolation.AlwaysHardSnap"));
	const bool bHardSnap = Target.AccumulatedErrorSeconds > ErrorAccumulationSeconds || CVarbPredictiveInterpolationAlwaysHardSnap->GetBool();
	bool bClearTarget = bHardSnap;
	if (bHardSnap)
	{
		// Too much error so just snap state here and be done with it
		Target.AccumulatedErrorSeconds = 0.0f;
		Handle->SetX(Target.PrevPosTarget);
		Handle->SetP(Target.PrevPosTarget);
		Handle->SetR(Target.PrevRotTarget);
		Handle->SetQ(Target.PrevRotTarget);
		Handle->SetV(Target.TargetState.LinVel);
		Handle->SetW(Target.TargetState.AngVel);
	}
	else // Velocity-based Replication
	{
		const Chaos::EObjectStateType ObjectState = Handle->ObjectState();
		if (ObjectState != Chaos::EObjectStateType::Dynamic)
		{
			RigidsSolver->GetEvolution()->SetParticleObjectState(Handle, Chaos::EObjectStateType::Dynamic);
		}


		// --- Velocity Replication ---
		// Get PosDiff
		const FVector PosDiff = TargetPos - CurrentState.Position;

		// Convert PosDiff to a velocity
		const FVector PosDiffVelocity = PosDiff / PosCorrectionTime;

		// Get LinVelDiff by adding inverted CurrentState.LinVel to TargetLinVel
		const FVector LinVelDiff = -CurrentState.LinVel + TargetLinVel;

		// Add PosDiffVelocity to LinVelDiff to get BlendedTargetVelocity
		const FVector BlendedTargetVelocity = LinVelDiff + PosDiffVelocity;

		// Multiply BlendedTargetVelocity with(deltaTime / interpolationTime), clamp to 1 and add to CurrentState.LinVel to get BlendedTargetVelocityInterpolated
		const float BlendStepAmount = FMath::Clamp(DeltaSeconds / InterpolationTime, 0.f, 1.f);
		const FVector RepLinVel = CurrentState.LinVel + (BlendedTargetVelocity * BlendStepAmount);


		// --- Angular Velocity Replication ---
		// Extrapolate current rotation along current angular velocity to see where we would end up
		float CurAngVelSize;
		FVector CurAngVelAxis;
		CurrentState.AngVel.FVector::ToDirectionAndLength(CurAngVelAxis, CurAngVelSize);
		CurAngVelSize = FMath::DegreesToRadians(CurAngVelSize);
		const FQuat CurRotExtrapDelta = FQuat(CurAngVelAxis, CurAngVelSize * DeltaSeconds);
		const FQuat CurRotExtrap = CurRotExtrapDelta * CurrentState.Quaternion;

		// Slerp from the extrapolated current rotation towards the target rotation
		// This takes current angular velocity into account
		const FQuat TargetRotBlended = FQuat::Slerp(CurRotExtrap, TargetRot, BlendStepAmount);

		// Get the rotational offset between the blended rotation target and the current rotation
		const FQuat TargetRotDelta = TargetRotBlended * CurrentState.Quaternion.Inverse();

		// Convert the rotational delta to angular velocity with a magnitude that will make it arrive at the rotation after DeltaTime has passed
		float WAngle;
		FVector WAxis;
		TargetRotDelta.ToAxisAndAngle(WAxis, WAngle);

		const FVector RepAngVel = WAxis * (WAngle / DeltaSeconds);


		// Apply velocity
		Handle->SetV(RepLinVel);
		Handle->SetW(RepAngVel);


		// Cache data for reconciliation
		Target.PrevPos = FVector(CurrentState.Position);
		Target.PrevLinVel = FVector(RepLinVel);
	}


	if (bShouldSleep)
	{
		// --- Sleep ---
		// Get the distance from the current position to the source position of our target state
		const float SourceDistanceSqr = (Target.PrevPosTarget - CurrentState.Position).SizeSquared();

		// Don't allow kinematic to sleeping transition
		static const auto CVarMaxDistanceToSleepSqr = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.MaxDistanceToSleepSqr"));
		if (SourceDistanceSqr < CVarMaxDistanceToSleepSqr->GetFloat() && !Handle->IsKinematic())
		{
			RigidsSolver->GetEvolution()->SetParticleObjectState(Handle, Chaos::EObjectStateType::Sleeping);
			bClearTarget = true;
		}
	}
	else
	{
		// --- Target Extrapolation ---
		static const auto CVarExtrapolationTimeMultiplier = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.PredictiveInterpolation.ExtrapolationTimeMultiplier"));
		if ((Target.TickCount * DeltaSeconds) < SendRate * CVarExtrapolationTimeMultiplier->GetFloat())
		{
			// Extrapolate target position
			Target.TargetState.Position = Target.TargetState.Position + Target.TargetState.LinVel * DeltaSeconds;

			// Extrapolate target rotation
			float TargetAngVelSize;
			FVector TargetAngVelAxis;
			Target.TargetState.AngVel.FVector::ToDirectionAndLength(TargetAngVelAxis, TargetAngVelSize);
			TargetAngVelSize = FMath::DegreesToRadians(TargetAngVelSize);
			const FQuat TargetRotExtrapDelta = FQuat(TargetAngVelAxis, TargetAngVelSize * DeltaSeconds);
			Target.TargetState.Quaternion = TargetRotExtrapDelta * Target.TargetState.Quaternion;
		}
	}

	Target.TickCount++;

	return bClearTarget;;
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

	const int32 LocalFrame = Target.ServerFrame - Target.FrameOffset;

	if (LocalFrame <= RewindData->CurrentFrame() && LocalFrame >= RewindData->GetEarliestFrame_Internal())
	{
		static constexpr Chaos::FFrameAndPhase::EParticleHistoryPhase RewindPhase = Chaos::FFrameAndPhase::EParticleHistoryPhase::PostPushData;

		FAsyncPhysicsTimestamp TimeStamp;
		TimeStamp.LocalFrame = RewindData->CurrentFrame();

		const float ResimErrorThreshold = Chaos::FPhysicsSolverBase::ResimulationErrorThreshold();

		auto PastState = RewindData->GetPastStateAtFrame(*Handle, LocalFrame, RewindPhase);

		const FVector ErrorOffset = (PastState.X() - Target.TargetState.Position);
		const float ErrorDistance = ErrorOffset.Size();
		const bool ShouldTriggerResim = ErrorDistance >= ResimErrorThreshold;
		float ColorLerp = ShouldTriggerResim ? 1.0f : 0.0f;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

		if (Chaos::FPhysicsSolverBase::CanDebugNetworkPhysicsPrediction())
		{
			UE_LOG(LogTemp, Log, TEXT("Apply Rigid body state at local frame %d with offset = %d"), LocalFrame, Target.FrameOffset);
			UE_LOG(LogTemp, Log, TEXT("Particle Position Error = %f | Should Trigger Resim = %s | Server Frame = %d | Client Frame = %d"), ErrorDistance, (ShouldTriggerResim ? TEXT("True") : TEXT("False")), Target.ServerFrame, LocalFrame);
			UE_LOG(LogTemp, Log, TEXT("Particle Target Position = %s | Current Position = %s"), *Target.TargetState.Position.ToString(), *PastState.X().ToString());
			UE_LOG(LogTemp, Log, TEXT("Particle Target Velocity = %s | Current Velocity = %s"), *Target.TargetState.LinVel.ToString(), *PastState.V().ToString());
			UE_LOG(LogTemp, Log, TEXT("Particle Target Quaternion = %s | Current Quaternion = %s"), *Target.TargetState.Quaternion.ToString(), *PastState.R().ToString());
			UE_LOG(LogTemp, Log, TEXT("Particle Target Omega = %s | Current Omega= %s"), *Target.TargetState.AngVel.ToString(), *PastState.W().ToString());

			{ // DrawDebug
				static constexpr float BoxSize = 5.0f;
				const FColor DebugColor = FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Red, ColorLerp).ToFColor(false);

				static const auto CVarNetCorrectionLifetime = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetCorrectionLifetime"));

				Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(Target.TargetState.Position, FVector(BoxSize, BoxSize, BoxSize), Target.TargetState.Quaternion, FColor::Orange, true, CVarNetCorrectionLifetime->GetFloat(), 0, 1.0f);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugBox(PastState.X(), FVector(6, 6, 6), PastState.R(), DebugColor, true, CVarNetCorrectionLifetime->GetFloat(), 0, 1.0f);

				Chaos::FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(PastState.X(), Target.TargetState.Position, 5.0f, FColor::Green, true, CVarNetCorrectionLifetime->GetFloat(), 0, 0.5f);
			}
		}
#endif

		if (ShouldTriggerResim)
		{
			RigidsSolver->GetEvolution()->GetIslandManager().SetParticleResimFrame(Handle, LocalFrame);

			int32 ResimFrame = RewindData->GetResimFrame();
			ResimFrame = (ResimFrame == INDEX_NONE) ? LocalFrame : FMath::Min(ResimFrame, LocalFrame);
			RewindData->SetResimFrame(ResimFrame);
		}
	}
	else if (LocalFrame > 0)
	{
		UE_LOG(LogPhysics, Warning, TEXT("FPhysicsReplication::ApplyRigidBodyState target frame (%d) out of rewind data bounds (%d,%d)"), LocalFrame,
			RewindData->GetEarliestFrame_Internal(), RewindData->CurrentFrame());
	}

	return true;
}

FName FPhysicsReplicationAsyncVR::GetFNameForStatId() const
{
	const static FLazyName StaticName("FPhysicsReplicationAsyncCallback");
	return StaticName;
}