// Fill out your copyright notice in the Description page of Project Settings.

#include "GripScripts/GS_Melee.h"
#include "VRGripInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsConstraintActor.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "GripMotionControllerComponent.h"

UGS_Melee::UGS_Melee(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer)
{
	bIsActive = true;
	WorldTransformOverrideType = EGSTransformOverrideType::ModifiesWorldTransform;

	bInjectPrePhysicsHandle = true;
	bInjectPostPhysicsHandle = true;
	WeaponRootOrientationComponent = NAME_None;
	OrientationComponentRelativeFacing = FTransform::Identity;

	bAutoSetPrimaryAndSecondaryHands = true;
	bPrimaryHandInRear = true;

	RollingVelocityAverage = FVector::ZeroVector;
	NumberOfFramesToAverageVelocity = 1;
	bIsLodged = false;

	bCanEverTick = true;
	bAlwaysTickPenetration = true;
	bCOMBetweenHands = false;
	bSkipGripMassChecks = true;
	bOnlyPenetrateWithTwoHands = false;
}

void UGS_Melee::UpdateDualHandInfo()
{
	TArray<FBPGripPair> HoldingControllers;
	bool bIsHeld;
	IVRGripInterface::Execute_IsHeld(GetParent(), HoldingControllers, bIsHeld);

	float PHand = 0.0f;
	float SHand = 0.0f;

	FBPActorGripInformation* FrontHandGrip = nullptr;
	FBPActorGripInformation* RearHandGrip = nullptr;

	SecondaryHand = FBPGripPair();
	PrimaryHand = FBPGripPair();

	int NumControllers = HoldingControllers.Num();

	for (FBPGripPair& Grip : HoldingControllers)
	{
		if (NumControllers > 1)
		{
			FBPActorGripInformation* GripInfo = Grip.HoldingController->GetGripPtrByID(Grip.GripID);
			if (GripInfo)
			{
				float GripDistanceOnPrimaryAxis = 0.f;
				FTransform relTransform(GripInfo->RelativeTransform.ToInverseMatrixWithScale());
				relTransform = relTransform.GetRelativeTransform(OrientationComponentRelativeFacing);

				// This is the Forward vector projected transform
				// The most negative one of these is the rearmost hand
				FVector localLoc = relTransform.GetTranslation();

				if ((bPrimaryHandInRear ? localLoc.X < PHand : localLoc.X > PHand) || !PrimaryHand.HoldingController)
				{
					PrimaryHand = Grip;
					PHand = localLoc.X;
				}

				if (((bPrimaryHandInRear ? localLoc.X > SHand : localLoc.X < SHand) || !SecondaryHand.HoldingController || SecondaryHand.HoldingController == PrimaryHand.HoldingController))
				{
					SecondaryHand = Grip;
					SHand = localLoc.X;
				}
			}
		}
		else
		{
			PrimaryHand = Grip;
			SecondaryHand = FBPGripPair();
		}
	}


	if (PrimaryHand.IsValid() && bCOMBetweenHands)
	{
		FBPActorGripInformation* GripInfo = PrimaryHand.HoldingController->GetGripPtrByID(PrimaryHand.GripID);

		if (SecondaryHand.IsValid())
		{
			FBPActorGripInformation* GripInfoS = SecondaryHand.HoldingController->GetGripPtrByID(SecondaryHand.GripID);

			if (GripInfo && GripInfoS)
			{
				FVector Primary = GripInfo->RelativeTransform.InverseTransformPositionNoScale(FVector::ZeroVector);
				FVector Secondary = GripInfoS->RelativeTransform.InverseTransformPositionNoScale(FVector::ZeroVector);

				FVector Final = ((Primary + Secondary) / 2.f);
				ObjectRelativeGripCenter.SetLocation(Final);
			}
		}
		else
		{
			if (GripInfo)
			{
			
				if (GripInfo->SecondaryGripInfo.bHasSecondaryAttachment)
				{
					FVector gripLoc = GripInfo->RelativeTransform.InverseTransformPositionNoScale(FVector::ZeroVector);
					FVector secGripLoc = GripInfo->SecondaryGripInfo.SecondaryRelativeTransform.GetLocation();
					FVector finalloc = (gripLoc + secGripLoc) / 2.f;
					FVector finalScaled = finalloc * GripInfo->RelativeTransform.GetScale3D();

					FTransform ownerTrans = GetOwner()->GetActorTransform();

					DrawDebugSphere(GetWorld(), ownerTrans.TransformPosition(finalScaled), 4.0f, 32, FColor::Orange, true);


					ObjectRelativeGripCenter.SetLocation(finalScaled);
					PrimaryHand.HoldingController->ReCreateGrip(*GripInfo);
				}
				else
				{
					ObjectRelativeGripCenter = FTransform::Identity;
				}
			}
		}
	}
}

