// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Pawn.h"
#include "Engine/InputDelegateBinding.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "VRVehiclePawn.generated.h"


/**
* This override of the base pawn allows for dual pawn usage in engine.
* It adds two new functions: SetBindToInput to bind input locally to the pawn and ForceSecondaryPossession which fakes possession so the 
* player can control the pawn as if they were locally possessed into it in a multiplayer enviroment (no lag).
*/
UCLASS(config = Game, BlueprintType)
class VREXPANSIONPLUGIN_API AVRVehiclePawn : public APawn
{
	GENERATED_BODY()

public:

	/** Call this function to detach safely pawn from its controller, knowing that we will be destroyed soon.	 */
	/*UFUNCTION(BlueprintCallable, Category = "Pawn", meta = (Keywords = "Delete"))
		virtual void DetachFromControllerPendingDestroy() override
	{
		if (Controller != NULL && Controller->GetPawn() == this)
		{
			Controller->PawnPendingDestroy(this);
			if (Controller != NULL)
			{
				Controller->UnPossess();
				Controller = NULL;
			}
		}
	}*/


	//UFUNCTION()
		virtual void OnRep_Controller() override
	{
		if ((Controller != NULL) && (Controller->GetPawn() == NULL))
		{
			// This ensures that APawn::OnRep_Pawn is called. Since we cant ensure replication order of APawn::Controller and AController::Pawn,
			// if APawn::Controller is repped first, it will set AController::Pawn locally. When AController::Pawn is repped, the rep value will not
			// be different from the just set local value, and OnRep_Pawn will not be called. This can cause problems if OnRep_Pawn does anything important.
			//
			// It would be better to never ever set replicated properties locally, but this is pretty core in the gameplay framework and I think there are
			// lots of assumptions made in the code base that the Pawn and Controller will always be linked both ways.
			//Controller->SetPawnFromRep(this);

			/*APlayerController* const PC = Cast<APlayerController>(Controller);
			if ((PC != NULL) && PC->bAutoManageActiveCameraTarget && (PC->PlayerCameraManager->ViewTarget.Target == Controller))
			{
				PC->AutoManageActiveCameraTarget(this);
			}*/
		}

		/*if (IsLocallyControlled())
		{
			SetBindToInput(Controller, true);
		}*/

	}


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

	UFUNCTION(BlueprintCallable, Category = "Pawn")
		virtual bool ForceSecondaryPossession(AController * NewController)
	{
		if (NewController)
		{
			PossessedBy(NewController);
		}
		else
		{
			UnPossessed();
		}

		return false;
		//INetworkPredictionInterface* NetworkPredictionInterface = GetPawn() ? Cast<INetworkPredictionInterface>(GetPawn()->GetMovementComponent()) : NULL;
		//if (NetworkPredictionInterface)
		//{
		//	NetworkPredictionInterface->ResetPredictionData_Server();
	//	}


	// Local PCs will have the Restart() triggered right away in ClientRestart (via PawnClientRestart()), but the server should call Restart() locally for remote PCs.
	// We're really just trying to avoid calling Restart() multiple times.
	//	if (!IsLocalPlayerController())
	//	{
		//	GetPawn()->Restart();
	//	}
	//	ClientRestart(GetPawn());

		//ChangeState(NAME_Playing);
		//if (bAutoManageActiveCameraTarget)
		//{
		//	AutoManageActiveCameraTarget(GetPawn());
		//	ResetCameraMode();
		//}
		//UpdateNavigationComponents();
	}

};