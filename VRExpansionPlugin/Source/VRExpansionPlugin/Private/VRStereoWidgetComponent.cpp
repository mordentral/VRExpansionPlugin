// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRStereoWidgetComponent.h"
#include "VRExpansionFunctionLibrary.h"
#include "VRBaseCharacter.h"
#include "TextureResource.h"
#include "Engine/Texture.h"
#include "IStereoLayers.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineGlobals.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
//#include "Widgets/SWindow.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/KismetSystemLibrary.h"
//#include "Input/HittestGrid.h"
//#include "SceneManagement.h"
#include "DynamicMeshBuilder.h"
//#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Slate/SGameLayerManager.h"
#include "Slate/SWorldWidgetScreenLayer.h"
#include "Widgets/SViewport.h"
#include "Widgets/SViewport.h"

// CVars
namespace StereoWidgetCvars
{
	static int32 ForceNoStereoWithVRWidgets = 0;
	FAutoConsoleVariableRef CVarForceNoStereoWithVRWidgets(
		TEXT("vr.ForceNoStereoWithVRWidgets"),
		ForceNoStereoWithVRWidgets,
		TEXT("When set to 0, will render stereo layer widgets as stereo by default.\n")
		TEXT("When set to 1, will not allow stereo widget components to use stereo layers, will instead fall back to default widget rendering.\n")
		TEXT("When set to 2, will render stereo layer widgets as both stereo and in game.\n")
		TEXT("0: Default, 1: Force no stereo, 2: Render both at once"),
		ECVF_Default);
}

UVRStereoWidgetRenderComponent::UVRStereoWidgetRenderComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Widget = nullptr;
	WidgetRenderScale = 1.0f;
	WidgetRenderGamma = 1.0f;
	bUseGammaCorrection = false;
	WidgetRenderer = nullptr;
	RenderTarget = nullptr;
	bDrawAtDesiredSize = true;
	RenderTargetClearColor = FLinearColor::Black;
	bDrawWithoutStereo = false;
	DrawRate = 60.0f;
	DrawCounter = 0.0f;
	bLiveTexture = true;
}

void UVRStereoWidgetRenderComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{

	IStereoLayers* StereoLayers;
	if (!GetVisibleFlag() || (!bDrawWithoutStereo && (!GEngine->StereoRenderingDevice.IsValid() || (StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers()) == nullptr)))
	{
	}
	else
	{
		DrawCounter += DeltaTime;

		if (DrawRate > 0.0f && DrawCounter >= (1.0f / DrawRate))
		{
			if (!IsRunningDedicatedServer())
			{
				RenderWidget(DeltaTime);
			}

			if (!bLiveTexture)
			{
				MarkStereoLayerDirty();
			}

			DrawCounter = 0.0f;
		}
	}

	if (bDrawWithoutStereo)
	{
		// Skip the stereo comps setup, we are just drawing to the texture
		Super::Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	}
	else
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	}
}

void UVRStereoWidgetRenderComponent::BeginPlay()
{
	Super::BeginPlay();

	if (WidgetClass.Get() != nullptr)
	{
		InitWidget();

		IStereoLayers* StereoLayers;
		if (!GetVisibleFlag() || (!bDrawWithoutStereo && (!GEngine->StereoRenderingDevice.IsValid() || (StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers()) == nullptr)))
		{
		}
		else
		{
			// Initial render
			RenderWidget(0.0f);
		}
	}
}

void UVRStereoWidgetRenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	ReleaseResources();
}

void UVRStereoWidgetRenderComponent::ReleaseResources()
{

#if !UE_SERVER
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
#endif
	if (Widget)
	{
		Widget = nullptr;
	}

	if (SlateWidget)
	{
		SlateWidget = nullptr;
	}

	if (WidgetRenderer)
	{
		BeginCleanup(WidgetRenderer);
		WidgetRenderer = nullptr;
	}

	Texture = nullptr;

	if (SlateWindow.IsValid())
	{
		if (/*!CanReceiveHardwareInput() && */FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterVirtualWindow(SlateWindow.ToSharedRef());
		}

		SlateWindow.Reset();
	}
}

void UVRStereoWidgetRenderComponent::DestroyComponent(bool bPromoteChildren/*= false*/)
{
	Super::DestroyComponent(bPromoteChildren);

	ReleaseResources();
}

void UVRStereoWidgetRenderComponent::SetWidgetAndInit(TSubclassOf<UUserWidget> NewWidgetClass)
{
	WidgetClass = NewWidgetClass;
	InitWidget();
}

void UVRStereoWidgetRenderComponent::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	// If the InLevel is null, it's a signal that the entire world is about to disappear, so
	// go ahead and remove this widget from the viewport, it could be holding onto too many
	// dangerous actor references that won't carry over into the next world.
	if (InLevel == nullptr && InWorld == GetWorld())
	{
		ReleaseResources();
	}
}

