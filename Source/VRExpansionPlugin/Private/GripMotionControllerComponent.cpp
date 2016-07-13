// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPluginPrivatePCH.h"
#include "GripMotionControllerComponent.h"
#include "Net/UnrealNetwork.h"
#include "PrimitiveSceneInfo.h"

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 11
#else
#include "Features/IModularFeatures.h"
#endif

namespace {
	/** This is to prevent destruction of motion controller components while they are
	in the middle of being accessed by the render thread */
	FCriticalSection CritSect;

	// This is already declared in the original motion controller component, redeclaring it provides a reference
	// I assume there is a better way, but I can't use the extern define for it as they have it in the damned anonymous name space
	// I also can't easily use IConsoleManager::Get() as there is no GetOnRenderThread specific implementation
	// Sadly this means that there is an annoying "VariableAlreadyDeclared" warning in the log

	/** Console variable for specifying whether motion controller late update is used */
	//extern TAutoConsoleVariable<int32> CVarEnableMotionControllerLateUpdate; // Can't use this

	TAutoConsoleVariable<int32> CVarEnableMotionControllerLateUpdate(
		TEXT("vr.EnableMotionControllerLateUpdate"),
		1,
		TEXT("This command allows you to specify whether the motion controller late update is applied.\n")
		TEXT(" 0: don't use late update\n")
		TEXT(" 1: use late update (default)"),
		ECVF_Cheat);
		
} // anonymous namespace

  //=============================================================================
UGripMotionControllerComponent::UGripMotionControllerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	PlayerIndex = 0;
	Hand = EControllerHand::Left;
	bDisableLowLatencyUpdate = false;
	bHasAuthority = false;
	bUseWithoutTracking = false;

	this->SetIsReplicated(true);

	// Default 100 htz update rate, same as the 100htz update rate of rep_notify, will be capped to 90/45 though because of vsync on HMD
	bReplicateControllerTransform = true;
	ControllerNetUpdateRate = 100.0f; // 100 htz is default
	ControllerNetUpdateCount = 0.0f;
}

//=============================================================================
UGripMotionControllerComponent::~UGripMotionControllerComponent()
{
	if (ViewExtension.IsValid())
	{
		{
			// This component could be getting accessed from the render thread so it needs to wait
			// before clearing MotionControllerComponent and allowing the destructor to continue
			FScopeLock ScopeLock(&CritSect);
			ViewExtension->MotionControllerComponent = NULL;
		}

		if (GEngine)
		{
			GEngine->ViewExtensions.Remove(ViewExtension);
		}
	}
	ViewExtension.Reset();
}

//=============================================================================
void UGripMotionControllerComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{

	// I am skipping the Scene and Primitive component replication here
	// Generally components aren't set to replicate anyway and I need it to NOT pass the Relative position through the network
	// There isn't much in the scene component to replicate anyway and primitive doesn't have ANY replicated variables
	// Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Skipping the owner with this as the owner will use the controllers location directly
	DOREPLIFETIME_CONDITION(UGripMotionControllerComponent, ReplicatedControllerTransform, COND_SkipOwner);
	DOREPLIFETIME(UGripMotionControllerComponent, GrippedActors);
	DOREPLIFETIME(UGripMotionControllerComponent, ControllerNetUpdateRate);
	DOREPLIFETIME(UGripMotionControllerComponent, bReplicateControllerTransform);
}

void UGripMotionControllerComponent::Server_SendControllerTransform_Implementation(FBPVRComponentPosRep NewTransform)
{
	// Store new transform and trigger OnRep_Function
	ReplicatedControllerTransform = NewTransform;

	// Server should no longer call this RPC itself, no need to filter for authority here anymore
	OnRep_ReplicatedControllerTransform();
}

bool UGripMotionControllerComponent::Server_SendControllerTransform_Validate(FBPVRComponentPosRep NewTransform)
{
	return true;
	// Optionally check to make sure that player is inside of their bounds and deny it if they aren't?
}

