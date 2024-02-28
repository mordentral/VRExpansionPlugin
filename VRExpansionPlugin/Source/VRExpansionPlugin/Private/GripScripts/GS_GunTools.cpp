// Fill out your copyright notice in the Description page of Project Settings.

#include "GripScripts/GS_GunTools.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(GS_GunTools)

#include "VRGripInterface.h"
#include "GripMotionControllerComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "IXRTrackingSystem.h"
#include "VRGlobalSettings.h"
#include "VRBaseCharacter.h"
#include "VRCharacter.h"
#include "VRRootComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
//#include "Camera/CameraComponent.h"
#include "ReplicatedVRCameraComponent.h"
#include "DrawDebugHelpers.h"

UGS_GunTools::UGS_GunTools(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer)
{
	bIsActive = true;
	WorldTransformOverrideType = EGSTransformOverrideType::OverridesWorldTransform;
	PivotOffset = FVector::ZeroVector;
	VirtualStockComponent = nullptr;
	MountWorldTransform = FTransform::Identity;
	//StockSnapOffset = FVector(0.f, 0.f, 0.f);
	bIsMounted = false;


	bHasRecoil = false;
	bApplyRecoilAsPhysicalForce = false;
	MaxRecoilTranslation = FVector::ZeroVector;
	MaxRecoilRotation = FVector::ZeroVector;
	MaxRecoilScale = FVector(1.f);
	bHasActiveRecoil = false;
	DecayRate = 20.f;
	LerpRate = 30.f;

	BackEndRecoilStorage = FTransform::Identity;

	bUseGlobalVirtualStockSettings = true;

	bUseHighQualityRemoteSimulation = false;

	bInjectPrePhysicsHandle = true;
	//bInjectPostPhysicsHandle = true;
	WeaponRootOrientationComponent = NAME_None;
	OrientationComponentRelativeFacing = FTransform::Identity;
	StoredRootOffset = FQuat::Identity;
}

void UGS_GunTools::OnBeginPlay_Implementation(UObject* CallingOwner)
{
	// Grip base has no super of this

	if (WeaponRootOrientationComponent.IsValid())
	{
		if (AActor * Owner = GetOwner())
		{
			FName CurrentCompName = NAME_None;
			for (UActorComponent* ChildComp : Owner->GetComponents())
			{
				CurrentCompName = ChildComp->GetFName();
				if (CurrentCompName == NAME_None)
					continue;

				if (CurrentCompName == WeaponRootOrientationComponent)
				{
					if (USceneComponent * SceneComp = Cast<USceneComponent>(ChildComp))
					{
						OrientationComponentRelativeFacing = SceneComp->GetRelativeTransform();
					}

					break;
				}
			}
		}
	}
}

void UGS_GunTools::HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation* HandleInfo, FTransform& KinPose)
{
	if (!bIsActive)
		return;

	if (WeaponRootOrientationComponent != NAME_None)
	{
		StoredRootOffset = HandleInfo->RootBoneRotation.GetRotation().Inverse() * OrientationComponentRelativeFacing.GetRotation();

		// Alter to rotate to x+ if we have an orientation component
		FQuat DeltaQuat = OrientationComponentRelativeFacing.GetRotation();
		
		KinPose.SetRotation(KinPose.GetRotation() * StoredRootOffset);
		HandleInfo->COMPosition.SetRotation(HandleInfo->COMPosition.GetRotation() * StoredRootOffset);
	}
	else
	{
		StoredRootOffset = FQuat::Identity;
	}

	if (GripInfo.bIsSlotGrip && !PivotOffset.IsZero())
	{
		KinPose.SetLocation(KinPose.TransformPosition(PivotOffset));
		HandleInfo->COMPosition.SetLocation(HandleInfo->COMPosition.TransformPosition(PivotOffset));
	}
}

/*void UGS_GunTools::HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation* HandleInfo)
{
}*/