void UVRStereoWidgetRenderComponent::InitWidget()
{
	if (IsTemplate())
		return;

#if !UE_SERVER
	FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &ThisClass::OnLevelRemovedFromWorld);
#endif
	if (IsRunningDedicatedServer())
		return;

	if (Widget && Widget->GetClass() == WidgetClass)
		return;

	if (Widget != nullptr)
	{
		Widget->MarkPendingKill();
		Widget = nullptr;
	}

	if (SlateWidget)
	{
		SlateWidget = nullptr;
	}

	// Don't do any work if Slate is not initialized
	if (FSlateApplication::IsInitialized())
	{
		UWorld* World = GetWorld();

		if (WidgetClass && Widget == nullptr && World && !World->bIsTearingDown)
		{
			Widget = CreateWidget(GetWorld(), WidgetClass);
			Widget->SetRenderScale(FVector2D(1.0f, 1.0f));
		}

#if WITH_EDITOR
		if (Widget && !World->IsGameWorld())// && !bEditTimeUsable)
		{
			if (!GEnableVREditorHacks)
			{
				// Prevent native ticking of editor component previews
				Widget->SetDesignerFlags(EWidgetDesignFlags::Designing);
			}
		}
#endif

		if (Widget)
		{
			SlateWidget = Widget->TakeWidget();
		}

		// Create the SlateWindow if it doesn't exists
		if (!SlateWindow.IsValid())
		{
			FVector2D DrawSize = this->GetQuadSize();
			SlateWindow = SNew(SVirtualWindow).Size(DrawSize);
			SlateWindow->SetIsFocusable(false);
			SlateWindow->SetVisibility(EVisibility::Visible);
			SlateWindow->SetContentScale(FVector2D(1.0f, 1.0f));

			if (Widget && !Widget->IsDesignTime())
			{
				if (UWorld* LocalWorld = GetWorld())
				{
					if (LocalWorld->IsGameWorld())
					{
						UGameInstance* GameInstance = LocalWorld->GetGameInstance();
						check(GameInstance);

						UGameViewportClient* GameViewportClient = GameInstance->GetGameViewportClient();
						if (GameViewportClient)
						{
							SlateWindow->AssignParentWidget(GameViewportClient->GetGameViewportWidget());
						}
					}
				}
			}

		}

		if (SlateWindow)
		{
			TSharedRef<SWidget> MyWidget = SlateWidget ? SlateWidget.ToSharedRef() : Widget->TakeWidget();
			SlateWindow->SetContent(MyWidget);
		}
	}
}

void UVRStereoWidgetRenderComponent::RenderWidget(float DeltaTime)
{
	if (!Widget)
		return;

	if (WidgetRenderer == nullptr)
	{
		WidgetRenderer = new FWidgetRenderer(bUseGammaCorrection);
		check(WidgetRenderer);
	}

	FVector2D DrawSize = this->GetQuadSize();
	FVector2D TextureSize = DrawSize;

	const int32 MaxAllowedDrawSize = GetMax2DTextureDimension();
	if (DrawSize.X <= 0 || DrawSize.Y <= 0 || DrawSize.X > MaxAllowedDrawSize || DrawSize.Y > MaxAllowedDrawSize)
	{
		return;
	}

	TSharedRef<SWidget> MyWidget = SlateWidget ? SlateWidget.ToSharedRef() : Widget->TakeWidget();

	if (bDrawAtDesiredSize)
	{
		SlateWindow->SlatePrepass(WidgetRenderScale);

		FVector2D DesiredSize = SlateWindow->GetDesiredSize();
		DesiredSize.X = FMath::RoundToInt(DesiredSize.X);
		DesiredSize.Y = FMath::RoundToInt(DesiredSize.Y);

		if (!DesiredSize.IsNearlyZero())
		{
			TextureSize = DesiredSize;// .IntPoint();

			WidgetRenderer->SetIsPrepassNeeded(false);

			if (SlateWindow->GetSizeInScreen() != DesiredSize)
			{
				SlateWindow->Resize(TextureSize);
			}
		}
		else
		{
			WidgetRenderer->SetIsPrepassNeeded(true);
		}
	}
	else
	{
		WidgetRenderer->SetIsPrepassNeeded(true);
	}

	if (RenderTarget == nullptr)
	{
		const EPixelFormat requestedFormat = FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();
		RenderTarget = NewObject<UTextureRenderTarget2D>();
		check(RenderTarget);
		RenderTarget->AddToRoot();
		RenderTarget->ClearColor = RenderTargetClearColor;
		RenderTarget->TargetGamma = WidgetRenderGamma;
		RenderTarget->InitCustomFormat(TextureSize.X, TextureSize.Y, requestedFormat /*PF_B8G8R8A8*/, false);
		MarkStereoLayerDirty();
	}
	else if (RenderTarget->Resource->GetSizeX() != TextureSize.X || RenderTarget->Resource->GetSizeY() != TextureSize.Y)
	{
		const EPixelFormat requestedFormat = FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();
		RenderTarget->InitCustomFormat(TextureSize.X, TextureSize.Y, requestedFormat /*PF_B8G8R8A8*/, false);
		RenderTarget->UpdateResourceImmediate();
		MarkStereoLayerDirty();
	}

	WidgetRenderer->DrawWidget(RenderTarget, MyWidget, WidgetRenderScale, TextureSize, DeltaTime);//DeltaTime);

	if (Texture != RenderTarget)
	{
		Texture = RenderTarget;
	}
}

  //=============================================================================