void UGS_Melee::SetPrimaryAndSecondaryHands(FBPGripPair& PrimaryGrip, FBPGripPair& SecondaryGrip)
{

}

void UGS_Melee::OnSecondaryGrip_Implementation(UGripMotionControllerComponent* Controller, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation)
{
	if (!bIsActive)
		return;

	UpdateDualHandInfo();
}

void UGS_Melee::OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation)
{
	if (!bIsActive)
		return;

	// Not storing an id, we should only be doing this once
//	GetOwner()->OnActorHit.AddDynamic(this, &UGS_Melee::OnActorHit);

	// This lets us change the grip settings prior to actually starting the grip off
	SetTickEnabled(true);

	//if (GrippingController->HasGripAuthority(GripInformation))
	{
		UpdateDualHandInfo();

		// If we have multiple hands then alter the grip settings here for what we have already, the other will wait until post event
		if (SecondaryHand.IsValid())
		{

			FBPActorGripInformation * GripInfo = SecondaryHand.HoldingController->GetGripPtrByID(SecondaryHand.GripID);
			GripInfo->AdvancedGripSettings.PhysicsSettings.PhysicsGripLocationSettings = EPhysicsGripCOMType::COM_GripAtControllerLoc;

			FBPActorPhysicsHandleInformation* HandleInfo = SecondaryHand.HoldingController->GetPhysicsGrip(SecondaryHand.GripID);
			if (HandleInfo)
			{
				SecondaryHandPhysicsSettings.FillTo(HandleInfo);
				SecondaryHand.HoldingController->UpdatePhysicsHandle(SecondaryHand.GripID, true);
			}


			GripInfo = PrimaryHand.HoldingController->GetGripPtrByID(PrimaryHand.GripID);

			if(bCOMBetweenHands)
				GripInfo->AdvancedGripSettings.PhysicsSettings.PhysicsGripLocationSettings = EPhysicsGripCOMType::COM_GripAtControllerLoc;
			//else
			//	GripInfo->AdvancedGripSettings.PhysicsSettings.PhysicsGripLocationSettings = EPhysicsGripCOMType::COM_SetAndGripAt;


			HandleInfo = PrimaryHand.HoldingController->GetPhysicsGrip(PrimaryHand.GripID);
			if (HandleInfo)
			{
				PrimaryHandPhysicsSettings.FillTo(HandleInfo);
				PrimaryHand.HoldingController->UpdatePhysicsHandle(PrimaryHand.GripID, true);
			}
		}
	}
}

