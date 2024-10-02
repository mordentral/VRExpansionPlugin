// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
//#include "Net/UnrealNetwork.h"
#include "Camera/CameraComponent.h"
#include "ReplicatedVRCameraComponent.generated.h"

class AVRBaseCharacter;
class AVRCharacter;

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
		TObjectPtr<AVRCharacter> AttachChar;
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

	// If true we will sample relative position for replication instead of the tracked settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera")
		bool bFPSDebugMode = false;

	// For non view target positional updates
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera")
	bool bSetPositionDuringTick;

	// If true will subtract the HMD's location from the position, useful for if the actors base is set to the HMD location always (simple character).
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera")
	//bool bOffsetByHMD;

	// If true will scale the tracking of the camera by TrackingScaler
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking")
		bool bScaleTracking;

	// A scale to be applied to the tracking of the camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking", meta = (ClampMin = "0.1", UIMin = "0.1", EditCondition = "bScaleTracking"))
		FVector TrackingScaler;

	// If true we will use the minimum height value to clamp the Z too
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking")
		bool bLimitMinHeight;

	// The minimum height to allow for this camera
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bLimitMinHeight"))
		float MinimumHeightAllowed;

	// If true will limit the max Z height that the camera is capable of reaching
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking")
		bool bLimitMaxHeight;

	// If we are limiting the max height, this is the maximum allowed value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking", meta = (ClampMin = "0.1", UIMin = "0.1", EditCondition = "bLimitMaxHeight"))
		float MaxHeightAllowed;

	// If true will limit the maximum offset from center of the players tracked space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking")
		bool bLimitBounds;

	// If we are limiting the maximum bounds, this is the maximum length of the vector from the center of the tracked space
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking", meta = (ClampMin = "0.1", UIMin = "0.1", EditCondition = "bLimitBounds"))
		float MaximumTrackedBounds;

	/** Sets lock to hmd automatically based on if the camera is currently locally controlled or not */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Advanced|Tracking")
		uint32 bAutoSetLockToHmd : 1;

	void ApplyTrackingParameters(FVector & OriginalPosition, bool bSkipLocZero = false);
	bool HasTrackingParameters();

	// Get Camera View is no longer required, they finally broke the HMD logic out into its own section!!
	//virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
	virtual void HandleXRCamera(float DeltaTime) override;

	UPROPERTY(EditDefaultsOnly, ReplicatedUsing = OnRep_ReplicatedCameraTransform, Category = "ReplicatedCamera|Networking")
	FBPVRComponentPosRep ReplicatedCameraTransform;

	// Returns the actual tracked transform of the HMD, as with RetainRoomscale = False we do not set the camera to it
	// Can also just use the HMD function library but this is a fast way if you already have a camera reference
	UFUNCTION(BlueprintPure, Category = "ReplicatedCamera|Tracking")
		FTransform GetHMDTrackingTransform();

	FVector LastUpdatesRelativePosition = FVector::ZeroVector;
	FRotator LastUpdatesRelativeRotation = FRotator::ZeroRotator;

	bool bLerpingPosition;
	bool bReppedOnce;

	// Run the smoothing step
	void RunNetworkedSmoothing(float DeltaTime);


	// Whether to smooth (lerp) between ticks for the replicated motion, DOES NOTHING if update rate is larger than FPS!
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ReplicatedCamera|Networking")
		bool bSmoothReplicatedMotion = true;

	// If true then we will use exponential smoothing with buffered correction
	UPROPERTY(EditAnywhere, Category = "ReplicatedCamera|Networking|Smoothing", meta = (editcondition = "bSmoothReplicatedMotion"))
		bool bUseExponentialSmoothing = true;

	// Timestep of smoothing translation
	UPROPERTY(EditAnywhere, Category = "ReplicatedCamera|Networking|Smoothing", meta = (editcondition = "bUseExponentialSmoothing"))
		float InterpolationSpeed = 25.0f;

	// Max distance to allow smoothing before snapping the remainder
	UPROPERTY(EditAnywhere, Category = "ReplicatedCamera|Networking|Smoothing", meta = (editcondition = "bUseExponentialSmoothing"))
		float NetworkMaxSmoothUpdateDistance = 50.f;

	// Max distance to allow smoothing before snapping entirely to the new position
	UPROPERTY(EditAnywhere, Category = "ReplicatedCamera|Networking|Smoothing", meta = (editcondition = "bUseExponentialSmoothing"))
		float NetworkNoSmoothUpdateDistance = 100.f;
	
	UFUNCTION()
    virtual void OnRep_ReplicatedCameraTransform();

protected:
	// Rate to update the position to the server, 100htz is default (same as replication rate, should also hit every tick).
		// On dedicated servers the update rate should be at or lower than the server tick rate for smoothing to work
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "ReplicatedCamera|Networking")
	float NetUpdateRate;
public:

	// Used in Tick() to accumulate before sending updates, didn't want to use a timer in this case.
	float NetUpdateCount;

	float GetNetUpdateRate() { return NetUpdateRate; }
	void SetNetUpdateRate(float NewNetUpdateRate);

	// I'm sending it unreliable because it is being resent pretty often
	UFUNCTION(Unreliable, Server, WithValidation)
	void Server_SendCameraTransform(FBPVRComponentPosRep NewTransform);

	// Pointer to an override to call from the owning character - this saves 7 bits a rep avoiding component IDs on the RPC
	typedef void (AVRBaseCharacter::*VRBaseCharTransformRPC_Pointer)(FBPVRComponentPosRep NewTransform);
	VRBaseCharTransformRPC_Pointer OverrideSendTransform;

	// Need this as I can't think of another way for an actor component to make sure it isn't on the server
	bool IsLocallyControlled() const;

	//bool IsServer();
};