// Fill out your copyright notice in the Description page of Project Settings.
#include "VRExpansionFunctionLibrary.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"
#include "IHeadMountedDisplay.h"
#include "Grippables/HandSocketComponent.h"

#if WITH_CHAOS
#include "Chaos/ParticleHandle.h"
#include "Chaos/KinematicGeometryParticles.h"
#include "Chaos/ParticleHandle.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PBDRigidsSolver.h"
#endif

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

//General Log
DEFINE_LOG_CATEGORY(VRExpansionFunctionLibraryLog);

void UVRExpansionFunctionLibrary::SetObjectsIgnoreCollision(UPrimitiveComponent* Prim1, FName OptionalBoneName1, UPrimitiveComponent* Prim2, FName OptionalBoneName2, bool bIgnoreCollision)
{
	if (Prim1 && Prim2)
	{
		FBodyInstance *Inst1 = Prim1->GetBodyInstance(OptionalBoneName1);
		FBodyInstance *Inst2 = Prim2->GetBodyInstance(OptionalBoneName2);

		if (Inst1 && Inst2)
		{
			Inst1->SetContactModification(bIgnoreCollision);
			Inst2->SetContactModification(bIgnoreCollision);
			if (FPhysScene* PhysScene = Prim1->GetWorld()->GetPhysicsScene())
			{
#if WITH_CHAOS
				/*FContactModBodyInstancePair newContactPair;
				newContactPair.Actor1 = Inst1->ActorHandle;
				newContactPair.Actor2 = Inst2->ActorHandle;
				newContactPair.bBody1IgnoreEntireActor = false;
				newContactPair.bBody2IgnoreEntireActor = false;
				*/

				Chaos::FUniqueIdx ID0 = Inst1->ActorHandle->UniqueIdx();
				Chaos::FUniqueIdx ID1 = Inst2->ActorHandle->UniqueIdx();

				Chaos::FIgnoreCollisionManager& IgnoreCollisionManager = PhysScene->GetSolver()->GetEvolution()->GetBroadPhase().GetIgnoreCollisionManager();

				FPhysicsCommand::ExecuteWrite(PhysScene, [&]()
					{
						using namespace Chaos;

						if (bIgnoreCollision)
						{
							if (!IgnoreCollisionManager.IgnoresCollision(ID0, ID1))
							{
								TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = Inst1->ActorHandle->Handle()->CastToRigidParticle();
								TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = Inst2->ActorHandle->Handle()->CastToRigidParticle();

								if (ParticleHandle0 && ParticleHandle1)
								{
									ParticleHandle0->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
									IgnoreCollisionManager.AddIgnoreCollisionsFor(ID0, ID1);

									ParticleHandle1->AddCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
									IgnoreCollisionManager.AddIgnoreCollisionsFor(ID1, ID0);
								}
							}
						}
						else
						{
							if (IgnoreCollisionManager.IgnoresCollision(ID0, ID1))
							{
								TPBDRigidParticleHandle<FReal, 3>* ParticleHandle0 = Inst1->ActorHandle->Handle()->CastToRigidParticle();
								TPBDRigidParticleHandle<FReal, 3>* ParticleHandle1 = Inst2->ActorHandle->Handle()->CastToRigidParticle();

								if (ParticleHandle0 && ParticleHandle1)
								{
									IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID0, ID1);
									IgnoreCollisionManager.RemoveIgnoreCollisionsFor(ID1, ID0);

									if (IgnoreCollisionManager.NumIgnoredCollision(ID0) < 1)
									{
										ParticleHandle0->RemoveCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
									}

									if (IgnoreCollisionManager.NumIgnoredCollision(ID1) < 1)
									{
										ParticleHandle1->RemoveCollisionConstraintFlag(Chaos::ECollisionConstraintFlags::CCF_BroadPhaseIgnoreCollisions);
									}
								}
							}
						}
					});

#elif PHYSICS_INTERFACE_PHYSX
				if (PxScene* PScene = PhysScene->GetPxScene())
				{
					if (FCCDContactModifyCallbackVR* ContactCallback = (FCCDContactModifyCallbackVR*)PScene->getCCDContactModifyCallback())
					{
						FRWScopeLock(ContactCallback->RWAccessLock, FRWScopeLockType::SLT_Write);
						FContactModBodyInstancePair newContactPair;
						newContactPair.Actor1 = Inst1->ActorHandle;
						newContactPair.Actor2 = Inst2->ActorHandle;
						newContactPair.bBody1IgnoreEntireActor = false;
						newContactPair.bBody2IgnoreEntireActor = false;

						if (bIgnoreCollision)
							ContactCallback->ContactsToIgnore.AddUnique(newContactPair);
						else
							ContactCallback->ContactsToIgnore.Remove(newContactPair);
					}

					if (FContactModifyCallbackVR* ContactCallback = (FContactModifyCallbackVR*)PScene->getContactModifyCallback())
					{
						FRWScopeLock(ContactCallback->RWAccessLock, FRWScopeLockType::SLT_Write);
						FContactModBodyInstancePair newContactPair;
						newContactPair.Actor1 = Inst1->ActorHandle;
						newContactPair.Actor2 = Inst2->ActorHandle;
						newContactPair.bBody1IgnoreEntireActor = false;
						newContactPair.bBody2IgnoreEntireActor = false;

						if (bIgnoreCollision)
							ContactCallback->ContactsToIgnore.AddUnique(newContactPair);
						else
							ContactCallback->ContactsToIgnore.Remove(newContactPair);
					}
				}
#endif
			}
		}
		else
		{
			UE_LOG(VRExpansionFunctionLibraryLog, Error, TEXT("Set Objects Ignore Collision called with object(s) with an invalid body instance!!"));
		}
	}
	else
	{
		UE_LOG(VRExpansionFunctionLibraryLog, Error, TEXT("Set Objects Ignore Collision called with invalid object(s)!!"));
	}
}

