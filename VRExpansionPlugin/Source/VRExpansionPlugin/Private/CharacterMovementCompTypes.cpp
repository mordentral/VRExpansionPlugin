// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Movement.cpp: Character movement implementation

=============================================================================*/

#include "CharacterMovementCompTypes.h"
#include "VRBaseCharacterMovementComponent.h"
#include "VRBPDatatypes.h"
#include "ParentRelativeAttachmentComponent.h"
#include "VRBaseCharacter.h"
#include "VRRootComponent.h"
#include "VRPlayerController.h"
	
FSavedMove_VRBaseCharacter::FSavedMove_VRBaseCharacter() : FSavedMove_Character()
{
	VRCapsuleLocation = FVector::ZeroVector;
	LFDiff = FVector::ZeroVector;
	VRCapsuleRotation = FRotator::ZeroRotator;
	VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;// _None;
}

uint8 FSavedMove_VRBaseCharacter::GetCompressedFlags() const
{
	// Fills in 01 and 02 for Jump / Crouch
	uint8 Result = FSavedMove_Character::GetCompressedFlags();

	// Not supporting custom movement mode directly at this time by replicating custom index
	// We use 4 bits for this so a maximum of 16 elements
	//Result |= (uint8)VRReplicatedMovementMode << 2;

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

bool FSavedMove_VRBaseCharacter::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	FSavedMove_VRBaseCharacter* nMove = (FSavedMove_VRBaseCharacter*)NewMove.Get();


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

	if (!FVector2D(LFDiff.X, LFDiff.Y).IsZero() && !FVector2D(nMove->LFDiff.X, nMove->LFDiff.Y).IsZero() && !FVector::Coincident(LFDiff.GetSafeNormal2D(), nMove->LFDiff.GetSafeNormal2D(), AccelDotThresholdCombine))
		return false;

	return FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
}


bool FSavedMove_VRBaseCharacter::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
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

void FSavedMove_VRBaseCharacter::SetInitialPosition(ACharacter* C)
{
	// See if we can get the VR capsule location
	//if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(C))
	//{
	if (UVRBaseCharacterMovementComponent* moveComp = Cast<UVRBaseCharacterMovementComponent>(C->GetMovementComponent()))
	{

		// Saving this out early because it will be wiped before the PostUpdate gets the values
		//ConditionalValues.MoveAction.MoveAction = moveComp->MoveAction.MoveAction;

		VRReplicatedMovementMode = moveComp->VRReplicatedMovementMode;

		if (moveComp->HasRequestedVelocity())
			ConditionalValues.RequestedVelocity = moveComp->RequestedVelocity;
		else
			ConditionalValues.RequestedVelocity = FVector::ZeroVector;

		// Throw out the Z value of the headset, its not used anyway for movement
		// Instead, re-purpose it to be the capsule half height
		if (AVRBaseCharacter* BaseChar = Cast<AVRBaseCharacter>(C))
		{
			if (BaseChar->VRReplicateCapsuleHeight)
				LFDiff.Z = BaseChar->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
			else
				LFDiff.Z = 0.0f;
		}
		else
			LFDiff.Z = 0.0f;
	}
	else
	{
		VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;//None;
		ConditionalValues.CustomVRInputVector = FVector::ZeroVector;
		ConditionalValues.RequestedVelocity = FVector::ZeroVector;
	}
	//}
	//else
	//{
	//	VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;//None;
	//	ConditionalValues.CustomVRInputVector = FVector::ZeroVector;
	//}

	FSavedMove_Character::SetInitialPosition(C);
}