void UGripMotionControllerComponent::FViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (!MotionControllerComponent)
	{
		return;
	}
	FScopeLock ScopeLock(&CritSect);

	if (MotionControllerComponent->bDisableLowLatencyUpdate || !CVarEnableMotionControllerLateUpdate.GetValueOnGameThread())
	{
		return;
	}	

	LateUpdatePrimitives.Reset();
	GatherLateUpdatePrimitives(MotionControllerComponent, LateUpdatePrimitives);

	// Loop through gripped actors
	for (FBPActorGripInformation actor : MotionControllerComponent->GrippedActors)
	{
		// Attached actors will already register as is above, so skipping GripWithAttachTo actors
		if (!actor.Actor /*|| actor.GripAttachmentType == EGripAttachmentType::GripWithAttachTo*/ || actor.bColliding)
			continue;

		UPrimitiveComponent *root = Cast<UPrimitiveComponent>(actor.Actor->GetRootComponent());

		if (!root)
			continue;

		// Get primitive components attached to the actor and update them with the late motion controller offset
		TInlineComponentArray<UPrimitiveComponent*> PrimComps(actor.Actor);
		for (UPrimitiveComponent * PrimitiveComponent : PrimComps)
		{
			GatherLateGripUpdatePrimitives(PrimitiveComponent, LateUpdatePrimitives, root, &actor);
		}
	}
}

void UGripMotionControllerComponent::FViewExtension::GatherLateGripUpdatePrimitives(USceneComponent* Component, TArray<LateUpdatePrimitiveInfo>& Primitives, UPrimitiveComponent * root, FBPActorGripInformation * actor)
{
	// If a scene proxy is present, cache it
	UPrimitiveComponent* PrimitiveComponent = dynamic_cast<UPrimitiveComponent*>(Component);
	if (PrimitiveComponent && PrimitiveComponent->SceneProxy)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveComponent->SceneProxy->GetPrimitiveSceneInfo();
		if (PrimitiveSceneInfo)
		{
			LateUpdatePrimitiveInfo PrimitiveInfo;
			PrimitiveInfo.IndexAddress = PrimitiveSceneInfo->GetIndexAddress();
			PrimitiveInfo.SceneInfo = PrimitiveSceneInfo;
			Primitives.Add(PrimitiveInfo);
		}
	}

	// Gather children proxies
	const int32 ChildCount = Component->GetNumChildrenComponents();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		USceneComponent* ChildComponent = Component->GetChildComponent(ChildIndex);
		if (!ChildComponent)
		{
			continue;
		}

		GatherLateGripUpdatePrimitives(ChildComponent, Primitives, root, actor);
	}
}

bool UGripMotionControllerComponent::GripActor(AActor* ActorToGrip, const FTransform &WorldOffset, bool bWorldOffsetIsRelative, FName OptionalSnapToSocketName, TEnumAsByte<EGripCollisionType> GripCollisionType, TEnumAsByte<EGripAttachmentType> GripAttachmentType/* bool bSweepCollision, bool bInteractiveCollision*/, bool bAllowSetMobility)
{
	if (!bIsServer || !ActorToGrip)
	{
		UE_LOG(LogTemp, Warning, TEXT("VRGripMotionController grab function was passed an invalid or already gripped actor"));
		return false;
	}

	UPrimitiveComponent *root = Cast<UPrimitiveComponent>(ActorToGrip->GetRootComponent());

	if (!root)
	{
		UE_LOG(LogTemp, Warning, TEXT("VRGripMotionController tried to grip an actor without a UPrimitiveComponent Root"));
		return false; // Need a primitive root
	}

	// Has to be movable to work
	if (root->Mobility != EComponentMobility::Movable)
	{
		if (bAllowSetMobility)
			root->SetMobility(EComponentMobility::Movable);
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("VRGripMotionController tried to grip an actor set to static mobility and bAllowSetMobility is false"));
			return false; // It is not movable, can't grip it
		}
	}

	root->IgnoreActorWhenMoving(this->GetOwner(), true);
	// So that events caused by sweep and the like will trigger correctly
	ActorToGrip->AddTickPrerequisiteComponent(this);

	// Don't add the component until after the rendering thread is done with the array
	//FScopeLock ScopeLock(&CritSect);

	FBPActorGripInformation newActorGrip;
	newActorGrip.GripCollisionType = GripCollisionType;
	newActorGrip.GripAttachmentType = GripAttachmentType;
	newActorGrip.Actor = ActorToGrip;
	newActorGrip.bOriginalReplicatesMovement = ActorToGrip->bReplicateMovement;

	switch (GripAttachmentType.GetValue())
	{
	case EGripAttachmentType::GripWithMoveTo:
	{
		if (OptionalSnapToSocketName.IsValid() && root->DoesSocketExist(OptionalSnapToSocketName))
		{
			// I inverse it so that laying out the sockets makes sense
			FTransform sockTrans = root->GetSocketTransform(OptionalSnapToSocketName, ERelativeTransformSpace::RTS_Component);
			newActorGrip.RelativeTransform = sockTrans.Inverse();
			newActorGrip.RelativeTransform.SetScale3D(ActorToGrip->GetActorScale3D());
		}
		else if (bWorldOffsetIsRelative)
			newActorGrip.RelativeTransform = WorldOffset;
		else
			newActorGrip.RelativeTransform = WorldOffset.GetRelativeTransform(this->GetComponentTransform());

	}break;

	/*case EGripAttachmentType::GripWithAttachTo:
	{	
	}break;*/

	default:break;
	}

	GrippedActors.Add(newActorGrip);
	NotifyGrip(newActorGrip);

	return true;
}

