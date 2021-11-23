// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "ReplicatedVRCameraComponent.generated.h"

class AVRBaseCharacter;

/**
* An overridden camera component that replicates its location in multiplayer
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = VRExpansionLibrary)
class VREXPANSIONPLUGIN_API UReplicatedVRCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UReplicatedVRCameraComponent(const FObjectInitializer& ObjectInitializer);


	// If true, this component will not perform logic in its tick, it will instead allow the character movement component to move it (unless the CMC is inactive, then it will go back to self managing)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRExpansionLibrary")
		bool bUpdateInCharacterMovement;

	UPROPERTY()
		TWeakObjectPtr<AVRBaseCharacter> AttachChar;
	void UpdateTracking(float DeltaTime);

	virtual void OnAttachmentChanged() override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	//virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;

	/** Whether or not this component has authority within the frame*/
	bool bHasAuthority;

	/** Whether or not this component is currently on the network server*/
	bool bIsServer;

	FTransform LastRelativePosition;
	bool bHadValidFirstVelocity;

	// If we should sample the velocity in world or local space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|ComponentVelocity")
		bool bSampleVelocityInWorldSpace;

	// For non view target positional updates
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera")
	bool bSetPositionDuringTick;

	// If true will subtract the HMD's location from the position, useful for if the actors base is set to the HMD location always (simple character).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera")
	bool bOffsetByHMD;

	/** Sets lock to hmd automatically based on if the camera is currently locally controlled or not */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera")
		uint32 bAutoSetLockToHmd : 1;

	//UFUNCTION(BlueprintCallable, Category = Camera)
		virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;

	UPROPERTY(EditDefaultsOnly, ReplicatedUsing = OnRep_ReplicatedCameraTransform, Category = "ReplicatedCamera|Networking")
	FBPVRComponentPosRep ReplicatedCameraTransform;

	FVector LastUpdatesRelativePosition;
	FRotator LastUpdatesRelativeRotation;

	bool bLerpingPosition;
	bool bReppedOnce;

	// Whether to smooth (lerp) between ticks for the replicated motion, DOES NOTHING if update rate is larger than FPS!
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "ReplicatedCamera|Networking")
		bool bSmoothReplicatedMotion;
	
	UFUNCTION()
	virtual void OnRep_ReplicatedCameraTransform()
	{
		if (bSmoothReplicatedMotion)
		{
			if (bReppedOnce)
			{
				bLerpingPosition = true;
				NetUpdateCount = 0.0f;
				LastUpdatesRelativePosition = this->GetRelativeLocation();
				LastUpdatesRelativeRotation = this->GetRelativeRotation();
			}
			else
			{
				SetRelativeLocationAndRotation(ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);
				bReppedOnce = true;
			}
		}
		else
			SetRelativeLocationAndRotation(ReplicatedCameraTransform.Position, ReplicatedCameraTransform.Rotation);
	}

	// Rate to update the position to the server, 100htz is default (same as replication rate, should also hit every tick).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "ReplicatedCamera|Networking")
	float NetUpdateRate;

	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case.
	float NetUpdateCount;

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
	void Server_SendCameraTransform(FBPVRComponentPosRep NewTransform);

	// Pointer to an override to call from the owning character - this saves 7 bits a rep avoiding component IDs on the RPC
	typedef void (AVRBaseCharacter::*VRBaseCharTransformRPC_Pointer)(FBPVRComponentPosRep NewTransform);
	VRBaseCharTransformRPC_Pointer OverrideSendTransform;

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	inline bool IsLocallyControlled() const
	{
		// I like epics new authority check more than my own
		const AActor* MyOwner = GetOwner();
		return MyOwner->HasLocalNetOwner();
		//const APawn* MyPawn = Cast<APawn>(MyOwner);
		//return MyPawn ? MyPawn->IsLocallyControlled() : false;// (MyOwner->Role == ENetRole::ROLE_Authority);
	}

	//bool IsServer();
};