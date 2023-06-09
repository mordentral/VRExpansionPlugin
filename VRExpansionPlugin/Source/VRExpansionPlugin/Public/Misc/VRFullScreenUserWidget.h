// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Info.h"
#include "Components/WidgetComponent.h"
#include "ISpectatorScreenController.h"
#include "Templates/NonNullPointer.h"
//#include "CompositingElement.h"
#include "VRFullScreenUserWidget.generated.h"

class FSceneViewport;
class FWidgetRenderer;
class FVRWidgetPostProcessHitTester;
class SConstraintCanvas;
class SVirtualWindow;
class SViewport;
class SWidget;
class ULevel;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPostProcessComponent;
class UTextureRenderTarget2D;
class UWorld;

#if WITH_EDITOR
class SLevelViewport;
#endif


UENUM(BlueprintType)
enum class EVRWidgetDisplayType : uint8
{
	/** Do not display. */
	Inactive,
	/** Display on a game viewport. */
	Viewport,
	/** Display as a post process. */
	PostProcess,
	/** Render to a texture and send to composure. */
   // Composure,
};


USTRUCT()
struct FVRFullScreenUserWidget_Viewport
{
	GENERATED_BODY()

public:
	//FVRFullScreenUserWidget_Viewport();
	bool Display(UWorld* World, UUserWidget* Widget, TAttribute<float> InDPIScale);
	void Hide(UWorld* World);
	//void Tick(UWorld* World, float DeltaSeconds);

#if WITH_EDITOR
	/**
	 * The viewport to use for displaying.
	 * Defaults to GetFirstActiveLevelViewport().

	 */
	TWeakPtr<FSceneViewport> EditorTargetViewport;
#endif

private:

	/** Constraint widget that contains the widget we want to display. */
	TWeakPtr<SConstraintCanvas> FullScreenCanvasWidget;

#if WITH_EDITOR
	/** Level viewport the widget was added to. */
	TWeakPtr<SLevelViewport> OverlayWidgetLevelViewport;
#endif
};

USTRUCT()
struct FVRFullScreenUserWidget_PostProcess
{
	GENERATED_BODY()

	FVRFullScreenUserWidget_PostProcess();
	void SetCustomPostProcessSettingsSource(TWeakObjectPtr<UObject> InCustomPostProcessSettingsSource);
	bool Display(UWorld* World, UUserWidget* Widget, bool bInRenderToTextureOnly, TAttribute<float> InDPIScale);
	void Hide(UWorld* World);
	void Tick(UWorld* World, float DeltaSeconds);

	TSharedPtr<SVirtualWindow> VREXPANSIONPLUGIN_API GetSlateWindow() const;

private:
	//bool CreatePostProcessComponent(UWorld* World);
	//void ReleasePostProcessComponent();

	bool CreateRenderer(UWorld* World, UUserWidget* Widget, TAttribute<float> InDPIScale);
	void ReleaseRenderer();
	void TickRenderer(UWorld* World, float DeltaSeconds);

	FIntPoint CalculateWidgetDrawSize(UWorld* World);
	bool IsTextureSizeValid(FIntPoint Size) const;

	void RegisterHitTesterWithViewport(UWorld* World);
	void UnRegisterHitTesterWithViewport();

	TSharedPtr<SViewport> GetViewport(UWorld* World) const;
	float GetDPIScaleForPostProcessHitTester(TWeakObjectPtr<UWorld> World) const;
	FPostProcessSettings* GetPostProcessSettings() const;

public:
	/**
	 * Post process material used to display the widget.
	 * SlateUI [Texture]
	 * TintColorAndOpacity [Vector]
	 * OpacityFromTexture [Scalar]
	 */
	//UPROPERTY(EditAnywhere, Category = PostProcess)
	//UMaterialInterface* PostProcessMaterial;

	/** Tint color and opacity for this component. */
	//UPROPERTY(EditAnywhere, Category = PostProcess)
	//FLinearColor PostProcessTintColorAndOpacity;

	/** Sets the amount of opacity from the widget's UI texture to use when rendering the translucent or masked UI to the viewport (0.0-1.0). */
	//UPROPERTY(EditAnywhere, Category = PostProcess, meta = (ClampMin = 0.0f, ClampMax = 1.0f))
	//float PostProcessOpacityFromTexture;

	/** The size of the rendered widget. */
	UPROPERTY(EditAnywhere, Category = PostProcess, meta=(InlineEditConditionToggle))
	bool bWidgetDrawSize;

	/** The size of the rendered widget. */
	UPROPERTY(EditAnywhere, Category = PostProcess, meta=(EditCondition= bWidgetDrawSize))
	FIntPoint WidgetDrawSize;

	/** Is the virtual window created to host the widget focusable? */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	bool bWindowFocusable;

	/** The visibility of the virtual window created to host the widget. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	EWindowVisibility WindowVisibility;

	/** Register with the viewport for hardware input from the mouse and keyboard. It can and will steal focus from the viewport. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category= PostProcess)
	bool bReceiveHardwareInput;

	/** The background color of the render target */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	FLinearColor RenderTargetBackgroundColor;

