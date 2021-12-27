// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GripMotionControllerComponent.h"
#include "Engine/Engine.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/ActorChannel.h"
#include "OptionalRepSkeletalMeshActor.generated.h"

// Temp comp to avoid some engine issues, exists only until a bug fix happens
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent, ChildCanTick), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UNoRepSphereComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UNoRepSphereComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Component Replication")
		bool bReplicateMovement;

	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
};

/**
* A component specifically for being able to turn off movement replication in the component at will
* Has the upside of also being a blueprintable base since UE4 doesn't allow that with std ones
*/

USTRUCT()
struct FSkeletalMeshComponentEndPhysicsTickFunctionVR : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

		UInversePhysicsSkeletalMeshComponent* TargetVR;

	virtual void ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;

	virtual FString DiagnosticMessage() override;

	virtual FName DiagnosticContext(bool bDetailed) override;

};
template<>
struct TStructOpsTypeTraits<FSkeletalMeshComponentEndPhysicsTickFunctionVR> : public TStructOpsTypeTraitsBase2<FSkeletalMeshComponentEndPhysicsTickFunctionVR>
{
	enum
	{
		WithCopy = false
	};
};

// A base skeletal mesh component that has been added to temp correct an engine bug with inversed scale and physics
UCLASS(Blueprintable, meta = (ChildCanTick, BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UInversePhysicsSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UInversePhysicsSkeletalMeshComponent(const FObjectInitializer& ObjectInitializer);

public:

	// Overrides the default of : true and allows for controlling it like in an actor, should be default of off normally with grippable components
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Component Replication")
		bool bReplicateMovement;

	// This is all overrides to fix the skeletal mesh inverse simulation bug
	// WILL BE REMOVED LATER when the engine is fixed
	FSkeletalMeshComponentEndPhysicsTickFunctionVR EndPhysicsTickFunctionVR;
	friend struct FSkeletalMeshComponentEndPhysicsTickFunctionVR;
	void EndPhysicsTickComponentVR(FSkeletalMeshComponentEndPhysicsTickFunctionVR& ThisTickFunction);
	void BlendInPhysicsInternalVR(FTickFunction& ThisTickFunction);
	void FinalizeAnimationUpdateVR();

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override
	{
		// Get rid of inverse issues
		FTransform newLocalToWorld = LocalToWorld;
		newLocalToWorld.SetScale3D(newLocalToWorld.GetScale3D().GetAbs());

		return Super::CalcBounds(newLocalToWorld);
	}

	UFUNCTION(BlueprintPure, Category = "VRExpansionFunctions")
	FBoxSphereBounds GetLocalBounds() const
	{
		return this->GetCachedLocalBounds();
	}

	void PerformBlendPhysicsBonesVR(const TArray<FBoneIndexType>& InRequiredBones, TArray<FTransform>& InBoneSpaceTransforms);
	virtual void RegisterEndPhysicsTick(bool bRegister) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	// END INVERSED MESH FIX

	virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker) override;

};

/**
*
*  A class specifically for turning off default physics replication with a skeletal mesh and fixing an
*  engine bug with inversed scale on skeletal meshes. Generally used for the physical hand setup.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent, ChildCanTick), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API AOptionalRepGrippableSkeletalMeshActor : public ASkeletalMeshActor
{
	GENERATED_BODY()

public:
	AOptionalRepGrippableSkeletalMeshActor(const FObjectInitializer& ObjectInitializer);

	// Skips the attachment replication
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Replication")
		bool bIgnoreAttachmentReplication;

	// Skips the physics replication
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Replication")
		bool bIgnorePhysicsReplication;

	// Fix bugs with replication and bReplicateMovement
	virtual void OnRep_ReplicateMovement() override;
	virtual void PostNetReceivePhysicState() override;
};