bool UGripMotionControllerComponent::DropActor(AActor* ActorToDrop, bool bSimulate)
{
	if (!bIsServer || !ActorToDrop)
	{
		UE_LOG(LogTemp, Warning, TEXT("VRGripMotionController drop function was passed an invalid actor"));
		return false;
	}

	bool bFoundActor = false;

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == ActorToDrop)
		{
			NotifyDrop(GrippedActors[i], bSimulate);
			
			GrippedActors.RemoveAt(i);
			bFoundActor = true;
			break;
		}
	}
	return bFoundActor;
}

void UGripMotionControllerComponent::NotifyGrip_Implementation(const FBPActorGripInformation &NewGrip)
{

	if (bIsServer)
		NewGrip.Actor->SetReplicateMovement(false);

	this->IgnoreActorWhenMoving(NewGrip.Actor, true);


	switch (NewGrip.GripAttachmentType.GetValue())
	{
	
	case EGripAttachmentType::GripWithMoveTo:
	{
		NewGrip.Actor->DisableComponentsSimulatePhysics();
	}

	/*case EGripAttachmentType::GripWithAttachTo:
	{
	}break;*/

	default:break;
	}
}

void UGripMotionControllerComponent::NotifyDrop_Implementation(const FBPActorGripInformation &NewDrop, bool bSimulate)
{

	if (bIsServer)
	{
		NewDrop.Actor->RemoveTickPrerequisiteComponent(this);
		this->IgnoreActorWhenMoving(NewDrop.Actor, false);
		NewDrop.Actor->SetReplicateMovement(NewDrop.bOriginalReplicatesMovement);

		UPrimitiveComponent *root = Cast<UPrimitiveComponent>(NewDrop.Actor->GetRootComponent());

		switch (NewDrop.GripAttachmentType.GetValue())
		{

		case EGripAttachmentType::GripWithMoveTo:
		{
		} break;

		/*case EGripAttachmentType::GripWithAttachTo:
		{
		}break;*/

		default: break;
		}

		if (root)
		{
			root->IgnoreActorWhenMoving(this->GetOwner(), false);
			root->SetSimulatePhysics(bSimulate);
		}
	}
}

bool UGripMotionControllerComponent::AddSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment, USceneComponent * SecondaryPointComponent)
{
	if (!GrippedActorToAddAttachment || !SecondaryPointComponent || !GrippedActors.Num())
		return false;

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == GrippedActorToAddAttachment)
		{
			GrippedActors[i].SecondaryAttachment = SecondaryPointComponent;
			GrippedActors[i].bHasSecondaryAttachment = true;
			return true;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::RemoveSecondaryAttachmentPoint(AActor * GrippedActorToAddAttachment)
{
	if (!GrippedActorToAddAttachment || !GrippedActors.Num())
		return false;

	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == GrippedActorToAddAttachment)
		{
			GrippedActors[i].SecondaryAttachment = nullptr;
			GrippedActors[i].bHasSecondaryAttachment = false;
			return false;
		}
	}

	return false;
}

