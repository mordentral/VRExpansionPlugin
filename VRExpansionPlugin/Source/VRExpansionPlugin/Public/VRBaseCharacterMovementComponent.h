// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRBPDatatypes.h"
#include "AITypes.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "VRBaseCharacterMovementComponent.generated.h"

class AVRBaseCharacter;

DECLARE_LOG_CATEGORY_EXTERN(LogVRBaseCharacterMovement, Log, All);

/** Delegate for notification when to handle a climbing step up, will override default step up logic if is bound to. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVROnPerformClimbingStepUp, FVector, FinalStepUpLocation);

/** Shared pointer for easy memory management of FSavedMove_Character, for accumulating and replaying network moves. */
//typedef TSharedPtr<class FSavedMove_Character> FSavedMovePtr;


//=============================================================================
/**
 * VRSimpleCharacterMovementComponent handles movement logic for the associated Character owner.
 * It supports various movement modes including: walking, falling, swimming, flying, custom.
 *
 * Movement is affected primarily by current Velocity and Acceleration. Acceleration is updated each frame
 * based on the input vector accumulated thus far (see UPawnMovementComponent::GetPendingInputVector()).
 *
 * Networking is fully implemented, with server-client correction and prediction included.
 *
 * @see ACharacter, UPawnMovementComponent
 * @see https://docs.unrealengine.com/latest/INT/Gameplay/Framework/Pawn/Character/
 */

UENUM(Blueprintable)
enum class EVRMoveAction : uint8
{
	VRMOVEACTION_None = 0x00,
	VRMOVEACTION_SnapTurn = 0x01,
	VRMOVEACTION_Teleport = 0x02,
	VRMOVEACTION_StopAllMovement = 0x03,
	VRMOVEACTION_SetRotation = 0x04,
	VRMOVEACTION_CUSTOM1 = 0x05,
	VRMOVEACTION_CUSTOM2 = 0x06,
	VRMOVEACTION_CUSTOM3 = 0x07,
	VRMOVEACTION_CUSTOM4 = 0x08,
	VRMOVEACTION_CUSTOM5 = 0x09,
	VRMOVEACTION_CUSTOM6 = 0x0A,
	VRMOVEACTION_CUSTOM7 = 0x0B,
	VRMOVEACTION_CUSTOM8 = 0x0C,
	VRMOVEACTION_CUSTOM9 = 0x0D,
	VRMOVEACTION_CUSTOM10 = 0x0E,
};

// What to do with the players velocity when specific move actions are called
// Default of none leaves it as is, for people with 0 ramp up time on acelleration
// This likely won't be that useful.
UENUM(Blueprintable)
enum class EVRMoveActionVelocityRetention : uint8
{
	// Leaves velocity as is
	VRMOVEACTION_Velocity_None = 0x00,

	// Clears velocity entirely
	VRMOVEACTION_Velocity_Clear = 0x01,

	// Rotates the velocity to match new heading
	VRMOVEACTION_Velocity_Turn = 0x02
};

UENUM(Blueprintable)
enum class EVRMoveActionDataReq : uint8
{
	VRMOVEACTIONDATA_None = 0x00,
	VRMOVEACTIONDATA_LOC = 0x01,
	VRMOVEACTIONDATA_ROT = 0x02,
	VRMOVEACTIONDATA_LOC_AND_ROT = 0x03
};



