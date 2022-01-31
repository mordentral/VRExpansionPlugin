// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRGripScriptBase.h"
#include "GripScripts/GS_Default.h"
#include "GS_GunTools.generated.h"

class UGripMotionControllerComponent;

// Event thrown when we enter into virtual stock mode
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVRVirtualStockModeChangedSignature, bool, IsVirtualStockEngaged);

// Global settings for this player
USTRUCT(BlueprintType, Category = "GunSettings")
struct VREXPANSIONPLUGIN_API FBPVirtualStockSettings
{
	GENERATED_BODY()
public:

	// *Global Value* Should we auto snap to the virtual stock by a set distance
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock")
		bool bUseDistanceBasedStockSnapping;

	// *Global Value* The distance before snapping to the stock / unsnapping
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock")
		float StockSnapDistance;

	// *Global Value* The distance from the edge of the stock snap distance where it will be at 100% influence
	// Prior to this threshold being hit it will lerp from standard hold to the virtual stock version.
	// A value of 0.0f will leave it always off
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock", meta = (ClampMin = "0.00", UIMin = "0.00"))
		float StockSnapLerpThreshold;

	// Current lerp value of the stock from zero influence to full influence
	UPROPERTY(BlueprintReadOnly, Category = "VirtualStock")
		float StockLerpValue;

	// *Global Value* An offset to apply to the HMD location to be considered the neck / mount pivot 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock")
		FVector_NetQuantize100 StockSnapOffset;

	// *Global Value* If we want to have the stock location adjust to follow the primary hands Z value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock")
		bool bAdjustZOfStockToPrimaryHand;

	// *Global Value* Whether we should lerp the location of the rearmost (stock side) hand, mostly used for snipers.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock|Smoothing")
		bool bSmoothStockHand;

	// *Global Value* How much influence the virtual stock smoothing should have, 0.0f is zero smoothing, 1.0f is full smoothing, you should test with full smoothing to get the amount you
	// want and then set the smoothing value up until it feels right between the fully smoothed and unsmoothed values.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock|Smoothing", meta = (editcondition = "bSmoothStockHand", ClampMin = "0.00", UIMin = "0.00", ClampMax = "1.00", UIMax = "1.00"))
		float SmoothingValueForStock;

	// Used to smooth filter the virtual stocks primary hand location
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|VirtualStock|Smoothing")
		FBPEuroLowPassFilterTrans StockHandSmoothing;

	// Draw debug elements showing the virtual stock location and angles to interacting components
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings|VirtualStock|Debug")
	bool bDebugDrawVirtualStock;

	void CopyFrom(FBPVirtualStockSettings & B)
	{
		bUseDistanceBasedStockSnapping = B.bUseDistanceBasedStockSnapping;
		StockSnapDistance = B.StockSnapDistance;
		StockSnapLerpThreshold = B.StockSnapLerpThreshold;
		StockSnapOffset = B.StockSnapOffset;
		bAdjustZOfStockToPrimaryHand = B.bAdjustZOfStockToPrimaryHand;
		bSmoothStockHand = B.bSmoothStockHand;
		SmoothingValueForStock = B.SmoothingValueForStock;
		StockHandSmoothing = B.StockHandSmoothing;
	}

