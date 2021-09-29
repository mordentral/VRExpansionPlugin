// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/VRAIPerceptionOverrides.h"
#include "EngineDefines.h"
#include "EngineGlobals.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "AIModule/Classes/AISystem.h"
#include "AIModule/Classes/Perception/AIPerceptionComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "AIModule/Classes/Perception/AISightTargetInterface.h"
#include "AIModule/Classes/Perception/AISenseConfig_Sight.h"
#include "AIModule/Classes/Perception/AIPerceptionSystem.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebugger/Public/GameplayDebuggerTypes.h"
#include "GameplayDebugger/Public/GameplayDebuggerCategory.h"
#endif
DEFINE_LOG_CATEGORY(LogAIPerceptionVR);

#define DO_SIGHT_VLOGGINGVR (0 && ENABLE_VISUAL_LOG)

#if DO_SIGHT_VLOGGINGVR
#define SIGHT_LOG_SEGMENTVR UE_VLOG_SEGMENT
#define SIGHT_LOG_LOCATIONVR UE_VLOG_LOCATION
#else
#define SIGHT_LOG_SEGMENTVR(...)
#define SIGHT_LOG_LOCATIONVR(...)
#endif // DO_SIGHT_VLOGGING

DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight"), STAT_AI_Sense_Sight, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Update Sort"), STAT_AI_Sense_Sight_UpdateSort, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Listener Update"), STAT_AI_Sense_Sight_ListenerUpdate, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Register Target"), STAT_AI_Sense_Sight_RegisterTarget, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Remove By Listener"), STAT_AI_Sense_Sight_RemoveByListener, STATGROUP_AI);
DECLARE_CYCLE_STAT(TEXT("Perception Sense: Sight, Remove To Target"), STAT_AI_Sense_Sight_RemoveToTarget, STATGROUP_AI);


static const int32 DefaultMaxTracesPerTick = 6;
static const int32 DefaultMinQueriesPerTimeSliceCheck = 40;

enum class EForEachResult : uint8
{
	Break,
	Continue,
};

template <typename T, class PREDICATE_CLASS>
EForEachResult ForEach(T& Array, const PREDICATE_CLASS& Predicate)
{
	for (typename T::ElementType& Element : Array)
	{
		if (Predicate(Element) == EForEachResult::Break)
		{
			return EForEachResult::Break;
		}
	}
	return EForEachResult::Continue;
}

enum EReverseForEachResult : uint8
{
	UnTouched,
	Modified,
};

template <typename T, class PREDICATE_CLASS>
EReverseForEachResult ReverseForEach(T& Array, const PREDICATE_CLASS& Predicate)
{
	EReverseForEachResult RetVal = EReverseForEachResult::UnTouched;
	for (int32 Index = Array.Num() - 1; Index >= 0; --Index)
	{
		if (Predicate(Array, Index) == EReverseForEachResult::Modified)
		{
			RetVal = EReverseForEachResult::Modified;
		}
	}
	return RetVal;
}

//----------------------------------------------------------------------//
// helpers
//----------------------------------------------------------------------//
FORCEINLINE_DEBUGGABLE bool CheckIsTargetInSightPie(const FPerceptionListener& Listener, const UAISense_Sight_VR::FDigestedSightProperties& DigestedProps, const FVector& TargetLocation, const float SightRadiusSq)
{
	if (FVector::DistSquared(Listener.CachedLocation, TargetLocation) <= SightRadiusSq)
	{
		const FVector DirectionToTarget = (TargetLocation - Listener.CachedLocation).GetUnsafeNormal();
		return FVector::DotProduct(DirectionToTarget, Listener.CachedDirection) > DigestedProps.PeripheralVisionAngleCos;
	}

	return false;
}

//----------------------------------------------------------------------//
// FAISightTargetVR
//----------------------------------------------------------------------//
const FAISightTargetVR::FTargetId FAISightTargetVR::InvalidTargetId = FAISystem::InvalidUnsignedID;

FAISightTargetVR::FAISightTargetVR(AActor* InTarget, FGenericTeamId InTeamId)
	: Target(InTarget), SightTargetInterface(NULL), TeamId(InTeamId)
{
	if (InTarget)
	{
		TargetId = InTarget->GetUniqueID();
	}
	else
	{
		TargetId = InvalidTargetId;
	}
}

//----------------------------------------------------------------------//
// FDigestedSightProperties
//----------------------------------------------------------------------//
UAISense_Sight_VR::FDigestedSightProperties::FDigestedSightProperties(const UAISenseConfig_Sight_VR& SenseConfig)
{
	SightRadiusSq = FMath::Square(SenseConfig.SightRadius);
	LoseSightRadiusSq = FMath::Square(SenseConfig.LoseSightRadius);
	PeripheralVisionAngleCos = FMath::Cos(FMath::Clamp(FMath::DegreesToRadians(SenseConfig.PeripheralVisionAngleDegrees), 0.f, PI));
	AffiliationFlags = SenseConfig.DetectionByAffiliation.GetAsFlags();
	// keep the special value of FAISystem::InvalidRange (-1.f) if it's set.
	AutoSuccessRangeSqFromLastSeenLocation = (SenseConfig.AutoSuccessRangeFromLastSeenLocation == FAISystem::InvalidRange) ? FAISystem::InvalidRange : FMath::Square(SenseConfig.AutoSuccessRangeFromLastSeenLocation);
}

