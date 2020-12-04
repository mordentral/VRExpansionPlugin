#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRGripScriptBase.h"
#include "GameFramework/WorldSettings.h"
#include "GripScripts/GS_Default.h"
#include "GS_Physics.generated.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#endif // WITH_PHYSX


/**
* A pure physics multi hand interaction grip script, expects that bAllowMultiGrips is set on the parent object*
*/
UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin), hideCategories = TickSettings)
class VREXPANSIONPLUGIN_API UGS_Physics : public UGS_Default
{
	GENERATED_BODY()
public:

	UGS_Physics(const FObjectInitializer& ObjectInitializer);

	// If true then we won't bind to the objects mass updates, we don't expect thing to attach to us
	// This is a perf savings when possible
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
		bool bSkipGripMassChecks;

	// Grip settings to use when a single hand is gripping, overrides interface defaults
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
		FBPAdvancedPhysicsHandleSettings SingleHandPhysicsSettings;

	// Grip settings to use when multiple hands are gripping
	// Overrides interface defaults
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics Settings")
		FBPAdvancedPhysicsHandleSettings MultiHandPhysicsSettings;


	void UpdateDualHandInfo(UGripMotionControllerComponent* GrippingController = nullptr, bool bRecreate = true);

	virtual void HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation* HandleInfo) override;
	//virtual void HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation* HandleInfo, FTransform& KinPose) override;

	virtual void OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) override;
	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;
};
