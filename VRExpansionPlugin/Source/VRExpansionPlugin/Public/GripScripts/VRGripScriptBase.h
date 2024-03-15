// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
//#include "Engine/Engine.h"
#include "UObject/Object.h"
#include "VRBPDatatypes.h"
#include "Tickable.h"

#include "VRGripScriptBase.generated.h"

class UGripMotionControllerComponent;
class UVRGripInterface;
class UPrimitiveComponent;
class AActor;


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

UCLASS(NotBlueprintable, BlueprintType, EditInlineNew, DefaultToInstanced, Abstract, ClassGroup = (VRExpansionPlugin), HideCategories = DefaultSettings)
class VREXPANSIONPLUGIN_API UVRGripScriptBase : public UObject, public FTickableGameObject
{
	GENERATED_BODY()
public:

	UVRGripScriptBase(const FObjectInitializer& ObjectInitializer);

	// Gets the first grip script of the specified type in this object, do NOT call this on tick, save out and store the reference given
	UFUNCTION(BlueprintCallable, Category = "VRGripScript|Functions", meta = (WorldContext = "WorldContextObject", bIgnoreSelf = "true", DisplayName = "GetGripScriptByClass", ExpandEnumAsExecs = "Result"))
		static UVRGripScriptBase* GetGripScriptByClass(UObject* WorldContextObject, TSubclassOf<UVRGripScriptBase> GripScriptClass, EBPVRResultSwitch& Result);

	bool IsSupportedForNetworking() const override
	{
		return true;
		//return bRequiresReplicationSupport || Super::IsSupportedForNetworking();
	}
	// I don't need to do this, there should be no dynamic script spawning and they are all name stable by default
	
	// Returns if the script is currently active and should be used
	bool IsScriptActive();

	// Is currently active helper variable, returned from IsScriptActive()
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "GSSettings")
	bool bIsActive;

private:
	// If we should replicate, if false we will never be added to our parents list.
	UPROPERTY(Replicated, EditDefaultsOnly, BlueprintReadOnly, Category = "GSSettings|Replication", meta = (AllowPrivateAccess = "true"))
	bool bReplicates = false;
public:

	/** Enable or disable replication. This is the equivalent of RemoteRole for actors (only a bool is required for components) */
	UFUNCTION(BlueprintCallable, Category = "Components")
		void SetIsReplicated(bool ShouldReplicate);

	/** Returns whether replication is enabled or not. */
	UFUNCTION(BlueprintCallable, Category = "Components")
	bool GetIsReplicated() const
	{
		return bReplicates;
	}

	// Replication Condition (If using subobject replication list)
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "GSSettings|Replication")
	TEnumAsByte<ELifetimeCondition> ReplicationCondition = ELifetimeCondition::COND_None;

	// Returns if the script is going to modify the world transform of the grip
	EGSTransformOverrideType GetWorldTransformOverrideType();

	// Whether this script overrides or modifies the world transform
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "GSSettings")
	EGSTransformOverrideType WorldTransformOverrideType;

	// Returns if the script wants auto drop to be ignored
	FORCEINLINE bool Wants_DenyAutoDrop()
	{
		return bDenyAutoDrop;
	}

	// Returns if we want to deny auto dropping
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "GSSettings")
		bool bDenyAutoDrop;

	// Returns if the script wants to force a drop
	FORCEINLINE bool Wants_ToForceDrop()
	{
		return bForceDrop;
	}

	// Returns if we want to force a drop
	UPROPERTY(BlueprintReadWrite, Category = "GSSettings")
		bool bForceDrop;

	// Flags the grip to be dropped as soon as possible
	UFUNCTION(BlueprintCallable, Category = "VRGripScript")
	void ForceGripToDrop()
	{
		bForceDrop = true;
	}

	// Returns if the script wants to deny late updates
	FORCEINLINE bool Wants_DenyLateUpdates()
	{
		return bDenyLateUpdates;
	}

	// Returns if we want to inject changes prior to the physics handle
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "GSSettings")
		bool bDenyLateUpdates;

	// Returns if the script wants auto drop to be ignored
	FORCEINLINE bool InjectPrePhysicsHandle()
	{
		return bInjectPrePhysicsHandle;
	}

	// Returns if we want to inject changes prior to the physics handle
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "GSSettings")
		bool bInjectPrePhysicsHandle;

	virtual void HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation * HandleInfo, FTransform & KinPose);

	// Returns if we want to inject changes after the physics handle
	FORCEINLINE bool InjectPostPhysicsHandle()
	{
		return bInjectPostPhysicsHandle;
	}

	// Returns if we want to inject changes after the physics handle
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "GSSettings")
		bool bInjectPostPhysicsHandle;

	virtual void HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation * HandleInfo);

	// Returns if the script is currently active and should be used
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VRGripScript")
	bool Wants_DenyTeleport(UGripMotionControllerComponent * Controller);
	virtual bool Wants_DenyTeleport_Implementation(UGripMotionControllerComponent* Controller);