void FSavedMove_VRBaseCharacter::CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation)
{
	UCharacterMovementComponent* CharMovement = InCharacter->GetCharacterMovement();

	// to combine move, first revert pawn position to PendingMove start position, before playing combined move on client
	CharMovement->UpdatedComponent->SetWorldLocationAndRotation(OldStartLocation, OldMove->StartRotation, false, nullptr, CharMovement->GetTeleportType());
	CharMovement->Velocity = OldMove->StartVelocity;

	CharMovement->SetBase(OldMove->StartBase.Get(), OldMove->StartBoneName);
	CharMovement->CurrentFloor = OldMove->StartFloor;

	// Now that we have reverted to the old position, prepare a new move from that position,
	// using our current velocity, acceleration, and rotation, but applied over the combined time from the old and new move.

	// Combine times for both moves
	DeltaTime += OldMove->DeltaTime;

	//FSavedMove_VRBaseCharacter * BaseSavedMove = (FSavedMove_VRBaseCharacter *)NewMove.Get();
	FSavedMove_VRBaseCharacter* BaseSavedMovePending = (FSavedMove_VRBaseCharacter*)OldMove;

	if (/*BaseSavedMove && */BaseSavedMovePending)
	{
		LFDiff.X += BaseSavedMovePending->LFDiff.X;
		LFDiff.Y += BaseSavedMovePending->LFDiff.Y;
	}

	// Roll back jump force counters. SetInitialPosition() below will copy them to the saved move.
	// Changes in certain counters like JumpCurrentCount don't allow move combining, so no need to roll those back (they are the same).
	InCharacter->JumpForceTimeRemaining = OldMove->JumpForceTimeRemaining;
	InCharacter->JumpKeyHoldTime = OldMove->JumpKeyHoldTime;
}

void FSavedMove_VRBaseCharacter::PostUpdate(ACharacter* C, EPostUpdateMode PostUpdateMode)
{
	FSavedMove_Character::PostUpdate(C, PostUpdateMode);

	// See if we can get the VR capsule location
	//if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(C))
	//{
	if (UVRBaseCharacterMovementComponent* moveComp = Cast<UVRBaseCharacterMovementComponent>(C->GetMovementComponent()))
	{
		ConditionalValues.CustomVRInputVector = moveComp->CustomVRInputVector;
		ConditionalValues.MoveActionArray = moveComp->MoveActionArray;
		moveComp->MoveActionArray.Clear();
	}
	//}
	/*if (ConditionalValues.MoveAction.MoveAction != EVRMoveAction::VRMOVEACTION_None)
	{
		// See if we can get the VR capsule location
		if (AVRBaseCharacter * VRC = Cast<AVRBaseCharacter>(C))
		{
			if (UVRBaseCharacterMovementComponent * moveComp = Cast<UVRBaseCharacterMovementComponent>(VRC->GetMovementComponent()))
			{
				// This is cleared out in perform movement so I need to save it before applying below
				EVRMoveAction tempAction = ConditionalValues.MoveAction.MoveAction;
				ConditionalValues.MoveAction = moveComp->MoveAction;
				ConditionalValues.MoveAction.MoveAction = tempAction;
			}
			else
			{
				ConditionalValues.MoveAction.Clear();
			}
		}
		else
		{
			ConditionalValues.MoveAction.Clear();
		}
	}*/
}

void FSavedMove_VRBaseCharacter::Clear()
{
	VRReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;// None;

	VRCapsuleLocation = FVector::ZeroVector;
	VRCapsuleRotation = FRotator::ZeroRotator;
	LFDiff = FVector::ZeroVector;

	ConditionalValues.CustomVRInputVector = FVector::ZeroVector;
	ConditionalValues.RequestedVelocity = FVector::ZeroVector;
	ConditionalValues.MoveActionArray.Clear();
	//ConditionalValues.MoveAction.Clear();

	FSavedMove_Character::Clear();
}

void FSavedMove_VRBaseCharacter::PrepMoveFor(ACharacter* Character)
{
	UVRBaseCharacterMovementComponent* BaseCharMove = Cast<UVRBaseCharacterMovementComponent>(Character->GetCharacterMovement());

	if (BaseCharMove)
	{
		BaseCharMove->MoveActionArray = ConditionalValues.MoveActionArray;
		//BaseCharMove->MoveAction = ConditionalValues.MoveAction; 
		BaseCharMove->CustomVRInputVector = ConditionalValues.CustomVRInputVector;//this->CustomVRInputVector;
		BaseCharMove->VRReplicatedMovementMode = this->VRReplicatedMovementMode;
	}

	if (!ConditionalValues.RequestedVelocity.IsZero())
	{
		BaseCharMove->RequestedVelocity = ConditionalValues.RequestedVelocity;
		BaseCharMove->SetHasRequestedVelocity(true);
	}
	else
	{
		BaseCharMove->SetHasRequestedVelocity(false);
	}

	FSavedMove_Character::PrepMoveFor(Character);
}

