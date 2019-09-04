// Fill out your copyright notice in the Description page of Project Settings.

#include "SteamVRKeyboardComponent.h"
#include "Engine/Engine.h"
#include "OpenVRExpansionFunctionLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
//#include "GripMotionControllerComponent.h"


//=============================================================================
USteamVRKeyboardComponent::USteamVRKeyboardComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

//=============================================================================
USteamVRKeyboardComponent::~USteamVRKeyboardComponent()
{
}

void USteamVRKeyboardComponent::OnUnregister()
{
#if STEAMVR_SUPPORTED_PLATFORM
	if (KeyboardHandle.IsValid())
	{
		EBPOVRResultSwitch Result;
		CloseVRKeyboard(Result);
	}
#endif

	Super::OnUnregister();
}


void USteamVRKeyboardComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if !STEAMVR_SUPPORTED_PLATFORM
return;
#else
	if (/*!UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn ||*/ !KeyboardHandle.IsValid())
	{
		return;
	}

	if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
	{
		return;
	}

	vr::IVROverlay* VROverlay = vr::VROverlay();

	
	vr::IVRInput* VRInput = vr::VRInput();

	if (!VROverlay)
	{
		return;
	}

	FTransform PlayerTransform = FTransform::Identity;

	// Get first local player controller
	/*APlayerController* PC = nullptr;
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		if (Iterator->Get()->IsLocalPlayerController())
		{
			PC = Iterator->Get();
			break;
		}
	}*/
	APlayerController* PC = nullptr;
	if (UWorld * CurWorld = GetWorld())
	{
		const ULocalPlayer* FirstPlayer = GEngine->GetFirstGamePlayer(CurWorld);
		PC = FirstPlayer ? FirstPlayer->GetPlayerController(CurWorld) : nullptr;
	}

	if (PC)
	{
		APawn * mpawn = PC->GetPawnOrSpectator();
		//bTextureNeedsUpdate = true;
		if (mpawn)
		{
			// Set transform to this relative transform
			PlayerTransform = mpawn->GetTransform();
		}
	}

	float WorldToMetersScale = UHeadMountedDisplayFunctionLibrary::GetWorldToMetersScale(GetWorld());

	// HMD Matrix
	FTransform RelTransform = this->GetComponentTransform();
	RelTransform = RelTransform.GetRelativeTransform(PlayerTransform);

	FQuat Rot = RelTransform.GetRotation();
	RelTransform.SetRotation(FQuat(Rot.Y, Rot.Z, -Rot.X, -Rot.W));

	FVector pos = RelTransform.GetTranslation();
	RelTransform.SetTranslation(FVector(pos.Y, pos.Z, -pos.X) / WorldToMetersScale);

	FVector scale = RelTransform.GetScale3D();
	RelTransform.SetScale3D(FVector(scale.Y, scale.Z, scale.X) / WorldToMetersScale);

	vr::HmdMatrix34_t NewTransform = UOpenVRExpansionFunctionLibrary::ToHmdMatrix34(RelTransform.ToMatrixNoScale());
	VROverlay->SetKeyboardTransformAbsolute(vr::ETrackingUniverseOrigin::TrackingUniverseStanding, &NewTransform);

	// Poll SteamVR events
	vr::VREvent_t VREvent;

	while (KeyboardHandle.IsValid() && VROverlay->PollNextOverlayEvent(KeyboardHandle.VRKeyboardHandle, &VREvent, sizeof(VREvent)))
	{

		//VRKeyboardEvent_None = 0,
		//VRKeyboardEvent_OverlayFocusChanged = 307, // data is overlay, global event
		//VRKeyboardEvent_OverlayShown = 500,
		//VRKeyboardEvent_OverlayHidden = 501,
		//VRKeyboardEvent_ShowKeyboard = 509, // Sent to keyboard renderer in the dashboard to invoke it
		//VRKeyboardEvent_HideKeyboard = 510, // Sent to keyboard renderer in the dashboard to hide it
		//VRKeyboardEvent_OverlayGamepadFocusGained = 511, // Sent to an overlay when IVROverlay::SetFocusOverlay is called on it
		//VRKeyboardEvent_OverlayGamepadFocusLost = 512, // Send to an overlay when it previously had focus and IVROverlay::SetFocusOverlay is called on something else
		//VRKeyboardEvent_OverlaySharedTextureChanged = 513,
		//VRKeyboardEvent_KeyboardClosed = 1200,
		//VRKeyboardEvent_KeyboardCharInput = 1201,
		//VRKeyboardEvent_KeyboardDone = 1202, // Sent when DONE button clicked on keyboard

		switch (VREvent.eventType)
		{
		case vr::VREvent_KeyboardCharInput:
		{
			char OutString[512];
			uint32 TextLen = VROverlay->GetKeyboardText((char*)&OutString, 512);
			OnKeyboardCharInput.Broadcast(FString(ANSI_TO_TCHAR(OutString)));
		}break;
		case vr::VREvent_KeyboardClosed:
		{
			if (KeyboardHandle.IsValid())
			{
				VROverlay->DestroyOverlay(KeyboardHandle.VRKeyboardHandle);
				KeyboardHandle.VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
			}
			OnKeyboardClosed.Broadcast();
		}break;
		case vr::VREvent_KeyboardDone:
		{
			char OutString[512];
			uint32 TextLen = VROverlay->GetKeyboardText((char*)&OutString, 512);
			OnKeyboardDone.Broadcast(FString(ANSI_TO_TCHAR(OutString)));

			if (KeyboardHandle.IsValid())
			{
				VROverlay->HideKeyboard();
				VROverlay->DestroyOverlay(KeyboardHandle.VRKeyboardHandle);
				KeyboardHandle.VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
			}
		}break;

		default:break;
		}
	}

#endif
}