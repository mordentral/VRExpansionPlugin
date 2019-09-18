// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "VRBaseCharacter.h"
#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "ReplicatedVRCameraComponent.h"
#include "ParentRelativeAttachmentComponent.h"
#include "VRRootComponent.h"
#include "VRCharacterMovementComponent.h"
#include "Runtime/Launch/Resources/Version.h"
#include "VRCharacter.generated.h"


DECLARE_LOG_CATEGORY_EXTERN(LogVRCharacter, Log, All);

UCLASS()
class VREXPANSIONPLUGIN_API AVRCharacter : public AVRBaseCharacter
{
	GENERATED_BODY()

public:
	AVRCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Overriding teleport so that it auto calls my controllers re-positioning
	virtual bool TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest = false, bool bNoCheck = false) override;

	UPROPERTY(Category = VRCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))	
	UVRRootComponent * VRRootReference;

	// Regenerates the base offsetcomponenttoworld that VR uses
	virtual void RegenerateOffsetComponentToWorld(bool bUpdateBounds, bool bCalculatePureYaw) override;
	virtual void SetCharacterSizeVR(float NewRadius, float NewHalfHeight, bool bUpdateOverlaps = true) override;
	virtual void SetCharacterHalfHeightVR(float HalfHeight, bool bUpdateOverlaps = true) override;

	/* 
	A helper function that offsets a given vector by the roots collision location
	pass in a teleport location and it provides the correct spot for it to be at your feet
	*/
	virtual FVector GetTeleportLocation(FVector OriginalLocation) override;
	
	
	// Overriding to correct some nav stuff
	FVector GetNavAgentLocation() const override;

	// An extended simple move to location with additional parameters
		virtual void ExtendedSimpleMoveToLocation(const FVector& GoalLocation, float AcceptanceRadius = -1, bool bStopOnOverlap = false,
			bool bUsePathfinding = true, bool bProjectDestinationToNavigation = true, bool bCanStrafe = false,
			TSubclassOf<UNavigationQueryFilter> FilterClass = NULL, bool bAllowPartialPath = true) override;

	//////////////////////////////////////////////////////////////////////////
	// Server RPCs that pass through to CharacterMovement (avoids RPC overhead for components).
	// The base RPC function (eg 'ServerMove') is auto-generated for clients to trigger the call to the server function,
	// eventually going to the _Implementation function (which we just pass to the CharacterMovementComponent).
	//////////////////////////////////////////////////////////////////////////

	/** Replicate position correction to client, associated with a timestamped servermove.  Client will replay subsequent moves after applying adjustment.  */
	UFUNCTION(unreliable, client)
		void ClientAdjustPositionVR(float TimeStamp, FVector NewLoc, uint16 NewYaw, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	void ClientAdjustPositionVR_Implementation(float TimeStamp, FVector NewLoc, uint16 NewYaw, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);

	/* Bandwidth saving version, when velocity is zeroed */
	UFUNCTION(unreliable, client)
		void ClientVeryShortAdjustPositionVR(float TimeStamp, FVector NewLoc, uint16 NewYaw, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);
	void ClientVeryShortAdjustPositionVR_Implementation(float TimeStamp, FVector NewLoc, uint16 NewYaw, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode);


	/** Replicated function sent by client to server - contains client movement and view info. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVR(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVR_Implementation(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVR_Validate(float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info. ExLight version is used if there was no requested velocity or customVRInputVector or Accell*/
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRExLight(float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRExLight_Implementation(float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRExLight_Validate(float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 CompressedMoveFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);


	/** Replicated function sent by client to server - contains client movement and view info for two moves. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDual(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRDual_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDual_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);

	/** Replicated function sent by client to server - contains client movement and view info for two moves. ExLight version is used if there was no requested velocity or customVRInputVector or Accell */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDualExLight(float TimeStamp0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRDualExLight_Implementation(float TimeStamp0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDualExLight_Validate(float TimeStamp0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);


	/** Replicated function sent by client to server - contains client movement and view info for two moves. First move is non root motion, second is root motion. */
	UFUNCTION(unreliable, server, WithValidation)
	virtual void ServerMoveVRDualHybridRootMotion(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual void ServerMoveVRDualHybridRootMotion_Implementation(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);
	virtual bool ServerMoveVRDualHybridRootMotion_Validate(float TimeStamp0, FVector_NetQuantize10 InAccel0, uint8 PendingFlags, uint32 View0, FVector_NetQuantize100 OldCapsuleLoc, FVRConditionalMoveRep OldConditionalReps, FVector_NetQuantize100 OldLFDiff, uint16 OldCapsuleYaw, float TimeStamp, FVector_NetQuantize10 InAccel, FVector_NetQuantize100 ClientLoc, FVector_NetQuantize100 CapsuleLoc, FVRConditionalMoveRep ConditionalReps, FVector_NetQuantize100 LFDiff, uint16 CapsuleYaw, uint8 NewFlags, FVRConditionalMoveRep2 MoveReps, uint8 ClientMovementMode);

	/* Resending an (important) old move. Process it if not already processed. */
	UFUNCTION(unreliable, server, WithValidation)
	void ServerMoveVROld(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags, FVRConditionalMoveRep ConditionalReps);
	void ServerMoveVROld_Implementation(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags, FVRConditionalMoveRep ConditionalReps);
	bool ServerMoveVROld_Validate(float OldTimeStamp, FVector_NetQuantize10 OldAccel, uint8 OldMoveFlags, FVRConditionalMoveRep ConditionalReps);
};