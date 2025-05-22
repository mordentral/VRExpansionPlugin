// This class is intended to provide support for Local Mixed play between a mouse and keyboard player
// and a VR player. It is not needed outside of that use.

#pragma once
#include "Engine/GameViewportClient.h"

//#include "Engine/Engine.h"
#include "CoreMinimal.h"
#include "InputKeyEventArgs.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

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

	bool IsValidGamePadKey(const FKey& InputKey);

	UFUNCTION()
		bool EventWindowClosing();

	virtual void PostInitProperties() override;
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual bool InputAxis(const FInputKeyEventArgs& Args) override;
};