UVRStereoWidgetComponent::UVRStereoWidgetComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
//	, bLiveTexture(false)
	, bSupportsDepth(false)
	, bNoAlphaChannel(false)
	//, Texture(nullptr)
	//, LeftTexture(nullptr)
	, bQuadPreserveTextureRatio(false)
	//, StereoLayerQuadSize(FVector2D(500.0f, 500.0f))
	, UVRect(FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f)))
	//, CylinderRadius(100)
	//, CylinderOverlayArc(100)EWidgetGeometryMode
	//, CylinderHeight(50)
	//, StereoLayerType(SLT_TrackerLocked)
	//, StereoLayerShape(SLSH_QuadLayer)
	, Priority(0)
	, bIsDirty(true)
	, bTextureNeedsUpdate(false)
	, LayerId(0)
	, LastTransform(FTransform::Identity)
	, bLastVisible(false)
{
	bShouldCreateProxy = true;
	bUseEpicsWorldLockedStereo = false;
	// Replace quad size with DrawSize instead
	//StereoLayerQuadSize = DrawSize;

	PrimaryComponentTick.TickGroup = TG_DuringPhysics;

	bIsDirty = true;
	bDirtyRenderTarget = false;
	bRenderBothStereoAndWorld = false;
	bDrawWithoutStereo = false;
	bDelayForRenderThread = false;
	bIsSleeping = false;
	//Texture = nullptr;
}

//=============================================================================
UVRStereoWidgetComponent::~UVRStereoWidgetComponent()
{
}

void UVRStereoWidgetComponent::BeginDestroy()
{
	IStereoLayers* StereoLayers;
	if (LayerId && GEngine->StereoRenderingDevice.IsValid() && (StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers()) != nullptr)
	{
		StereoLayers->DestroyLayer(LayerId);
		LayerId = 0;
	}

	Super::BeginDestroy();
}


void UVRStereoWidgetComponent::OnUnregister()
{
	IStereoLayers* StereoLayers;
	if (LayerId && GEngine->StereoRenderingDevice.IsValid() && (StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers()) != nullptr)
	{
		StereoLayers->DestroyLayer(LayerId);
		LayerId = 0;
	}

	Super::OnUnregister();
}

void UVRStereoWidgetComponent::DrawWidgetToRenderTarget(float DeltaTime)
{
	Super::DrawWidgetToRenderTarget(DeltaTime);

	bDirtyRenderTarget = true;
}

void UVRStereoWidgetComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{

	// Precaching what the widget uses for draw time here as it gets modified in the super tick
	bool bWidgetDrew = ShouldDrawWidget();

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);


	//bool bIsCurVis = IsWidgetVisible();

	bool bIsVisible = IsVisible() && IsWidgetVisible() && !bIsSleeping;// && ((GetWorld()->TimeSince(GetLastRenderTime()) <= 0.5f));

	// If we are set to not use stereo layers or we don't have a valid stereo layer device
	if (
		StereoWidgetCvars::ForceNoStereoWithVRWidgets == 1 ||
		bDrawWithoutStereo ||
		!UVRExpansionFunctionLibrary::IsInVREditorPreviewOrGame() || 
		!GEngine->StereoRenderingDevice.IsValid() || 
		(GEngine->StereoRenderingDevice->GetStereoLayers() == nullptr)
		)
	{
		if (!bShouldCreateProxy)
		{
			bShouldCreateProxy = true;
			//MarkRenderStateDirty(); // Recreate

			if (LayerId)
			{
				if (GEngine->StereoRenderingDevice.IsValid())
				{
					IStereoLayers* StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers();
					if (StereoLayers)
						StereoLayers->DestroyLayer(LayerId);
				}
				LayerId = 0;
			}
		}

		return;
	}
	else if (bRenderBothStereoAndWorld || StereoWidgetCvars::ForceNoStereoWithVRWidgets == 2) // Forcing both modes at once
	{
		if (!bShouldCreateProxy)
		{
			bShouldCreateProxy = true;
			MarkRenderStateDirty(); // Recreate
		}
	}
	else // Stereo only
	{
		if (bShouldCreateProxy)
		{
			bShouldCreateProxy = false;
			MarkRenderStateDirty(); // Recreate
		}
	}

