// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
//#include "Engine/Engine.h"
//#include "VRBPDatatypes.h"
#include "Components/StereoLayerComponent.h"
#include "Components/WidgetComponent.h"
//#include "Animation/UMGSequencePlayer.h"

#include "VRStereoWidgetComponent.generated.h"

class FWidgetRenderer;
class UUserWidget;
class UTextureRenderTarget2D;
class UStereoLayerShape;


/**
* A stereo component that displays a widget instead of a texture.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin), HideCategories = ("Stereoscopic Properties", Collision))
class VREXPANSIONPLUGIN_API UVRStereoWidgetRenderComponent : public UStereoLayerComponent
{
	GENERATED_BODY()

public:
	UVRStereoWidgetRenderComponent(const FObjectInitializer& ObjectInitializer);

	/** The class of User Widget to create and display an instance of */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
		TSubclassOf<UUserWidget> WidgetClass;

	/** The User Widget object displayed and managed by this component */
	UPROPERTY(BlueprintReadWrite, Transient, DuplicateTransient, Category = "WidgetSettings")
		TObjectPtr<UUserWidget> Widget;

	/** If true then we sample the requested size of the widget and reset the texture to be that size */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
		bool bDrawAtDesiredSize;

	/** The desired render scale of the widget */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
		float WidgetRenderScale;

	/** The desired render gamma of the widget */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
		float WidgetRenderGamma;

	/** Automatically correct for gamma */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
	bool bUseGammaCorrection;

	/** The desired clear color of the render target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
		FLinearColor RenderTargetClearColor;

	/** If true we will draw to the render target even without active stereo layers and skip the stereo tick*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
		bool bDrawWithoutStereo;

	/** Rate (HTZ) we should draw the texture at */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WidgetSettings", meta = (ExposeOnSpawn = true))
		float DrawRate;

	// Counts how long until next draw
	float DrawCounter;

	/** The Slate widget to be displayed by this component.  Only one of either Widget or SlateWidget can be used */
	TSharedPtr<SWidget> SlateWidget;

	class FWidgetRenderer* WidgetRenderer;

	/** The render target being display */
	UPROPERTY(BlueprintReadOnly, Transient, DuplicateTransient, Category = "WidgetSettings")
		TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** The slate window that contains the user widget content */
	TSharedPtr<class SVirtualWindow> SlateWindow;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void DestroyComponent(bool bPromoteChildren/*= false*/) override;

	UFUNCTION(BlueprintCallable, Category = "WidgetSettings")
	void SetWidgetAndInit(TSubclassOf<UUserWidget> NewWidgetClass);

	void OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld);
	void InitWidget();
	void RenderWidget(float DeltaTime);
	void ReleaseResources();
};