	/** The blend mode for the widget. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
	EWidgetBlendMode RenderTargetBlendMode;

	/** List of composure layers that are expecting to use the WidgetRenderTarget. */
	//UPROPERTY(EditAnywhere, Category= PostProcess)
    //TArray<ACompositingElement*> ComposureLayerTargets;

	/** The target to which the user widget is rendered. */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> WidgetRenderTarget;

	/** Only render to the UTextureRenderTarget2D - do not output to the final viewport. Unless DrawtoVRPreview is active */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
		bool bRenderToTextureOnly;

	/** If we should automatically try to draw and manage this to the VR Preview */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = PostProcess)
		bool bDrawToVRPreview;

	// VR Spectator mode to use when active
	UPROPERTY(EditAnywhere, Category = "User Interface")
		ESpectatorScreenMode VRDisplayType;

	// VR Spectator mode to use when not active
	UPROPERTY(EditAnywhere, Category = "User Interface")
		ESpectatorScreenMode PostVRDisplayType;

#if WITH_EDITOR
	/**
	 * The viewport to use for displaying.
	 *
	 * Defaults to GetFirstActiveLevelViewport().
	 */
	TWeakPtr<FSceneViewport> EditorTargetViewport;
#endif
private:
	/** Post process component used to add the material to the post process chain. */
	//UPROPERTY(Transient)
	//UPostProcessComponent* PostProcessComponent;

	/** The dynamic instance of the material that the render target is attached to. */
	//UPROPERTY(Transient)
	//UMaterialInstanceDynamic* PostProcessMaterialInstance;

	/** The slate window that contains the user widget content. */
	TSharedPtr<SVirtualWindow> SlateWindow;

	/** The slate viewport we are registered to. */
	TWeakPtr<SViewport> ViewportWidget;

	/** Helper class for drawing widgets to a render target. */
	FWidgetRenderer* WidgetRenderer;

	/** The size of the rendered widget */
	FIntPoint CurrentWidgetDrawSize;

	/** Hit tester when we want the hardware input. */
	TSharedPtr<FVRWidgetPostProcessHitTester> CustomHitTestPath;

};

/**
 * Will set the Widgets on a viewport either by Widgets are first rendered to a render target, then that render target is displayed in the world.
 */
UCLASS(BlueprintType, meta=(ShowOnlyInnerProperties))
class VREXPANSIONPLUGIN_API UVRFullScreenUserWidget : public UObject
{
	GENERATED_BODY()

public:
	UVRFullScreenUserWidget(const FObjectInitializer& ObjectInitializer);

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	bool ShouldDisplay(UWorld* World) const;
	EVRWidgetDisplayType GetDisplayType(UWorld* World) const;
	bool IsDisplayed() const;

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetComp")
	bool IsDisplayRequested()
	{
		return bDisplayRequested;
	}

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetComp")
	virtual bool Display(UWorld* World);

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetComp")
	virtual void Hide();

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetComp")
		void SetIsHidden(bool bNewHidden)
	{
		if (bNewHidden)
		{
			Hide();
		}
		else
		{
			if (World.IsValid())
			{
				Display(World.Get());
			}
			else
			{
				UWorld* myWorld = this->GetWorld();
				if (myWorld)
				{
					Display(myWorld);
				}
			}
		}
	}

	virtual void Tick(float DeltaTime);

	void SetDisplayTypes(EVRWidgetDisplayType InEditorDisplayType, EVRWidgetDisplayType InGameDisplayType, EVRWidgetDisplayType InPIEDisplayType);
	void SetOverrideWidget(UUserWidget* InWidget);

	/**
	 * If using EVPWidgetDisplayType::PostProcess, you can specify a custom post process settings that should be modified.
	 * By default, a new post process component is added to AWorldSettings.
	 *
	 * @param InCustomPostProcessSettingsSource An object containing a FPostProcessSettings UPROPERTY()
	 */
	//void SetCustomPostProcessSettingsSource(TWeakObjectPtr<UObject> InCustomPostProcessSettingsSource);

#if WITH_EDITOR
	/**
	 * Sets the TargetViewport to use on both the Viewport and the PostProcess class.
	 *
	 * Overrides the viewport to use for displaying.
	 * Defaults to GetFirstActiveLevelViewport().
	 */
	void SetEditorTargetViewport(TWeakPtr<FSceneViewport> InTargetViewport);
	/** Resets the TargetViewport  */
	void ResetEditorTargetViewport();
#endif

protected:
	bool InitWidget();
	void ReleaseWidget();

	FVector2D FindSceneViewportSize();
	float GetViewportDPIScale();

private:
	void OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld);
	void OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources);

