// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "Misc/VRLogComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(VRLogComponent)

#include "Engine/Engine.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Engine/GameViewportClient.h"
#include "GlobalRenderResources.h"

//#include "Engine/Engine.h"

/* Top of File */
#define LOCTEXT_NAMESPACE "VRLogComponent" 

  //=============================================================================
UVRLogComponent::UVRLogComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	MaxLineLength = 130;
	MaxStoredMessages = 10000;
}

//=============================================================================
UVRLogComponent::~UVRLogComponent()
{

}


void UVRLogComponent::SetConsoleText(FString Text)
{
	UConsole* ViewportConsole = (GEngine->GameViewport != nullptr) ? GEngine->GameViewport->ViewportConsole : nullptr;

	if (!ViewportConsole)
		return;

	// Using append because UpdatePrecompletedInputLine is private and append calls it
	ViewportConsole->SetInputText("");
	ViewportConsole->AppendInputText(Text);
}

void UVRLogComponent::SendKeyEventToConsole(FKey Key, EInputEvent KeyEvent)
{
	UConsole* ViewportConsole = (GEngine->GameViewport != nullptr) ? GEngine->GameViewport->ViewportConsole : nullptr;

	if (!ViewportConsole)
		return;

	ViewportConsole->FakeGotoState(FName(TEXT("Typing")));
	ViewportConsole->InputKey(IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Key, KeyEvent);
	ViewportConsole->FakeGotoState(NAME_None);
}

void UVRLogComponent::AppendTextToConsole(FString Text, bool bReturnAtEnd)
{
	UConsole* ViewportConsole = (GEngine->GameViewport != nullptr) ? GEngine->GameViewport->ViewportConsole : nullptr;

	if (!ViewportConsole)
		return;

	ViewportConsole->AppendInputText(Text);

	if (bReturnAtEnd)
	{
		ViewportConsole->FakeGotoState(FName(TEXT("Typing")));
		ViewportConsole->InputKey(IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), EKeys::Enter, EInputEvent::IE_Released);
		ViewportConsole->FakeGotoState(NAME_None);
	}

}

bool UVRLogComponent::DrawConsoleToRenderTarget2D(EBPVRConsoleDrawType DrawType, UTextureRenderTarget2D * Texture, float ScrollOffset, bool bForceDraw)
{
	if (!bForceDraw && DrawType == EBPVRConsoleDrawType::VRConsole_Draw_OutputLogOnly && !OutputLogHistory.bIsDirty)
	{
		return false;
	}
	//LastRenderedOutputLogSize 

//	check(WorldContextObject);
	UWorld* World = GetWorld();//GEngine->GetWorldFromContextObject(WorldContextObject, false);

	if (!World)
		return false;

	// Create or find the canvas object to use to render onto the texture.  Multiple canvas render target textures can share the same canvas.
	UCanvas* Canvas = World->GetCanvasForRenderingToTarget();

	if (!Canvas)
		return false;

	// Create the FCanvas which does the actual rendering.
	//const ERHIFeatureLevel::Type FeatureLevel = World != nullptr ? World->FeatureLevel : GMaxRHIFeatureLevel;
	FCanvas * RenderCanvas = new FCanvas(
		(FRenderTarget*)Texture->GameThread_GetRenderTargetResource(),
		nullptr,
		World,
		World->GetFeatureLevel(),
		// Draw immediately so that interleaved SetVectorParameter (etc) function calls work as expected
		FCanvas::CDM_ImmediateDrawing);

	Canvas->Init(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight(), nullptr, RenderCanvas);
	Canvas->Update();

	switch (DrawType)
	{
	//case EBPVRConsoleDrawType::VRConsole_Draw_ConsoleAndOutputLog: DrawConsole(true, Canvas); DrawOutputLog(true, Canvas); break;
	case EBPVRConsoleDrawType::VRConsole_Draw_ConsoleOnly: DrawConsole(false, Canvas); break;
	case EBPVRConsoleDrawType::VRConsole_Draw_OutputLogOnly: DrawOutputLog(false, Canvas, ScrollOffset); break;
	default: break;
	}

	// Clean up and flush the rendering canvas.
	Canvas->Canvas = nullptr;
	RenderCanvas->Flush_GameThread();
	delete RenderCanvas;
	RenderCanvas = nullptr;

	// It renders without this, is it actually required?
	// Enqueue the rendering command to copy the freshly rendering texture resource back to the render target RHI 
	// so that the texture is updated and available for rendering.
	/*ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER
	(
		CanvasRenderTargetResolveCommand,
		FTextureRenderTargetResource*,
		RenderTargetResource,
		Texture->GameThread_GetRenderTargetResource(),
		{
			RHICmdList.CopyToResolveTarget(RenderTargetResource->GetRenderTargetTexture(), RenderTargetResource->TextureRHI, true, FResolveParams());
		}
	);*/

	return true;
}



