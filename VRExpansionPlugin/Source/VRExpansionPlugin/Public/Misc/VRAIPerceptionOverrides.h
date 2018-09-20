// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "VRBaseCharacter.h"
#include "AIModule/Classes/GenericTeamAgentInterface.h"
#include "AIModule/Classes/Perception/AISense.h"
#include "AIModule/Classes/Perception/AISenseConfig.h"

#include "VRAIPerceptionOverrides.generated.h"

class IAISightTargetInterface;
class UAISense_Sight_VR;

class FGameplayDebuggerCategory;
class UAIPerceptionComponent;

UCLASS(meta = (DisplayName = "AI Sight VR config"))
class VREXPANSIONPLUGIN_API UAISenseConfig_Sight_VR : public UAISenseConfig
{
	GENERATED_UCLASS_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sense", NoClear, config)
		TSubclassOf<UAISense_Sight_VR> Implementation;

	/** Maximum sight distance to notice a target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sense", config, meta = (UIMin = 0.0, ClampMin = 0.0))
		float SightRadius;

	/** Maximum sight distance to see target that has been already seen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sense", config, meta = (UIMin = 0.0, ClampMin = 0.0))
		float LoseSightRadius;

	/** How far to the side AI can see, in degrees. Use SetPeripheralVisionAngle to change the value at runtime.
	*	The value represents the angle measured in relation to the forward vector, not the whole range. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sense", config, meta = (UIMin = 0.0, ClampMin = 0.0, UIMax = 180.0, ClampMax = 180.0, DisplayName = "PeripheralVisionHalfAngleDegrees"))
		float PeripheralVisionAngleDegrees;

	/** */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sense", config)
		FAISenseAffiliationFilter DetectionByAffiliation;

