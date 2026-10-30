// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/OptionalRepSkeletalMeshActor.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(OptionalRepSkeletalMeshActor)

#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsReplication.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Physics/PhysicsInterfaceCore.h"
#include "Misc/MemStack.h"
#include "EngineLogs.h"
#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif

UNoRepSphereComponent::UNoRepSphereComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->SetIsReplicatedByDefault(true);
	this->PrimaryComponentTick.bCanEverTick = false;
	SphereRadius = 4.0f;
	SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	//SetAllMassScale(0.0f); Engine hates calling this in constructor


	BodyInstance.bOverrideMass = true; 
	BodyInstance.SetMassOverride(0.f);
}

void UNoRepSphereComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// For std properties
	FDoRepLifetimeParams PushModelParams{ COND_None, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UNoRepSphereComponent, bReplicateMovement, PushModelParams);

	RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(USceneComponent, AttachParent, COND_InitialOnly);
	RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(USceneComponent, AttachSocketName, COND_InitialOnly);
	RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(USceneComponent, AttachChildren, COND_InitialOnly);
	RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(USceneComponent, RelativeLocation, COND_InitialOnly);
	RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(USceneComponent, RelativeRotation, COND_InitialOnly);
	RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(USceneComponent, RelativeScale3D, COND_InitialOnly);
	//DISABLE_REPLICATED_PRIVATE_PROPERTY(AActor, AttachmentReplication);
}

void UNoRepSphereComponent::SetReplicateMovement(bool bNewReplicateMovement)
{
	bReplicateMovement = bNewReplicateMovement;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UNoRepSphereComponent, bReplicateMovement, this);
#endif
}

void UNoRepSphereComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

}

void FSkeletalMeshComponentEndPhysicsTickFunctionVR::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	QUICK_SCOPE_CYCLE_COUNTER(FSkeletalMeshComponentEndPhysicsTickFunctionVR_ExecuteTick);

	FActorComponentTickFunction::ExecuteTickHelper(TargetVR, /*bTickInEditor=*/ false, DeltaTime, TickType, [this](float DilatedTime)
		{
			TargetVR->EndPhysicsTickComponentVR(*this);
		});
}

FString FSkeletalMeshComponentEndPhysicsTickFunctionVR::DiagnosticMessage()
{
	if (TargetVR)
	{
		return TargetVR->GetFullName() + TEXT("[EndPhysicsTickVR]");
	}
	return TEXT("<NULL>[EndPhysicsTickVR]");
}

FName FSkeletalMeshComponentEndPhysicsTickFunctionVR::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("SkeletalMeshComponentEndPhysicsTickVR"));
}


UInversePhysicsSkeletalMeshComponent::UInversePhysicsSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicateMovement = true;
	bReplicatePhysicsToAutonomousProxy = false;

	// Replaces the stock end physics tick (see RegisterEndPhysicsTick), same settings as the stock one
	bUseSignPreservingPhysicsBlend = true;
	EndPhysicsTickFunctionVR.TickGroup = TG_EndPhysics;
	EndPhysicsTickFunctionVR.bCanEverTick = true;
	EndPhysicsTickFunctionVR.bStartWithTickEnabled = true;
	EndPhysicsTickFunctionVR.bRunTransactionally = true;
}

void UInversePhysicsSkeletalMeshComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Don't replicate if set to not do it
	if (!IRISNetReplication::IsIris(this))
	{
		DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeLocation, bReplicateMovement);
		DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeRotation, bReplicateMovement);
		DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeScale3D, bReplicateMovement);
	}
}


void UInversePhysicsSkeletalMeshComponent::SetReplicateMovement(bool bNewReplicateMovement)
{
	bReplicateMovement = bNewReplicateMovement;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UInversePhysicsSkeletalMeshComponent, bReplicateMovement, this);
#endif
}

void UInversePhysicsSkeletalMeshComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Anim events dispatched in the super tick can destroy us
	if (!IsValid(this))
	{
		return;
	}

	// The stock tick picks its end group from whether ITS end physics tick is registered, ours replaces it.
	// Match what it does when physics is required so animation finishes before physics runs.
	if (ThisTickFunction && !PrimaryComponentTick.bRunOnAnyThread && EndPhysicsTickFunctionVR.IsTickFunctionRegistered())
	{
		ThisTickFunction->EndTickGroup = TG_PrePhysics;
	}
}