#if !UE_SERVER

	// Same check that the widget runs prior to ticking
	if (IsRunningDedicatedServer() || !GetSlateWindow() || GetSlateWindow()->GetContent() == SNullWidget::NullWidget)
	{
		return;
	}

	IStereoLayers* StereoLayers;
	if (!UVRExpansionFunctionLibrary::IsInVREditorPreviewOrGame() || !GEngine->StereoRenderingDevice.IsValid() || !RenderTarget)
	{
		return;
	}

	StereoLayers = GEngine->StereoRenderingDevice->GetStereoLayers();

	if (StereoLayers == nullptr)
		return;

	FTransform Transform = LastTransform;
	// Never true until epic fixes back end code
	if (false)//StereoLayerType == SLT_WorldLocked)
	{
		Transform = GetComponentTransform();
	}
	else if (Space == EWidgetSpace::Screen)
	{
		Transform = GetRelativeTransform();
	}
	else if(bIsVisible) // World locked here now
	{

		if (bUseEpicsWorldLockedStereo)
		{
			// Its incorrect......even in 4.17
			Transform = FTransform(FRotator(0.f,-180.f, 0.f)) * GetComponentTransform();
			//Transform.ConcatenateRotation(FRotator(0.0f, -180.0f, 0.0f).Quaternion());
		}
		else
		{
			// Fix this when stereo world locked works again
			// Thanks to mitch for the temp work around idea

			APlayerController* PC = nullptr;
			if (UWorld * CurWorld = GetWorld())
			{
				const ULocalPlayer* FirstPlayer = GEngine->GetFirstGamePlayer(CurWorld);
				PC = FirstPlayer ? FirstPlayer->GetPlayerController(CurWorld) : nullptr;
			}

			if (PC)
			{
				APawn * mpawn = PC->GetPawnOrSpectator();
				//bTextureNeedsUpdate = true;
				if (mpawn)
				{

					// Offset the transform by the widget pivot.
					float DeltaY = (Pivot.X - 0.5f) * DrawSize.X;
					float DeltaZ = (Pivot.Y - 0.5f) * DrawSize.Y;
					FTransform OffsetTransform = FTransform(FVector(0.f, DeltaY, DeltaZ));
					OffsetTransform = OffsetTransform * GetComponentTransform();

					// Set transform to this relative transform

					bool bHandledTransform = false;
					if (AVRBaseCharacter* BaseVRChar = Cast<AVRBaseCharacter>(mpawn))
					{
						if (USceneComponent* CameraParent = BaseVRChar->VRReplicatedCamera->GetAttachParent())
						{
							Transform = OffsetTransform.GetRelativeTransform(CameraParent->GetComponentTransform());
							Transform = FTransform(FRotator(0.f, -180.f, 0.f)) * Transform;
							bHandledTransform = true;
						}
					}
					else if (UCameraComponent* Camera = mpawn->FindComponentByClass<UCameraComponent>())
					{
						// Look for generic camera comp and use its attach parent
						if (USceneComponent* CameraParent = Camera->GetAttachParent())
						{
							Transform = OffsetTransform.GetRelativeTransform(CameraParent->GetComponentTransform());
							Transform = FTransform(FRotator(0.f, -180.f, 0.f)) * Transform;
							bHandledTransform = true;							
						}
					}

					if(!bHandledTransform) // Just use the pawn as we don't know the heirarchy
					{
						Transform = OffsetTransform.GetRelativeTransform(mpawn->GetTransform());
						Transform = FTransform(FRotator(0.f, -180.f, 0.f)) * Transform;
					}
					
					// OpenVR y+ Up, +x Right, -z Going away
					// UE4 z+ up, +y right, +x forward

					//Transform.ConcatenateRotation(FRotator(0.0f, -180.0f, 0.0f).Quaternion());
					// I might need to inverse X axis here to get it facing the correct way, we'll see
					
					//Transform = mpawn->GetActorTransform().GetRelativeTransform(GetComponentTransform());
				}
			}
			else
			{
				// No PC, destroy the layer and enable drawing it normally.
				bShouldCreateProxy = true;

				if (LayerId)
				{
					StereoLayers->DestroyLayer(LayerId);
					LayerId = 0;
				}
				return;
			}
			//
			//Transform = GetRelativeTransform();
		}
	}

	// If the transform changed dirty the layer and push the new transform
	
	if (!bIsDirty)
	{
		if (bLastVisible != bIsVisible)
		{
			bIsDirty = true;
		}
		else if (bDirtyRenderTarget || FMemory::Memcmp(&LastTransform, &Transform, sizeof(Transform)) != 0)
		{
			bIsDirty = true;
		}
	}

	bool bCurrVisible = bIsVisible;
	if (!RenderTarget || !RenderTarget->Resource)
	{
		bCurrVisible = false;
	}

	if (bIsDirty)
	{
		// OpenXR doesn't take the transforms scale component into account for the stereo layer, so we need to scale the buffer instead
		bool bScaleBuffer = false;
		static FName SystemName(TEXT("OpenXR"));
		if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
		{
			bScaleBuffer = true;
		}

		IStereoLayers::FLayerDesc LayerDsec;
		LayerDsec.Priority = Priority;
		LayerDsec.QuadSize = FVector2D(DrawSize);
		LayerDsec.UVRect = UVRect;

		if (bDelayForRenderThread && !LastTransform.Equals(FTransform::Identity))
		{
			LayerDsec.Transform = LastTransform;
			if (bScaleBuffer)
			{
				LayerDsec.QuadSize = FVector2D(DrawSize) * FVector2D(LastTransform.GetScale3D());
			}
		}
		else
		{
			LayerDsec.Transform = Transform;
			if (bScaleBuffer)
			{
				LayerDsec.QuadSize = FVector2D(DrawSize) * FVector2D(Transform.GetScale3D());
			}
		}

		if (RenderTarget)
		{
			LayerDsec.Texture = RenderTarget->Resource->TextureRHI;
			LayerDsec.Flags |= (RenderTarget->GetMaterialType() == MCT_TextureExternal) ? IStereoLayers::LAYER_FLAG_TEX_EXTERNAL : 0;
		}
		// Forget the left texture implementation
		//if (LeftTexture)
		//{
		//	LayerDsec.LeftTexture = LeftTexture->Resource->TextureRHI;
		//}

		LayerDsec.Flags |= IStereoLayers::LAYER_FLAG_TEX_CONTINUOUS_UPDATE;// (/*bLiveTexture*/true) ? IStereoLayers::LAYER_FLAG_TEX_CONTINUOUS_UPDATE : 0;
		LayerDsec.Flags |= (bNoAlphaChannel) ? IStereoLayers::LAYER_FLAG_TEX_NO_ALPHA_CHANNEL : 0;
		LayerDsec.Flags |= (bQuadPreserveTextureRatio) ? IStereoLayers::LAYER_FLAG_QUAD_PRESERVE_TEX_RATIO : 0;
		LayerDsec.Flags |= (bSupportsDepth) ? IStereoLayers::LAYER_FLAG_SUPPORT_DEPTH : 0;
		LayerDsec.Flags |= (!bCurrVisible) ? IStereoLayers::LAYER_FLAG_HIDDEN : 0;

		// Fix this later when WorldLocked is no longer wrong.
		switch (Space)
		{
		case EWidgetSpace::World:
		{
			if(bUseEpicsWorldLockedStereo)
				LayerDsec.PositionType = IStereoLayers::WorldLocked;
			else
				LayerDsec.PositionType = IStereoLayers::TrackerLocked;

			//LayerDsec.Flags |= IStereoLayers::LAYER_FLAG_SUPPORT_DEPTH;
		}break;

		case EWidgetSpace::Screen:
		default:
		{
			LayerDsec.PositionType = IStereoLayers::FaceLocked;
		}break;
		}

		switch (GeometryMode)
		{
		case EWidgetGeometryMode::Cylinder:
		{
			UStereoLayerShapeCylinder* Cylinder = Cast<UStereoLayerShapeCylinder>(Shape);

			if (!Cylinder)
			{
				if (Shape)
				{
					Shape->MarkPendingKill();
				}

				Cylinder = NewObject<UStereoLayerShapeCylinder>(this, NAME_None, RF_Public);
				Shape = Cylinder;
			}

			if (Cylinder)
			{
				const float ArcAngleRadians = FMath::DegreesToRadians(CylinderArcAngle);
				const float Radius = GetDrawSize().X / ArcAngleRadians;

				Cylinder->Height = GetDrawSize().Y;//CylinderHeight_DEPRECATED;
				Cylinder->OverlayArc = CylinderArcAngle;// CylinderOverlayArc_DEPRECATED;
				Cylinder->Radius = Radius;// CylinderRadius_DEPRECATED;
			}
			break;

			//LayerDsec.ShapeType = IStereoLayers::CylinderLayer;

		}break;
		case EWidgetGeometryMode::Plane:
		default:
		{
			UStereoLayerShapeQuad* Quad = Cast<UStereoLayerShapeQuad>(Shape);

			if (!Quad)
			{
				if (Shape)
				{
					Shape->MarkPendingKill();
				}
				Shape = NewObject<UStereoLayerShapeQuad>(this, NAME_None, RF_Public);
			}
			//LayerDsec.ShapeType = IStereoLayers::QuadLayer;
		}break;
		}

		if(Shape)
			Shape->ApplyShape(LayerDsec);

		if (LayerId)
		{
			StereoLayers->SetLayerDesc(LayerId, LayerDsec);
		}
		else
		{
			LayerId = StereoLayers->CreateLayer(LayerDsec);
		}

	}

	LastTransform = Transform;
	bLastVisible = bCurrVisible;
	bIsDirty = false;
	bDirtyRenderTarget = false;