bool UGripMotionControllerComponent::TeleportMoveGrippedActor(AActor * GrippedActorToMove)
{
	if (!GrippedActorToMove || !GrippedActors.Num())
		return false;

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor == GrippedActorToMove)
		{
			// GetRelativeTransformReverse had some serious fucking floating point errors associated with it that was fucking everything up
			// Not sure whats wrong with the function but I might want to push a patch out eventually
			WorldTransform = GrippedActors[i].RelativeTransform.GetRelativeTransform(InverseTransform);

			// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
			GrippedActors[i].Actor->SetActorTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);
			if (bIsServer)
				GrippedActors[i].bColliding = false;

			return true;
		}
	}

	return false;
}


void UGripMotionControllerComponent::PostTeleportMoveGrippedActors()
{
	if (!GrippedActors.Num())
		return;

	FTransform WorldTransform;
	FTransform InverseTransform = this->GetComponentTransform().Inverse();
	for (int i = GrippedActors.Num() - 1; i >= 0; --i)
	{
		if (GrippedActors[i].Actor)
		{
			// GetRelativeTransformReverse had some serious fucking floating point errors associated with it that was fucking everything up
			// Not sure whats wrong with the function but I might want to push a patch out eventually
			WorldTransform = GrippedActors[i].RelativeTransform.GetRelativeTransform(InverseTransform);

			// Need to use WITH teleport for this function so that the velocity isn't updated and without sweep so that they don't collide
			GrippedActors[i].Actor->SetActorTransform(WorldTransform, false, NULL, ETeleportType::TeleportPhysics);
			if(bIsServer)
				GrippedActors[i].bColliding = false;
		}
		else
		{
			if (bIsServer)
				GrippedActors.RemoveAt(i); // If it got garbage collected then just remove the pointer, won't happen with new uproperty use, but keeping it here anyway
		}
	}
}

void UGripMotionControllerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Moved this here instead of in the polling function, it was ticking once per frame anyway so no loss of perf
	// It doesn't need to be there and now I can pre-check
	// Also epics implementation in the polling function didn't work anyway as it was based off of playercontroller which is not the owner of this controller
	
	// Cache state from the game thread for use on the render thread
	// No need to check if in game thread here as tick always is
	bHasAuthority = IsLocallyControlled();
	bIsServer = IsServer();

	// Server/remote clients don't set the controller position in VR
	// Don't call positional checks and don't create the late update scene view
	if (bHasAuthority)
	{
		if (!ViewExtension.IsValid() && GEngine)
		{
			TSharedPtr< FViewExtension, ESPMode::ThreadSafe > NewViewExtension(new FViewExtension(this));
			ViewExtension = NewViewExtension;
			GEngine->ViewExtensions.Add(ViewExtension);
		}

		// This is the owning player, now you can get the controller's location and rotation from the correct source
		FVector Position;
		FRotator Orientation;
		bTracked = PollControllerState(Position, Orientation);

		if (bTracked)
		{
			SetRelativeLocationAndRotation(Position, Orientation);
		}

		if (!bTracked && !bUseWithoutTracking)
			return; // Don't update anything including location

		// Don't bother with any of this if not replicating transform
		if (bReplicateControllerTransform)
		{
			if (bIsServer) // Skip sending the RPC if this is the server, it doesn't need to.
			{
				ReplicatedControllerTransform.Position = Position;
				ReplicatedControllerTransform.Orientation = Orientation;
			}
			else
			{
				ControllerNetUpdateCount += DeltaTime;

				if (ControllerNetUpdateCount >= (1.0f / ControllerNetUpdateRate))
				{
					ControllerNetUpdateCount = 0.0f;

					if (bTracked)
					{
						ReplicatedControllerTransform.Position = Position;
						ReplicatedControllerTransform.Orientation = Orientation;

						Server_SendControllerTransform(ReplicatedControllerTransform);
					}
				}
			}
		}
	}

	if (GrippedActors.Num())
	{
		FTransform WorldTransform;
		FTransform InverseTransform = this->GetComponentTransform().Inverse();

		for (int i = GrippedActors.Num() - 1; i >= 0; --i)
		{
			if (GrippedActors[i].Actor)
			{
				switch (GrippedActors[i].GripAttachmentType.GetValue())
				{
				case EGripAttachmentType::GripWithMoveTo:
				{
					if (!bIsServer && GrippedActors[i].bColliding)
						continue;

					// GetRelativeTransformReverse had some serious fucking floating point errors associated with it that was fucking everything up
					// Not sure whats wrong with the function but I might want to push a patch out eventually
					WorldTransform = GrippedActors[i].RelativeTransform.GetRelativeTransform(InverseTransform);

					// Need to figure out best default behavior
					/*if (GrippedActors[i].bHasSecondaryAttachment && GrippedActors[i].SecondaryAttachment)
					{
						WorldTransform.SetRotation((WorldTransform.GetLocation() - GrippedActors[i].SecondaryAttachment->GetComponentLocation()).ToOrientationRotator().Quaternion());
					}*/

					if (GrippedActors[i].GripCollisionType == EGripCollisionType::InteractiveCollisionWithPhysics && bIsServer)
					{
						FHitResult HitOut;
						// Need to use without teleport so that the physics velocity is updated for when the actor is released to throw
						GrippedActors[i].Actor->SetActorTransform(WorldTransform, true, &HitOut);

						if (HitOut.bBlockingHit)// && HitOut.IsValidBlockingHit())
						{
							GrippedActors[i].bColliding = true;
							if (!GrippedActors[i].Actor->bReplicateMovement)
								GrippedActors[i].Actor->SetReplicateMovement(true);
						}
						else
						{
							GrippedActors[i].bColliding = false;
							if (GrippedActors[i].Actor->bReplicateMovement) // So we don't call on on change event over and over locally
								GrippedActors[i].Actor->SetReplicateMovement(false);
						}
					}
					else if (GrippedActors[i].GripCollisionType == EGripCollisionType::SweepWithPhysics && bIsServer)
					{
						FVector OriginalPosition(GrippedActors[i].Actor->GetActorLocation());
						FRotator OriginalOrientation(GrippedActors[i].Actor->GetActorRotation());

						FVector NewPosition(WorldTransform.GetTranslation());
						FRotator NewOrientation(WorldTransform.GetRotation());

						// Now sweep collision separately so we can get hits but not have the location altered
						if (bUseWithoutTracking || NewPosition != OriginalPosition || NewOrientation != OriginalOrientation)
						{
							FVector move = NewPosition - OriginalPosition;

							// ComponentSweepMulti does nothing if moving < KINDA_SMALL_NUMBER in distance, so it's important to not try to sweep distances smaller than that. 
							const float MinMovementDistSq = (FMath::Square(4.f*KINDA_SMALL_NUMBER));

							if (bUseWithoutTracking || move.SizeSquared() > MinMovementDistSq || NewOrientation == OriginalOrientation)
							{
								if (CheckActorWithSweep(GrippedActors[i].Actor, move, NewOrientation))
								{

								}
							}
						}

						// Move the actor, we are not offsetting by the hit result anyway
						GrippedActors[i].Actor->SetActorTransform(WorldTransform, false);
					}
					else // This is the only one that is called on the client side in a network environment
					{
						// Move the actor, we are not offsetting by the hit result anyway
						GrippedActors[i].Actor->SetActorTransform(WorldTransform, false);
					}
				}break;

				/*case EGripAttachmentType::GripWithAttachTo: 
				{
				}break;*/

				default: break;
				}
			}
			else
			{
				if (bIsServer)
					GrippedActors.RemoveAt(i); // If it got garbage collected then just remove the pointer, won't happen with new uproperty use, but keeping it here anyway
			}

		}
	}
}