void UInversePhysicsSkeletalMeshComponent::RegisterEndPhysicsTick(bool bRegister)
{
	// Every registration path (TickComponent, RegisterComponentTickFunctions, physics state changes) funnels through here
	if (!bUseSignPreservingPhysicsBlend)
	{
		RegisterEndPhysicsTickVR(false);
		Super::RegisterEndPhysicsTick(bRegister);
		return;
	}

	// Keep the stock end physics tick (and its blend) off, ours replaces it
	Super::RegisterEndPhysicsTick(false);
	RegisterEndPhysicsTickVR(bRegister);
}

void UInversePhysicsSkeletalMeshComponent::SetUseSignPreservingPhysicsBlend(bool bNewUseSignPreservingPhysicsBlend)
{
	if (bUseSignPreservingPhysicsBlend == bNewUseSignPreservingPhysicsBlend)
	{
		return;
	}

	// If either end physics tick is currently running, swap to the other one now
	const bool bEndPhysicsTickRegistered = EndPhysicsTickFunctionVR.IsTickFunctionRegistered() || EndPhysicsTickFunction.IsTickFunctionRegistered();
	bUseSignPreservingPhysicsBlend = bNewUseSignPreservingPhysicsBlend;

	if (bEndPhysicsTickRegistered)
	{
		RegisterEndPhysicsTick(true);
	}
}

void UInversePhysicsSkeletalMeshComponent::RegisterEndPhysicsTickVR(bool bRegister)
{
	// Mirrors USkeletalMeshComponent::RegisterEndPhysicsTick
	if (bRegister != EndPhysicsTickFunctionVR.IsTickFunctionRegistered())
	{
		if (bRegister)
		{
			UWorld* World = GetWorld();
			if (World && World->EndPhysicsTickFunction.IsTickFunctionRegistered() && SetupActorComponentTickFunction(&EndPhysicsTickFunctionVR))
			{
				EndPhysicsTickFunctionVR.TargetVR = this;
				// Make sure our EndPhysicsTick gets called after physics simulation is finished
				EndPhysicsTickFunctionVR.AddPrerequisite(World, World->EndPhysicsTickFunction);
			}
		}
		else
		{
			EndPhysicsTickFunctionVR.UnRegisterTickFunction();
		}
	}
}

void UInversePhysicsSkeletalMeshComponent::EndPhysicsTickComponentVR(FSkeletalMeshComponentEndPhysicsTickFunctionVR& ThisTickFunction)
{
	// Mirrors USkeletalMeshComponent::EndPhysicsTickComponent, with our own blend

	// if physics is disabled on dedicated server, no reason to be here.
	if (!bEnablePhysicsOnDedicatedServer && IsRunningDedicatedServer())
	{
		FinalizeBoneTransform();
		return;
	}

	if (IsRegistered() && IsSimulatingPhysics() && RigidBodyIsAwake())
	{
		if (bNotifySyncComponentToRBPhysics)
		{
			OnSyncComponentToRBPhysics();
		}

		SyncComponentToRBPhysics();
	}

	if (ShouldBlendPhysicsBones())
	{
		if (IsRegistered())
		{
			BlendInPhysicsVR();
		}
	}
}

void UInversePhysicsSkeletalMeshComponent::BlendInPhysicsVR()
{
	check(IsInGameThread());

	// Can't do anything without a SkeletalMesh
	if (!GetSkeletalMeshAsset())
	{
		return;
	}

	// We now have all the animations blended together and final relative transforms for each bone.
	// If we don't have or want any physics, we do nothing.
	if (Bodies.Num() > 0 && CollisionEnabledHasPhysics(GetCollisionEnabled()))
	{
		// Blocks on any running anim evaluation (and swaps its buffers back) like the stock blend does, then copy the
		// bone space pose into our own array since the engine's is private now. Always on the game thread (no parallel blend).
		TArrayView<const FTransform> BoneSpaceView = GetBoneSpaceTransformsView();
		BlendBoneSpaceTransformsVR.Reset(BoneSpaceView.Num());
		BlendBoneSpaceTransformsVR.Append(BoneSpaceView.GetData(), BoneSpaceView.Num());

		// Fetch the editable buffer after the evaluation is done, it can swap
		PerformBlendPhysicsBonesVR(RequiredBones, GetEditableComponentSpaceTransforms(), BlendBoneSpaceTransformsVR);
		FinalizeAnimationUpdateVR();
	}
}

