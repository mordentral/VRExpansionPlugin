// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/VRAIPerceptionOverrides.h"
#include "EngineDefines.h"
#include "EngineGlobals.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "AISystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "VisualLogger/VisualLogger.h"
#include "Perception/AISightTargetInterface.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AIPerceptionSystem.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerTypes.h"
#include "GameplayDebuggerCategory.h"
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
	TArray<int32> InvalidQueries;
	TArray<FAISightTargetVR::FTargetId> InvalidTargets;
	InvalidQueries.Reserve(InitialInvalidItemsSize);
	InvalidTargets.Reserve(InitialInvalidItemsSize);

	AIPerception::FListenerMap& ListenersMap = *GetListeners();

	FAISightQueryVR* SightQuery = SightQueryQueue.GetData();
	for (int32 QueryIndex = 0; QueryIndex < SightQueryQueue.Num(); ++QueryIndex, ++SightQuery)
	{
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
			FPerceptionListener& Listener = ListenersMap[SightQuery->ObserverId];
			ensure(Listener.Listener.IsValid());
			FAISightTargetVR& Target = ObservedTargets[SightQuery->TargetId];

			const bool bTargetValid = Target.Target.IsValid();
			const bool bListenerValid = Listener.Listener.IsValid();

			// @todo figure out what should we do if not valid
			if (bTargetValid && bListenerValid)
			{
				AActor* TargetActor = Target.Target.Get();
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
					SIGHT_LOG_SEGMENTVR(Listener.Listener.Get()->GetOwner(), Listener.CachedLocation, TargetLocation, FColor::Green, TEXT("%s"), *(Target.TargetId.ToString()));

					FVector OutSeenLocation(0.f);
					// do line checks
					if (Target.SightTargetInterface != NULL)
					{
						int32 NumberOfLoSChecksPerformed = 0;
						// defaulting to 1 to have "full strength" by default instead of "no strength"
						if (Target.SightTargetInterface->CanBeSeenFrom(Listener.CachedLocation, OutSeenLocation, NumberOfLoSChecksPerformed, StimulusStrength, Listener.Listener->GetBodyActor()) == true)
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
							SIGHT_LOG_LOCATIONVR(Listener.Listener.Get()->GetOwner(), TargetLocation, 25.f, FColor::Red, TEXT(""));
						}

						TracesCount += NumberOfLoSChecksPerformed;
					}
					else
					{
						// we need to do tests ourselves
						FHitResult HitResult;
						const bool bHit = World->LineTraceSingleByChannel(HitResult, Listener.CachedLocation, TargetLocation
							, DefaultSightCollisionChannel
							, FCollisionQueryParams(SCENE_QUERY_STAT(AILineOfSight), true, Listener.Listener->GetBodyActor()));

						++TracesCount;

						if (bHit == false || (HitResult.Actor.IsValid() && HitResult.Actor->IsOwnedBy(TargetActor)))
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
							SIGHT_LOG_LOCATIONVR(Listener.Listener.Get()->GetOwner(), TargetLocation, 25.f, FColor::Red, TEXT(""));
						}
					}
				}
				// communicate failure only if we've seen give actor before
				else if (SightQuery->bLastResult)
				{
					SIGHT_LOG_SEGMENTVR(Listener.Listener.Get()->GetOwner(), Listener.CachedLocation, TargetLocation, FColor::Red, TEXT("%s"), *(Target.TargetId.ToString()));
					Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, TargetLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
					SightQuery->bLastResult = false;
				}

				SightQuery->Importance = CalcQueryImportance(Listener, TargetLocation, SightRadiusSq);

				// restart query
				SightQuery->Age = 0.f;
			}
			else
			{
				// put this index to "to be removed" array
				InvalidQueries.Add(QueryIndex);
				if (bTargetValid == false)
				{
					InvalidTargets.AddUnique(SightQuery->TargetId);
				}
			}
		}
		else
		{
			// age unprocessed queries so that they can advance in the queue during next sort
			SightQuery->Age += 1.f;
		}

		SightQuery->RecalcScore();
	}
