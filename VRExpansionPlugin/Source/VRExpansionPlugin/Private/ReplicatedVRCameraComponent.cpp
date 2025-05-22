// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "ReplicatedVRCameraComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(ReplicatedVRCameraComponent)

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "VRBaseCharacter.h"
#include "VRCharacter.h"
#include "VRRootComponent.h"
#include "IXRTrackingSystem.h"
#include "IXRCamera.h"
#include "Rendering/MotionVectorSimulation.h"

#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif

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
	//bOffsetByHMD = false;
	bLimitMinHeight = false;
	MinimumHeightAllowed = 0.0f;
	bLimitMaxHeight = false;
	MaxHeightAllowed = 300.f;
	bLimitBounds = false;
	// Just shy of 20' distance from the center of tracked space
	MaximumTrackedBounds = 1028;

	bSetPositionDuringTick = false;
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

	// For properties with special conditions
	FDoRepLifetimeParams PushModelParamsWithCondition{ COND_SkipOwner, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	// Skipping the owner with this as the owner will use the location directly
	DOREPLIFETIME_WITH_PARAMS_FAST(UReplicatedVRCameraComponent, ReplicatedCameraTransform, PushModelParamsWithCondition);

	// For std properties
	FDoRepLifetimeParams PushModelParams{ COND_None, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UReplicatedVRCameraComponent, NetUpdateRate, PushModelParams);
	//DOREPLIFETIME(UReplicatedVRCameraComponent, bSmoothReplicatedMotion); // This doesn't need to be replicated
	//DOREPLIFETIME(UReplicatedVRCameraComponent, bReplicateTransform);
}

void UReplicatedVRCameraComponent::Server_SendCameraTransform_Implementation(FBPVRComponentPosRep NewTransform)
{
	// Store new transform and trigger OnRep_Function
	ReplicatedCameraTransform = NewTransform;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UReplicatedVRCameraComponent, ReplicatedCameraTransform, this);
#endif

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
	if (AVRCharacter* CharacterOwner = Cast<AVRCharacter>(this->GetOwner()))
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
	return /*bOffsetByHMD ||*/ bScaleTracking || bLimitMaxHeight || bLimitMinHeight || bLimitBounds || (AttachChar && !AttachChar->bRetainRoomscale);
}

void UReplicatedVRCameraComponent::ApplyTrackingParameters(FVector &OriginalPosition, bool bSkipLocZero)
{
	// I'm keeping the original values here as it lets me send them out for seated mode
	if (!bSkipLocZero && (AttachChar && !AttachChar->bRetainRoomscale))
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
					ApplyTrackingParameters(Position, true);
				}

				ReplicatedCameraTransform.Position = Position;
				ReplicatedCameraTransform.Rotation = Orientation.Rotator();

				if (IsValid(AttachChar) && !AttachChar->bRetainRoomscale)
				{	
					// Zero out camera posiiton
					Position.X = 0.0f;
					Position.Y = 0.0f;

					FRotator StoredCameraRotOffset = FRotator::ZeroRotator;
					if (AttachChar->VRMovementReference && AttachChar->VRMovementReference->GetReplicatedMovementMode() == EVRConjoinedMovementModes::C_VRMOVE_Seated)
					{
						//StoredCameraRotOffset = AttachChar->SeatInformation.InitialRelCameraTransform.Rotator();
					}
					else
					{
						StoredCameraRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(Orientation.Rotator());
					}

					Position += StoredCameraRotOffset.RotateVector(FVector(-AttachChar->VRRootReference->VRCapsuleOffset.X, -AttachChar->VRRootReference->VRCapsuleOffset.Y, 0.0f));
				
				}

				SetRelativeTransform(FTransform(Orientation, Position));
			}
		}
	}
	else
	{
		// Run any networked smoothing
		RunNetworkedSmoothing(DeltaTime);
	}

	// Save out the component velocity from this and last frame
	if(bHadValidFirstVelocity || !LastRelativePosition.Equals(FTransform::Identity))
	{ 
		bHadValidFirstVelocity = true;
		ComponentVelocity = ((bSampleVelocityInWorldSpace ? GetComponentLocation() : GetRelativeLocation()) - LastRelativePosition.GetTranslation()) / DeltaTime;
	}

	LastRelativePosition = bSampleVelocityInWorldSpace ? this->GetComponentTransform() : this->GetRelativeTransform();
}