bool UGripMotionControllerComponent::CheckActorWithSweep(AActor * ActorToCheck, FVector Move, FRotator newOrientation)
{
	TArray<FHitResult> Hits;
	// WARNING: HitResult is only partially initialized in some paths. All data is valid only if bFilledHitResult is true.
	FHitResult BlockingHit(NoInit);
	BlockingHit.bBlockingHit = false;
	BlockingHit.Time = 1.f;
	bool bFilledHitResult = false;
	bool bMoved = false;
	bool bIncludesOverlapsAtEnd = false;
	bool bRotationOnly = false;

	UPrimitiveComponent *root = Cast<UPrimitiveComponent>(ActorToCheck->GetRootComponent());

	if (!root || !root->IsQueryCollisionEnabled())
		return false;

	FVector start(root->GetComponentLocation());

	const bool bCollisionEnabled = root->IsQueryCollisionEnabled();

	if (bCollisionEnabled)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (!root->IsRegistered())
		{
			if (ActorToCheck)
			{
				UE_LOG(LogTemp, Warning, TEXT("%s MovedComponent %s not initialized deleteme %d in grip motion controller"), *ActorToCheck->GetName(), *root->GetName(), ActorToCheck->IsPendingKill());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("MovedComponent %s not initialized in grip motion controller"), *root->GetFullName());
			}
		}
#endif

		UWorld* const MyWorld = GetWorld();
		FComponentQueryParams Params(TEXT("sweep_params"), ActorToCheck);

		FCollisionResponseParams ResponseParam;
		root->InitSweepCollisionParams(Params, ResponseParam);

		bool const bHadBlockingHit = MyWorld->ComponentSweepMulti(Hits, root, start, start + Move, newOrientation, Params);

		if (bHadBlockingHit)
		{
			int32 BlockingHitIndex = INDEX_NONE;
			float BlockingHitNormalDotDelta = BIG_NUMBER;
			for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
			{
				const FHitResult& TestHit = Hits[HitIdx];

				// Ignore the owning actor to the motion controller
				if (TestHit.Actor == this->GetOwner())
				{
					if (Hits.Num() == 1)
						return false;
					else
						continue;
				}

				if (TestHit.bBlockingHit && TestHit.IsValidBlockingHit())
				{
					if (TestHit.Time == 0.f)
					{
						// We may have multiple initial hits, and want to choose the one with the normal most opposed to our movement.
						const float NormalDotDelta = (TestHit.ImpactNormal | Move);
						if (NormalDotDelta < BlockingHitNormalDotDelta)
						{
							BlockingHitNormalDotDelta = NormalDotDelta;
							BlockingHitIndex = HitIdx;
						}
					}
					else if (BlockingHitIndex == INDEX_NONE)
					{
						// First non-overlapping blocking hit should be used, if an overlapping hit was not.
						// This should be the only non-overlapping blocking hit, and last in the results.
						BlockingHitIndex = HitIdx;
						break;
					}
					//}
				}
			}

			// Update blocking hit, if there was a valid one.
			if (BlockingHitIndex >= 0)
			{
				BlockingHit = Hits[BlockingHitIndex];
				bFilledHitResult = true;
			}
		}
	}

	// Handle blocking hit notifications. Avoid if pending kill (which could happen after overlaps).
	if (BlockingHit.bBlockingHit && !root->IsPendingKill())
	{
		check(bFilledHitResult);
		if (root->IsDeferringMovementUpdates())
		{
			FScopedMovementUpdate* ScopedUpdate = root->GetCurrentScopedMovement();
			ScopedUpdate->AppendBlockingHitAfterMove(BlockingHit);
		}
		else
		{
			root->DispatchBlockingHit(*ActorToCheck, BlockingHit);
		}
	}

	return true;

}

