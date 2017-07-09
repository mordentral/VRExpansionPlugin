// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "MotionControllerComponent.h"
#include "VRGripInterface.h"
#include "PhysicsEngine/ConstraintInstance.h"

#include "PhysicsPublic.h"

#if WITH_PHYSX
#include "PhysXSupport.h"
#endif // WITH_PHYSX


#include "VRLeverComponent.generated.h"


UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRLeverComponent : public UStaticMeshComponent, public IVRGripInterface, public IGameplayTagAssetInterface
{
	GENERATED_UCLASS_BODY()

	~UVRLeverComponent();


	// ------------------------------------------------
	// Gameplay tag interface
	// ------------------------------------------------

	/** Overridden to return requirements tags */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
	{
		TagContainer = GameplayTags;
	}

	/** Tags that are set on this object */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "GameplayTags")
		FGameplayTagContainer GameplayTags;

	// End Gameplay Tag Interface

	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;
	

	// Requires bReplicates to be true for the component
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface")
		bool bRepGameplayTags;
		

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		bool bIsPhysicsLever;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		EVRInteractibleAxis LeverRotationAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRLeverComponent")
		float LeverLimit;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		EGripMovementReplicationSettings MovementReplicationSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRGripInterface")
		float BreakDistance;

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
		bool bIsHeld; // Set on grip notify, not net serializing

	UPROPERTY(BlueprintReadWrite, Category = "VRGripInterface")
		UGripMotionControllerComponent * HoldingController; // Set on grip notify, not net serializing

	USceneComponent * ParentComponent;


	virtual void OnUnregister() override;;

#if WITH_PHYSX
	physx::PxD6Joint* HandleData;
	int32 SceneIndex;
#endif

	bool DestroyConstraint()
	{
	#if WITH_PHYSX
		if (HandleData)
		{
			// use correct scene
			PxScene* PScene = GetPhysXSceneFromIndex(SceneIndex);
			if (PScene)
			{
				SCOPED_SCENE_WRITE_LOCK(PScene);
				// Destroy joint.
				HandleData->release();
			}

			HandleData = NULL;
			return true;
		}
		else
		{
			return false;
		}
	#endif // WITH_PHYSX

		return true;
	}

	bool SetupConstraint()
	{

#if WITH_PHYSX

		if (HandleData)
			return true;

		// Get the PxRigidDynamic that we want to grab.
		FBodyInstance* rBodyInstance = this->GetBodyInstance(NAME_None);
		if (!rBodyInstance)
		{
			return false;
		}

		
		ExecuteOnPxRigidDynamicReadWrite(rBodyInstance, [&](PxRigidDynamic* Actor)
		{
			PxScene* Scene = Actor->getScene();

			PxTransform KinPose;
			PxVec3 KinLocation;
			PxTransform GrabbedActorPose;

			// If we don't already have a handle - make one now.
			if (!HandleData)
			{
				PxD6Joint* NewJoint = NULL;
				PxRigidDynamic * ParentBody = NULL;

				if (ParentComponent)
				{
					UPrimitiveComponent * PrimComp = Cast<UPrimitiveComponent>(ParentComponent);

					if (PrimComp)
						ParentBody = PrimComp->BodyInstance.GetPxRigidDynamic_AssumesLocked();
				}
				
				PxTransform LocalParentTrans = PxTransform(PxIdentity);
				if (ParentBody)
				{
					// Get local space location of the constraint end for our parent if we have one
					LocalParentTrans = ParentBody->getGlobalPose().getInverse() * Actor->getGlobalPose();
				}
				else
				{
					// If no parent this will fall back to world space of self
					LocalParentTrans = Actor->getGlobalPose();
				}

				NewJoint = PxD6JointCreate(Scene->getPhysics(), ParentBody, LocalParentTrans, Actor, PxTransform(PxIdentity));
				if (!NewJoint)
				{
					HandleData = NULL;
				}
				else
				{
					// No constraint instance
					NewJoint->userData = NULL; // don't need
					HandleData = NewJoint;

					// Remember the scene index that the handle joint/actor are in.
					FPhysScene* RBScene = FPhysxUserData::Get<FPhysScene>(Scene->userData);
					const uint32 SceneType = rBodyInstance->UseAsyncScene(RBScene) ? PST_Async : PST_Sync;
					SceneIndex = RBScene->PhysXSceneIndex[SceneType];

					// Pretty Much Unbreakable
					NewJoint->setBreakForce(PX_MAX_REAL, PX_MAX_REAL);
					NewJoint->setConstraintFlag(PxConstraintFlag::ePROJECTION, false);
					
					NewJoint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);

					PxConstraintFlags Flags = NewJoint->getConstraintFlags();

					// False flags
					Flags |= PxConstraintFlag::ePROJECTION;
					Flags |= PxConstraintFlag::eCOLLISION_ENABLED;
					
					// True flags
					//Flags &= ~PxConstraintFlag::eCOLLISION_ENABLED;

					NewJoint->setConstraintFlags(Flags);
					
					// Setting up the joint
					NewJoint->setMotion(PxD6Axis::eX, PxD6Motion::eLOCKED);
					NewJoint->setMotion(PxD6Axis::eY, PxD6Motion::eLOCKED);
					NewJoint->setMotion(PxD6Axis::eZ, PxD6Motion::eLOCKED);

					NewJoint->setMotion(PxD6Axis::eTWIST, LeverRotationAxis == EVRInteractibleAxis::Axis_X ? PxD6Motion::eLIMITED : PxD6Motion::eLOCKED);
					NewJoint->setMotion(PxD6Axis::eSWING1, LeverRotationAxis == EVRInteractibleAxis::Axis_Y ? PxD6Motion::eLIMITED : PxD6Motion::eLOCKED);
					NewJoint->setMotion(PxD6Axis::eSWING2, LeverRotationAxis == EVRInteractibleAxis::Axis_Z ? PxD6Motion::eLIMITED : PxD6Motion::eLOCKED);
				

					const float LeverLimitRad = LeverLimit * /*InTwistLimitScale **/ (PI / 180.0f);
					//PxReal LimitContactDistance = FMath::DegreesToRadians(FMath::Max(1.f, ProfileInstance.ConeLimit.ContactDistance /** InTwistLimitScale*/));

					//The limit values need to be clamped so it will be valid in PhysX
					PxReal ZLimitAngle = FMath::ClampAngle(LeverLimit /** InSwing1LimitScale*/, KINDA_SMALL_NUMBER, 179.9999f) * (PI / 180.0f);
					PxReal YLimitAngle = FMath::ClampAngle(LeverLimit /** InSwing2LimitScale*/, KINDA_SMALL_NUMBER, 179.9999f) * (PI / 180.0f);
					//PxReal LimitContactDistance = FMath::DegreesToRadians(FMath::Max(1.f, ProfileInstance.ConeLimit.ContactDistance * FMath::Min(InSwing1LimitScale, InSwing2LimitScale)));

					NewJoint->setSwingLimit(PxJointLimitCone(YLimitAngle, ZLimitAngle));
					NewJoint->setTwistLimit(PxJointAngularLimitPair(-LeverLimitRad, LeverLimitRad));

					return true;
				}
			}

			return false;
		});