bool UGS_GunTools::GetWorldTransform_Implementation
(
	UGripMotionControllerComponent* GrippingController, 
	float DeltaTime, FTransform & WorldTransform, 
	const FTransform &ParentTransform, 
	FBPActorGripInformation &Grip, 
	AActor * actor, 
	UPrimitiveComponent * root, 
	bool bRootHasInterface, 
	bool bActorHasInterface, 
	bool bIsForTeleport
) 
{
	if (!GrippingController)
		return false;

	bool bSkipHighQualityOperations = !bUseHighQualityRemoteSimulation && !GrippingController->HasAuthority();

	/*if (!GunViewExtension.IsValid() && GEngine)
	{
		GunViewExtension = FSceneViewExtensions::NewExtension<FGunViewExtension>(GrippingController);
	}*/

	// Just simple transform setting
	if (bHasRecoil && bHasActiveRecoil)
	{
		BackEndRecoilStorage.Blend(BackEndRecoilStorage, BackEndRecoilTarget, FMath::Clamp(LerpRate * DeltaTime, 0.f, 1.f));
		BackEndRecoilTarget.Blend(BackEndRecoilTarget, FTransform::Identity, FMath::Clamp(DecayRate * DeltaTime, 0.f, 1.f));
		bHasActiveRecoil = !BackEndRecoilTarget.Equals(FTransform::Identity);

		if (!bHasActiveRecoil)
		{
			BackEndRecoilStorage.SetIdentity();
			BackEndRecoilTarget.SetIdentity();
		}		
	}

	if (bHasActiveRecoil)
	{
		// Using a matrix to avoid FTransform inverse math issues
		FTransform relTransform(Grip.RelativeTransform.ToInverseMatrixWithScale());

		// Eventually may want to adjust the pivot of the recoil rotation by the PivotOffset vector...
		FVector Pivot = relTransform.GetLocation() + PivotOffset;
		const FTransform PivotToWorld = FTransform(FQuat::Identity, Pivot);
		const FTransform WorldToPivot = FTransform(FQuat::Identity, -Pivot);

		WorldTransform = WorldToPivot * BackEndRecoilStorage * PivotToWorld * Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;
	}
	else
		WorldTransform = Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;

	// Check the grip lerp state, this it ouside of the secondary attach check below because it can change the result of it
	if (Grip.SecondaryGripInfo.bHasSecondaryAttachment && Grip.SecondaryGripInfo.SecondaryAttachment)
	{
		if (!bSkipHighQualityOperations && bUseVirtualStock)
		{
			if (IsValid(VirtualStockComponent))
			{
				FRotator PureYaw = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(VirtualStockComponent->GetComponentRotation());
				MountWorldTransform = FTransform(PureYaw.Quaternion(), VirtualStockComponent->GetComponentLocation() + PureYaw.RotateVector(VirtualStockSettings.StockSnapOffset));
			}
			/*else if (GrippingController->bHasAuthority && GEngine->XRSystem.IsValid() && GEngine->XRSystem->IsHeadTrackingAllowedForWorld(*GetWorld()))
			{
				FQuat curRot = FQuat::Identity;
				FVector curLoc = FVector::ZeroVector;

				if (GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, curRot, curLoc))
				{
					// Translate hmd offset by the gripping controllers parent component, this should be in the same space
					FRotator PureYaw = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(curRot.Rotator());

					if (AVRCharacter* OwningCharacter = Cast<AVRCharacter>(GrippingController->GetOwner()))
					{
						if (!OwningCharacter->bRetainRoomscale)
						{
							curLoc.X = 0.0f;
							curLoc.Y = 0.0f;
							curLoc += PureYaw.RotateVector(FVector(-OwningCharacter->VRRootReference->VRCapsuleOffset.X, -OwningCharacter->VRRootReference->VRCapsuleOffset.Y, -OwningCharacter->VRRootReference->GetScaledCapsuleHalfHeight()));
						}
					}

					MountWorldTransform = FTransform(PureYaw.Quaternion(), curLoc + PureYaw.RotateVector(VirtualStockSettings.StockSnapOffset)) * GrippingController->GetAttachParent()->GetComponentTransform();
				}
			}*/
			else if(IsValid(CameraComponent))
			{		
				FRotator PureYaw = UVRExpansionFunctionLibrary::GetHMDPureYaw_I(CameraComponent->GetComponentRotation());
				MountWorldTransform = FTransform(PureYaw.Quaternion(), CameraComponent->GetComponentLocation() + PureYaw.RotateVector(VirtualStockSettings.StockSnapOffset));
			}

			float StockSnapDistance = FMath::Square(VirtualStockSettings.StockSnapDistance);
			float DistSquared = FVector::DistSquared(ParentTransform.GetTranslation(), MountWorldTransform.GetTranslation());

			if (!VirtualStockSettings.bUseDistanceBasedStockSnapping || (DistSquared <= StockSnapDistance))
			{

				float StockSnapLerpThresh = FMath::Square(VirtualStockSettings.StockSnapLerpThreshold);

				if (StockSnapLerpThresh > 0.0f)
					VirtualStockSettings.StockLerpValue = 1.0f - FMath::Clamp((DistSquared - (StockSnapDistance - StockSnapLerpThresh)) / StockSnapLerpThresh, 0.0f, 1.0f);
				else
					VirtualStockSettings.StockLerpValue = 1.0f; // Just skip lerping logic

				if (!bIsMounted)
				{
					VirtualStockSettings.StockHandSmoothing.ResetSmoothingFilter();
					
					// Mount up
					bIsMounted = true;
					OnVirtualStockModeChanged.Broadcast(bIsMounted);
				}

				// Adjust the mount location to follow the Z of the primary hand
				if (VirtualStockSettings.bAdjustZOfStockToPrimaryHand)
				{
					FVector WorldTransVec = MountWorldTransform.GetTranslation();

					if (WorldTransVec.Z >= ParentTransform.GetTranslation().Z)
					{
						WorldTransVec.Z = ParentTransform.GetTranslation().Z;
						MountWorldTransform.SetLocation(WorldTransVec);
					}
				}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (VirtualStockSettings.bDebugDrawVirtualStock)
				{
					DrawDebugLine(GetWorld(), ParentTransform.GetTranslation(), MountWorldTransform.GetTranslation(), FColor::Red);
					DrawDebugLine(GetWorld(), Grip.SecondaryGripInfo.SecondaryAttachment->GetComponentLocation(), MountWorldTransform.GetTranslation(), FColor::Green);
					DrawDebugSphere(GetWorld(), MountWorldTransform.GetTranslation(), 10.f, 32, FColor::White);
				}
#endif
			}
			else
			{
				if (bIsMounted)
				{
					bIsMounted = false;
					VirtualStockSettings.StockLerpValue = 0.0f;
					OnVirtualStockModeChanged.Broadcast(bIsMounted);
				}
			}
		}
	}
	else
	{
		if (bIsMounted)
		{
			bIsMounted = false;
			VirtualStockSettings.StockLerpValue = 0.0f;
			OnVirtualStockModeChanged.Broadcast(bIsMounted);
		}
	}

	// Check the grip lerp state, this it ouside of the secondary attach check below because it can change the result of it
	if ((Grip.SecondaryGripInfo.bHasSecondaryAttachment && Grip.SecondaryGripInfo.SecondaryAttachment) || Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
	{
		switch (Grip.SecondaryGripInfo.GripLerpState)
		{
		case EGripLerpState::StartLerp:
		case EGripLerpState::EndLerp:
		{
			if (Grip.SecondaryGripInfo.curLerp > 0.01f)
				Grip.SecondaryGripInfo.curLerp -= DeltaTime;
			else
			{
				Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::NotLerping;
			}

		}break;
		//case EGripLerpState::ConstantLerp_DEPRECATED:
		case EGripLerpState::NotLerping:
		default:break;
		}
	}

	// Handle the interp and multi grip situations, re-checking the grip situation here as it may have changed in the switch above.
	if ((Grip.SecondaryGripInfo.bHasSecondaryAttachment && Grip.SecondaryGripInfo.SecondaryAttachment) || Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
	{
		FTransform NewWorldTransform = WorldTransform;
		FTransform SecondaryTransform = Grip.RelativeTransform * ParentTransform;

		// Checking secondary grip type for the scaling setting
		ESecondaryGripType SecondaryType = ESecondaryGripType::SG_None;

		if (bRootHasInterface)
			SecondaryType = IVRGripInterface::Execute_SecondaryGripType(root);
		else if (bActorHasInterface)
			SecondaryType = IVRGripInterface::Execute_SecondaryGripType(actor);

		// If the grip is a custom one, skip all of this logic we won't be changing anything
		if (SecondaryType != ESecondaryGripType::SG_Custom)
		{
			// Variables needed for multi grip transform
			FVector BasePoint = ParentTransform.GetLocation();
			FVector Pivot = ParentTransform.GetLocation();

			if (Grip.bIsSlotGrip)
			{			
				if (FBPActorPhysicsHandleInformation * PhysHandle = GrippingController->GetPhysicsGrip(Grip))
				{
					Pivot = SecondaryTransform.TransformPositionNoScale(SecondaryTransform.InverseTransformPositionNoScale(Pivot) + (StoredRootOffset * PhysHandle->RootBoneRotation.GetRotation()).RotateVector(PivotOffset));
				}
				else
				{
					Pivot = SecondaryTransform.TransformPositionNoScale(SecondaryTransform.InverseTransformPositionNoScale(Pivot) + OrientationComponentRelativeFacing.GetRotation().RotateVector(PivotOffset));
				}
			}

			// Debug draw for COM movement with physics grips
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			static const auto CVarDrawCOMDebugSpheresAccess = IConsoleManager::Get().FindConsoleVariable(TEXT("vr.DrawDebugCenterOfMassForGrips"));
			if (CVarDrawCOMDebugSpheresAccess->GetInt() > 0)
			{
				DrawDebugSphere(GetWorld(), Pivot, 5, 32, FColor::Orange, false);
			}
#endif

			const FTransform PivotToWorld = FTransform(FQuat::Identity, Pivot);//BasePoint);
			const FTransform WorldToPivot = FTransform(FQuat::Identity, -Pivot);//-BasePoint);

			FVector frontLocOrig;
			FVector frontLoc;

			// Ending lerp out of a multi grip
			if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
			{
				WorldTransform.Blend(WorldTransform, RelativeTransOnSecondaryRelease* GrippingController->GetPivotTransform(), FMath::Clamp(Grip.SecondaryGripInfo.curLerp / Grip.SecondaryGripInfo.LerpToRate, 0.0f, 1.0f));
				return true;
			}
			else // Is in a multi grip, might be lerping into it as well.
			{
				//FVector curLocation; // Current location of the secondary grip

				// Calculates the correct secondary attachment location and sets frontLoc to it
				CalculateSecondaryLocation(frontLoc, BasePoint, Grip, GrippingController);

				frontLocOrig = (/*WorldTransform*/SecondaryTransform.TransformPosition(Grip.SecondaryGripInfo.SecondaryRelativeTransform.GetLocation())) - BasePoint;

				// Apply any smoothing settings and lerping in / constant lerping
				GunTools_ApplySmoothingAndLerp(Grip, frontLoc, frontLocOrig, DeltaTime, bSkipHighQualityOperations);

				Grip.SecondaryGripInfo.LastRelativeLocation = frontLoc;
			}

			// Get any scaling addition from a scaling secondary grip type
			FVector Scaler = FVector(1.0f);
			GetAnyScaling(Scaler, Grip, frontLoc, frontLocOrig, SecondaryType, SecondaryTransform);

			Grip.SecondaryGripInfo.SecondaryGripDistance = FVector::Dist(frontLocOrig, frontLoc);

			if (!bSkipHighQualityOperations && AdvSecondarySettings.bUseAdvancedSecondarySettings && AdvSecondarySettings.bUseSecondaryGripDistanceInfluence)
			{
				float rotScaler = 1.0f - FMath::Clamp((Grip.SecondaryGripInfo.SecondaryGripDistance - AdvSecondarySettings.GripInfluenceDeadZone) / FMath::Max(AdvSecondarySettings.GripInfluenceDistanceToZero, 1.0f), 0.0f, 1.0f);
				frontLoc = FMath::Lerp(frontLocOrig, frontLoc, rotScaler);
			}

			// Skip rot val for scaling only
			if (SecondaryType != ESecondaryGripType::SG_ScalingOnly)
			{
				// Get shoulder mount addition rotation
				if (!bSkipHighQualityOperations && bUseVirtualStock && bIsMounted)
				{
					// Get the rotation difference from the initial second grip
					FQuat rotVal = FQuat::FindBetweenVectors(GrippingController->GetPivotLocation() - MountWorldTransform.GetTranslation(), (frontLoc + BasePoint) - MountWorldTransform.GetTranslation());
					FQuat MountAdditionRotation = FQuat::FindBetweenVectors(frontLocOrig, GrippingController->GetPivotLocation() - MountWorldTransform.GetTranslation());

					if (VirtualStockSettings.StockLerpValue < 1.0f)
					{
						// Rebase the world transform to the pivot point, add the rotation, remove the pivot point rebase
						FTransform NA = FTransform(rotVal * MountAdditionRotation, FVector::ZeroVector, Scaler);
						FTransform NB = FTransform(FQuat::FindBetweenVectors(frontLocOrig, frontLoc), FVector::ZeroVector, Scaler);
						NA.NormalizeRotation();
						NB.NormalizeRotation();

						// Quaternion interpolation
						NA.Blend(NB, NA, VirtualStockSettings.StockLerpValue);

						NewWorldTransform = WorldTransform * WorldToPivot * NA * PivotToWorld;
					}
					else
					{
						// Rebase the world transform to the pivot point, add the rotation, remove the pivot point rebase
						NewWorldTransform = WorldTransform * WorldToPivot * MountAdditionRotation * FTransform(rotVal, FVector::ZeroVector, Scaler) * PivotToWorld;
					}
				}
				else
				{
					// Get the rotation difference from the initial second grip
					FQuat rotVal = FQuat::FindBetweenVectors(frontLocOrig, frontLoc);

					// Rebase the world transform to the pivot point, add the rotation, remove the pivot point rebase
					NewWorldTransform = WorldTransform * WorldToPivot * FTransform(rotVal, FVector::ZeroVector, Scaler) * PivotToWorld;
				}
			}
			else
			{

				// Get shoulder mount addition rotation
				if (!bSkipHighQualityOperations && bUseVirtualStock && bIsMounted)
				{
					FQuat MountAdditionRotation = FQuat::FindBetweenVectors(frontLocOrig, GrippingController->GetPivotLocation() - MountWorldTransform.GetTranslation());

					// If it is exactly 1.0f then lets skip all of the extra logic and just set it
					if (VirtualStockSettings.StockLerpValue < 1.0f)
					{
						FTransform NA = FTransform(MountAdditionRotation, FVector::ZeroVector, Scaler);
						FTransform NB = FTransform(FQuat::Identity, FVector::ZeroVector, Scaler);
						NA.NormalizeRotation();
						NB.NormalizeRotation();

						// Quaternion interpolation
						NA.Blend(NB, NA, VirtualStockSettings.StockLerpValue);
						NewWorldTransform = WorldTransform * WorldToPivot * NA * PivotToWorld;
					}
					else
					{
						// Rebase the world transform to the pivot point, add the scaler, remove the pivot point rebase
						NewWorldTransform = WorldTransform * WorldToPivot * MountAdditionRotation * FTransform(FQuat::Identity, FVector::ZeroVector, Scaler) * PivotToWorld;
					}
				}
				else
				{
					// Rebase the world transform to the pivot point, add the scaler, remove the pivot point rebase
					NewWorldTransform = WorldTransform * WorldToPivot * FTransform(FQuat::Identity, FVector::ZeroVector, Scaler) * PivotToWorld;
				}
			}
		}

		if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::StartLerp)
		{
			WorldTransform.Blend(NewWorldTransform, WorldTransform, FMath::Clamp(Grip.SecondaryGripInfo.curLerp / Grip.SecondaryGripInfo.LerpToRate, 0.0f, 1.0f));
		}
		else
		{
			WorldTransform = NewWorldTransform;
		}

		if (bIsMounted && VirtualStockSettings.bSmoothStockHand)
		{
			if (GrippingController->GetAttachParent())
			{
				FTransform ParentTrans = GrippingController->GetAttachParent()->GetComponentTransform();
				FTransform ParentRel = WorldTransform * ParentTrans.Inverse();
				ParentRel.Blend(ParentRel, VirtualStockSettings.StockHandSmoothing.RunFilterSmoothing(ParentRel, DeltaTime), VirtualStockSettings.SmoothingValueForStock);
				WorldTransform = ParentRel * ParentTrans;
			}
		}

		if (Grip.SecondaryGripInfo.bHasSecondaryAttachment)
		{
			RelativeTransOnSecondaryRelease = WorldTransform.GetRelativeTransform(GrippingController->GetPivotTransform());
		}
	}

	return true;
}

void UGS_GunTools::OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation)
{

	if (bUseGlobalVirtualStockSettings)
	{
		if (GrippingController->IsLocallyControlled())
		{
			FBPVirtualStockSettings VirtualSettings;
			UVRGlobalSettings::GetVirtualStockGlobalSettings(VirtualSettings);
			VirtualStockSettings.CopyFrom(VirtualSettings);
		}
	}

	// Super doesn't do anything on grip

	// Reset smoothing filters
	if (AdvSecondarySettings.bUseConstantGripScaler)
	{
		if (AdvSecondarySettings.bUseGlobalSmoothingSettings)
		{
			const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
			AdvSecondarySettings.SecondarySmoothing.CutoffSlope = VRSettings.OneEuroCutoffSlope;
			AdvSecondarySettings.SecondarySmoothing.DeltaCutoff = VRSettings.OneEuroDeltaCutoff;
			AdvSecondarySettings.SecondarySmoothing.MinCutoff = VRSettings.OneEuroMinCutoff;
		}

		AdvSecondarySettings.SecondarySmoothing.ResetSmoothingFilter();
	}

	if (bUseVirtualStock)
	{
		ResetStockVariables();
	}

	GetVirtualStockTarget(GrippingController);
}

