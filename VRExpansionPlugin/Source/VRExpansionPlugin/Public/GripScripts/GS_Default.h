#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "GS_Default.generated.h"


/**
* The default grip transform logic for the motion controllers
*/
UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UGS_Default : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	UGS_Default(const FObjectInitializer& ObjectInitializer);

	//virtual void BeginPlay_Implementation() override;
	virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) override;

	virtual void GetAnyScaling(FVector& Scaler, FBPActorGripInformation& Grip, FVector& frontLoc, FVector& frontLocOrig, ESecondaryGripType SecondaryType, FTransform& SecondaryTransform);	
	virtual void ApplySmoothingAndLerp(FBPActorGripInformation& Grip, FVector& frontLoc, FVector& frontLocOrig, float DeltaTime);

	virtual void CalculateSecondaryLocation(FVector & frontLoc, const FVector& BasePoint, FBPActorGripInformation& Grip, UGripMotionControllerComponent * GrippingController);
};

// An extended default grip script that adds less common grip features that were moved out of the default implementation
UCLASS(BlueprintType, ClassGroup = (VRExpansionPlugin), hideCategories = TickSettings)
class VREXPANSIONPLUGIN_API UGS_ExtendedDefault : public UGS_Default
{
	GENERATED_BODY()
public:

	// Whether clamp the grip scaling in scaling grips
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings")
		bool bLimitGripScaling;

	// Minimum size to allow scaling in double grip to reach
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bLimitGripScaling"))
		FVector_NetQuantize100 MinimumGripScaling;

	// Maximum size to allow scaling in double grip to reach
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bLimitGripScaling"))
		FVector_NetQuantize100 MaximumGripScaling;

	virtual void GetAnyScaling(FVector& Scaler, FBPActorGripInformation& Grip, FVector& frontLoc, FVector& frontLocOrig, ESecondaryGripType SecondaryType, FTransform& SecondaryTransform) override;
};