USTRUCT()
struct VREXPANSIONPLUGIN_API FVRMoveActionContainer
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY()
	EVRMoveAction MoveAction;
	UPROPERTY()
	EVRMoveActionDataReq MoveActionDataReq;
	UPROPERTY()
	FVector MoveActionLoc;
	UPROPERTY()
	FRotator MoveActionRot;
	UPROPERTY()
	EVRMoveActionVelocityRetention VelRetentionSetting;

	FVRMoveActionContainer()
	{
		Clear();
	}

	void Clear()
	{
		MoveAction = EVRMoveAction::VRMOVEACTION_None;
		MoveActionDataReq = EVRMoveActionDataReq::VRMOVEACTIONDATA_None;
		MoveActionLoc = FVector::ZeroVector;
		MoveActionRot = FRotator::ZeroRotator;
		VelRetentionSetting = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None;
	}

	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;
		
		Ar.SerializeBits(&MoveAction, 4); // 16 elements, only allowing 1 per frame, they aren't flags

		switch (MoveAction)
		{
		case EVRMoveAction::VRMOVEACTION_None: break;
		case EVRMoveAction::VRMOVEACTION_SetRotation:
		case EVRMoveAction::VRMOVEACTION_SnapTurn:
		{
			uint16 Yaw = 0;
			uint16 Pitch = 0;
			
			if (Ar.IsSaving())
			{
				Yaw = FRotator::CompressAxisToShort(MoveActionRot.Yaw);
				Ar << Yaw;

				bool bTeleportGrips = MoveActionRot.Roll > 0.0f;
				Ar.SerializeBits(&bTeleportGrips, 1);

				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					Pitch = FRotator::CompressAxisToShort(MoveActionRot.Pitch);
					Ar << Pitch;
				}
			}
			else
			{
				Ar << Yaw;
				MoveActionRot.Yaw = FRotator::DecompressAxisFromShort(Yaw);



				bool bTeleportGrips = false;
				Ar.SerializeBits(&bTeleportGrips, 1);
				MoveActionRot.Roll = bTeleportGrips ? 1.0f : 0.0f;

				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					Ar << Pitch;
					MoveActionRot.Pitch = FRotator::DecompressAxisFromShort(Pitch);
				}
			}

			//bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);
		}break;
		case EVRMoveAction::VRMOVEACTION_Teleport: // Not replicating rot as Control rot does that already
		{
			uint16 Yaw = 0;
			uint16 Pitch = 0;

			if (Ar.IsSaving())
			{
				Yaw = FRotator::CompressAxisToShort(MoveActionRot.Yaw);
				Ar << Yaw;

				bool bSkipEncroachment = MoveActionRot.Roll > 0.0f;
				Ar.SerializeBits(&bSkipEncroachment, 1);
				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					Pitch = FRotator::CompressAxisToShort(MoveActionRot.Pitch);
					Ar << Pitch;
				}
			}
			else
			{
				Ar << Yaw;
				MoveActionRot.Yaw = FRotator::DecompressAxisFromShort(Yaw);

				bool bSkipEncroachment = false;
				Ar.SerializeBits(&bSkipEncroachment, 1);
				MoveActionRot.Roll = bSkipEncroachment ? 1.0f : 0.0f;
				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					Ar << Pitch;
					MoveActionRot.Pitch = FRotator::DecompressAxisFromShort(Pitch);
				}
			}

			bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);
		}break;
		case EVRMoveAction::VRMOVEACTION_StopAllMovement:
		{}break;
		default: // Everything else
		{
			// Defines how much to replicate - only 4 possible values, 0 - 3 so only send 2 bits
			Ar.SerializeBits(&MoveActionDataReq, 2);

			if (((uint8)MoveActionDataReq & (uint8)EVRMoveActionDataReq::VRMOVEACTIONDATA_LOC) != 0)
				bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);

			if (((uint8)MoveActionDataReq & (uint8)EVRMoveActionDataReq::VRMOVEACTIONDATA_ROT) != 0)
				MoveActionRot.SerializeCompressedShort(Ar);

		}break;
		}

		return bOutSuccess;
	}
};
template<>
struct TStructOpsTypeTraits< FVRMoveActionContainer > : public TStructOpsTypeTraitsBase2<FVRMoveActionContainer>
{
	enum
	{
		WithNetSerializer = true
	};
};

USTRUCT()
struct VREXPANSIONPLUGIN_API FVRMoveActionArray
{
	GENERATED_USTRUCT_BODY()
public:
	UPROPERTY()
		TArray<FVRMoveActionContainer> MoveActions;

	void Clear()
	{
		MoveActions.Empty();
	}
	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;
		uint8 MoveActionCount = (uint8)MoveActions.Num();
		bool bHasAMoveAction = MoveActionCount > 0;
		Ar.SerializeBits(&bHasAMoveAction, 1);

		if (bHasAMoveAction)
		{
			bool bHasMoreThanOneMoveAction = MoveActionCount > 1;
			Ar.SerializeBits(&bHasMoreThanOneMoveAction, 1);

			if (Ar.IsSaving())
			{
				if (bHasMoreThanOneMoveAction)
				{
					Ar << MoveActionCount;

					for (int i = 0; i < MoveActionCount; i++)
					{
						bOutSuccess &= MoveActions[i].NetSerialize(Ar, Map, bOutSuccess);
					}
				}
				else
				{
					bOutSuccess &= MoveActions[0].NetSerialize(Ar, Map, bOutSuccess);
				}
			}
			else
			{
				if (bHasMoreThanOneMoveAction)
				{
					Ar << MoveActionCount;
				}
				else
					MoveActionCount = 1;

				for (int i = 0; i < MoveActionCount; i++)
				{
					FVRMoveActionContainer MoveAction;
					bOutSuccess &= MoveAction.NetSerialize(Ar, Map, bOutSuccess);
					MoveActions.Add(MoveAction);
				}
			}
		}