//=============================================================================
bool UGripMotionControllerComponent::PollControllerState(FVector& Position, FRotator& Orientation)
{
	// THIS IS WRONG, it would have never worked!!!
	/*if (IsInGameThread())
	{
		// Cache state from the game thread for use on the render thread
		const APlayerController* Actor = Cast<APlayerController>(GetOwner());
		bHasAuthority = !Actor || Actor->IsLocalPlayerController();
	}*/

	if ((PlayerIndex != INDEX_NONE) && bHasAuthority)
	{

#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION <= 11
		for (auto MotionController : GEngine->MotionControllerDevices)
		{
			if ((MotionController != nullptr) && MotionController->GetControllerOrientationAndPosition(PlayerIndex, Hand, Orientation, Position))
			{
				CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, Hand);
				return true;
			}
		}
#else
		// New iteration and retrieval for 4.12
		TArray<IMotionController*> MotionControllers = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
		for (auto MotionController : MotionControllers)
		{
			if ((MotionController != nullptr) && MotionController->GetControllerOrientationAndPosition(PlayerIndex, Hand, Orientation, Position))
			{
				CurrentTrackingStatus = MotionController->GetControllerTrackingStatus(PlayerIndex, Hand);
				return true;
			}
		}
#endif
	}
	return false;
}

//=============================================================================
void UGripMotionControllerComponent::FViewExtension::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	if (!MotionControllerComponent)
	{
		return;
	}
	FScopeLock ScopeLock(&CritSect);

	if (MotionControllerComponent->bDisableLowLatencyUpdate || !CVarEnableMotionControllerLateUpdate.GetValueOnRenderThread())
	{
		return;
	}

	// Poll state for the most recent controller transform
	FVector Position;
	FRotator Orientation;

	//bool bRenderTracked = bRenderTracked = MotionControllerComponent->PollControllerState(Position, Orientation);
	if (!MotionControllerComponent->PollControllerState(Position, Orientation))
	{
		return;
	}

	if (LateUpdatePrimitives.Num())
	{
		// Calculate the late update transform that will rebase all children proxies within the frame of reference
		FTransform OldLocalToWorldTransform = MotionControllerComponent->CalcNewComponentToWorld(MotionControllerComponent->GetRelativeTransform());
		FTransform NewLocalToWorldTransform = MotionControllerComponent->CalcNewComponentToWorld(FTransform(Orientation, Position));
		FMatrix LateUpdateTransform = (OldLocalToWorldTransform.Inverse() * NewLocalToWorldTransform).ToMatrixWithScale();
		
		FPrimitiveSceneInfo* RetrievedSceneInfo;
		FPrimitiveSceneInfo* CachedSceneInfo;
		
		// Apply delta to the affected scene proxies
		for (auto PrimitiveInfo : LateUpdatePrimitives)
		{
			RetrievedSceneInfo = InViewFamily.Scene->GetPrimitiveSceneInfo(*PrimitiveInfo.IndexAddress);
			CachedSceneInfo = PrimitiveInfo.SceneInfo;

			// If the retrieved scene info is different than our cached scene info then the primitive was removed from the scene
			if (CachedSceneInfo == RetrievedSceneInfo && CachedSceneInfo->Proxy)
			{
				CachedSceneInfo->Proxy->ApplyLateUpdateTransform(LateUpdateTransform);
			}
		}
		LateUpdatePrimitives.Reset();
	}
}

void UGripMotionControllerComponent::FViewExtension::GatherLateUpdatePrimitives(USceneComponent* Component, TArray<LateUpdatePrimitiveInfo>& Primitives)
{
	// If a scene proxy is present, cache it
	UPrimitiveComponent* PrimitiveComponent = dynamic_cast<UPrimitiveComponent*>(Component);
	if (PrimitiveComponent && PrimitiveComponent->SceneProxy)
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveComponent->SceneProxy->GetPrimitiveSceneInfo();
		if (PrimitiveSceneInfo)
		{
			LateUpdatePrimitiveInfo PrimitiveInfo;
			PrimitiveInfo.IndexAddress = PrimitiveSceneInfo->GetIndexAddress();
			PrimitiveInfo.SceneInfo = PrimitiveSceneInfo;
			Primitives.Add(PrimitiveInfo);
		}
	}

	// Gather children proxies
	const int32 ChildCount = Component->GetNumChildrenComponents();
	for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
	{
		USceneComponent* ChildComponent = Component->GetChildComponent(ChildIndex);
		if (!ChildComponent)
		{
			continue;
		}

		GatherLateUpdatePrimitives(ChildComponent, Primitives);
	}
}