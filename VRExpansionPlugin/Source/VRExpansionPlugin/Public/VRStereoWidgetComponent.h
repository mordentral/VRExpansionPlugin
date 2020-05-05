// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "VRBPDatatypes.h"
#include "VRGripInterface.h"
#include "Components/WidgetComponent.h"
#include "Components/StereoLayerComponent.h"
#include "StereoLayerShapes.h"

#include "VRStereoWidgetComponent.generated.h"


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
		UStereoLayerShape* Shape;

	void BeginDestroy() override;
	void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void DrawWidgetToRenderTarget(float DeltaTime) override;
	virtual TStructOnScope<FActorComponentInstanceData>  GetComponentInstanceData() const override;
	void ApplyVRComponentInstanceData(class FVRStereoWidgetComponentInstanceData* WidgetInstanceData);

	virtual void UpdateRenderTarget(FIntPoint DesiredRenderTargetSize) override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;

	/**
	* Change the quad size. This is the unscaled height and width, before component scale is applied.
	* @param	InQuadSize: new quad size.
	*/
	//UFUNCTION(BlueprintCallable, Category = "Components|Stereo Layer")
		//void SetQuadSize(FVector2D InQuadSize);

	// Manually mark the stereo layer texture for updating
	//UFUNCTION(BlueprintCallable, Category = "Components|Stereo Layer")
	//	void MarkTextureForUpdate();

	// If true, use Epics world locked stereo implementation instead of my own temp solution
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		bool bUseEpicsWorldLockedStereo;

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
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		uint32 bSupportsDepth : 1;

	/** True if the texture should not use its own alpha channel (1.0 will be substituted) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		uint32 bNoAlphaChannel : 1;

	/** True if the quad should internally set it's Y value based on the set texture's dimensions */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StereoLayer")
		uint32 bQuadPreserveTextureRatio : 1;

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

	bool bShouldCreateProxy;

private:
	/** Dirty state determines whether the stereo layer needs updating **/
	bool bIsDirty;
	bool bDirtyRenderTarget;

	/** Texture needs to be marked for update **/
	bool bTextureNeedsUpdate;

	/** IStereoLayer id, 0 is unassigned **/
	uint32 LayerId;

	/** Last transform is cached to determine if the new frames transform has changed **/
	FTransform LastTransform;

	/** Last frames visiblity state **/
	bool bLastVisible;

};