	/** If not an InvalidRange (which is the default), we will always be able to see the target that has already been seen if they are within this range of their last seen location. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sense", config, meta = (UIMin = 0.0, ClampMin = 0.0))
		float AutoSuccessRangeFromLastSeenLocation;

	virtual TSubclassOf<UAISense> GetSenseImplementation() const override;

#if WITH_GAMEPLAY_DEBUGGER
	virtual void DescribeSelfToGameplayDebugger(const UAIPerceptionComponent* PerceptionComponent, FGameplayDebuggerCategory* DebuggerCategory) const;
#endif // WITH_GAMEPLAY_DEBUGGER

};


namespace ESightPerceptionEventNameVR
{
	enum Type
	{
		Undefined,
		GainedSight,
		LostSight
	};
}

USTRUCT()
struct VREXPANSIONPLUGIN_API FAISightEventVR
{
	GENERATED_USTRUCT_BODY()

		typedef UAISense_Sight_VR FSenseClass;

	float Age;
	ESightPerceptionEventNameVR::Type EventType;

	UPROPERTY()
		AActor* SeenActor;

	UPROPERTY()
		AActor* Observer;

	FAISightEventVR() : SeenActor(nullptr), Observer(nullptr) {}

	FAISightEventVR(AActor* InSeenActor, AActor* InObserver, ESightPerceptionEventNameVR::Type InEventType)
		: Age(0.f), EventType(InEventType), SeenActor(InSeenActor), Observer(InObserver)
	{
	}
};

struct FAISightTargetVR
{
	typedef uint32 FTargetId;
	static const FTargetId InvalidTargetId;

	TWeakObjectPtr<AActor> Target;
	IAISightTargetInterface* SightTargetInterface;
	FGenericTeamId TeamId;
	FTargetId TargetId;

	FAISightTargetVR(AActor* InTarget = NULL, FGenericTeamId InTeamId = FGenericTeamId::NoTeam);

	FORCEINLINE FVector GetLocationSimple() const
	{
		// Changed this up to support my VR Characters
		const AVRBaseCharacter * VRChar = Cast<const AVRBaseCharacter>(Target);
		return Target.IsValid() ? (VRChar != nullptr ? VRChar->GetVRLocation_Inline() : Target->GetActorLocation()) : FVector::ZeroVector;
	}

	FORCEINLINE const AActor* GetTargetActor() const { return Target.Get(); }
};

struct FAISightQueryVR
{
	FPerceptionListenerID ObserverId;
	FAISightTargetVR::FTargetId TargetId;

	float Age;
	float Score;
	float Importance;

	FVector LastSeenLocation;

	uint32 bLastResult : 1;

	FAISightQueryVR(FPerceptionListenerID ListenerId = FPerceptionListenerID::InvalidID(), FAISightTargetVR::FTargetId Target = FAISightTargetVR::InvalidTargetId)
		: ObserverId(ListenerId), TargetId(Target), Age(0), Score(0), Importance(0), LastSeenLocation(FAISystem::InvalidLocation), bLastResult(false)
	{
	}

	void RecalcScore()
	{
		Score = Age + Importance;
	}

	void ForgetPreviousResult()
	{
		LastSeenLocation = FAISystem::InvalidLocation;
		bLastResult = false;
	}

	class FSortPredicate
	{
	public:
		FSortPredicate()
		{}

		bool operator()(const FAISightQueryVR& A, const FAISightQueryVR& B) const
		{
			return A.Score > B.Score;
		}
	};
};

UCLASS(ClassGroup = AI, config = Game)
class VREXPANSIONPLUGIN_API UAISense_Sight_VR : public UAISense
{
	GENERATED_UCLASS_BODY()

public:
	struct FDigestedSightProperties
	{
		float PeripheralVisionAngleCos;
		float SightRadiusSq;
		float AutoSuccessRangeSqFromLastSeenLocation;
		float LoseSightRadiusSq;
		uint8 AffiliationFlags;

		FDigestedSightProperties();
		FDigestedSightProperties(const UAISenseConfig_Sight_VR& SenseConfig);
	};

	typedef TMap<FAISightTargetVR::FTargetId, FAISightTargetVR> FTargetsContainer;
	FTargetsContainer ObservedTargets;
	TMap<FPerceptionListenerID, FDigestedSightProperties> DigestedProperties;

	TArray<FAISightQueryVR> SightQueryQueue;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "AI Perception", config)
		int32 MaxTracesPerTick;

	UPROPERTY(EditDefaultsOnly, Category = "AI Perception", config)
		int32 MinQueriesPerTimeSliceCheck;

	UPROPERTY(EditDefaultsOnly, Category = "AI Perception", config)
		double MaxTimeSlicePerTick;

	UPROPERTY(EditDefaultsOnly, Category = "AI Perception", config)
		float HighImportanceQueryDistanceThreshold;

	float HighImportanceDistanceSquare;

	UPROPERTY(EditDefaultsOnly, Category = "AI Perception", config)
		float MaxQueryImportance;

	UPROPERTY(EditDefaultsOnly, Category = "AI Perception", config)
		float SightLimitQueryImportance;

	ECollisionChannel DefaultSightCollisionChannel;

public:

	virtual void PostInitProperties() override;

	void RegisterEvent(const FAISightEventVR& Event);

	virtual void RegisterSource(AActor& SourceActors) override;
	virtual void UnregisterSource(AActor& SourceActor) override;
	virtual void CleanseInvalidSources() override;

	virtual void OnListenerForgetsActor(const FPerceptionListener& Listener, AActor& ActorToForget) override;
	virtual void OnListenerForgetsAll(const FPerceptionListener& Listener) override;

protected:
	virtual float Update() override;

	virtual bool ShouldAutomaticallySeeTarget(const FDigestedSightProperties& PropDigest, FAISightQueryVR* SightQuery, FPerceptionListener& Listener, AActor* TargetActor, float& OutStimulusStrength) const;

	void OnNewListenerImpl(const FPerceptionListener& NewListener);
	void OnListenerUpdateImpl(const FPerceptionListener& UpdatedListener);
	void OnListenerRemovedImpl(const FPerceptionListener& UpdatedListener);

	void GenerateQueriesForListener(const FPerceptionListener& Listener, const FDigestedSightProperties& PropertyDigest);

	enum FQueriesOperationPostProcess
	{
		DontSort,
		Sort
	};
	void RemoveAllQueriesByListener(const FPerceptionListener& Listener, FQueriesOperationPostProcess PostProcess);
	void RemoveAllQueriesToTarget(const FAISightTargetVR::FTargetId& TargetId, FQueriesOperationPostProcess PostProcess);

	/** returns information whether new LoS queries have been added */
	bool RegisterTarget(AActor& TargetActor, FQueriesOperationPostProcess PostProcess);

	FORCEINLINE void SortQueries() { SightQueryQueue.Sort(FAISightQueryVR::FSortPredicate()); }

	float CalcQueryImportance(const FPerceptionListener& Listener, const FVector& TargetLocation, const float SightRadiusSq) const;
};
