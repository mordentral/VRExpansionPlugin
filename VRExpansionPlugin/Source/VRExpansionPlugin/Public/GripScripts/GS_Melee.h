#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRGripScriptBase.h"
#include "GameFramework/WorldSettings.h"
#include "GripScripts/GS_Default.h"
#include "GS_Melee.generated.h"

#if PHYSICS_INTERFACE_PHYSX
#include "PhysXPublic.h"
#endif // WITH_PHYSX



// The type of melee hit zone we are
UENUM(BlueprintType)
enum class EVRMeleeZoneType : uint8
{
	// This zone is only valid for stabs
	VRPMELLE_ZONETYPE_Stab UMETA(DisplayName = "Stab"),

	// This zone is only valid for hits
	VRPMELLE_ZONETYPE_Hit UMETA(DisplayName = "Hit"),

	// This zone is valid for both stabs and hits
	VRPMELLE_ZONETYPE_StabAndHit UMETA(DisplayName = "StabAndHit")

};

// The type of COM selection to use
UENUM(BlueprintType)
enum class EVRMeleeComType : uint8
{
	// Does not set COM
	VRPMELEECOM_Normal UMETA(DisplayName = "Normal"),

	// Sets COM to between hands
	VRPMELEECOM_BetweenHands UMETA(DisplayName = "BetweenHands"),

	// Uses the primary hand as com location
	VRPMELEECOM_PrimaryHand  UMETA(DisplayName = "PrimaryHand")
};

// The type of primary hand selection to use
UENUM(BlueprintType)
enum class EVRMeleePrimaryHandType : uint8
{
	// Uses the rearmost hand as the primary hand
	VRPHAND_Rear UMETA(DisplayName = "Rear"),

	// Uses the foremost hand as the primary hand
	VRPHAND_Front  UMETA(DisplayName = "Front"),

	// Uses the first slotted hand as the primary hand
	// If neither are slotted then its first come first serve and both hannds load the secondary settings
	VRPHAND_Slotted UMETA(DisplayName = "Slotted")
};

// A Lodge component data struct
USTRUCT(BlueprintType, Category = "Lodging")
struct VREXPANSIONPLUGIN_API FBPHitSurfaceProperties
{
	GENERATED_BODY()
public:

	// Does this surface type allow penetration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Property")
		bool bSurfaceAllowsPenetration;

	// Scaler to damage applied from hitting this surface with blunt damage
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Property")
		float BluntDamageScaler;

	// Scaler to damage applied from hitting this surface with sharp damage
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Property")
		float SharpDamageScaler;

	// Alters the stab velocity to let you make it harder or easier to stab this surface
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Property")
		float StabVelocityScaler;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Property")
	TEnumAsByte<EPhysicalSurface> SurfaceType;

	FBPHitSurfaceProperties()
	{
		// Default to true on this
		bSurfaceAllowsPenetration = true;
		BluntDamageScaler = 1.f;
		SharpDamageScaler = 1.f;
		StabVelocityScaler = 1.f;
		SurfaceType = EPhysicalSurface::SurfaceType_Default;
	}
};