void UInversePhysicsSkeletalMeshComponent::FinalizeAnimationUpdateVR()
{
	// Mirrors USkeletalMeshComponent::FinalizeAnimationUpdate

	// Flip bone buffer and send 'post anim' notification
	FinalizeBoneTransform();

	if (!bSimulationUpdatesChildTransforms || !IsSimulatingPhysics())	//If we simulate physics the call to MoveComponent already updates the children transforms. If we are confident that animation will not be needed this can be skipped. TODO: this should be handled at the scene component layer
	{
		// Update Child Transform - The above function changes bone transform, so will need to update child transform
		// But only children attached to us via a socket.
		UpdateChildTransforms(EUpdateTransformFlags::OnlyUpdateIfUsingSocket);
	}

	if (bUpdateOverlapsOnAnimationFinalize)
	{
		// animation often change overlap.
		UpdateOverlaps();
	}

	// update bounds
	// *NOTE* bSkipBoundsUpdateWhenInterpolating / AnimEvaluationContext are private, always update them
	{
		// Cached local bounds are now out of date
		InvalidateCachedBounds();

		UpdateBounds();
	}

	// Need to send new bounds to
	MarkRenderTransformDirty();

	// New bone positions need to be sent to render thread
	MarkRenderDynamicDataDirty();

	// If we have any follower components, they need to be refreshed as well.
	RefreshFollowerComponents();
}

void UInversePhysicsSkeletalMeshComponent::GetWeldedBodies(TArray<FBodyInstance*>& OutWeldedBodies, TArray<FName>& OutLabels, bool bIncludingAutoWeld)
{
	UPhysicsAsset* PhysicsAsset = GetPhysicsAsset();

	for (int32 BodyIdx = 0; BodyIdx < Bodies.Num(); ++BodyIdx)
	{
		FBodyInstance* BI = Bodies[BodyIdx];
		if (BI && (BI->WeldParent != nullptr || (bIncludingAutoWeld && BI->bAutoWeld)))
		{
			OutWeldedBodies.Add(BI);
			if (PhysicsAsset)
			{
				if (UBodySetup* PhysicsAssetBodySetup = PhysicsAsset->SkeletalBodySetups[BodyIdx].Get())
				{
					OutLabels.Add(PhysicsAssetBodySetup->BoneName);
				}
				else
				{
					OutLabels.Add(NAME_None);
				}
			}
			else
			{
				OutLabels.Add(NAME_None);
			}

		}
	}

	for (USceneComponent* Child : GetAttachChildren())
	{
		if (UPrimitiveComponent* PrimChild = Cast<UPrimitiveComponent>(Child))
		{
			PrimChild->GetWeldedBodies(OutWeldedBodies, OutLabels, bIncludingAutoWeld);
		}
	}
}

FBodyInstance* UInversePhysicsSkeletalMeshComponent::GetBodyInstance(FName BoneName, bool bGetWelded, int32) const
{
	UPhysicsAsset* const PhysicsAsset = GetPhysicsAsset();
	FBodyInstance* BodyInst = NULL;

	if (PhysicsAsset != NULL)
	{
		// A name of NAME_None indicates 'root body'
		if (BoneName == NAME_None)
		{
			if (Bodies.IsValidIndex(RootBodyData.BodyIndex))
			{
				BodyInst = Bodies[RootBodyData.BodyIndex];
			}
		}
		// otherwise, look for the body
		else
		{
			int32 BodyIndex = PhysicsAsset->FindBodyIndex(BoneName);
			if (Bodies.IsValidIndex(BodyIndex))
			{
				BodyInst = Bodies[BodyIndex];
			}
		}

		BodyInst = (bGetWelded && BodyInstance.WeldParent) ? BodyInstance.WeldParent : BodyInst;
	}

	return BodyInst;
}

