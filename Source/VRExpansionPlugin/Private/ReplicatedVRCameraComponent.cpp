// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "Net/UnrealNetwork.h"
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
	//bReplicateTransform = true;
	NetUpdateRate = 100.0f; // 100 htz is default
	NetUpdateCount = 0.0f;

	bUsePawnControlRotation = false;
	bAutoSetLockToHmd = true;
	bOffsetByHMD = false;

	//bUseVRNeckOffset = true;
	//VRNeckOffset = FTransform(FRotator::ZeroRotator, FVector(15.0f,0,0), FVector(1.0f));

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
	//DOREPLIFETIME(UReplicatedVRCameraComponent, bReplicateTransform);
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
	bHasAuthority = IsLocallyControlled();
	//bIsServer = IsServer();

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bHasAuthority && bReplicates)
	{
		ReplicatedTransform.Position = this->RelativeLocation;//Position;
		ReplicatedTransform.SetRotation(this->RelativeRotation);//.Orientation = this->RelativeRotation;//Orientation;

		// Don't bother with any of this if not replicating transform
		if (GetNetMode() == NM_Client)	//if (bHasAuthority && bReplicateTransform)
		{
			NetUpdateCount += DeltaTime;

			if (NetUpdateCount >= (1.0f / NetUpdateRate))
			{
				NetUpdateCount = 0.0f;
				Server_SendTransform(ReplicatedTransform);
			}
		}
	}
}

void UReplicatedVRCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	if (bAutoSetLockToHmd)
	{
		if (IsLocallyControlled())
			bLockToHmd = true;
		else
			bLockToHmd = false;
	}

	if (bLockToHmd && GEngine->HMDDevice.IsValid() && GEngine->HMDDevice->IsHeadTrackingAllowed() && GEngine->HMDDevice->HasValidTrackingPosition())
	{
		ResetRelativeTransform();
		const FTransform ParentWorld = GetComponentToWorld();
		GEngine->HMDDevice->SetupLateUpdate(ParentWorld, this);

		FQuat Orientation;
		FVector Position;
		if (GEngine->HMDDevice->UpdatePlayerCamera(Orientation, Position))
		{
			if (bOffsetByHMD)
			{
				Position.X = 0;
				Position.Y = 0;
			}

			SetRelativeTransform(FTransform(Orientation, Position));
		}
	}

	if (bUsePawnControlRotation)
	{
		const APawn* OwningPawn = Cast<APawn>(GetOwner());
		const AController* OwningController = OwningPawn ? OwningPawn->GetController() : nullptr;
		if (OwningController && OwningController->IsLocalPlayerController())
		{
			const FRotator PawnViewRotation = OwningPawn->GetViewRotation();
			if (!PawnViewRotation.Equals(GetComponentRotation()))
			{
				SetWorldRotation(PawnViewRotation);
			}
		}
	}

	if (bUseAdditiveOffset)//bUseVRNeckOffset)
	{
		FTransform OffsetCamToBaseCam = AdditiveOffset;//VRNeckOffset;
		FTransform BaseCamToWorld = GetComponentToWorld();
		FTransform OffsetCamToWorld = OffsetCamToBaseCam * BaseCamToWorld;

		DesiredView.Location = OffsetCamToWorld.GetLocation();
		DesiredView.Rotation = OffsetCamToWorld.Rotator();

#if WITH_EDITORONLY_DATA
		if (ProxyMeshComponent)
		{
			ResetProxyMeshTransform();

			FTransform LocalTransform = ProxyMeshComponent->GetRelativeTransform();
			FTransform WorldTransform = LocalTransform * OffsetCamToWorld;

			ProxyMeshComponent->SetWorldTransform(WorldTransform);
		}
#endif
	}
	else
	{
		DesiredView.Location = GetComponentLocation();
		DesiredView.Rotation = GetComponentRotation();
	}

	DesiredView.FOV = bUseAdditiveOffset ? (FieldOfView + AdditiveFOVOffset) : FieldOfView;
	DesiredView.AspectRatio = AspectRatio;
	DesiredView.bConstrainAspectRatio = bConstrainAspectRatio;
	DesiredView.bUseFieldOfViewForLOD = bUseFieldOfViewForLOD;
	DesiredView.ProjectionMode = ProjectionMode;
	DesiredView.OrthoWidth = OrthoWidth;
	DesiredView.OrthoNearClipPlane = OrthoNearClipPlane;
	DesiredView.OrthoFarClipPlane = OrthoFarClipPlane;

	// See if the CameraActor wants to override the PostProcess settings used.
	DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;
	if (PostProcessBlendWeight > 0.0f)
	{
		DesiredView.PostProcessSettings = PostProcessSettings;
	}

#if WITH_EDITOR
	ResetProxyMeshTransform();
#endif //WITH_EDITOR
}