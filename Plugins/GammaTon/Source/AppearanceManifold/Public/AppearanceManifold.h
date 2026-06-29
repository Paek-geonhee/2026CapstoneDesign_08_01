// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FAppearanceManifoldModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	FString GetPluginDirectory() const;
	void RegisterPythonScriptPath();
	void EnsurePythonDependenciesInstalled();
};