/**
* A widget component that displays the widget in a stereo layer instead of in worldspace.
* Currently this class uses a custom postion instead of the engines WorldLocked for stereo layers
* This is because world locked stereo layers don't account for player movement currently.
*/
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent), ClassGroup = (VRExpansionPlugin))
class VREXPANSIONPLUGIN_API UVRStereoWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UVRStereoWidgetComponent(const FObjectInitializer& ObjectInitializer);

	friend class FStereoLayerComponentVisualizer;

	~UVRStereoWidgetComponent();


	/** Specifies which shape of layer it is.  Note that some shapes will be supported only on certain platforms! **/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, NoClear, Instanced, Category = "StereoLayer", DisplayName = "Stereo Layer Shape")
		TObjectPtr<UStereoLayerShape> Shape;

	void BeginDestroy() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void DrawWidgetToRenderTarget(float DeltaTime) override;
	virtual TStructOnScope<FActorComponentInstanceData>  GetComponentInstanceData() const override;
	void ApplyVRComponentInstanceData(class FVRStereoWidgetComponentInstanceData* WidgetInstanceData);

	virtual void UpdateRenderTarget(FIntPoint DesiredRenderTargetSize) override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;


	/** If true then this stereo widget will skip visibility checks when in stereo mode */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		bool bAlwaysVisible = false;

	// If true forces the widget to render both stereo and world widgets
	// Overriden by the console command vr.ForceNoStereoWithVRWidgets if it is set to 1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		bool bRenderBothStereoAndWorld;

	/** Forces the widget to skip stereo regardless of all other settings (localized version of vr.ForceNoStereoWithVRWidgets)*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		bool bDrawWithoutStereo;

	// If true, use Epics world locked stereo implementation instead of my own temp solution
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		bool bUseEpicsWorldLockedStereo;

	// If true, will cache and delay the transform adjustment for one frame in order to sync with the game thread better
	// Not for use with late updated parents.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		bool bDelayForRenderThread;

	// If true will not render or update until false
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		bool bIsSleeping;

	/**
	* Change the layer's render priority, higher priorities render on top of lower priorities
	* @param	InPriority: Priority value
	*/
	UFUNCTION(BlueprintCallable, Category = "Components|Stereo Layer")
		void SetPriority(int32 InPriority);

	// @return the render priority
	UFUNCTION(BlueprintCallable, Category = "Components|Stereo Layer")
		int32 GetPriority() const { return Priority; }

	/** True if the stereo layer needs to support depth intersections with the scene geometry, if available on the platform */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		uint32 bSupportsDepth : 1;

	/** True if the texture should not use its own alpha channel (1.0 will be substituted) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		uint32 bNoAlphaChannel : 1;

	/** True if the quad should internally set it's Y value based on the set texture's dimensions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		uint32 bQuadPreserveTextureRatio : 1;

	/** Additional flags not included in IStereoLayers::ELayerFlags */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StereoLayer", Meta = (GetOptions = "EditorFlagCollector.GetFlagNames"))
		TArray<FName> AdditionalFlags;

	UPROPERTY()
	TObjectPtr<class UTexture2D> TextureRef = nullptr;

protected:
	/** Texture displayed on the stereo layer (is stereocopic textures are supported on the platfrom and more than one texture is provided, this will be the right eye) **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StereoLayer")
	//	class UTexture* Texture;

	// Forget left texture implementation
	/** Texture displayed on the stereo layer for left eye, if stereoscopic textures are supported on the platform **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "StereoLayer | Cubemap Overlay Properties")
	//	class UTexture* LeftTexture;

public:
	/** Size of the rendered stereo layer quad **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer | Quad Overlay Properties")
	//	FVector2D StereoLayerQuadSize;

	/** UV coordinates mapped to the quad face **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer | Quad Overlay Properties")
		FBox2D UVRect;

	/** Radial size of the rendered stereo layer cylinder **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer | Cylinder Overlay Properties")
	//	float CylinderRadius;

	/** Arc angle for the stereo layer cylinder **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer | Cylinder Overlay Properties")
		//float CylinderOverlayArc;

	/** Height of the stereo layer cylinder **/
	///UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer | Cylinder Overlay Properties")
	//	int CylinderHeight;

	/** Specifies how and where the quad is rendered to the screen **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer")
	//TEnumAsByte<enum EStereoLayerType> StereoLayerType;

	// Forcing quad layer so that it works with the widget better
	/** Specifies which type of layer it is.  Note that some shapes will be supported only on certain platforms! **/
	//UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer")
	//	TEnumAsByte<enum EStereoLayerShape> StereoLayerShape;

	/** Render priority among all stereo layers, higher priority render on top of lower priority **/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, export, Category = "StereoLayer")
		int32 Priority;

	/** IStereoLayer id, 0 is unassigned **/
	uint32 LayerId;

	bool bShouldCreateProxy;

private:
	/** Dirty state determines whether the stereo layer needs updating **/
	bool bIsDirty;
	bool bDirtyRenderTarget;

	/** Texture needs to be marked for update **/
	bool bTextureNeedsUpdate;

	/** Last transform is cached to determine if the new frames transform has changed **/
	FTransform LastTransform;

	/** Last frames visiblity state **/
	bool bLastVisible;

};