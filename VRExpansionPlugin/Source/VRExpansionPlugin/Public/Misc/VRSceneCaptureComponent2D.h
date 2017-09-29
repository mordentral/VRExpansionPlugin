// This class is intended to correctly offset a scene capture for stereo rendering
// It is unfinished and I am leaving it out of the compiled base unless I go back to it
// Leaving this commented out in here for reference
/*
#pragma once
#include "Components/SceneCaptureComponent2D.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "CoreMinimal.h"
#include "IHeadMountedDisplay.h"

#include "VRSceneCaptureComponent2D.generated.h"


UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UVRSceneCaptureComponent2D : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:

	// Toggles applying late HMD positional / rotational updates to the capture
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRExpansionPlugin")
		bool bTrackLocalHMDOrCamera;

	// If is an HMD enabled capture, is this the left eye
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRExpansionPlugin")
		bool bIsLeftEye;

	//virtual void UpdateSceneCaptureContents(USceneCaptureComponent2D* CaptureComponent) override
	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override
	{

		// Apply eye offset
		// Apply eye matrix
		// Apply late update

		if (bTrackLocalHMDOrCamera)
		{

			EStereoscopicPass StereoPass = bIsLeftEye ? EStereoscopicPass::eSSP_LEFT_EYE : eSSP_RIGHT_EYE;

			FQuat Orientation = FQuat::Identity;
			FVector Position = FVector::ZeroVector;

			if (GEngine->HMDDevice.IsValid() && GEngine->IsStereoscopic3D() && GEngine->HMDDevice->IsHeadTrackingAllowed() && GEngine->HMDDevice->HasValidTrackingPosition())
			{
				GEngine->HMDDevice->GetCurrentOrientationAndPosition(Orientation, Position);

				float WorldToMeters = GetWorld() ? GetWorld()->GetWorldSettings()->WorldToMeters : 100.0f;

				GEngine->StereoRenderingDevice->CalculateStereoViewOffset(StereoPass, Orientation.Rotator(), WorldToMeters , Position);

				this->bUseCustomProjectionMatrix = true;

				float ActualFOV = 90.0f;
				if (GEngine->HMDDevice.IsValid())
				{
					float HMDVerticalFOV, HMDHorizontalFOV;
					GEngine->HMDDevice->GetFieldOfView(HMDHorizontalFOV, HMDVerticalFOV);
					if (HMDHorizontalFOV > 0)
					{
						ActualFOV = HMDHorizontalFOV;
					}
				}

				this->CustomProjectionMatrix = GEngine->HMDDevice.Get()->GetStereoProjectionMatrix(StereoPass, ActualFOV);
				this->CaptureStereoPass = StereoPass;
			}
			else
			{
				this->bUseCustomProjectionMatrix = false;
				this->CaptureStereoPass = EStereoscopicPass::eSSP_FULL;

				APlayerController* Player = GetWorld()->GetFirstPlayerController();
				if (Player != nullptr && Player->IsLocalController())
				{
					if (Player->GetPawn())
					{
						for (UActorComponent* CamComponent : Player->GetPawn()->GetComponentsByClass(UCameraComponent::StaticClass()))
						{
							UCameraComponent * CameraComponent = Cast<UCameraComponent>(CamComponent);

							if (CameraComponent != nullptr && CameraComponent->bIsActive)
							{
								FTransform trans = CameraComponent->GetRelativeTransform();

								Orientation = trans.GetRotation();
								Position = trans.GetTranslation();
								break;
							}
						}
					}
				}
			}

			this->SetRelativeLocationAndRotation(Position, Orientation);
		}
		else
		{
			this->bUseCustomProjectionMatrix = false;
			this->CaptureStereoPass = EStereoscopicPass::eSSP_FULL;
		}

		// This pulls from the GetComponentToWorld so setting just prior to it should have worked	
		Super::UpdateSceneCaptureContents(Scene);
	}

};*/