		return bOutSuccess;
	}
};
template<>
struct TStructOpsTypeTraits< FVRMoveActionArray > : public TStructOpsTypeTraitsBase2<FVRMoveActionArray>
{
	enum
	{
		WithNetSerializer = true
	};
};


USTRUCT()
struct VREXPANSIONPLUGIN_API FVRConditionalMoveRep
{
	GENERATED_USTRUCT_BODY()
public:

	UPROPERTY(Transient)
		FVector CustomVRInputVector;
	UPROPERTY(Transient)
		FVector RequestedVelocity;
	UPROPERTY(Transient)
		FVRMoveActionArray MoveActionArray;
		//FVRMoveActionContainer MoveAction;

	FVRConditionalMoveRep()
	{
		CustomVRInputVector = FVector::ZeroVector;
		RequestedVelocity = FVector::ZeroVector;
	}

	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		bool bHasVRinput = !CustomVRInputVector.IsZero();
		bool bHasRequestedVelocity = !RequestedVelocity.IsZero();
		bool bHasMoveAction = MoveActionArray.MoveActions.Num() > 0;//MoveAction.MoveAction != EVRMoveAction::VRMOVEACTION_None;

		bool bHasAnyProperties = bHasVRinput || bHasRequestedVelocity || bHasMoveAction;
		Ar.SerializeBits(&bHasAnyProperties, 1);

		if (bHasAnyProperties)
		{
			Ar.SerializeBits(&bHasVRinput, 1);
			Ar.SerializeBits(&bHasRequestedVelocity, 1);
			//Ar.SerializeBits(&bHasMoveAction, 1);

			if (bHasVRinput)
				bOutSuccess &= SerializePackedVector<100, 22/*30*/>(CustomVRInputVector, Ar);

			if (bHasRequestedVelocity)
				bOutSuccess &= SerializePackedVector<100, 22/*30*/>(RequestedVelocity, Ar);

			//if (bHasMoveAction)
			MoveActionArray.NetSerialize(Ar, Map, bOutSuccess);
		}

		return bOutSuccess;
	}

};

template<>
struct TStructOpsTypeTraits< FVRConditionalMoveRep > : public TStructOpsTypeTraitsBase2<FVRConditionalMoveRep>
{
	enum
	{
		WithNetSerializer = true
	};
};

USTRUCT()
struct VREXPANSIONPLUGIN_API FVRConditionalMoveRep2
{
	GENERATED_USTRUCT_BODY()
public:

	// Moved these here to avoid having to duplicate tons of properties
	UPROPERTY(Transient)
		UPrimitiveComponent* ClientMovementBase;
	UPROPERTY(Transient)
		FName ClientBaseBoneName;

	UPROPERTY(Transient)
	uint16 ClientYaw;

	UPROPERTY(Transient)
	uint16 ClientPitch;

	UPROPERTY(Transient)
	uint8 ClientRoll;

	FVRConditionalMoveRep2()
	{
		ClientMovementBase = nullptr;
		ClientBaseBoneName = NAME_None;
		ClientRoll = 0;
		ClientPitch = 0;
		ClientYaw = 0;
	}

	void UnpackAndSetINTRotations(uint32 Rotation32)
	{
		// Reversed the order of these so it costs less to replicate
		ClientYaw = (Rotation32 & 65535);
		ClientPitch = (Rotation32 >> 16);
	}

	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		bool bRepRollAndPitch = (ClientRoll != 0 || ClientPitch != 0);
		Ar.SerializeBits(&bRepRollAndPitch, 1);

		if (bRepRollAndPitch)
		{
			// Reversed the order of these
			uint32 Rotation32 = (((uint32)ClientPitch) << 16) | ((uint32)ClientYaw);
			Ar.SerializeIntPacked(Rotation32);
			Ar << ClientRoll;

			if (Ar.IsLoading())
			{
				UnpackAndSetINTRotations(Rotation32);
			}
		}
		else
		{
			uint32 Yaw32 = ClientYaw;
			Ar.SerializeIntPacked(Yaw32);
			ClientYaw = (uint16)Yaw32;
		}

