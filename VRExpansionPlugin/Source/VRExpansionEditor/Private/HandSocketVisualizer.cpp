// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "HandSocketVisualizer.h"
#include "SceneManagement.h"
//#include "UObject/Field.h"
#include "VRBPDatatypes.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
//#include "Persona.h"

IMPLEMENT_HIT_PROXY(HHandSocketVisProxy, HComponentVisProxy);
#define LOCTEXT_NAMESPACE "HandSocketVisualizer"

bool FHandSocketVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	bool bEditing = false;
	if (VisProxy && VisProxy->Component.IsValid())
	{
		bEditing = true;
		if (VisProxy->IsA(HHandSocketVisProxy::StaticGetType()))
		{

			if( const UHandSocketComponent * HandComp = UpdateSelectedHandComponent(VisProxy))
			{
				HHandSocketVisProxy* Proxy = (HHandSocketVisProxy*)VisProxy;
				if (Proxy)
				{
					CurrentlySelectedBone = Proxy->TargetBoneName;
					CurrentlySelectedBoneIdx = Proxy->BoneIdx;
					TargetViewport = InViewportClient->Viewport;
				}
			}
		}
	}

	return bEditing;
}


bool FHandSocketVisualizer::GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const
{
	if (TargetViewport == nullptr || TargetViewport != ViewportClient->Viewport)
	{
		return false;
	}

	if (HandPropertyPath.IsValid() && CurrentlySelectedBone != NAME_None/* && CurrentlySelectedBone != "HandSocket"*/)
	{
		if (CurrentlySelectedBone == "HandSocket")
		{
			UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent();
			if (CurrentlyEditingComponent)
			{
				if (CurrentlyEditingComponent->bMirrorVisualizationMesh)
				{
					FTransform NewTrans = CurrentlyEditingComponent->GetRelativeTransform();
					NewTrans.Mirror(CurrentlyEditingComponent->GetAsEAxis(CurrentlyEditingComponent->MirrorAxis), CurrentlyEditingComponent->GetAsEAxis(CurrentlyEditingComponent->FlipAxis));

					if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
					{
						NewTrans = NewTrans * ParentComp->GetComponentTransform();
					}

					OutMatrix = FRotationMatrix::Make(NewTrans.GetRotation());
				}
			}

			return false;
		}
		else if (CurrentlySelectedBone == "Visualizer")
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{

				FTransform newTrans = FTransform::Identity;
				if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
				{
					if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
					{
						newTrans = CurrentlyEditingComponent->HandRelativePlacement * ParentComp->GetComponentTransform();
					}
				}
				else
				{
					newTrans = CurrentlyEditingComponent->GetHandRelativePlacement() * CurrentlyEditingComponent->GetComponentTransform();
				}

				OutMatrix = FRotationMatrix::Make(newTrans.GetRotation());
			}
		}
		else
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				FTransform newTrans = CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx);
				OutMatrix = FRotationMatrix::Make(newTrans.GetRotation());
			}
		}

		return true;
	}

	return false;
}

bool FHandSocketVisualizer::IsVisualizingArchetype() const
{
	return (HandPropertyPath.IsValid() && HandPropertyPath.GetParentOwningActor() && FActorEditorUtils::IsAPreviewOrInactiveActor(HandPropertyPath.GetParentOwningActor()));
}

void FHandSocketVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (TargetViewport == nullptr || TargetViewport != Viewport)
	{
		return;
	}

	if (const UHandSocketComponent* HandComp = Cast<const UHandSocketComponent>(Component))
	{
		if (CurrentlySelectedBone != NAME_None)
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				if (!CurrentlyEditingComponent->HandVisualizerComponent)
				{
					return;
				}

				int32 XL;
				int32 YL;
				const FIntRect CanvasRect = Canvas->GetViewRect();

				FPlane location = View->Project(CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx).GetLocation());
				StringSize(GEngine->GetLargeFont(), XL, YL, *CurrentlySelectedBone.ToString());
				//const float DrawPositionX = location.X - XL;
				//const float DrawPositionY = location.Y - YL;
				const float DrawPositionX = FMath::FloorToFloat(CanvasRect.Min.X + (CanvasRect.Width() - XL) * 0.5f);
				const float DrawPositionY = CanvasRect.Min.Y + 50.0f;
				Canvas->DrawShadowedString(DrawPositionX, DrawPositionY, *CurrentlySelectedBone.ToString(), GEngine->GetLargeFont(), FLinearColor::Yellow);
			}
		}
	}
}

void FHandSocketVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	//UWorld* World = Component->GetWorld();
	//return World && (World->WorldType == EWorldType::EditorPreview || World->WorldType == EWorldType::Inactive);

	//cast the component into the expected component type
	if (const UHandSocketComponent* HandComponent = Cast<UHandSocketComponent>(Component))
	{
		if (!HandComponent->HandVisualizerComponent)
			return;

		//This is an editor only uproperty of our targeting component, that way we can change the colors if we can't see them against the background
		const FLinearColor SelectedColor = FLinearColor::Yellow;//TargetingComponent->EditorSelectedColor;
		const FLinearColor UnselectedColor = FLinearColor::White;//TargetingComponent->EditorUnselectedColor;
		const FVector Location = HandComponent->HandVisualizerComponent->GetComponentLocation();
		float BoneScale = 1.0f - ((View->ViewLocation - Location).SizeSquared() / FMath::Square(100.0f));
		BoneScale = FMath::Clamp(BoneScale, 0.2f, 1.0f);
		HHandSocketVisProxy* newHitProxy = new HHandSocketVisProxy(Component);
		newHitProxy->TargetBoneName = "Visualizer";
		PDI->SetHitProxy(newHitProxy);
		PDI->DrawPoint(Location, CurrentlySelectedBone == newHitProxy->TargetBoneName ? SelectedColor : FLinearColor::Red, 20.f * BoneScale, SDPG_Foreground);
		PDI->SetHitProxy(NULL);
		newHitProxy = nullptr;

		newHitProxy = new HHandSocketVisProxy(Component);
		newHitProxy->TargetBoneName = "HandSocket";
		BoneScale = 1.0f - ((View->ViewLocation - HandComponent->GetComponentLocation()).SizeSquared() / FMath::Square(100.0f));
		BoneScale = FMath::Clamp(BoneScale, 0.2f, 1.0f);
		PDI->SetHitProxy(newHitProxy);
		PDI->DrawPoint(HandComponent->GetComponentLocation(), FLinearColor::Green, 20.f * BoneScale, SDPG_Foreground);
		PDI->SetHitProxy(NULL);
		newHitProxy = nullptr;

		if (HandComponent->bUseCustomPoseDeltas)
		{
			TArray<FTransform> BoneTransforms = HandComponent->HandVisualizerComponent->GetBoneSpaceTransforms();
			FTransform ParentTrans = HandComponent->HandVisualizerComponent->GetComponentTransform();
			// We skip root bone, moving the visualizer itself handles that
			for (int i = 1; i < HandComponent->HandVisualizerComponent->GetNumBones(); i++)
			{
				FName BoneName = HandComponent->HandVisualizerComponent->GetBoneName(i);
				FTransform BoneTransform = HandComponent->HandVisualizerComponent->GetBoneTransform(i);
				FVector BoneLoc = BoneTransform.GetLocation();
				BoneScale = 1.0f - ((View->ViewLocation - BoneLoc).SizeSquared() / FMath::Square(100.0f));
				BoneScale = FMath::Clamp(BoneScale, 0.1f, 0.9f);
				newHitProxy = new HHandSocketVisProxy(Component);
				newHitProxy->TargetBoneName = BoneName;
				newHitProxy->BoneIdx = i;
				PDI->SetHitProxy(newHitProxy);
				PDI->DrawPoint(BoneLoc, CurrentlySelectedBone == newHitProxy->TargetBoneName ? SelectedColor : UnselectedColor, 20.f * BoneScale, SDPG_Foreground);
				PDI->SetHitProxy(NULL);
				newHitProxy = nullptr;
			}
		}

		if (HandComponent->bShowRangeVisualization)
		{
			float RangeVisualization = HandComponent->OverrideDistance;

			if (RangeVisualization <= 0.0f)
			{
				if (USceneComponent* Parent = Cast<USceneComponent>(HandComponent->GetAttachParent()))
				{
					FStructProperty* ObjectProperty = CastField<FStructProperty>(Parent->GetClass()->FindPropertyByName("VRGripInterfaceSettings"));

					AActor* ParentsActor = nullptr;
					if (!ObjectProperty)
					{
						ParentsActor = Parent->GetOwner();
						if (ParentsActor)
						{
							ObjectProperty = CastField<FStructProperty>(Parent->GetOwner()->GetClass()->FindPropertyByName("VRGripInterfaceSettings"));
						}
					}

					if (ObjectProperty)
					{
						UObject* Target = ParentsActor;

						if (Target == nullptr)
						{
							Target = Parent;
						}

						if (const FBPInterfaceProperties* Curve = ObjectProperty->ContainerPtrToValuePtr<FBPInterfaceProperties>(Target))
						{
							if (HandComponent->SlotPrefix == "VRGripS")
							{
								RangeVisualization = Curve->SecondarySlotRange;
							}
							else
							{
								RangeVisualization = Curve->PrimarySlotRange;
							}
						}
					}
				}
			}

			// Scale into our parents space as that is actually what the range is based on			
			FBox BoxToDraw = FBox::BuildAABB(FVector::ZeroVector, FVector(RangeVisualization) * HandComponent->GetAttachParent()->GetComponentScale());
			BoxToDraw.Min += HandComponent->GetComponentLocation();
			BoxToDraw.Max += HandComponent->GetComponentLocation();

			DrawWireBox(PDI, BoxToDraw, FColor::Green, 0.0f);
		}
	}
}