#if UE_WITH_IRIS
	/** Register all replication fragments */
	virtual void RegisterReplicationFragments(UE::Net::FFragmentRegistrationContext& Context, UE::Net::EFragmentRegistrationFlags RegistrationFlags) override;
#endif // UE_WITH_IRIS

	virtual void GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const override;

	// doesn't currently compile in editor builds, not sure why the linker is screwing up there but works elsewhere
	//virtual void PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker);
	virtual bool CallRemoteFunction(UFunction * Function, void * Parms, FOutParmRec * OutParms, FFrame * Stack) override;
	virtual int32 GetFunctionCallspace(UFunction * Function, FFrame * Stack) override;

	// FTickableGameObject functions
	
	
	// If true then this scrip can tick when bAllowticking is true
	UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "TickSettings")
		bool bCanEverTick;

	// If true and we bCanEverTick, then will fire off the tick function
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "TickSettings")
		bool bAllowTicking;

	// Set whether the grip script can tick or not
	UFUNCTION(BlueprintCallable, Category = "TickSettings")
		void SetTickEnabled(bool bTickEnabled);

	/**
	 * Function called every frame on this GripScript. Override this function to implement custom logic to be executed every frame.
	 * Only executes if bCanEverTick is true and bAllowTicking is true
	 *
	 * @param DeltaTime - The time since the last tick.
	 */
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual bool IsTickableInEditor() const;
	virtual bool IsTickableWhenPaused() const override;
	virtual ETickableTickType GetTickableTickType() const;
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetWorld() const override;

	// End tickable object information


	// Returns the expected grip transform (relative * controller + addition)
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		FTransform GetGripTransform(const FBPActorGripInformation &Grip, const FTransform & ParentTransform);

	// Returns the current world transform of the owning object (or root comp of if it is an actor)
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		FTransform GetParentTransform(bool bGetWorldTransform = true, FName BoneName = NAME_None);

	// Returns the scene component of the parent, either being the parent itself or the root comp of it.
	// Nullptr if there is no valid scene component
	UFUNCTION(BlueprintCallable, Category = "VRGripScript")
		USceneComponent* GetParentSceneComp();

	// Returns the root body instance of the parent
	FBodyInstance * GetParentBodyInstance(FName OptionalBoneName = NAME_None);

	// Returns the parent component or actor to this
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		UObject * GetParent();

	// Returns the owning actor 
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		AActor * GetOwner();

	// If the owning actor has authority on this connection 
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		bool HasAuthority();

	// If the owning actor is on the server on this connection 
	UFUNCTION(BlueprintPure, Category = "VRGripScript")
		bool IsServer();

	virtual void BeginDestroy();
	void EndPlay(const EEndPlayReason::Type EndPlayReason);

	// Not all scripts will require this function, specific ones that use things like Lever logic however will. Best to call it.
	// Grippables will automatically call this, however if you manually spawn a grip script during play or you make your own
	// Interfaced grip object and give it grippables, YOU will be required to call this event on them.
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		void OnEndPlay(const EEndPlayReason::Type EndPlayReason);
	virtual void OnEndPlay_Implementation(const EEndPlayReason::Type EndPlayReason);

	void BeginPlay(UObject * CallingOwner);
	bool bAlreadyNotifiedPlay = false;
	virtual void PostInitProperties() override;

	// Not all scripts will require this function, specific ones that use things like Lever logic however will. Best to call it.
	// Grippables will automatically call this, however if you manually spawn a grip script during play or you make your own
	// Interfaced grip object and give it grippables, YOU will be required to call this event on them.
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		void OnBeginPlay(UObject * CallingOwner);
		virtual void OnBeginPlay_Implementation(UObject * CallingOwner);

	// Overrides or Modifies the world transform with this grip script
	UFUNCTION(BlueprintNativeEvent, Category = "VRGripScript")
		bool GetWorldTransform(UGripMotionControllerComponent * GrippingController, float DeltaTime, UPARAM(ref) FTransform & WorldTransform, const FTransform &ParentTransform, UPARAM(ref) FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport);
		virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport);

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



	virtual bool CallCorrect_GetWorldTransform(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport)
	{
		return GetWorldTransform_Implementation(OwningController, DeltaTime, WorldTransform, ParentTransform, Grip, actor, root, bRootHasInterface, bActorHasInterface, bIsForTeleport);
	}
};


UCLASS(Blueprintable, Abstract, ClassGroup = (VRExpansionPlugin), ShowCategories = DefaultSettings)
class VREXPANSIONPLUGIN_API UVRGripScriptBaseBP : public UVRGripScriptBase
{
	GENERATED_BODY()
public:

	virtual bool CallCorrect_GetWorldTransform(UGripMotionControllerComponent * OwningController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) override
	{
		return GetWorldTransform(OwningController, DeltaTime, WorldTransform, ParentTransform, Grip, actor, root, bRootHasInterface, bActorHasInterface, bIsForTeleport);
	}

	virtual void Tick(float DeltaTime) override;

	/** Event called every frame if ticking is enabled */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Tick"))
		void ReceiveTick(float DeltaSeconds);
};