UAISense_Sight_VR::FDigestedSightProperties::FDigestedSightProperties()
	: PeripheralVisionAngleCos(0.f), SightRadiusSq(-1.f), AutoSuccessRangeSqFromLastSeenLocation(FAISystem::InvalidRange), LoseSightRadiusSq(-1.f), AffiliationFlags(-1)
{}

//----------------------------------------------------------------------//
// UAISense_Sight_VR
//----------------------------------------------------------------------//
UAISense_Sight_VR::UAISense_Sight_VR(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MaxTracesPerTick(DefaultMaxTracesPerTick)
	, MinQueriesPerTimeSliceCheck(DefaultMinQueriesPerTimeSliceCheck)
	, MaxTimeSlicePerTick(0.005) // 5ms
	, HighImportanceQueryDistanceThreshold(300.f)
	, MaxQueryImportance(60.f)
	, SightLimitQueryImportance(10.f)
{
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		UAISenseConfig_Sight_VR* SightConfigCDO = GetMutableDefault<UAISenseConfig_Sight_VR>();
		SightConfigCDO->Implementation = UAISense_Sight_VR::StaticClass();

		OnNewListenerDelegate.BindUObject(this, &UAISense_Sight_VR::OnNewListenerImpl);
		OnListenerUpdateDelegate.BindUObject(this, &UAISense_Sight_VR::OnListenerUpdateImpl);
		OnListenerRemovedDelegate.BindUObject(this, &UAISense_Sight_VR::OnListenerRemovedImpl);
	}

	NotifyType = EAISenseNotifyType::OnPerceptionChange;

	bAutoRegisterAllPawnsAsSources = true;
	bNeedsForgettingNotification = true;

	DefaultSightCollisionChannel = GET_AI_CONFIG_VAR(DefaultSightCollisionChannel);
}

FORCEINLINE_DEBUGGABLE float UAISense_Sight_VR::CalcQueryImportance(const FPerceptionListener& Listener, const FVector& TargetLocation, const float SightRadiusSq) const
{
	const float DistanceSq = FVector::DistSquared(Listener.CachedLocation, TargetLocation);
	return DistanceSq <= HighImportanceDistanceSquare ? MaxQueryImportance
		: FMath::Clamp((SightLimitQueryImportance - MaxQueryImportance) / SightRadiusSq * DistanceSq + MaxQueryImportance, 0.f, MaxQueryImportance);
}

void UAISense_Sight_VR::PostInitProperties()
{
	Super::PostInitProperties();
	HighImportanceDistanceSquare = FMath::Square(HighImportanceQueryDistanceThreshold);
}

#if WITH_EDITOR
void UAISenseConfig_Sight_VR::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	static const FName NAME_AutoSuccessRangeFromLastSeenLocation = GET_MEMBER_NAME_CHECKED(UAISenseConfig_Sight_VR, AutoSuccessRangeFromLastSeenLocation);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		const FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == NAME_AutoSuccessRangeFromLastSeenLocation)
		{
			if (AutoSuccessRangeFromLastSeenLocation < 0)
			{
				AutoSuccessRangeFromLastSeenLocation = FAISystem::InvalidRange;
			}
		}
	}
}
#endif // WITH_EDITOR

bool UAISense_Sight_VR::ShouldAutomaticallySeeTarget(const FDigestedSightProperties& PropDigest, FAISightQueryVR* SightQuery, FPerceptionListener& Listener, AActor* TargetActor, float& OutStimulusStrength) const
{
	OutStimulusStrength = 1.0f;

	if ((PropDigest.AutoSuccessRangeSqFromLastSeenLocation != FAISystem::InvalidRange) && (SightQuery->LastSeenLocation != FAISystem::InvalidLocation))
	{
		// Changed this up to support my VR Characters
		const AVRBaseCharacter * VRChar = Cast<const AVRBaseCharacter>(TargetActor);
		const float DistanceToLastSeenLocationSq = FVector::DistSquared(VRChar != nullptr ? VRChar->GetVRLocation_Inline() : TargetActor->GetActorLocation(), SightQuery->LastSeenLocation);
		return (DistanceToLastSeenLocationSq <= PropDigest.AutoSuccessRangeSqFromLastSeenLocation);
	}

	return false;
}

float UAISense_Sight_VR::Update()
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight);

	const UWorld* World = GEngine->GetWorldFromContextObject(GetPerceptionSystem()->GetOuter(), EGetWorldErrorMode::LogAndReturnNull);

	if (World == NULL)
	{
		return SuspendNextUpdate;
	}

	// sort Sight Queries
	{
		auto RecalcScore = [](FAISightQueryVR& SightQuery)->EForEachResult
		{
			SightQuery.RecalcScore();
			return EForEachResult::Continue;
		};

		SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_UpdateSort);
		// Sort out of range queries
		if (bSightQueriesOutOfRangeDirty)
		{
			ForEach(SightQueriesOutOfRange, RecalcScore);
			SightQueriesOutOfRange.Sort(FAISightQueryVR::FSortPredicate());
			NextOutOfRangeIndex = 0;
			bSightQueriesOutOfRangeDirty = false;
		}

		// Sort in range queries
		ForEach(SightQueriesInRange, RecalcScore);
		SightQueriesInRange.Sort(FAISightQueryVR::FSortPredicate());
	}

	int32 TracesCount = 0;
	int32 NumQueriesProcessed = 0;
	double TimeSliceEnd = FPlatformTime::Seconds() + MaxTimeSlicePerTick;
	bool bHitTimeSliceLimit = false;
	//#define AISENSE_SIGHT_TIMESLICING_DEBUG
