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

//public:
	// If to use HMD offset
	bool bOffsetByHMD;

//protected:

	/** Sets lock to hmd automatically based on if the camera is currently locally controlled or not */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		uint32 bAutoSetLockToHmd : 1;

	// Would have to offset controllers by same amount or will feel off
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	//bool bUseVRNeckOffset;

	/** An optional extra transform to adjust the final view without moving the component, in the camera's local space, sets additive offset */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
	//FTransform VRNeckOffset;


	UFUNCTION(BlueprintCallable, Category = Camera)
		virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ReplicatedTransform, Category = "VRExpansionLibrary")
	FBPVRComponentPosRep ReplicatedTransform;

	UFUNCTION()
	virtual void OnRep_ReplicatedTransform()
	{
		ReplicatedTransform.Unpack();

		SetRelativeLocationAndRotation(ReplicatedTransform.UnpackedLocation, ReplicatedTransform.UnpackedRotation);
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
	FORCEINLINE bool IsLocallyControlled() const
	{
		// I like epics new authority check more than my own
		const AActor* MyOwner = GetOwner();
		const APawn* MyPawn = Cast<APawn>(MyOwner);
		return MyPawn ? MyPawn->IsLocallyControlled() : (MyOwner->Role == ENetRole::ROLE_Authority);
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