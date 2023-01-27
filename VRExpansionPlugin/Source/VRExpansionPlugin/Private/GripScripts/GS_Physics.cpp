// Fill out your copyright notice in the Description page of Project Settings.

#include "GripScripts/GS_Physics.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GS_Physics)

#include "VRGripInterface.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "GripMotionControllerComponent.h"

UGS_Physics::UGS_Physics(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer)
{
	bIsActive = true;
	WorldTransformOverrideType = EGSTransformOverrideType::None;

	bDenyLateUpdates = true;

	bInjectPrePhysicsHandle = false;
	bInjectPostPhysicsHandle = true;

	bCanEverTick = false;

	SingleHandPhysicsSettings.TwistSettings.MaxForceCoefficient = 1.f;
	SingleHandPhysicsSettings.SwingSettings = SingleHandPhysicsSettings.TwistSettings;
	SingleHandPhysicsSettings.SlerpSettings = SingleHandPhysicsSettings.TwistSettings;

	SingleHandPhysicsSettings.XAxisSettings.MaxForceCoefficient = 1.f;
	SingleHandPhysicsSettings.YAxisSettings = SingleHandPhysicsSettings.XAxisSettings;
	SingleHandPhysicsSettings.ZAxisSettings = SingleHandPhysicsSettings.XAxisSettings;

}

void UGS_Physics::UpdateDualHandInfo(UGripMotionControllerComponent* GrippingController, bool bReCreate)
{
	TArray<FBPGripPair> HoldingControllers;
	bool bIsHeld;
	IVRGripInterface::Execute_IsHeld(GetParent(), HoldingControllers, bIsHeld);

	int NumControllers = HoldingControllers.Num();

	for (FBPGripPair& Grip : HoldingControllers)
	{
		if (NumControllers > 1)
		{
			FBPActorPhysicsHandleInformation* HandleInfo = Grip.HoldingController->GetPhysicsGrip(Grip.GripID);
			if (HandleInfo)
			{
				MultiHandPhysicsSettings.FillTo(HandleInfo);

				if(bReCreate && Grip.HoldingController != GrippingController)
					Grip.HoldingController->UpdatePhysicsHandle(Grip.GripID, true);
			}
		}
		else
		{
			FBPActorPhysicsHandleInformation* HandleInfo = Grip.HoldingController->GetPhysicsGrip(Grip.GripID);
			if (HandleInfo)
			{
				SingleHandPhysicsSettings.FillTo(HandleInfo);

				if(bReCreate)
					Grip.HoldingController->UpdatePhysicsHandle(Grip.GripID, true);
			}
		}
	}
}


void UGS_Physics::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation)
{
	if (!bIsActive)
		return;

	UpdateDualHandInfo(GrippingController, true);
}

void UGS_Physics::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
{

	if (!bIsActive)
		return;

	UpdateDualHandInfo(nullptr, true);
}

// Should I be orienting it to the controller for these types of grips?
/*void UGS_Physics::HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation * HandleInfo, FTransform & KinPose)
{
	if (!bIsActive)
		return;

	if (WeaponRootOrientationComponent != NAME_None)
	{
		// Alter to rotate to x+ if we have an orientation component
		FQuat DeltaQuat = OrientationComponentRelativeFacing.GetRotation();

		// This moves the kinematic actor to face its X+ in the direction designated
		KinPose.SetRotation(KinPose.GetRotation() * (HandleInfo->RootBoneRotation.GetRotation().Inverse() * DeltaQuat));
		HandleInfo->COMPosition.SetRotation(HandleInfo->COMPosition.GetRotation() * (HandleInfo->RootBoneRotation.GetRotation().Inverse() * DeltaQuat));
	}
}*/

void UGS_Physics::HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation * HandleInfo)
{
	if (!bIsActive)
		return;


	UpdateDualHandInfo(GrippingController, false);
}