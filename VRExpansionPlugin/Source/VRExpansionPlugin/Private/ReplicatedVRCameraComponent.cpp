// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "ReplicatedVRCameraComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Engine.h"
#include "VRBaseCharacter.h"
#include "IXRTrackingSystem.h"
#include "IXRCamera.h"
#include "Rendering/MotionVectorSimulation.h"
#include "VRBaseCharacter.h"
#include "IHeadMountedDisplay.h"


// Ported epics head tracking allowed for world fix back to this temp patch in 4.27
bool TMP_IsHeadTrackingAllowedForWorld(IXRTrackingSystem* XRSystem, UWorld* World)
{
#if WITH_EDITOR
	// This implementation is constrained by hotfix rules.  It would be better to cache this somewhere.
	if (!XRSystem->IsHeadTrackingAllowed())
	{
		return false;
	}

	if (World->WorldType != EWorldType::PIE)
	{
		return true;
	}

	// If we are a pie instance then the first pie world that is not a dedicated server uses head tracking
	const int32 MyPIEInstanceID = World->GetOutermost()->PIEInstanceID;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::PIE && WorldContext.RunAsDedicated == false && WorldContext.World())
		{
			return WorldContext.World()->GetOutermost()->PIEInstanceID == MyPIEInstanceID;
		}
	}
	return false;
#endif
	return XRSystem->IsHeadTrackingAllowedForWorld(*World);
}


UReplicatedVRCameraComponent::UReplicatedVRCameraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	//PrimaryComponentTick.TickGroup = TG_PrePhysics;

	SetIsReplicatedByDefault(true);
	SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));

	// Default 100 htz update rate, same as the 100htz update rate of rep_notify, will be capped to 90/45 though because of vsync on HMD
	//bReplicateTransform = true;
	NetUpdateRate = 100.0f; // 100 htz is default
	NetUpdateCount = 0.0f;

	bUsePawnControlRotation = false;
	bAutoSetLockToHmd = true;
	bOffsetByHMD = false;

	bSetPositionDuringTick = false;
	bSmoothReplicatedMotion = false;
	bLerpingPosition = false;
	bReppedOnce = false;

	OverrideSendTransform = nullptr;

	LastRelativePosition = FTransform::Identity;
	bSampleVelocityInWorldSpace = false;
	bHadValidFirstVelocity = false;

	//bUseVRNeckOffset = true;
	//VRNeckOffset = FTransform(FRotator::ZeroRotator, FVector(15.0f,0,0), FVector(1.0f));

}


//=============================================================================
void UReplicatedVRCameraComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{

	// I am skipping the Scene component replication here
	// Generally components aren't set to replicate anyway and I need it to NOT pass the Relative position through the network
	// There isn't much in the scene component to replicate anyway
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DISABLE_REPLICATED_PRIVATE_PROPERTY(USceneComponent, RelativeLocation);
	DISABLE_REPLICATED_PRIVATE_PROPERTY(USceneComponent, RelativeRotation);
	DISABLE_REPLICATED_PRIVATE_PROPERTY(USceneComponent, RelativeScale3D);

	// Skipping the owner with this as the owner will use the location directly
	DOREPLIFETIME_CONDITION(UReplicatedVRCameraComponent, ReplicatedCameraTransform, COND_SkipOwner);
	DOREPLIFETIME(UReplicatedVRCameraComponent, NetUpdateRate);
	DOREPLIFETIME(UReplicatedVRCameraComponent, bSmoothReplicatedMotion);
	//DOREPLIFETIME(UReplicatedVRCameraComponent, bReplicateTransform);
}

// Just skipping this, it generates warnings for attached meshes when using this method of denying transform replication
/*void UReplicatedVRCameraComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't ever replicate these, they are getting replaced by my custom send anyway
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeLocation, false);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeRotation, false);
	DOREPLIFETIME_ACTIVE_OVERRIDE(USceneComponent, RelativeScale3D, false);
}*/