	FBPVirtualStockSettings()
	{
		StockSnapOffset = FVector(0.f, 0.f, 0.f);
		bAdjustZOfStockToPrimaryHand = true;
		StockSnapDistance = 35.f;
		StockSnapLerpThreshold = 20.0f;
		StockLerpValue = 0.0f;
		bUseDistanceBasedStockSnapping = true;
		SmoothingValueForStock = 0.0f;
		bSmoothStockHand = false;

		// Speed up the lerp on fast movements for this
		StockHandSmoothing.DeltaCutoff = 20.0f;
		StockHandSmoothing.MinCutoff = 5.0f;

		bDebugDrawVirtualStock = false;
	}
};

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FGunTools_AdvSecondarySettings
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings")
		bool bUseAdvancedSecondarySettings;

	// Scaler used for handling the smoothing amount, 0.0f is zero smoothing, 1.0f is full smoothing, you should test with full smoothing to get the amount you
	// want and then set the smoothing value up until it feels right between the fully smoothed and unsmoothed values.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings|Smoothing", meta = (editcondition = "bUseAdvancedSecondarySettings", ClampMin = "0.00", UIMin = "0.00", ClampMax = "1.00", UIMax = "1.00"))
		float SecondaryGripScaler;

	// If true we will constantly be lerping with the grip scaler instead of only sometimes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings|Smoothing", meta = (editcondition = "bUseAdvancedSecondarySettings"))
		bool bUseConstantGripScaler;

	// If true will override custom settings for the smoothing values with the global settings in VRSettings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings|Smoothing", meta = (editcondition = "bUseAdvancedSecondarySettings"))
		bool bUseGlobalSmoothingSettings;

	// Used to smooth filter the secondary influence
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings|Smoothing")
		FBPEuroLowPassFilter SecondarySmoothing;

	// Whether to scale the secondary hand influence off of distance from grip point
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings|DistanceInfluence", meta = (editcondition = "bUseAdvancedSecondarySettings"))
		bool bUseSecondaryGripDistanceInfluence;

	// If true, will use the GripInfluenceDeadZone as a constant value instead of calculating the distance and lerping, lets you define a static influence amount.
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SecondaryGripSettings", meta = (editcondition = "bUseSecondaryGripDistanceInfluence"))
	//	bool bUseGripInfluenceDeadZoneAsConstant;

	// Distance from grip point in local space where there is 100% influence
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings|DistanceInfluence", meta = (editcondition = "bUseSecondaryGripDistanceInfluence", ClampMin = "0.00", UIMin = "0.00", ClampMax = "256.00", UIMax = "256.00"))
		float GripInfluenceDeadZone;

	// Distance from grip point in local space before all influence is lost on the secondary grip (1.0f - 0.0f influence over this range)
	// this comes into effect outside of the deadzone
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AdvSecondarySettings|DistanceInfluence", meta = (editcondition = "bUseSecondaryGripDistanceInfluence", ClampMin = "1.00", UIMin = "1.00", ClampMax = "256.00", UIMax = "256.00"))
		float GripInfluenceDistanceToZero;

	FGunTools_AdvSecondarySettings()
	{
		bUseAdvancedSecondarySettings = false;
		SecondaryGripScaler = 0.0f;
		bUseGlobalSmoothingSettings = true;
		bUseSecondaryGripDistanceInfluence = false;
		//bUseGripInfluenceDeadZoneAsConstant(false),
		GripInfluenceDeadZone = 50.0f;
		GripInfluenceDistanceToZero = 100.0f;
		bUseConstantGripScaler = false;
	}
};


// A grip script that adds useful fire-arm related features to grips
// Just adding it to the grippable object provides the features without removing standard
// Grip features.
UCLASS(NotBlueprintable, ClassGroup = (VRExpansionPlugin), hideCategories = TickSettings)
class VREXPANSIONPLUGIN_API UGS_GunTools : public UGS_Default
{
	GENERATED_BODY()
public:

	UGS_GunTools(const FObjectInitializer& ObjectInitializer);

	virtual void OnGrip_Implementation(UGripMotionControllerComponent * GrippingController, const FBPActorGripInformation & GripInformation) override;
	virtual void OnSecondaryGrip_Implementation(UGripMotionControllerComponent * Controller, USceneComponent * SecondaryGripComponent, const FBPActorGripInformation & GripInformation) override;
	virtual void OnBeginPlay_Implementation(UObject* CallingOwner) override;
	virtual void HandlePrePhysicsHandle(UGripMotionControllerComponent* GrippingController, const FBPActorGripInformation &GripInfo, FBPActorPhysicsHandleInformation* HandleInfo, FTransform& KinPose) override;
	//virtual void HandlePostPhysicsHandle(UGripMotionControllerComponent* GrippingController, FBPActorPhysicsHandleInformation* HandleInfo) override;


	// The name of the component that is used to orient the weapon along its primary axis
	// If it does not exist then the weapon is assumed to be X+ facing.
	// Also used to perform some calculations, make sure it is parented to the gripped object (root component for actors),
	// and that the X+ vector of the orientation component is facing the forward direction of the weapon (gun tip for guns, ect).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Settings")
		FName WeaponRootOrientationComponent;
	FTransform OrientationComponentRelativeFacing;
	FQuat StoredRootOffset;

	// (default false) If true will run through the entire simulation that the owning client uses for the gun. If false, does a lighter and more performant approximation.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GunSettings")
		bool bUseHighQualityRemoteSimulation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GunSettings")
	FGunTools_AdvSecondarySettings AdvSecondarySettings;

	// Offset to apply to the pivot (good for centering pivot into the palm ect).
	// For this to apply to the physical center of mass as well an OrientationComponent needs to be defined
	// So that we have a valid directional vector to work off of, otherwise the pivot will be in component space and you 
	// will have a harder time aligning it if the weapon is off axis (still works, just less intuitive).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pivot")
		FVector_NetQuantize100 PivotOffset;

	UFUNCTION(BlueprintCallable, Category = "VirtualStock")
		void SetVirtualStockComponent(USceneComponent * NewStockComponent)
	{
		VirtualStockComponent = NewStockComponent;
	}

	UFUNCTION(BlueprintCallable, Category = "VirtualStock")
		void SetVirtualStockEnabled(bool bAllowVirtualStock)
	{
		if (!bUseVirtualStock && bAllowVirtualStock)
			ResetStockVariables();

		bUseVirtualStock = bAllowVirtualStock;
	}

