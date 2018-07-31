// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"


class FVRExpansionPluginModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void RegisterSettings();

	void UnregisterSettings();
};