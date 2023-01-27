// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "ReplicatedVRCameraComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(ReplicatedVRCameraComponent)

#include "Net/UnrealNetwork.h"
#include "VRBaseCharacter.h"
#include "IXRTrackingSystem.h"
#include "IXRCamera.h"
#include "Rendering/MotionVectorSimulation.h"


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
	bScaleTracking = false;
	TrackingScaler = FVector(1.0f);
	bOffsetByHMD = false;
	bLimitMinHeight = false;
	MinimumHeightAllowed = 0.0f;
	bLimitMaxHeight = false;
	MaxHeightAllowed = 300.f;
	bLimitBounds = false;
	// Just shy of 20' distance from the center of tracked space
	MaximumTrackedBounds = 1028;

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
		AttachChar = nullptr;
	}

	Super::OnAttachmentChanged();
}

bool UReplicatedVRCameraComponent::HasTrackingParameters()
{
	return bOffsetByHMD || bScaleTracking || bLimitMaxHeight || bLimitMinHeight || bLimitBounds;
}

void UReplicatedVRCameraComponent::ApplyTrackingParameters(FVector &OriginalPosition)
{
	if (bOffsetByHMD)
	{
		OriginalPosition.X = 0;
		OriginalPosition.Y = 0;
	}

	if (bLimitBounds)
	{
		OriginalPosition.X = FMath::Clamp(OriginalPosition.X, -MaximumTrackedBounds, MaximumTrackedBounds);
		OriginalPosition.Y = FMath::Clamp(OriginalPosition.Y, -MaximumTrackedBounds, MaximumTrackedBounds);
	}

	if (bScaleTracking)
	{
		OriginalPosition *= TrackingScaler;
	}

	if (bLimitMaxHeight)
	{
		OriginalPosition.Z = FMath::Min(MaxHeightAllowed, OriginalPosition.Z);
	}

	if (bLimitMinHeight)
	{
		OriginalPosition.Z = FMath::Max(MinimumHeightAllowed, OriginalPosition.Z);
	}
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
				if (HasTrackingParameters())
				{
					ApplyTrackingParameters(Position);
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
				SetRelativeLocationAndRotation(MotionSampleUpdateBuffer[0].Position, MotionSampleUpdateBuffer[0].Rotation);

				static const auto CVarDoubleBufferTrackedDevices = IConsoleManager::Get().FindConsoleVariable(TEXT("vr.DoubleBufferReplicatedTrackedDevices"));
				if (CVarDoubleBufferTrackedDevices->GetBool())
				{
					LastUpdatesRelativePosition = this->GetRelativeLocation();
					LastUpdatesRelativeRotation = this->GetRelativeRotation();
					NetUpdateCount = 0.0f;

					// Move to next sample, we are catching up
					MotionSampleUpdateBuffer[0] = MotionSampleUpdateBuffer[1];
				}
				else
				{
					// Stop lerping, wait for next update if it is delayed or lost then it will hitch here
					// Actual prediction might be something to consider in the future, but rough to do in VR
					// considering the speed and accuracy of movements
					// would like to consider sub stepping but since there is no server rollback...not sure how useful it would be
					// and might be perf taxing enough to not make it worth it.
					bLerpingPosition = false;
					NetUpdateCount = 0.0f;
				}
			}
			else
			{
				// Removed variables to speed this up a bit
				SetRelativeLocationAndRotation(
					FMath::Lerp(LastUpdatesRelativePosition, (FVector)MotionSampleUpdateBuffer[0].Position, LerpVal),
					FMath::Lerp(LastUpdatesRelativeRotation, MotionSampleUpdateBuffer[0].Rotation, LerpVal)
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


	if (!bUpdateInCharacterMovement || !IsValid(AttachChar))
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

void UReplicatedVRCameraComponent::HandleXRCamera()
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
			if (XRSystem->IsHeadTrackingAllowedForWorld(*GetWorld()))
			{
				const FTransform ParentWorld = CalcNewComponentToWorld(FTransform());
				XRCamera->SetupLateUpdate(ParentWorld, this, bLockToHmd == 0);

				if (bLockToHmd)
				{
					FQuat Orientation;
					FVector Position;
					if (XRCamera->UpdatePlayerCamera(Orientation, Position))
					{
						if (HasTrackingParameters())
						{
							ApplyTrackingParameters(Position);
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
}

void UReplicatedVRCameraComponent::OnRep_ReplicatedCameraTransform()
{
    if (GetNetMode() < ENetMode::NM_Client && HasTrackingParameters())
    {
        // Ensure that we clamp to the expected values from the client
        ApplyTrackingParameters(ReplicatedCameraTransform.Position);
    }
    
    if (bSmoothReplicatedMotion)
    {
        static const auto CVarDoubleBufferTrackedDevices = IConsoleManager::Get().FindConsoleVariable(TEXT("vr.DoubleBufferReplicatedTrackedDevices"));
        if (bReppedOnce)
        {
            bLerpingPosition = true;
            NetUpdateCount = 0.0f;
            LastUpdatesRelativePosition = this->GetRelativeLocation();
            LastUpdatesRelativeRotation = this->GetRelativeRotation();
            
            if (CVarDoubleBufferTrackedDevices->GetBool())
            {
                MotionSampleUpdateBuffer[0] = MotionSampleUpdateBuffer[1];
                MotionSampleUpdateBuffer[1] = ReplicatedCameraTransform;
            }
            else
            {
                MotionSampleUpdateBuffer[0] = ReplicatedCameraTransform;
                // Also set the buffered value in case double buffering gets turned on
                MotionSampleUpdateBuffer[1] = MotionSampleUpdateBuffer[0];
            }
        }
        else
        {
            SetRelativeLocationAndRotation(ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);
            bReppedOnce = true;
            
            // Filling the second index in as well in case they turn on double buffering
            MotionSampleUpdateBuffer[1] = ReplicatedCameraTransform;
            MotionSampleUpdateBuffer[0] = MotionSampleUpdateBuffer[1];
        }
    }
    else
        SetRelativeLocationAndRotation(ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);
}