public:

	/** The display type when the world is an editor world. */
	UPROPERTY(EditAnywhere, Category = "User Interface")
		EVRWidgetDisplayType EditorDisplayType;

	/** The display type when the world is a game world. */
	UPROPERTY(EditAnywhere, Category = "User Interface")
		EVRWidgetDisplayType GameDisplayType;

	/** The display type when the world is a PIE world. */
	UPROPERTY(EditAnywhere, Category = "User Interface", meta = (DisplayName = "PIE Display Type"))
		EVRWidgetDisplayType PIEDisplayType;

	/** Behavior when the widget should be display by the slate attached to the viewport. */
	UPROPERTY(EditAnywhere, Category = "Viewport", meta = (ShowOnlyInnerProperties))
		FVRFullScreenUserWidget_Viewport ViewportDisplayType;

	/** The class of User Widget to create and display an instance of */
	UPROPERTY(EditAnywhere, Category = "User Interface")
	TSubclassOf<UUserWidget> WidgetClass;

	/** Behavior when the widget should be display by a post process. */
	UPROPERTY(EditAnywhere, Category = "Post Process", meta = (ShowOnlyInnerProperties))
	FVRFullScreenUserWidget_PostProcess PostProcessDisplayType;

	// Get a pointer to the inner widget.
	// Note: This should not be stored!
	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetComp")
	UUserWidget* GetWidget() const { return Widget; };

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetComp")
		UTextureRenderTarget2D* GetPostProcessRenderTarget() const { return PostProcessDisplayType.WidgetRenderTarget; };

private:
	/** The User Widget object displayed and managed by this component */
	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UUserWidget> Widget;

	/** The world the widget is attached to. */
	TWeakObjectPtr<UWorld> World;

	/** How we currently displaying the widget. */
	EVRWidgetDisplayType CurrentDisplayType;

	/** The user requested the widget to be displayed. It's possible that some setting are invalid and the widget will not be displayed. */
	bool bDisplayRequested;

#if WITH_EDITOR
	/**
	 * The viewport to use for displaying.
	 * Defaults to GetFirstActiveLevelViewport().
	 */
	TWeakPtr<FSceneViewport> EditorTargetViewport;
#endif
};

/**
 * Widgets are first rendered to a render target, then that render target is displayed in the world.
 */
UCLASS(Blueprintable, ClassGroup = "UserInterface", HideCategories = (Actor, Input, Movement, Collision, Rendering, "Utilities|Transformation", LOD), ShowCategories = ("Input|MouseInput", "Input|TouchInput"))
class VREXPANSIONPLUGIN_API AVRFullScreenUserWidgetActor : public AInfo
{
	GENERATED_BODY()

public:
	AVRFullScreenUserWidgetActor(const FObjectInitializer& ObjectInitializer);

	//~ Begin AActor interface
	virtual void PostInitializeComponents() override;
	virtual void PostLoad() override;
	virtual void PostActorCreated() override;
	virtual void Destroyed() override;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** If true the widget will be shown right away, if false you will need to set SetWidgetVisible(true) to show it */
	UPROPERTY(EditAnywhere, Category = "User Interface")
		bool bShowOnInit;

	/** */
	UPROPERTY(VisibleAnywhere, Instanced, NoClear, Category = "User Interface", meta = (ShowOnlyInnerProperties))
		UVRFullScreenUserWidget* ScreenUserWidget;

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetActor")
		UVRFullScreenUserWidget* GetPreviewWidgetComp();

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetActor")
	void SetPIEDisplayType(EVRWidgetDisplayType NewDisplayType)
	{
		if (IsValid(ScreenUserWidget))
		{
			ScreenUserWidget->PIEDisplayType = NewDisplayType;
			ScreenUserWidget->Hide();
			RequestGameDisplay();
		}
	}

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetActor")
	void SetGameDisplayType(EVRWidgetDisplayType NewDisplayType)
	{
		if (IsValid(ScreenUserWidget))
		{
			ScreenUserWidget->GameDisplayType = NewDisplayType;
			ScreenUserWidget->Hide();
			RequestGameDisplay();
		}
	}

	// Set the widget to visible or not, this will be overriden by any changed to the actors hidden state
	// IE: Setting actor to hidden will force this hidden as well, also setting the actor to visible will do the opposite
	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetActor")
		void SetWidgetVisible(bool bIsVisible);

	virtual void SetActorHiddenInGame(bool bNewHidden) override
	{
		SetWidgetVisible(bNewHidden);
		Super::SetActorHiddenInGame(bNewHidden);
	}

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetActor")
		UUserWidget* GetWidget();

	UFUNCTION(BlueprintCallable, Category = "FullScreenWidgetActor")
		UTextureRenderTarget2D* GetPostProcessRenderTarget();

#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif //WITH_EDITOR
	//~ End AActor Interface

private:
	void RequestEditorDisplay();
	void RequestGameDisplay();

protected:


#if WITH_EDITORONLY_DATA
	/** Display requested and will be executed on the first frame because we can't call BP function in the loading phase */
	bool bEditorDisplayRequested;
#endif
};