// A Lodge component data struct
USTRUCT(BlueprintType, Category = "Lodging")
struct VREXPANSIONPLUGIN_API FBPLodgeComponentInfo
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LodgeComponentInfo")
		FName ComponentName;

	// Type of collision zone we are
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LodgeComponentInfo")
		EVRMeleeZoneType ZoneType;

	// If true than we will calculate hit impulse off of its total value and not just off of it axially aligned to the forward of this body
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LodgeComponentInfo")
		bool bIgnoreForwardVectorForHitImpulse;

	// For end users to provide a base damage per zone if they want
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LodgeComponentInfo")
		float DamageScaler;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LodgeComponentInfo")
		float PenetrationDepth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LodgeComponentInfo")
		bool bAllowPenetrationInReverseAsWell;

	// This is the impulse velocity (along forward axis of component) required to throw an OnPenetrated event from a PenetrationNotifierComponent
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		float PenetrationVelocity;

	// This is the impulse velocity required to throw an OnHit event from a PenetrationNotifierComponent (If a stab didn't take place)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		float MinimumHitVelocity;

	// The acceptable range of the dot product of the forward vector and the impact normal to define a valid facing
	// Subtracted from the 1.0f forward facing value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		float AcceptableForwardProductRange;

	// The acceptable range of the dot product of the forward vector and the impact normal to define a valid facing
	// Subtracted from the 1.0f forward facing value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		float AcceptableForwardProductRangeForHits;

	FBPLodgeComponentInfo()
	{
		ComponentName = NAME_None;
		ZoneType = EVRMeleeZoneType::VRPMELLE_ZONETYPE_StabAndHit;
		bIgnoreForwardVectorForHitImpulse = false;
		DamageScaler = 0.f;
		PenetrationDepth = 100.f;
		bAllowPenetrationInReverseAsWell = false;
		PenetrationVelocity = 8000.f;
		MinimumHitVelocity = 1000.f;
		AcceptableForwardProductRange = 0.1f;
		AcceptableForwardProductRangeForHits = 0.1f;
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LodgeComponentInfo")
	TWeakObjectPtr<UPrimitiveComponent> TargetComponent;

	FORCEINLINE bool operator==(const FName& Other) const
	{
		return (ComponentName == Other);
	}

};


// Event thrown when we the melee weapon becomes lodged
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams(FVROnMeleeShouldLodgeSignature, FBPLodgeComponentInfo, LogComponent, AActor *, OtherActor, UPrimitiveComponent *, OtherComp, ECollisionChannel, OtherCompCollisionChannel, FBPHitSurfaceProperties, HitSurfaceProperties, FVector, NormalImpulse, const FHitResult&, Hit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SevenParams(FVROnMeleeOnHit, FBPLodgeComponentInfo, LogComponent, AActor*, OtherActor, UPrimitiveComponent*, OtherComp, ECollisionChannel, OtherCompCollisionChannel, FBPHitSurfaceProperties, HitSurfaceProperties, FVector, NormalImpulse, const FHitResult&, Hit);

/**
* A Melee grip script that hands multi hand interactions and penetration notifications*
* The per surface damage and penetration options have been moved to the project settings unless the per script override is set
*/
UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin), hideCategories = TickSettings)
class VREXPANSIONPLUGIN_API UGS_Melee : public UGS_Default
{
	GENERATED_BODY()
public:

	UGS_Melee(const FObjectInitializer& ObjectInitializer);