void UReplicatedVRCameraComponent::RunNetworkedSmoothing(float DeltaTime)
{
	FVector RetainPositionOffset(0.0f, 0.0f, ReplicatedCameraTransform.Position.Z);

	if (AttachChar && !AttachChar->bRetainRoomscale)
	{
		FRotator StoredCameraRotOffset = FRotator::ZeroRotator;
		if (AttachChar->VRMovementReference && AttachChar->VRMovementReference->GetReplicatedMovementMode() == EVRConjoinedMovementModes::C_VRMOVE_Seated)
		{
			//StoredCameraRotOffset = AttachChar->SeatInformation.InitialRelCameraTransform.Rotator();
		}
		else
		{
			StoredCameraRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(ReplicatedCameraTransform.Rotation);
		}

		RetainPositionOffset += StoredCameraRotOffset.RotateVector(FVector(-AttachChar->VRRootReference->VRCapsuleOffset.X, -AttachChar->VRRootReference->VRCapsuleOffset.Y, 0.0f));
	}

	if (bLerpingPosition)
	{
		if (!bUseExponentialSmoothing)
		{
			NetUpdateCount += DeltaTime;
			float LerpVal = FMath::Clamp(NetUpdateCount / (1.0f / NetUpdateRate), 0.0f, 1.0f);

			if (LerpVal >= 1.0f)
			{
				if (AttachChar && !AttachChar->bRetainRoomscale)
				{
					SetRelativeLocationAndRotation(RetainPositionOffset, ReplicatedCameraTransform.Rotation);
				}
				else
				{
					SetRelativeLocationAndRotation(ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);
				}

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

				if (AttachChar && !AttachChar->bRetainRoomscale)
				{
					// Removed variables to speed this up a bit
					SetRelativeLocationAndRotation(
						FMath::Lerp(LastUpdatesRelativePosition, RetainPositionOffset, LerpVal),
						FMath::Lerp(LastUpdatesRelativeRotation, ReplicatedCameraTransform.Rotation, LerpVal)
					);
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
		else // Exponential Smoothing
		{
			if (InterpolationSpeed <= 0.f)
			{
				if (AttachChar && !AttachChar->bRetainRoomscale)
				{
					SetRelativeLocationAndRotation(RetainPositionOffset, ReplicatedCameraTransform.Rotation);
				}
				else
				{
					SetRelativeLocationAndRotation((FVector)ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);
				}

				bLerpingPosition = false;
				return;
			}

			const float Alpha = FMath::Clamp(DeltaTime * InterpolationSpeed, 0.f, 1.f);

			FTransform NA = FTransform(GetRelativeRotation(), GetRelativeLocation(), FVector(1.0f));
			FTransform NB = FTransform::Identity;

			if (AttachChar && !AttachChar->bRetainRoomscale)
			{
				NB = FTransform(ReplicatedCameraTransform.Rotation, RetainPositionOffset, FVector(1.0f));
			}
			else
			{
				NB = FTransform(ReplicatedCameraTransform.Rotation, (FVector)ReplicatedCameraTransform.Position, FVector(1.0f));
			}

			NA.NormalizeRotation();
			NB.NormalizeRotation();

			NA.Blend(NA, NB, Alpha);

			// If we are nearly equal then snap to final position
			if (NA.EqualsNoScale(NB))
			{
				if (AttachChar && !AttachChar->bRetainRoomscale)
				{
					SetRelativeLocationAndRotation(RetainPositionOffset, ReplicatedCameraTransform.Rotation);
				}
				else
				{ 
					SetRelativeLocationAndRotation(ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);
				}
			}
			else // Else just keep going
			{
				SetRelativeLocationAndRotation(NA.GetTranslation(), NA.Rotator());
			}
		}
	}
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
			if (!RelativeLoc.Equals(LastUpdatesRelativePosition) || !RelativeRot.Equals(LastUpdatesRelativeRotation))
			{
				NetUpdateCount += DeltaTime;

				if (NetUpdateCount >= (1.0f / NetUpdateRate))
				{
					NetUpdateCount = 0.0f;

					// Already stored out now, only do this for FPS debug characters
					if (bFPSDebugMode)
					{
						ReplicatedCameraTransform.Position = RelativeLoc;
						ReplicatedCameraTransform.Rotation = RelativeRot;
					}

#if WITH_PUSH_MODEL
					MARK_PROPERTY_DIRTY_FROM_NAME(UReplicatedVRCameraComponent, ReplicatedCameraTransform, this);
#endif

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

					LastUpdatesRelativeRotation = RelativeRot;
					LastUpdatesRelativePosition = RelativeLoc;
				}
			}
		}
	}
}

void UReplicatedVRCameraComponent::HandleXRCamera(float DeltaTime)
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
					if (XRCamera->UpdatePlayerCamera(Orientation, Position, DeltaTime))
					{
						if (HasTrackingParameters())
						{
							ApplyTrackingParameters(Position, true);
						}

						ReplicatedCameraTransform.Position = Position;
						ReplicatedCameraTransform.Rotation = Orientation.Rotator();

						if (IsValid(AttachChar) && !AttachChar->bRetainRoomscale)
						{
							// Zero out XY for non retained
							Position.X = 0.0f;
							Position.Y = 0.0f;
							//FRotator OffsetRotator = 
							if (AttachChar->VRMovementReference && AttachChar->VRMovementReference->GetReplicatedMovementMode() != EVRConjoinedMovementModes::C_VRMOVE_Seated)
							{
								//AttachChar->SeatInformation.InitialRelCameraTransform.Rotator();

								FRotator StoredCameraRotOffset = FRotator::ZeroRotator;
								if (AttachChar->VRMovementReference->GetReplicatedMovementMode() == EVRConjoinedMovementModes::C_VRMOVE_Seated)
								{
									//StoredCameraRotOffset = AttachChar->SeatInformation.InitialRelCameraTransform.Rotator();
								}
								else
								{
									StoredCameraRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(Orientation.Rotator());
								}

								Position += StoredCameraRotOffset.RotateVector(FVector(-AttachChar->VRRootReference->VRCapsuleOffset.X, -AttachChar->VRRootReference->VRCapsuleOffset.Y, 0.0f));
							}
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

FTransform UReplicatedVRCameraComponent::GetHMDTrackingTransform()
{
	return FTransform(ReplicatedCameraTransform.Rotation, ReplicatedCameraTransform.Position);
}

void UReplicatedVRCameraComponent::OnRep_ReplicatedCameraTransform()
{
    if (GetNetMode() < ENetMode::NM_Client && HasTrackingParameters())
    {
        // Ensure that we clamp to the expected values from the client
        ApplyTrackingParameters(ReplicatedCameraTransform.Position, true);
    }

	FVector CameraPosition = ReplicatedCameraTransform.Position;
	if (AttachChar && !AttachChar->bRetainRoomscale)
	{
		CameraPosition.X = 0;
		CameraPosition.Y = 0;

		FRotator StoredCameraRotOffset = FRotator::ZeroRotator;
		if (AttachChar->VRMovementReference && AttachChar->VRMovementReference->GetReplicatedMovementMode() == EVRConjoinedMovementModes::C_VRMOVE_Seated)
		{
			//StoredCameraRotOffset = AttachChar->SeatInformation.InitialRelCameraTransform.Rotator();
		}
		else
		{
			StoredCameraRotOffset = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(ReplicatedCameraTransform.Rotation);
		}

		CameraPosition += StoredCameraRotOffset.RotateVector(FVector(-AttachChar->VRRootReference->VRCapsuleOffset.X, -AttachChar->VRRootReference->VRCapsuleOffset.Y, 0.0f));

	}
    
    if (bSmoothReplicatedMotion)
    {
        if (bReppedOnce)
        {
            bLerpingPosition = true;
            NetUpdateCount = 0.0f;
            LastUpdatesRelativePosition = this->GetRelativeLocation();
            LastUpdatesRelativeRotation = this->GetRelativeRotation();

			if (bUseExponentialSmoothing)
			{
				FVector OldToNewVector = CameraPosition - LastUpdatesRelativePosition;
				float NewDistance = OldToNewVector.SizeSquared();

				// Too far, snap to the new value
				if (NewDistance >= FMath::Square(NetworkNoSmoothUpdateDistance))
				{
					SetRelativeLocationAndRotation(CameraPosition, ReplicatedCameraTransform.Rotation);
					bLerpingPosition = false;
				}
				// Outside of the buffer distance, snap within buffer and keep smoothing from there
				else if (NewDistance >= FMath::Square(NetworkMaxSmoothUpdateDistance))
				{
					FVector Offset = (OldToNewVector.Size() - NetworkMaxSmoothUpdateDistance) * OldToNewVector.GetSafeNormal();
					SetRelativeLocation(LastUpdatesRelativePosition + Offset);
				}
			}
        }
        else
        {
            SetRelativeLocationAndRotation(CameraPosition, ReplicatedCameraTransform.Rotation);
            bReppedOnce = true;
        }
    }
    else
        SetRelativeLocationAndRotation(CameraPosition, ReplicatedCameraTransform.Rotation);
}

void UReplicatedVRCameraComponent::SetNetUpdateRate(float NewNetUpdateRate)
{
	NetUpdateRate = NewNetUpdateRate;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UReplicatedVRCameraComponent, NetUpdateRate, this);
#endif
}

bool UReplicatedVRCameraComponent::IsLocallyControlled() const
{
	// I like epics new authority check more than my own
	const AActor* MyOwner = GetOwner();
	return MyOwner->HasLocalNetOwner();
}