#include "GammaTon.h"
#include "SGammaTonPanel.h"
#include "ToolMenus.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "GammaTon"

IMPLEMENT_MODULE(FGammaTonModule, GammaTon)

const FName FGammaTonModule::TabId("GammaTonTab");

void FGammaTonModule::StartupModule() {
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabId,
        FOnSpawnTab::CreateRaw(this, &FGammaTonModule::SpawnTab))
        .SetDisplayName(LOCTEXT("TabTitle", "GammaTon"))
        .SetTooltipText(LOCTEXT("TabTooltip", "Simulate dust / weathering on selected actors (γ-ton, SIGGRAPH 2005)"))
        .SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FGammaTonModule::RegisterMenus));
}

void FGammaTonModule::ShutdownModule() {
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::Get()->UnregisterOwner(this);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

void FGammaTonModule::RegisterMenus() {
    FToolMenuOwnerScoped OwnerScoped(this);
    UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    FToolMenuSection& Section = Menu->FindOrAddSection(
        "GammaTonSection",
        LOCTEXT("GammaTonSection", "Weathering Simulation"));
    Section.AddMenuEntry(
        "OpenGammaTon",
        LOCTEXT("OpenGammaTon",        "GammaTon Dust Simulator"),
        LOCTEXT("OpenGammaTonTooltip", "Simulate dust / weathering on selected actors (γ-ton, SIGGRAPH 2005)"),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"),
        FUIAction(FExecuteAction::CreateRaw(this, &FGammaTonModule::OpenPanel)));
}

void FGammaTonModule::OpenPanel() {
    FGlobalTabmanager::Get()->TryInvokeTab(TabId);
}

TSharedRef<SDockTab> FGammaTonModule::SpawnTab(const FSpawnTabArgs& Args) {
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SGammaTonPanel)
        ];
}

#undef LOCTEXT_NAMESPACE
