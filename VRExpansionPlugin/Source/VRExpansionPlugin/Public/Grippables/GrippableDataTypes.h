// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
//#include "Engine/EngineTypes.h"
#include "Engine/ReplicatedState.h"
#include "GrippableDataTypes.generated.h"

// A version of the attachment structure that include welding data
USTRUCT()
struct VREXPANSIONPLUGIN_API FRepAttachmentWithWeld : public FRepAttachment
{
public:
	GENERATED_BODY()

	// Add in the is welded property
	UPROPERTY()
	bool bIsWelded;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	FRepAttachmentWithWeld();
};

template<>
struct TStructOpsTypeTraits< FRepAttachmentWithWeld > : public TStructOpsTypeTraitsBase2<FRepAttachmentWithWeld>
{
	enum
	{
		WithNetSerializer = true//,
		//WithNetSharedSerialization = true,
	};
};