#ifdef AISENSE_SIGHT_TIMESLICING_DEBUG
	UE_LOG(LogAIPerceptionVR, VeryVerbose, TEXT("UAISense_Sight_VR::Update processed %d sources in %f seconds [time slice limited? %d]"), NumQueriesProcessed, TimeSpent, bHitTimeSliceLimit ? 1 : 0);
#else
	UE_LOG(LogAIPerceptionVR, VeryVerbose, TEXT("UAISense_Sight_VR::Update processed %d sources [time slice limited? %d]"), NumQueriesProcessed, bHitTimeSliceLimit ? 1 : 0);
#endif // AISENSE_SIGHT_TIMESLICING_DEBUG

	if (InvalidQueries.Num() > 0)
	{
		for (int32 Index = InvalidQueries.Num() - 1; Index >= 0; --Index)
		{
			// removing with swapping here, since queue is going to be sorted anyway
			SightQueryQueue.RemoveAtSwap(InvalidQueries[Index], 1, /*bAllowShrinking*/false);
		}

		if (InvalidTargets.Num() > 0)
		{
			// this should not be happening since UAIPerceptionSystem::OnPerceptionStimuliSourceEndPlay introduction
			UE_VLOG(GetPerceptionSystem(), LogAIPerceptionVR, Error, TEXT("Invalid sight targets found during UAISense_Sight_VR::Update call"));

			for (const auto& TargetId : InvalidTargets)
			{
				// remove affected queries
				RemoveAllQueriesToTarget(TargetId, DontSort);
				// remove target itself
				ObservedTargets.Remove(TargetId);
			}

			// remove holes
			ObservedTargets.Compact();
		}
	}

	// sort Sight Queries
	{
		SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_UpdateSort);
		SortQueries();
	}

	//return SightQueryQueue.Num() > 0 ? 1.f/6 : FLT_MAX;
	return 0.f;
}

void UAISense_Sight_VR::RegisterEvent(const FAISightEventVR& Event)
{

}

void UAISense_Sight_VR::RegisterSource(AActor& SourceActor)
{
	RegisterTarget(SourceActor, Sort);
}

void UAISense_Sight_VR::UnregisterSource(AActor& SourceActor)
{
	const FAISightTargetVR::FTargetId AsTargetId = SourceActor.GetUniqueID();
	FAISightTargetVR AsTarget;

	if (ObservedTargets.RemoveAndCopyValue(AsTargetId, AsTarget)
		&& SightQueryQueue.Num() > 0)
	{
		AActor* TargetActor = AsTarget.Target.Get();

		if (TargetActor)
		{
			// notify all interested observers that this source is no longer
			// visible		
			AIPerception::FListenerMap& ListenersMap = *GetListeners();
			const FAISightQueryVR* SightQuery = &SightQueryQueue[SightQueryQueue.Num() - 1];
			for (int32 QueryIndex = SightQueryQueue.Num() - 1; QueryIndex >= 0; --QueryIndex, --SightQuery)
			{
				if (SightQuery->TargetId == AsTargetId)
				{
					if (SightQuery->bLastResult == true)
					{
						FPerceptionListener& Listener = ListenersMap[SightQuery->ObserverId];
						ensure(Listener.Listener.IsValid());

						Listener.RegisterStimulus(TargetActor, FAIStimulus(*this, 0.f, SightQuery->LastSeenLocation, Listener.CachedLocation, FAIStimulus::SensingFailed));
					}

					SightQueryQueue.RemoveAt(QueryIndex, 1, /*bAllowShrinking=*/false);
				}
			}
			// no point in sorting, we haven't change the order of other queries
		}
	}
}

