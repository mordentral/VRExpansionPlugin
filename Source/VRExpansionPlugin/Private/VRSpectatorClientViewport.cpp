// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "VRExpansionPluginPrivatePCH.h"
#include "VRSpectatorClientViewport.h"

UVRSpectatorClientViewport::UVRSpectatorClientViewport(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Add an additional player entry at the end of every list, this is for the VR pawn and will be set to NULL
	// VR Pawns must be the last entry in the player data now to correctly work. So make them last
	SplitscreenInfo[ESplitScreenType::None].PlayerData.Add(FPerPlayerSplitscreenData(0.0f, 0.0f, 0.0f, 0.0f));
	SplitscreenInfo[ESplitScreenType::TwoPlayer_Vertical].PlayerData.Add(FPerPlayerSplitscreenData(0.0f, 0.0f, 0.0f, 0.0f));
	SplitscreenInfo[ESplitScreenType::TwoPlayer_Horizontal].PlayerData.Add(FPerPlayerSplitscreenData(0.0f, 0.0f, 0.0f, 0.0f));
	SplitscreenInfo[ESplitScreenType::ThreePlayer_FavorTop].PlayerData.Add(FPerPlayerSplitscreenData(0.0f, 0.0f, 0.0f, 0.0f));
	SplitscreenInfo[ESplitScreenType::ThreePlayer_FavorBottom].PlayerData.Add(FPerPlayerSplitscreenData(0.0f, 0.0f, 0.0f, 0.0f));
	SplitscreenInfo[ESplitScreenType::FourPlayer].PlayerData.Add(FPerPlayerSplitscreenData(0.0f, 0.0f, 0.0f, 0.0f));

	// Up max count by 1
	MaxSplitscreenPlayers = 5;
}

void UVRSpectatorClientViewport::UpdateActiveSplitscreenType()
{
	ESplitScreenType::Type SplitType = ESplitScreenType::None;
	const int32 NumPlayers = GEngine->GetNumGamePlayers(GetWorld());
	const UGameMapsSettings* Settings = GetDefault<UGameMapsSettings>();

		//                               This is private....
	if (Settings->bUseSplitscreen /*&& !bDisableSplitScreenOverride*/)
	{
		switch (NumPlayers)
		{
		case 0:
		case 1:
		case 2:
			SplitType = ESplitScreenType::None;
			break;

		case 3:
			switch (Settings->TwoPlayerSplitscreenLayout)
			{
			case ETwoPlayerSplitScreenType::Horizontal:
				SplitType = ESplitScreenType::TwoPlayer_Horizontal;
				break;

			case ETwoPlayerSplitScreenType::Vertical:
				SplitType = ESplitScreenType::TwoPlayer_Vertical;
				break;

			default:
				check(0);
			}
			break;

		case 4:
			switch (Settings->ThreePlayerSplitscreenLayout)
			{
			case EThreePlayerSplitScreenType::FavorTop:
				SplitType = ESplitScreenType::ThreePlayer_FavorTop;
				break;

			case EThreePlayerSplitScreenType::FavorBottom:
				SplitType = ESplitScreenType::ThreePlayer_FavorBottom;
				break;

			default:
				check(0);
			}
			break;

		default:
			ensure(NumPlayers == 5);
			SplitType = ESplitScreenType::FourPlayer;
			break;
		}
	}
	else
	{
		SplitType = ESplitScreenType::None;
	}

	ActiveSplitscreenType = SplitType;
}