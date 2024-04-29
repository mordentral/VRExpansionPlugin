#pragma once

#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "CoreMinimal.h"
//#include "UObject/ObjectMacros.h"
#include "Components/ActorComponent.h"
#include "EngineDefines.h"
#if UE_ENABLE_DEBUG_DRAWING
//#include "Chaos/DebugDrawQueue.h"
#endif
#include "VREPhysicalAnimationComponent.generated.h"

struct FReferenceSkeleton;

USTRUCT()
struct VREXPANSIONPLUGIN_API FWeldedBoneDriverData
{
	GENERATED_BODY()
public:
	FTransform RelativeTransform;
	FName BoneName;
	//FPhysicsShapeHandle ShapeHandle;

	FTransform LastLocal;

	FWeldedBoneDriverData() :
		RelativeTransform(FTransform::Identity),
		BoneName(NAME_None)
	{
	}

	/*FORCEINLINE bool operator==(const FPhysicsShapeHandle& Other) const
	{
		return (ShapeHandle == Other);
	}*/

	FORCEINLINE bool operator==(const FName& Other) const
	{
		return (BoneName == Other);
	}
};

UCLASS(meta = (BlueprintSpawnableComponent), ClassGroup = Physics)
class VREXPANSIONPLUGIN_API UVREPhysicalAnimationComponent : public UPhysicalAnimationComponent
{
	GENERATED_UCLASS_BODY()

public:

	/** Is the welded bone driver paused */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = WeldedBoneDriver)
		bool bIsPaused;

	/** IF true then we will auto adjust the sleep settings of the body so that it won't rest during welded bone driving */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = WeldedBoneDriver)
		bool bAutoSetPhysicsSleepSensitivity;

	/** The threshold multiplier to set on the body */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = WeldedBoneDriver)
		float SleepThresholdMultiplier;

	/** The Base bone to use as the bone driver root */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = WeldedBoneDriver)
		TArray<FName> BaseWeldedBoneDriverNames;

	UPROPERTY()
		TArray<FWeldedBoneDriverData> BoneDriverMap;

	// Call to setup the welded body driver, initializes all mappings and caches shape contexts
	// Requires that SetSkeletalMesh be called first
	UFUNCTION(BlueprintCallable, Category = PhysicalAnimation)
	void SetupWeldedBoneDriver(TArray<FName> BaseBoneNames);

	// Refreshes the welded bone driver, use this in cases where the body may have changed (such as welding to another body or switching physics)
	UFUNCTION(BlueprintCallable, Category = PhysicalAnimation)
		void RefreshWeldedBoneDriver();
	
	// Sets the welded bone driver to be paused, you can also stop the component from ticking but that will also stop any physical animations going on
	UFUNCTION(BlueprintCallable, Category = PhysicalAnimation)
		void SetWeldedBoneDriverPaused(bool bPaused);

	UFUNCTION(BlueprintPure, Category = PhysicalAnimation)
		bool IsWeldedBoneDriverPaused();

	void SetupWeldedBoneDriver_Implementation(bool bReInit = false);

	//void OnWeldedMassUpdated(FBodyInstance* BodyInstance);
	void UpdateWeldedBoneDriver(float DeltaTime);


	/** If true then we will debug draw the mesh collision post shape alterations */
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = WeldedBoneDriver)
		//bool bDebugDrawCollision = false;

#if UE_ENABLE_DEBUG_DRAWING
	//void DebugDrawMesh(const TArray<Chaos::FLatentDrawCommand>& DrawCommands);
#endif

	FTransform GetWorldSpaceRefBoneTransform(FReferenceSkeleton& RefSkel, int32 BoneIndex, int32 ParentBoneIndex);
	FTransform GetRefPoseBoneRelativeTransform(USkeletalMeshComponent* SkeleMesh, FName BoneName, FName ParentBoneName);

	//FCalculateCustomPhysics OnCalculateCustomPhysics;
	//void CustomPhysics(float DeltaTime, FBodyInstance* BodyInstance);

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
};