#ifdef AISENSE_SIGHT_TIMESLICING_DEBUG
	double TimeSpent = 0.0;
	double LastTime = FPlatformTime::Seconds();
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG
	static const int32 InitialInvalidItemsSize = 16;
	enum class EOperationType : uint8
	{
		Remove,
		SwapList
	};
	struct FQueryOperation
	{
		FQueryOperation(bool bInInRange, EOperationType InOpType, int32 InIndex) : bInRange(bInInRange), OpType(InOpType), Index(InIndex) {}
		bool bInRange;
		EOperationType OpType;
		int32 Index;
	};
	TArray<FQueryOperation> QueryOperations;
	TArray<FAISightTargetVR::FTargetId> InvalidTargets;
	QueryOperations.Reserve(InitialInvalidItemsSize);
	InvalidTargets.Reserve(InitialInvalidItemsSize);

	AIPerception::FListenerMap& ListenersMap = *GetListeners();

	int32 InRangeItr = 0;
	int32 OutOfRangeItr = 0;
	for (int32 QueryIndex = 0; QueryIndex < SightQueriesInRange.Num() + SightQueriesOutOfRange.Num(); ++QueryIndex)
	{
		// Calculate next in range query
		int32 InRangeIndex = SightQueriesInRange.IsValidIndex(InRangeItr) ? InRangeItr : INDEX_NONE;
		FAISightQueryVR* InRangeQuery = InRangeIndex != INDEX_NONE ? &SightQueriesInRange[InRangeIndex] : nullptr;

		// Calculate next out of range query
		int32 OutOfRangeIndex = SightQueriesOutOfRange.IsValidIndex(OutOfRangeItr) ? (NextOutOfRangeIndex + OutOfRangeItr) % SightQueriesOutOfRange.Num() : INDEX_NONE;
		FAISightQueryVR* OutOfRangeQuery = OutOfRangeIndex != INDEX_NONE ? &SightQueriesOutOfRange[OutOfRangeIndex] : nullptr;
		if (OutOfRangeQuery)
		{
			OutOfRangeQuery->RecalcScore();
		}

		// Compare to real find next query
		const bool bIsInRangeQuery = (InRangeQuery && OutOfRangeQuery) ? FAISightQueryVR::FSortPredicate()(*InRangeQuery, *OutOfRangeQuery) : !OutOfRangeQuery;
		FAISightQueryVR* SightQuery = bIsInRangeQuery ? InRangeQuery : OutOfRangeQuery;

		// Time slice limit check - spread out checks to every N queries so we don't spend more time checking timer than doing work
		NumQueriesProcessed++;
#ifdef AISENSE_SIGHT_TIMESLICING_DEBUG
		TimeSpent += (FPlatformTime::Seconds() - LastTime);
		LastTime = FPlatformTime::Seconds();
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG
		if (bHitTimeSliceLimit == false && (NumQueriesProcessed % MinQueriesPerTimeSliceCheck) == 0 && FPlatformTime::Seconds() > TimeSliceEnd)
		{
			bHitTimeSliceLimit = true;
			// do not break here since that would bypass queue aging
		}

		if (TracesCount < MaxTracesPerTick && bHitTimeSliceLimit == false)
		{
			bIsInRangeQuery ? ++InRangeItr : ++OutOfRangeItr;

			FPerceptionListener& Listener = ListenersMap[SightQuery->ObserverId];

			FAISightTargetVR& Target = ObservedTargets[SightQuery->TargetId];
			AActor* TargetActor = Target.Target.Get();
			UAIPerceptionComponent* ListenerPtr = Listener.Listener.Get();
			ensure(ListenerPtr);

			// @todo figure out what should we do if not valid
			if (TargetActor && ListenerPtr)
			{
				//AActor* nTargetActor = Target.Target.Get();
				// Changed this up to support my VR Characters
				const AVRBaseCharacter * VRChar = Cast<const AVRBaseCharacter>(TargetActor);
				const FVector TargetLocation = VRChar != nullptr ? VRChar->GetVRLocation_Inline() : TargetActor->GetActorLocation();

				const FDigestedSightProperties& PropDigest = DigestedProperties[SightQuery->ObserverId];
				const float SightRadiusSq = SightQuery->bLastResult ? PropDigest.LoseSightRadiusSq : PropDigest.SightRadiusSq;

				float StimulusStrength = 1.f;

				// @Note that automagical "seeing" does not care about sight range nor vision cone
				const bool bShouldAutomatically = ShouldAutomaticallySeeTarget(PropDigest, SightQuery, Listener, TargetActor, StimulusStrength);
				if (bShouldAutomatically)
				{
					// Pretend like we've seen this target where we last saw them
					Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, StimulusStrength, SightQuery->LastSeenLocation, Listener.CachedLocation));
					SightQuery->bLastResult = true;
				}
				else if (CheckIsTargetInSightPie(Listener, PropDigest, TargetLocation, SightRadiusSq))
				{
					SIGHT_LOG_SEGMENTVR(ListenerPtr->GetOwner(), Listener.CachedLocation, TargetLocation, FColor::Green, TEXT("%s"), *(Target.TargetId.ToString()));

					FVector OutSeenLocation(0.f);
					// do line checks
					if (Target.SightTargetInterface != NULL)
					{
						int32 NumberOfLoSChecksPerformed = 0;
						// defaulting to 1 to have "full strength" by default instead of "no strength"
						const bool bWasVisible = SightQuery->bLastResult;
						if (Target.SightTargetInterface->CanBeSeenFrom(Listener.CachedLocation, OutSeenLocation, NumberOfLoSChecksPerformed, StimulusStrength, ListenerPtr->GetBodyActor(), &bWasVisible, &SightQuery->UserData) == true)
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, StimulusStrength, OutSeenLocation, Listener.CachedLocation));
							SightQuery->bLastResult = true;
							SightQuery->LastSeenLocation = OutSeenLocation;
						}
						// communicate failure only if we've seen give actor before
						else if (SightQuery->bLastResult == true)
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, TargetLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
							SightQuery->bLastResult = false;
							SightQuery->LastSeenLocation = FAISystem::InvalidLocation;
						}

						if (SightQuery->bLastResult == false)
						{
							SIGHT_LOG_LOCATIONVR(ListenerPtr->GetOwner(), TargetLocation, 25.f, FColor::Red, TEXT(""));
						}

						TracesCount += NumberOfLoSChecksPerformed;
					}
					else
					{
						// we need to do tests ourselves
						FHitResult HitResult;
						const bool bHit = World->LineTraceSingleByChannel(HitResult, Listener.CachedLocation, TargetLocation
							, DefaultSightCollisionChannel
							, FCollisionQueryParams(SCENE_QUERY_STAT(AILineOfSight), true, ListenerPtr->GetBodyActor()));

						++TracesCount;

						auto HitResultActorIsOwnedByTargetActor = [&HitResult, TargetActor]()
						{
							AActor* HitResultActor = HitResult.Actor.Get();
							return (HitResultActor ? HitResultActor->IsOwnedBy(TargetActor) : false);
						};

						if (bHit == false || HitResultActorIsOwnedByTargetActor())
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 1.f, TargetLocation, Listener.CachedLocation));
							SightQuery->bLastResult = true;
							SightQuery->LastSeenLocation = TargetLocation;
						}
						// communicate failure only if we've seen give actor before
						else if (SightQuery->bLastResult == true)
						{
							Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, TargetLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
							SightQuery->bLastResult = false;
							SightQuery->LastSeenLocation = FAISystem::InvalidLocation;
						}

						if (SightQuery->bLastResult == false)
						{
							SIGHT_LOG_LOCATIONVR(ListenerPtr->GetOwner(), TargetLocation, 25.f, FColor::Red, TEXT(""));
						}
					}
				}
				// communicate failure only if we've seen give actor before
				else if (SightQuery->bLastResult)
				{
					SIGHT_LOG_SEGMENTVR(ListenerPtr->GetOwner(), Listener.CachedLocation, TargetLocation, FColor::Red, TEXT("%s"), *(Target.TargetId.ToString()));
					Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, TargetLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
					SightQuery->bLastResult = false;
				}

				SightQuery->Importance = CalcQueryImportance(Listener, TargetLocation, SightRadiusSq);
				const bool bShouldBeInRange = SightQuery->Importance > 0.0f;
				if (bIsInRangeQuery != bShouldBeInRange)
				{
					QueryOperations.Add(FQueryOperation(bIsInRangeQuery, EOperationType::SwapList, bIsInRangeQuery ? InRangeIndex : OutOfRangeIndex));
				}

				// restart query
				SightQuery->OnProcessed();
			}
			else
			{
				// put this index to "to be removed" array
				QueryOperations.Add(FQueryOperation(bIsInRangeQuery, EOperationType::Remove, bIsInRangeQuery ? InRangeIndex : OutOfRangeIndex));
				if (TargetActor == nullptr)
				{
					InvalidTargets.AddUnique(SightQuery->TargetId);
				}
			}
		}
		else
		{
			break;
		}
	}

	NextOutOfRangeIndex = SightQueriesOutOfRange.Num() > 0 ? (NextOutOfRangeIndex + OutOfRangeItr) % SightQueriesOutOfRange.Num() : 0;

