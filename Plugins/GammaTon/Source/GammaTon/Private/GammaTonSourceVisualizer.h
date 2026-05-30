#pragma once
#include "CoreMinimal.h"
#include "TickableEditorObject.h"
#include "Core/GTCore.h"

// Draws the GammaTon source shape in the editor viewport each frame.
// Lifetime is tied to the panel window — create on open, destroy on close.
class FGammaTonSourceVisualizer : public FTickableEditorObject
{
public:
    FGammaTonSourceVisualizer();
    virtual ~FGammaTonSourceVisualizer();

    void SetSource (const GTGammaSource& InSource);
    void SetSources(const TArray<GTGammaSource>& InSources);
    void SetVisible(bool bInVisible);

    // FTickableEditorObject
    virtual void    Tick(float DeltaTime) override;
    virtual bool    IsTickable() const override { return bVisible; }
    virtual TStatId GetStatId() const override;

private:
    void DrawAreaTop(UWorld* W, const GTGammaSource& S) const;
    void DrawDirectional(UWorld* W, const GTGammaSource& S) const;
    void DrawPoint(UWorld* W, const GTGammaSource& S) const;
    void DrawEnvironment(UWorld* W, const GTGammaSource& S) const;

    static void BuildFrame(const FVector& N, FVector& T, FVector& B);

    TArray<GTGammaSource> Sources_;
    bool                  bVisible = false;
};
