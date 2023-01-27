// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Grippables/GrippableCharacter.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GrippableCharacter)

#include "VRGlobalSettings.h"
#include "Grippables/GrippableSkeletalMeshComponent.h"


AGrippableCharacter::AGrippableCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass(ACharacter::MeshComponentName, UVRGlobalSettings::GetDefaultGrippableCharacterMeshComponentClass()))
{
	ViewOriginationSocket = NAME_None;
	GrippableMeshReference = Cast<UGrippableSkeletalMeshComponent>(GetMesh());
}

void AGrippableCharacter::GetActorEyesViewPoint(FVector& Location, FRotator& Rotation) const
{
	if (ViewOriginationSocket != NAME_None)
	{
		if (USkeletalMeshComponent* MyMesh = GetMesh())
		{
			FTransform SocketTransform = MyMesh->GetSocketTransform(ViewOriginationSocket, RTS_World);
			Location = SocketTransform.GetLocation();
			Rotation = SocketTransform.GetRotation().Rotator();
			return;
		}
	}

	Super::GetActorEyesViewPoint(Location, Rotation);
}