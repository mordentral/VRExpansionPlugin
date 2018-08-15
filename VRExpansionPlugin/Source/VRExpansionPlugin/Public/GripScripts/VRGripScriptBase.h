// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
//#include "GripMotionControllerComponent.h"
//#include "MotionControllerComponent.h"
//#include "VRGripInterface.h"
//#include "GameplayTagContainer.h"
//#include "GameplayTagAssetInterface.h"
//#include "VRInteractibleFunctionLibrary.h"
//#include "PhysicsEngine/ConstraintInstance.h"


#include "VRGripScriptBase.generated.h"

class UGripMotionControllerComponent;

UENUM(Blueprintable)
enum class EGSTransformOverrideType : uint8
{
	/** Does not alter the world transform */
	None,

	/* Overrides the world transform */
	OverridesWorldTransform,

	/* Modifies the world transform*/
	ModifiesWorldTransform
};

UCLASS(NotBlueprintable, EditInlineNew, DefaultToInstanced, Abstract, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRGripScriptBase : public UObject
{
	GENERATED_BODY()
public:

	//#TODO: Add OnSecondaryGrip and OnSecondaryGripRelease?

	UVRGripScriptBase(const FObjectInitializer& ObjectInitializer);

	/*bool IsSupportedForNetworking() const override
	{
		return bRequiresReplicationSupport || IsNameStableForNetworking();
	}*/
	// I don't need to do this, there should be no dynamic script spawning and they are all name stable by default
	
	// Returns if the script is currently active and should be used
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		bool IsScriptActive();
	virtual bool IsScriptActive_Implementation();

	// Is currently active helper variable, normally returned from IsScriptActive()
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "DefaultSettings")
	bool bIsActive;

	// Returns if the script is going to modify the world transform of the grip
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
	EGSTransformOverrideType GetWorldTransformOverrideType();
	virtual EGSTransformOverrideType GetWorldTransformOverrideType_Implementation();

	// Whether this script overrides or modifies the world transform
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "DefaultSettings")
	EGSTransformOverrideType WorldTransformOverrideType;

	// Returns if the script wants auto drop to be ignored
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		bool Wants_DenyAutoDrop();
	virtual bool Wants_DenyAutoDrop_Implementation();

	// Returns if the script is currently active and should be used
	/*UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript")
	bool Wants_DenyTeleport();
	virtual bool Wants_DenyTeleport_Implementation();*/
	

	// If true then this will tell the owning grippable that it needs to be replicated, forcing all other attached scripts to also be replicated
	// If the other scripts don't replicate any variables then they will have minimal overhead.
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "DefaultSettings")
		bool bRequiresReplicationSupport;

	virtual void GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const override;
	virtual bool CallRemoteFunction(UFunction * Function, void * Parms, FOutParmRec * OutParms, FFrame * Stack) override;
	virtual int32 GetFunctionCallspace(UFunction * Function, void * Parameters, FFrame * Stack) override;

	// Returns the current world transform of the owning object (or root comp of if it is an actor)
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
	FTransform GetParentTransform(bool bGetWorldTransform = true)
	{
		UObject * ParentObj = this->GetOuter();

		if (USceneComponent * PrimParent = Cast<USceneComponent>(ParentObj))
		{
			return bGetWorldTransform ? PrimParent->GetComponentTransform() : PrimParent->GetRelativeTransform();
		}
		else if (AActor * ParentActor = Cast<AActor>(ParentObj))
		{
			return ParentActor->GetActorTransform();
		}

		return FTransform::Identity;
	}

	// Returns the parent component or actor to this
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		UObject * GetParent()
	{
		return this->GetOuter();
	}

	// Returns the owning actor 
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		AActor * GetOwner()
	{
		return Cast<AActor>(this->GetOutermost());
	}

	// If we have authority 
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		bool HasAuthority()
	{
		if (AActor * MyOwner = Cast<AActor>(this->GetOutermost()))
		{
			return MyOwner->Role == ROLE_Authority;
		}

		return false;
	}

	// Not all scripts will require this function, specific ones that use things like Lever logic however will. Best to call it.
	// Grippables will automatically call this, however if you manually spawn a grip script during play or you make your own
	// Interfaced grip object and give it grippables, YOU will be required to call this event on them.
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript|Init")
		void OnBeginPlay(UObject * CallingOwner);
		virtual void OnBeginPlay_Implementation(UObject * CallingOwner);

	// Overrides or Modifies the world transform with this grip script
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript|Steps")
		void GetWorldTransform(UGripMotionControllerComponent * GrippingController, float DeltaTime, /*UPARAM(ref)*/ FTransform & WorldTransform, const FTransform &ParentTransform, UPARAM(ref) FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface);
		virtual void GetWorldTransform_Implementation(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface);

	// Event triggered on the interfaced object when gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		void OnGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);
		virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
	void OnGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);

	// Event triggered on the interfaced object when secondary gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
	void OnSecondaryGrip(UGripMotionControllerComponent * Controller, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation);
	virtual void OnSecondaryGrip_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
	void OnSecondaryGripRelease(UGripMotionControllerComponent * Controller, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation);
	virtual void OnSecondaryGripRelease_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation);
	

	// Begin virtual funcs

	virtual void CallCorrect_GetWorldTransform(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface)
	{
		GetWorldTransform_Implementation(OwningController, DeltaTime, WorldTransform, ParentTransform, Grip, actor, root, bRootHasInterface, bActorHasInterface);
	}

	virtual bool CallCorrect_Wants_DenyAutoDrop()
	{
		return Wants_DenyAutoDrop_Implementation();
	}

	virtual bool CallCorrect_IsScriptActive()
	{
		return IsScriptActive_Implementation();
	}

	virtual EGSTransformOverrideType CallCorrect_GetWorldTransformOverrideType()
	{
		return GetWorldTransformOverrideType_Implementation();
	}
};


UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, Abstract, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRGripScriptBaseBP : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	virtual void CallCorrect_GetWorldTransform(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface) override
	{
		GetWorldTransform(OwningController, DeltaTime, WorldTransform, ParentTransform, Grip, actor, root, bRootHasInterface, bActorHasInterface);
	}

	virtual bool CallCorrect_Wants_DenyAutoDrop() override
	{
		return Wants_DenyAutoDrop();
	}

	virtual bool CallCorrect_IsScriptActive() override
	{
		return IsScriptActive();
	}

	virtual EGSTransformOverrideType CallCorrect_GetWorldTransformOverrideType() override
	{
		return GetWorldTransformOverrideType();
	}

};