#pragma once

#include "ComponentVisualizer.h"
#include "Grippables/HandSocketComponent.h"
#include "ActorEditorUtils.h"

/**Base class for clickable targeting editing proxies*/
struct VREXPANSIONEDITOR_API HHandSocketVisProxy : public HComponentVisProxy
{
	DECLARE_HIT_PROXY();

    HHandSocketVisProxy(const UActorComponent* InComponent)
		: HComponentVisProxy(InComponent, HPP_Wireframe)
	{
		BoneIdx = 0;
		TargetBoneName = NAME_None;
	}

	uint32 BoneIdx;
	FName TargetBoneName;
};

class VREXPANSIONEDITOR_API FHandSocketVisualizer : public FComponentVisualizer
{
public:
	FHandSocketVisualizer()
	{
		CurrentlySelectedBone = NAME_None;
		CurrentlySelectedBoneIdx = INDEX_NONE;
		HandPropertyPath = FComponentPropertyPath();
		TargetViewport = nullptr;
	}

	virtual ~FHandSocketVisualizer()
	{

	}

	UPROPERTY()
		FComponentPropertyPath HandPropertyPath;

	FName CurrentlySelectedBone;
	uint32 CurrentlySelectedBoneIdx;

	UPROPERTY()
	FViewport* TargetViewport;

	UHandSocketComponent* GetCurrentlyEditingComponent() const
	{ 
		return Cast<UHandSocketComponent>(HandPropertyPath.GetComponent());;
	}

	const UHandSocketComponent* UpdateSelectedHandComponent(HComponentVisProxy* VisProxy)
	{
		const UHandSocketComponent* HandComp = CastChecked<const UHandSocketComponent>(VisProxy->Component.Get());
		UHandSocketComponent* OldHandComp = Cast<UHandSocketComponent>(HandPropertyPath.GetComponent());
		AActor* OldOwningActor = HandPropertyPath.GetParentOwningActor();
		HandPropertyPath = FComponentPropertyPath(HandComp);
		AActor* NewOwningActor = HandPropertyPath.GetParentOwningActor();

		if (HandPropertyPath.IsValid())
		{
			if (OldOwningActor != NewOwningActor || OldHandComp != HandComp)
			{
				// Reset selection state if we are selecting a different actor to the one previously selected
				CurrentlySelectedBoneIdx = INDEX_NONE;
				CurrentlySelectedBone = NAME_None;
			}

			return HandComp;
		}

		HandPropertyPath = FComponentPropertyPath();
		return nullptr;
	}

	bool SaveAnimationAsset(const FString& InAssetPath, const FString& InAssetName);


	bool GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const override;

	bool IsVisualizingArchetype() const override;

	virtual void DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;
	virtual bool VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click) override;
	bool GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const override;
	bool HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale) override;
	virtual void EndEditing() override;

private:

};
