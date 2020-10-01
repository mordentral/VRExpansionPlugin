// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#include "VRExpansionPlugin.h"

#include "Grippables/GrippablePhysicsReplication.h"

#include "VRGlobalSettings.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"

#define LOCTEXT_NAMESPACE "FVRExpansionPluginModule"

void FVRExpansionPluginModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	RegisterSettings();

#if PHYSICS_INTERFACE_PHYSX
	FPhysScene_PhysX::PhysicsReplicationFactory = MakeShared<IPhysicsReplicationFactoryVR>();
	FPhysScene_PhysX::ContactModifyCallbackFactory = MakeShared<IContactModifyCallbackFactoryVR>();
	FPhysScene_PhysX::CCDContactModifyCallbackFactory = MakeShared<ICCDContactModifyCallbackFactoryVR>();
	//FPhysScene_ImmediatePhysX::PhysicsReplicationFactory = MakeShared<IPhysicsReplicationFactoryVR>();
#elif WITH_CHAOS
	FPhysScene_Chaos::PhysicsReplicationFactory = MakeShared<IPhysicsReplicationFactoryVR>();
#endif
}

void FVRExpansionPluginModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	UnregisterSettings();
}

void FVRExpansionPluginModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		// Create the new category
		ISettingsContainerPtr SettingsContainer = SettingsModule->GetContainer("Project");


		SettingsModule->RegisterSettings("Project", "Plugins", "VRExpansionPlugin",
			LOCTEXT("VRExpansionSettingsName", "VRExpansion Settings"),
			LOCTEXT("VRExpansionSettingsDescription", "Configure global settings for the VRExpansionPlugin"),
			GetMutableDefault<UVRGlobalSettings>());
	}
}

void FVRExpansionPluginModule::UnregisterSettings()
{
	// Ensure to unregister all of your registered settings here, hot-reload would
	// otherwise yield unexpected results.
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "VRExpansionPlugin");
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVRExpansionPluginModule, VRExpansionPlugin)