// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine.h"
#include "ReplicatedVRCameraComponent.generated.h"


UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UReplicatedVRCameraComponent : public UCameraComponent
{
	GENERATED_UCLASS_BODY()
		//	~UGripMotionControllerComponent();

	void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	/** Whether or not this component has authority within the frame*/
	bool bHasAuthority;

	/** Whether or not this component is currently on the network server*/
	bool bIsServer;

	// Whether to ever replicate position
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "VRExpansionLibrary")
	//bool bReplicateTransform;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReplicatedTransform, Category = "VRExpansionLibrary")
	FBPVRComponentPosRep ReplicatedTransform;

	UFUNCTION()
	virtual void OnRep_ReplicatedTransform()
	{
		SetRelativeLocationAndRotation(ReplicatedTransform.Position, ReplicatedTransform.Orientation);
	}

	// Rate to update the position to the server, 100htz is default (same as replication rate, should also hit every tick).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "VRExpansionLibrary")
	float NetUpdateRate;

	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case.
	float NetUpdateCount;

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
	void Server_SendTransform(FBPVRComponentPosRep NewTransform);

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	bool IsLocallyControlled() const
	{
		// Epic used a check for a player controller to control has authority, however the controllers are always attached to a pawn
		// So this check would have always failed to work in the first place.....

		APawn* Owner = Cast<APawn>(GetOwner());

		if (!Owner)
		{
			//const APlayerController* Actor = Cast<APlayerController>(GetOwner());
			//if (!Actor)
			return false;

			//return Actor->IsLocalPlayerController();
		}

		return Owner->IsLocallyControlled();
	}

	bool IsServer() const
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
	}
};