void UGS_Melee::OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed)
{

	if (!bIsActive)
		return;

	if(!bAlwaysTickPenetration)
		SetTickEnabled(false);

	if (SecondaryHand.IsValid() && SecondaryHand.HoldingController == ReleasingController && SecondaryHand.GripID == GripInformation.GripID)
	{
		SecondaryHand = FBPGripPair();
	}
	else if (PrimaryHand.IsValid() && PrimaryHand.HoldingController == ReleasingController && PrimaryHand.GripID == GripInformation.GripID)
	{
		if (SecondaryHand.IsValid())
		{
			PrimaryHand = SecondaryHand;
			SecondaryHand = FBPGripPair();
		}
		else
		{
			PrimaryHand = FBPGripPair();
		}
	}


	if (PrimaryHand.IsValid())
	{

		FBPActorPhysicsHandleInformation* HandleInfo = PrimaryHand.HoldingController->GetPhysicsGrip(PrimaryHand.GripID);
		if (HandleInfo)
		{
			FBPActorGripInformation * GripInfo = PrimaryHand.HoldingController->GetGripPtrByID(PrimaryHand.GripID);

			if (GripInfo)
			{

				//Reset defaults here still!!!
				HandleInfo->LinConstraint.XDrive.bEnablePositionDrive = true;
				HandleInfo->LinConstraint.XDrive.bEnableVelocityDrive = true;
				HandleInfo->LinConstraint.XDrive.Stiffness = GripInfo->Stiffness;
				HandleInfo->LinConstraint.XDrive.Damping = GripInfo->Damping;

				HandleInfo->LinConstraint.YDrive = HandleInfo->LinConstraint.XDrive;
				HandleInfo->LinConstraint.ZDrive = HandleInfo->LinConstraint.XDrive;

				HandleInfo->AngConstraint.SwingDrive.bEnablePositionDrive = false;
				HandleInfo->AngConstraint.SwingDrive.bEnableVelocityDrive = false;
				HandleInfo->AngConstraint.TwistDrive.bEnablePositionDrive = false;
				HandleInfo->AngConstraint.TwistDrive.bEnableVelocityDrive = false;
				HandleInfo->AngConstraint.AngularDriveMode = EAngularDriveMode::SLERP;
				HandleInfo->AngConstraint.SlerpDrive.bEnablePositionDrive = true;
				HandleInfo->AngConstraint.SlerpDrive.bEnableVelocityDrive = true;

				if (GripInfo->AdvancedGripSettings.PhysicsSettings.bUsePhysicsSettings && GripInfo->AdvancedGripSettings.PhysicsSettings.bUseCustomAngularValues)
				{
					HandleInfo->AngConstraint.SlerpDrive.Damping = GripInfo->AdvancedGripSettings.PhysicsSettings.AngularDamping;
					HandleInfo->AngConstraint.SlerpDrive.Stiffness = GripInfo->AdvancedGripSettings.PhysicsSettings.AngularStiffness;
				}
				else
				{
					HandleInfo->AngConstraint.SlerpDrive.Damping = GripInfo->Damping * 1.4f;
					HandleInfo->AngConstraint.SlerpDrive.Stiffness = GripInfo->Stiffness * 1.5f;
				}

				FBPAdvGripSettings AdvSettings = IVRGripInterface::Execute_AdvancedGripSettings(GripInfo->GrippedObject);
				GripInfo->AdvancedGripSettings.PhysicsSettings.PhysicsGripLocationSettings = EPhysicsGripCOMType::COM_GripAtControllerLoc; //AdvSettings.PhysicsSettings.PhysicsGripLocationSettings;

				//GripInfo->AdvancedGripSettings.PhysicsSettings.PhysicsGripLocationSettings = EPhysicsGripCOMType::COM_SetAndGripAt;

				PrimaryHand.HoldingController->UpdatePhysicsHandle(PrimaryHand.GripID, true);
			}
		}
	}

}

void UGS_Melee::OnBeginPlay_Implementation(UObject * CallingOwner)
{
	// Grip base has no super of this

	if (AActor * Owner = GetOwner())
	{
		FName CurrentCompName = NAME_None;
		bool bSearchRootComp = WeaponRootOrientationComponent.IsValid();
		int RemainingCount = PenetrationNotifierComponents.Num();
		for (UActorComponent* ChildComp : Owner->GetComponents())
		{
			CurrentCompName = ChildComp->GetFName();
			if (CurrentCompName == NAME_None)
				continue;

			if (bSearchRootComp && CurrentCompName == WeaponRootOrientationComponent)
			{
				bSearchRootComp = false;
				if (USceneComponent * SceneComp = Cast<USceneComponent>(ChildComp))
				{
					OrientationComponentRelativeFacing = SceneComp->GetRelativeTransform();
				}
			}

			if (FBPLodgeComponentInfo * Found = PenetrationNotifierComponents.FindByKey(CurrentCompName))
			{
				if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ChildComp))
				{
					Found->TargetComponent = TWeakObjectPtr<UPrimitiveComponent>(PrimComp);
					//PrimComp->OnComponentHit.AddDynamic(this, &UGS_Melee::OnLodgeHitCallback);
				}

				// Decrement even if it failed the cast, they just had it wrong.
				RemainingCount--;
			}

			if (!bSearchRootComp && RemainingCount < 1)
			{
				break;
			}
		}

		// If we found at least one penetration object
		if (RemainingCount < PenetrationNotifierComponents.Num())
		{
			Owner->OnActorHit.AddDynamic(this, &UGS_Melee::OnLodgeHitCallback);
		}
	}
}