#else
		return false;
#endif // WITH_PHYSX

		return false;
	}


	// Grip interface setup

	// Set up as deny instead of allow so that default allows for gripping
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool DenyGripping();

	// How an interfaced object behaves when teleporting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripInterfaceTeleportBehavior TeleportBehavior();

	// Should this object simulate on drop
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool SimulateOnDrop();

	// Grip type to use when gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType SlotGripType();

	// Grip type to use when not gripping a slot
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripCollisionType FreeGripType();

	// Secondary grip type
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		ESecondaryGripType SecondaryGripType();

	// Define which movement repliation setting to use
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripMovementReplicationSettings GripMovementReplicationType();

	// Define the late update setting
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		EGripLateUpdateSettings GripLateUpdateSetting();

	// What grip stiffness to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripStiffness();

	// What grip damping to use if using a physics constraint
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripDamping();

	// Get the advanced physics settings for this grip
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		FBPAdvGripPhysicsSettings AdvancedPhysicsSettings();

	// What distance to break a grip at (only relevent with physics enabled grips
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		float GripBreakDistance();

	// Get closest secondary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestSecondarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName OverridePrefix);

	// Get closest primary slot in range
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void ClosestPrimarySlotInRange(FVector WorldLocation, bool & bHadSlotInRange, FTransform & SlotWorldTransform, FName OverridePrefix);

	// Check if the object is an interactable
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		bool IsInteractible();

	// Returns if the object is held and if so, which pawn is holding it
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void IsHeld(UGripMotionControllerComponent *& CurHoldingController, bool & bCurIsHeld);

	// Sets is held, used by the plugin
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void SetHeld(UGripMotionControllerComponent * NewHoldingController, bool bNewIsHeld);

	// Get interactable settings
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		FBPInteractionSettings GetInteractionSettings();


	// Events //

	// Event triggered each tick on the interfaced object when gripped, can be used for custom movement or grip based logic
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void TickGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation, FVector MControllerLocDelta, float DeltaTime);

	// Event triggered on the interfaced object when gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGrip(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when child component is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnChildGripRelease(UGripMotionControllerComponent * ReleasingController, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary gripped
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnSecondaryGrip(USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Event triggered on the interfaced object when secondary grip is released
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripInterface")
		void OnSecondaryGripRelease(USceneComponent * ReleasingSecondaryGripComponent, const FBPActorGripInformation & GripInformation);

	// Interaction Functions

	// Call to use an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnUsed();

	// Call to stop using an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnEndUsed();

	// Call to use an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnSecondaryUsed();

	// Call to stop using an object
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripInterface")
		void OnEndSecondaryUsed();

};