#ifdef AISENSE_SIGHT_TIMESLICING_DEBUG
	UE_LOG(LogAIPerceptionVR, VeryVerbose, TEXT("UAISense_Sight_VR::Update processed %d sources in %f seconds [time slice limited? %d]"), NumQueriesProcessed, TimeSpent, bHitTimeSliceLimit ? 1 : 0);
#else
	UE_LOG(LogAIPerceptionVR, VeryVerbose, TEXT("UAISense_Sight_VR::Update processed %d sources [time slice limited? %d]"), NumQueriesProcessed, bHitTimeSliceLimit ? 1 : 0);
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG

	if (QueryOperations.Num() > 0)
	{
		// Sort by InRange and by descending Index 
		QueryOperations.Sort([](const FQueryOperation& LHS, const FQueryOperation& RHS)->bool
			{
				if (LHS.bInRange != RHS.bInRange)
					return LHS.bInRange;
				return LHS.Index > RHS.Index;
			});
		// Do all the removes first and save the out of range swaps because we will insert them at the right location to prevent sorting
		TArray<FAISightQueryVR> SightQueriesOutOfRangeToInsert;
		for (FQueryOperation& Operation : QueryOperations)
		{
			if (Operation.OpType == EOperationType::SwapList)
			{
				if (Operation.bInRange)
				{
					SightQueriesOutOfRangeToInsert.Push(SightQueriesInRange[Operation.Index]);
				}
				else
				{
					SightQueriesInRange.Add(SightQueriesOutOfRange[Operation.Index]);
				}
			}

			if (Operation.bInRange)
			{
				// In range queries are always sorted at the beginning of the update
				SightQueriesInRange.RemoveAtSwap(Operation.Index, 1, /*bAllowShrinking*/false);
			}
			else
			{
				// Preserve the list ordered
				SightQueriesOutOfRange.RemoveAt(Operation.Index, 1, /*bAllowShrinking*/false);
				if (Operation.Index < NextOutOfRangeIndex)
				{
					NextOutOfRangeIndex--;
				}
			}
		}
		// Reinsert the saved out of range swaps
		if (SightQueriesOutOfRangeToInsert.Num() > 0)
		{
			SightQueriesOutOfRange.Insert(SightQueriesOutOfRangeToInsert.GetData(), SightQueriesOutOfRangeToInsert.Num(), NextOutOfRangeIndex);
			NextOutOfRangeIndex += SightQueriesOutOfRangeToInsert.Num();
		}

		if (InvalidTargets.Num() > 0)
		{
			// this should not be happening since UAIPerceptionSystem::OnPerceptionStimuliSourceEndPlay introduction
			UE_VLOG(GetPerceptionSystem(), LogAIPerceptionVR, Error, TEXT("Invalid sight targets found during UAISense_Sight_VR::Update call"));

			for (const auto& TargetId : InvalidTargets)
			{
				// remove affected queries
				RemoveAllQueriesToTarget(TargetId);
				// remove target itself
				ObservedTargets.Remove(TargetId);
			}

			// remove holes
			ObservedTargets.Compact();
		}
	}

	//return SightQueryQueue.Num() > 0 ? 1.f/6 : FLT_MAX;
	return 0.f;
}