// Copied from PhysAnim.cpp (file local there), used by PerformBlendPhysicsBonesVR
namespace InversePhysicsSkeletalMeshStatics
{
struct FAssetWorldBoneTM
{
	FTransform	TM;			// Should never contain scaling.
	bool bUpToDate;			// If this equals PhysAssetUpdateNum, then the matrix is up to date.
};

typedef TArray<FAssetWorldBoneTM, TMemStackAllocator<alignof(FAssetWorldBoneTM)>> TAssetWorldBoneTMArray;

static void UpdateWorldBoneTMVR(TAssetWorldBoneTMArray& WorldBoneTMs, const TArray<FTransform>& InBoneSpaceTransforms, int32 BoneIndex, USkeletalMeshComponent* SkelComp, const FTransform& LocalToWorldTM, const FVector& Scale3D)
{
	// If its already up to date - do nothing
	if (WorldBoneTMs[BoneIndex].bUpToDate)
	{
		return;
	}

	FTransform ParentTM, RelTM;
	if (BoneIndex == 0)
	{
		// If this is the root bone, we use the mesh component LocalToWorld as the parent transform.
		ParentTM = LocalToWorldTM;
	}
	else
	{
		// If not root, use our cached world-space bone transforms.
		int32 ParentIndex = SkelComp->GetSkeletalMeshAsset()->GetRefSkeleton().GetParentIndex(BoneIndex);
		UpdateWorldBoneTMVR(WorldBoneTMs, InBoneSpaceTransforms, ParentIndex, SkelComp, LocalToWorldTM, Scale3D);
		ParentTM = WorldBoneTMs[ParentIndex].TM;
	}

	if (InBoneSpaceTransforms.IsValidIndex(BoneIndex))
	{
		RelTM = InBoneSpaceTransforms[BoneIndex];
		RelTM.ScaleTranslation(Scale3D);

		WorldBoneTMs[BoneIndex].TM = RelTM * ParentTM;
		WorldBoneTMs[BoneIndex].bUpToDate = true;
	}
}
}

