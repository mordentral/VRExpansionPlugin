// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionEditor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Grippables/HandSocketComponent.h"
#include "PropertyEditorModule.h"
#include "HandSocketVisualizer.h"
#include "HandSocketComponentDetails.h"
#include "VRGlobalSettingsDetails.h"
#include "VRGlobalSettings.h"


IMPLEMENT_MODULE(FVRExpansionEditorModule, VRExpansionEditor);

void FVRExpansionEditorModule::StartupModule()
{
	RegisterComponentVisualizer(UHandSocketComponent::StaticClass()->GetFName(), MakeShareable(new FHandSocketVisualizer));

	// Register detail customizations
	{
		auto& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >("PropertyEditor");

		// Register our customization to be used by a class 'UMyClass' or 'AMyClass'. Note the prefix must be dropped.
		PropertyModule.RegisterCustomClassLayout(
			UHandSocketComponent::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FHandSocketComponentDetails::MakeInstance)
		);

		PropertyModule.RegisterCustomClassLayout(
			UVRGlobalSettings::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FVRGlobalSettingsDetails::MakeInstance)
		);

		PropertyModule.NotifyCustomizationModuleChanged();
	}

}

void FVRExpansionEditorModule::ShutdownModule()
{
	if (GUnrealEd != NULL)
	{
		// Iterate over all class names we registered for
		for (FName ClassName : RegisteredComponentClassNames)
		{
			GUnrealEd->UnregisterComponentVisualizer(ClassName);
		}
	}

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		auto& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.UnregisterCustomClassLayout(UHandSocketComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UVRGlobalSettings::StaticClass()->GetFName());
	}
}

void FVRExpansionEditorModule::RegisterComponentVisualizer(FName ComponentClassName, TSharedPtr<FComponentVisualizer> Visualizer)
{
	if (GUnrealEd != NULL)
	{
		GUnrealEd->RegisterComponentVisualizer(ComponentClassName, Visualizer);
	}

	RegisteredComponentClassNames.Add(ComponentClassName);

	if (Visualizer.IsValid())
	{
		Visualizer->OnRegister();
	}
}