void UVRExpansionFunctionLibrary::LowPassFilter_RollingAverage(FVector lastAverage, FVector newSample, FVector & newAverage, int32 numSamples)
{
	newAverage = lastAverage;
	newAverage -= newAverage / numSamples;
	newAverage += newSample / numSamples;
}

void UVRExpansionFunctionLibrary::LowPassFilter_Exponential(FVector lastAverage, FVector newSample, FVector & newAverage, float sampleFactor)
{
	newAverage = (newSample * sampleFactor) + ((1 - sampleFactor) * lastAverage);
}

bool UVRExpansionFunctionLibrary::GetIsActorMovable(AActor * ActorToCheck)
{
	if (!ActorToCheck)
		return false;

	if (USceneComponent * rootComp = ActorToCheck->GetRootComponent())
	{
		 return rootComp->Mobility == EComponentMobility::Movable;
	}

	return false;
}

void UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName(FName SlotType, AActor * Actor, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent* QueryController)
{
	bHadSlotInRange = false;
	SlotWorldTransform = FTransform::Identity;
	SlotName = NAME_None;
	UHandSocketComponent* TargetHandSocket = nullptr;

	if (!Actor)
		return;

	MaxRange = FMath::Square(MaxRange);

	if (USceneComponent *rootComp = Actor->GetRootComponent())
	{
		FVector RelativeWorldLocation = rootComp->GetComponentTransform().InverseTransformPosition(WorldLocation);
		float ClosestSlotDistance = -0.1f;

		TArray<FName> SocketNames = rootComp->GetAllSocketNames();

		FString GripIdentifier = SlotType.ToString();

		int foundIndex = 0;

		for (int i = 0; i < SocketNames.Num(); ++i)
		{
			if (SocketNames[i].ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
			{
			
				float vecLen = FVector::DistSquared(RelativeWorldLocation, rootComp->GetSocketTransform(SocketNames[i], ERelativeTransformSpace::RTS_Component).GetLocation());

				if (MaxRange >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
				{
					ClosestSlotDistance = vecLen;
					bHadSlotInRange = true;
					foundIndex = i;
				}
			}
		}

		TArray<USceneComponent*> AttachChildren = rootComp->GetAttachChildren();

		for (USceneComponent * AttachChild : AttachChildren)
		{
			if (AttachChild && AttachChild->IsA<UHandSocketComponent>())
			{
				if (UHandSocketComponent* SocketComp = Cast<UHandSocketComponent>(AttachChild))
				{
					if (SocketComp->SlotPrefix.ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
					{
						float vecLen = FVector::DistSquared(RelativeWorldLocation, SocketComp->GetRelativeLocation());

						if (SocketComp->bAlwaysInRange)
						{
							TargetHandSocket = SocketComp;
							ClosestSlotDistance = vecLen;
							bHadSlotInRange = true;
						}
						else
						{
							float RangeVal = (SocketComp->OverrideDistance > 0.0f ? FMath::Square(SocketComp->OverrideDistance) : MaxRange);
							if (RangeVal >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
							{
								TargetHandSocket = SocketComp;
								ClosestSlotDistance = vecLen;
								bHadSlotInRange = true;
							}
						}
					}
				}
			}
		}


		if (bHadSlotInRange)
		{
			if (TargetHandSocket)
			{
				SlotWorldTransform = TargetHandSocket->GetHandSocketTransform(QueryController);
				SlotName = TargetHandSocket->GetFName();
				SlotWorldTransform.SetScale3D(FVector(1.0f));
			}
			else
			{
				SlotWorldTransform = rootComp->GetSocketTransform(SocketNames[foundIndex]);
				SlotName = SocketNames[foundIndex];
				SlotWorldTransform.SetScale3D(FVector(1.0f));
			}
		}
	}
}

void UVRExpansionFunctionLibrary::GetGripSlotInRangeByTypeName_Component(FName SlotType, UPrimitiveComponent * Component, FVector WorldLocation, float MaxRange, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName & SlotName, UGripMotionControllerComponent* QueryController)
{
	bHadSlotInRange = false;
	SlotWorldTransform = FTransform::Identity;
	SlotName = NAME_None;
	UHandSocketComponent* TargetHandSocket = nullptr;

	if (!Component)
		return;

	FVector RelativeWorldLocation = Component->GetComponentTransform().InverseTransformPosition(WorldLocation);
	MaxRange = FMath::Square(MaxRange);

	float ClosestSlotDistance = -0.1f;

	TArray<FName> SocketNames = Component->GetAllSocketNames();

	FString GripIdentifier = SlotType.ToString();

	int foundIndex = 0;

	for (int i = 0; i < SocketNames.Num(); ++i)
	{
		if (SocketNames[i].ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
		{
			float vecLen = FVector::DistSquared(RelativeWorldLocation, Component->GetSocketTransform(SocketNames[i], ERelativeTransformSpace::RTS_Component).GetLocation());

			if (MaxRange >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
			{
				ClosestSlotDistance = vecLen;
				bHadSlotInRange = true;
				foundIndex = i;
			}
		}
	}

	TArray<USceneComponent*> AttachChildren = Component->GetAttachChildren();

	for (USceneComponent* AttachChild : AttachChildren)
	{
		if (AttachChild && AttachChild->IsA<UHandSocketComponent>())
		{
			if (UHandSocketComponent* SocketComp = Cast<UHandSocketComponent>(AttachChild))
			{
				if (SocketComp->SlotPrefix.ToString().Contains(GripIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromStart))
				{
					float vecLen = FVector::DistSquared(RelativeWorldLocation, SocketComp->GetRelativeLocation());
					if (SocketComp->bAlwaysInRange)
					{
						TargetHandSocket = SocketComp;
						ClosestSlotDistance = vecLen;
						bHadSlotInRange = true;
					}
					else
					{
						float RangeVal = (SocketComp->OverrideDistance > 0.0f ? FMath::Square(SocketComp->OverrideDistance) : MaxRange);
						if (RangeVal >= vecLen && (ClosestSlotDistance < 0.0f || vecLen < ClosestSlotDistance))
						{
							TargetHandSocket = SocketComp;
							ClosestSlotDistance = vecLen;
							bHadSlotInRange = true;
						}
					}
				}
			}
		}
	}

	if (bHadSlotInRange)
	{
		if (TargetHandSocket)
		{
			SlotWorldTransform = TargetHandSocket->GetHandSocketTransform(QueryController);
			SlotName = TargetHandSocket->GetFName();
			SlotWorldTransform.SetScale3D(FVector(1.0f));
		}
		else
		{
			SlotWorldTransform = Component->GetSocketTransform(SocketNames[foundIndex]);
			SlotName = SocketNames[foundIndex];
			SlotWorldTransform.SetScale3D(FVector(1.0f));
		}
	}
}

FRotator UVRExpansionFunctionLibrary::GetHMDPureYaw(FRotator HMDRotation)
{
	return GetHMDPureYaw_I(HMDRotation);
}

EBPHMDWornState UVRExpansionFunctionLibrary::GetIsHMDWorn()
{

	if (GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice())
	{
		return (EBPHMDWornState)GEngine->XRSystem->GetHMDDevice()->GetHMDWornState();
	}

	return EBPHMDWornState::Unknown;
}

bool UVRExpansionFunctionLibrary::GetIsHMDConnected()
{
	return GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice() && GEngine->XRSystem->GetHMDDevice()->IsHMDConnected();
}

EBPHMDDeviceType UVRExpansionFunctionLibrary::GetHMDType()
{
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		/*
			if (GEngine && GEngine->XRSystem.IsValid())
	{
		Ar.Logf(*GEngine->XRSystem->GetVersionString());
	}
		*/

		// #TODO 4.19: Figure out a way to replace this...its broken now
		/*IHeadMountedDisplay* HMDDevice = GEngine->XRSystem->GetHMDDevice();
		if (HMDDevice)
		{
			EHMDDeviceType::Type HMDDeviceType = HMDDevice->GetHMDDeviceType();

			switch (HMDDeviceType)
			{
			case EHMDDeviceType::DT_ES2GenericStereoMesh: return EBPHMDDeviceType::DT_ES2GenericStereoMesh; break;
			case EHMDDeviceType::DT_GearVR: return EBPHMDDeviceType::DT_GearVR; break;
			case EHMDDeviceType::DT_Morpheus: return EBPHMDDeviceType::DT_Morpheus; break;
			case EHMDDeviceType::DT_OculusRift: return EBPHMDDeviceType::DT_OculusRift; break;
			case EHMDDeviceType::DT_SteamVR: return EBPHMDDeviceType::DT_SteamVR; break;
			case EHMDDeviceType::DT_GoogleVR: return EBPHMDDeviceType::DT_GoogleVR; break;
			}

		}*/

		// There are no device type entries for these now....
		// Does the device type go away soon leaving only FNames?
		// #TODO: 4.19?
		// GearVR doesn't even return anything gut OculusHMD in FName currently.

		static const FName SteamVRSystemName(TEXT("SteamVR"));
		static const FName OculusSystemName(TEXT("OculusHMD"));
		static const FName PSVRSystemName(TEXT("PSVR"));
		static const FName OSVRSystemName(TEXT("OSVR"));
		static const FName GoogleARCoreSystemName(TEXT("FGoogleARCoreHMD"));
		static const FName AppleARKitSystemName(TEXT("AppleARKit"));
		static const FName GoogleVRHMDSystemName(TEXT("FGoogleVRHMD"));

		FName DeviceName(NAME_None);
		DeviceName = GEngine->XRSystem->GetSystemName();


		if (DeviceName == FName(TEXT("SimpleHMD")))
			return EBPHMDDeviceType::DT_ES2GenericStereoMesh;
		else if (DeviceName == SteamVRSystemName)
			return EBPHMDDeviceType::DT_SteamVR;
		else if (DeviceName == OculusSystemName)
			return EBPHMDDeviceType::DT_OculusHMD;
		else if (DeviceName == PSVRSystemName)
			return EBPHMDDeviceType::DT_PSVR;
		else if (DeviceName == OSVRSystemName)
			return EBPHMDDeviceType::DT_SteamVR;
		else if (DeviceName == GoogleARCoreSystemName)
			return EBPHMDDeviceType::DT_GoogleARCore;
		else if (DeviceName == AppleARKitSystemName)
			return EBPHMDDeviceType::DT_AppleARKit;
		else if (DeviceName == GoogleVRHMDSystemName)
			return EBPHMDDeviceType::DT_GoogleVR;
	}

	// Default to unknown
	return EBPHMDDeviceType::DT_Unknown;
}


bool UVRExpansionFunctionLibrary::IsInVREditorPreviewOrGame()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine))
		{
			TOptional<FPlayInEditorSessionInfo> PlayInfo = EdEngine->GetPlayInEditorSessionInfo();
			if (PlayInfo.IsSet())
			{				
				return PlayInfo->OriginalRequestParams.SessionPreviewTypeOverride == EPlaySessionPreviewType::VRPreview;
			}
			else
			{
				return false;
			}
		}
	}
#endif

	// Is not an editor build, default to true here
	return true;
}

bool UVRExpansionFunctionLibrary::IsInVREditorPreview()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine))
		{
			TOptional<FPlayInEditorSessionInfo> PlayInfo = EdEngine->GetPlayInEditorSessionInfo();
			if (PlayInfo.IsSet())
			{
				return PlayInfo->OriginalRequestParams.SessionPreviewTypeOverride == EPlaySessionPreviewType::VRPreview;
			}
			else
			{
				return false;
			}
		}
	}