void UVRLogComponent::DrawConsole(bool bLowerHalf, UCanvas* Canvas)
{
	UConsole* ViewportConsole = (GEngine->GameViewport != nullptr) ? GEngine->GameViewport->ViewportConsole : nullptr;
	if (!ViewportConsole)
		return;

	ViewportConsole->PostRender_Console_Open(Canvas);

}

void UVRLogComponent::DrawOutputLog(bool bUpperHalf, UCanvas* Canvas, float ScrollOffset)
{
	UFont* Font = GEngine->GetSmallFont();// GEngine->GetTinyFont();//GEngine->GetSmallFont();

	// determine the height of the text
	float xl, yl;
	Canvas->StrLen(Font, TEXT("M"), xl, yl);
	float Height = FMath::FloorToFloat(Canvas->ClipY);// *0.75f);


	// Background
	FLinearColor BackgroundColor = FColor::Black.ReinterpretAsLinear();
	BackgroundColor.A = 1.0f;
	FCanvasTileItem ConsoleTile(FVector2D(0, 0.0f), GBlackTexture, FVector2D(Canvas->ClipX, Canvas->ClipY), FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), BackgroundColor);

	// Preserve alpha to allow single-pass composite
	ConsoleTile.BlendMode = SE_BLEND_AlphaBlend;

	Canvas->DrawItem(ConsoleTile);

	FCanvasTextItem ConsoleText(FVector2D(0, 0 + Height - 5 - yl), FText::FromString(TEXT("")), Font, FColor::Emerald);

	const TArray< TSharedPtr<FVRLogMessage> > LoggedMessages = OutputLogHistory.GetMessages();
	
	int32 ScrollPos = 0;

	if(ScrollOffset > 0 && LoggedMessages.Num() > 1)
		ScrollPos = FMath::Clamp(FMath::RoundToInt(LoggedMessages.Num() * ScrollOffset ) , 0, LoggedMessages.Num() - 1);

	float Xpos = 0.0f;
	float Ypos = 0.0f;
	for (int i = LoggedMessages.Num() - (1 + ScrollPos); i >= 0 && Ypos <= Height - yl; i--)//auto &Message : LoggedMessages)
	{
		switch (LoggedMessages[i]->Verbosity)
		{

		case ELogVerbosity::Error:
		case ELogVerbosity::Fatal: ConsoleText.SetColor(FLinearColor(0.7f,0.1f,0.1f)); break;
		case ELogVerbosity::Warning: ConsoleText.SetColor(FLinearColor(0.5f,0.5f,0.0f)); break;

		case ELogVerbosity::Log:
		default: ConsoleText.SetColor(FLinearColor(0.8f,0.8f,0.8f));
		}

		Ypos += yl;
		ConsoleText.Text = FText::Format(NSLOCTEXT("VRLogComponent", "ConsoleFormat", "{0}"), FText::FromString(*LoggedMessages[i]->Message));
		Canvas->DrawItem(ConsoleText, 0, Height - Ypos);
	}

	OutputLogHistory.bIsDirty = false;
}



#undef LOCTEXT_NAMESPACE 
/* Bottom of File */