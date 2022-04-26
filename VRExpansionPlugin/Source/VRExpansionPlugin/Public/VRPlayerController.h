// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "VRBPDatatypes.h"
#include "VRPathFollowingComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "VRPlayerController.generated.h"


// A base player controller specifically for handling OnCameraManagerCreated.
// Used in case you don't want the VRPlayerCharacter changes in a PendingPlayerController
UCLASS()
class VREXPANSIONPLUGIN_API AVRBasePlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	// Event called in BPs when the camera manager is created (only fired on locally controlled player controllers)
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnCameraManagerCreated"), Category = Actor)
	void OnCameraManagerCreated(APlayerCameraManager* CameraManager);

	virtual void SpawnPlayerCameraManager() override
	{
		Super::SpawnPlayerCameraManager();

		if (PlayerCameraManager != NULL && IsLocalController())
		{
			OnCameraManagerCreated(PlayerCameraManager);
		}
	}

};


UCLASS()
class VREXPANSIONPLUGIN_API AVRPlayerController : public AVRBasePlayerController
{
	GENERATED_BODY()

public:
	AVRPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// New path finding return, not actually sending anything currently unless the character created one for us
	// or the user added one to us. The default implementation is fine for us.
	//virtual IPathFollowingAgentInterface* GetPathFollowingAgent() const override;

	// Disable the ServerUpdateCamera function defaulted on in PlayerCameraManager
	// We are manually replicating the camera position and rotation ourselves anyway
	// Generally that function will just be additional replication overhead
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VRPlayerController")
		bool bDisableServerUpdateCamera;

	/** spawn cameras for servers and owning players */
	virtual void SpawnPlayerCameraManager() override;

	FRotator LastRotationInput;

	/**
	* Processes player input (immediately after PlayerInput gets ticked) and calls UpdateRotation().
	* PlayerTick is only called if the PlayerController has a PlayerInput object. Therefore, it will only be called for locally controlled PlayerControllers.
	* I am overriding this so that for VRCharacters it doesn't apply the view rotation and instead lets CMC handle it
	*/
	virtual void PlayerTick(float DeltaTime) override;
};


/**
* Utility class, when set as the default local player it will spawn the target PlayerController class instead as the pending player controller
*/
UCLASS(BlueprintType, Blueprintable, meta = (ShortTooltip = "Utility class, when set as the default local player it will spawn the target PlayerController class instead as the pending one"))
class VREXPANSIONPLUGIN_API UVRLocalPlayer : public ULocalPlayer
{
	GENERATED_UCLASS_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LocalPlayer")
	TSubclassOf<class APlayerController> OverridePendingLevelPlayerControllerClass;
	
	virtual bool SpawnPlayActor(const FString& URL, FString& OutError, UWorld* InWorld)
	{
		if (OverridePendingLevelPlayerControllerClass)
		{
			PendingLevelPlayerControllerClass = OverridePendingLevelPlayerControllerClass;
		}

		return Super::SpawnPlayActor(URL, OutError, InWorld);
	}
};