// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FOpenVRExpansionPluginModule : public IModuleInterface
{
public:

	FOpenVRExpansionPluginModule()
	{
		//OpenVRDLLHandle = nullptr;
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	//bool LoadOpenVRModule();
	//void UnloadOpenVRModule();

	//void* OpenVRDLLHandle;
};