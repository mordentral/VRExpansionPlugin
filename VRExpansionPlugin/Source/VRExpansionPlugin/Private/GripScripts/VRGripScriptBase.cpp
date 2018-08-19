// Fill out your copyright notice in the Description page of Project Settings.

#include "GripScripts/VRGripScriptBase.h"
#include "GripMotionControllerComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/NetDriver.h"
 
UVRGripScriptBase::UVRGripScriptBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
//	PrimaryComponentTick.bCanEverTick = false;
//	PrimaryComponentTick.bStartWithTickEnabled = false;
//	PrimaryComponentTick.TickGroup = ETickingGroup::TG_PrePhysics;
	WorldTransformOverrideType = EGSTransformOverrideType::None;
}


void UVRGripScriptBase::OnBeginPlay_Implementation(UObject * CallingOwner) {};

bool UVRGripScriptBase::GetWorldTransform_Implementation(UGripMotionControllerComponent* GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface) { return true; }
void UVRGripScriptBase::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) {}
void UVRGripScriptBase::OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed) {}
void UVRGripScriptBase::OnSecondaryGrip_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}
void UVRGripScriptBase::OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation) {}


EGSTransformOverrideType UVRGripScriptBase::GetWorldTransformOverrideType() { return WorldTransformOverrideType; }
bool UVRGripScriptBase::IsScriptActive() { return bIsActive; }
bool UVRGripScriptBase::Wants_DenyAutoDrop() { return bDenyAutoDrop; }
//bool UVRGripScriptBase::Wants_DenyTeleport_Implementation() { return false; }


void UVRGripScriptBase::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	// Uobject has no replicated props
	//Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate here if required
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(GetClass());
	if (BPClass != NULL)
	{
		BPClass->GetLifetimeBlueprintReplicationList(OutLifetimeProps);
	}
}

bool UVRGripScriptBase::CallRemoteFunction(UFunction * Function, void * Parms, FOutParmRec * OutParms, FFrame * Stack)
{
	AActor* Owner = GetOwner();//Cast<AActor>(GetOuter());
	if (Owner)
	{
		UNetDriver* NetDriver = Owner->GetNetDriver();
		if (NetDriver)
		{
			NetDriver->ProcessRemoteFunction(Owner, Function, Parms, OutParms, Stack, this);
			return true;
		}
	}

	return false;
}

int32 UVRGripScriptBase::GetFunctionCallspace(UFunction * Function, void * Parameters, FFrame * Stack)
{
	AActor* Owner = GetOwner();// Cast<AActor>(GetOuter());
	return (Owner ? Owner->GetFunctionCallspace(Function, Parameters, Stack) : FunctionCallspace::Local);
}