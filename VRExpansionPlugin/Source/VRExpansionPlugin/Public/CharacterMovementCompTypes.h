// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRBPDatatypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "Components/SkeletalMeshComponent.h"
#include "CharacterMovementCompTypes.generated.h"


class AVRBaseCharacter;
class UVRBaseCharacterMovementComponent;

UENUM(Blueprintable)
enum class EVRMoveAction : uint8
{
	VRMOVEACTION_None = 0x00,
	VRMOVEACTION_SnapTurn = 0x01,
	VRMOVEACTION_Teleport = 0x02,
	VRMOVEACTION_StopAllMovement = 0x03,
	VRMOVEACTION_SetRotation = 0x04,
	VRMOVEACTION_PauseTracking = 0x14, // Reserved from here up to 0x40
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
	VRMOVEACTION_CUSTOM11 = 0x0F,
	VRMOVEACTION_CUSTOM12 = 0x10,
	VRMOVEACTION_CUSTOM13 = 0x11,
	VRMOVEACTION_CUSTOM14 = 0x12,
	VRMOVEACTION_CUSTOM15 = 0x13,
	// Up to 0x20 currently allowed for
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
		FVector MoveActionVel;
	UPROPERTY()
		FRotator MoveActionRot;
	UPROPERTY()
		uint8 MoveActionFlags;
	UPROPERTY()
		TArray<UObject*> MoveActionObjectReferences;
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
		MoveActionVel = FVector::ZeroVector;
		MoveActionRot = FRotator::ZeroRotator;
		MoveActionFlags = 0;
		VelRetentionSetting = EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_None;
		MoveActionObjectReferences.Empty();
	}

	/** Network serialization */
	// Doing a custom NetSerialize here because this is sent via RPCs and should change on every update
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bOutSuccess = true;

		Ar.SerializeBits(&MoveAction, 6); // 64 elements, only allowing 1 per frame, they aren't flags

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
				bool bUseLocOnly = MoveActionFlags & 0x04;
				Ar.SerializeBits(&bUseLocOnly, 1);

				if (!bUseLocOnly)
				{
					Yaw = FRotator::CompressAxisToShort(MoveActionRot.Yaw);
					Ar << Yaw;
				}
				else
				{
					Ar << MoveActionLoc;
				}

				bool bTeleportGrips = MoveActionFlags & 0x01;// MoveActionRot.Roll > 0.0f && MoveActionRot.Roll < 1.5f;
				Ar.SerializeBits(&bTeleportGrips, 1);

				if (!bTeleportGrips)
				{
					bool bTeleportCharacter = MoveActionFlags & 0x02;// MoveActionRot.Roll > 1.5f;
					Ar.SerializeBits(&bTeleportCharacter, 1);
				}

				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					bOutSuccess &= SerializePackedVector<100, 30>(MoveActionVel, Ar);
					//Pitch = FRotator::CompressAxisToShort(MoveActionRot.Pitch);
					//Ar << Pitch;
				}
			}
			else
			{

				bool bUseLocOnly = false;
				Ar.SerializeBits(&bUseLocOnly, 1);
				MoveActionFlags |= (bUseLocOnly << 2);

				if (!bUseLocOnly)
				{
					Ar << Yaw;
					MoveActionRot.Yaw = FRotator::DecompressAxisFromShort(Yaw);
				}
				else
				{
					Ar << MoveActionLoc;
				}

				bool bTeleportGrips = false;
				Ar.SerializeBits(&bTeleportGrips, 1);
				MoveActionFlags |= (uint8)bTeleportGrips; //.Roll = bTeleportGrips ? 1.0f : 0.0f;

				if (!bTeleportGrips)
				{
					bool bTeleportCharacter = false;
					Ar.SerializeBits(&bTeleportCharacter, 1);
					MoveActionFlags |= ((uint8)bTeleportCharacter << 1);
						//MoveActionRot.Roll = 2.0f;
				}

				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					bOutSuccess &= SerializePackedVector<100, 30>(MoveActionVel, Ar);
					//Ar << Pitch;
					//MoveActionRot.Pitch = FRotator::DecompressAxisFromShort(Pitch);
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

				bool bSkipEncroachment = MoveActionFlags & 0x01;// MoveActionRot.Roll > 0.0f;
				Ar.SerializeBits(&bSkipEncroachment, 1);
				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					bOutSuccess &= SerializePackedVector<100, 30>(MoveActionVel, Ar);
					//Pitch = FRotator::CompressAxisToShort(MoveActionRot.Pitch);
					//Ar << Pitch;
				}
			}
			else
			{
				Ar << Yaw;
				MoveActionRot.Yaw = FRotator::DecompressAxisFromShort(Yaw);

				bool bSkipEncroachment = false;
				Ar.SerializeBits(&bSkipEncroachment, 1);
				MoveActionFlags |= (uint8)bSkipEncroachment;
				//MoveActionRot.Roll = bSkipEncroachment ? 1.0f : 0.0f;
				Ar.SerializeBits(&VelRetentionSetting, 2);

				if (VelRetentionSetting == EVRMoveActionVelocityRetention::VRMOVEACTION_Velocity_Turn)
				{
					bOutSuccess &= SerializePackedVector<100, 30>(MoveActionVel, Ar);
					//Ar << Pitch;
					//MoveActionRot.Pitch = FRotator::DecompressAxisFromShort(Pitch);
				}
			}

			bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);
		}break;
		case EVRMoveAction::VRMOVEACTION_StopAllMovement:
		{}break;
		case EVRMoveAction::VRMOVEACTION_PauseTracking:
		{

			Ar.SerializeBits(&MoveActionFlags, 1);
			bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);

			uint16 Yaw = 0;
			// Loc and rot for capsule should also be sent here
			if (Ar.IsSaving())
			{
				Yaw = FRotator::CompressAxisToShort(MoveActionRot.Yaw);
				Ar << Yaw;
			}
			else
			{
				Ar << Yaw;
				MoveActionRot.Yaw = FRotator::DecompressAxisFromShort(Yaw);
			}

		}break;
		default: // Everything else
		{
			// Defines how much to replicate - only 4 possible values, 0 - 3 so only send 2 bits
			Ar.SerializeBits(&MoveActionDataReq, 2);

			if (((uint8)MoveActionDataReq & (uint8)EVRMoveActionDataReq::VRMOVEACTIONDATA_LOC) != 0)
				bOutSuccess &= SerializePackedVector<100, 30>(MoveActionLoc, Ar);

			if (((uint8)MoveActionDataReq & (uint8)EVRMoveActionDataReq::VRMOVEACTIONDATA_ROT) != 0)
				MoveActionRot.SerializeCompressedShort(Ar);

			bool bSerializeObjects = MoveActionObjectReferences.Num() > 0;
			Ar.SerializeBits(&bSerializeObjects, 1);
			if (bSerializeObjects)
			{
				Ar << MoveActionObjectReferences;
			}

			bool bSerializeFlags = MoveActionFlags != 0x00;
			Ar.SerializeBits(&bSerializeFlags, 1);
			if (bSerializeFlags)
			{
				Ar << MoveActionFlags;
			}

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

		bool bIsLoading = Ar.IsLoading();

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
			{
				bOutSuccess &= SerializePackedVector<100, 22/*30*/>(CustomVRInputVector, Ar);
			}
			else if (bIsLoading)
			{
				CustomVRInputVector = FVector::ZeroVector;
			}

			if (bHasRequestedVelocity)
			{
				bOutSuccess &= SerializePackedVector<100, 22/*30*/>(RequestedVelocity, Ar);
			}
			else if (bIsLoading)
			{
				RequestedVelocity = FVector::ZeroVector;
			}

			//if (bHasMoveAction)
			MoveActionArray.NetSerialize(Ar, Map, bOutSuccess);
		}
		else if (bIsLoading)
		{
			CustomVRInputVector = FVector::ZeroVector;
			RequestedVelocity = FVector::ZeroVector;
			MoveActionArray.Clear();
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

// #TODO: DELETE THIS
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
	USkeletalMeshComponent* MeshRef;
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
	virtual void PrepMoveFor(ACharacter* Character) override;
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;

	/** Set the properties describing the final position, etc. of the moved pawn. */
	virtual void PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode) override;

	FSavedMove_VRBaseCharacter();

	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override;
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