void UReplicatedVRCameraComponent::Server_SendCameraTransform_Implementation(FBPVRComponentPosRep NewTransform)
{
	// Store new transform and trigger OnRep_Function
	ReplicatedCameraTransform = NewTransform;

	// Don't call on rep on the server if the server controls this controller
	if (!bHasAuthority)
	{
		OnRep_ReplicatedCameraTransform();
	}
}

bool UReplicatedVRCameraComponent::Server_SendCameraTransform_Validate(FBPVRComponentPosRep NewTransform)
{
	return true;
	// Optionally check to make sure that player is inside of their bounds and deny it if they aren't?
}

/*bool UReplicatedVRCameraComponent::IsServer()
{
	if (GEngine != nullptr && GWorld != nullptr)
	{
		switch (GEngine->GetNetMode(GWorld))
		{
		case NM_Client:
		{return false; } break;
		case NM_DedicatedServer:
		case NM_ListenServer:
		default:
		{return true; } break;
		}
	}

	return false;
}*/

void UReplicatedVRCameraComponent::OnAttachmentChanged()
{
	if (AVRBaseCharacter* CharacterOwner = Cast<AVRBaseCharacter>(this->GetOwner()))
	{
		AttachChar = CharacterOwner;
	}
	else
	{
		AttachChar.Reset();
	}

	Super::OnAttachmentChanged();
}

void UReplicatedVRCameraComponent::UpdateTracking(float DeltaTime)
{
	bHasAuthority = IsLocallyControlled();

	// Don't do any of the below if we aren't the authority
	if (bHasAuthority)
	{
		// For non view target positional updates (third party and the like)
		if (bSetPositionDuringTick && bLockToHmd && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowedForWorld(*GetWorld()))
		{
			//ResetRelativeTransform();
			FQuat Orientation;
			FVector Position;
			if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, Orientation, Position))
			{
				if (bOffsetByHMD)
				{
					Position.X = 0;
					Position.Y = 0;
				}

				SetRelativeTransform(FTransform(Orientation, Position));
			}
		}
	}
	else
	{
		if (bLerpingPosition)
		{
			NetUpdateCount += DeltaTime;
			float LerpVal = FMath::Clamp(NetUpdateCount / (1.0f / NetUpdateRate), 0.0f, 1.0f);

			if (LerpVal >= 1.0f)
			{
				SetRelativeLocationAndRotation(ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);

				// Stop lerping, wait for next update if it is delayed or lost then it will hitch here
				// Actual prediction might be something to consider in the future, but rough to do in VR
				// considering the speed and accuracy of movements
				// would like to consider sub stepping but since there is no server rollback...not sure how useful it would be
				// and might be perf taxing enough to not make it worth it.
				bLerpingPosition = false;
				NetUpdateCount = 0.0f;
			}
			else
			{
				// Removed variables to speed this up a bit
				SetRelativeLocationAndRotation(
					FMath::Lerp(LastUpdatesRelativePosition, (FVector)ReplicatedCameraTransform.Position, LerpVal),
					FMath::Lerp(LastUpdatesRelativeRotation, ReplicatedCameraTransform.Rotation, LerpVal)
				);
			}
		}
	}

	// Save out the component velocity from this and last frame
	if(bHadValidFirstVelocity || !LastRelativePosition.Equals(FTransform::Identity))
	{ 
		bHadValidFirstVelocity = true;
		ComponentVelocity = ((bSampleVelocityInWorldSpace ? GetComponentLocation() : GetRelativeLocation()) - LastRelativePosition.GetTranslation()) / DeltaTime;
	}

	LastRelativePosition = bSampleVelocityInWorldSpace ? this->GetComponentTransform() : this->GetRelativeTransform();
}


void UReplicatedVRCameraComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);


	if (!bUpdateInCharacterMovement || !AttachChar.IsValid())
	{
		UpdateTracking(DeltaTime);
	}
	else
	{
		UCharacterMovementComponent* CharMove = AttachChar->GetCharacterMovement();
		if (!CharMove || !CharMove->IsComponentTickEnabled() || !CharMove->IsActive() || (!CharMove->PrimaryComponentTick.bTickEvenWhenPaused && GetWorld()->IsPaused()))
		{
			// Our character movement isn't handling our updates, lets do it ourself.
			UpdateTracking(DeltaTime);
		}
	}

	if (bHasAuthority)
	{
		// Send changes
		if (this->GetIsReplicated())
		{
			FRotator RelativeRot = GetRelativeRotation();
			FVector RelativeLoc = GetRelativeLocation();

			// Don't rep if no changes
			if (!RelativeLoc.Equals(ReplicatedCameraTransform.Position) || !RelativeRot.Equals(ReplicatedCameraTransform.Rotation))
			{
				NetUpdateCount += DeltaTime;

				if (NetUpdateCount >= (1.0f / NetUpdateRate))
				{
					NetUpdateCount = 0.0f;
					ReplicatedCameraTransform.Position = RelativeLoc;
					ReplicatedCameraTransform.Rotation = RelativeRot;


					if (GetNetMode() == NM_Client)
					{
						AVRBaseCharacter* OwningChar = Cast<AVRBaseCharacter>(GetOwner());
						if (OverrideSendTransform != nullptr && OwningChar != nullptr)
						{
							(OwningChar->* (OverrideSendTransform))(ReplicatedCameraTransform);
						}
						else
						{
							// Don't bother with any of this if not replicating transform
							//if (bHasAuthority && bReplicateTransform)
							Server_SendCameraTransform(ReplicatedCameraTransform);
						}
					}
				}
			}
		}
	}
}

void UReplicatedVRCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
	bool bIsLocallyControlled = IsLocallyControlled();

	if (bAutoSetLockToHmd)
	{
		if (bIsLocallyControlled)
			bLockToHmd = true;
		else
			bLockToHmd = false;
	}

	if (bIsLocallyControlled && GEngine && GEngine->XRSystem.IsValid() && GetWorld() && GetWorld()->WorldType != EWorldType::Editor)
	{
		IXRTrackingSystem* XRSystem = GEngine->XRSystem.Get();
		auto XRCamera = XRSystem->GetXRCamera();

		if (XRCamera.IsValid())
		{
			//if (XRSystem->IsHeadTrackingAllowedForWorld(*GetWorld()))
			if (TMP_IsHeadTrackingAllowedForWorld(XRSystem, GetWorld()))
			{
				const FTransform ParentWorld = CalcNewComponentToWorld(FTransform());
				XRCamera->SetupLateUpdate(ParentWorld, this, bLockToHmd == 0);
				
				if (bLockToHmd)
				{
					FQuat Orientation;
					FVector Position;
					if (XRCamera->UpdatePlayerCamera(Orientation, Position))
					{
						if (bOffsetByHMD)
						{
							Position.X = 0;
							Position.Y = 0;
						}

						SetRelativeTransform(FTransform(Orientation, Position));
					}
					else
					{
						SetRelativeScale3D(FVector(1.0f));
						//ResetRelativeTransform(); stop doing this, it is problematic
						// Let the camera freeze in the last position instead
						// Setting scale by itself makes sure we don't get camera scaling but keeps the last location and rotation alive
					}
				}

				// #TODO: Check back on this, was moved here in 4.20 but shouldn't it be inside of bLockToHMD?
				XRCamera->OverrideFOV(this->FieldOfView);
			}
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

	if (bUseAdditiveOffset)
	{
		FTransform OffsetCamToBaseCam = AdditiveOffset;
		FTransform BaseCamToWorld = GetComponentToWorld();
		FTransform OffsetCamToWorld = OffsetCamToBaseCam * BaseCamToWorld;

		DesiredView.Location = OffsetCamToWorld.GetLocation();
		DesiredView.Rotation = OffsetCamToWorld.Rotator();
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

	// If this camera component has a motion vector simumlation transform, use that for the current view's previous transform
	DesiredView.PreviousViewTransform = FMotionVectorSimulation::Get().GetPreviousTransform(this);
}