void UAISense_Sight_VR::RegisterEvent(const FAISightEventVR& Event)
{

}

void UAISense_Sight_VR::RegisterSource(AActor& SourceActor)
{
	RegisterTarget(SourceActor);
}

void UAISense_Sight_VR::UnregisterSource(AActor& SourceActor)
{
	const FAISightTargetVR::FTargetId AsTargetId = SourceActor.GetUniqueID();
	FAISightTargetVR AsTarget;

	if (ObservedTargets.RemoveAndCopyValue(AsTargetId, AsTarget)
		&& (SightQueriesInRange.Num() + SightQueriesOutOfRange.Num()) > 0)
	{
		AActor* TargetActor = AsTarget.Target.Get();

		if (TargetActor)
		{
			// notify all interested observers that this source is no longer
			// visible		
			AIPerception::FListenerMap& ListenersMap = *GetListeners();
			auto RemoveQuery = [this, &ListenersMap, &AsTargetId, &TargetActor](TArray<FAISightQueryVR>& SightQueries, const int32 QueryIndex)->EReverseForEachResult
			{
				FAISightQueryVR* SightQuery = &SightQueries[QueryIndex];
				if (SightQuery->TargetId == AsTargetId)
				{
					if (SightQuery->bLastResult == true)
					{
						FPerceptionListener& Listener = ListenersMap[SightQuery->ObserverId];
						ensure(Listener.Listener.IsValid());

						Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, SightQuery->LastSeenLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
					}

					SightQueries.RemoveAtSwap(QueryIndex, 1, /*bAllowShrinking=*/false);
					return EReverseForEachResult::Modified;
				}

				return EReverseForEachResult::UnTouched;
			};

			ReverseForEach(SightQueriesInRange, RemoveQuery);
			if (ReverseForEach(SightQueriesOutOfRange, RemoveQuery) == EReverseForEachResult::Modified)
			{
				bSightQueriesOutOfRangeDirty = true;
			}
		}
	}
}