FVRCharacterScopedMovementUpdate::FVRCharacterScopedMovementUpdate(USceneComponent* Component, EScopedUpdate::Type ScopeBehavior, bool bRequireOverlapsEventFlagToQueueOverlaps)
	: FScopedMovementUpdate(Component, ScopeBehavior, bRequireOverlapsEventFlagToQueueOverlaps)
{
	UVRRootComponent* RootComponent = Cast<UVRRootComponent>(Owner);
	if (RootComponent)
	{
		InitialVRTransform = RootComponent->OffsetComponentToWorld;
	}
}

void FVRCharacterScopedMovementUpdate::RevertMove()
{
	bool bTransformIsDirty = IsTransformDirty();

	FScopedMovementUpdate::RevertMove();

	UVRRootComponent* RootComponent = Cast<UVRRootComponent>(Owner);
	if (RootComponent)
	{
		// If the base class was going to miss bad overlaps, ie: the offsetcomponent to world is different but the transform isn't
		if (!bTransformIsDirty && !IsDeferringUpdates() && !InitialVRTransform.Equals(RootComponent->OffsetComponentToWorld))
		{
			RootComponent->UpdateOverlaps();
		}

		// Fix offset
		RootComponent->GenerateOffsetToWorld();
	}
}


FVRCharacterNetworkMoveData::FVRCharacterNetworkMoveData() : FCharacterNetworkMoveData()
{
	VRCapsuleLocation = FVector::ZeroVector;
	LFDiff = FVector::ZeroVector;
	VRCapsuleRotation = 0;
	ReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;
}

FVRCharacterNetworkMoveData::~FVRCharacterNetworkMoveData()
{
}

void FVRCharacterNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	// Handles the movement base itself now	
	FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);

	// I know that we overloaded this, so it should be our base type
	if (const FSavedMove_VRBaseCharacter* SavedMove = (const FSavedMove_VRBaseCharacter*)(&ClientMove))
	{
		ReplicatedMovementMode = SavedMove->VRReplicatedMovementMode;
		ConditionalMoveReps = SavedMove->ConditionalValues;

		// #TODO: Roll these into the conditionals
		VRCapsuleLocation = SavedMove->VRCapsuleLocation;
		LFDiff = SavedMove->LFDiff;
		VRCapsuleRotation = FRotator::CompressAxisToShort(SavedMove->VRCapsuleRotation.Yaw);
	}
}

