// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "Components/PrimitiveComponent.h"
//#include "Grippables/GrippablePhysicsReplication.h"
#include "CollisionIgnoreSubsystem.generated.h"
//#include "GrippablePhysicsReplication.generated.h"


DECLARE_LOG_CATEGORY_EXTERN(VRE_CollisionIgnoreLog, Log, All);

USTRUCT()
struct FCollisionPrimPair
{
	GENERATED_BODY()
public:

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Prim1;
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Prim2;

	FCollisionPrimPair()
	{
		Prim1 = nullptr;
		Prim2 = nullptr;
	}

	FORCEINLINE bool operator==(const FCollisionPrimPair& Other) const
	{
		/*if (!Prim1.IsValid() || !Prim2.IsValid())
			return false;

		if (!Other.Prim1.IsValid() || !Other.Prim2.IsValid())
			return false;*/
		
		return(
			(Prim1.Get() == Other.Prim1.Get() || Prim1.Get() == Other.Prim2.Get()) &&
			(Prim2.Get() == Other.Prim1.Get() || Prim2.Get() == Other.Prim2.Get())
			);
	}

	friend uint32 GetTypeHash(const FCollisionPrimPair& InKey)
	{
		return GetTypeHash(InKey.Prim1) ^ GetTypeHash(InKey.Prim2);
	}

};

USTRUCT()
struct FCollisionIgnorePair
{
	GENERATED_BODY()
public:

	///FCollisionPrimPair PrimitivePair;
	FPhysicsActorHandle Actor1;
	UPROPERTY()
	FName BoneName1;
	FPhysicsActorHandle Actor2;
	UPROPERTY()
	FName BoneName2;

	// Flip our elements to retain a default ordering in an array
	void FlipElements()
	{
		FPhysicsActorHandle tH = Actor1;
		Actor1 = Actor2;
		Actor2 = tH;

		FName tN = BoneName1;
		BoneName1 = BoneName2;
		BoneName2 = tN;
	}

	FORCEINLINE bool operator==(const FCollisionIgnorePair& Other) const
	{
		return (
			(BoneName1 == Other.BoneName1 || BoneName1 == Other.BoneName2) &&
			(BoneName2 == Other.BoneName2 || BoneName2 == Other.BoneName1)
			);
	}

	FORCEINLINE bool operator==(const FName& Other) const
	{
		return (BoneName1 == Other || BoneName2 == Other);
	}
};

USTRUCT()
struct FCollisionIgnorePairArray
{
	GENERATED_BODY()
public:

	UPROPERTY()
	TArray<FCollisionIgnorePair> PairArray;
};

UCLASS()
class VREXPANSIONPLUGIN_API UCollisionIgnoreSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:


	UCollisionIgnoreSubsystem() :
		Super()
	{
	}

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
		// Not allowing for editor type as this is a replication subsystem
	}

	/** Implement this for initialization of instances of the system */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override
	{
		Super::Initialize(Collection);
	}

	virtual void Deinitialize() override
	{
		Super::Deinitialize();

		if (UpdateHandle.IsValid())
		{
			GetWorld()->GetTimerManager().ClearTimer(UpdateHandle);
		}
	}

	UPROPERTY()
	TMap<FCollisionPrimPair, FCollisionIgnorePairArray> CollisionTrackedPairs;
	//TArray<FCollisionIgnorePair> CollisionTrackedPairs;

	UPROPERTY()
	TMap<FCollisionPrimPair, FCollisionIgnorePairArray> RemovedPairs;
	//TArray<FCollisionIgnorePair> RemovedPairs;

	//
	void UpdateTimer()
	{
		RemovedPairs.Reset();

		if (CollisionTrackedPairs.Num() > 0)
		{
			if (!UpdateHandle.IsValid())
			{
				// Setup the heartbeat on 1htz checks
				GetWorld()->GetTimerManager().SetTimer(UpdateHandle, this, &UCollisionIgnoreSubsystem::CheckActiveFilters, 1.0f, true, 1.0f);
			}
		}
		else if (UpdateHandle.IsValid())
		{
			GetWorld()->GetTimerManager().ClearTimer(UpdateHandle);
		}
	}

	UFUNCTION(Category = "Collision")
		void CheckActiveFilters();

	// #TODO implement this, though it should be rare
	void InitiateIgnore();

	void SetComponentCollisionIgnoreState(bool bIterateChildren1, bool bIterateChildren2, UPrimitiveComponent* Prim1, FName OptionalBoneName1, UPrimitiveComponent* Prim2, FName OptionalBoneName2, bool bIgnoreCollision, bool bCheckFilters = false);
	void RemoveComponentCollisionIgnoreState(UPrimitiveComponent* Prim1);
	bool IsComponentIgnoringCollision(UPrimitiveComponent* Prim1);
private:

	FTimerHandle UpdateHandle;

};