#endif

	// Is not an editor build, default to false here
	return false;
}

void UVRExpansionFunctionLibrary::NonAuthorityMinimumAreaRectangle(class UObject* WorldContextObject, const TArray<FVector>& InVerts, const FVector& SampleSurfaceNormal, FVector& OutRectCenter, FRotator& OutRectRotation, float& OutSideLengthX, float& OutSideLengthY, bool bDebugDraw)
{
	float MinArea = -1.f;
	float CurrentArea = -1.f;
	FVector SupportVectorA, SupportVectorB;
	FVector RectSideA, RectSideB;
	float MinDotResultA, MinDotResultB, MaxDotResultA, MaxDotResultB;
	FVector TestEdge;
	float TestEdgeDot = 0.f;
	FVector PolyNormal(0.f, 0.f, 1.f);
	TArray<int32> PolyVertIndices;

	// Bail if we receive an empty InVerts array
	if (InVerts.Num() == 0)
	{
		return;
	}

	// Compute the approximate normal of the poly, using the direction of SampleSurfaceNormal for guidance
	PolyNormal = (InVerts[InVerts.Num() / 3] - InVerts[0]) ^ (InVerts[InVerts.Num() * 2 / 3] - InVerts[InVerts.Num() / 3]);
	if ((PolyNormal | SampleSurfaceNormal) < 0.f)
	{
		PolyNormal = -PolyNormal;
	}

	// Transform the sample points to 2D
	FMatrix SurfaceNormalMatrix = FRotationMatrix::MakeFromZX(PolyNormal, FVector(1.f, 0.f, 0.f));
	TArray<FVector> TransformedVerts;
	OutRectCenter = FVector(0.f);
	for (int32 Idx = 0; Idx < InVerts.Num(); ++Idx)
	{
		OutRectCenter += InVerts[Idx];
		TransformedVerts.Add(SurfaceNormalMatrix.InverseTransformVector(InVerts[Idx]));
	}
	OutRectCenter /= InVerts.Num();

	// Compute the convex hull of the sample points
	ConvexHull2D::ComputeConvexHull(TransformedVerts, PolyVertIndices);

	// Minimum area rectangle as computed by http://www.geometrictools.com/Documentation/MinimumAreaRectangle.pdf
	for (int32 Idx = 1; Idx < PolyVertIndices.Num() - 1; ++Idx)
	{
		SupportVectorA = (TransformedVerts[PolyVertIndices[Idx]] - TransformedVerts[PolyVertIndices[Idx - 1]]).GetSafeNormal();
		SupportVectorA.Z = 0.f;
		SupportVectorB.X = -SupportVectorA.Y;
		SupportVectorB.Y = SupportVectorA.X;
		SupportVectorB.Z = 0.f;
		MinDotResultA = MinDotResultB = MaxDotResultA = MaxDotResultB = 0.f;

		for (int TestVertIdx = 1; TestVertIdx < PolyVertIndices.Num(); ++TestVertIdx)
		{
			TestEdge = TransformedVerts[PolyVertIndices[TestVertIdx]] - TransformedVerts[PolyVertIndices[0]];
			TestEdgeDot = SupportVectorA | TestEdge;
			if (TestEdgeDot < MinDotResultA)
			{
				MinDotResultA = TestEdgeDot;
			}
			else if (TestEdgeDot > MaxDotResultA)
			{
				MaxDotResultA = TestEdgeDot;
			}

			TestEdgeDot = SupportVectorB | TestEdge;
			if (TestEdgeDot < MinDotResultB)
			{
				MinDotResultB = TestEdgeDot;
			}
			else if (TestEdgeDot > MaxDotResultB)
			{
				MaxDotResultB = TestEdgeDot;
			}
		}

		CurrentArea = (MaxDotResultA - MinDotResultA) * (MaxDotResultB - MinDotResultB);
		if (MinArea < 0.f || CurrentArea < MinArea)
		{
			MinArea = CurrentArea;
			RectSideA = SupportVectorA * (MaxDotResultA - MinDotResultA);
			RectSideB = SupportVectorB * (MaxDotResultB - MinDotResultB);
		}
	}

	RectSideA = SurfaceNormalMatrix.TransformVector(RectSideA);
	RectSideB = SurfaceNormalMatrix.TransformVector(RectSideB);
	OutRectRotation = FRotationMatrix::MakeFromZX(PolyNormal, RectSideA).Rotator();
	OutSideLengthX = RectSideA.Size();
	OutSideLengthY = RectSideB.Size();

#if ENABLE_DRAW_DEBUG
	if (bDebugDraw)
	{
		UWorld* World = (WorldContextObject) ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
		if (World != nullptr)
		{
			DrawDebugSphere(World, OutRectCenter, 10.f, 12, FColor::Yellow, true);
			DrawDebugCoordinateSystem(World, OutRectCenter, SurfaceNormalMatrix.Rotator(), 100.f, true);
			DrawDebugLine(World, OutRectCenter - RectSideA * 0.5f + FVector(0, 0, 10.f), OutRectCenter + RectSideA * 0.5f + FVector(0, 0, 10.f), FColor::Green, true, -1, 0, 5.f);
			DrawDebugLine(World, OutRectCenter - RectSideB * 0.5f + FVector(0, 0, 10.f), OutRectCenter + RectSideB * 0.5f + FVector(0, 0, 10.f), FColor::Blue, true, -1, 0, 5.f);
		}
		else
		{
			FFrame::KismetExecutionMessage(TEXT("WorldContext required for MinimumAreaRectangle to draw a debug visualization."), ELogVerbosity::Warning);
		}
	}
#endif
}

