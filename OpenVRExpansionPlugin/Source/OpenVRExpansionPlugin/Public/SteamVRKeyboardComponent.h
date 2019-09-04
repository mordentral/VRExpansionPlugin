// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
//#include "EngineMinimal.h"
//#include "VRBPDatatypes.h"
#include "OpenVRExpansionFunctionLibrary.h"
//#include "GripMotionControllerComponent.h"
#include "Engine/Engine.h"

#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"

#include "SteamVRKeyboardComponent.generated.h"


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRKeyboardStringCallbackSignature, FString, Text);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVRKeyboardNullCallbackSignature);

/**
* Allows displaying / hiding and sending input to and from the SteamVR keyboard. Has events for keyboard inputs
* Generally outdated with the data table based keyboards I added, but still useful.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class OPENVREXPANSIONPLUGIN_API USteamVRKeyboardComponent : public USceneComponent
{

public:

	GENERATED_BODY()

public:
	USteamVRKeyboardComponent(const FObjectInitializer& ObjectInitializer);

	~USteamVRKeyboardComponent();

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void OnUnregister() override;
	// Keyboard Functions //

#if STEAMVR_SUPPORTED_PLATFORM
	FBPOpenVRKeyboardHandle KeyboardHandle;
#endif

	UPROPERTY(BlueprintAssignable, Category = "VRExpansionFunctions|SteamVR")
	FVRKeyboardStringCallbackSignature OnKeyboardDone;

	UPROPERTY(BlueprintAssignable, Category = "VRExpansionFunctions|SteamVR")
	FVRKeyboardNullCallbackSignature OnKeyboardClosed;

	UPROPERTY(BlueprintAssignable, Category = "VRExpansionFunctions|SteamVR")
		FVRKeyboardStringCallbackSignature OnKeyboardCharInput;

	
	// Opens the vrkeyboard, can fail if already open or in use
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", ExpandEnumAsExecs = "Result"))
		void OpenVRKeyboard(bool bIsForPassword, bool bIsMultiline, bool bUseMinimalMode, bool bIsRightHand, int32 MaxCharacters, FString Description, FString StartingString, EBPOVRResultSwitch & Result)
	{
#if !STEAMVR_SUPPORTED_PLATFORM
		Result = EBPOVRResultSwitch::OnFailed;
		return;
#else
		if (/*!UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn ||*/ KeyboardHandle.IsValid())
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		if (KeyboardHandle.IsValid())
		{
			Result = EBPOVRResultSwitch::OnSucceeded;
			return;
		}

		if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		vr::IVROverlay* VROverlay = vr::VROverlay();

		if (!VROverlay)
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		vr::EVROverlayError OverlayError;
		OverlayError = VROverlay->CreateOverlay("KeyboardOverlay", "Keyboard Overlay", &KeyboardHandle.VRKeyboardHandle);

		if (OverlayError != vr::EVROverlayError::VROverlayError_None || !KeyboardHandle.IsValid())
		{
			KeyboardHandle.VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		vr::EGamepadTextInputMode Inputmode = bIsForPassword ? vr::EGamepadTextInputMode::k_EGamepadTextInputModePassword : vr::EGamepadTextInputMode::k_EGamepadTextInputModeNormal;
		vr::EGamepadTextInputLineMode LineInputMode = bIsMultiline ? vr::EGamepadTextInputLineMode::k_EGamepadTextInputLineModeMultipleLines : vr::EGamepadTextInputLineMode::k_EGamepadTextInputLineModeSingleLine;
		uint32 HandInteracting = bIsRightHand ? 0 : 1;

		if (bIsForPassword)
			OverlayError = VROverlay->ShowKeyboardForOverlay(KeyboardHandle.VRKeyboardHandle, Inputmode, LineInputMode, TCHAR_TO_ANSI(*Description), MaxCharacters, TCHAR_TO_ANSI(*StartingString), bUseMinimalMode, HandInteracting);
		else
			OverlayError = VROverlay->ShowKeyboardForOverlay(KeyboardHandle.VRKeyboardHandle, Inputmode, LineInputMode, TCHAR_TO_ANSI(*Description), MaxCharacters, TCHAR_TO_ANSI(*StartingString), bUseMinimalMode, HandInteracting);

		if (OverlayError != vr::EVROverlayError::VROverlayError_None)
		{
			VROverlay->DestroyOverlay(KeyboardHandle.VRKeyboardHandle);
			KeyboardHandle.VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		//VROverlay->SetOverlayAlpha(KeyboardHandle.VRKeyboardHandle, 0.0f); // Might need to remove this, keyboard would be invis?
		VROverlay->ShowOverlay(KeyboardHandle.VRKeyboardHandle);

		//		// Set the position of the keyboard in world space
		//virtual void SetKeyboardTransformAbsolute(ETrackingUniverseOrigin eTrackingOrigin, const HmdMatrix34_t *pmatTrackingOriginToKeyboardTransform) = 0;


		//		// Set the position of the keyboard in overlay space by telling it to avoid a rectangle in the overlay. Rectangle coords have (0,0) in the bottom left
		//virtual void SetKeyboardPositionForOverlay(VROverlayHandle_t ulOverlayHandle, HmdRect2_t avoidRect) = 0;
		//VROverlay->SetOverlayTransformAbsolute(,ETrackingUniverseOrigin::TrackingUniverseStanding,HMDMatrix)

		//const float WorldToMeterScale = FMath::Max(GetWorldToMetersScale(), 0.1f);
		//OVR_VERIFY(VROverlay->SetOverlayWidthInMeters(Layer.OverlayHandle, Layer.LayerDesc.QuadSize.X / WorldToMeterScale));
		//OVR_VERIFY(VROverlay->SetOverlayTexelAspect(Layer.OverlayHandle, Layer.LayerDesc.QuadSize.X / Layer.LayerDesc.QuadSize.Y));
		//OVR_VERIFY(VROverlay->SetOverlaySortOrder(Layer.OverlayHandle, Layer.LayerDesc.Priority));

		this->SetComponentTickEnabled(true);
		Result = EBPOVRResultSwitch::OnSucceeded;
#endif
	}


	// Closes the vrkeyboard, can fail if not already open
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", ExpandEnumAsExecs = "Result"))
		void CloseVRKeyboard(EBPOVRResultSwitch & Result)
	{
#if !STEAMVR_SUPPORTED_PLATFORM
		Result = EBPOVRResultSwitch::OnFailed;
		return;
#else
		if (/*!UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn ||*/ !KeyboardHandle.IsValid())
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		vr::IVROverlay* VROverlay = vr::VROverlay();

		if (!VROverlay)
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		VROverlay->HideKeyboard();
		VROverlay->HideOverlay(KeyboardHandle.VRKeyboardHandle);

		vr::EVROverlayError OverlayError;
		OverlayError = VROverlay->DestroyOverlay(KeyboardHandle.VRKeyboardHandle);
		KeyboardHandle.VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
		this->SetComponentTickEnabled(false);
		Result = EBPOVRResultSwitch::OnSucceeded;
#endif
	}


	// Re-Opens the vr keyboard that is currently active, can be used for switching interacting hands and the like.
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", ExpandEnumAsExecs = "Result"))
	void ReOpenVRKeyboardForUser(bool bIsForPassword, bool bIsMultiline, bool bUseMinimalMode, bool bIsRightHand, int32 MaxCharacters, FString Description, FString StartingString, EBPOVRResultSwitch & Result)
	{
#if !STEAMVR_SUPPORTED_PLATFORM
		Result = EBPOVRResultSwitch::OnFailed;
		return;
#else
		if (/*!UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn ||*/ !KeyboardHandle.IsValid())
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		vr::IVROverlay* VROverlay = vr::VROverlay();

		if (!VROverlay)
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		vr::EVROverlayError OverlayError;

		VROverlay->HideKeyboard();

		vr::EGamepadTextInputMode Inputmode = bIsForPassword ? vr::EGamepadTextInputMode::k_EGamepadTextInputModePassword : vr::EGamepadTextInputMode::k_EGamepadTextInputModeNormal;
		vr::EGamepadTextInputLineMode LineInputMode = bIsMultiline ? vr::EGamepadTextInputLineMode::k_EGamepadTextInputLineModeMultipleLines : vr::EGamepadTextInputLineMode::k_EGamepadTextInputLineModeSingleLine;
		uint32 HandInteracting = bIsRightHand ? 0 : 1;

		if (bIsForPassword)
			OverlayError = VROverlay->ShowKeyboardForOverlay(KeyboardHandle.VRKeyboardHandle, Inputmode, LineInputMode, TCHAR_TO_ANSI(*Description), MaxCharacters, TCHAR_TO_ANSI(*StartingString), bUseMinimalMode, HandInteracting);
		else
			OverlayError = VROverlay->ShowKeyboardForOverlay(KeyboardHandle.VRKeyboardHandle, Inputmode, LineInputMode, TCHAR_TO_ANSI(*Description), MaxCharacters, TCHAR_TO_ANSI(*StartingString), bUseMinimalMode, HandInteracting);

		if (OverlayError != vr::EVROverlayError::VROverlayError_None)
		{
			VROverlay->DestroyOverlay(KeyboardHandle.VRKeyboardHandle);
			KeyboardHandle.VRKeyboardHandle = vr::k_ulOverlayHandleInvalid;
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		Result = EBPOVRResultSwitch::OnSucceeded;
#endif
	}


	// Closes the vrkeyboard, can fail if not already open
	UFUNCTION(BlueprintCallable, Category = "VRExpansionFunctions|SteamVR", meta = (bIgnoreSelf = "true", ExpandEnumAsExecs = "Result"))
		void GetVRKeyboardText(FString & Text, EBPOVRResultSwitch & Result)
	{
#if !STEAMVR_SUPPORTED_PLATFORM
		Result = EBPOVRResultSwitch::OnFailed;
		return;
#else
		if (/*!UOpenVRExpansionFunctionLibrary::VRGetGenericInterfaceFn ||*/ !KeyboardHandle.IsValid())
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		if (!GEngine->XRSystem.IsValid() || (GEngine->XRSystem->GetSystemName() != SteamVRSystemName))
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		vr::IVROverlay* VROverlay = vr::VROverlay();

		if (!VROverlay)
		{
			Result = EBPOVRResultSwitch::OnFailed;
			return;
		}

		char OutString[512];
		uint32 TextLen = VROverlay->GetKeyboardText((char*)&OutString, 512);

		Text = FString(ANSI_TO_TCHAR(OutString));
		Result = EBPOVRResultSwitch::OnSucceeded;
#endif
	}


};