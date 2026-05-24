#include "GammaTon.h"
#include "SGammaTonPanel.h"
#include "ToolMenus.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "GammaTon"

IMPLEMENT_MODULE(FGammaTonModule, GammaTon)

void FGammaTonModule::StartupModule() {
    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(
            this, &FGammaTonModule::RegisterMenus));
}

void FGammaTonModule::ShutdownModule() {
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::Get()->UnregisterOwner(this);
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
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateRaw(this, &FGammaTonModule::OpenPanel)));
}

void FGammaTonModule::OpenPanel() {
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("WindowTitle", "GammaTon Dust Simulator"))
        .ClientSize(FVector2D(460, 700))
        .SizingRule(ESizingRule::FixedSize)
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        [
            SNew(SGammaTonPanel)
        ];
    FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