bool FVRCharacterNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	NetworkMoveType = MoveType;

	bool bLocalSuccess = true;
	const bool bIsSaving = Ar.IsSaving();

	Ar << TimeStamp;

	// Handle switching the acceleration rep
	// Can't use SerializeOptionalValue here as I don't want to bitwise compare floats
	bool bRepAccel = bIsSaving ? !Acceleration.IsNearlyZero() : false;
	Ar.SerializeBits(&bRepAccel, 1);

	if (bRepAccel)
	{
		Acceleration.NetSerialize(Ar, PackageMap, bLocalSuccess);
	}
	else
	{
		if (!bIsSaving)
		{
			Acceleration = FVector::ZeroVector;
		}
	}

	//Location.NetSerialize(Ar, PackageMap, bLocalSuccess);

	uint16 Yaw = bIsSaving ? FRotator::CompressAxisToShort(ControlRotation.Yaw) : 0;
	uint16 Pitch = bIsSaving ? FRotator::CompressAxisToShort(ControlRotation.Pitch) : 0;
	uint16 Roll = bIsSaving ? FRotator::CompressAxisToShort(ControlRotation.Roll) : 0;
	bool bRepYaw = Yaw != 0;

	ACharacter* CharacterOwner = CharacterMovement.GetCharacterOwner();

	bool bCanRepRollAndPitch = (CharacterOwner && (CharacterOwner->bUseControllerRotationRoll || CharacterOwner->bUseControllerRotationPitch));
	bool bRepRollAndPitch = bCanRepRollAndPitch && (Roll != 0 || Pitch != 0);
	Ar.SerializeBits(&bRepRollAndPitch, 1);

	if (bRepRollAndPitch)
	{
		// Reversed the order of these
		uint32 Rotation32 = 0;
		uint32 Yaw32 = bIsSaving ? Yaw : 0;

		if (bIsSaving)
		{
			Rotation32 = (((uint32)Roll) << 16) | ((uint32)Pitch);
			Ar.SerializeIntPacked(Rotation32);
		}
		else
		{
			Ar.SerializeIntPacked(Rotation32);

			// Reversed the order of these so it costs less to replicate
			Pitch = (Rotation32 & 65535);
			Roll = (Rotation32 >> 16);
		}
	}

	uint32 Yaw32 = bIsSaving ? Yaw : 0;

	Ar.SerializeBits(&bRepYaw, 1);
	if (bRepYaw)
	{
		Ar.SerializeIntPacked(Yaw32);
		Yaw = (uint16)Yaw32;
	}

	if (!bIsSaving)
	{
		ControlRotation.Yaw = bRepYaw ? FRotator::DecompressAxisFromShort(Yaw) : 0;
		ControlRotation.Pitch = bRepRollAndPitch ? FRotator::DecompressAxisFromShort(Pitch) : 0;
		ControlRotation.Roll = bRepRollAndPitch ? FRotator::DecompressAxisFromShort(Roll) : 0;
	}

	// ControlRotation : FRotator handles each component zero/non-zero test; it uses a single signal bit for zero/non-zero, and uses 16 bits per component if non-zero.
	//ControlRotation.NetSerialize(Ar, PackageMap, bLocalSuccess);

	SerializeOptionalValue<uint8>(bIsSaving, Ar, CompressedMoveFlags, 0);
	SerializeOptionalValue<uint8>(bIsSaving, Ar, MovementMode, MOVE_Walking);
	VRCapsuleLocation.NetSerialize(Ar, PackageMap, bLocalSuccess);
	Ar << VRCapsuleRotation;

	if (MoveType == ENetworkMoveType::NewMove)
	{
		Location.NetSerialize(Ar, PackageMap, bLocalSuccess);

		// Location, relative movement base, and ending movement mode is only used for error checking, so only save for the final move.
		SerializeOptionalValue<UPrimitiveComponent*>(bIsSaving, Ar, MovementBase, nullptr);
		SerializeOptionalValue<FName>(bIsSaving, Ar, MovementBaseBoneName, NAME_None);
		//SerializeOptionalValue<uint8>(bIsSaving, Ar, MovementMode, MOVE_Walking); // Epic has this like this too, but it is bugged and killing movements
	}

	bool bHasReplicatedMovementMode = ReplicatedMovementMode != EVRConjoinedMovementModes::C_MOVE_MAX;
	Ar.SerializeBits(&bHasReplicatedMovementMode, 1);

	if (bHasReplicatedMovementMode)
	{
		// Increased to 6 bits for 64 total elements instead of 16
		Ar.SerializeBits(&ReplicatedMovementMode, 6);
	}
	else if (!bIsSaving)
	{
		ReplicatedMovementMode = EVRConjoinedMovementModes::C_MOVE_MAX;
	}

	// Rep out our custom move settings
	ConditionalMoveReps.NetSerialize(Ar, PackageMap, bLocalSuccess);

	//VRCapsuleLocation.NetSerialize(Ar, PackageMap, bLocalSuccess);
	LFDiff.NetSerialize(Ar, PackageMap, bLocalSuccess);
	//Ar << VRCapsuleRotation;

	return !Ar.IsError();
}


void FVRCharacterMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(CharacterMovement, PendingAdjustment);

	if (const UVRBaseCharacterMovementComponent* BaseMovecomp = Cast<const UVRBaseCharacterMovementComponent>(&CharacterMovement))
	{
		bHasRotation = !BaseMovecomp->bUseClientControlRotation;
	}
}