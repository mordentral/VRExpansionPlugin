// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "ReplicatedVRCameraComponent.h"


UReplicatedVRCameraComponent::UReplicatedVRCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
//	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	this->SetIsReplicated(true);
	this->RelativeScale3D = FVector(1.0f, 1.0f, 1.0f);

	// Default 100 htz update rate, same as the 100htz update rate of rep_notify, will be capped to 90/45 though because of vsync on HMD
	bReplicateTransform = true;
	NetUpdateRate = 100.0f; // 100 htz is default
	NetUpdateCount = 0.0f;
}


//=============================================================================
void UReplicatedVRCameraComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{

	// I am skipping the Scene component replication here
	// Generally components aren't set to replicate anyway and I need it to NOT pass the Relative position through the network
	// There isn't much in the scene component to replicate anyway
	// Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Skipping the owner with this as the owner will use the location directly
	DOREPLIFETIME_CONDITION(UReplicatedVRCameraComponent, ReplicatedTransform, COND_SkipOwner);
	DOREPLIFETIME(UReplicatedVRCameraComponent, NetUpdateRate);
	DOREPLIFETIME(UReplicatedVRCameraComponent, bReplicateTransform);
}

void UReplicatedVRCameraComponent::Server_SendTransform_Implementation(FBPVRComponentPosRep NewTransform)
{
	// Store new transform and trigger OnRep_Function
	ReplicatedTransform = NewTransform;

	// Don't call on rep on the server if the server controls this controller
	if (!bHasAuthority)
	{
		OnRep_ReplicatedTransform();
	}
}

bool UReplicatedVRCameraComponent::Server_SendTransform_Validate(FBPVRComponentPosRep NewTransform)
{
	return true;
	// Optionally check to make sure that player is inside of their bounds and deny it if they aren't?
}

void UReplicatedVRCameraComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bHasAuthority = IsLocallyControlled();
	bIsServer = IsServer();

	// Don't bother with any of this if not replicating transform
	if (bHasAuthority && bReplicateTransform)
	{
		NetUpdateCount += DeltaTime;

		if (NetUpdateCount >= (1.0f / NetUpdateRate))
		{
			NetUpdateCount = 0.0f;

			ReplicatedTransform.Position = this->RelativeLocation;//Position;
			ReplicatedTransform.Orientation = this->RelativeRotation;//Orientation;
			Server_SendTransform(ReplicatedTransform);
		}
	}
}