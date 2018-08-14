// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "GS_InteractibleSettings.generated.h"


USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPGS_InteractionSettings
{
	GENERATED_BODY()
public:

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
		uint32 bLimitsInLocalSpace : 1;

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

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector_NetQuantize100 InitialLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector_NetQuantize100 MinLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Linear")
		FVector_NetQuantize100 MaxLinearTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings|Angular")
		FRotator InitialAngularTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		FRotator MinAngularTranslation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "AngularSettings")
		FRotator MaxAngularTranslation;

	FBPGS_InteractionSettings() :
		bLimitsInLocalSpace(true),
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
	{}
};

UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UGS_InteractibleSettings : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	UGS_InteractibleSettings(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadWrite, EditAnywhere)// Category = "InteractionSettings")
	FBPGS_InteractionSettings InteractionSettings;

	//virtual void BeginPlay_Implementation() override;
	virtual void GetWorldTransform_Implementation(UGripMotionControllerComponent * GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface) override;
	virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false) override;
};