void UAISense_Sight_VR::CleanseInvalidSources()
{
	bool bInvalidSourcesFound = false;
	int32 NumInvalidSourcesFound = 0;
	for (FTargetsContainer::TIterator ItTarget(ObservedTargets); ItTarget; ++ItTarget)
	{
		if (ItTarget->Value.Target.IsValid() == false)
		{
			// remove affected queries
			RemoveAllQueriesToTarget(ItTarget->Key, DontSort);
			// remove target itself
			ItTarget.RemoveCurrent();

			bInvalidSourcesFound = true;
			NumInvalidSourcesFound++;
		}
	}

	UE_LOG(LogAIPerceptionVR, Verbose, TEXT("UAISense_Sight_VR::CleanseInvalidSources called and removed %d invalid sources"), NumInvalidSourcesFound);

	if (bInvalidSourcesFound)
	{
		// remove holes
		ObservedTargets.Compact();
		SortQueries();
	}
	else
	{
		UE_VLOG(GetPerceptionSystem(), LogAIPerceptionVR, Error, TEXT("UAISense_Sight_VR::CleanseInvalidSources called and no invalid targets were found"));
	}
}

bool UAISense_Sight_VR::RegisterTarget(AActor& TargetActor, FQueriesOperationPostProcess PostProcess)
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
				FAISightQueryVR SightQuery(ItListener->Key, SightTarget->TargetId);
				SightQuery.Importance = CalcQueryImportance(ItListener->Value, TargetLocation, PropDigest.SightRadiusSq);

				SightQueryQueue.Add(SightQuery);
				bNewQueriesAdded = true;
			}
		}
	}

	// sort Sight Queries
	if (PostProcess == Sort && bNewQueriesAdded)
	{
		SortQueries();
		RequestImmediateUpdate();
	}

	return bNewQueriesAdded;
}

void UAISense_Sight_VR::OnNewListenerImpl(const FPerceptionListener& NewListener)
{
	check(NewListener.Listener.IsValid());
	const UAISenseConfig_Sight_VR* SenseConfig = Cast<const UAISenseConfig_Sight_VR>(NewListener.Listener->GetSenseConfig(GetSenseID()));
	check(SenseConfig);
	const FDigestedSightProperties PropertyDigest(*SenseConfig);
	DigestedProperties.Add(NewListener.GetListenerID(), PropertyDigest);

	GenerateQueriesForListener(NewListener, PropertyDigest);
}

void UAISense_Sight_VR::GenerateQueriesForListener(const FPerceptionListener& Listener, const FDigestedSightProperties& PropertyDigest)
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
			FAISightQueryVR SightQuery(Listener.GetListenerID(), ItTarget->Key);
			SightQuery.Importance = CalcQueryImportance(Listener, ItTarget->Value.GetLocationSimple(), PropertyDigest.SightRadiusSq);

			SightQueryQueue.Add(SightQuery);
			bNewQueriesAdded = true;
		}
	}

	// sort Sight Queries
	if (bNewQueriesAdded)
	{
		SortQueries();
		RequestImmediateUpdate();
	}
}

void UAISense_Sight_VR::OnListenerUpdateImpl(const FPerceptionListener& UpdatedListener)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_ListenerUpdate);

	// first, naive implementation:
	// 1. remove all queries by this listener
	// 2. proceed as if it was a new listener

	// remove all queries
	RemoveAllQueriesByListener(UpdatedListener, DontSort);

	// see if this listener is a Target as well
	const FAISightTargetVR::FTargetId AsTargetId = UpdatedListener.GetBodyActorUniqueID();
	FAISightTargetVR* AsTarget = ObservedTargets.Find(AsTargetId);
	if (AsTarget != NULL)
	{
		RemoveAllQueriesToTarget(AsTargetId, DontSort);
		if (AsTarget->Target.IsValid())
		{
			RegisterTarget(*(AsTarget->Target.Get()), DontSort);
		}
	}

	const FPerceptionListenerID ListenerID = UpdatedListener.GetListenerID();

	if (UpdatedListener.HasSense(GetSenseID()))
	{
		const UAISenseConfig_Sight_VR* SenseConfig = Cast<const UAISenseConfig_Sight_VR>(UpdatedListener.Listener->GetSenseConfig(GetSenseID()));
		check(SenseConfig);
		FDigestedSightProperties& PropertiesDigest = DigestedProperties.FindOrAdd(ListenerID);
		PropertiesDigest = FDigestedSightProperties(*SenseConfig);

		GenerateQueriesForListener(UpdatedListener, PropertiesDigest);
	}
	else
	{
		DigestedProperties.Remove(ListenerID);
	}
}