bool UAISense_Sight_VR::RegisterTarget(AActor& TargetActor, const TFunction<void(FAISightQueryVR&)>& OnAddedFunc /*= nullptr*/)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RegisterTarget);

	FAISightTargetVR* SightTarget = ObservedTargets.Find(TargetActor.GetUniqueID());

	if (SightTarget != nullptr && SightTarget->GetTargetActor() != &TargetActor)
	{
		// this means given unique ID has already been recycled. 
		FAISightTargetVR NewSightTarget(&TargetActor);

		SightTarget = &(ObservedTargets.Add(NewSightTarget.TargetId, NewSightTarget));
		SightTarget->SightTargetInterface = Cast<IAISightTargetInterface>(&TargetActor);
	}
	else if (SightTarget == nullptr)
	{
		FAISightTargetVR NewSightTarget(&TargetActor);

		SightTarget = &(ObservedTargets.Add(NewSightTarget.TargetId, NewSightTarget));
		SightTarget->SightTargetInterface = Cast<IAISightTargetInterface>(&TargetActor);
	}

	// set/update data
	SightTarget->TeamId = FGenericTeamId::GetTeamIdentifier(&TargetActor);

	// generate all pairs and add them to current Sight Queries
	bool bNewQueriesAdded = false;
	AIPerception::FListenerMap& ListenersMap = *GetListeners();

	// Changed this up to support my VR Characters
	const AVRBaseCharacter * VRChar = Cast<const AVRBaseCharacter>(&TargetActor);
	const FVector TargetLocation = VRChar != nullptr ? VRChar->GetVRLocation_Inline() : TargetActor.GetActorLocation();

	for (AIPerception::FListenerMap::TConstIterator ItListener(ListenersMap); ItListener; ++ItListener)
	{
		const FPerceptionListener& Listener = ItListener->Value;
		const IGenericTeamAgentInterface* ListenersTeamAgent = Listener.GetTeamAgent();

		if (Listener.HasSense(GetSenseID()) && Listener.GetBodyActor() != &TargetActor)
		{
			const FDigestedSightProperties& PropDigest = DigestedProperties[Listener.GetListenerID()];
			if (FAISenseAffiliationFilter::ShouldSenseTeam(ListenersTeamAgent, TargetActor, PropDigest.AffiliationFlags))
			{
				// create a sight query		
				const float Importance = CalcQueryImportance(ItListener->Value, TargetLocation, PropDigest.SightRadiusSq);
				const bool bInRange = Importance > 0.0f;
				if (!bInRange)
				{
					bSightQueriesOutOfRangeDirty = true;
				}
				FAISightQueryVR& AddedQuery = bInRange ? SightQueriesInRange.AddDefaulted_GetRef() : SightQueriesOutOfRange.AddDefaulted_GetRef();
				AddedQuery.ObserverId = ItListener->Key;
				AddedQuery.TargetId = SightTarget->TargetId;

				AddedQuery.Importance = Importance;

				if (OnAddedFunc)
				{
					OnAddedFunc(AddedQuery);
				}
				bNewQueriesAdded = true;
			}
		}
	}

	// sort Sight Queries
	if (bNewQueriesAdded)
	{
		RequestImmediateUpdate();
	}

	return bNewQueriesAdded;
}

void UAISense_Sight_VR::OnNewListenerImpl(const FPerceptionListener& NewListener)
{
	UAIPerceptionComponent* NewListenerPtr = NewListener.Listener.Get();
	check(NewListenerPtr);
	const UAISenseConfig_Sight_VR* SenseConfig = Cast<const UAISenseConfig_Sight_VR>(NewListenerPtr->GetSenseConfig(GetSenseID()));

	check(SenseConfig);
	const FDigestedSightProperties PropertyDigest(*SenseConfig);
	DigestedProperties.Add(NewListener.GetListenerID(), PropertyDigest);

	GenerateQueriesForListener(NewListener, PropertyDigest);
}

void UAISense_Sight_VR::GenerateQueriesForListener(const FPerceptionListener& Listener, const FDigestedSightProperties& PropertyDigest, const TFunction<void(FAISightQueryVR&)>& OnAddedFunc/*= nullptr */)
{
	bool bNewQueriesAdded = false;
	const IGenericTeamAgentInterface* ListenersTeamAgent = Listener.GetTeamAgent();
	const AActor* Avatar = Listener.GetBodyActor();

	// create sight queries with all legal targets
	for (FTargetsContainer::TConstIterator ItTarget(ObservedTargets); ItTarget; ++ItTarget)
	{
		const AActor* TargetActor = ItTarget->Value.GetTargetActor();
		if (TargetActor == NULL || TargetActor == Avatar)
		{
			continue;
		}

		if (FAISenseAffiliationFilter::ShouldSenseTeam(ListenersTeamAgent, *TargetActor, PropertyDigest.AffiliationFlags))
		{
			// create a sight query		
			const float Importance = CalcQueryImportance(Listener, ItTarget->Value.GetLocationSimple(), PropertyDigest.SightRadiusSq);
			const bool bInRange = Importance > 0.0f;
			if (!bInRange)
			{
				bSightQueriesOutOfRangeDirty = true;
			}
			FAISightQueryVR& AddedQuery = bInRange ? SightQueriesInRange.AddDefaulted_GetRef() : SightQueriesOutOfRange.AddDefaulted_GetRef();
			AddedQuery.ObserverId = Listener.GetListenerID();
			AddedQuery.TargetId = ItTarget->Key;

			AddedQuery.Importance = Importance;


			if (OnAddedFunc)
			{
				OnAddedFunc(AddedQuery);
			}
			bNewQueriesAdded = true;
		}
	}

	// sort Sight Queries
	if (bNewQueriesAdded)
	{
		RequestImmediateUpdate();
	}
}

