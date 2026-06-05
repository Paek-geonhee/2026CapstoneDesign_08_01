#pragma once
#include "CoreMinimal.h"
#include "TickableEditorObject.h"
#include "Core/GTCore.h"

// Draws a single γ-ton debug ray path in the editor viewport each frame.
// Lifetime is tied to the panel — create on open, destroy on close.
class FGammaTonRayVisualizer : public FTickableEditorObject
{
public:
    FGammaTonRayVisualizer();
    virtual ~FGammaTonRayVisualizer();

    void SetPath(const GTRayPath& InPath);
    void ClearPath();
    void SetVisible(bool bInVisible);

    virtual void    Tick(float DeltaTime) override;
    virtual bool    IsTickable() const override { return bVisible && bHasPath; }
    virtual TStatId GetStatId() const override;

private:
    void   DrawPath(UWorld* W) const;
    FColor EventColor(GTBounceEvent Event) const;

    GTRayPath Path;
    bool      bVisible = false;
    bool      bHasPath = false;
};