#endif
}


void UVRStereoWidgetComponent::SetPriority(int32 InPriority)
{
	if (Priority == InPriority)
	{
		return;
	}

	Priority = InPriority;
	bIsDirty = true;
}

void UVRStereoWidgetComponent::UpdateRenderTarget(FIntPoint DesiredRenderTargetSize)
{
	Super::UpdateRenderTarget(DesiredRenderTargetSize);
}

/** Represents a billboard sprite to the scene manager. */
class FStereoWidget3DSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}
	/** Initialization constructor. */
	FStereoWidget3DSceneProxy(UVRStereoWidgetComponent* InComponent, ISlate3DRenderer& InRenderer)
		: FPrimitiveSceneProxy(InComponent)
		, Pivot(InComponent->GetPivot())
		, Renderer(InRenderer)
		, RenderTarget(InComponent->GetRenderTarget())
		, MaterialInstance(InComponent->GetMaterialInstance())
		, BodySetup(InComponent->GetBodySetup())
		, BlendMode(InComponent->GetBlendMode())
		, GeometryMode(InComponent->GetGeometryMode())
		, ArcAngle(FMath::DegreesToRadians(InComponent->GetCylinderArcAngle()))
	{
		bWillEverBeLit = false;
		bCreateSceneProxy = InComponent->bShouldCreateProxy;
		MaterialRelevance = MaterialInstance->GetRelevance(GetScene().GetFeatureLevel());
	}

	// FPrimitiveSceneProxy interface.
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		if(!bCreateSceneProxy)
			return;

