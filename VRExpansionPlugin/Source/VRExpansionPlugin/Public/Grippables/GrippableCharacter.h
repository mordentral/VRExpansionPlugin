// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Grippables/GrippableSkeletalMeshComponent.h"
#include "GrippableCharacter.generated.h"

UCLASS()
class VREXPANSIONPLUGIN_API AGrippableCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AGrippableCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// A reference to the grippable character that can be used instead of casting the root, BP doesn't like the class override.
	UPROPERTY(Category = GrippableCharacter, VisibleAnywhere, Transient, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
		UGrippableSkeletalMeshComponent * GrippableMeshReference;

	// A Custom bone to use on the character mesh as the originator for the perception systems sight sense
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
		FName ViewOriginationSocket;

	virtual void GetActorEyesViewPoint(FVector& Location, FRotator& Rotation) const override;

};