void UGS_GunTools::GetVirtualStockTarget(UGripMotionControllerComponent * GrippingController)
{
	if (GrippingController && (GrippingController->HasAuthority() || bUseHighQualityRemoteSimulation))
	{
		if (AVRBaseCharacter * vrOwner = Cast<AVRBaseCharacter>(GrippingController->GetOwner()))
		{
			if (vrOwner->VRReplicatedCamera)
			{
				CameraComponent = vrOwner->VRReplicatedCamera;
				return;
			}
		}
		else
		{
			TArray<USceneComponent*> children = GrippingController->GetOwner()->GetRootComponent()->GetAttachChildren();

			for (int i = 0; i < children.Num(); i++)
			{
				if (children[i]->IsA(UCameraComponent::StaticClass()))
				{
					CameraComponent = children[i];
					return;
				}
			}
		}

		CameraComponent = nullptr;
	}
}

void UGS_GunTools::OnSecondaryGrip_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation)
{
	// Super doesn't do anything on Secondary grip

	// Reset smoothing filters
	if (AdvSecondarySettings.bUseConstantGripScaler)
	{
		if (AdvSecondarySettings.bUseGlobalSmoothingSettings)
		{
			const UVRGlobalSettings& VRSettings = *GetDefault<UVRGlobalSettings>();
			AdvSecondarySettings.SecondarySmoothing.CutoffSlope = VRSettings.OneEuroCutoffSlope;
			AdvSecondarySettings.SecondarySmoothing.DeltaCutoff = VRSettings.OneEuroDeltaCutoff;
			AdvSecondarySettings.SecondarySmoothing.MinCutoff = VRSettings.OneEuroMinCutoff;
		}

		AdvSecondarySettings.SecondarySmoothing.ResetSmoothingFilter();
	}

	if (bUseVirtualStock)
		ResetStockVariables();
}

