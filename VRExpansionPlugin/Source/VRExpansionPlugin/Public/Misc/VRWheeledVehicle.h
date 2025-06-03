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


// This only exists to expose the blueprint properties of the transmission values
USTRUCT(BlueprintType, Category = "VRWheeledVehicle")
struct VREXPANSIONPLUGIN_API FBPVehicleTransmissionConfig : public FVehicleTransmissionConfig
{
	GENERATED_BODY()
public:

	void SetFrom(FVehicleTransmissionConfig & Config)
	{
		bUseAutomaticGears = Config.bUseAutomaticGears;
		bUseAutoReverse = Config.bUseAutoReverse;
		FinalRatio = Config.FinalRatio;
		ForwardGearRatios = Config.ForwardGearRatios;
		ReverseGearRatios = Config.ReverseGearRatios;
		ChangeUpRPM = Config.ChangeUpRPM;
		ChangeDownRPM = Config.ChangeDownRPM;
		GearChangeTime = Config.GearChangeTime;
		TransmissionEfficiency = Config.TransmissionEfficiency;
	}

	void SetTo(FVehicleTransmissionConfig& Config)
	{
		Config.bUseAutomaticGears = bUseAutomaticGears;
		Config.bUseAutoReverse = bUseAutoReverse;
		Config.FinalRatio = FinalRatio;
		Config.ForwardGearRatios = ForwardGearRatios;
		Config.ReverseGearRatios = ReverseGearRatios;
		Config.ChangeUpRPM = ChangeUpRPM;
		Config.ChangeDownRPM = ChangeDownRPM;
		Config.GearChangeTime = GearChangeTime;
		Config.TransmissionEfficiency = TransmissionEfficiency;
	}
};

/**
* This override of the base wheeled vehicle allows for dual pawn usage in engine.
*/
UCLASS(config = Game, BlueprintType)
class VREXPANSIONPLUGIN_API AVRWheeledVehicle : public AWheeledVehiclePawn
//#endif
{
	GENERATED_BODY()

public:


	UFUNCTION(BlueprintPure, Category = "Pawn")
	float GetFinalGearRatio()
	{
		float CurrentGearRatio = 0.0f;
		if (UChaosWheeledVehicleMovementComponent* MoveComp = Cast<UChaosWheeledVehicleMovementComponent>(this->GetMovementComponent()))
		{		
			CurrentGearRatio = MoveComp->TransmissionSetup.FinalRatio;
		}

		return CurrentGearRatio;
	}

	UFUNCTION(BlueprintCallable, Category = "Pawn")
	void SetFinalGearRatio(float NewFinalGearRatio)
	{
		if (UChaosWheeledVehicleMovementComponent* MoveComp = Cast<UChaosWheeledVehicleMovementComponent>(this->GetMovementComponent()))
		{
			MoveComp->TransmissionSetup.FinalRatio = NewFinalGearRatio;
		}
	}

	// Calls the movement components override controller function
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

						//if (UInputDelegateBinding::SupportsInputDelegate(GetClass()))
						//{
							InputComponent->bBlockInput = bBlockInput;
							UInputDelegateBinding::BindInputDelegates(GetClass(), InputComponent);
						//}
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