bool UVRExpansionFunctionLibrary::EqualEqual_FBPActorGripInformation(const FBPActorGripInformation &A, const FBPActorGripInformation &B)
{
	return A == B;
}


FTransform_NetQuantize UVRExpansionFunctionLibrary::MakeTransform_NetQuantize(FVector Translation, FRotator Rotation, FVector Scale)
{
	return FTransform_NetQuantize(Rotation, Translation, Scale);
}

void UVRExpansionFunctionLibrary::BreakTransform_NetQuantize(const FTransform_NetQuantize& InTransform, FVector& Translation, FRotator& Rotation, FVector& Scale)
{
	Translation = InTransform.GetLocation();
	Rotation = InTransform.Rotator();
	Scale = InTransform.GetScale3D();
}

FTransform_NetQuantize UVRExpansionFunctionLibrary::Conv_TransformToTransformNetQuantize(const FTransform &InTransform)
{
	return FTransform_NetQuantize(InTransform);
}

UGripMotionControllerComponent * UVRExpansionFunctionLibrary::Conv_GripPairToMotionController(const FBPGripPair &GripPair)
{
	return GripPair.HoldingController;
}

uint8 UVRExpansionFunctionLibrary::Conv_GripPairToGripID(const FBPGripPair &GripPair)
{
	return GripPair.GripID;
}

