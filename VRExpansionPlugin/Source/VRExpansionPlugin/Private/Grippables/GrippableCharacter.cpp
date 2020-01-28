// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/GrippableCharacter.h"
#include "Grippables/GrippableSkeletalMeshComponent.h"


AGrippableCharacter::AGrippableCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGrippableSkeletalMeshComponent>(ACharacter::MeshComponentName))

{}