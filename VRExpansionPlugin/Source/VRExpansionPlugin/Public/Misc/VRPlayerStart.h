#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerStart.h"
#include "Components/SceneComponent.h"
#include "VRPlayerStart.generated.h"

/**
*	A normal player start except I replaced the root component with a scene component so that the spawn
*	transform will match our VR characters.
*/
UCLASS(Blueprintable, ClassGroup = Common, hidecategories = Collision)
class VREXPANSIONPLUGIN_API AVRPlayerStart : public APlayerStart
{
	GENERATED_BODY()
private:
	UPROPERTY()
    TObjectPtr<USceneComponent> VRRootComp;
public:

	AVRPlayerStart(const FObjectInitializer& ObjectInitializer);

	/** Returns VRRootComp subobject **/
	class USceneComponent* GetVRRootComponent() const { return VRRootComp; }

	// Override this to use capsule even if it isn't the root component
	virtual void GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const override;
	virtual void FindBase() override;
	virtual void Validate() override;
};