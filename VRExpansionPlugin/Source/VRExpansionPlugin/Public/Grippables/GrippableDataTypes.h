// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GrippableDataTypes.generated.h"

// A version of the attachment structure that include welding data
USTRUCT()
struct FRepAttachmentWithWeld : public FRepAttachment
{
public:
	GENERATED_BODY()

	// Add in the is welded property
	UPROPERTY()
	bool bIsWelded;

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		// Our additional weld bit is here
		Ar.SerializeBits(&bIsWelded, 1);
		Ar << AttachParent;
		LocationOffset.NetSerialize(Ar, Map, bOutSuccess);
		RelativeScale3D.NetSerialize(Ar, Map, bOutSuccess);
		RotationOffset.SerializeCompressedShort(Ar);
		Ar << AttachSocket;
		Ar << AttachComponent;
		return true;
	}

	FRepAttachmentWithWeld()
	{
		bIsWelded = false;
	}
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