void UGS_Melee::OnEndPlay_Implementation(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor * Owner = GetOwner())
	{
		Owner->OnActorHit.RemoveDynamic(this, &UGS_Melee::OnLodgeHitCallback);
	}
}

void UGS_Melee::OnLodgeHitCallback(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bIsActive || bIsLodged || OtherActor == SelfActor)
		return;

	if (bOnlyPenetrateWithTwoHands && !SecondaryHand.IsValid())
		return;

	if (UPrimitiveComponent * root = Cast<UPrimitiveComponent>(SelfActor->GetRootComponent()))
	{	
		if (FBodyInstance * rBodyInstance = root->GetBodyInstance())
		{
			RollingVelocityAverage += FVector::CrossProduct(RollingAngVelocityAverage, Hit.ImpactPoint - rBodyInstance->GetCOMPosition());
		}
	}

	FVector FrameToFrameVelocity = RollingVelocityAverage;

	// If we hit, then regardless of penetration, end the velocity history and reset
	RollingVelocityAverage = FVector::ZeroVector;
	RollingAngVelocityAverage = FVector::ZeroVector;


	for(FBPLodgeComponentInfo &LodgeData : PenetrationNotifierComponents)
	{
		if (!LodgeData.TargetComponent.IsValid())
			continue;

		FBox LodgeBox = LodgeData.TargetComponent->Bounds.GetBox();
		if (LodgeData.TargetComponent.IsValid() && LodgeBox.IsInsideOrOn(Hit.ImpactPoint))
		{
			FVector ForwardVec = LodgeData.TargetComponent->GetForwardVector();

			// Using swept objects hit normal as we are looking for a facing from ourselves
			float DotValue = FMath::Abs(FVector::DotProduct(Hit.Normal, ForwardVec));
			FVector Velocity = FrameToFrameVelocity.ProjectOnToNormal(ForwardVec);

			// Check if the velocity was strong enough along our axis to count as a lodge event
			// Also that our facing was in the relatively correct direction
			if(DotValue >= (1.0f - LodgeData.AcceptableForwardProductRange) && Velocity.SizeSquared() >= FMath::Square(LodgeData.PenetrationVelocity))
			{
				OnShouldLodgeInObject.Broadcast(LodgeData, OtherActor, Hit.GetComponent(), Hit.GetComponent()->GetCollisionObjectType(), NormalImpulse, Hit);
				break;
			}
		}
	}
}


void UGS_Melee::HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation * HandleInfo, FTransform & KinPose)
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
}

void UGS_Melee::HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation * HandleInfo)
{
	if (!bIsActive)
		return;

	if(bSkipGripMassChecks)
		HandleInfo->bSkipMassCheck = true;

	if (SecondaryHand.IsValid())
	{
		if (GrippingController == SecondaryHand.HoldingController && HandleInfo->GripID == SecondaryHand.GripID)
		{
			SecondaryHandPhysicsSettings.FillTo(HandleInfo);
		}
		else if (GrippingController == PrimaryHand.HoldingController && HandleInfo->GripID == PrimaryHand.GripID)
		{
			PrimaryHandPhysicsSettings.FillTo(HandleInfo);
		}

		if (bCOMBetweenHands && SecondaryHand.IsValid())
		{
			if (UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(GetParentSceneComp()))
			{
				if (FBodyInstance * rBodyInstance = PrimComp->GetBodyInstance())
				{
					FPhysicsCommand::ExecuteWrite(rBodyInstance->ActorHandle, [&](const FPhysicsActorHandle& Actor)
						{
							FTransform localCom = FPhysicsInterface::GetComTransformLocal_AssumesLocked(Actor);
							localCom.SetLocation((HandleInfo->RootBoneRotation * ObjectRelativeGripCenter).GetLocation());
							FPhysicsInterface::SetComLocalPose_AssumesLocked(Actor, localCom);
							HandleInfo->bSetCOM = true; // Should i remove this?
							HandleInfo->bSkipResettingCom = true;
						});
				}
			}
		}
	}
}