void UAISense_Sight_VR::OnListenerUpdateImpl(const FPerceptionListener& UpdatedListener)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_ListenerUpdate);

	// first, naive implementation:
	// 1. remove all queries by this listener
	// 2. proceed as if it was a new listener

	// see if this listener is a Target as well
	const FAISightTargetVR::FTargetId AsTargetId = UpdatedListener.GetBodyActorUniqueID();
	FAISightTargetVR* AsTarget = ObservedTargets.Find(AsTargetId);
	if (AsTarget != NULL)
	{
		if (AsTarget->Target.IsValid())
		{
			// if still a valid target then backup list of observers for which the listener was visible to restore in the newly created queries
			TSet<FPerceptionListenerID> LastVisibleObservers;
			RemoveAllQueriesToTarget(AsTargetId, [&LastVisibleObservers](const FAISightQueryVR& Query)
			{
				if (Query.bLastResult)
				{
					LastVisibleObservers.Add(Query.ObserverId);
				}
			});

			RegisterTarget(*(AsTarget->Target.Get()), [&LastVisibleObservers](FAISightQueryVR& Query)
			{
				Query.bLastResult = LastVisibleObservers.Contains(Query.ObserverId);
			});
		}
		else
		{
			RemoveAllQueriesToTarget(AsTargetId);
		}
	}

	const FPerceptionListenerID ListenerID = UpdatedListener.GetListenerID();

	if (UpdatedListener.HasSense(GetSenseID()))
	{
		// if still a valid sense then backup list of targets that were visible by the listener to restore in the newly created queries
		TSet<FAISightTargetVR::FTargetId> LastVisibleTargets;
		RemoveAllQueriesByListener(UpdatedListener, [&LastVisibleTargets](const FAISightQueryVR& Query)
		{
			if (Query.bLastResult)
			{
				LastVisibleTargets.Add(Query.TargetId);
			}
		});

		const UAISenseConfig_Sight_VR* SenseConfig = Cast<const UAISenseConfig_Sight_VR>(UpdatedListener.Listener->GetSenseConfig(GetSenseID()));
		check(SenseConfig);
		FDigestedSightProperties& PropertiesDigest = DigestedProperties.FindOrAdd(ListenerID);
		PropertiesDigest = FDigestedSightProperties(*SenseConfig);

		GenerateQueriesForListener(UpdatedListener, PropertiesDigest, [&LastVisibleTargets](FAISightQueryVR & Query)
		{
			Query.bLastResult = LastVisibleTargets.Contains(Query.TargetId);
		});
	}
	else
	{
		// remove all queries
		RemoveAllQueriesByListener(UpdatedListener);
		DigestedProperties.Remove(ListenerID);
	}
}

void UAISense_Sight_VR::OnListenerConfigUpdated(const FPerceptionListener& UpdatedListener)
{

	bool bSkipListenerUpdate = false;
	const FPerceptionListenerID ListenerID = UpdatedListener.GetListenerID();


	FDigestedSightProperties* PropertiesDigest = DigestedProperties.Find(ListenerID);
	if (PropertiesDigest)
	{
		// The only parameter we need to rebuild all the queries for this listener is if the affiliation mask changed, otherwise there is nothing to update.
		const UAISenseConfig_Sight_VR* SenseConfig = CastChecked<const UAISenseConfig_Sight_VR>(UpdatedListener.Listener->GetSenseConfig(GetSenseID()));
		FDigestedSightProperties NewPropertiesDigest(*SenseConfig);
		bSkipListenerUpdate = NewPropertiesDigest.AffiliationFlags == PropertiesDigest->AffiliationFlags;
		*PropertiesDigest = NewPropertiesDigest;
	}

	if (!bSkipListenerUpdate)
	{
		Super::OnListenerConfigUpdated(UpdatedListener);
	}
}

void UAISense_Sight_VR::OnListenerRemovedImpl(const FPerceptionListener& RemovedListener)
{

	RemoveAllQueriesByListener(RemovedListener);

	DigestedProperties.FindAndRemoveChecked(RemovedListener.GetListenerID());

	// note: there use to be code to remove all queries _to_ listener here as well
	// but that was wrong - the fact that a listener gets unregistered doesn't have to
	// mean it's being removed from the game altogether.
}


void UAISense_Sight_VR::RemoveAllQueriesByListener(const FPerceptionListener& Listener, const TFunction<void(const FAISightQueryVR&)>& OnRemoveFunc/*= nullptr */)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RemoveByListener);

	if ((SightQueriesInRange.Num() + SightQueriesOutOfRange.Num()) == 0)
	{
		return;
	}

	const uint32 ListenerId = Listener.GetListenerID();



	auto RemoveQuery = [&ListenerId, &OnRemoveFunc](TArray<FAISightQueryVR>& SightQueries, const int32 QueryIndex)->EReverseForEachResult
	{
		const FAISightQueryVR& SightQuery = SightQueries[QueryIndex];

		if (SightQuery.ObserverId == ListenerId)
		{
			if (OnRemoveFunc)
			{
				OnRemoveFunc(SightQuery);
			}
			SightQueries.RemoveAtSwap(QueryIndex, 1, /*bAllowShrinking=*/false);

			return EReverseForEachResult::Modified;
		}



		return EReverseForEachResult::UnTouched;
	};
	ReverseForEach(SightQueriesInRange, RemoveQuery);
	if (ReverseForEach(SightQueriesOutOfRange, RemoveQuery) == EReverseForEachResult::Modified)
	{

		bSightQueriesOutOfRangeDirty = true;
	}
}

void UAISense_Sight_VR::RemoveAllQueriesToTarget(const FAISightTargetVR::FTargetId& TargetId, const TFunction<void(const FAISightQueryVR&)>& OnRemoveFunc/*= nullptr */)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RemoveToTarget);

	auto RemoveQuery = [&TargetId, &OnRemoveFunc](TArray<FAISightQueryVR>& SightQueries, const int32 QueryIndex)->EReverseForEachResult
	{
		const FAISightQueryVR& SightQuery = SightQueries[QueryIndex];

		if (SightQuery.TargetId == TargetId)
		{
			if (OnRemoveFunc)
			{
				OnRemoveFunc(SightQuery);
			}
			SightQueries.RemoveAtSwap(QueryIndex, 1, /*bAllowShrinking=*/false);

			return EReverseForEachResult::Modified;
		}

		return EReverseForEachResult::UnTouched;
	};
	ReverseForEach(SightQueriesInRange, RemoveQuery);
	if (ReverseForEach(SightQueriesOutOfRange, RemoveQuery) == EReverseForEachResult::Modified)
	{

		bSightQueriesOutOfRangeDirty = true;
	}
}


