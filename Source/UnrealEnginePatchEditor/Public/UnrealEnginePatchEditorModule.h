#pragma once
#include "Modules/ModuleInterface.h"

class FUnrealEnginePatchEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void RegisterMenus();
    TSharedPtr<class SWidget> CreatePatchPanelWidget();
    TSharedPtr<SDockTab> OnSpawnPatchTab(const FSpawnTabArgs& Args);
};
