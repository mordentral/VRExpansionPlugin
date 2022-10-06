// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "ChaosWheeledVehicleMovementComponent.h"

#include "UObject/ObjectMacros.h"
#include "GameFramework/Pawn.h"
#include "Engine/InputDelegateBinding.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "VRWheeledVehicle.generated.h"


/**
* This override of the base wheeled vehicle allows for dual pawn usage in engine.
*/
UCLASS(config = Game, BlueprintType)
class VREXPANSIONPLUGIN_API AVRWheeledVehicle : public AWheeledVehiclePawn
//#endif
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Pawn")
		virtual bool SetBindToInput(AController * CController, bool bBindToInput)
	{
		APlayerController * playe = Cast<APlayerController>(CController);

		if (playe != NULL)
		{
			if(InputComponent)
				playe->PopInputComponent(InputComponent); // Make sure it is off the stack

			if (!bBindToInput)
			{			
				// Unregister input component if we created one
				DestroyPlayerInputComponent();
				return true;
			}
			else
			{
				// Set up player input component, if there isn't one already.
				if (InputComponent == NULL)
				{
					InputComponent = CreatePlayerInputComponent();
					if (InputComponent)
					{
						SetupPlayerInputComponent(InputComponent);
						InputComponent->RegisterComponent();

						if (UInputDelegateBinding::SupportsInputDelegate(GetClass()))
						{
							InputComponent->bBlockInput = bBlockInput;
							UInputDelegateBinding::BindInputDelegates(GetClass(), InputComponent);
						}
					}
				}

				if (InputComponent)
				{
					playe->PushInputComponent(InputComponent); // Enforce input as top of stack so it gets input first and can consume it
					return true;
				}
			}
		}
		else
		{
			// Unregister input component if we created one
			DestroyPlayerInputComponent();
			return false;
		}

		return false;
	}

	// Calls the movement components override controller function
	UFUNCTION(BlueprintCallable, Category = "Pawn")
		virtual bool SetOverrideController(AController * NewController)
	{
		if (UChaosWheeledVehicleMovementComponent* MoveComp = Cast<UChaosWheeledVehicleMovementComponent>(this->GetMovementComponent()))
		{
			MoveComp->SetOverrideController(NewController);
			return true;
		}
		
		return false;
	}

};