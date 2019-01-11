#pragma once

#include "CoreMinimal.h"
#include "VRGripScriptBase.h"
#include "GameFramework/WorldSettings.h"
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

	inline void Default_GetAnyScaling(FVector & Scaler, FBPActorGripInformation & Grip, FVector & frontLoc, FVector & frontLocOrig, ESecondaryGripType SecondaryType, FTransform & SecondaryTransform)
	{
		if (Grip.SecondaryGripInfo.GripLerpState != EGripLerpState::EndLerp)
		{

			//float Scaler = 1.0f;
			if (SecondaryType == ESecondaryGripType::SG_FreeWithScaling_Retain || SecondaryType == ESecondaryGripType::SG_SlotOnlyWithScaling_Retain || SecondaryType == ESecondaryGripType::SG_ScalingOnly)
			{
				/*Grip.SecondaryScaler*/ Scaler = FVector(frontLoc.Size() / frontLocOrig.Size());
				//bRescalePhysicsGrips = true; // This is for the physics grips

				if (Grip.AdvancedGripSettings.SecondaryGripSettings.bUseSecondaryGripSettings && Grip.AdvancedGripSettings.SecondaryGripSettings.bLimitGripScaling)
				{
					// Get the total scale after modification
					// #TODO: convert back to singular float version? Can get Min() & Max() to convert the float to a range...think about it
					FVector WorldScale = /*WorldTransform*/SecondaryTransform.GetScale3D();
					FVector CombinedScale = WorldScale * Scaler;

					// Clamp to the minimum and maximum values
					CombinedScale.X = FMath::Clamp(CombinedScale.X, Grip.AdvancedGripSettings.SecondaryGripSettings.MinimumGripScaling.X, Grip.AdvancedGripSettings.SecondaryGripSettings.MaximumGripScaling.X);
					CombinedScale.Y = FMath::Clamp(CombinedScale.Y, Grip.AdvancedGripSettings.SecondaryGripSettings.MinimumGripScaling.Y, Grip.AdvancedGripSettings.SecondaryGripSettings.MaximumGripScaling.Y);
					CombinedScale.Z = FMath::Clamp(CombinedScale.Z, Grip.AdvancedGripSettings.SecondaryGripSettings.MinimumGripScaling.Z, Grip.AdvancedGripSettings.SecondaryGripSettings.MaximumGripScaling.Z);

					// Recreate in scaler form so that the transform chain below works as normal
					Scaler = CombinedScale / WorldScale;
				}
				//Scaler = Grip.SecondaryScaler;
			}
		}
	}
	
	inline void Default_ApplySmoothingAndLerp(FBPActorGripInformation & Grip, FVector &frontLoc, FVector & frontLocOrig, float DeltaTime)
	{
		if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::StartLerp) // Lerp into the new grip to smooth the transition
		{
			/*if (Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler_DEPRECATED < 1.0f)
			{
				FVector SmoothedValue = Grip.AdvancedGripSettings.SecondaryGripSettings.SecondarySmoothing.RunFilterSmoothing(frontLoc, DeltaTime);

				frontLoc = FMath::Lerp(SmoothedValue, frontLoc, Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler_DEPRECATED);
			}*/

			frontLocOrig = FMath::Lerp(frontLocOrig, frontLoc, FMath::Clamp(Grip.SecondaryGripInfo.curLerp / Grip.SecondaryGripInfo.LerpToRate, 0.0f, 1.0f));
		}
		/*else if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::ConstantLerp_DEPRECATED) // If there is a frame by frame lerp
		{
			FVector SmoothedValue = Grip.AdvancedGripSettings.SecondaryGripSettings.SecondarySmoothing.RunFilterSmoothing(frontLoc, DeltaTime);

			frontLoc = FMath::Lerp(SmoothedValue, frontLoc, Grip.AdvancedGripSettings.SecondaryGripSettings.SecondaryGripScaler_DEPRECATED);
		}*/
	}
};
