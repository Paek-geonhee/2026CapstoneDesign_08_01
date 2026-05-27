#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FGammaTonModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static const FName TabId;

private:
	void RegisterMenus();
	void OpenPanel();
	TSharedRef<class SDockTab> SpawnTab(const class FSpawnTabArgs& Args);
};
