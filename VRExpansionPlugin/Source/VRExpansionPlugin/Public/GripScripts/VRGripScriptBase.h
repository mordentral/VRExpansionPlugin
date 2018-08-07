// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "GripMotionControllerComponent.h"
//#include "MotionControllerComponent.h"
//#include "VRGripInterface.h"
//#include "GameplayTagContainer.h"
//#include "GameplayTagAssetInterface.h"
//#include "VRInteractibleFunctionLibrary.h"
//#include "PhysicsEngine/ConstraintInstance.h"


#include "VRGripScriptBase.generated.h"


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


UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, Abstract, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRGripScriptBase : public UObject
{
	GENERATED_BODY()
public:
	UVRGripScriptBase(const FObjectInitializer& ObjectInitializer);
	// Need to add TICK and BeginPlay implementations

	// Returns if the script is currently active and should be used
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript")
		bool IsScriptActive();
	virtual bool IsScriptActive_Implementation();

	// Is currently active helper variable, normally returned from IsScriptActive()
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "DefaultSettings")
	bool bIsActive;

	// Returns if the script is going to modify the world transform of the grip
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript")
	EGSTransformOverrideType GetWorldTransformOverrideType();
	virtual EGSTransformOverrideType GetWorldTransformOverrideType_Implementation();

	// Whether this script overrides or modifies the world transform
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "DefaultSettings")
	EGSTransformOverrideType WorldTransformOverrideType;


	// Returns if the script wants auto drop to be ignored
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript")
		bool Wants_DenyAutoDrop();
	virtual bool Wants_DenyAutoDrop_Implementation();

	// Returns if the script is currently active and should be used
	/*UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript")
	bool Wants_DenyTeleport();
	virtual bool Wants_DenyTeleport_Implementation();*/
	


	// Returns the current world transform of the owning object (or root comp of if it is an actor)
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
	FTransform GetParentTransform()
	{
		UObject * ParentObj = this->GetOuter();

		if (USceneComponent * PrimParent = Cast<USceneComponent>(ParentObj))
		{
			return PrimParent->GetComponentTransform();
		}
		else if (AActor * ParentActor = Cast<AActor>(ParentObj))
		{
			return ParentActor->GetActorTransform();
		}

		return FTransform::Identity;
	}

	// Returns the current world transform of the owning object (or root comp of if it is an actor)
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		UObject * GetParent()
	{
		return this->GetOuter();
	}


	// Implement VRGripInterface so that we can add functionality with it

	// Useful functions to override in c++ for functionality
	//virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction);

	// Not all scripts will require this function, specific ones that use things like Lever logic however will. Best to call it.
	//UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript|Init")
		//void BeginPlay();
		//virtual void BeginPlay_Implementation();

	// Overrides or Modifies the world transform with this grip script
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript|Steps")
		void GetWorldTransform(UGripMotionControllerComponent * GrippingController, float DeltaTime, UPARAM(ref) FTransform & WorldTransform, const FTransform &ParentTransform, UPARAM(ref) FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface);
		virtual void GetWorldTransform_Implementation(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface);

	// Event triggered on the interfaced object when gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		void OnGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);
		virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		void OnGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);
		virtual void OnGripRelease_Implementation(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation, bool bWasSocketed = false);

};