void UAISense_Sight_VR::OnListenerRemovedImpl(const FPerceptionListener& UpdatedListener)
{
	RemoveAllQueriesByListener(UpdatedListener, DontSort);

	DigestedProperties.FindAndRemoveChecked(UpdatedListener.GetListenerID());

	// note: there use to be code to remove all queries _to_ listener here as well
	// but that was wrong - the fact that a listener gets unregistered doesn't have to
	// mean it's being removed from the game altogether.
}

void UAISense_Sight_VR::RemoveAllQueriesByListener(const FPerceptionListener& Listener, FQueriesOperationPostProcess PostProcess)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RemoveByListener);

	if (SightQueryQueue.Num() == 0)
	{
		return;
	}

	const uint32 ListenerId = Listener.GetListenerID();
	bool bQueriesRemoved = false;

	const FAISightQueryVR* SightQuery = &SightQueryQueue[SightQueryQueue.Num() - 1];
	for (int32 QueryIndex = SightQueryQueue.Num() - 1; QueryIndex >= 0; --QueryIndex, --SightQuery)
	{
		if (SightQuery->ObserverId == ListenerId)
		{
			SightQueryQueue.RemoveAt(QueryIndex, 1, /*bAllowShrinking=*/false);
			bQueriesRemoved = true;
		}
	}

	if (PostProcess == Sort && bQueriesRemoved)
	{
		SortQueries();
	}
}

void UAISense_Sight_VR::RemoveAllQueriesToTarget(const FAISightTargetVR::FTargetId& TargetId, FQueriesOperationPostProcess PostProcess)
{
	SCOPE_CYCLE_COUNTER(STAT_AI_Sense_Sight_RemoveToTarget);

	if (SightQueryQueue.Num() == 0)
	{
		return;
	}

	bool bQueriesRemoved = false;

	const FAISightQueryVR* SightQuery = &SightQueryQueue[SightQueryQueue.Num() - 1];
	for (int32 QueryIndex = SightQueryQueue.Num() - 1; QueryIndex >= 0; --QueryIndex, --SightQuery)
	{
		if (SightQuery->TargetId == TargetId)
		{
			SightQueryQueue.RemoveAt(QueryIndex, 1, /*bAllowShrinking=*/false);
			bQueriesRemoved = true;
		}
	}

	if (PostProcess == Sort && bQueriesRemoved)
	{
		SortQueries();
	}
}

void UAISense_Sight_VR::OnListenerForgetsActor(const FPerceptionListener& Listener, AActor& ActorToForget)
{
	const uint32 ListenerId = Listener.GetListenerID();
	const uint32 TargetId = ActorToForget.GetUniqueID();

	for (FAISightQueryVR& SightQuery : SightQueryQueue)
	{
		if (SightQuery.ObserverId == ListenerId && SightQuery.TargetId == TargetId)
		{
			// assuming one query per observer-target pair
			SightQuery.ForgetPreviousResult();
			break;
		}
	}
}

void UAISense_Sight_VR::OnListenerForgetsAll(const FPerceptionListener& Listener)
{
	const uint32 ListenerId = Listener.GetListenerID();

	for (FAISightQueryVR& SightQuery : SightQueryQueue)
	{
		if (SightQuery.ObserverId == ListenerId)
		{
			SightQuery.ForgetPreviousResult();
		}
	}
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