/** Shared pointer for easy memory management of FSavedMove_Character, for accumulating and replaying network moves. */
//typedef TSharedPtr<class FSavedMove_Character> FSavedMovePtr;
struct VREXPANSIONPLUGIN_API FVRCharacterNetworkMoveData : public FCharacterNetworkMoveData
{
public:

	FVector_NetQuantize100 VRCapsuleLocation;
	FVector_NetQuantize100 LFDiff;
	uint16 VRCapsuleRotation;
	EVRConjoinedMovementModes ReplicatedMovementMode;
	FVRConditionalMoveRep ConditionalMoveReps;

	FVRCharacterNetworkMoveData();

	virtual ~FVRCharacterNetworkMoveData();
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
};

struct VREXPANSIONPLUGIN_API FVRCharacterNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
public:

	/**
	 * Default constructor. Sets data storage (NewMoveData, PendingMoveData, OldMoveData) to point to default data members. Override those pointers to instead point to custom data if you want to use derived classes.
	 */
	FVRCharacterNetworkMoveDataContainer() : FCharacterNetworkMoveDataContainer()
	{
		NewMoveData = &VRBaseDefaultMoveData[0];
		PendingMoveData = &VRBaseDefaultMoveData[1];
		OldMoveData = &VRBaseDefaultMoveData[2];
	}

	virtual ~FVRCharacterNetworkMoveDataContainer()
	{
	}

	/**
 * Passes through calls to ClientFillNetworkMoveData on each FCharacterNetworkMoveData matching the client moves. Note that ClientNewMove will never be null, but others may be.
 */
 //virtual void ClientFillNetworkMoveData(const FSavedMove_Character* ClientNewMove, const FSavedMove_Character* ClientPendingMove, const FSavedMove_Character* ClientOldMove);

 /**
  * Serialize movement data. Passes Serialize calls to each FCharacterNetworkMoveData as applicable, based on bHasPendingMove and bHasOldMove.
  */
  //virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap);



protected:


	FVRCharacterNetworkMoveData VRBaseDefaultMoveData[3];

};

struct VREXPANSIONPLUGIN_API FVRCharacterMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
public:

	FVRCharacterMoveResponseDataContainer() : FCharacterMoveResponseDataContainer()
	{
	}

	virtual ~FVRCharacterMoveResponseDataContainer()
	{
	}

	/**
	 * Copy the FClientAdjustment and set a few flags relevant to that data.
	 */
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;

	//bool bHasRotation; // By default ClientAdjustment.NewRot is not serialized. Set this to true after base ServerFillResponseData if you want Rotation to be serialized.

};