FVector_NetQuantize UVRExpansionFunctionLibrary::Conv_FVectorToFVectorNetQuantize(const FVector &InVector)
{
	return FVector_NetQuantize(InVector);
}

FVector_NetQuantize UVRExpansionFunctionLibrary::MakeVector_NetQuantize(FVector InVector)
{
	return FVector_NetQuantize(InVector);
}

FVector_NetQuantize10 UVRExpansionFunctionLibrary::Conv_FVectorToFVectorNetQuantize10(const FVector &InVector)
{
	return FVector_NetQuantize10(InVector);
}

FVector_NetQuantize10 UVRExpansionFunctionLibrary::MakeVector_NetQuantize10(FVector InVector)
{
	return FVector_NetQuantize10(InVector);
}

FVector_NetQuantize100 UVRExpansionFunctionLibrary::Conv_FVectorToFVectorNetQuantize100(const FVector &InVector)
{
	return FVector_NetQuantize100(InVector);
}

FVector_NetQuantize100 UVRExpansionFunctionLibrary::MakeVector_NetQuantize100(FVector InVector)
{
	return FVector_NetQuantize100(InVector);
}

USceneComponent* UVRExpansionFunctionLibrary::AddSceneComponentByClass(UObject* Outer, TSubclassOf<USceneComponent> Class, const FTransform & ComponentRelativeTransform)
{
	if (Class != nullptr && Outer != nullptr)
	{
		USceneComponent* Component = NewObject<USceneComponent>(Outer, *Class);
		if (Component != nullptr)
		{
			if (USceneComponent * ParentComp = Cast<USceneComponent>(Outer))
				Component->SetupAttachment(ParentComp);

			Component->RegisterComponent();
			Component->SetRelativeTransform(ComponentRelativeTransform);
			
			return Component;
		}
		else
		{
			return nullptr;
		}
	}

	return nullptr;
}