void UAISense_Sight_VR::OnListenerForgetsActor(const FPerceptionListener& Listener, AActor& ActorToForget)
{
	const uint32 ListenerId = Listener.GetListenerID();
	const uint32 TargetId = ActorToForget.GetUniqueID();

	auto ForgetPreviousResult = [&ListenerId, &TargetId](FAISightQueryVR& SightQuery)->EForEachResult
	{
		if (SightQuery.ObserverId == ListenerId && SightQuery.TargetId == TargetId)
		{
			// assuming one query per observer-target pair
			SightQuery.ForgetPreviousResult();
			return EForEachResult::Break;
		}
		return EForEachResult::Continue;
	};

	if (ForEach(SightQueriesInRange, ForgetPreviousResult) == EForEachResult::Continue)
	{
		ForEach(SightQueriesOutOfRange, ForgetPreviousResult);
	}
}

void UAISense_Sight_VR::OnListenerForgetsAll(const FPerceptionListener& Listener)
{
	const uint32 ListenerId = Listener.GetListenerID();

	auto ForgetPreviousResult = [&ListenerId](FAISightQueryVR& SightQuery)->EForEachResult
	{
		if (SightQuery.ObserverId == ListenerId)
		{
			SightQuery.ForgetPreviousResult();
		}

		return EForEachResult::Continue;
	};

	ForEach(SightQueriesInRange, ForgetPreviousResult);
	ForEach(SightQueriesOutOfRange, ForgetPreviousResult);
}


//----------------------------------------------------------------------//
// 
//----------------------------------------------------------------------//
UAISenseConfig_Sight_VR::UAISenseConfig_Sight_VR(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	DebugColor = FColor::Green;
	AutoSuccessRangeFromLastSeenLocation = -1.0;
	SightRadius = 3000.f;
	LoseSightRadius = 3500.f;
	PeripheralVisionAngleDegrees = 90;
	DetectionByAffiliation.bDetectEnemies = true;
	Implementation = UAISense_Sight_VR::StaticClass();
}

TSubclassOf<UAISense> UAISenseConfig_Sight_VR::GetSenseImplementation() const
{
	return *Implementation;
}

#if WITH_GAMEPLAY_DEBUGGER
static FString DescribeColorHelper(const FColor& Color)
{
	int32 MaxColors = GColorList.GetColorsNum();
	for (int32 Idx = 0; Idx < MaxColors; Idx++)
	{
		if (Color == GColorList.GetFColorByIndex(Idx))
		{
			return GColorList.GetColorNameByIndex(Idx);
		}
	}

	return FString(TEXT("color"));
}

void UAISenseConfig_Sight_VR::DescribeSelfToGameplayDebugger(const UAIPerceptionComponent* PerceptionComponent, FGameplayDebuggerCategory* DebuggerCategory) const
{
	if (PerceptionComponent == nullptr || DebuggerCategory == nullptr)
	{
		return;
	}

	FColor SightRangeColor = FColor::Green;
	FColor LoseSightRangeColor = FColorList::NeonPink;

	// don't call Super implementation on purpose, replace color description line
	DebuggerCategory->AddTextLine(
		FString::Printf(TEXT("%s: {%s}%s {white}rangeIN:{%s}%s {white} rangeOUT:{%s}%s"), *GetSenseName(),
			*GetDebugColor().ToString(), *DescribeColorHelper(GetDebugColor()),
			*SightRangeColor.ToString(), *DescribeColorHelper(SightRangeColor),
			*LoseSightRangeColor.ToString(), *DescribeColorHelper(LoseSightRangeColor))
	);

	const AActor* BodyActor = PerceptionComponent->GetBodyActor();
	if (BodyActor != nullptr)
	{
		FVector BodyLocation, BodyFacing;
		PerceptionComponent->GetLocationAndDirection(BodyLocation, BodyFacing);

		DebuggerCategory->AddShape(FGameplayDebuggerShape::MakeCylinder(BodyLocation, LoseSightRadius, 25.0f, LoseSightRangeColor));
		DebuggerCategory->AddShape(FGameplayDebuggerShape::MakeCylinder(BodyLocation, SightRadius, 25.0f, SightRangeColor));

		const float SightPieLength = FMath::Max(LoseSightRadius, SightRadius);
		DebuggerCategory->AddShape(FGameplayDebuggerShape::MakeSegment(BodyLocation, BodyLocation + (BodyFacing * SightPieLength), SightRangeColor));
		DebuggerCategory->AddShape(FGameplayDebuggerShape::MakeSegment(BodyLocation, BodyLocation + (BodyFacing.RotateAngleAxis(PeripheralVisionAngleDegrees, FVector::UpVector) * SightPieLength), SightRangeColor));
		DebuggerCategory->AddShape(FGameplayDebuggerShape::MakeSegment(BodyLocation, BodyLocation + (BodyFacing.RotateAngleAxis(-PeripheralVisionAngleDegrees, FVector::UpVector) * SightPieLength), SightRangeColor));
	}
}
#endif // WITH_GAMEPLAY_DEBUGGER