#if WITH_EDITOR
		const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

		auto WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
			FLinearColor(0, 0.5f, 1.f)	
		);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);

		FMaterialRenderProxy* ParentMaterialProxy = nullptr;
		if (bWireframe)
		{
			ParentMaterialProxy = WireframeMaterialInstance;
		}
		else
		{
			ParentMaterialProxy = MaterialInstance->GetRenderProxy();
		}
#else
		FMaterialRenderProxy* ParentMaterialProxy = MaterialInstance->GetRenderProxy();
#endif

		//FSpriteTextureOverrideRenderProxy* TextureOverrideMaterialProxy = new FSpriteTextureOverrideRenderProxy(ParentMaterialProxy,

		const FMatrix& ViewportLocalToWorld = GetLocalToWorld();

		FMatrix PreviousLocalToWorld;

		if (!GetScene().GetPreviousLocalToWorld(GetPrimitiveSceneInfo(), PreviousLocalToWorld))
		{
			PreviousLocalToWorld = GetLocalToWorld();
		}

		if (RenderTarget)//false)//RenderTarget)
		{
			FTextureResource* TextureResource = RenderTarget->Resource;
			if (TextureResource)
			{
				if (GeometryMode == EWidgetGeometryMode::Plane)
				{
					float U = -RenderTarget->SizeX * Pivot.X;
					float V = -RenderTarget->SizeY * Pivot.Y;
					float UL = RenderTarget->SizeX * (1.0f - Pivot.X);
					float VL = RenderTarget->SizeY * (1.0f - Pivot.Y);

					int32 VertexIndices[4];

					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						FDynamicMeshBuilder MeshBuilder(Views[ViewIndex]->GetFeatureLevel());

						if (VisibilityMap & (1 << ViewIndex))
						{
							VertexIndices[0] = MeshBuilder.AddVertex(-FVector(0, U, V), FVector2D(0, 0), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);
							VertexIndices[1] = MeshBuilder.AddVertex(-FVector(0, U, VL), FVector2D(0, 1), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);
							VertexIndices[2] = MeshBuilder.AddVertex(-FVector(0, UL, VL), FVector2D(1, 1), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);
							VertexIndices[3] = MeshBuilder.AddVertex(-FVector(0, UL, V), FVector2D(1, 0), FVector(0, -1, 0), FVector(0, 0, -1), FVector(1, 0, 0), FColor::White);

							MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[1], VertexIndices[2]);
							MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[2], VertexIndices[3]);

							FDynamicMeshBuilderSettings Settings;
							Settings.bDisableBackfaceCulling = false;
							Settings.bReceivesDecals = true;
							Settings.bUseSelectionOutline = true;
							MeshBuilder.GetMesh(ViewportLocalToWorld, PreviousLocalToWorld, ParentMaterialProxy, SDPG_World, Settings, nullptr, ViewIndex, Collector, FHitProxyId());
						}
					}
				}
				else
				{
					ensure(GeometryMode == EWidgetGeometryMode::Cylinder);

					const int32 NumSegments = FMath::Lerp(4, 32, ArcAngle / PI);


					const float Radius = RenderTarget->SizeX / ArcAngle;
					const float Apothem = Radius * FMath::Cos(0.5f*ArcAngle);
					const float ChordLength = 2.0f * Radius * FMath::Sin(0.5f*ArcAngle);

					const float PivotOffsetX = ChordLength * (0.5 - Pivot.X);
					const float V = -RenderTarget->SizeY * Pivot.Y;
					const float VL = RenderTarget->SizeY * (1.0f - Pivot.Y);

					int32 VertexIndices[4];

					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						FDynamicMeshBuilder MeshBuilder(Views[ViewIndex]->GetFeatureLevel());

						if (VisibilityMap & (1 << ViewIndex))
						{
							const float RadiansPerStep = ArcAngle / NumSegments;

							FVector LastTangentX;
							FVector LastTangentY;
							FVector LastTangentZ;

							for (int32 Segment = 0; Segment < NumSegments; Segment++)
							{
								const float Angle = -ArcAngle / 2 + Segment * RadiansPerStep;
								const float NextAngle = Angle + RadiansPerStep;

								// Polar to Cartesian
								const float X0 = Radius * FMath::Cos(Angle) - Apothem;
								const float Y0 = Radius * FMath::Sin(Angle);
								const float X1 = Radius * FMath::Cos(NextAngle) - Apothem;
								const float Y1 = Radius * FMath::Sin(NextAngle);

								const float U0 = static_cast<float>(Segment) / NumSegments;
								const float U1 = static_cast<float>(Segment + 1) / NumSegments;

								const FVector Vertex0 = -FVector(X0, PivotOffsetX + Y0, V);
								const FVector Vertex1 = -FVector(X0, PivotOffsetX + Y0, VL);
								const FVector Vertex2 = -FVector(X1, PivotOffsetX + Y1, VL);
								const FVector Vertex3 = -FVector(X1, PivotOffsetX + Y1, V);

								FVector TangentX = Vertex3 - Vertex0;
								TangentX.Normalize();
								FVector TangentY = Vertex1 - Vertex0;
								TangentY.Normalize();
								FVector TangentZ = FVector::CrossProduct(TangentX, TangentY);

								if (Segment == 0)
								{
									LastTangentX = TangentX;
									LastTangentY = TangentY;
									LastTangentZ = TangentZ;
								}

								VertexIndices[0] = MeshBuilder.AddVertex(Vertex0, FVector2D(U0, 0), LastTangentX, LastTangentY, LastTangentZ, FColor::White);
								VertexIndices[1] = MeshBuilder.AddVertex(Vertex1, FVector2D(U0, 1), LastTangentX, LastTangentY, LastTangentZ, FColor::White);
								VertexIndices[2] = MeshBuilder.AddVertex(Vertex2, FVector2D(U1, 1), TangentX, TangentY, TangentZ, FColor::White);
								VertexIndices[3] = MeshBuilder.AddVertex(Vertex3, FVector2D(U1, 0), TangentX, TangentY, TangentZ, FColor::White);

								MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[1], VertexIndices[2]);
								MeshBuilder.AddTriangle(VertexIndices[0], VertexIndices[2], VertexIndices[3]);

								LastTangentX = TangentX;
								LastTangentY = TangentY;
								LastTangentZ = TangentZ;
							}

							FDynamicMeshBuilderSettings Settings;
							Settings.bDisableBackfaceCulling = false;
							Settings.bReceivesDecals = true;
							Settings.bUseSelectionOutline = true;
							MeshBuilder.GetMesh(ViewportLocalToWorld, PreviousLocalToWorld, ParentMaterialProxy, SDPG_World, Settings, nullptr, ViewIndex, Collector, FHitProxyId());
						}
					}
				}
			}
		}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				RenderCollision(BodySetup, Collector, ViewIndex, ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
#endif
	}

	void RenderCollision(UBodySetup* InBodySetup, FMeshElementCollector& Collector, int32 ViewIndex, const FEngineShowFlags& EngineShowFlags, const FBoxSphereBounds& InBounds, bool bRenderInEditor) const
	{
		if (InBodySetup)
		{
			bool bDrawCollision = EngineShowFlags.Collision && IsCollisionEnabled();

			if (bDrawCollision && AllowDebugViewmodes())
			{
				// Draw simple collision as wireframe if 'show collision', collision is enabled, and we are not using the complex as the simple
				const bool bDrawSimpleWireframeCollision = InBodySetup->CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple;

				if (FMath::Abs(GetLocalToWorld().Determinant()) < SMALL_NUMBER)
				{
					// Catch this here or otherwise GeomTransform below will assert
					// This spams so commented out
					//UE_LOG(LogStaticMesh, Log, TEXT("Zero scaling not supported (%s)"), *StaticMesh->GetPathName());
				}
				else
				{
					const bool bDrawSolid = !bDrawSimpleWireframeCollision;
					const bool bProxyIsSelected = IsSelected();

					if (bDrawSolid)
					{
						// Make a material for drawing solid collision stuff
						auto SolidMaterialInstance = new FColoredMaterialRenderProxy(
							GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(),
							GetWireframeColor()
						);

						Collector.RegisterOneFrameMaterialProxy(SolidMaterialInstance);

						FTransform GeomTransform(GetLocalToWorld());
						InBodySetup->AggGeom.GetAggGeom(GeomTransform, GetWireframeColor().ToFColor(true), SolidMaterialInstance, false, true, DrawsVelocity(), ViewIndex, Collector);
					}
					// wireframe
					else
					{
						FColor CollisionColor = FColor(157, 149, 223, 255);
						FTransform GeomTransform(GetLocalToWorld());
						InBodySetup->AggGeom.GetAggGeom(GeomTransform, GetSelectionColor(CollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true), nullptr, false, false, DrawsVelocity(), ViewIndex, Collector);
					}
				}
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		bool bVisible = true;

		FPrimitiveViewRelevance Result;

		MaterialRelevance.SetPrimitiveViewRelevance(Result);

		Result.bDrawRelevance = IsShown(View) && bVisible && View->Family->EngineShowFlags.WidgetComponents;
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = IsShadowCast(View);
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		Result.bEditorPrimitiveRelevance = false;
		Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;

		return Result;
	}

	virtual void GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const override
	{
		bDynamic = false;
		bRelevant = false;
		bLightMapped = false;
		bShadowMapped = false;
	}

	virtual void OnTransformChanged() override
	{
		Origin = GetLocalToWorld().GetOrigin();
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }

	uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }

private:
	FVector Origin;
	FVector2D Pivot;
	ISlate3DRenderer& Renderer;
	UTextureRenderTarget2D* RenderTarget;
	UMaterialInstanceDynamic* MaterialInstance;
	FMaterialRelevance MaterialRelevance;
	UBodySetup* BodySetup;
	EWidgetBlendMode BlendMode;
	EWidgetGeometryMode GeometryMode;
	float ArcAngle;
	bool bCreateSceneProxy;
};