void UInversePhysicsSkeletalMeshComponent::PerformBlendPhysicsBonesVR(const TArray<FBoneIndexType>& InRequiredBones, TArray<FTransform>& InOutComponentSpaceTransforms, TArray<FTransform>& InOutBoneSpaceTransforms)
{
	// Mirrors USkeletalMeshComponent::PerformBlendPhysicsBones (PhysAnim.cpp) except for keeping the sign of the component scale
	using namespace InversePhysicsSkeletalMeshStatics;
	QUICK_SCOPE_CYCLE_COUNTER(STAT_InversePhysicsBlendPhysicsBones);

	UPhysicsAsset* const PhysicsAsset = GetPhysicsAsset();
	check(PhysicsAsset);

	if (InOutComponentSpaceTransforms.Num() == 0)
	{
		return;
	}

	if (InOutBoneSpaceTransforms.Num() == 0)
	{
		return;
	}

	// Get the scene, and do nothing if we can't get one.
	FPhysScene* PhysScene = nullptr;
	if (GetWorld() != nullptr)
	{
		PhysScene = GetWorld()->GetPhysicsScene();
	}

	if (PhysScene == nullptr)
	{
		return;
	}

	FMemMark Mark(FMemStack::Get());
	// Make sure scratch space is big enough.
	TAssetWorldBoneTMArray WorldBoneTMs;
	WorldBoneTMs.AddZeroed(InOutComponentSpaceTransforms.Num());

	FTransform LocalToWorldTM = GetComponentTransform();

	// PhysAnim.cpp - PerformBlendPhysicsBones
	// This fixes the simulated inversed scaled skeletal mesh bug
	LocalToWorldTM.SetScale3D(LocalToWorldTM.GetScale3D().GetSignVector());
	LocalToWorldTM.NormalizeRotation();

	// Original implementation stomps negative scale values
	//LocalToWorldTM.RemoveScaling();

	// This will update both InOutComponentSpaceTransforms and InOutBoneSpaceTransforms for every
	// required bone, leaving any others unchanged.
	FPhysicsCommand::ExecuteRead(this, [&]()
		{
			// Note that IsInstanceSimulatingPhysics returns false for kinematic bodies, so
			// bSimulatedRootBody means "is the root physics body dynamic" (not the same as the root bone)
			const bool bSimulatedRootBody = Bodies.IsValidIndex(RootBodyData.BodyIndex) &&
				Bodies[RootBodyData.BodyIndex]->IsInstanceSimulatingPhysics();

			// Get the anticipated component transform - noting that if PhysicsTransformUpdateMode is
			// set to SimulationUpatesComponentTransform then this will come from the simulation.
			const FTransform NewComponentTransform = GetComponentTransformFromBodyInstance(Bodies[RootBodyData.BodyIndex]);

			// Get component scale. If we have a body on the root bone, this will be the scale applied 
			// to its relative physics position.
			FVector TotalScale3D = GetComponentTransform().GetScale3D();
			FVector RecipScale3D = TotalScale3D.Reciprocal();

			// For bodies that are not at the root bone, we will also need to apply the inverse of
			// the root bone scale to the body positions reported by physics. We will update the
			// scale below when we have processed the body on the root bone (if there is one)
			const FVector RootBoneScale = InOutBoneSpaceTransforms[0].GetScale3D();
			const bool bUnitScaledRootBone = RootBoneScale.Equals(FVector(1, 1, 1), UE_SMALL_NUMBER);
			bool bHaveSetParentScale = bUnitScaledRootBone;	// We don't need to change the scale if root has no scale

			// For each bone:
			// * Update the WorldBoneTMs entry
			// * Update InOutBoneSpaceTransforms, using the physics blend weights
			// * Update InOutComponentSpaceTransforms using transforms calculated from the bone space transforms
			//
			// This prevents intermediate physics blend weights from "breaking" joints (e.g. if the
			// weight was just applied in world space).
			for (int32 i = 0; i < InRequiredBones.Num(); i++)
			{
				// Gets set to true if we have a valid body and the skeletal mesh option has been set to
				// be driven by kinematic body parts.
				bool bDriveMeshWhenKinematic = false;

				int32 BoneIndex = InRequiredBones[i];

				// See if this is a physics bone - i.e. if there is a body registered to/associated with it.
				// If so - get its world space matrix and its parents world space matrix and calc relative atom.
				int32 BodyIndex = PhysicsAsset->FindBodyIndex(GetSkeletalMeshAsset()->GetRefSkeleton().GetBoneName(BoneIndex));
				if (BodyIndex != INDEX_NONE)
				{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					// tracking down TTP 280421. Remove this if this doesn't happen. 
					if (!ensure(Bodies.IsValidIndex(BodyIndex)))
					{
						UE_LOGF(LogPhysics, Warning, "%ls(Mesh %ls, PhysicsAsset %ls)",
							*GetName(), *GetNameSafe(GetSkeletalMeshAsset()), *GetNameSafe(PhysicsAsset));
						UE_LOGF(LogPhysics, Warning, " - # of BodySetup (%d), # of Bodies (%d), Invalid BodyIndex(%d)",
							PhysicsAsset->SkeletalBodySetups.Num(), Bodies.Num(), BodyIndex);
						continue;
					}
#endif
					FBodyInstance* PhysicsAssetBodyInstance = Bodies[BodyIndex];

					// If this body is welded to something, it will not have an updated transform so we will use the
					// bone transform. This means that if the mesh continues to play an animation, the pose will not 
					// match the pose when the weld happened. Ideally we would restore the relative transform at the
					// time of the weld, but we do not explicitly store that data (though it could perhaps be 
					// recovered from the collision geometry hierarchy, it's easy to just not animate.)
					if (PhysicsAssetBodyInstance->WeldParent != nullptr)
					{
						BodyIndex = INDEX_NONE;
					}

					bDriveMeshWhenKinematic =
						bUpdateMeshWhenKinematic &&
						PhysicsAssetBodyInstance->IsValidBodyInstance();

					// Only process this bone if it is simulated, or required due to the bDriveMeshWhenKinematic flag
					// Also, if the root bone is scaled, we need to update WorldBoneTMs, even for Kinematics that are 
					// not blended into the output because they may be a parent body of a dynamic body
					const bool bIsSimulatingPhysics = PhysicsAssetBodyInstance->IsInstanceSimulatingPhysics();
					if (bIsSimulatingPhysics || bDriveMeshWhenKinematic || !bUnitScaledRootBone)
					{
						FTransform PhysTM = PhysicsAssetBodyInstance->GetUnrealWorldTransform_AssumesLocked();

						// Store this world-space transform in cache.
						WorldBoneTMs[BoneIndex].TM = PhysTM;
						WorldBoneTMs[BoneIndex].bUpToDate = true;

						if (PhysicsAssetBodyInstance->IsPhysicsDisabled())
						{
							continue;
						}

						// NOTE: if the root bone is scaled we need to update the WorldBoneTMs above for both
						// kinematic and dynamic bodies so that the parent pose calculation below is correct. 
						// For kinematics that are not blended into the output pose, we are done here
						if (!bIsSimulatingPhysics && !bDriveMeshWhenKinematic)
						{
							continue;
						}

						// Bodies that are not on the root bone must have the position reported by physics
						// inverse scaled by the root bone scale.
						if ((BoneIndex > 0) && !bHaveSetParentScale)
						{
							TotalScale3D *= RootBoneScale;
							RecipScale3D = TotalScale3D.Reciprocal();
							bHaveSetParentScale = true;
						}

						// Note that bBlendPhysics is a flag that is used to force use of the physics
						// body, irrespective of whether they are simulated or not, or the value of the
						// physics blend weight.
						//
						// Note that the existence of kinematic body parts towards/at the root can be a
						// problem when they have a blend weight of zero in the asset, which is the
						// default. This will result in InOutBoneSpaceTransforms not including the
						// transform offset that goes to the real-world physics locations of body parts,
						// so rag-dolls (etc) will get translated with the movement component. If this
						// happens and is not the desired behavior, the solution to this is for the
						// owner to explicitly set the physics blend weight of kinematic parts to one.
						float PhysicsBlendWeight = bBlendPhysics ? 1.f : PhysicsAssetBodyInstance->PhysicsBlendWeight;

						// If the body instance is disabled, then we want to use the animation transform
						// and ignore the physics one
						if (PhysicsAssetBodyInstance->IsPhysicsDisabled())
						{
							PhysicsBlendWeight = 0.0f;
						}

						// Only do the calculations here if there is a PhysicsBlendWeight, since in the
						// end we will use this weight to blend from the input/animation value to the
						// physical one.
						if (PhysicsBlendWeight > 0.f)
						{
							if (!(ensure(InOutBoneSpaceTransforms.Num())))
							{
								continue;
							}

							// Find the transform of the parent of this bone.
							FTransform ParentWorldTM;
							if (BoneIndex == 0)
							{
								ParentWorldTM = LocalToWorldTM;
							}
							else
							{
								// If not root, get parent TM from cache (making sure its up-to-date).
								int32 ParentIndex = GetSkeletalMeshAsset()->GetRefSkeleton().GetParentIndex(BoneIndex);
								UpdateWorldBoneTMVR(WorldBoneTMs, InOutBoneSpaceTransforms, ParentIndex, this, LocalToWorldTM, TotalScale3D);
								ParentWorldTM = WorldBoneTMs[ParentIndex].TM;
							}

							// Calculate the relative transform of the current and parent (physical) bones.
							FTransform RelTM = PhysTM.GetRelativeTransform(ParentWorldTM);
							RelTM.RemoveScaling();	// NOTE: this also normalizes the rotation
							FQuat RelRot(RelTM.GetRotation());
							FVector RelPos = RecipScale3D * RelTM.GetLocation();
							FTransform PhysicalBoneSpaceTransform = FTransform(
								RelRot, RelPos, InOutBoneSpaceTransforms[BoneIndex].GetScale3D());

							// Now blend in this atom. See if we are forcing this bone to always be blended in
							InOutBoneSpaceTransforms[BoneIndex].Blend(
								InOutBoneSpaceTransforms[BoneIndex], PhysicalBoneSpaceTransform, PhysicsBlendWeight);
						}
					}
				}

				if (!(ensure(BoneIndex < InOutComponentSpaceTransforms.Num())))
				{
					continue;
				}

				// Update InOutComponentSpaceTransforms entry for this bone now - it will be the parent
				// component-space transform, offset with the current bone-space transform.
				if (BoneIndex == 0)
				{
					if (!(ensure(InOutBoneSpaceTransforms.Num())))
					{
						continue;
					}
					InOutComponentSpaceTransforms[0] = InOutBoneSpaceTransforms[0];
				}
				else
				{
					if (bDriveMeshWhenKinematic || bLocalSpaceKinematics || BodyIndex == INDEX_NONE || Bodies[BodyIndex]->IsInstanceSimulatingPhysics())
					{
						if (!(ensure(BoneIndex < InOutBoneSpaceTransforms.Num())))
						{
							continue;
						}
						const int32 ParentIndex = GetSkeletalMeshAsset()->GetRefSkeleton().GetParentIndex(BoneIndex);
						InOutComponentSpaceTransforms[BoneIndex] = InOutBoneSpaceTransforms[BoneIndex] * InOutComponentSpaceTransforms[ParentIndex];

						//
						// Normalize rotations.
						// We want to remove any loss of precision due to accumulation of error.
						// i.e. A componentSpace transform is the accumulation of all of its local space parents. The further down the chain, the greater the error.
						// SpaceBases are used by external systems, we feed this to PhysX, send this to gameplay through bone and socket queries, etc.
						// So this is a good place to make sure all transforms are normalized.
						//
						InOutComponentSpaceTransforms[BoneIndex].NormalizeRotation();
					}
					else if (bSimulatedRootBody)
					{
						InOutComponentSpaceTransforms[BoneIndex] =
							Bodies[BodyIndex]->GetUnrealWorldTransform_AssumesLocked().GetRelativeTransform(NewComponentTransform);
					}
				}
			}
		});	//end scope for read lock

}


void UInversePhysicsSkeletalMeshComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// For std properties
	FDoRepLifetimeParams PushModelParams{ COND_None, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UInversePhysicsSkeletalMeshComponent, bReplicateMovement, PushModelParams);
}

AOptionalRepGrippableSkeletalMeshActor::AOptionalRepGrippableSkeletalMeshActor(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer.SetDefaultSubobjectClass<UInversePhysicsSkeletalMeshComponent>(TEXT("SkeletalMeshComponent0")))
{
	bIgnoreAttachmentReplication = false;
	bIgnorePhysicsReplication = false;
}

void AOptionalRepGrippableSkeletalMeshActor::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// For properties with special conditions
	FDoRepLifetimeParams PushModelParamsWithCondition{ COND_InitialOnly, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(AOptionalRepGrippableSkeletalMeshActor, bIgnoreAttachmentReplication, PushModelParamsWithCondition);
	DOREPLIFETIME_WITH_PARAMS_FAST(AOptionalRepGrippableSkeletalMeshActor, bIgnorePhysicsReplication, PushModelParamsWithCondition);

	if (bIgnoreAttachmentReplication)
	{
		RESET_REPLIFETIME_CONDITION_PRIVATE_PROPERTY(AActor, AttachmentReplication, COND_InitialOnly);
	}
	//DISABLE_REPLICATED_PRIVATE_PROPERTY(AActor, AttachmentReplication);
}

void AOptionalRepGrippableSkeletalMeshActor::OnRep_ReplicateMovement()
{
	if (bIgnorePhysicsReplication)
	{
		return;
	}

	Super::OnRep_ReplicateMovement();
}

void AOptionalRepGrippableSkeletalMeshActor::PostNetReceivePhysicState()
{
	if (bIgnorePhysicsReplication)
	{
		return;
	}

	Super::PostNetReceivePhysicState();
}