bool FHandSocketVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	if (TargetViewport == nullptr || TargetViewport != ViewportClient->Viewport)
	{
		return false;
	}

	if (HandPropertyPath.IsValid() && CurrentlySelectedBone != NAME_None && CurrentlySelectedBone != "HandSocket")
	{
		if (CurrentlySelectedBone == "HandSocket")
		{
			return false;
		}
		else if (CurrentlySelectedBone == "Visualizer")
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				FTransform newTrans = FTransform::Identity;
				if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
				{
					if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
					{
						newTrans = CurrentlyEditingComponent->HandRelativePlacement * ParentComp->GetComponentTransform();
					}
				}
				else
				{
					newTrans = CurrentlyEditingComponent->GetHandRelativePlacement() * CurrentlyEditingComponent->GetComponentTransform();
				}

				OutLocation = newTrans.GetLocation();
			}
		}
		else
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				OutLocation = CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx).GetLocation();
			}
		}

		return true;
	}

	return false;
}

bool FHandSocketVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{

	if (TargetViewport == nullptr || TargetViewport != Viewport)
	{
		return false;
	}

	bool bHandled = false;

	if (HandPropertyPath.IsValid())
	{
		if (CurrentlySelectedBone == "HandSocket" || CurrentlySelectedBone == NAME_None)
		{
			bHandled = false;
		}
		else if (CurrentlySelectedBone == "Visualizer")
		{
			const FScopedTransaction Transaction(LOCTEXT("ChangingComp", "ChangingComp"));

			UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent();
			if (!CurrentlyEditingComponent)
			{
				return false;
			}

			CurrentlyEditingComponent->Modify();
			if (AActor* Owner = CurrentlyEditingComponent->GetOwner())
			{
				Owner->Modify();
			}
			bool bLevelEdit = ViewportClient->IsLevelEditorClient();

			FTransform CurrentTrans = FTransform::Identity;

			if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
			{
				if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
				{
					CurrentTrans = CurrentlyEditingComponent->HandRelativePlacement * ParentComp->GetComponentTransform();
				}
			}
			else
			{
				CurrentTrans = CurrentlyEditingComponent->GetHandRelativePlacement() * CurrentlyEditingComponent->GetComponentTransform();
			}

			if (!DeltaTranslate.IsNearlyZero())
			{
				CurrentTrans.AddToTranslation(DeltaTranslate);
			}

			if (!DeltaRotate.IsNearlyZero())
			{
				CurrentTrans.SetRotation(DeltaRotate.Quaternion() * CurrentTrans.GetRotation());
			}

			if (!DeltaScale.IsNearlyZero())
			{
				CurrentTrans.MultiplyScale3D(DeltaScale);
			}

			if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
			{
				if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
				{
					CurrentlyEditingComponent->HandRelativePlacement = CurrentTrans.GetRelativeTransform(ParentComp->GetComponentTransform());
				}
			}
			else
			{
				CurrentlyEditingComponent->HandRelativePlacement = CurrentTrans.GetRelativeTransform(CurrentlyEditingComponent->GetComponentTransform());
			}

			NotifyPropertyModified(CurrentlyEditingComponent, FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
			//GEditor->RedrawLevelEditingViewports(true);
			bHandled = true;

		}
		else
		{
			UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent();
			if (!CurrentlyEditingComponent || !CurrentlyEditingComponent->HandVisualizerComponent)
			{
				return false;
			}

			const FScopedTransaction Transaction(LOCTEXT("ChangingComp", "ChangingComp"));

			CurrentlyEditingComponent->Modify();
			if (AActor* Owner = CurrentlyEditingComponent->GetOwner())
			{
				Owner->Modify();
			}
			bool bLevelEdit = ViewportClient->IsLevelEditorClient();
		
			FTransform BoneTrans = CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx);
			FTransform NewTrans = BoneTrans;
			NewTrans.SetRotation(DeltaRotate.Quaternion() * NewTrans.GetRotation());

			FQuat DeltaRotateMod = NewTrans.GetRelativeTransform(BoneTrans).GetRotation();
			bool bFoundBone = false;
			for (FBPVRHandPoseBonePair& BonePair : CurrentlyEditingComponent->CustomPoseDeltas)
			{
				if (BonePair.BoneName == CurrentlySelectedBone)
				{
					bFoundBone = true;
					BonePair.DeltaPose *= DeltaRotateMod;
					break;
				}
			}

			if (!bFoundBone)
			{
				FBPVRHandPoseBonePair newBonePair;
				newBonePair.BoneName = CurrentlySelectedBone;
				newBonePair.DeltaPose *= DeltaRotateMod;
				CurrentlyEditingComponent->CustomPoseDeltas.Add(newBonePair);
				bFoundBone = true;
			}

			if (bFoundBone)
			{
				NotifyPropertyModified(CurrentlyEditingComponent, FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, CustomPoseDeltas)));
			}

			//GEditor->RedrawLevelEditingViewports(true);
			bHandled = true;
		}
	}

	return bHandled;
}

void FHandSocketVisualizer::EndEditing()
{
	HandPropertyPath = FComponentPropertyPath();
	CurrentlySelectedBone = NAME_None;
	CurrentlySelectedBoneIdx = INDEX_NONE;
	TargetViewport = nullptr;
}

#undef LOCTEXT_NAMESPACE