FPrimitiveSceneProxy* UVRStereoWidgetComponent::CreateSceneProxy()
{
	if (Space == EWidgetSpace::Screen)
	{
		return nullptr;
	}

	if (WidgetRenderer && GetSlateWindow() && GetSlateWindow()->GetContent() != SNullWidget::NullWidget)
	{
		RequestRedraw();
		LastWidgetRenderTime = 0;

		return new FStereoWidget3DSceneProxy(this, *WidgetRenderer->GetSlateRenderer());
	}

	return nullptr;
}

class FVRStereoWidgetComponentInstanceData : public FActorComponentInstanceData
{
public:
	FVRStereoWidgetComponentInstanceData(const UVRStereoWidgetComponent* SourceComponent)
		: FActorComponentInstanceData(SourceComponent)
		, RenderTarget(SourceComponent->GetRenderTarget())
	{}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		FActorComponentInstanceData::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<UVRStereoWidgetComponent>(Component)->ApplyVRComponentInstanceData(this);
	}

	/*virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		FActorComponentInstanceData::AddReferencedObjects(Collector);

		UClass* WidgetUClass = *WidgetClass;
		Collector.AddReferencedObject(WidgetUClass);
		Collector.AddReferencedObject(RenderTarget);
	}*/

public:
	UTextureRenderTarget2D* RenderTarget;
};

TStructOnScope<FActorComponentInstanceData> UVRStereoWidgetComponent::GetComponentInstanceData() const
{
	return MakeStructOnScope<FActorComponentInstanceData, FVRStereoWidgetComponentInstanceData>(this);
}

void UVRStereoWidgetComponent::ApplyVRComponentInstanceData(FVRStereoWidgetComponentInstanceData* WidgetInstanceData)
{
	check(WidgetInstanceData);

	// Note: ApplyComponentInstanceData is called while the component is registered so the rendering thread is already using this component
	// That means all component state that is modified here must be mirrored on the scene proxy, which will be recreated to receive the changes later due to MarkRenderStateDirty.

	if (GetWidgetClass() != WidgetClass)
	{
		return;
	}

	RenderTarget = WidgetInstanceData->RenderTarget;

	// Also set the texture
	//Texture = RenderTarget;
	// Not needed anymore, just using the render target directly now

	if (MaterialInstance && RenderTarget)
	{
		MaterialInstance->SetTextureParameterValue("SlateUI", RenderTarget);
	}

	MarkRenderStateDirty();
}