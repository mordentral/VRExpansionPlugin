
#include "Mover/VRMoverComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VRMoverComponent)

#include "VRBPDatatypes.h"
#include "DefaultMovementSet/LayeredMoves/BasicLayeredMoves.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Curves/CurveFloat.h" // Delete after tests, only needed for cloning
#include "ReplicatedVRCameraComponent.h"

DEFINE_LOG_CATEGORY(LogVRMoverComponent);


static const FName Name_CharacterMotionComponent(TEXT("MoverComponent"));

/*

	Layered move to inject VR movement into the mover component

*/


FLayeredMove_VRMovement::FLayeredMove_VRMovement()
	: Velocity(FVector::ZeroVector)
	, MagnitudeOverTime(nullptr)
	, SettingsFlags(0)
{
	// This should never end
	DurationMs = -1.0f;
}

bool FLayeredMove_VRMovement::IsFinished(float CurrentSimTimeMs) const
{
	// We never end the VR velocity injection
	return false;
}

bool FLayeredMove_VRMovement::GenerateMove(const FMoverTickStartData& SimState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove)
{
	const FMoverDefaultSyncState* SyncState = SimState.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
	check(SyncState);

	const float DeltaSeconds = TimeStep.StepMs * 0.001f;

	// Convert starting velocity based on starting orientation, if settings call for it
	if (SettingsFlags & (uint8)ELayeredMove_ConstantVelocitySettingsFlags::VelocityStartRelative &&
		StartSimTimeMs == TimeStep.BaseSimTimeMs)
	{
		SettingsFlags &= ~(uint8)ELayeredMove_ConstantVelocitySettingsFlags::VelocityStartRelative;
		Velocity = SyncState->GetOrientation_WorldSpace().RotateVector(Velocity);
	}

	FVector VelocityThisFrame = Velocity;

	// Put velocity into worldspace
	if (SettingsFlags & (uint8)ELayeredMove_ConstantVelocitySettingsFlags::VelocityAlwaysRelative)
	{
		VelocityThisFrame = SyncState->GetOrientation_WorldSpace().RotateVector(Velocity);
	}

	if (MagnitudeOverTime && DurationMs > 0)
	{
		const float TimeValue = DurationMs > 0.f ? FMath::Clamp((TimeStep.BaseSimTimeMs - StartSimTimeMs) / DurationMs, 0.f, 1.f) : TimeStep.BaseSimTimeMs;
		const float TimeFactor = MagnitudeOverTime->GetFloatValue(TimeValue);
		VelocityThisFrame *= TimeFactor;
	}

	OutProposedMove.LinearVelocity = VelocityThisFrame;

	return true;
}

FLayeredMoveBase* FLayeredMove_VRMovement::Clone() const
{
	FLayeredMove_VRMovement* CopyPtr = new FLayeredMove_VRMovement(*this);
	return CopyPtr;
}

void FLayeredMove_VRMovement::NetSerialize(FArchive& Ar)
{
	Super::NetSerialize(Ar);

	SerializePackedVector<10, 16>(Velocity, Ar);
	Ar << SettingsFlags;
	Ar << MagnitudeOverTime;
}

UScriptStruct* FLayeredMove_VRMovement::GetScriptStruct() const
{
	return FLayeredMove_VRMovement::StaticStruct();
}

FString FLayeredMove_VRMovement::ToSimpleString() const
{
	return FString::Printf(TEXT("LinearVelocity"));
}

void FLayeredMove_VRMovement::AddReferencedObjects(class FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(Collector);
}


/*

	VR HMD Sync State to store the HMD velocities
	Which should be pulled from input somehow
*/

FMoverDataStructBase* FVRMoverHMDSyncState::Clone() const
{
	// TODO: ensure that this memory allocation jives with deletion method
	FVRMoverHMDSyncState* CopyPtr = new FVRMoverHMDSyncState(*this);
	return CopyPtr;
}

bool FVRMoverHMDSyncState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Super::NetSerialize(Ar, Map, bOutSuccess);

	SerializeFixedVector<2, 8>(MoveDirectionIntent, Ar);
	bOutSuccess = true;
	return true;
}



AVRMoverBasePawn::AVRMoverBasePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	CharacterMotionComponent = CreateDefaultSubobject<UMoverComponent>(Name_CharacterMotionComponent);
	ensure(CharacterMotionComponent);

	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SetReplicatingMovement(false);	// disable Actor-level movement replication, since our Mover component will handle it

	// Let the code know if blueprints have implemented input
	auto IsImplementedInBlueprint = [](const UFunction* Func) -> bool
	{
		return Func && ensure(Func->GetOuter())
			&& Func->GetOuter()->IsA(UBlueprintGeneratedClass::StaticClass());
	};

	static FName ProduceInputBPFuncName = FName(TEXT("OnProduceInputInBlueprint"));
	UFunction* ProduceInputFunction = GetClass()->FindFunctionByName(ProduceInputBPFuncName);
	bHasProduceInputinBpFunc = IsImplementedInBlueprint(ProduceInputFunction);
}

void AVRMoverBasePawn::BeginPlay()
{
	Super::BeginPlay();

	// In constructor for the data instead of begin play? Might even be able to queue the layered move there too
	if (IsValid(CharacterMotionComponent))
	{ 
		// Add the sync data for our layered HMD movement injection
		CharacterMotionComponent->PersistentSyncStateDataTypes.Add(FMoverDataPersistence(FVRMoverHMDSyncState::StaticStruct(), true));

		// Needs to be handled off of possession instead?
		if (HasLocalNetOwner())
		{
			// Add the persistant HMDlayered move addition
			TSharedPtr<FLayeredMove_VRMovement> VRMoveLayer = MakeShared<FLayeredMove_VRMovement>();
			CharacterMotionComponent->QueueLayeredMove(VRMoveLayer);
		}
	}
}


UPrimitiveComponent* AVRMoverBasePawn::GetMovementBase() const
{
	return CharacterMotionComponent ? CharacterMotionComponent->GetMovementBase() : nullptr;
}


void AVRMoverBasePawn::ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult)
{
	OnProduceInput((float)SimTimeMs, InputCmdResult);

	if (bHasProduceInputinBpFunc)
	{
		InputCmdResult = OnProduceInputInBlueprint((float)SimTimeMs, InputCmdResult);
	}
}


void AVRMoverBasePawn::OnProduceInput(float DeltaMs, FMoverInputCmdContext& OutInputCmd)
{

	// Generate user commands. Called right before the Character movement simulation will tick (for a locally controlled pawn)
	// This isn't meant to be the best way of doing a camera system. It is just meant to show a couple of ways it may be done
	// and to make sure we can keep distinct the movement, rotation, and view angles.
	// Styles 1-3 are really meant to be used with a gamepad.
	//
	// Its worth calling out: the code that happens here is happening *outside* of the Character movement simulation. All we are doing
	// is generating the input being fed into that simulation. That said, this means that A) the code below does not run on the server
	// (and non controlling clients) and B) the code is not rerun during reconcile/resimulates. Use this information guide any
	// decisions about where something should go (such as aim assist, lock on targeting systems, etc): it is hard to give absolute
	// answers and will depend on the game and its specific needs. In general, at this time, I'd recommend aim assist and lock on 
	// targeting systems to happen /outside/ of the system, i.e, here. But I can think of scenarios where that may not be ideal too.

	//FCharacterDefaultInputs& CharacterInputs = OutInputCmd.InputCollection.FindOrAddMutableDataByType<FVRCharacterExtendedInputs>();

}
