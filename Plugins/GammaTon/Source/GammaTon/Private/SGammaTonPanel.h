#pragma once
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Core/GTCore.h"
#include "GammaTonSourceVisualizer.h"
#include "GammaTonRayVisualizer.h"

class SGammaTonPanel : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SGammaTonPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SGammaTonPanel() override;

private:
    // ── Global simulation params ───────────────────────────────────────────
    int32 NTonsPerIter  = 30000;
    int32 MaxBounces    = 10;
    float DepositK      = 0.50f;
    float FlowStep      = 30.0f;
    int32 NumIterations = 20;
    int32 TextureSize   = 1024;
    float BounceDistance  = 50.0f;
    float ParabolaGravity = 0.5f;

    // ── Cross-channel rules (paper §3.4) ──────────────────────────────────
    float CrossRustFromHumidity  = 0.0f;
    float CrossHumidityDecay     = 0.0f;
    float CrossPigmentCoversDust = 0.0f;

    // ── Scenario ──────────────────────────────────────────────────────────
    int32        ScenarioIdx          = 0;
    FLinearColor ScenarioDustColor    = FLinearColor(0.42f, 0.38f, 0.32f, 1.0f);
    FLinearColor ScenarioPigmentColor = FLinearColor(0.15f, 0.10f, 0.06f, 1.0f);
    void         ApplyScenario(int32 Idx);

    // ── Detail textures (optional — nullptr = flat color) ─────────────────
    UTexture2D* DustTexture_    = nullptr;
    UTexture2D* PigmentTexture_ = nullptr;

    // ── Dust/pigment tint intensity [0=no color change, 1=full effect] ────
    float DustVisibility_ = 1.0f;

    // ── Per-actor γ-reflectance fallback defaults ─────────────────────────
    float ReflDeltaS = 0.5f;
    float ReflDeltaP = 0.0f;
    float ReflDeltaF = 0.0f;

    // ── Ton Types (P2-D: multiple simultaneous γ-ton types) ───────────────
    struct FTonTypeEntry {
        FString Name   = TEXT("Ton Type");
        float   Weight = 1.0f;
        // Initial motion probabilities
        float MotionKs = 0.5f, MotionKp = 0.0f, MotionKf = 0.2f;
        // Initial carrier composition
        float CarrierSD = 1.0f, CarrierSP = 0.0f, CarrierSR = 0.0f, CarrierSH = 0.0f;
        // Source
        int32 SourceTypeIdx = 0;
        float SrcCX = 0.0f, SrcCY = 0.0f, SrcCZ = 1400.0f;
        float SrcDX = 0.0f, SrcDY = 0.0f, SrcDZ = -1.0f;
        float SrcSpread = 5.0f, SrcHalfX = 500.0f, SrcHalfZ = 500.0f;
        // Collapse state — toggled by the card header button
        bool bCollapsed = false;
    };

    TArray<TSharedPtr<FTonTypeEntry>> TonTypes_;
    TSharedPtr<class SBox>            TonTypesContainer_;

    void   SetTonTypes(const std::vector<GTTonType>& types);
    void   RebuildTonTypesUI();
    FReply OnAddTonTypeClicked();
    GTTonType     EntryToTonType(const FTonTypeEntry& e) const;
    GTGammaSource EntryToSource (const FTonTypeEntry& e) const;

    // ── Per-actor γ-reflectance ───────────────────────────────────────────
    struct FActorReflEntry {
        FString Name;
        // γ-reflectance decay rates
        float DeltaS = 0.5f;
        float DeltaP = 0.0f;
        float DeltaF = 0.0f;
        // Initial surface material (논문 §4 stain-bleeding: 액터별 풍화 초기값)
        float InitSD = 0.0f;  // 초기 먼지 밀도
        float InitSP = 0.0f;  // 초기 색소 (녹/이끼 등)
        float InitSR = 0.0f;  // 초기 거칠기
        float InitSH = 0.0f;  // 초기 습도
    };
    TArray<TSharedPtr<FActorReflEntry>>         ActorReflEntries_;
    TMap<FString, TSharedPtr<FActorReflEntry>> AllActorSettings_;
    TSharedPtr<class SBox>                     ActorReflContainer_;
    FReply OnRefreshActorsClicked();
    void   RebuildActorReflUI();
    TArray<GTGammaReflectance> BuildPerActorRefl   (int32 NumActors) const;
    TArray<GTMaterialProps>    BuildPerActorInitMat(int32 NumActors) const;

    // ── Viewport visualizers ──────────────────────────────────────────────
    TUniquePtr<FGammaTonSourceVisualizer> Visualizer_;
    TUniquePtr<FGammaTonRayVisualizer>    RayVisualizer_;
    void RefreshVisualizer();

    // ── UI helpers ────────────────────────────────────────────────────────
    TSharedPtr<class SMultiLineEditableText> StatusText_;
    TArray<TSharedPtr<FString>>              SourceOptions_;
    TArray<TSharedPtr<FString>>              ScenarioOptions_;

    // ── Undo snapshot (Run 직전 머티리얼 1단계 저장) ──────────────────────
    struct FUndoEntry {
        TWeakObjectPtr<UStaticMeshComponent> Component;
        TWeakObjectPtr<UMaterialInterface>   Material;
    };
    TArray<FUndoEntry> UndoSnapshot_;

    FReply OnRunClicked();
    FReply OnUndoClicked();
    FReply OnTraceRayClicked();
    void   SetStatus(const FString& Msg);

    TSharedRef<SWidget> MakeSrcFloatBox(float& Val);
    TSharedRef<SWidget> MakeUnitBox(float& Val);
    static TSharedRef<SWidget> MakeRow(const FString& Label, TSharedRef<SWidget> Control);
    static TSharedRef<SWidget> MakeFloatBox(float& Val);
    static TSharedRef<SWidget> MakeIntBox(int32& Val);

    FString StatusMsg_ = TEXT("Ready. Select actors and press Run.");
};
