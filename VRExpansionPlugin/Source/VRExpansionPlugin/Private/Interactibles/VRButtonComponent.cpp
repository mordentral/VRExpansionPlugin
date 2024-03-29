// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Interactibles/VRButtonComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VRButtonComponent)

#include "Net/UnrealNetwork.h"
//#include "VRGripInterface.h"
#include "GripMotionControllerComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif

  //=============================================================================
UVRButtonComponent::UVRButtonComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	this->SetGenerateOverlapEvents(true);
	this->PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = true;

	LastToggleTime = 0.0f;
	DepressDistance = 8.0f;
	ButtonEngageDepth = 8.0f;
	DepressSpeed = 50.0f;

	ButtonAxis = EVRInteractibleAxis::Axis_Z;
	ButtonType = EVRButtonType::Btn_Toggle_Return;

	MinTimeBetweenEngaging = 0.1f;

	bIsEnabled = true;
	StateChangeAuthorityType = EVRStateChangeAuthorityType::CanChangeState_All;
	bButtonState = false;

	this->SetCollisionResponseToAllChannels(ECR_Overlap);

	bSkipOverlapFiltering = false;
	InitialRelativeTransform = FTransform::Identity;

	bReplicateMovement = false;
}

//=============================================================================
UVRButtonComponent::~UVRButtonComponent()
{
}

void UVRButtonComponent::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// For std properties
	FDoRepLifetimeParams PushModelParams{ COND_None, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UVRButtonComponent, InitialRelativeTransform, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVRButtonComponent, bReplicateMovement, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UVRButtonComponent, StateChangeAuthorityType, PushModelParams);

	// For properties with special conditions
	FDoRepLifetimeParams PushModelParamsWithCondition{ COND_InitialOnly, REPNOTIFY_OnChanged, /*bIsPushBased=*/true };

	DOREPLIFETIME_WITH_PARAMS_FAST(UVRButtonComponent, bButtonState, PushModelParamsWithCondition);
}

void UVRButtonComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);

	// Replicate the levers initial transform if we are replicating movement
	//DOREPLIFETIME_ACTIVE_OVERRIDE(UVRButtonComponent, InitialRelativeTransform, bReplicateMovement);
	
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeScale3D, bReplicateMovement);
}

void UVRButtonComponent::OnRegister()
{
	Super::OnRegister();
	ResetInitialButtonLocation();
}

void UVRButtonComponent::BeginPlay()
{
	// Call the base class 
	Super::BeginPlay();

	SetButtonToRestingPosition();

	OnComponentBeginOverlap.AddUniqueDynamic(this, &UVRButtonComponent::OnOverlapBegin);
	OnComponentEndOverlap.AddUniqueDynamic(this, &UVRButtonComponent::OnOverlapEnd);
}

void UVRButtonComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	// Call supers tick (though I don't think any of the base classes to this actually implement it)
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float WorldTime = GetWorld()->GetRealTimeSeconds();

	if (IsValid(LocalInteractingComponent))
	{
		// If button was set to inactive during use
		if (!bIsEnabled)
		{
			// Remove interacting component and return, next tick will begin lerping back
			LocalInteractingComponent = nullptr;
			return;
		}

		FTransform OriginalBaseTransform = CalcNewComponentToWorld(InitialRelativeTransform);

		float CheckDepth = FMath::Clamp(GetAxisValue(InitialLocation) - GetAxisValue(OriginalBaseTransform.InverseTransformPosition(LocalInteractingComponent->GetComponentLocation())), 0.0f, DepressDistance);

		if (CheckDepth > 0.0f)
		{

			float ClampMinDepth = 0.0f;
			
			// If active and a toggled stay, then clamp min to the toggled stay location
			if (ButtonType == EVRButtonType::Btn_Toggle_Stay && bButtonState)
				ClampMinDepth = -(ButtonEngageDepth + (1.e-2f)); // + NOT_SO_UE_KINDA_SMALL_NUMBER

			float NewDepth = FMath::Clamp(GetAxisValue(InitialComponentLoc) + (-CheckDepth), -DepressDistance, ClampMinDepth);
			this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(SetAxisValue(NewDepth)), false);

			if (ButtonType == EVRButtonType::Btn_Toggle_Return || ButtonType == EVRButtonType::Btn_Toggle_Stay)
			{
				if ((StateChangeAuthorityType == EVRStateChangeAuthorityType::CanChangeState_All) ||
					(StateChangeAuthorityType == EVRStateChangeAuthorityType::CanChangeState_Server && GetNetMode() < ENetMode::NM_Client) ||
					(StateChangeAuthorityType == EVRStateChangeAuthorityType::CanChangeState_Owner && IsValid(LocalLastInteractingActor) && LocalLastInteractingActor->HasLocalNetOwner()))
				{
					if (!bToggledThisTouch && NewDepth <= (-ButtonEngageDepth) + UE_KINDA_SMALL_NUMBER && (WorldTime - LastToggleTime) >= MinTimeBetweenEngaging)
					{
						LastToggleTime = WorldTime;
						bToggledThisTouch = true;
						bButtonState = !bButtonState;
						ReceiveButtonStateChanged(bButtonState, LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
						OnButtonStateChanged.Broadcast(bButtonState, LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
					}
				}
			}
		}
	}
	else
	{
		// Std precision tolerance should be fine
		if (this->GetRelativeLocation().Equals(GetTargetRelativeLocation()))
		{
			this->SetComponentTickEnabled(false);

			OnButtonEndInteraction.Broadcast(LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
			ReceiveButtonEndInteraction(LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());

			LocalInteractingComponent = nullptr; // Just reset it here so it only does it once
			LocalLastInteractingComponent = nullptr;
		}
		else
			this->SetRelativeLocation(FMath::VInterpConstantTo(this->GetRelativeLocation(), GetTargetRelativeLocation(), DeltaTime, DepressSpeed), false);
	}


	// Press buttons always get checked, both during press AND during lerping for if they are active or not.
	if (ButtonType == EVRButtonType::Btn_Press)
	{
		if ((StateChangeAuthorityType == EVRStateChangeAuthorityType::CanChangeState_All) ||
			(StateChangeAuthorityType == EVRStateChangeAuthorityType::CanChangeState_Server && GetNetMode() < ENetMode::NM_Client) ||
			(StateChangeAuthorityType == EVRStateChangeAuthorityType::CanChangeState_Owner && IsValid(LocalLastInteractingActor) && LocalLastInteractingActor->HasLocalNetOwner()))
		{
			// Check for if we should set the state of the button, done here as for the press button the lerp counts for input
			bool bCheckState = (GetAxisValue(InitialRelativeTransform.InverseTransformPosition(this->GetRelativeLocation())) <= (-ButtonEngageDepth) + UE_KINDA_SMALL_NUMBER);
			if (bButtonState != bCheckState && (WorldTime - LastToggleTime) >= MinTimeBetweenEngaging)

			{
				LastToggleTime = WorldTime;
				bButtonState = bCheckState;
				ReceiveButtonStateChanged(bButtonState, LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
				OnButtonStateChanged.Broadcast(bButtonState, LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
			}
		}
	}

}

bool UVRButtonComponent::IsValidOverlap_Implementation(UPrimitiveComponent * OverlapComponent)
{

	// Early out on the simple checks
	if (!OverlapComponent || OverlapComponent == GetAttachParent() || OverlapComponent->GetAttachParent() == GetAttachParent())
		return false;

	// Should return faster checking for owning character
	AActor * OverlapOwner = OverlapComponent->GetOwner();

	if (IsValid(OverlapOwner))
	{
		if (OverlapOwner->IsA(ACharacter::StaticClass()))
			return true;

		const AActor* OverlapNetOwner = OverlapOwner->GetNetOwner();
		if (IsValid(OverlapNetOwner) && (OverlapNetOwner->IsA(APlayerController::StaticClass()) || OverlapNetOwner->IsA(ACharacter::StaticClass())))
			return true;
	}

	// Because epic motion controllers are not owned by characters have to check here too in case someone implements it like that
	// Now since our grip controllers are a subclass to the std ones we only need to check for the base one instead of both.
	USceneComponent * OurAttachParent = OverlapComponent->GetAttachParent();
	if (OurAttachParent && OurAttachParent->IsA(UMotionControllerComponent::StaticClass()))
		return true;

	// Now check for if it is a grippable object and if it is currently held
	if (OverlapComponent->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		TArray<FBPGripPair> Controllers;
		bool bIsHeld;
		IVRGripInterface::Execute_IsHeld(OverlapComponent, Controllers, bIsHeld);

		if (bIsHeld)
			return true;
	}
	else if(OverlapOwner && OverlapOwner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		TArray<FBPGripPair> Controllers;
		bool bIsHeld;
		IVRGripInterface::Execute_IsHeld(OverlapOwner, Controllers, bIsHeld);

		if (bIsHeld)
			return true;
	}

	return false;
}

void UVRButtonComponent::SetLastInteractingActor()
{

	// Early out on the simple checks
	if (!IsValid(LocalInteractingComponent) || LocalInteractingComponent == GetAttachParent() || LocalInteractingComponent->GetAttachParent() == GetAttachParent())
	{
		LocalLastInteractingActor = nullptr;
		LocalLastInteractingComponent = nullptr;
		return;
	}

	LocalLastInteractingComponent = LocalInteractingComponent;

	// Should return faster checking for owning character
	AActor * OverlapOwner = LocalInteractingComponent->GetOwner();
	if (OverlapOwner && OverlapOwner->IsA(ACharacter::StaticClass()))
	{
		LocalLastInteractingActor = OverlapOwner;
		return;
	}

	// Now check for if it is a grippable object and if it is currently held
	if (LocalInteractingComponent->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		TArray<FBPGripPair> Controllers;
		bool bIsHeld;
		IVRGripInterface::Execute_IsHeld(LocalLastInteractingComponent.Get(), Controllers, bIsHeld);

		if (bIsHeld && Controllers.Num())
		{
			AActor * ControllerOwner = Controllers[0].HoldingController != nullptr ? Controllers[0].HoldingController->GetOwner() : nullptr;
			if (ControllerOwner)
			{
				LocalLastInteractingActor = ControllerOwner;
				return;
			}
		}
	}
	else if (OverlapOwner && OverlapOwner->GetClass()->ImplementsInterface(UVRGripInterface::StaticClass()))
	{
		TArray<FBPGripPair> Controllers;
		bool bIsHeld;
		IVRGripInterface::Execute_IsHeld(OverlapOwner, Controllers, bIsHeld);

		if (bIsHeld && Controllers.Num())
		{
			AActor * ControllerOwner = Controllers[0].HoldingController != nullptr ? Controllers[0].HoldingController->GetOwner() : nullptr;
			if (ControllerOwner)
			{
				LocalLastInteractingActor = ControllerOwner;
				return;
			}
		}
	}

	// Fall back to the owner, wasn't held and wasn't a character
	if (OverlapOwner)
	{
		LocalLastInteractingActor = OverlapOwner;
		return;
	}

	LocalLastInteractingActor = nullptr;
	return;
}

void UVRButtonComponent::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Other Actor is the actor that triggered the event. Check that is not ourself.  
	if (bIsEnabled && !IsValid(LocalInteractingComponent) && (bSkipOverlapFiltering || IsValidOverlap(OtherComp)))
	{
		LocalInteractingComponent = OtherComp;

		FTransform OriginalBaseTransform = CalcNewComponentToWorld(InitialRelativeTransform);
		FVector loc = LocalInteractingComponent->GetComponentLocation();
		InitialLocation = OriginalBaseTransform.InverseTransformPosition(LocalInteractingComponent->GetComponentLocation());
		InitialComponentLoc = OriginalBaseTransform.InverseTransformPosition(this->GetComponentLocation());
		bToggledThisTouch = false;

		this->SetComponentTickEnabled(true);

		if (LocalInteractingComponent != LocalLastInteractingComponent.Get())
		{
			SetLastInteractingActor();
			OnButtonBeginInteraction.Broadcast(LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
			ReceiveButtonBeginInteraction(LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
		}
	}
}

void UVRButtonComponent::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (IsValid(LocalInteractingComponent) && OtherComp == LocalInteractingComponent)
	{
		LocalInteractingComponent = nullptr;
	}
}

FVector UVRButtonComponent::GetTargetRelativeLocation()
{
	// If target is the half pressed
	if (ButtonType == EVRButtonType::Btn_Toggle_Stay && bButtonState)
	{
		// 1.e-2f = MORE_UE_KINDA_SMALL_NUMBER
		return InitialRelativeTransform.TransformPosition(SetAxisValue(-(ButtonEngageDepth + (1.e-2f))));
	}

	// Else return going all the way back
	return InitialRelativeTransform.GetTranslation();

}

void UVRButtonComponent::SetButtonToRestingPosition(bool bLerpToPosition)
{
	switch (ButtonType)
	{
	case EVRButtonType::Btn_Press:
	{
	}break;
	case EVRButtonType::Btn_Toggle_Return:
	{}break;
	case EVRButtonType::Btn_Toggle_Stay:
	{
		if (!bLerpToPosition)
		{
			float ClampMinDepth = 0.0f;

			// If active and a toggled stay, then clamp min to the toggled stay location
			if (bButtonState)
				ClampMinDepth = -(ButtonEngageDepth + (1.e-2f)); // + NOT_SO_UE_KINDA_SMALL_NUMBER

			float NewDepth = FMath::Clamp(ClampMinDepth, -DepressDistance, ClampMinDepth);
			this->SetRelativeLocation(InitialRelativeTransform.TransformPosition(SetAxisValue(NewDepth)), false);
		}
		else
			this->SetComponentTickEnabled(true); // This will trigger the lerp to resting position

	}break;
	default:break;
	}
}

void UVRButtonComponent::SetButtonState(bool bNewButtonState, bool bCallButtonChangedEvent, bool bSnapIntoPosition)
{
	// No change
	if (bButtonState == bNewButtonState)
		return;

	bButtonState = bNewButtonState;
	SetButtonToRestingPosition(!bSnapIntoPosition);
	LastToggleTime = GetWorld()->GetRealTimeSeconds();

	if (bCallButtonChangedEvent)
	{
		ReceiveButtonStateChanged(bButtonState, LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
		OnButtonStateChanged.Broadcast(bButtonState, LocalLastInteractingActor.Get(), LocalLastInteractingComponent.Get());
	}
}

void UVRButtonComponent::ResetInitialButtonLocation()
{
	// Get our initial relative transform to our parent (or not if un-parented).
	InitialRelativeTransform = this->GetRelativeTransform();
}

bool UVRButtonComponent::IsButtonInUse()
{
	return IsValid(LocalInteractingComponent);
}

float UVRButtonComponent::GetAxisValue(FVector CheckLocation)
{
	switch (ButtonAxis)
	{
	case EVRInteractibleAxis::Axis_X:
		return CheckLocation.X; break;
	case EVRInteractibleAxis::Axis_Y:
		return CheckLocation.Y; break;
	case EVRInteractibleAxis::Axis_Z:
		return CheckLocation.Z; break;
	default:return 0.0f; break;
	}
}

FVector UVRButtonComponent::SetAxisValue(float SetValue)
{
	FVector vec = FVector::ZeroVector;

	switch (ButtonAxis)
	{
	case EVRInteractibleAxis::Axis_X:
		vec.X = SetValue; break;
	case EVRInteractibleAxis::Axis_Y:
		vec.Y = SetValue; break;
	case EVRInteractibleAxis::Axis_Z:
		vec.Z = SetValue; break;
	}

	return vec;
}

void UVRButtonComponent::SetReplicateMovement(bool bNewReplicateMovement)
{
	bReplicateMovement = bNewReplicateMovement;
#if WITH_PUSH_MODEL
		MARK_PROPERTY_DIRTY_FROM_NAME(UVRButtonComponent, bReplicateMovement, this);
#endif
}

void UVRButtonComponent::SetStateChangeAuthorityType(EVRStateChangeAuthorityType NewStateChangeAuthorityType)
{
	StateChangeAuthorityType = NewStateChangeAuthorityType;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UVRButtonComponent, StateChangeAuthorityType, this);
#endif
}

void UVRButtonComponent::SetButtonState(bool bNewButtonState)
{
	bButtonState = bNewButtonState;
#if WITH_PUSH_MODEL
	MARK_PROPERTY_DIRTY_FROM_NAME(UVRButtonComponent, bButtonState, this);
#endif
}