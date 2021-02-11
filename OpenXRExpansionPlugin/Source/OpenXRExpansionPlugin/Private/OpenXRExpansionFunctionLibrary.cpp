// Fill out your copyright notice in the Description page of Project Settings.
#include "OpenXRExpansionFunctionLibrary.h"
//#include "EngineMinimal.h"
#include "Engine/Engine.h"
#include <openxr/openxr.h>
#include "CoreMinimal.h"
#include "IXRTrackingSystem.h"

UOpenXRExpansionFunctionLibrary::UOpenXRExpansionFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//=============================================================================
UOpenXRExpansionFunctionLibrary::~UOpenXRExpansionFunctionLibrary()
{

}