void UGS_GunTools::ResetRecoil()
{
	BackEndRecoilStorage = FTransform::Identity;
	BackEndRecoilTarget = FTransform::Identity;
}

void UGS_GunTools::AddRecoilInstance(const FTransform & RecoilAddition, FVector Optional_Location)
{
	if (!bHasRecoil)
		return;

	if (bApplyRecoilAsPhysicalForce)
	{		
		if (FBodyInstance * BodyInst = GetParentBodyInstance())
		{
			BodyInst->AddImpulseAtPosition(RecoilAddition.GetLocation(), Optional_Location);
		}
	}
	else
	{
		BackEndRecoilTarget += RecoilAddition;

		FVector CurVec = BackEndRecoilTarget.GetTranslation();
		CurVec.X = FMath::Clamp(CurVec.X, -FMath::Abs(MaxRecoilTranslation.X), FMath::Abs(MaxRecoilTranslation.X));
		CurVec.Y = FMath::Clamp(CurVec.Y, -FMath::Abs(MaxRecoilTranslation.Y), FMath::Abs(MaxRecoilTranslation.Y));
		CurVec.Z = FMath::Clamp(CurVec.Z, -FMath::Abs(MaxRecoilTranslation.Z), FMath::Abs(MaxRecoilTranslation.Z));
		BackEndRecoilTarget.SetTranslation(CurVec);

		FVector CurScale = BackEndRecoilTarget.GetScale3D();
		CurScale.X = FMath::Clamp(CurScale.X, -FMath::Abs(MaxRecoilScale.X), FMath::Abs(MaxRecoilScale.X));
		CurScale.Y = FMath::Clamp(CurScale.Y, -FMath::Abs(MaxRecoilScale.Y), FMath::Abs(MaxRecoilScale.Y));
		CurScale.Z = FMath::Clamp(CurScale.Z, -FMath::Abs(MaxRecoilScale.Z), FMath::Abs(MaxRecoilScale.Z));
		BackEndRecoilTarget.SetScale3D(CurScale);

		FRotator curRot = BackEndRecoilTarget.Rotator();
		curRot.Pitch = FMath::Clamp(curRot.Pitch, -FMath::Abs(MaxRecoilRotation.Y), FMath::Abs(MaxRecoilRotation.Y));
		curRot.Yaw = FMath::Clamp(curRot.Yaw, -FMath::Abs(MaxRecoilRotation.Z), FMath::Abs(MaxRecoilRotation.Z));
		curRot.Roll = FMath::Clamp(curRot.Roll, -FMath::Abs(MaxRecoilRotation.X), FMath::Abs(MaxRecoilRotation.X));
		BackEndRecoilTarget.SetRotation(curRot.Quaternion());

		bHasActiveRecoil = !BackEndRecoilTarget.Equals(FTransform::Identity);
	}
}