void UGS_Melee::Tick(float DeltaTime)
{
	AActor* myOwner = GetOwner();

	if (UPrimitiveComponent * root = Cast<UPrimitiveComponent>(myOwner->GetRootComponent()))
	{
		FBodyInstance* rBodyInstance = root->GetBodyInstance();
		if (rBodyInstance && rBodyInstance->IsValidBodyInstance())
		{
			UVRExpansionFunctionLibrary::LowPassFilter_RollingAverage(RollingVelocityAverage, rBodyInstance->GetUnrealWorldVelocity(), RollingVelocityAverage, NumberOfFramesToAverageVelocity);
			UVRExpansionFunctionLibrary::LowPassFilter_RollingAverage(RollingAngVelocityAverage, rBodyInstance->GetUnrealWorldAngularVelocityInRadians(), RollingAngVelocityAverage, NumberOfFramesToAverageVelocity);
		}
	}
}

bool UGS_Melee::GetWorldTransform_Implementation
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

	// Just simple transform setting
	WorldTransform = Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;
	if (Grip.SecondaryGripInfo.bHasSecondaryAttachment)
	{
		WorldTransform.SetLocation((GrippingController->GetPivotLocation() + Grip.SecondaryGripInfo.SecondaryAttachment->GetComponentLocation()) / 2.f);
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
				/*if (Grip.SecondaryGripInfo.bHasSecondaryAttachment &&
					Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripSettings &&
					Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler_DEPRECATED < 1.0f)
				{
					Grip.SecondaryGripInfo.GripLerpState = EGripLerpState::ConstantLerp_DEPRECATED;
				}
				else*/
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
			FVector BasePoint = ParentTransform.GetLocation(); // Get our pivot point
			const FTransform PivotToWorld = FTransform(FQuat::Identity, BasePoint);
			const FTransform WorldToPivot = FTransform(FQuat::Identity, -BasePoint);

			FVector frontLocOrig;
			FVector frontLoc;

			// Ending lerp out of a multi grip
			if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::EndLerp)
			{
				frontLocOrig = (/*WorldTransform*/SecondaryTransform.TransformPosition(Grip.SecondaryGripInfo.SecondaryRelativeTransform.GetLocation())) - BasePoint;
				frontLoc = Grip.SecondaryGripInfo.LastRelativeLocation;

				frontLocOrig = FMath::Lerp(frontLoc, frontLocOrig, FMath::Clamp(Grip.SecondaryGripInfo.curLerp / Grip.SecondaryGripInfo.LerpToRate, 0.0f, 1.0f));
			}
			else // Is in a multi grip, might be lerping into it as well.
			{
				//FVector curLocation; // Current location of the secondary grip
				
				bool bPulledControllerLoc = false;
				if (GrippingController->bHasAuthority && Grip.SecondaryGripInfo.SecondaryAttachment->GetOwner() == GrippingController->GetOwner())
				{
					if (UGripMotionControllerComponent * OtherController = Cast<UGripMotionControllerComponent>(Grip.SecondaryGripInfo.SecondaryAttachment))
					{
						if (!OtherController->bUseWithoutTracking)
						{
							FVector Position = FVector::ZeroVector;
							FRotator Orientation = FRotator::ZeroRotator;
							float WorldToMeters = GetWorld() ? GetWorld()->GetWorldSettings()->WorldToMeters : 100.0f;
							if (OtherController->GripPollControllerState(Position, Orientation, WorldToMeters))
							{
								frontLoc = OtherController->CalcControllerComponentToWorld(Orientation, Position).GetLocation() - BasePoint;
								///*curLocation*/ frontLoc = OtherController->CalcNewComponentToWorld(FTransform(Orientation, Position)).GetLocation() - BasePoint;
								bPulledControllerLoc = true;
							}
						}
					}
				}

				if (!bPulledControllerLoc)
					/*curLocation*/ frontLoc = Grip.SecondaryGripInfo.SecondaryAttachment->GetComponentLocation() - BasePoint;

				frontLocOrig = (/*WorldTransform*/SecondaryTransform.TransformPosition(Grip.SecondaryGripInfo.SecondaryRelativeTransform.GetLocation())) - BasePoint;

				// Apply any smoothing settings and lerping in / constant lerping
				ApplySmoothingAndLerp(Grip, frontLoc, frontLocOrig, DeltaTime);

				Grip.SecondaryGripInfo.LastRelativeLocation = frontLoc;
			}

			// Get any scaling addition from a scaling secondary grip type
			FVector Scaler = FVector(1.0f);
			if (SecondaryType == ESecondaryGripType::SG_FreeWithScaling_Retain || SecondaryType == ESecondaryGripType::SG_SlotOnlyWithScaling_Retain || SecondaryType == ESecondaryGripType::SG_ScalingOnly)
			{
				GetAnyScaling(Scaler, Grip, frontLoc, frontLocOrig, SecondaryType, SecondaryTransform);
			}

			Grip.SecondaryGripInfo.SecondaryGripDistance = FVector::Dist(frontLocOrig, frontLoc);

			/*if (Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripSettings && Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripDistanceInfluence_DEPRECATED)
			{
				float rotScaler = 1.0f - FMath::Clamp((Grip.SecondaryGripInfo.SecondaryGripDistance - Grip.AdvancedGripSettings.SecondaryGripSettings.GripInfluenceDeadZone_DEPRECATED) / FMath::Max(Grip.AdvancedGripSettings.SecondaryGripSettings.GripInfluenceDistanceToZero_DEPRECATED, 1.0f), 0.0f, 1.0f);
				frontLoc = FMath::Lerp(frontLocOrig, frontLoc, rotScaler);
			}*/

			// Skip rot val for scaling only
			if (SecondaryType != ESecondaryGripType::SG_ScalingOnly)
			{
				// Get the rotation difference from the initial second grip
				FQuat rotVal = FQuat::FindBetweenVectors(frontLocOrig, frontLoc);

				// Rebase the world transform to the pivot point, add the rotation, remove the pivot point rebase
				WorldTransform = WorldTransform * WorldToPivot * FTransform(rotVal, FVector::ZeroVector, Scaler) * PivotToWorld;
			}
			else
			{
				// Rebase the world transform to the pivot point, add the scaler, remove the pivot point rebase
				WorldTransform = WorldTransform * WorldToPivot * FTransform(FQuat::Identity, FVector::ZeroVector, Scaler) * PivotToWorld;
			}
		}


		// Fixup X rotation to keep it aligned with the primary hand.
		//WorldTransform = Grip.RelativeTransform * Grip.AdditionTransform * ParentTransform;
		
		// Get primary hand relative position
		FTransform InverseTrans(Grip.RelativeTransform.ToInverseMatrixWithScale());

		// Get the original location of the hand
		FVector origLocation = InverseTrans.GetLocation();

		FVector orientedvector = FVector::VectorPlaneProject(origLocation, -OrientationComponentRelativeFacing.GetRotation().GetForwardVector());
		FVector newLocation = FVector::VectorPlaneProject(WorldTransform.InverseTransformPosition(GrippingController->GetPivotLocation()), OrientationComponentRelativeFacing.GetRotation().GetForwardVector());

		FQuat DeltaQuat = FQuat::FindBetweenVectors(orientedvector, newLocation);

		WorldTransform.SetRotation(DeltaQuat * WorldTransform.GetRotation());
	}

	return true;
}
