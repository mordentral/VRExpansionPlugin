// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GrippableCharacter.generated.h"

UCLASS()
class VREXPANSIONPLUGIN_API AGrippableCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AGrippableCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

};