		bool bHasMovementBase = MovementBaseUtility::IsDynamicBase(ClientMovementBase);
		Ar.SerializeBits(&bHasMovementBase, 1);

		if (bHasMovementBase)
		{
			Ar << ClientMovementBase;

			bool bValidName = ClientBaseBoneName != NAME_None;
			Ar.SerializeBits(&bValidName, 1);

			// This saves 9 bits on average, we almost never have a valid bone name and default rep goes to 9 bits for hardcoded
			// total of 6 bits savings as we use 3 extra for our flags in here.
			if (bValidName)
			{
				Ar << ClientBaseBoneName;
			}
		}

		return bOutSuccess;
	}

};

template<>
struct TStructOpsTypeTraits< FVRConditionalMoveRep2 > : public TStructOpsTypeTraitsBase2<FVRConditionalMoveRep2>
{
	enum
	{
		WithNetSerializer = true
	};
};

/**
* Helper to change mesh bone updates within a scope.
* Example usage:
*	{
*		FScopedPreventMeshBoneUpdate ScopedNoMeshBoneUpdate(CharacterOwner->GetMesh(), EKinematicBonesUpdateToPhysics::SkipAllBones);
*		// Do something to move mesh, bones will not update
*	}
*	// Movement of mesh at this point will use previous setting.
*/
struct FScopedMeshBoneUpdateOverrideVR
{
	FScopedMeshBoneUpdateOverrideVR(USkeletalMeshComponent* Mesh, EKinematicBonesUpdateToPhysics::Type OverrideSetting)
		: MeshRef(Mesh)
	{
		if (MeshRef)
		{
			// Save current state.
			SavedUpdateSetting = MeshRef->KinematicBonesUpdateType;
			// Override bone update setting.
			MeshRef->KinematicBonesUpdateType = OverrideSetting;
		}
	}

	~FScopedMeshBoneUpdateOverrideVR()
	{
		if (MeshRef)
		{
			// Restore bone update flag.
			MeshRef->KinematicBonesUpdateType = SavedUpdateSetting;
		}
	}

private:
	USkeletalMeshComponent * MeshRef;
	EKinematicBonesUpdateToPhysics::Type SavedUpdateSetting;
};


class VREXPANSIONPLUGIN_API FSavedMove_VRBaseCharacter : public FSavedMove_Character
{

public:

	EVRConjoinedMovementModes VRReplicatedMovementMode;

	FVector VRCapsuleLocation;
	FVector LFDiff;
	FRotator VRCapsuleRotation;
	FVRConditionalMoveRep ConditionalValues;

	void Clear();
	virtual void SetInitialPosition(ACharacter* C);

	FSavedMove_VRBaseCharacter() : FSavedMove_Character()
	{
		VRCapsuleLocation = FVector::ZeroVector;
		LFDiff = FVector::ZeroVector;
		VRCapsuleRotation = FRotator::ZeroRotator;
		VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;// _None;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		// Fills in 01 and 02 for Jump / Crouch
		uint8 Result = FSavedMove_Character::GetCompressedFlags();

		// Not supporting custom movement mode directly at this time by replicating custom index
		// We use 4 bits for this so a maximum of 16 elements
		Result |= (uint8)VRReplicatedMovementMode << 2;

		// This takes up custom_2
		/*if (bWantsToSnapTurn)
		{
			Result |= FLAG_SnapTurn;
		}*/

		// Reserved_1, and Reserved_2, Flag_Custom_0 and Flag_Custom_1 are used up
		// By the VRReplicatedMovementMode packing


		// only custom_2 and custom_3 are left currently
		return Result;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		FSavedMove_VRBaseCharacter * nMove = (FSavedMove_VRBaseCharacter *)NewMove.Get();


		if (!nMove || (VRReplicatedMovementMode != nMove->VRReplicatedMovementMode))
			return false;

		if (ConditionalValues.MoveActionArray.MoveActions.Num() > 0 || nMove->ConditionalValues.MoveActionArray.MoveActions.Num() > 0)
			return false;

		if (!ConditionalValues.CustomVRInputVector.IsZero() || !nMove->ConditionalValues.CustomVRInputVector.IsZero())
			return false;

		if (!ConditionalValues.RequestedVelocity.IsZero() || !nMove->ConditionalValues.RequestedVelocity.IsZero())
			return false;

		// Hate this but we really can't combine if I am sending a new capsule height
		if (!FMath::IsNearlyEqual(LFDiff.Z, nMove->LFDiff.Z))
			return false;

		if (!LFDiff.IsZero() && !nMove->LFDiff.IsZero() && !FVector::Coincident(LFDiff.GetSafeNormal2D(), nMove->LFDiff.GetSafeNormal2D(), AccelDotThresholdCombine))
			return false;

		return FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}


	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override
	{
		// Auto important if toggled climbing
		if (VRReplicatedMovementMode != EVRConjoinedMovementModes::C_MOVE_MAX)//_None)
			return true;

		if (!ConditionalValues.CustomVRInputVector.IsZero())	
			return true;

		if (!ConditionalValues.RequestedVelocity.IsZero())
			return true;

		if (ConditionalValues.MoveActionArray.MoveActions.Num() > 0)
			return true;

		// #TODO: What to do here?
		// This is debatable, however it will ALWAYS be non zero realistically and only really effects step ups for the most part
		//if (!LFDiff.IsNearlyZero())
		//return true;

		// Else check parent class
		return FSavedMove_Character::IsImportantMove(LastAckedMove);
	}

