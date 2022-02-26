// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "GripMotionControllerComponent.h"
#include "GS_InteractibleSettings.generated.h"


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPGS_InteractionSettings
{
	GENERATED_BODY()
public:

	bool bHasValidBaseTransform; // So we don't have to equals the transform
	FTransform BaseTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		uint32 bLimitsInLocalSpace : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		uint32 bGetInitialPositionsOnBeginPlay : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		uint32 bLimitX : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		uint32 bLimitY : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		uint32 bLimitZ : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bLimitPitch : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bLimitYaw : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bLimitRoll : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		uint32 bIgnoreHandRotation : 1;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear", meta = (editcondition = "!bGetInitialPositionsOnBeginPlay"))
		FVector_NetQuantize100 InitialLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector_NetQuantize100 MinLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector_NetQuantize100 MaxLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular", meta = (editcondition = "!bGetInitialPositionsOnBeginPlay"))
		FRotator InitialAngularTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		FRotator MinAngularTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		FRotator MaxAngularTranslation;

	FBPGS_InteractionSettings() :
		bLimitsInLocalSpace(true),
		bGetInitialPositionsOnBeginPlay(true),
		bLimitX(false),
		bLimitY(false),
		bLimitZ(false),
		bLimitPitch(false),
		bLimitYaw(false),
		bLimitRoll(false),
		bIgnoreHandRotation(false),
		InitialLinearTranslation(FVector::ZeroVector),
		MinLinearTranslation(FVector::ZeroVector),
		MaxLinearTranslation(FVector::ZeroVector),
		InitialAngularTranslation(FRotator::ZeroRotator),
		MinAngularTranslation(FRotator::ZeroRotator),
		MaxAngularTranslation(FRotator::ZeroRotator)
	{
		BaseTransform = FTransform::Identity;
		bHasValidBaseTransform = false;
	}
};

// A Grip script that overrides the default grip behavior and adds custom clamping logic instead,
UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin), hideCategories = TickSettings)
class VREXPANSIONPLUGIN_API UGS_InteractibleSettings : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	UGS_InteractibleSettings(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "InteractionSettings")
	FBPGS_InteractionSettings InteractionSettings;

	virtual void OnBeginPlay_Implementation(UObject * CallingOwner) override;
	virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) override;
	virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false) override;

	// Flags the the interaction settings so that it will regenerate removing the hand rotation.
	// Use this if you just changed the relative hand transform.
	UFUNCTION(BlueprintCallable, Category = "InteractionSettings")
	void RemoveHandRotation()
	{
		// Flag the base transform to be re-applied
		InteractionSettings.bHasValidBaseTransform = false;
	}

	inline void RemoveRelativeRotation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation)
	{
		InteractionSettings.BaseTransform = GripInformation.RelativeTransform;

		// Reconstitute the controller transform relative to the object, then remove the rotation and set it back to relative to controller
		// This could likely be done easier by just removing rotation that the object doesn't possess but for now this will do.
		FTransform compTrans = this->GetParentTransform(true, GripInformation.GrippedBoneName);

		InteractionSettings.BaseTransform = FTransform(InteractionSettings.BaseTransform.ToInverseMatrixWithScale()) * compTrans; // Reconstitute transform
		InteractionSettings.BaseTransform.SetScale3D(GrippingController->GetPivotTransform().GetScale3D());
		InteractionSettings.BaseTransform.SetRotation(FQuat::Identity); // Remove rotation

		InteractionSettings.BaseTransform = compTrans.GetRelativeTransform(InteractionSettings.BaseTransform); // Set back to relative
		InteractionSettings.bHasValidBaseTransform = true;
	}
};
