// Fill out your copyright notice in the Description page of Project Settings.

#include "Grippables/GrippablePhysicsReplication.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GrippablePhysicsReplication)

#include "UObject/ObjectMacros.h"
#include "UObject/Interface.h"
#include "DrawDebugHelpers.h"
#include "Chaos/ChaosMarshallingManager.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "VRGlobalSettings.h"
#include "PhysicsEngine/PhysicsSettings.h"
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

struct FAsyncPhysicsRepCallbackDataVR : public Chaos::FSimCallbackInput
{
	TArray<FAsyncPhysicsDesiredState> Buffer;
	ErrorCorrectionData ErrorCorrection;

	void Reset()
	{
		Buffer.Reset();
	}
};

class FPhysicsReplicationAsyncCallbackVR final : public Chaos::TSimCallbackObject<FAsyncPhysicsRepCallbackDataVR>
{
	virtual void OnPreSimulate_Internal() override
	{
		FPhysicsReplicationVR::ApplyAsyncDesiredStateVR(GetDeltaTime_Internal(), GetConsumerInput_Internal());
	}
};

FPhysicsReplicationVR::~FPhysicsReplicationVR()
{
	if (AsyncCallbackServer)
	{
		if (auto* Solver = PhysSceneVR->GetSolver())
		{
			Solver->UnregisterAndFreeSimCallbackObject_External(AsyncCallbackServer);
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
	CurAsyncDataVR = nullptr;
	AsyncCallbackServer = nullptr;
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

	CurAsyncDataVR = AsyncCallbackServer->GetProducerInputData_External();
	CurAsyncDataVR->ErrorCorrection.PositionLerp = PositionLerp;
	CurAsyncDataVR->ErrorCorrection.AngleLerp = AngleLerp;
	CurAsyncDataVR->ErrorCorrection.LinearVelocityCoefficient = LinearVelocityCoefficient;
	CurAsyncDataVR->ErrorCorrection.AngularVelocityCoefficient = AngularVelocityCoefficient;
}

void FPhysicsReplicationVR::ApplyAsyncDesiredStateVR(const float DeltaSeconds, const FAsyncPhysicsRepCallbackDataVR* AsyncData)
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
}


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

	if (ShouldSkipPhysicsReplication())
	{
		return false;
	}

	if (!BI->IsInstanceSimulatingPhysics())
	{
		return false;
	}

	// LocalFrame the local frame number we should use to access past state in the rewind data
	// NumPredictedFrames is how many frames (steps) ahead "now" is for the client compared to the latest data we've received from the server (use this to determine accurately how far ahead this object should extrapolate from its "physics target"

	Chaos::FPhysicsSolver* Solver = PhysSceneVR->GetSolver();
	Chaos::FRewindData* RewindData = Solver->GetRewindData();

	if (RewindData && LocalFrame > RewindData->GetEarliestFrame_Internal() && LocalFrame < RewindData->CurrentFrame())
	{
		auto Proxy = BI->GetPhysicsActorHandle();
		const auto P = RewindData->GetPastStateAtFrame(*Proxy->GetHandle_LowLevel(), LocalFrame);

#if 0
		// Debugging/test: compare and print out if locations differ
		if (FVector::DistSquared(PhysicsTarget.TargetState.Position, P.X()) > 1.f)
		{
			const int32 EarliestFrame = RewindData->GetEarliestFrame_Internal();
			const int32 CurrentFrame = RewindData->CurrentFrame();

			UE_LOG(LogTemp, Warning, TEXT(""));
			UE_LOG(LogTemp, Warning, TEXT("Location differs"));
			UE_LOG(LogTemp, Warning, TEXT("   Replicated: %s"), *PhysicsTarget.TargetState.Position.ToString());
			UE_LOG(LogTemp, Warning, TEXT("   Historic: %s"), *P.X().ToString());
		}
#endif
	}

	// Call into the old ApplyRigidBodyState function for now,
	// "new leash mode" should replace this.
	// Note that old ApplyRigidBodyState is overridden in other projects, so consider backwards compat path
	float PingSecondsOneWay = RewindData == nullptr ? InPingSecondsOneWay : (Solver->GetLastDt() * NumPredictedFrames) * 0.5f;
	return ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, ErrorCorrection, PingSecondsOneWay);
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
			if (AsyncCallbackServer == nullptr)	//sync case
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
				FAsyncPhysicsDesiredState AsyncDesiredState;
				AsyncDesiredState.WorldTM = IdealWorldTM;
				AsyncDesiredState.LinearVelocity = NewState.LinVel;
				AsyncDesiredState.AngularVelocity = NewState.AngVel;
				AsyncDesiredState.Proxy = static_cast<Chaos::FSingleParticlePhysicsProxy*>(BI->GetPhysicsActorHandle());
				AsyncDesiredState.ErrorCorrection = { ErrorCorrection.LinearVelocityCoefficient, ErrorCorrection.AngularVelocityCoefficient, ErrorCorrection.PositionLerp, ErrorCorrection.AngleLerp };
				AsyncDesiredState.bShouldSleep = bShouldSleep;
				CurAsyncDataVR->Buffer.Add(AsyncDesiredState);
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
		if (AsyncCallbackServer == nullptr)
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
	if (AsyncCallbackServer == nullptr)
	{
		if (auto* Solver = PhysSceneVR->GetSolver())
		{
			AsyncCallbackServer = Solver->CreateAndRegisterSimCallbackObject_External<FPhysicsReplicationAsyncCallbackVR>();
		}
	}

	using namespace Chaos;


	int32 LocalFrameOffset = 0;		// LocalFrame = ServerFrame + LocalFrameOffset;
	int32 NumPredictedFrames = 0;	// How many frames "ahead" of the server we are predicting. That is, how many frames are in flight between us and the server.

	if (UWorld* World = GetOwningWorld())
	{
		if (World->GetNetMode() == NM_Client)
		{
			if (APlayerController* PlayerController = World->GetFirstPlayerController())
			{
				Chaos::FPhysicsSolver* Solver = PhysSceneVR->GetSolver();
				check(Solver);

				static IConsoleVariable* EnableNetworkPhysicsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.EnableNetworkPhysicsPrediction"));
				if (EnableNetworkPhysicsCVar && EnableNetworkPhysicsCVar->GetInt() == 1)
				{
					LocalFrameOffset = PlayerController->GetLocalToServerAsyncPhysicsTickOffset();
				}
				//TODO: as we send physics updates down we need to record latest seen
				//NumPredictedFrames = Solver->GetCurrentFrame() - ClientFrameInfo.LastProcessedInputFrame;
			}
		}
	}

	const FRigidBodyErrorCorrection& PhysicErrorCorrection = UPhysicsSettings::Get()->PhysicErrorCorrection;

	if (AsyncCallbackServer)
	{
		PrepareAsyncData_ExternalVR(PhysicErrorCorrection);
	}

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
								const bool bRestoredState = ApplyRigidBodyState(DeltaSeconds, BI, PhysicsTarget, PhysicErrorCorrection, PingSecondsOneWay, LocalFrame, NumPredictedFrames);

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
			}

			if (bRemoveItr)
			{
				OnTargetRestored(Itr.Key().Get(), Itr.Value());
				Itr.RemoveCurrent();
			}
		}
	}
	CurAsyncDataVR = nullptr;

	//GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Phys Rep Tick!"));
	//FPhysicsReplication::OnTick(DeltaSeconds, ComponentsToTargets);
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