	virtual void PrepMoveFor(ACharacter* Character) override;
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;

	/** Set the properties describing the final position, etc. of the moved pawn. */
	virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode) override;
};

// Using this fixes the problem where the character capsule isn't reset after a scoped movement update revert (pretty much just in StepUp operations)
class VREXPANSIONPLUGIN_API FVRCharacterScopedMovementUpdate : public FScopedMovementUpdate
{
public:

	FVRCharacterScopedMovementUpdate(USceneComponent* Component, EScopedUpdate::Type ScopeBehavior = EScopedUpdate::DeferredUpdates, bool bRequireOverlapsEventFlagToQueueOverlaps = true);

	FTransform InitialVRTransform;

	/** Revert movement to the initial location of the Component at the start of the scoped update. Also clears pending overlaps and sets bHasMoved to false. */
	void RevertMove();
};

UCLASS()
class VREXPANSIONPLUGIN_API UVRBaseCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:

	UVRBaseCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	bool bNotifyTeleported;

	/** BaseVR Character movement component belongs to */
	UPROPERTY(Transient, DuplicateTransient)
		AVRBaseCharacter* BaseVRCharacterOwner;

	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent);

	virtual void PerformMovement(float DeltaSeconds) override;
	//virtual void ReplicateMoveToServer(float DeltaTime, const FVector& NewAcceleration) override;

	// Overriding this to run the seated logic
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;

	// Skip force updating position if we are seated.
	virtual bool ForcePositionUpdate(float DeltaTime) override;

	// Adding seated transition
	void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	// Called when a valid climbing step up movement is found, if bound to the default auto step up is not performed to let custom step up logic happen instead.
	UPROPERTY(BlueprintAssignable, Category = "VRMovement")
		FVROnPerformClimbingStepUp OnPerformClimbingStepUp;

	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result);

	// Can't be inline anymore
	FVector GetActorFeetLocationVR() const;

	FORCEINLINE bool HasRequestedVelocity()
	{
		return bHasRequestedVelocity;
	}

	void SetHasRequestedVelocity(bool bNewHasRequestedVelocity);
	bool IsClimbing() const;

	// Sets the crouching half height since it isn't exposed during runtime to blueprints
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void SetCrouchedHalfHeight(float NewCrouchedHalfHeight);

	// Setting this higher will divide the wall slide effect by this value, to reduce collision sliding.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement", meta = (ClampMin = "0.0", UIMin = "0", ClampMax = "5.0", UIMax = "5"))
		float VRWallSlideScaler;

	/** Custom version of SlideAlongSurface that handles different movement modes separately; namely during walking physics we might not want to slide up slopes. */
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact) override;

	// Add in the custom replicated movement that climbing mode uses, this is a cutom vector that is applied to character movements
	// on the next tick as a movement input..
	UFUNCTION(BlueprintCallable, Category = "BaseVRCharacterMovementComponent|VRLocations")
		void AddCustomReplicatedMovement(FVector Movement);

	// Called to check if the server is performing a move action on a non controlled character
	// If so then we just run the logic right away as it can't be inlined and won't be replicated
	void CheckServerAuthedMoveAction();

	// Perform a snap turn in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_SnapTurn(float SnapTurnDeltaYaw, EVRMoveActionVelocityRetention VelocityRetention = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None, bool bFlagGripTeleport = false);

	// Perform a rotation set in line with the move actions system
	// This node specifically sets the FACING direction to a value, where your HMD is pointed
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_SetRotation(float NewYaw, EVRMoveActionVelocityRetention VelocityRetention = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None, bool bFlagGripTeleport = false);

	// Perform a teleport in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_Teleport(FVector TeleportLocation, FRotator TeleportRotation, EVRMoveActionVelocityRetention VelocityRetention = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None, bool bSkipEncroachmentCheck = false);

	// Perform StopAllMovementImmediately in line with the move action system
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_StopAllMovement();
	
	// Perform a custom moveaction that you define, will call the OnCustomMoveActionPerformed event in the character when processed so you can run your own logic
	// Be sure to set the minimum data replication requirements for your move action in order to save on replication.
	// Move actions are currently limited to 1 per frame.
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void PerformMoveAction_Custom(EVRMoveAction MoveActionToPerform, EVRMoveActionDataReq DataRequirementsForMoveAction, FVector MoveActionVector, FRotator MoveActionRotator);

	FVRMoveActionArray MoveActionArray;

	bool CheckForMoveAction();
	bool DoMASnapTurn(FVRMoveActionContainer& MoveAction);
	bool DoMASetRotation(FVRMoveActionContainer& MoveAction);
	bool DoMATeleport(FVRMoveActionContainer& MoveAction);
	bool DoMAStopAllMovement(FVRMoveActionContainer& MoveAction);

	FVector CustomVRInputVector;
	FVector AdditionalVRInputVector;
	FVector LastPreAdditiveVRVelocity;
	bool bHadExtremeInput;
	bool bApplyAdditionalVRInputVectorAsNegative;
	
	// Rewind the relative movement that we had with the HMD
	inline void RewindVRRelativeMovement()
	{
		if (bApplyAdditionalVRInputVectorAsNegative)
		{
			//FHitResult AHit;
			MoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false);
			//SafeMoveUpdatedComponent(-AdditionalVRInputVector, UpdatedComponent->GetComponentQuat(), false, AHit);
		}
	}

	// Any movement above this value we will consider as have been a tracking jump and null out the movement in the character
	// Raise this value higher if players are noticing freezing when moving quickly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement", meta = (ClampMin = "0.0", UIMin = "0"))
		float TrackingLossThreshold;

	// If we hit the tracking loss threshold then rewind position instead of running to the new location
	// Will force the HMD to stay in its original spot prior to the tracking jump
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool bHoldPositionOnTrackingLossThresholdHit;

	// Rewind the relative movement that we had with the HMD, this is exposed to Blueprint so that custom movement modes can use it to rewind prior to movement actions.
	// Returns the Vector required to get back to the original position (for custom movement modes)
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		FVector RewindVRMovement();

	// Gets the current CustomInputVector for use in custom movement modes
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		FVector GetCustomInputVector();

	bool bWasInPushBack;
	bool bIsInPushBack;
	void StartPushBackNotification(FHitResult HitResult);
	void EndPushBackNotification();

	bool bJustUnseated;

	//virtual void SendClientAdjustment() override;

	virtual bool VerifyClientTimeStamp(float TimeStamp, FNetworkPredictionData_Server_Character & ServerData) override;

	inline void ApplyVRMotionToVelocity(float deltaTime)
	{
		bHadExtremeInput = false;

		if (AdditionalVRInputVector.IsNearlyZero() && CustomVRInputVector.IsNearlyZero())
		{
			LastPreAdditiveVRVelocity = FVector::ZeroVector;
			return;
		}

		LastPreAdditiveVRVelocity = (AdditionalVRInputVector / deltaTime); // Save off pre-additive Velocity for restoration next tick	

		if (LastPreAdditiveVRVelocity.SizeSquared() > FMath::Square(TrackingLossThreshold))
		{
			bHadExtremeInput = true;
			if (bHoldPositionOnTrackingLossThresholdHit)
			{
				LastPreAdditiveVRVelocity = FVector::ZeroVector;
			}
		}

		// Post the HMD velocity checks, add in our direct movement now
		LastPreAdditiveVRVelocity += (CustomVRInputVector / deltaTime);

		Velocity += LastPreAdditiveVRVelocity;
	}

	inline void RestorePreAdditiveVRMotionVelocity()
	{
		if (!LastPreAdditiveVRVelocity.IsNearlyZero())
		{
			if (bHadExtremeInput)
			{
				// Just zero out the velocity here
				Velocity = FVector::ZeroVector;
			}
			else
			{
				// This doesn't work with input in the opposing direction
				/*FVector ProjectedVelocity = Velocity.ProjectOnToNormal(LastPreAdditiveVRVelocity.GetSafeNormal());
				float VelSq = ProjectedVelocity.SizeSquared();
				float AddSq = LastPreAdditiveVRVelocity.SizeSquared();

				if (VelSq > AddSq || ProjectedVelocity.Equals(LastPreAdditiveVRVelocity, 0.1f))
				{
					// Subtract velocity if we still relatively retain it in the normalized direction
					Velocity -= LastPreAdditiveVRVelocity;
				}*/

				Velocity -= LastPreAdditiveVRVelocity;
			}
		}

		LastPreAdditiveVRVelocity = FVector::ZeroVector;
	}

	virtual void PhysCustom(float deltaTime, int32 Iterations) override;
	virtual void PhysCustom_Climbing(float deltaTime, int32 Iterations);
	virtual void PhysCustom_LowGrav(float deltaTime, int32 Iterations);

	// Teleport grips on correction to fixup issues
	virtual void OnClientCorrectionReceived(class FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;

	// Fix network smoothing with our default mesh back in
	virtual void SimulatedTick(float DeltaSeconds) override;

	// Skip updates with rotational differences
	virtual void SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation) override;

	/**
	* Smooth mesh location for network interpolation, based on values set up by SmoothCorrection.
	* Internally this simply calls SmoothClientPosition_Interpolate() then SmoothClientPosition_UpdateVisuals().
	* This function is not called when bNetworkSmoothingComplete is true.
	* @param DeltaSeconds Time since last update.
	*/
	virtual void SmoothClientPosition(float DeltaSeconds) override;

	/** Update mesh location based on interpolated values. */
	void SmoothClientPosition_UpdateVRVisuals();

	// Added in 4.16
	///* Allow custom handling when character hits a wall while swimming. */
	//virtual void HandleSwimmingWallHit(const FHitResult& Hit, float DeltaTime);

	// If true will never count a physicsbody channel component as the floor, to prevent jitter / physics problems.
	// Make sure that you set simulating objects to the physics body channel if you want this to work correctly
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool bIgnoreSimulatingComponentsInFloorCheck;

	// If true will run the control rotation in the CMC instead of in the player controller
	// This puts the player rotation into the scoped movement (perf savings) and also ensures it is properly rotated prior to movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		bool bRunControlRotationInMovementComponent;

	// Moved into compute floor dist
	// Option to Skip simulating components when looking for floor
	/*virtual bool FloorSweepTest(
		const FVector& Start,
		FHitResult& OutHit,
		const FVector& End,
		ECollisionChannel TraceChannel,
		const struct FCollisionShape& CollisionShape,
		const struct FCollisionQueryParams& Params,
		const struct FCollisionResponseParams& ResponseParam
	) const override;*/

	virtual void ComputeFloorDist(const FVector& CapsuleLocation, float LineDistance, float SweepDistance, FFindFloorResult& OutFloorResult, float SweepRadius, const FHitResult* DownwardSweepResult = NULL) const override;

	// Need to use actual capsule location for step up
	virtual bool VRClimbStepUp(const FVector& GravDir, const FVector& Delta, const FHitResult &InHit, FStepDownResult* OutStepDownResult = nullptr);

	// Height to auto step up
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepHeight;

	/* Custom distance that is required before accepting a climbing stepup
	*  This is to help with cases where head wobble causes falling backwards
	*  Do NOT set to larger than capsule radius!
	*  #TODO: Port to SimpleCharacter as well
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingEdgeRejectDistance;

	// Higher values make it easier to trigger a step up onto a platform and moves you farther in to the base *DEFUNCT*
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepUpMultiplier;

	// If true will clamp the maximum movement on climbing step up to: VRClimbingStepUpMaxSize
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		bool bClampClimbingStepUp;

	// Maximum X/Y vector size to use when climbing stepping up (prevents very deep step ups from large movements).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingStepUpMaxSize;

	// If true will automatically set falling when a stepup occurs during climbing
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		bool SetDefaultPostClimbMovementOnStepUp;

	// Max velocity on releasing a climbing grip
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		float VRClimbingMaxReleaseVelocitySize;

	/* Custom distance that is required before accepting a walking stepup
	*  This is to help promote stepping up, engine default is 0.15f, generally you want it lower than that
	*  Do NOT set to larger than capsule radius!
	*  #TODO: Port to SimpleCharacter as well
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement")
		float VREdgeRejectDistance;

	UFUNCTION(BlueprintCallable, Category = "VRMovement|Climbing")
		void SetClimbingMode(bool bIsClimbing);

	// Default movement mode to switch to post climb ended, only used if SetDefaultPostClimbMovementOnStepUp is true
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|Climbing")
		EVRConjoinedMovementModes DefaultPostClimbMovement;

	// Overloading this to handle an edge case
	virtual void ApplyNetworkMovementMode(const uint8 ReceivedMode) override;

	/*
	* This is called client side to make a replicated movement mode change that hits the server in the saved move.
	*
	* Custom Movement Mode is currently limited to 0 - 8, the index's 0 and 1 are currently used up for the plugin movement modes.
	* So setting it to 0 or 1 would be Climbing, and LowGrav respectivly, this leaves 2-8 as open index's for use.
	* For a total of 6 Custom movement modes past the currently implemented plugin ones.
	*/
	UFUNCTION(BlueprintCallable, Category = "VRMovement")
		void SetReplicatedMovementMode(EVRConjoinedMovementModes NewMovementMode);

	/*
	* Call this to convert the current movement mode to a Conjoined one for reference
	*
	* Custom Movement Mode is currently limited to 0 - 8, the index's 0 and 1 are currently used up for the plugin movement modes.
	* So setting it to 0 or 1 would be Climbing, and LowGrav respectivly, this leaves 2-8 as open index's for use.
	* For a total of 6 Custom movement modes past the currently implemented plugin ones.
	*/
	UFUNCTION(BlueprintPure, Category = "VRMovement")
		EVRConjoinedMovementModes GetReplicatedMovementMode();

	// We use 4 bits for this so a maximum of 16 elements
	EVRConjoinedMovementModes VRReplicatedMovementMode;

	FORCEINLINE void ApplyReplicatedMovementMode(EVRConjoinedMovementModes &NewMovementMode, bool bClearMovementMode = false)
	{
		if (NewMovementMode != EVRConjoinedMovementModes::C_MOVE_MAX)//None)
		{
			if (NewMovementMode <= EVRConjoinedMovementModes::C_MOVE_MAX)
			{
				// Is a default movement mode, just directly set it
				SetMovementMode((EMovementMode)NewMovementMode);
			}
			else // Is Custom
			{
				// Auto calculates the difference for our VR movements, index is from 0 so using climbing should get me correct index's as it is the first custom mode
				SetMovementMode(EMovementMode::MOVE_Custom, (((int8)NewMovementMode - (uint8)EVRConjoinedMovementModes::C_VRMOVE_Climbing)));
			}

			// Clearing it here instead now, as this way the code can inject it during PerformMovement
			// Specifically used by the Climbing Step up, so that server rollbacks are supported
			if(bClearMovementMode)
				NewMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;//None;
		}
	}

	void UpdateFromCompressedFlags(uint8 Flags) override;

	FVector RoundDirectMovement(FVector InMovement) const;

	// Setting this below 1.0 will change how fast you de-accelerate when touching a wall
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|LowGrav", meta = (ClampMin = "0.0", UIMin = "0", ClampMax = "5.0", UIMax = "5"))
		float VRLowGravWallFrictionScaler;

	// If true then low grav will ignore the default physics volume fluid friction, useful if you have a mix of low grav and normal movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRMovement|LowGrav")
		bool VRLowGravIgnoresDefaultFluidFriction;

	/** Replicate position correction to client, associated with a timestamped servermove.  Client will replay subsequent moves after applying adjustment.  */
	UFUNCTION(unreliable, client)
		virtual void ClientAdjustPosition(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void ClientAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, FVector NewVel, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override
	{
		//this->CustomVRInputVector = FVector::ZeroVector;

		Super::ClientAdjustPosition_Implementation(TimeStamp, NewLoc, NewVel, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
	}

	/* Bandwidth saving version, when velocity is zeroed */
	UFUNCTION(unreliable, client)
	virtual void ClientVeryShortAdjustPosition(float TimeStamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void ClientVeryShortAdjustPosition_Implementation(float TimeStamp, FVector NewLoc, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override
	{
		//this->CustomVRInputVector = FVector::ZeroVector;

		Super::ClientVeryShortAdjustPosition_Implementation(TimeStamp, NewLoc, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
	}
};