	void ResetStockVariables()
	{
		VirtualStockSettings.StockHandSmoothing.ResetSmoothingFilter();
	}

	void GetVirtualStockTarget(UGripMotionControllerComponent * GrippingController);

	// Call to use an object
	UPROPERTY(BlueprintAssignable, Category = "VirtualStock")
		FVRVirtualStockModeChangedSignature OnVirtualStockModeChanged;

	// Overrides the pivot location to be at this component instead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock")
		bool bUseVirtualStock;

	FTransform MountWorldTransform;
	bool bIsMounted;
	FTransform RelativeTransOnSecondaryRelease;
	TWeakObjectPtr<USceneComponent> CameraComponent;

	// Overrides the default behavior of using the HMD location for the stock and uses this component instead
	UPROPERTY(BlueprintReadWrite, Category = "VirtualStock")
		TWeakObjectPtr<USceneComponent> VirtualStockComponent;

	// Loads the global virtual stock settings on grip (only if locally controlled, you need to manually replicate and store the global settings
	// In the character if networked).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock")
		bool bUseGlobalVirtualStockSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VirtualStock", meta = (editcondition = "!bUseGlobalVirtualStockSettings"))
		FBPVirtualStockSettings VirtualStockSettings;

	// If this gun has recoil
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
		bool bHasRecoil;

	// If true then the recoil will be added as a physical force instead of logical blend
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
		bool bApplyRecoilAsPhysicalForce;

	// Maximum recoil addition
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (editcondition = "bHasRecoil"))
		FVector_NetQuantize100 MaxRecoilTranslation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (editcondition = "bHasRecoil"))
		FVector_NetQuantize100 MaxRecoilRotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (editcondition = "bHasRecoil"))
		FVector_NetQuantize100 MaxRecoilScale;

	// Recoil decay rate, how fast it decays back to baseline
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (editcondition = "bHasRecoil"))
		float DecayRate;

	// Recoil lerp rate, how long it takes to lerp to the target recoil amount (0.0f would be instant)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (editcondition = "bHasRecoil"))
		float LerpRate;

	// Stores the current amount of recoil
	FTransform BackEndRecoilStorage;

	// Stores the target amount of recoil
	FTransform BackEndRecoilTarget;

	bool bHasActiveRecoil;
	
	// Adds a recoil instance to the gun tools, the option location is for if using the physical recoil mode
	// Physical recoil is in world space and positional only, logical recoil is in relative space to the mesh itself and uses all
	// of the transforms properties.
	UFUNCTION(BlueprintCallable, Category = "Recoil")
		void AddRecoilInstance(const FTransform & RecoilAddition, FVector Optional_Location = FVector::ZeroVector);

	UFUNCTION(BlueprintCallable, Category = "Recoil")
		void ResetRecoil();

	virtual bool GetWorldTransform_Implementation(UGripMotionControllerComponent * GrippingController, float DeltaTime, FTransform & WorldTransform, const FTransform &ParentTransform, FBPActorGripInformation &Grip, AActor * actor, UPrimitiveComponent * root, bool bRootHasInterface, bool bActorHasInterface, bool bIsForTeleport) override;
	
	// Applies the two hand modifier, broke this out into a function so that we can handle late updates
	static void ApplyTwoHandModifier(FTransform & OriginalTransform)
	{


	}

	// Returns the smoothed value now
	inline FVector GunTools_ApplySmoothingAndLerp(FBPActorGripInformation & Grip, FVector &frontLoc, FVector & frontLocOrig, float DeltaTime, bool bSkipHighQualitySimulations)
	{
		FVector SmoothedValue = frontLoc;

		if (Grip.SecondaryGripInfo.GripLerpState == EGripLerpState::StartLerp) // Lerp into the new grip to smooth the transition
		{
			if (!bSkipHighQualitySimulations && AdvSecondarySettings.SecondaryGripScaler < 1.0f)
			{
				SmoothedValue = AdvSecondarySettings.SecondarySmoothing.RunFilterSmoothing(frontLoc, DeltaTime);
				frontLoc = FMath::Lerp(frontLoc, SmoothedValue, AdvSecondarySettings.SecondaryGripScaler);

			}
			//Default_ApplySmoothingAndLerp(Grip, frontLoc, frontLocOrig, DeltaTime);
		}
		else if (!bSkipHighQualitySimulations && AdvSecondarySettings.bUseAdvancedSecondarySettings && AdvSecondarySettings.bUseConstantGripScaler) // If there is a frame by frame lerp
		{
			SmoothedValue = AdvSecondarySettings.SecondarySmoothing.RunFilterSmoothing(frontLoc, DeltaTime);
			frontLoc = FMath::Lerp(frontLoc, SmoothedValue, AdvSecondarySettings.SecondaryGripScaler);
		}

		return SmoothedValue;
	}
};

