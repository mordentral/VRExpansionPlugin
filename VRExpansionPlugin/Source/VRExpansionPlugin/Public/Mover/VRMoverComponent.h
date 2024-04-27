// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "MoverComponent.h"
#include "MoverTypes.h"
#include "GameFramework/Pawn.h"
#include "LayeredMove.h"
#include "VRMoverComponent.generated.h"

class UCurveVector;
class UCurveFloat;

DECLARE_LOG_CATEGORY_EXTERN(LogVRMoverComponent, Log, All);

/** Linear Velocity: A method of inducing a straight-line velocity on an actor over time  */
USTRUCT(BlueprintType)
struct VREXPANSIONPLUGIN_API FLayeredMove_VRMovement : public FLayeredMoveBase
{
	GENERATED_USTRUCT_BODY()

	FLayeredMove_VRMovement();

	virtual ~FLayeredMove_VRMovement() {}

	// Units per second, could be worldspace vs relative depending on Flags
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
		FVector Velocity;

	// Optional curve float for controlling the magnitude of the velocity applied to the actor over the duration of the move
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
		TObjectPtr<UCurveFloat> MagnitudeOverTime;

	// @see ELayeredMove_ConstantVelocitySettingsFlags
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Mover)
		uint8 SettingsFlags;

	// Never finish this movement
	virtual bool IsFinished(float CurrentSimTimeMs) const;

	// Generate a movement 
	virtual bool GenerateMove(const FMoverTickStartData& StartState, const FMoverTimeStep& TimeStep, const UMoverComponent* MoverComp, UMoverBlackboard* SimBlackboard, FProposedMove& OutProposedMove) override;

	virtual FLayeredMoveBase* Clone() const override;

	virtual void NetSerialize(FArchive& Ar) override;

	virtual UScriptStruct* GetScriptStruct() const override;

	virtual FString ToSimpleString() const override;

	virtual void AddReferencedObjects(class FReferenceCollector& Collector) override;
};

template<>
struct TStructOpsTypeTraits< FLayeredMove_VRMovement > : public TStructOpsTypeTraitsBase2< FLayeredMove_VRMovement >
{
	enum
	{
		//WithNetSerializer = true,
		WithCopy = true
	};
};

// Data block containing basic sync state information
USTRUCT(BlueprintType)
struct VREXPANSIONPLUGIN_API FVRMoverHMDSyncState : public FMoverDataStructBase
{
	GENERATED_USTRUCT_BODY()

public:
	// Movement intent direction relative to MovementBase if set, world space otherwise. Magnitude of range (0-1)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Mover)
		FVector MoveDirectionIntent;

public:

	FVRMoverHMDSyncState()
	{
		// In constructor list when the actual var is made
		MoveDirectionIntent = FVector::ZeroVector;
	}

	virtual ~FVRMoverHMDSyncState() {}

	// @return newly allocated copy of this FMoverDefaultSyncState. Must be overridden by child classes
	virtual FMoverDataStructBase* Clone() const override;

	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;

	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }

	// Need implemented (if needed)
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override
	{
		return false;
	}

	// Needs implemented (should we even do this?)
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float Pct) override
	{
		return;
	}


};

template<>
struct TStructOpsTypeTraits< FVRMoverHMDSyncState > : public TStructOpsTypeTraitsBase2< FVRMoverHMDSyncState >
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true
	};
};



UCLASS()
class VREXPANSIONPLUGIN_API AVRMoverBasePawn : public APawn, public IMoverInputProducerInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	AVRMoverBasePawn(const FObjectInitializer& ObjectInitializer);

	/** Accessor for the actor's movement component */
	UFUNCTION(BlueprintPure, Category = Mover)
		UMoverComponent* GetMoverComponent() const { return CharacterMotionComponent; }

	virtual void BeginPlay() override;

protected:

	virtual UPrimitiveComponent* GetMovementBase() const override;

	// Entry point for input production. Do not override. To extend in derived character types, override OnProduceInput for native types or implement "Produce Input" blueprint event
	virtual void ProduceInput_Implementation(int32 SimTimeMs, FMoverInputCmdContext& InputCmdResult) override;

	// Override this function in native class to author input for the next simulation frame. Consider also calling Super method.
	virtual void OnProduceInput(float DeltaMs, FMoverInputCmdContext& InputCmdResult);

	// Implement this event in Blueprints to author input for the next simulation frame. Consider also calling Parent event.
	UFUNCTION(BlueprintImplementableEvent, DisplayName = "On Produce Input", meta = (ScriptName = "OnProduceInput"))
		FMoverInputCmdContext OnProduceInputInBlueprint(float DeltaMs, FMoverInputCmdContext InputCmd);

protected:
	UPROPERTY(Category = Movement, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		TObjectPtr<UMoverComponent> CharacterMotionComponent;

	uint8 bHasProduceInputinBpFunc : 1;
};