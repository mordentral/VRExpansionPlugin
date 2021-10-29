// This class is intended to provide support for Local Mixed play between a mouse and keyboard player
// and a VR player. It is not needed outside of that use.

#pragma once
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "CoreMinimal.h"

#include "VRGameViewportClient.generated.h"

UENUM(Blueprintable)
enum class EVRGameInputMethod : uint8
{
	GameInput_Default,
	GameInput_SharedKeyboardAndMouse,
	GameInput_KeyboardAndMouseToPlayer2,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVROnWindowCloseRequested);


/**
* Subclass this in a blueprint to overwrite how default input is passed around in engine between local characters.
* Generally used for passing keyboard / mouse input to a secondary local player for local mixed gameplay in VR
*/
UCLASS(Blueprintable)
class VREXPANSIONPLUGIN_API UVRGameViewportClient : public UGameViewportClient
{
	GENERATED_UCLASS_BODY()

public:

	// Event thrown when the window is closed
	UPROPERTY(BlueprintAssignable, Category = "VRExpansionPlugin")
		FVROnWindowCloseRequested BPOnWindowCloseRequested;

	// If true then forced window closing will be canceled (alt-f4, ect)
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRExpansionPlugin")
		bool bIgnoreWindowCloseCommands;

	// Input Method for the viewport
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRExpansionPlugin")
		EVRGameInputMethod GameInputMethod;

	// If true we will also shuffle gamepad input according to the GameInputMethod
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRExpansionPlugin")
		bool bAlsoChangeGamepPadInput;

	// A List of input categories to consider as valid gamepad ones if bIsGamepad is true on the input event
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRExpansionPlugin")
		TArray<FName> GamepadInputCategories;

	bool IsValidGamePadKey(const FKey & InputKey)
	{
		if (!bAlsoChangeGamepPadInput)
			return false;

		FName KeyCategory = InputKey.GetMenuCategory();
		
		return GamepadInputCategories.Contains(KeyCategory);
	}

	UFUNCTION()
	bool EventWindowClosing()
	{
		if (BPOnWindowCloseRequested.IsBound())
		{
			BPOnWindowCloseRequested.Broadcast();
		}

		if (bIgnoreWindowCloseCommands)
		{
			return false;
		}
		
		return true;
	}

	virtual void PostInitProperties() override
	{
		Super::PostInitProperties();

		if (GamepadInputCategories.Num() < 1)
		{
			GamepadInputCategories.Add(FName(TEXT("Gamepad")));
			GamepadInputCategories.Add(FName(TEXT("PS4")));
			GamepadInputCategories.Add(FName(TEXT("XBox One")));
			GamepadInputCategories.Add(FName(TEXT("Touch")));
			GamepadInputCategories.Add(FName(TEXT("Gesture")));
		}

		OnWindowCloseRequested().BindUObject(this, &UVRGameViewportClient::EventWindowClosing);
	}


	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
	{
		// Early out if a gamepad event or ignoring input or is default setup / no GEngine
		if(GameInputMethod == EVRGameInputMethod::GameInput_Default || IgnoreInput() || (EventArgs.IsGamepad() && !IsValidGamePadKey(EventArgs.Key)))
			return Super::InputKey(EventArgs);

		const int32 NumLocalPlayers = World->GetGameInstance()->GetNumLocalPlayers();

		// Also early out if number of players is less than 2
		if (NumLocalPlayers < 2)
			return Super::InputKey(EventArgs);

		// Its const so have to copy and send a new one in now that the function signature has changed
		FInputKeyEventArgs NewStruct = EventArgs;

		if (GameInputMethod == EVRGameInputMethod::GameInput_KeyboardAndMouseToPlayer2)
		{
			// keyboard / mouse always go to player 0, so + 1 will be player 2
			NewStruct.ControllerId++;
			return Super::InputKey(NewStruct);
		}
		else // Shared keyboard and mouse
		{
			bool bRetVal = false;
			for (int32 i = 0; i < NumLocalPlayers; i++)
			{
				NewStruct.ControllerId = i;
				bRetVal = Super::InputKey(NewStruct) || bRetVal;
			}

			return bRetVal;
		}
	}

	virtual bool InputAxis(FViewport* tViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples = 1, bool bGamepad = false) override
	{		
		
		const int32 NumLocalPlayers = World->GetGameInstance()->GetNumLocalPlayers();

		// Early out if a gamepad or not a mouse event (vr controller) or ignoring input or is default setup / no GEngine
		if (((!Key.IsMouseButton() && !bGamepad) || (bGamepad && !IsValidGamePadKey(Key))) || NumLocalPlayers < 2 || GameInputMethod == EVRGameInputMethod::GameInput_Default || IgnoreInput())
			return Super::InputAxis(tViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);

		if (GameInputMethod == EVRGameInputMethod::GameInput_KeyboardAndMouseToPlayer2)
		{
			// keyboard / mouse always go to player 0, so + 1 will be player 2
			++ControllerId;
			return Super::InputAxis(tViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
		}
		else // Shared keyboard and mouse
		{
			bool bRetVal = false;
			for (int32 i = 0; i < NumLocalPlayers; i++)
			{
				bRetVal = Super::InputAxis(tViewport, i, Key, Delta, DeltaTime, NumSamples, bGamepad) || bRetVal;
			}

			return bRetVal;
		}

	}
};

UVRGameViewportClient::UVRGameViewportClient(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GameInputMethod = EVRGameInputMethod::GameInput_Default;
	bAlsoChangeGamepPadInput = false;
}