	UFUNCTION()
	void OnLodgeHitCallback(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION(BlueprintCallable, Category = "Weapon Settings")
		void SetIsLodged(bool IsLodged, UPrimitiveComponent * LodgeComponent)
	{
		bIsLodged = IsLodged;
		LodgedComponent = LodgeComponent;
	}

	bool bIsLodged;
	TWeakObjectPtr<UPrimitiveComponent> LodgedComponent;

	//virtual void Tick(float DeltaTime) override;

	// Thrown if we should lodge into a hit object
	UPROPERTY(BlueprintAssignable, Category = "Melee|Lodging")
		FVROnMeleeShouldLodgeSignature OnShouldLodgeInObject;

	UPROPERTY(BlueprintAssignable, Category = "Melee|Hit")
		FVROnMeleeOnHit OnMeleeHit;

	// Always tick for penetration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lodging")
		bool bAlwaysTickPenetration;

	// Only penetrate with two hands on the weapon
	// Mostly for very large weapons
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lodging")
		bool bOnlyPenetrateWithTwoHands;

	// A list of surface types that allow penetration and their properties
	// If empty then the script will use the global settings, if filled with anything then it will override the global settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee|Lodging")
		TArray<FBPHitSurfaceProperties> OverrideMeleeSurfaceSettings;

//	FVector RollingVelocityAverage;
	//FVector RollingAngVelocityAverage;

	// The name of the component that is used to orient the weapon along its primary axis
	// If it does not exist then the weapon is assumed to be X+ facing.
	// Also used to perform some calculations, make sure it is parented to the gripped object (root component for actors).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		FName WeaponRootOrientationComponent;
	FTransform OrientationComponentRelativeFacing;

	// UpdateHand location on the shaft in the X axis
	// If primary hand is false then it will do the secondary hand
	// World location is of the pivot generally, I have it passing in so people can snap
	// LocDifference returns the relative distance of the change in position (or zero if there was none).
	UFUNCTION(BlueprintCallable, Category = "Weapon Settings")
		void UpdateHandPosition(FBPGripPair HandPair, FVector HandWorldPosition, FVector & LocDifference);

	// UpdateHand location and rotation on the shaft in the X axis
	// If primary hand is false then it will do the secondary hand
	// World location is of the pivot generally, I have it passing in so people can snap
	// LocDifference returns the relative distance of the change in position (or zero if there was none).
	UFUNCTION(BlueprintCallable, Category = "Weapon Settings")
	void UpdateHandPositionAndRotation(FBPGripPair HandPair, FTransform HandWorldTransform, FVector& LocDifference, float& RotDifference, bool bUpdateLocation = true, bool bUpdateRotation = true);


	// This is a built list of components that act as penetration notifiers, they will have their OnHit bound too and we will handle penetration logic
	// off of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		TArray<FBPLodgeComponentInfo> PenetrationNotifierComponents;

	bool bCheckLodge;
	bool bIsHeld;

	FVector LastRelativePos;
	FVector RelativeBetweenGripsCenterPos;

	// If true then we won't bind to the objects mass updates, we don't expect thing to attach to us
	// This is a perf savings when possible
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		bool bSkipGripMassChecks;

	// When true, will auto set the primary and secondary hands by the WeaponRootOrientationComponents X Axis distance.
	// Smallest value along the X Axis will be considered the primary hand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		bool bAutoSetPrimaryAndSecondaryHands;

	// If we couldn't decide on a true valid primary hand then this will be false and we will load secondary settings for both
	bool bHasValidPrimaryHand;

	UFUNCTION(BlueprintCallable, Category = "Weapon Settings")
		void SetPrimaryAndSecondaryHands(FBPGripPair & PrimaryGrip, FBPGripPair & SecondaryGrip);

	// Which method of primary hand to select
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		EVRMeleePrimaryHandType PrimaryHandSelectionType;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Settings")
	FBPGripPair PrimaryHand;

	UPROPERTY(BlueprintReadOnly, Category = "Weapon Settings")
	FBPGripPair SecondaryHand;

	// To select the type of com setting to use
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		EVRMeleeComType COMType;

	FTransform ObjectRelativeGripCenter;

	void SetComBetweenHands(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation * HandleInfo);


	// Grip settings to use on the primary hand when multiple grips are active
	// Falls back to the standard grip settings when only one grip is active
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		FBPAdvancedPhysicsHandleSettings PrimaryHandPhysicsSettings;

	// Grip settings to use on the secondary hand when multiple grips are active
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		FBPAdvancedPhysicsHandleSettings SecondaryHandPhysicsSettings;


	void UpdateDualHandInfo();

	virtual void HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation* HandleInfo) override;
	virtual void HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation* HandleInfo, FTransform& KinPose) override;
	virtual void OnBeginPlay_Implementation(UObject* CallingOwner) override;
	virtual void OnEndPlay_Implementation(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnSecondaryGrip_Implementation(UGripMotionControllerComponent* Controller, USceneComponent* SecondaryGripComponent, const FBPActorGripInformation& GripInformation) override;

	virtual void OnGrip_Implementation(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation& GripInformation) override;

	virtual void OnGripRelease_Implementation(UGripMotionControllerComponent* ReleasingController, const FBPActorGripInformation& GripInformation, bool bWasSocketed = false) override;

	virtual bool Wants_DenyTeleport_Implementation(UGripMotionControllerComponent* Controller) override;

	//virtual void BeginPlay_Implementation() override;
	virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) override;


};
