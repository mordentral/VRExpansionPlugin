// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/GrippableDataTypes.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GrippableDataTypes)

#include "GameFramework/Actor.h"


bool FRepAttachmentWithWeld::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
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

FRepAttachmentWithWeld::FRepAttachmentWithWeld()
{
	bIsWelded = false;
}