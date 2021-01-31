// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FGameAction_RuntimeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	void StartupModule() override;
	void ShutdownModule() override;
};
