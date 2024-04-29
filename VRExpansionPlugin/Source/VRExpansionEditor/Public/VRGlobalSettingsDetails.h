// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWindow.h"

class IDetailLayoutBuilder;

class FVRGlobalSettingsDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	FReply OnCorrectInvalidAnimationAssets();

	FReply OnFixShadowShader();
	
	void OnLockedStateUpdated(IDetailLayoutBuilder* LayoutBuilder);


	FVRGlobalSettingsDetails()
	{
	}
};

