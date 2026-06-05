#include "SGammaTonPanel.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Colors/SColorBlock.h"
#include "GammaTonMeshBridge.h"
#include "GammaTonTextureBridge.h"
#include "Core/GTRayIntersect.h"
#include "Core/GTSimulator.h"
#include "Engine/Texture2D.h"

#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Editor.h"
#include "Selection.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "GammaTon"

// Inter/Roboto 기본 Slate 폰트에는 한글 포함 안 됨
static FSlateFontInfo GGetKorFont(int32 Size = 9)
{
#if PLATFORM_WINDOWS
    static const FString MalgunPath = []() -> FString {
        FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
        if (Root.IsEmpty()) Root = TEXT("C:\\Windows");
        return FPaths::Combine(Root, TEXT("Fonts"), TEXT("malgun.ttf"));
    }();
    if (FPaths::FileExists(MalgunPath)) {
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        return FSlateFontInfo(MalgunPath, Size);
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }
#endif
    return FCoreStyle::GetDefaultFontStyle("Regular", Size);
}

// ── Scenario presets ──────────────────────────────────────────────────────────

struct FGTScenario {
    const TCHAR* Name;
    const TCHAR* Desc;  // shown in the info panel below the combo
    const TCHAR* Tags;  // one-line summary: source type, dominant motion, notes
    // Sim
    int32 NTons; int32 MaxBounces; float DepositK; float FlowStep; int32 NumIter;
    // Motion probabilities + γ-reflectance
    float Ks; float Kp; float Kf;
    float DeltaS; float DeltaP; float DeltaF;
    // kp parabola
    float BounceDistance;
    // Carrier (sd, sp, sr, sh)
    float CSD; float CSP; float CSR; float CSH;
    // Source  (0=AREA_TOP  1=DIRECTIONAL  2=POINT  3=ENVIRONMENT)
    int32 SrcType;
    float CX, CY, CZ;
    float DX, DY, DZ;
    float Spread; float HalfX; float HalfZ;
    // Cross-channel rules (paper §3.4)
    float RustFromHumidity;  // sh→sr growth rate
    float HumidityDecay;     // sh evaporation per iteration
    float PigmentCoversDust; // sp→sd suppression
    // Material colours
    FLinearColor DustCol;
    FLinearColor PigCol;
};

static const FGTScenario GScenarios[] = {
    // ── 0  Custom ─────────────────────────────────────────────────────────────
    { TEXT("Custom"),
      TEXT("No preset applied. Configure every parameter manually. "
           "Use this when you need precise control or are experimenting with a new weathering type."),
      TEXT("Manual  ·  all parameters editable"),
      30000, 10, 0.50f, 30.f, 20,
      0.5f, 0.0f, 0.2f,  0.5f, 0.0f, 0.0f,  50.f,
      1.f, 0.f, 0.f, 0.f,
      0, 0,0,1400, 0,0,-1, 5,500,500,
      0.0f, 0.0f, 0.0f,
      FLinearColor(0.42f,0.38f,0.32f,1), FLinearColor(0.15f,0.10f,0.06f,1) },

    // ── 1  Stain Bleeding ─────────────────────────────────────────────────────
    { TEXT("Stain Bleeding"),
      TEXT("Pigment bounces off the surface, then drains along it under gravity, "
           "pooling in joints, cracks, and lower edges. "
           "Reproduces rust streaks, water stains, or paint runoff draining downward. "
           "(Paper example, §3.5)"),
      TEXT("Single-ton  ·  AREA_TOP  ·  kp → kf dominant  ·  strong flow"),
      40000, 15, 0.45f, 35.f, 30,
      0.0f, 0.8f, 0.2f,  0.0f, 0.4f, 0.05f,  60.f,
      0.0f, 0.9f, 0.05f, 0.15f,
      0, 0,0,1400, 0,0,-1, 5,600,600,
      0.0f, 0.0f, 0.0f,
      FLinearColor(0.32f,0.22f,0.10f,1), FLinearColor(0.60f,0.28f,0.05f,1) },

    // ── 2  Metallic Patina ────────────────────────────────────────────────────
    { TEXT("Metallic Patina"),
      TEXT("Atmospheric oxidation arriving from all directions simultaneously. "
           "Tons reflect many times before settling, building up a green-blue patina "
           "concentrated on upward-facing and exposed metal surfaces. "
           "(Paper example, §3.5)"),
      TEXT("Single-ton  ·  ENVIRONMENT  ·  reflection-dominant  ·  pigment-only carrier"),
      50000, 20, 0.30f, 20.f, 40,
      1.0f, 0.0f, 0.0f,  0.15f, 0.0f, 0.0f,  50.f,
      0.0f, 1.0f, 0.2f, 0.0f,
      2, 0,0,0, 0,0,-1, 0,800,800,
      0.0f, 0.0f, 0.0f,
      FLinearColor(0.18f,0.42f,0.20f,1), FLinearColor(0.08f,0.35f,0.12f,1) },

    // ── 3  Urban Rain ─────────────────────────────────────────────────────────
    { TEXT("Urban Rain"),
      TEXT("Two weathering agents simulate the layered grime on city buildings: "
           "vertical rainfall that flows down facades depositing dirt in crevices, "
           "and a separate wind-blown dust/soot stream arriving from the side. "
           "Cross-channel humidity decay keeps wet areas visually distinct."),
      TEXT("Multi-ton  ·  AREA_TOP + DIRECTIONAL  ·  high humidity  ·  dual deposit"),
      50000, 12, 0.35f, 50.f, 30,
      0.15f, 0.1f, 0.65f,  0.4f, 0.1f, 0.05f,  50.f,
      0.3f, 0.05f, 0.0f, 0.85f,
      0, 0,0,1500, 0,0,-1, 3,800,800,
      0.015f, 0.5f, 0.0f,
      FLinearColor(0.35f,0.30f,0.25f,1), FLinearColor(0.12f,0.09f,0.06f,1) },

    // ── 4  Desert Sand ────────────────────────────────────────────────────────
    { TEXT("Desert Sand"),
      TEXT("Wind-driven sand particles arrive from the side at a shallow angle, "
           "bouncing off hard surfaces before settling. "
           "Warm tan grit accumulates on windward faces, ledges, and any surface "
           "that intercepts the flow; sheltered areas stay relatively clean."),
      TEXT("Single-ton  ·  DIRECTIONAL  ·  kp-dominant  ·  side wind  ·  high dust"),
      40000, 14, 0.45f, 20.f, 25,
      0.50f, 0.30f, 0.1f,  0.4f, 0.2f, 0.05f,  40.f,
      1.0f, 0.25f, 0.1f, 0.0f,
      0, -1000,0,600, 1,0,-0.15f, 6,700,700,
      0.0f, 0.0f, 0.0f,
      FLinearColor(0.74f,0.62f,0.38f,1), FLinearColor(0.68f,0.52f,0.28f,1) },

    // ── 5  Industrial Soot ────────────────────────────────────────────────────
    { TEXT("Industrial Soot"),
      TEXT("Airborne carbon particles fall from above and bounce multiple times "
           "before settling. Dark grey-black coating builds up uniformly on "
           "horizontal ledges and any upward-facing geometry. "
           "Use for chimneys, factory rooftops, or urban structures near heavy industry."),
      TEXT("Single-ton  ·  AREA_TOP  ·  kp-dominant  ·  dark pigment  ·  high dust"),
      45000, 16, 0.40f, 25.f, 25,
      0.40f, 0.40f, 0.1f,  0.35f, 0.15f, 0.05f,  45.f,
      0.6f, 0.95f, 0.15f, 0.0f,
      0, 0,0,1500, 0,0,-1, 4,700,700,
      0.0f, 0.0f, 0.0f,
      FLinearColor(0.15f,0.13f,0.11f,1), FLinearColor(0.05f,0.04f,0.03f,1) },

    // ── 6  Pipe Drip ──────────────────────────────────────────────────────────
    { TEXT("Pipe Drip"),
      TEXT("A single point source simulates a leaking pipe or joint dripping water. "
           "High surface-flow probability means tons travel far down the geometry, "
           "building a narrow rust-and-mineral streak directly below the leak. "
           "Roughness (sr) picked up from the surface amplifies surface detail."),
      TEXT("Single-ton  ·  POINT  ·  kf-dominant  ·  high humidity  ·  rust growth"),
      30000, 15, 0.55f, 40.f, 25,
      0.05f, 0.05f, 0.8f,  0.3f, 0.1f, 0.05f,  50.f,
      0.2f, 0.5f, 0.05f, 0.95f,
      1, 0,0,350, 0,0,-1, 10,0,0,
      0.03f, 0.4f, 0.0f,
      FLinearColor(0.42f,0.38f,0.28f,1), FLinearColor(0.35f,0.20f,0.08f,1) },

    // ── 7  Biological Growth ──────────────────────────────────────────────────
    { TEXT("Biological Growth"),
      TEXT("Two agents work together: moisture flows into low and concave areas, "
           "and moss spores settle preferentially where surfaces remain damp. "
           "Rapid humidity decay prevents oversaturation on exposed faces, "
           "so green growth concentrates realistically in shaded crevices and corners."),
      TEXT("Multi-ton  ·  AREA_TOP + ENVIRONMENT  ·  very high flow  ·  humidity-gated"),
      50000, 12, 0.35f, 55.f, 40,
      0.1f, 0.05f, 0.92f,  0.3f, 0.05f, 0.05f,  50.f,
      0.05f, 0.7f, 0.0f, 0.95f,
      0, 0,0,1500, 0,0,-1, 3,800,800,
      0.005f, 0.65f, 0.12f,
      FLinearColor(0.22f,0.26f,0.15f,1), FLinearColor(0.08f,0.20f,0.04f,1) },

    // ── 8  Coastal Salt Spray ─────────────────────────────────────────────────
    { TEXT("Coastal Salt Spray"),
      TEXT("Sea wind carries salt crystals from the side at a low angle. "
           "Windward surfaces are roughened (sr) by crystal abrasion and coated "
           "with pale white mineral deposits. Sheltered faces and recesses "
           "receive far less exposure, creating strong directional contrast."),
      TEXT("Single-ton  ·  DIRECTIONAL  ·  side wind  ·  high roughness  ·  white tint"),
      40000, 12, 0.40f, 20.f, 25,
      0.5f, 0.3f, 0.1f,  0.4f, 0.15f, 0.05f,  40.f,
      0.85f, 0.15f, 0.5f, 0.25f,
      0, -1200,0,700, 1,0,-0.1f, 10,600,600,
      0.008f, 0.1f, 0.0f,
      FLinearColor(0.90f,0.88f,0.84f,1), FLinearColor(0.82f,0.80f,0.76f,1) },
};

static const int GNumScenarios = UE_ARRAY_COUNT(GScenarios);

// ── Destructor ────────────────────────────────────────────────────────────────

SGammaTonPanel::~SGammaTonPanel()
{
    SaveSettings();
    if (Visualizer_)    Visualizer_->SetVisible(false);
    if (RayVisualizer_) RayVisualizer_->SetVisible(false);
}

// ── Scenario application ──────────────────────────────────────────────────────

static GTGammaSource ScenarioToSource(const FGTScenario& S)
{
    GTGammaSource Src;
    Src.type        = (GTSourceType)S.SrcType;
    Src.center      = { S.CX, S.CY, S.CZ };
    Src.direction   = GTVec3{ S.DX, S.DY, S.DZ }.normalized();
    Src.spread_deg  = S.Spread;
    Src.area_half_x = S.HalfX;
    Src.area_half_z = S.HalfZ;
    return Src;
}

void SGammaTonPanel::ApplyScenario(int32 Idx)
{
    if (Idx <= 0 || Idx >= GNumScenarios) return;  // 0 = Custom, no-op
    const FGTScenario& S = GScenarios[Idx];

    NTonsPerIter   = S.NTons;
    MaxBounces     = S.MaxBounces;
    DepositK       = S.DepositK;
    FlowStep       = S.FlowStep;
    NumIterations  = S.NumIter;
    BounceDistance = S.BounceDistance;
    ReflDeltaS = S.DeltaS; ReflDeltaP = S.DeltaP; ReflDeltaF = S.DeltaF;

    CrossRustFromHumidity  = S.RustFromHumidity;
    CrossHumidityDecay     = S.HumidityDecay;
    CrossPigmentCoversDust = S.PigmentCoversDust;

    ScenarioDustColor    = S.DustCol;
    ScenarioPigmentColor = S.PigCol;

    // Build primary ton type from scenario struct
    using E = GTTransportEvent; using En = GTTransportEntity; using Ch = GTTransportChannel;
    GTTonType primary;
    primary.name        = std::string(TCHAR_TO_ANSI(S.Name));
    primary.weight      = 1.0f;
    primary.init_motion = { S.Ks, S.Kp, S.Kf };
    primary.init_carrier= { S.CSD, S.CSP, S.CSR, S.CSH };
    primary.sources     = { ScenarioToSource(S) };
    primary.rules       = GTDefaultTransportRules();
    switch (Idx) {
    case 2: primary.rules.push_back({ E::PICKUP, En::TON, Ch::SR, En::SURFACE, Ch::SR, 1.0f }); break;
    case 6: primary.rules.push_back({ E::PICKUP, En::TON, Ch::SR, En::SURFACE, Ch::SR, 0.5f }); break;
    default: break;
    }

    std::vector<GTTonType> types = { primary };

    // Multi-ton scenarios (P2-D: compound weathering)
    if (Idx == 3) {
        // Urban Rain: add wind-blown dust/soot as a second ton type
        GTTonType dust;
        dust.name        = "Dust/Soot";
        dust.weight      = 0.5f;
        dust.init_motion = { 0.55f, 0.25f, 0.1f };
        dust.init_carrier= { 1.0f, 0.2f, 0.0f, 0.0f };
        GTGammaSource dSrc;
        dSrc.type = GTSourceType::DIRECTIONAL;
        dSrc.center = { -1000.f, 0.f, 600.f };
        dSrc.direction = GTVec3{ 1.f, 0.f, -0.2f }.normalized();
        dSrc.spread_deg = 5.f; dSrc.area_half_x = 600.f; dSrc.area_half_z = 600.f;
        dust.sources = { dSrc };
        dust.rules   = GTDefaultTransportRules();
        types.push_back(dust);
    }
    else if (Idx == 7) {
        // Biological Growth: add moss spore ton type that flows and settles on damp surfaces
        GTTonType moss;
        moss.name        = "Moss Spores";
        moss.weight      = 0.2f;  // 0.6→0.2: 이끼 포자 밀도 감소, 전체 덮임 방지
        moss.init_motion = { 0.0f, 0.0f, 0.9f };
        moss.init_carrier= { 0.0f, 0.9f, 0.0f, 0.7f };
        GTGammaSource mSrc;
        mSrc.type = GTSourceType::ENVIRONMENT;
        mSrc.center = { 0.f, 0.f, 0.f };
        mSrc.direction = { 0.f, 0.f, -1.f };
        mSrc.spread_deg = 20.f; mSrc.area_half_x = 1200.f; mSrc.area_half_z = 1200.f;
        moss.sources = { mSrc };
        moss.rules   = GTDefaultTransportRules();
        types.push_back(moss);
    }

    SetTonTypes(types);
    RefreshVisualizer();
}

// ── Source / visualizer helpers ───────────────────────────────────────────────

void SGammaTonPanel::RefreshVisualizer()
{
    if (!Visualizer_) return;
    TArray<GTGammaSource> All;
    for (const auto& E : TonTypes_)
        All.Add(EntryToSource(*E));
    Visualizer_->SetSources(All);
}

// ── Widget helpers ────────────────────────────────────────────────────────────

TSharedRef<SWidget> SGammaTonPanel::MakeRow(const FString& Label, TSharedRef<SWidget> Ctrl) {
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [ SNew(SBox).WidthOverride(160.f) [ SNew(STextBlock).Text(FText::FromString(Label)) ] ]
        + SHorizontalBox::Slot().FillWidth(1.f) [ Ctrl ];
}
TSharedRef<SWidget> SGammaTonPanel::MakeFloatBox(float& Val) {
    return SNew(SSpinBox<float>)
        .MinValue(-100000.f).MaxValue(100000.f)
        .Value_Lambda([&Val]() { return Val; })
        .OnValueChanged_Lambda([&Val](float v) { Val = v; });
}
TSharedRef<SWidget> SGammaTonPanel::MakeIntBox(int32& Val) {
    return SNew(SSpinBox<int32>)
        .MinValue(1).MaxValue(10000000)
        .Value_Lambda([&Val]() { return Val; })
        .OnValueChanged_Lambda([&Val](int32 v) { Val = v; });
}
TSharedRef<SWidget> SGammaTonPanel::MakeSrcFloatBox(float& Val) {
    // Spread 전용 (0~180도), 범위를 좁혀서 드래그 감도 현실화
    return SNew(SSpinBox<float>)
        .MinValue(0.f).MaxValue(180.f).Delta(1.f)
        .Value_Lambda([&Val]() { return Val; })
        .OnValueChanged_Lambda([this, &Val](float v) { Val = v; RefreshVisualizer(); });
}
TSharedRef<SWidget> SGammaTonPanel::MakeDirFloatBox(float& Val) {
    return SNew(SSpinBox<float>)
        .MinValue(-1.f).MaxValue(1.f).Delta(0.05f)
        .Value_Lambda([&Val]() { return Val; })
        .OnValueChanged_Lambda([this, &Val](float v) { Val = v; RefreshVisualizer(); });
}
// CX/CY/CZ 전용 — 드래그 없이 직접 타이핑
TSharedRef<SWidget> SGammaTonPanel::MakePosEntryBox(float& Val) {
    float* Ptr = &Val;
    return SNew(SNumericEntryBox<float>)
        .AllowSpin(true)
        .MinValue(TOptional<float>())
        .MaxValue(TOptional<float>())
        .MinSliderValue(TOptional<float>())
        .MaxSliderValue(TOptional<float>())
        .LinearDeltaSensitivity(1)
        .Delta(1.0f)
        .ShiftMultiplier(0.1f)
        .CtrlMultiplier(10.0f)
        .Value_Lambda([Ptr]() { return TOptional<float>(*Ptr); })
        .OnValueChanged_Lambda([this, Ptr](float v) { *Ptr = v; RefreshVisualizer(); })
        .OnValueCommitted_Lambda([this, Ptr](float v, ETextCommit::Type) { *Ptr = v; RefreshVisualizer(); });
}
TSharedRef<SWidget> SGammaTonPanel::MakeUnitBox(float& Val) {
    return SNew(SSpinBox<float>)
        .MinValue(0.f).MaxValue(1.f).Delta(0.05f)
        .Value_Lambda([&Val]() { return Val; })
        .OnValueChanged_Lambda([&Val](float v) { Val = v; });
}

// ── Construct ─────────────────────────────────────────────────────────────────

void SGammaTonPanel::Construct(const FArguments& InArgs)
{
    SourceOptions_ = {
        MakeShared<FString>(TEXT("Directional Particle")),
        MakeShared<FString>(TEXT("Spot Particle")),
        MakeShared<FString>(TEXT("Sky Particle")),
    };
    for (int i = 0; i < GNumScenarios; i++)
        ScenarioOptions_.Add(MakeShared<FString>(GScenarios[i].Name));

    ChildSlot
    [
        SNew(SBox).MinDesiredWidth(440.f)
        [
            SNew(SScrollBox)

            // ── Run / Undo ────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 10, 8, 2)
            [
                SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), "PrimaryButton")
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                .ContentPadding(FMargin(0.f, 10.f))
                .OnClicked(this, &SGammaTonPanel::OnRunClicked)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("RunBtn", "▶  Apply Weathering"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    .ColorAndOpacity(FLinearColor::White)
                    .Justification(ETextJustify::Center)
                ]
            ]
            + SScrollBox::Slot().Padding(8, 3, 8, 4)
            [
                SNew(SButton)
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                .ContentPadding(FMargin(0.f, 5.f))
                .OnClicked(this, &SGammaTonPanel::OnUndoClicked)
                .ToolTipText(LOCTEXT("TipUndo",
                    "마지막 Run 직전 상태로 머티리얼을 되돌립니다.\n"
                    "Run을 한 번 실행한 뒤에만 동작합니다."))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("UndoBtn", "↩  Undo Last Simulation"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    .Justification(ETextJustify::Center)
                ]
            ]

            // ── 실행 결과 요약 ─────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 0, 8, 8)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.06f, 0.08f, 0.12f, 1.f))
                .Padding(FMargin(12.f, 8.f))
                [
                    SAssignNew(ResultText_, STextBlock)
                    .Font(GGetKorFont(9))
                    .ColorAndOpacity(FLinearColor(0.55f, 0.65f, 0.80f, 1.f))
                    .AutoWrapText(true)
                    .Text(LOCTEXT("ResultReady", "Ready.  Select actors and press Run."))
                ]
            ]

            // ── Scenario ──────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ScnHeader", "Scenario Preset"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                MakeRow(TEXT("Scenario"),
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&ScenarioOptions_)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) {
                        ScenarioIdx = ScenarioOptions_.Find(Item);
                        ApplyScenario(ScenarioIdx);
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                        return SNew(STextBlock).Font(GGetKorFont(9)).Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(ScenarioOptions_[0])
                    [
                        SNew(STextBlock).Font(GGetKorFont(9))
                        .Text_Lambda([this]() { return FText::FromString(*ScenarioOptions_[ScenarioIdx]); })
                    ]
                )
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.08f, 0.10f, 0.14f, 1.f))
                .Padding(FMargin(10.f, 7.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                    [
                        SNew(STextBlock).Font(GGetKorFont(8))
                        .ColorAndOpacity(FLinearColor(0.55f, 0.75f, 1.0f, 1.f))
                        .Text_Lambda([this]() {
                            return FText::FromString(GScenarios[FMath::Clamp(ScenarioIdx, 0, GNumScenarios-1)].Tags);
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).Font(GGetKorFont(9)).AutoWrapText(true)
                        .ColorAndOpacity(FLinearColor(0.80f, 0.80f, 0.80f, 1.f))
                        .Text_Lambda([this]() {
                            return FText::FromString(GScenarios[FMath::Clamp(ScenarioIdx, 0, GNumScenarios-1)].Desc);
                        })
                    ]
                ]
            ]

            // ── Simulation ────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("SimHeader", "Simulation"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            // γ-tons / iter  +  Iterations  (한 줄)
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(SBox).WidthOverride(120.f) [ SNew(STextBlock).Text(LOCTEXT("LblNTons", "Particles per Step")) ] ]
                + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
                [
                    SNew(SNumericEntryBox<int32>).AllowSpin(false)
                    .Value_Lambda([this]() { return TOptional<int32>(NTonsPerIter); })
                    .OnValueCommitted_Lambda([this](int32 v, ETextCommit::Type) { NTonsPerIter = FMath::Max(1, v); })
                    .ToolTipText(LOCTEXT("TipNTons",
                        "Number of particles emitted in a single step.\n"
                        "Higher values produce more even coverage but increase compute time per step."))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [ SNew(STextBlock).Text(LOCTEXT("LblIter", "Simulation Steps")) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SNumericEntryBox<int32>).AllowSpin(false)
                    .Value_Lambda([this]() { return TOptional<int32>(NumIterations); })
                    .OnValueCommitted_Lambda([this](int32 v, ETextCommit::Type) { NumIterations = FMath::Max(1, v); })
                    .ToolTipText(LOCTEXT("TipIter",
                        "Total number of simulation passes.\n"
                        "More steps build up thicker weathering layers and smoother gradients."))
                ]
            ]
            // Deposit K
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Accumulation Strength"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).MinSliderValue(0.f).MaxSliderValue(1.f).Delta(0.01f)
                .Value_Lambda([this]() { return DepositK; })
                .OnValueChanged_Lambda([this](float v) { DepositK = v; })
                .ToolTipText(LOCTEXT("TipDeposit", "Global deposit scale per particle. 0 = no deposit, 1 = maximum accumulation."))
            )]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Texture size (px)"),
                SNew(SNumericEntryBox<int32>).AllowSpin(false)
                .Value_Lambda([this]() { return TOptional<int32>(TextureSize); })
                .OnValueCommitted_Lambda([this](int32 v, ETextCommit::Type) { TextureSize = FMath::Max(1, v); })
                .ToolTipText(LOCTEXT("TipTex", "Output texture resolution in pixels. Recommended: 512–2048. Larger values give sharper detail but use more memory."))
            )]
            // Advanced toggle button
            + SScrollBox::Slot().Padding(12, 6, 12, 2)
            [
                SNew(SButton).HAlign(HAlign_Left)
                .ButtonStyle(FAppStyle::Get(), "NoBorder")
                .OnClicked_Lambda([this]() -> FReply {
                    bSimAdvancedExpanded_ = !bSimAdvancedExpanded_;
                    return FReply::Handled();
                })
                [
                    SNew(STextBlock)
                    .ColorAndOpacity(FLinearColor(0.5f, 0.7f, 1.f, 1.f))
                    .Text_Lambda([this]() {
                        return FText::FromString(bSimAdvancedExpanded_
                            ? TEXT("▼  Advanced") : TEXT("▶  Advanced"));
                    })
                ]
            ]
            + SScrollBox::Slot().Padding(12, 0)
            [
                SNew(SBox)
                .Visibility_Lambda([this]() {
                    return bSimAdvancedExpanded_ ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [ MakeRow(TEXT("Max Interactions"),
                        SNew(SNumericEntryBox<int32>).AllowSpin(false)
                        .Value_Lambda([this]() { return TOptional<int32>(MaxBounces); })
                        .OnValueCommitted_Lambda([this](int32 v, ETextCommit::Type) { MaxBounces = FMath::Max(1, v); })
                        .ToolTipText(LOCTEXT("TipBounce", "Maximum scatter / bounce / flow events per particle before it settles. Lower values concentrate deposits near the source."))
                    )]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [ MakeRow(TEXT("Flow Distance (cm)"),
                        SNew(SNumericEntryBox<float>).AllowSpin(false)
                        .Value_Lambda([this]() { return TOptional<float>(FlowStep); })
                        .OnValueCommitted_Lambda([this](float v, ETextCommit::Type) { FlowStep = FMath::Max(0.f, v); })
                        .ToolTipText(LOCTEXT("TipFlow", "Distance a particle travels per surface-flow event. Larger values create longer drip or streak marks."))
                    )]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [ MakeRow(TEXT("Bounce Distance (cm)"),
                        SNew(SNumericEntryBox<float>).AllowSpin(false)
                        .Value_Lambda([this]() { return TOptional<float>(BounceDistance); })
                        .OnValueCommitted_Lambda([this](float v, ETextCommit::Type) { BounceDistance = FMath::Max(0.f, v); })
                        .ToolTipText(LOCTEXT("TipBDist", "Maximum arc length for a bounce event in cm. Controls how far particles hop across the surface."))
                    )]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [ MakeRow(TEXT("Gravity Effect"),
                        SNew(SNumericEntryBox<float>).AllowSpin(false)
                        .Value_Lambda([this]() { return TOptional<float>(ParabolaGravity); })
                        .OnValueCommitted_Lambda([this](float v, ETextCommit::Type) { ParabolaGravity = FMath::Max(0.f, v); })
                        .ToolTipText(LOCTEXT("TipGrav", "Downward pull applied to bounce arcs. Higher values create shorter, more curved trajectories."))
                    )]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 2)
                    [ SNew(STextBlock).Text(LOCTEXT("CrsHdr", "Material Interactions"))
                      .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [ MakeRow(TEXT("Moisture → Rust Rate"),
                        SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f)
                        .Value_Lambda([this]() { return CrossRustFromHumidity; })
                        .OnValueChanged_Lambda([this](float v) { CrossRustFromHumidity = v; })
                        .ToolTipText(LOCTEXT("TipCrossRust", "Per-step conversion rate of moisture into rust. 0 = no rust growth, 1 = rapid oxidation."))
                    )]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [ MakeRow(TEXT("Moisture Evaporation"),
                        SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f)
                        .Value_Lambda([this]() { return CrossHumidityDecay; })
                        .OnValueChanged_Lambda([this](float v) { CrossHumidityDecay = v; })
                        .ToolTipText(LOCTEXT("TipCrossDecay", "How quickly moisture dries out each step. Higher values keep wet zones small and focused."))
                    )]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
                    [ MakeRow(TEXT("Rust Covers Dust"),
                        SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f)
                        .Value_Lambda([this]() { return CrossPigmentCoversDust; })
                        .OnValueChanged_Lambda([this](float v) { CrossPigmentCoversDust = v; })
                        .ToolTipText(LOCTEXT("TipCrossPig", "How much rust suppresses dust on the same surface. 1 = rust fully covers any underlying dust."))
                    )]
                ]
            ]

            // ── Ton Types ─────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("TonTypesHdr", "Particle Types"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("AddType", "+ Add Particle"))
                .OnClicked(this, &SGammaTonPanel::OnAddTonTypeClicked)
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [ SAssignNew(TonTypesContainer_, SBox) ]

            // ── Per-Actor γ-Reflectance ───────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PerActorHdr", "Surface Settings per Object"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("RefreshActors", "Refresh Actor List from Selection"))
                .OnClicked(this, &SGammaTonPanel::OnRefreshActorsClicked)
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SAssignNew(ActorReflContainer_, SBox)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("NoActors", "(Click Refresh to populate from current selection)"))
                    .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.f))
                ]
            ]

            // ── Occluder Actors ───────────────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("OccluderHdr", "Blocking Objects"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() {
                        return bAutoOccluder_ ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState s) {
                        bAutoOccluder_ = (s == ECheckBoxState::Checked);
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 8, 0)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("AutoOccLabel", "Auto-detect nearby occluders"))
                    .ToolTipText(LOCTEXT("TipAutoOcc",
                        "Automatically include nearby Static Mesh actors as blocking objects when you run the simulation.\n"
                        "These objects block particles but do not receive weathering themselves."))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [
                    SNew(STextBlock).Text(LOCTEXT("AutoOccRadLabel", "Radius (cm):"))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SNumericEntryBox<float>).AllowSpin(false)
                    .Value_Lambda([this]() { return TOptional<float>(AutoOccluderRadius_); })
                    .OnValueCommitted_Lambda([this](float v, ETextCommit::Type) {
                        AutoOccluderRadius_ = FMath::Max(0.f, v);
                        if (Visualizer_) {
                            TArray<AActor*> Sel;
                            for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
                                if (AActor* A = Cast<AActor>(*It)) Sel.Add(A);
                            FBox Bounds(EForceInit::ForceInit);
                            for (AActor* A : Sel) Bounds += A->GetComponentsBoundingBox(true);
                            FVector Center = Sel.IsEmpty() ? FVector::ZeroVector : Bounds.GetCenter();
                            Visualizer_->ShowOccluderRadius(Center, AutoOccluderRadius_, 3.0f);
                        }
                    })
                    .ToolTipText(LOCTEXT("TipAutoOccRadius",
                        "Search radius (cm) from the center of the selected actors.\n"
                        "Any Static Mesh within this radius is added as a blocking object.\n"
                        "Press Enter to preview the radius sphere in the viewport for 3 seconds."))
                ]
            ]

            // ── Dust / Pigment ────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("MatHdr", "Dust / Rust"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("DustSectionLbl", "Dust"))
              .ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f, 1.f)) ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() { return !bDustUseTexture_ ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState s) { if (s == ECheckBoxState::Checked) bDustUseTexture_ = false; })
                    [ SNew(STextBlock).Text(LOCTEXT("DustColorLbl", "Color")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox).IsEnabled_Lambda([this]() { return !bDustUseTexture_; })
                    [
                        SNew(SColorBlock)
                        .Color_Lambda([this]() { return ScenarioDustColor; })
                        .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& E) -> FReply {
                            if (!bDustUseTexture_ && E.GetEffectingButton() == EKeys::LeftMouseButton) {
                                FColorPickerArgs Args;
                                Args.bIsModal = true; Args.bUseAlpha = false;
                                Args.InitialColor = ScenarioDustColor;
                                Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
                                    [this](FLinearColor C) { ScenarioDustColor = C; });
                                OpenColorPicker(Args);
                            }
                            return FReply::Handled();
                        })
                        .ToolTipText(LOCTEXT("TipDustCol", "Tint color for deposited dust. Click to open the color picker."))
                    ]
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() { return bDustUseTexture_ ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState s) { if (s == ECheckBoxState::Checked) bDustUseTexture_ = true; })
                    [ SNew(STextBlock).Text(LOCTEXT("DustTexLbl", "Texture")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox).IsEnabled_Lambda([this]() { return bDustUseTexture_; })
                    [
                        SNew(SObjectPropertyEntryBox)
                        .AllowedClass(UTexture2D::StaticClass()).AllowClear(true)
                        .ObjectPath_Lambda([this]() { return DustTexture_ ? DustTexture_->GetPathName() : FString(); })
                        .OnObjectChanged_Lambda([this](const FAssetData& Data) {
                            DustTexture_ = Cast<UTexture2D>(Data.GetAsset());
                            if (DustTexture_) bDustUseTexture_ = true;
                        })
                        .ToolTipText(LOCTEXT("TipDustTex", "Optional detail texture multiplied on top of the dust color for added surface variation."))
                    ]
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("PigSectionLbl", "Rust"))
              .ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f, 1.f)) ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() { return !bPigmentUseTexture_ ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState s) { if (s == ECheckBoxState::Checked) bPigmentUseTexture_ = false; })
                    [ SNew(STextBlock).Text(LOCTEXT("PigColorLbl", "Color")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox).IsEnabled_Lambda([this]() { return !bPigmentUseTexture_; })
                    [
                        SNew(SColorBlock)
                        .Color_Lambda([this]() { return ScenarioPigmentColor; })
                        .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& E) -> FReply {
                            if (!bPigmentUseTexture_ && E.GetEffectingButton() == EKeys::LeftMouseButton) {
                                FColorPickerArgs Args;
                                Args.bIsModal = true; Args.bUseAlpha = false;
                                Args.InitialColor = ScenarioPigmentColor;
                                Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
                                    [this](FLinearColor C) { ScenarioPigmentColor = C; });
                                OpenColorPicker(Args);
                            }
                            return FReply::Handled();
                        })
                        .ToolTipText(LOCTEXT("TipPigCol", "Tint color for deposited rust. Click to open the color picker."))
                    ]
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() { return bPigmentUseTexture_ ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState s) { if (s == ECheckBoxState::Checked) bPigmentUseTexture_ = true; })
                    [ SNew(STextBlock).Text(LOCTEXT("PigTexLbl", "Texture")) ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox).IsEnabled_Lambda([this]() { return bPigmentUseTexture_; })
                    [
                        SNew(SObjectPropertyEntryBox)
                        .AllowedClass(UTexture2D::StaticClass()).AllowClear(true)
                        .ObjectPath_Lambda([this]() { return PigmentTexture_ ? PigmentTexture_->GetPathName() : FString(); })
                        .OnObjectChanged_Lambda([this](const FAssetData& Data) {
                            PigmentTexture_ = Cast<UTexture2D>(Data.GetAsset());
                            if (PigmentTexture_) bPigmentUseTexture_ = true;
                        })
                        .ToolTipText(LOCTEXT("TipPigTex", "Optional detail texture multiplied on top of the rust color for added surface variation."))
                    ]
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Weathering Intensity"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([this]() { return DustVisibility_; })
                .OnValueChanged_Lambda([this](float V) { DustVisibility_ = V; })
                .ToolTipText(LOCTEXT("TipDustVis", "Overall weathering color intensity. 0 = invisible, 1 = full effect. Use this as a master opacity for the weathering layer."))
            )]

            // ── Post-Process ──────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("PPHdr", "Post-Process"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Deposit Rate"),
                SNew(SSpinBox<float>).MinValue(0.01f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([this]() { return PostProcessConfig_.probabilistic_deposit; })
                .OnValueChanged_Lambda([this](float v) { PostProcessConfig_.probabilistic_deposit = v; })
                .ToolTipText(LOCTEXT("TipDepRate", "Fraction of particles that actually deposit material. 1.0 = all deposit, 0.3 = only 30% deposit. Lower values add a grainy, sparse look."))
            )]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(SCheckBox)
                  .IsChecked_Lambda([this]() { return PostProcessConfig_.useThreshold ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                  .OnCheckStateChanged_Lambda([this](ECheckBoxState s) { PostProcessConfig_.useThreshold = (s == ECheckBoxState::Checked); }) ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4, 0, 0, 0)
                [ SNew(STextBlock).Text(LOCTEXT("ThreshLabel", "Threshold + Sigmoid (contrast)")) ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Threshold"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(0.9f).Delta(0.01f)
                .Value_Lambda([this]() { return PostProcessConfig_.threshold; })
                .OnValueChanged_Lambda([this](float v) { PostProcessConfig_.threshold = v; })
                .ToolTipText(LOCTEXT("TipThresh", "Deposit values below this level are cut to zero, sharpening the weathering boundary."))
            )]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Steepness"),
                SNew(SSpinBox<float>).MinValue(1.f).MaxValue(50.f).Delta(0.5f)
                .Value_Lambda([this]() { return PostProcessConfig_.sigmoid_steepness; })
                .OnValueChanged_Lambda([this](float v) { PostProcessConfig_.sigmoid_steepness = v; })
                .ToolTipText(LOCTEXT("TipSteep", "Controls the transition sharpness of the sigmoid contrast curve. Higher = harder edge between clean and weathered."))
            )]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(SCheckBox)
                  .IsChecked_Lambda([this]() { return PostProcessConfig_.useNoiseMask ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                  .OnCheckStateChanged_Lambda([this](ECheckBoxState s) { PostProcessConfig_.useNoiseMask = (s == ECheckBoxState::Checked); }) ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4, 0, 0, 0)
                [ SNew(STextBlock).Text(LOCTEXT("NoiseMaskLabel", "Fractal Noise Mask")) ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Scale"),
                SNew(SSpinBox<float>).MinValue(0.5f).MaxValue(64.f).Delta(0.5f)
                .Value_Lambda([this]() { return PostProcessConfig_.noise_scale; })
                .OnValueChanged_Lambda([this](float v) { PostProcessConfig_.noise_scale = v; })
                .ToolTipText(LOCTEXT("TipNoiseScale", "Frequency of the fractal noise. Higher values create smaller, finer noise patches."))
            )]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Octaves"),
                SNew(SSpinBox<int32>).MinValue(1).MaxValue(8)
                .Value_Lambda([this]() { return PostProcessConfig_.noise_octaves; })
                .OnValueChanged_Lambda([this](int32 v) { PostProcessConfig_.noise_octaves = v; })
                .ToolTipText(LOCTEXT("TipNoiseOct", "Number of fractal layers. More octaves add fine detail but increase compute time."))
            )]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Strength"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([this]() { return PostProcessConfig_.noise_strength; })
                .OnValueChanged_Lambda([this](float v) { PostProcessConfig_.noise_strength = v; })
                .ToolTipText(LOCTEXT("TipNoiseStr", "How strongly the noise mask modulates the final deposit. 0 = no noise, 1 = full noise variation."))
            )]
            + SScrollBox::Slot().Padding(8, 2)
            [ MakeRow(TEXT("Seed"),
                SNew(SSpinBox<int32>).MinValue(0).MaxValue(999999)
                .Value_Lambda([this]() { return (int32)PostProcessConfig_.noise_seed; })
                .OnValueChanged_Lambda([this](int32 v) { PostProcessConfig_.noise_seed = (uint32_t)v; })
                .ToolTipText(LOCTEXT("TipNoiseSeed", "Random seed for the noise pattern. Change this to get a different weathering texture variation."))
            )]

            // ── Debug ─────────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(4, 8, 4, 0)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.18f, 1.f))
                .Padding(FMargin(8.f, 5.f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("DbgHdr", "Debug"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f))
                ]
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("TraceBtn", "Preview Particle Path"))
                .OnClicked(this, &SGammaTonPanel::OnTraceRayClicked)
                .ToolTipText(LOCTEXT("TipTrace",
                    "Trace a single particle path and draw it in the viewport.\n"
                    "Useful for verifying source position and direction before running a full simulation.\n"
                    "Detailed results are printed to the Output Log."))
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("ClearPathBtn", "Clear Particle Path"))
                .OnClicked_Lambda([this]() -> FReply {
                    if (RayVisualizer_) RayVisualizer_->ClearPath();
                    return FReply::Handled();
                })
                .ToolTipText(LOCTEXT("TipClearPath", "Remove the particle path visualization from the viewport."))
            ]
        ]
    ];

    Visualizer_ = MakeUnique<FGammaTonSourceVisualizer>();
    Visualizer_->SetVisible(true);

    RayVisualizer_ = MakeUnique<FGammaTonRayVisualizer>();
    RayVisualizer_->SetVisible(true);

    // Build initial ton type UI after containers are created
    SetTonTypes({ GTTonType{} });
    RefreshVisualizer();

    // 설정 복원
    LoadSettings();
}

// ── Ton Types ─────────────────────────────────────────────────────────────────

GTGammaSource SGammaTonPanel::EntryToSource(const FTonTypeEntry& e) const
{
    GTGammaSource Src;
    Src.type        = (GTSourceType)e.SourceTypeIdx;
    Src.center      = { e.SrcCX, e.SrcCY, e.SrcCZ };
    {
        float P = FMath::DegreesToRadians(e.SrcPitch);
        float Y = FMath::DegreesToRadians(e.SrcYaw);
        Src.direction = GTVec3{ FMath::Cos(P)*FMath::Cos(Y),
                                FMath::Cos(P)*FMath::Sin(Y),
                                FMath::Sin(P) }.normalized();
    }
    Src.spread_deg  = e.SrcSpread;
    Src.area_half_x = e.SrcHalfX;
    Src.area_half_z = e.SrcHalfZ;
    return Src;
}

GTTonType SGammaTonPanel::EntryToTonType(const FTonTypeEntry& e) const
{
    GTTonType T;
    T.name         = std::string(TCHAR_TO_ANSI(*e.Name));
    T.weight       = e.Weight;
    T.init_motion  = { e.MotionKs, e.MotionKp, e.MotionKf };
    T.init_carrier = { e.CarrierSD, e.CarrierSP, e.CarrierSR, e.CarrierSH };
    T.sources = { EntryToSource(e) };
    T.rules   = GTDefaultTransportRules();
    return T;
}

void SGammaTonPanel::SetTonTypes(const std::vector<GTTonType>& types)
{
    TonTypes_.Empty();
    for (const auto& t : types) {
        auto E = MakeShared<FTonTypeEntry>();
        E->Name       = FString(t.name.c_str());
        E->Weight     = t.weight;
        E->MotionKs   = t.init_motion.ks;
        E->MotionKp   = t.init_motion.kp;
        E->MotionKf   = t.init_motion.kf;
        E->CarrierSD  = t.init_carrier.sd;
        E->CarrierSP  = t.init_carrier.sp;
        E->CarrierSR  = t.init_carrier.sr;
        E->CarrierSH  = t.init_carrier.sh;
        if (!t.sources.empty()) {
            const auto& src = t.sources[0];
            E->SourceTypeIdx = (int32)src.type;
            E->SrcCX    = src.center.x;  E->SrcCY = src.center.y;  E->SrcCZ = src.center.z;
            E->SrcPitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(src.direction.z, -1.f, 1.f)));
            E->SrcYaw   = FMath::RadiansToDegrees(FMath::Atan2(src.direction.y, src.direction.x));
            E->SrcSpread = src.spread_deg;
            E->SrcHalfX  = src.area_half_x;
            E->SrcHalfZ  = src.area_half_z;
        }
        TonTypes_.Add(E);
    }
    RebuildTonTypesUI();
}

FReply SGammaTonPanel::OnAddTonTypeClicked()
{
    TonTypes_.Add(MakeShared<FTonTypeEntry>());
    RebuildTonTypesUI();
    return FReply::Handled();
}

void SGammaTonPanel::RebuildTonTypesUI()
{
    if (!TonTypesContainer_.IsValid()) return;

    TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);

    // Compact label+spinbox for 3-per-row layout (short labels like "ks", "sd")
    auto MakeC = [](const FString& Lbl, TSharedRef<SWidget> Ctrl) -> TSharedRef<SWidget> {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(40.f) [ SNew(STextBlock).Text(FText::FromString(Lbl)) ] ]
            + SHorizontalBox::Slot().FillWidth(1.f) [ Ctrl ];
    };

    for (int32 ti = 0; ti < TonTypes_.Num(); ti++) {
        TSharedPtr<FTonTypeEntry> Entry = TonTypes_[ti];

        auto MakeSrcCombo = [this, Entry]() -> TSharedRef<SWidget> {
            int32 CIdx = FMath::Clamp(Entry->SourceTypeIdx, 0, SourceOptions_.Num() - 1);
            return SNew(SComboBox<TSharedPtr<FString>>)
            .OptionsSource(&SourceOptions_)
            .InitiallySelectedItem(SourceOptions_[CIdx])
            .OnSelectionChanged_Lambda([this, Entry](TSharedPtr<FString> Item, ESelectInfo::Type) {
                Entry->SourceTypeIdx = SourceOptions_.Find(Item);
                RefreshVisualizer();
            })
            .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                return SNew(STextBlock).Text(FText::FromString(*Item));
            })
            [
                SNew(STextBlock).Text_Lambda([this, Entry]() {
                    return FText::FromString(*SourceOptions_[FMath::Clamp(Entry->SourceTypeIdx, 0, SourceOptions_.Num()-1)]);
                })
            ];
        };

        // Build the card as a plain SVerticalBox so we can use C++ if-blocks for
        // the collapsible body.
        TSharedRef<SVerticalBox> CardBox = SNew(SVerticalBox);

        // ── Header (always visible) ───────────────────────────────────────────
        CardBox->AddSlot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
            [
                SNew(SButton)
                .Text_Lambda([Entry]() {
                    return FText::FromString(Entry->bCollapsed ? TEXT("▶") : TEXT("▼"));
                })
                .OnClicked_Lambda([this, Entry]() -> FReply {
                    Entry->bCollapsed = !Entry->bCollapsed;
                    RebuildTonTypesUI();
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text_Lambda([Entry, ti]() {
                    return FText::FromString(
                        Entry->Name.IsEmpty()
                        ? FString::Printf(TEXT("Type %d"), ti + 1)
                        : Entry->Name);
                })
                .ColorAndOpacity(FLinearColor(1.f, 1.f, 0.6f, 1.f))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
            [
                SNew(SButton).Text(FText::FromString(TEXT("− Remove")))
                .IsEnabled_Lambda([this]() { return TonTypes_.Num() > 1; })
                .OnClicked_Lambda([this, ti]() -> FReply {
                    if (ti < TonTypes_.Num()) {
                        TonTypes_.RemoveAt(ti);
                        RebuildTonTypesUI();
                        RefreshVisualizer();
                    }
                    return FReply::Handled();
                })
            ]
        ];

        // ── Body (hidden when collapsed) ──────────────────────────────────────
        if (!Entry->bCollapsed)
        {
            // ── Source (맨 위) ────────────────────────────────────────────────
            // ── Source ───────────────────────────────────────────────────────────
            static const TCHAR* SrcTypeDescs[] = {
                TEXT("지정 방향으로 평행하게 발사. DX/DY/DZ = (0,0,-1) 이면 수직 낙하(비·낙진), 수평이면 바람·모래가 됩니다. Spread로 퍼짐 각도를 제어합니다."),
                TEXT("단일 점(CX/CY/CZ)에서 원뿔 형태로 발사. Spread로 원뿔 각도를 제어합니다. 파이프 누수·국소 발생원에 적합합니다."),
                TEXT("모든 방향에서 구 형태로 동시에 발사. HalfX가 구의 반지름입니다. 대기 산화·전방위 환경 풍화에 적합합니다."),
            };

            CardBox->AddSlot().AutoHeight().Padding(0, 2)
            [ SNew(STextBlock).Text(LOCTEXT("SrcLbl", "  Source")).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f)) ];
            CardBox->AddSlot().AutoHeight() [ MakeRow(TEXT("Type"), MakeSrcCombo()) ];

            CardBox->AddSlot().AutoHeight().Padding(0, 4, 0, 1)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("  Position (cm)"))).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f)) ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("X"), SNew(SNumericEntryBox<float>).AllowSpin(true)
                    .MinValue(TOptional<float>()).MaxValue(TOptional<float>())
                    .MinSliderValue(TOptional<float>()).MaxSliderValue(TOptional<float>())
                    .LinearDeltaSensitivity(1).Delta(1.0f).ShiftMultiplier(0.1f).CtrlMultiplier(10.0f)
                    .Value_Lambda([Entry]() { return TOptional<float>(Entry->SrcCX); })
                    .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcCX = v; RefreshVisualizer(); })
                    .OnValueCommitted_Lambda([this, Entry](float v, ETextCommit::Type) { Entry->SrcCX = v; RefreshVisualizer(); })
                    .ToolTipText(LOCTEXT("TipSrcX", "Emission origin X position in world space (cm)."))) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Y"), SNew(SNumericEntryBox<float>).AllowSpin(true)
                    .MinValue(TOptional<float>()).MaxValue(TOptional<float>())
                    .MinSliderValue(TOptional<float>()).MaxSliderValue(TOptional<float>())
                    .LinearDeltaSensitivity(1).Delta(1.0f).ShiftMultiplier(0.1f).CtrlMultiplier(10.0f)
                    .Value_Lambda([Entry]() { return TOptional<float>(Entry->SrcCY); })
                    .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcCY = v; RefreshVisualizer(); })
                    .OnValueCommitted_Lambda([this, Entry](float v, ETextCommit::Type) { Entry->SrcCY = v; RefreshVisualizer(); })
                    .ToolTipText(LOCTEXT("TipSrcY", "Emission origin Y position in world space (cm)."))) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Z"), SNew(SNumericEntryBox<float>).AllowSpin(true)
                    .MinValue(TOptional<float>()).MaxValue(TOptional<float>())
                    .MinSliderValue(TOptional<float>()).MaxSliderValue(TOptional<float>())
                    .LinearDeltaSensitivity(1).Delta(1.0f).ShiftMultiplier(0.1f).CtrlMultiplier(10.0f)
                    .Value_Lambda([Entry]() { return TOptional<float>(Entry->SrcCZ); })
                    .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcCZ = v; RefreshVisualizer(); })
                    .OnValueCommitted_Lambda([this, Entry](float v, ETextCommit::Type) { Entry->SrcCZ = v; RefreshVisualizer(); })
                    .ToolTipText(LOCTEXT("TipSrcZ", "Emission origin Z height in world space (cm). Default 1400 cm places the source well above most actors."))) ]
            ];

            // Direction (Pitch / Yaw) — DIRECTIONAL, POINT only
            CardBox->AddSlot().AutoHeight().Padding(0, 4, 0, 1)
            [
                SNew(SBox)
                .Visibility_Lambda([Entry]() {
                    return (Entry->SourceTypeIdx == 0 || Entry->SourceTypeIdx == 1)
                        ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [ SNew(STextBlock).Text(FText::FromString(TEXT("  Direction (°)"))).ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f)) ]
            ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SBox)
                .Visibility_Lambda([Entry]() {
                    return (Entry->SourceTypeIdx == 0 || Entry->SourceTypeIdx == 1)
                        ? EVisibility::Visible : EVisibility::Collapsed;
                })
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        MakeC(TEXT("Pitch"), SNew(SSpinBox<float>)
                            .MinValue(-90.f).MaxValue(90.f).Delta(1.f)
                            .Value_Lambda([Entry]() { return Entry->SrcPitch; })
                            .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcPitch = v; RefreshVisualizer(); })
                            .ToolTipText(LOCTEXT("TipPitch",
                                "Vertical emission angle.\n"
                                "-90° = straight down  |  0° = horizontal  |  90° = straight up")))
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        MakeC(TEXT("Yaw"), SNew(SSpinBox<float>)
                            .MinValue(-180.f).MaxValue(180.f).Delta(1.f)
                            .Value_Lambda([Entry]() { return Entry->SrcYaw; })
                            .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcYaw = v; RefreshVisualizer(); })
                            .ToolTipText(LOCTEXT("TipYaw",
                                "Horizontal emission rotation.\n"
                                "0° = +X  |  90° = +Y  |  ±180° = -X")))
                    ]
                ]
            ];

            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                // Spread — POINT only (Spot Particle)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox)
                    .Visibility_Lambda([Entry]() {
                        return (Entry->SourceTypeIdx == 1)
                            ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    [
                        MakeC(TEXT("Sprd"), SNew(SSpinBox<float>)
                            .MinValue(0.f).MaxValue(90.f).Delta(1.f)
                            .Value_Lambda([Entry]() { return Entry->SrcSpread; })
                            .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcSpread = v; RefreshVisualizer(); })
                            .ToolTipText(LOCTEXT("TipSprd", "Half-angle of the emission cone in degrees (max 90°).\nUse this to widen the spot from a focused beam to a broad cone.")))
                    ]
                ]
                // HalfX — DIRECTIONAL, ENVIRONMENT 만 해당 (POINT 숨김)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox)
                    .Visibility_Lambda([Entry]() {
                        return (Entry->SourceTypeIdx != 1)
                            ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    [
                        MakeC(TEXT("HalfX"), SNew(SSpinBox<float>)
                            .MinValue(0.f).MaxValue(5000.f).Delta(1.f)
                            .Value_Lambda([Entry]() { return Entry->SrcHalfX; })
                            .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcHalfX = v; RefreshVisualizer(); })
                            .ToolTipText(LOCTEXT("TipHalfX",
                                "Emission area half-width (cm).\n"
                                "· Directional: rectangle half-width\n"
                                "· Sky Particle: sphere radius")))
                    ]
                ]
                // HalfZ — DIRECTIONAL 만 해당
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox)
                    .Visibility_Lambda([Entry]() {
                        return (Entry->SourceTypeIdx == 0)
                            ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    [
                        MakeC(TEXT("HalfZ"), SNew(SSpinBox<float>)
                            .MinValue(0.f).MaxValue(5000.f).Delta(1.f)
                            .Value_Lambda([Entry]() { return Entry->SrcHalfZ; })
                            .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcHalfZ = v; RefreshVisualizer(); })
                            .ToolTipText(LOCTEXT("TipHalfZ",
                                "Emission area half-depth (cm).\n"
                                "· Directional: rectangle half-depth along the perpendicular axis")))
                    ]
                ]
            ];

            // ── Motion (Source 아래) ──────────────────────────────────────────
            CardBox->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
            [ SNew(STextBlock).Text(LOCTEXT("MotLbl", "  Movement Mode")).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f)) ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Scat"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->MotionKs; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->MotionKs = v; })
                    .ToolTipText(LOCTEXT("TipKs", "Probability of a random scatter event. If Scatter + Bounce + Flow < 1, the remainder is the settle probability."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Bnce"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->MotionKp; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->MotionKp = v; })
                    .ToolTipText(LOCTEXT("TipKp", "Probability of a parabolic bounce. The particle arcs through the air under gravity before landing on a new surface."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Flow"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->MotionKf; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->MotionKf = v; })
                    .ToolTipText(LOCTEXT("TipKf", "Probability of surface flow. The particle slides along the surface in the gravity-tangent direction, creating drip or streak effects."))
                )]
            ];

            // ── Carrier ───────────────────────────────────────────────────────
            CardBox->AddSlot().AutoHeight().Padding(0, 4, 0, 2)
            [ SNew(STextBlock).Text(LOCTEXT("CarLbl", "  Particle Carries")).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f)) ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Dust"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSD; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSD = v; })
                    .ToolTipText(LOCTEXT("TipSD", "Amount of dust carried by this particle type. Deposited as a grey/brown surface layer."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Rust"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSP; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSP = v; })
                    .ToolTipText(LOCTEXT("TipSP", "Amount of rust / pigment carried. Deposited as a colored stain and can trigger rust-growth via Material Interactions."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Rough"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSR; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSR = v; })
                    .ToolTipText(LOCTEXT("TipSR", "Roughness deposited on the surface. Higher values make the surface grittier and increase particle capture on subsequent passes."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("Moist"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSH; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSH = v; })
                    .ToolTipText(LOCTEXT("TipSH", "Moisture deposited on the surface. High moisture accelerates rust growth and enables biological effects via Material Interactions."))
                )]
            ];

            // ── Weight (맨 아래) ──────────────────────────────────────────────
            CardBox->AddSlot().AutoHeight().Padding(0, 6, 0, 2)
            [
                MakeRow(TEXT("  Weight"),
                    SNew(SSpinBox<float>)
                    .MinValue(0.f).MaxValue(100.f)
                    .MinSliderValue(0.f).MaxSliderValue(10.f)
                    .Delta(0.1f)
                    .Value_Lambda([Entry]() { return Entry->Weight; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->Weight = FMath::Max(0.f, v); })
                    .ToolTipText(LOCTEXT("TipWeight", "Relative emission weight compared to other particle types. A type with weight 2 emits twice as many particles as one with weight 1."))
                )
            ];

        }

        VBox->AddSlot().AutoHeight().Padding(0, 4)
        [
            SNew(SBorder)
            .BorderBackgroundColor(FLinearColor(0.1f, 0.1f, 0.15f, 1.f))
            .Padding(6.f)
            [ CardBox ]
        ];
    }

    TonTypesContainer_->SetContent(VBox);
}

// ── Per-actor reflectance ─────────────────────────────────────────────────────

FReply SGammaTonPanel::OnRefreshActorsClicked()
{
    TArray<AActor*> Actors;
    for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
        if (AActor* A = Cast<AActor>(*It)) Actors.Add(A);

    // Flush current entries into the persistent map before rebuilding.
    for (auto& E : ActorReflEntries_)
        AllActorSettings_.Add(E->Name, E);

    ActorReflEntries_.Empty();
    for (AActor* A : Actors) {
        FString Name = A->GetActorLabel();  // Outliner 표시 이름 사용
        if (AllActorSettings_.Contains(Name)) {
            ActorReflEntries_.Add(AllActorSettings_[Name]);
        } else {
            auto E = MakeShared<FActorReflEntry>();
            E->Name   = Name;
            E->DeltaS = ReflDeltaS;
            E->DeltaP = ReflDeltaP;
            E->DeltaF = ReflDeltaF;
            AllActorSettings_.Add(Name, E);
            ActorReflEntries_.Add(E);
        }
    }

    RebuildActorReflUI();
    return FReply::Handled();
}

void SGammaTonPanel::RebuildActorReflUI()
{
    if (!ActorReflContainer_.IsValid()) return;

    if (ActorReflEntries_.IsEmpty()) {
        ActorReflContainer_->SetContent(
            SNew(STextBlock)
            .Text(LOCTEXT("NoActors2", "(no actors selected)"))
            .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.f))
        );
        return;
    }

    TSharedRef<SVerticalBox> VBox = SNew(SVerticalBox);
    for (auto& Entry : ActorReflEntries_) {
        VBox->AddSlot().AutoHeight().Padding(0, 3)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("  ") + Entry->Name))
                .ColorAndOpacity(FLinearColor(0.8f, 0.8f, 0.5f, 1.f))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Scatter Fade"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([Entry]() { return Entry->DeltaS; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->DeltaS = v; })
                .ToolTipText(LOCTEXT("TipDeltaS",
                    "How quickly scatter probability fades per bounce on this object.\n"
                    "Higher values make particles settle sooner, concentrating deposits near the source.\n"
                    "0 = no fade (infinite scatter), 1 = settles immediately on first contact.\n"
                    "Recommended: 0.3–0.7"))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Bounce Fade"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([Entry]() { return Entry->DeltaP; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->DeltaP = v; })
                .ToolTipText(LOCTEXT("TipDeltaP",
                    "How quickly bounce probability fades per impact on this object.\n"
                    "Faded bounce converts to surface flow, forming drip streaks.\n"
                    "Higher values create more runoff and fewer hops.\n"
                    "Recommended: 0.0–0.3"))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Flow Fade"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([Entry]() { return Entry->DeltaF; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->DeltaF = v; })
                .ToolTipText(LOCTEXT("TipDeltaF",
                    "How quickly surface flow fades per step on this object.\n"
                    "Higher values produce short, stubby streaks.\n"
                    "0 = flows indefinitely, 1 = stops after a single step.\n"
                    "Recommended: 0.0–0.2"))
            )]
            // ── 초기 재질값 (논문 §4 stain-bleeding) ─────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("InitMatHdr", "    — Pre-existing Weathering —"))
                .ColorAndOpacity(FLinearColor(0.6f, 0.75f, 0.6f, 1.f))
                .ToolTipText(LOCTEXT("InitMatTip",
                    "Weathering already present on the surface before the simulation runs.\n"
                    "For example, setting Starting Rust > 0 lets rust bleed onto adjacent surfaces from the very first step."))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Starting Dust"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSD; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSD = v; })
                .ToolTipText(LOCTEXT("TipInitSD", "Dust already covering this surface. 0 = clean, 1 = fully coated."))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Starting Rust"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSP; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSP = v; })
                .ToolTipText(LOCTEXT("TipInitSP", "Rust already on this surface. Pre-existing rust spreads onto connected surfaces from the first simulation step."))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Starting Roughness"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSR; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSR = v; })
                .ToolTipText(LOCTEXT("TipInitSR", "Surface roughness before simulation. Rougher surfaces capture more particles, accelerating weathering buildup."))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Starting Moisture"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSH; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSH = v; })
                .ToolTipText(LOCTEXT("TipInitSH", "Moisture already on this surface. High starting moisture immediately activates rust growth and biological effects via Material Interactions."))
            )]
        ];
    }
    ActorReflContainer_->SetContent(VBox);
}

TArray<GTGammaReflectance> SGammaTonPanel::BuildPerActorRefl(int32 NumActors) const
{
    TArray<GTGammaReflectance> Out;
    if (ActorReflEntries_.Num() == NumActors) {
        for (const auto& E : ActorReflEntries_) {
            GTGammaReflectance R;
            R.delta_s = E->DeltaS;
            R.delta_p = E->DeltaP;
            R.delta_f = E->DeltaF;
            Out.Add(R);
        }
    } else {
        GTGammaReflectance Global;
        Global.delta_s = ReflDeltaS;
        Global.delta_p = ReflDeltaP;
        Global.delta_f = ReflDeltaF;
        for (int32 i = 0; i < NumActors; i++) Out.Add(Global);
    }
    return Out;
}

// Build initial surfel material values from UI entries (논문 §4 stain-bleeding).
// A non-zero InitSP on a rusted chain actor, for example, lets PICKUP rules
// transport that pigment onto adjacent surfaces from the first iteration.
TArray<GTMaterialProps> SGammaTonPanel::BuildPerActorInitMat(int32 NumActors) const
{
    TArray<GTMaterialProps> Out;
    if (ActorReflEntries_.Num() == NumActors) {
        for (const auto& E : ActorReflEntries_)
            Out.Add({ E->InitSD, E->InitSP, E->InitSR, E->InitSH });
    } else {
        for (int32 i = 0; i < NumActors; i++) Out.Add(GTMaterialProps{});
    }
    return Out;
}

// ── Run ───────────────────────────────────────────────────────────────────────


static const TCHAR* GGTIni = TEXT("GammaTon/Settings");

void SGammaTonPanel::SaveSettings() const
{
    const TCHAR* S = GGTIni;
    // Simulation
    GConfig->SetInt  (S, TEXT("NTonsPerIter"),    NTonsPerIter,         GEditorPerProjectIni);
    GConfig->SetInt  (S, TEXT("NumIterations"),   NumIterations,        GEditorPerProjectIni);
    GConfig->SetInt  (S, TEXT("MaxBounces"),      MaxBounces,           GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("DepositK"),        DepositK,             GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("FlowStep"),        FlowStep,             GEditorPerProjectIni);
    GConfig->SetInt  (S, TEXT("TextureSize"),     TextureSize,          GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("BounceDistance"),  BounceDistance,       GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("ParabolaGravity"), ParabolaGravity,      GEditorPerProjectIni);
    GConfig->SetInt  (S, TEXT("ScenarioIdx"),     ScenarioIdx,          GEditorPerProjectIni);
    // Cross-channel
    GConfig->SetFloat(S, TEXT("CrossRust"),    CrossRustFromHumidity,  GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("CrossDecay"),   CrossHumidityDecay,     GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("CrossPigment"), CrossPigmentCoversDust, GEditorPerProjectIni);
    // Colors
    GConfig->SetString(S, TEXT("DustColor"),    *FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f"), ScenarioDustColor.R,    ScenarioDustColor.G,    ScenarioDustColor.B,    ScenarioDustColor.A),    GEditorPerProjectIni);
    GConfig->SetString(S, TEXT("PigmentColor"), *FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f"), ScenarioPigmentColor.R, ScenarioPigmentColor.G, ScenarioPigmentColor.B, ScenarioPigmentColor.A), GEditorPerProjectIni);
    GConfig->SetFloat (S, TEXT("DustVisibility"), DustVisibility_, GEditorPerProjectIni);
    // Textures
    GConfig->SetString(S, TEXT("DustTexPath"),    DustTexture_    ? *DustTexture_->GetPathName()    : TEXT(""), GEditorPerProjectIni);
    GConfig->SetString(S, TEXT("PigTexPath"),     PigmentTexture_ ? *PigmentTexture_->GetPathName() : TEXT(""), GEditorPerProjectIni);
    GConfig->SetBool  (S, TEXT("bDustUseTex"),    bDustUseTexture_,    GEditorPerProjectIni);
    GConfig->SetBool  (S, TEXT("bPigUseTex"),     bPigmentUseTexture_, GEditorPerProjectIni);
    // Reflectance defaults
    GConfig->SetFloat(S, TEXT("ReflDeltaS"), ReflDeltaS, GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("ReflDeltaP"), ReflDeltaP, GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("ReflDeltaF"), ReflDeltaF, GEditorPerProjectIni);
    // Occluder
    GConfig->SetBool (S, TEXT("bAutoOccluder"),  bAutoOccluder_,      GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("OccluderRadius"), AutoOccluderRadius_, GEditorPerProjectIni);
    // Post-process
    GConfig->SetFloat(S, TEXT("PP_ProbDep"),   PostProcessConfig_.probabilistic_deposit, GEditorPerProjectIni);
    GConfig->SetBool (S, TEXT("PP_UseThr"),    PostProcessConfig_.useThreshold,          GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("PP_Thr"),       PostProcessConfig_.threshold,             GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("PP_Steep"),     PostProcessConfig_.sigmoid_steepness,     GEditorPerProjectIni);
    GConfig->SetBool (S, TEXT("PP_UseNoise"),  PostProcessConfig_.useNoiseMask,          GEditorPerProjectIni);
    GConfig->SetInt  (S, TEXT("PP_Octaves"),   PostProcessConfig_.noise_octaves,         GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("PP_Strength"),  PostProcessConfig_.noise_strength,        GEditorPerProjectIni);
    GConfig->SetFloat(S, TEXT("PP_Scale"),     PostProcessConfig_.noise_scale,           GEditorPerProjectIni);
    GConfig->SetInt  (S, TEXT("PP_Seed"),      (int32)PostProcessConfig_.noise_seed,     GEditorPerProjectIni);
    // TonTypes
    GConfig->SetInt(S, TEXT("TonTypeCount"), TonTypes_.Num(), GEditorPerProjectIni);
    for (int32 i = 0; i < TonTypes_.Num(); i++) {
        const auto& E = *TonTypes_[i];
        FString P = FString::Printf(TEXT("TT%d_"), i);
        GConfig->SetString(S, *(P+TEXT("Name")),    *E.Name,          GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("Wt")),      E.Weight,         GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("Ks")),      E.MotionKs,       GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("Kp")),      E.MotionKp,       GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("Kf")),      E.MotionKf,       GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("SD")),      E.CarrierSD,      GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("SP")),      E.CarrierSP,      GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("SR")),      E.CarrierSR,      GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("SH")),      E.CarrierSH,      GEditorPerProjectIni);
        GConfig->SetInt   (S, *(P+TEXT("SrcType")), E.SourceTypeIdx,  GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("CX")),      E.SrcCX,          GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("CY")),      E.SrcCY,          GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("CZ")),      E.SrcCZ,          GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("Pitch")),    E.SrcPitch,       GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("Yaw")),      E.SrcYaw,         GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("Sprd")),    E.SrcSpread,      GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("HX")),      E.SrcHalfX,       GEditorPerProjectIni);
        GConfig->SetFloat (S, *(P+TEXT("HZ")),      E.SrcHalfZ,       GEditorPerProjectIni);
    }
    GConfig->Flush(false, GEditorPerProjectIni);
}

void SGammaTonPanel::LoadSettings()
{
    const TCHAR* S = GGTIni;
    // Simulation
    GConfig->GetInt  (S, TEXT("NTonsPerIter"),    NTonsPerIter,         GEditorPerProjectIni);
    GConfig->GetInt  (S, TEXT("NumIterations"),   NumIterations,        GEditorPerProjectIni);
    GConfig->GetInt  (S, TEXT("MaxBounces"),      MaxBounces,           GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("DepositK"),        DepositK,             GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("FlowStep"),        FlowStep,             GEditorPerProjectIni);
    GConfig->GetInt  (S, TEXT("TextureSize"),     TextureSize,          GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("BounceDistance"),  BounceDistance,       GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("ParabolaGravity"), ParabolaGravity,      GEditorPerProjectIni);
    GConfig->GetInt  (S, TEXT("ScenarioIdx"),     ScenarioIdx,          GEditorPerProjectIni);
    // Cross-channel
    GConfig->GetFloat(S, TEXT("CrossRust"),    CrossRustFromHumidity,  GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("CrossDecay"),   CrossHumidityDecay,     GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("CrossPigment"), CrossPigmentCoversDust, GEditorPerProjectIni);
    // Colors
    auto ParseColor = [](const FString& Str, FLinearColor& Out) {
        TArray<FString> P; Str.ParseIntoArray(P, TEXT(","));
        if (P.Num() >= 4) Out = FLinearColor(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3]));
    };
    FString DustColorStr, PigColorStr;
    if (GConfig->GetString(S, TEXT("DustColor"),    DustColorStr,  GEditorPerProjectIni)) ParseColor(DustColorStr,  ScenarioDustColor);
    if (GConfig->GetString(S, TEXT("PigmentColor"), PigColorStr,   GEditorPerProjectIni)) ParseColor(PigColorStr,   ScenarioPigmentColor);
    GConfig->GetFloat(S, TEXT("DustVisibility"), DustVisibility_, GEditorPerProjectIni);
    // Textures
    FString DustPath, PigPath;
    GConfig->GetString(S, TEXT("DustTexPath"), DustPath, GEditorPerProjectIni);
    GConfig->GetString(S, TEXT("PigTexPath"),  PigPath,  GEditorPerProjectIni);
    GConfig->GetBool  (S, TEXT("bDustUseTex"), bDustUseTexture_,    GEditorPerProjectIni);
    GConfig->GetBool  (S, TEXT("bPigUseTex"),  bPigmentUseTexture_, GEditorPerProjectIni);
    if (!DustPath.IsEmpty()) DustTexture_    = LoadObject<UTexture2D>(nullptr, *DustPath);
    if (!PigPath.IsEmpty())  PigmentTexture_ = LoadObject<UTexture2D>(nullptr, *PigPath);
    // Reflectance defaults
    GConfig->GetFloat(S, TEXT("ReflDeltaS"), ReflDeltaS, GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("ReflDeltaP"), ReflDeltaP, GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("ReflDeltaF"), ReflDeltaF, GEditorPerProjectIni);
    // Occluder
    GConfig->GetBool (S, TEXT("bAutoOccluder"),  bAutoOccluder_,      GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("OccluderRadius"), AutoOccluderRadius_, GEditorPerProjectIni);
    // Post-process
    GConfig->GetFloat(S, TEXT("PP_ProbDep"),  PostProcessConfig_.probabilistic_deposit, GEditorPerProjectIni);
    GConfig->GetBool (S, TEXT("PP_UseThr"),   PostProcessConfig_.useThreshold,          GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("PP_Thr"),      PostProcessConfig_.threshold,             GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("PP_Steep"),    PostProcessConfig_.sigmoid_steepness,     GEditorPerProjectIni);
    GConfig->GetBool (S, TEXT("PP_UseNoise"), PostProcessConfig_.useNoiseMask,          GEditorPerProjectIni);
    GConfig->GetInt  (S, TEXT("PP_Octaves"),  PostProcessConfig_.noise_octaves,         GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("PP_Strength"), PostProcessConfig_.noise_strength,        GEditorPerProjectIni);
    GConfig->GetFloat(S, TEXT("PP_Scale"),    PostProcessConfig_.noise_scale,           GEditorPerProjectIni);
    int32 NoiseSeed = 42;
    GConfig->GetInt  (S, TEXT("PP_Seed"),     NoiseSeed, GEditorPerProjectIni);
    PostProcessConfig_.noise_seed = (uint32_t)NoiseSeed;
    // TonTypes
    int32 TypeCount = 0;
    if (GConfig->GetInt(S, TEXT("TonTypeCount"), TypeCount, GEditorPerProjectIni) && TypeCount > 0) {
        TonTypes_.Empty();
        for (int32 i = 0; i < TypeCount; i++) {
            auto E = MakeShared<FTonTypeEntry>();
            FString P = FString::Printf(TEXT("TT%d_"), i);
            GConfig->GetString(S, *(P+TEXT("Name")),    E->Name,          GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("Wt")),      E->Weight,        GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("Ks")),      E->MotionKs,      GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("Kp")),      E->MotionKp,      GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("Kf")),      E->MotionKf,      GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("SD")),      E->CarrierSD,     GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("SP")),      E->CarrierSP,     GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("SR")),      E->CarrierSR,     GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("SH")),      E->CarrierSH,     GEditorPerProjectIni);
            GConfig->GetInt   (S, *(P+TEXT("SrcType")), E->SourceTypeIdx, GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("CX")),      E->SrcCX,         GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("CY")),      E->SrcCY,         GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("CZ")),      E->SrcCZ,         GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("Pitch")),    E->SrcPitch,      GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("Yaw")),      E->SrcYaw,        GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("Sprd")),    E->SrcSpread,     GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("HX")),      E->SrcHalfX,      GEditorPerProjectIni);
            GConfig->GetFloat (S, *(P+TEXT("HZ")),      E->SrcHalfZ,      GEditorPerProjectIni);
            TonTypes_.Add(E);
        }
        RebuildTonTypesUI();
        RefreshVisualizer();
    }
}

FReply SGammaTonPanel::OnRunClicked()
{
    TArray<AActor*> Actors;
    for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
        if (AActor* A = Cast<AActor>(*It)) Actors.Add(A);

    if (Actors.IsEmpty()) {
        SetStatus(TEXT("No actors selected."));
        if (ResultText_.IsValid()) {
            ResultText_->SetText(LOCTEXT("ResultNoActors",
                "No actors selected.\nSelect one or more Static Mesh actors in the Outliner, then press Apply Weathering."));
            ResultText_->SetColorAndOpacity(FLinearColor(0.9f, 0.6f, 0.2f, 1.f));
        }
        return FReply::Handled();
    }

    if (bAutoOccluder_)
        AutoPopulateOccluders(Actors);

    TArray<AActor*> OccluderActors;
    for (const auto& Weak : OccluderActors_)
        if (AActor* A = Weak.Get()) OccluderActors.Add(A);

    SetStatus(FString::Printf(TEXT("Extracting %d actor(s)%s..."), Actors.Num(),
        OccluderActors.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" + %d occluder(s)"), OccluderActors.Num())));

    GTRayIntersector           Intersector;
    TArray<GTGammaReflectance> PerActorRefl    = BuildPerActorRefl(Actors.Num());
    TArray<GTMaterialProps>    PerActorInitMat = BuildPerActorInitMat(Actors.Num());

    FGTSceneData Scene = FGammaTonMeshBridge::BuildScene(Actors, OccluderActors, PerActorRefl, PerActorInitMat, Intersector, TextureSize);
    if (!Scene.valid) {
        SetStatus(TEXT("No valid Static Mesh components found."));
        return FReply::Handled();
    }

    // Pre-load existing textures for accumulation.
    // Skip actors whose material changed (no GammaTon MID) — treat them as fresh starts.
    int Preloaded = 0;
    for (int i = 0; i < (int)Scene.textures.size(); i++) {
        UStaticMeshComponent* SMC = Scene.components[i];
        if (!SMC) continue;
        UMaterialInstanceDynamic* PrevMID = Cast<UMaterialInstanceDynamic>(SMC->GetMaterial(0));
        if (PrevMID) {
            UTexture* Dummy = nullptr;
            if (!PrevMID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("AgingTex")), Dummy))
                PrevMID = nullptr;
        }
        if (!PrevMID) continue;  // material changed or first run — start fresh

        // TODO: actorNames 는 현재 Actor->GetName() (내부 이름) 사용 중.
        // TODO: GetActorLabel() (Outliner 표시 이름) 으로 통일하고 저장 경로도 정리 필요.
        FString Safe   = Scene.actorNames[i].Replace(TEXT(" "), TEXT("_"));
        FString TexRef = TEXT("/Game/GammaTon/") + Safe + TEXT("/") + Safe + TEXT("_Dust.") + Safe + TEXT("_Dust");
        if (UTexture2D* Prev = LoadObject<UTexture2D>(nullptr, *TexRef)) {
            FGammaTonTextureBridge::LoadTextureIntoObjTexture(Prev, Scene.textures[i]);
            Preloaded++;
        }
    }

    SetStatus(FString::Printf(TEXT("Scene: %d meshes, %d surfels%s. Simulating..."),
        (int)Scene.meshes.size(), (int)Scene.surfels.size(),
        Preloaded > 0 ? *FString::Printf(TEXT(" (+%d prior)"), Preloaded) : TEXT("")));

    // Build config
    GTSimConfig Config;
    Config.n_tons_per_iter       = NTonsPerIter;
    Config.max_bounces           = MaxBounces;
    Config.flow_step             = FlowStep;
    Config.deposit_k             = DepositK;
    Config.bounce_distance       = BounceDistance;
    Config.parabola_gravity      = ParabolaGravity;
    Config.probabilistic_deposit = PostProcessConfig_.probabilistic_deposit;
    Config.ton_types.clear();
    for (const auto& E : TonTypes_)
        Config.ton_types.push_back(EntryToTonType(*E));
    if (Config.ton_types.empty())
        Config.ton_types.push_back(GTTonType{});

    GTSimulator Sim(Scene.surfels, Intersector, Scene.meshes, Config, &Scene.textures);

    TArray<GTIterationStats> PerIterStats;
    GTIterationStats Total;
    {
        FScopedSlowTask Task(NumIterations,
            LOCTEXT("SimProg", "Running γ-ton simulation..."));
        Task.MakeDialog(true);
        for (int Iter = 0; Iter < NumIterations; Iter++) {
            Task.EnterProgressFrame(1.f,
                FText::Format(LOCTEXT("IterFmt", "Iteration {0}/{1}"), Iter+1, NumIterations));
            if (Task.ShouldCancel()) break;
            GTIterationStats IterStat = Sim.runIteration();
            Total.accumulate(IterStat);
            PerIterStats.Add(IterStat);
            // Cross-channel material interactions (paper §3.4)
            GTCrossChannelRules CCRules;
            CCRules.rust_from_humidity  = CrossRustFromHumidity;
            CCRules.humidity_decay      = CrossHumidityDecay;
            CCRules.pigment_covers_dust = CrossPigmentCoversDust;
            Sim.applyCrossChannel(CCRules);
            // Per-surfel γ-reflectance update — uses each surfel's base_reflectance (paper §3.4)
            Sim.updateReflectance();
        }
    }

    // Smooth out per-iteration shot noise with a Gaussian blur (2 passes of 5×5).
    // Applied before saving so the texture is smooth without altering the deposit total.
    for (auto& Tex : Scene.textures)
        Tex.blur(2);

    // Post-process: clustering and contrast enhancement (Task #3).
    // Order: RD (cluster) → Threshold (contrast) → Voronoi (large-scale patches).
    for (auto& Tex : Scene.textures) {
        if (PostProcessConfig_.useThreshold)
            Tex.applyThresholdSigmoid(PostProcessConfig_.threshold,
                                      PostProcessConfig_.sigmoid_steepness);
        if (PostProcessConfig_.useNoiseMask)
            Tex.applyNoiseMask(PostProcessConfig_.noise_octaves,
                               PostProcessConfig_.noise_seed,
                               PostProcessConfig_.noise_strength,
                               PostProcessConfig_.noise_scale);
    }

    // Snapshot pre-simulation materials for one-level undo
    UndoSnapshot_.Empty();
    for (int i = 0; i < Scene.components.Num(); i++) {
        if (UStaticMeshComponent* SMC = Scene.components[i]) {
            FUndoEntry E;
            E.Component = SMC;
            E.Material  = SMC->GetMaterial(0);
            UndoSnapshot_.Add(E);
        }
    }

    // Save textures and apply materials
    int Applied = 0;
    FString ManifoldExportDir;
    for (int i = 0; i < (int)Scene.textures.size(); i++) {
        FString Name = Scene.actorNames[i];
        UTexture2D* Tex = FGammaTonTextureBridge::CreateAndSaveTexture(Scene.textures[i], Name);
        if (Tex) {
            int UVCh = (i < Scene.atlasUVChannels.Num()) ? Scene.atlasUVChannels[i] : 1;
            FGammaTonTextureBridge::ApplyToComponent(
                Scene.components[i], Tex, Name,
                ScenarioDustColor, ScenarioPigmentColor, UVCh,
                bDustUseTexture_ ? DustTexture_ : nullptr,
                bPigmentUseTexture_ ? PigmentTexture_ : nullptr,
                DustVisibility_);
            Applied++;
        }
        // Export BaseColor / Specular / Roughness PNGs for Manifold
        FString Dir = FGammaTonTextureBridge::ExportManifoldPNGs(
            Scene.textures[i], Scene.meshes[i], Scene.components[i], Name,
            ScenarioDustColor, ScenarioPigmentColor,
            DustVisibility_,
            bDustUseTexture_ ? DustTexture_ : nullptr,
            bPigmentUseTexture_ ? PigmentTexture_ : nullptr);
        if (!Dir.IsEmpty()) ManifoldExportDir = Dir;
    }

    // ── Build & save simulation log ───────────────────────────────────────
    FString LogFilePath;
    {
        static const TCHAR* SrcNames[] = {
            TEXT("AREA_TOP"), TEXT("DIRECTIONAL"), TEXT("POINT"), TEXT("ENVIRONMENT") };
        FDateTime Now = FDateTime::Now();
        FString Log;

        Log += TEXT("=== GammaTon Simulation Log ===\n");
        Log += FString::Printf(TEXT("Timestamp      : %s\n\n"),
            *Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")));

        // Parameters
        Log += TEXT("--- Parameters ---\n");
        Log += FString::Printf(TEXT("Scenario       : %s\n"),    **ScenarioOptions_[ScenarioIdx]);
        Log += FString::Printf(TEXT("γ-tons / iter  : %d\n"),    NTonsPerIter);
        Log += FString::Printf(TEXT("Iterations run : %d / %d\n"), PerIterStats.Num(), NumIterations);
        Log += FString::Printf(TEXT("Max bounces    : %d\n"),    MaxBounces);
        Log += FString::Printf(TEXT("Deposit K      : %.3f\n"),  DepositK);
        Log += FString::Printf(TEXT("Flow step (cm) : %.1f\n"),  FlowStep);
        Log += FString::Printf(TEXT("Texture size   : %d px\n"), TextureSize);
        Log += FString::Printf(TEXT("Bounce dist    : %.1f cm\n"), BounceDistance);
        Log += FString::Printf(TEXT("Parabola grav  : %.3f\n"),  ParabolaGravity);
        Log += FString::Printf(TEXT("Cross rust     : %.4f\n"),  CrossRustFromHumidity);
        Log += FString::Printf(TEXT("Cross sh decay : %.4f\n"),  CrossHumidityDecay);
        Log += FString::Printf(TEXT("Cross sp->sd   : %.4f\n\n"), CrossPigmentCoversDust);

        // Ton Types
        Log += TEXT("--- Ton Types ---\n");
        for (int32 ti = 0; ti < TonTypes_.Num(); ti++) {
            const auto& E = *TonTypes_[ti];
            Log += FString::Printf(TEXT("[Type %d: %s]  weight=%.2f\n"), ti + 1, *E.Name, E.Weight);
            Log += FString::Printf(TEXT("  Motion : ks=%.3f  kp=%.3f  kf=%.3f\n"),
                E.MotionKs, E.MotionKp, E.MotionKf);
            Log += FString::Printf(TEXT("  Carrier: sd=%.3f  sp=%.3f  sr=%.3f  sh=%.3f\n"),
                E.CarrierSD, E.CarrierSP, E.CarrierSR, E.CarrierSH);
            int32 SrcIdx = FMath::Clamp(E.SourceTypeIdx, 0, 3);
            {
                float Pr = FMath::DegreesToRadians(E.SrcPitch);
                float Yr = FMath::DegreesToRadians(E.SrcYaw);
                float DX = FMath::Cos(Pr)*FMath::Cos(Yr);
                float DY = FMath::Cos(Pr)*FMath::Sin(Yr);
                float DZ = FMath::Sin(Pr);
                Log += FString::Printf(
                    TEXT("  Source : %s  center=(%.0f,%.0f,%.0f)  pitch=%.1f yaw=%.1f  dir=(%.2f,%.2f,%.2f)  spread=%.1f deg\n"),
                    SrcNames[SrcIdx],
                    E.SrcCX, E.SrcCY, E.SrcCZ,
                    E.SrcPitch, E.SrcYaw, DX, DY, DZ, E.SrcSpread);
            }
        }
        Log += TEXT("\n");

        // Per-Actor Settings
        Log += TEXT("--- Per-Actor Settings ---\n");
        if (ActorReflEntries_.Num() == Actors.Num()) {
            for (const auto& E : ActorReflEntries_) {
                Log += FString::Printf(TEXT("[%s]\n"), *E->Name);
                Log += FString::Printf(TEXT("  Ds=%.3f  Dp=%.3f  Df=%.3f\n"),
                    E->DeltaS, E->DeltaP, E->DeltaF);
                Log += FString::Printf(TEXT("  Init: sd=%.3f  sp=%.3f  sr=%.3f  sh=%.3f\n"),
                    E->InitSD, E->InitSP, E->InitSR, E->InitSH);
            }
        } else {
            Log += FString::Printf(TEXT("Global (fallback): Ds=%.3f  Dp=%.3f  Df=%.3f\n"),
                ReflDeltaS, ReflDeltaP, ReflDeltaF);
        }
        Log += TEXT("\n");

        // Per-Iteration Stats
        Log += TEXT("--- Per-Iteration Stats ---\n");
        Log += FString::Printf(TEXT("%-6s  %-8s  %-8s  %-10s  %-8s  %-8s  %-8s  %-8s\n"),
            TEXT("Iter"), TEXT("Settled"), TEXT("Escaped"), TEXT("AvgBounce"),
            TEXT("Reflect"), TEXT("Bounce"), TEXT("Flow"), TEXT("Settle"));
        for (int32 i = 0; i < PerIterStats.Num(); i++) {
            const GTIterationStats& S = PerIterStats[i];
            Log += FString::Printf(TEXT("%-6d  %-8d  %-8d  %-10.2f  %-8d  %-8d  %-8d  %-8d\n"),
                i + 1, S.settled, S.escaped, S.avgBounces(),
                S.ev_reflect, S.ev_bounce, S.ev_flow, S.ev_settle);
        }
        Log += TEXT("\n");

        // Summary
        Log += TEXT("--- Summary ---\n");
        Log += FString::Printf(TEXT("Total settled  : %d\n"),   Total.settled);
        Log += FString::Printf(TEXT("Total escaped  : %d\n"),   Total.escaped);
        Log += FString::Printf(TEXT("Avg bounces    : %.2f\n"), Total.avgBounces());
        Log += FString::Printf(TEXT("Events         : reflect=%d  bounce=%d  flow=%d  settle=%d\n"),
            Total.ev_reflect, Total.ev_bounce, Total.ev_flow, Total.ev_settle);
        Log += FString::Printf(TEXT("Actors applied : %d\n"),   Applied);

        FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GammaTonLogs"));
        IFileManager::Get().MakeDirectory(*Dir, true);
        LogFilePath = FPaths::Combine(Dir,
            FString::Printf(TEXT("GT_%s.txt"), *Now.ToString(TEXT("%Y%m%d_%H%M%S"))));
        FFileHelper::SaveStringToFile(Log, *LogFilePath,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    // 결과 요약 — 상단 카드에 표시
    if (ResultText_.IsValid()) {
        ResultText_->SetText(FText::FromString(FString::Printf(
            TEXT("✓  %d / %d iters  ·  %d actor(s) applied\n"
                 "Settled %d  ·  Escaped %d  ·  Avg %.1f bounces"),
            PerIterStats.Num(), NumIterations, Applied,
            Total.settled, Total.escaped, Total.avgBounces())));
        ResultText_->SetColorAndOpacity(FLinearColor(0.4f, 0.85f, 0.5f, 1.f));
    }

    SetStatus(FString::Printf(
        TEXT("Done!  %d/%d iters | Settled: %d | Escaped: %d | Avg bounces: %.2f\n"
             "reflect=%d  bounce=%d  flow=%d  settle=%d\n"
             "Textures saved to /Game/GammaTon/ (%d actors).\n"
             "Manifold PNGs: %s\n"
             "Log: %s"),
        PerIterStats.Num(), NumIterations,
        Total.settled, Total.escaped, Total.avgBounces(),
        Total.ev_reflect, Total.ev_bounce, Total.ev_flow, Total.ev_settle,
        Applied,
        ManifoldExportDir.IsEmpty() ? TEXT("(export failed)") : *ManifoldExportDir,
        *LogFilePath));

    return FReply::Handled();
}

// ── Undo Last Simulation ──────────────────────────────────────────────────────

FReply SGammaTonPanel::OnUndoClicked()
{
    if (UndoSnapshot_.IsEmpty()) {
        SetStatus(TEXT("Nothing to undo — run simulation first."));
        if (ResultText_.IsValid()) {
            ResultText_->SetText(LOCTEXT("ResultNoUndo", "↩  Nothing to undo."));
            ResultText_->SetColorAndOpacity(FLinearColor(0.7f, 0.5f, 0.3f, 1.f));
        }
        return FReply::Handled();
    }

    int Restored = 0;
    for (auto& E : UndoSnapshot_) {
        if (E.Component.IsValid()) {
            E.Component->SetMaterial(0, E.Material.Get());
            Restored++;
        }
    }
    UndoSnapshot_.Empty();

    if (ResultText_.IsValid()) {
        ResultText_->SetText(FText::FromString(
            FString::Printf(TEXT("↩  Reverted %d actor(s) to pre-simulation material."), Restored)));
        ResultText_->SetColorAndOpacity(FLinearColor(0.75f, 0.6f, 0.35f, 1.f));
    }
    SetStatus(FString::Printf(TEXT("Reverted %d actor(s) to pre-simulation material."), Restored));
    return FReply::Handled();
}

void SGammaTonPanel::SetStatus(const FString& Msg) {
    StatusMsg_ = Msg;
    UE_LOG(LogTemp, Log, TEXT("[GammaTon] %s"), *Msg);
}

// ── Trace Single Ray ──────────────────────────────────────────────────────────

FReply SGammaTonPanel::OnTraceRayClicked()
{
    TArray<AActor*> Actors;
    for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
        if (AActor* A = Cast<AActor>(*It)) Actors.Add(A);

    if (Actors.IsEmpty()) {
        SetStatus(TEXT("No actors selected. Select an actor first."));
        return FReply::Handled();
    }

    if (bAutoOccluder_)
        AutoPopulateOccluders(Actors);

    TArray<AActor*> OccluderActors;
    for (const auto& Weak : OccluderActors_)
        if (AActor* A = Weak.Get()) OccluderActors.Add(A);

    GTRayIntersector           Intersector;
    TArray<GTGammaReflectance> PerActorRefl    = BuildPerActorRefl(Actors.Num());
    TArray<GTMaterialProps>    PerActorInitMat = BuildPerActorInitMat(Actors.Num());

    FGTSceneData Scene = FGammaTonMeshBridge::BuildScene(Actors, OccluderActors, PerActorRefl, PerActorInitMat, Intersector, TextureSize);
    if (!Scene.valid) {
        SetStatus(TEXT("No valid Static Mesh components found."));
        return FReply::Handled();
    }

    // Load prior textures so tex_before values reflect accumulated state.
    for (int i = 0; i < (int)Scene.textures.size(); i++) {
        FString Safe   = Scene.actorNames[i].Replace(TEXT(" "), TEXT("_"));
        FString TexRef = TEXT("/Game/GammaTon/") + Safe + TEXT("/") + Safe + TEXT("_Dust.") + Safe + TEXT("_Dust");
        if (UTexture2D* Prev = LoadObject<UTexture2D>(nullptr, *TexRef))
            FGammaTonTextureBridge::LoadTextureIntoObjTexture(Prev, Scene.textures[i]);
    }

    GTSimConfig Config;
    Config.n_tons_per_iter  = NTonsPerIter;
    Config.max_bounces      = MaxBounces;
    Config.flow_step        = FlowStep;
    Config.deposit_k        = DepositK;
    Config.bounce_distance  = BounceDistance;
    Config.parabola_gravity = ParabolaGravity;
    Config.ton_types.clear();
    for (const auto& E : TonTypes_)
        Config.ton_types.push_back(EntryToTonType(*E));
    if (Config.ton_types.empty())
        Config.ton_types.push_back(GTTonType{});

    GTSimulator Sim(Scene.surfels, Intersector, Scene.meshes, Config, &Scene.textures);
    GTRayPath   Path = Sim.traceTonDebug();

    if (RayVisualizer_) RayVisualizer_->SetPath(Path);

    // Build detailed log output
    const FString Sep  = TEXT("  ------------------------------------------------------------\n");
    const FString Sep2 = TEXT("============================================================\n");

    FString RayLog;
    RayLog += Sep2;
    RayLog += TEXT("  GammaTon Debug Ray\n");
    RayLog += FString::Printf(TEXT("  Origin : (%8.1f, %8.1f, %8.1f)\n"),
        Path.origin.x, Path.origin.y, Path.origin.z);
    RayLog += Sep2;
    RayLog += TEXT("\n");

    for (int i = 0; i < (int)Path.hits.size(); i++) {
        const GTRayHitRecord& R = Path.hits[i];
        FString EvName = FString(ANSI_TO_TCHAR(GTBounceEventName(R.event)));

        float DotDirN = R.dir_in.dot(R.normal);
        bool  bBackFace = DotDirN > 0.0f;
        FString FaceTag = bBackFace ? TEXT("[!! BACKFACE !!]") : TEXT("[front]");

        RayLog += FString::Printf(TEXT("  [Bounce %d]  %s\n"), i + 1, *EvName);
        RayLog += FString::Printf(TEXT("  pos    : (%8.1f, %8.1f, %8.1f)\n"),
            R.position.x, R.position.y, R.position.z);
        RayLog += FString::Printf(TEXT("  N_geom : (%7.3f, %7.3f, %7.3f)\n"),
            R.normal.x, R.normal.y, R.normal.z);
        RayLog += FString::Printf(TEXT("  N_shade: (%7.3f, %7.3f, %7.3f)  dot(dir,N)=%+.3f  %s\n"),
            R.shading_normal.x, R.shading_normal.y, R.shading_normal.z,
            DotDirN, *FaceTag);
        RayLog += FString::Printf(TEXT("  dir_in : (%7.3f, %7.3f, %7.3f)\n"),
            R.dir_in.x, R.dir_in.y, R.dir_in.z);

        if (R.event == GTBounceEvent::Reflect ||
            R.event == GTBounceEvent::Diffuse  ||
            R.event == GTBounceEvent::Flow) {
            RayLog += FString::Printf(TEXT("  dir_out: (%7.3f, %7.3f, %7.3f)\n"),
                R.dir_out.x, R.dir_out.y, R.dir_out.z);
        }

        RayLog += FString::Printf(TEXT("  UV     : (%.4f, %.4f)\n"), R.uv.x, R.uv.y);
        RayLog += FString::Printf(TEXT("  tex    : sd=%.4f  sp=%.4f  sr=%.4f  sh=%.4f\n"),
            R.tex_before.sd, R.tex_before.sp, R.tex_before.sr, R.tex_before.sh);
        RayLog += FString::Printf(TEXT("  motion : ks %.3f->%.3f  kp %.3f->%.3f  kf %.3f->%.3f\n"),
            R.motion_before.ks, R.motion_after.ks,
            R.motion_before.kp, R.motion_after.kp,
            R.motion_before.kf, R.motion_after.kf);

        if (R.event == GTBounceEvent::Settle || R.event == GTBounceEvent::Flow) {
            RayLog += FString::Printf(TEXT("  deposit: sd+%.4f  sp+%.4f  sr+%.4f  sh+%.4f\n"),
                R.deposit.sd, R.deposit.sp, R.deposit.sr, R.deposit.sh);
        }

        RayLog += Sep;
    }

    int NumBounces = (int)Path.hits.size();
    RayLog += Sep2;
    RayLog += Path.escaped
        ? FString::Printf(TEXT("  RESULT : Escaped  (%d bounces)\n"), NumBounces)
        : FString::Printf(TEXT("  RESULT : Settled  (%d bounces)\n"), NumBounces);
    RayLog += Sep2;

    UE_LOG(LogTemp, Display, TEXT("%s"), *RayLog);
    SetStatus(RayLog);

    return FReply::Handled();
}

// ── Auto occluder population ──────────────────────────────────────────────────

void SGammaTonPanel::AutoPopulateOccluders(const TArray<AActor*>& Targets)
{
    if (Targets.IsEmpty()) return;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    // Compute combined AABB of all target actors
    FBox TargetBounds(EForceInit::ForceInit);
    for (AActor* A : Targets)
        TargetBounds += A->GetComponentsBoundingBox(true);
    FVector Center = TargetBounds.GetCenter();

    int32 Added = 0;
    for (TActorIterator<AActor> It(World); It; ++It) {
        AActor* Candidate = *It;
        if (Targets.Contains(Candidate)) continue;
        if (!Candidate->FindComponentByClass<UStaticMeshComponent>()) continue;

        bool bAlready = false;
        for (const auto& Weak : OccluderActors_)
            if (Weak.Get() == Candidate) { bAlready = true; break; }
        if (bAlready) continue;

        FBox CandBounds = Candidate->GetComponentsBoundingBox(true);
        // Distance from center to the nearest point on the candidate AABB
        float Dist = FVector::Dist(Center, CandBounds.GetClosestPointTo(Center));
        if (Dist <= AutoOccluderRadius_) {
            OccluderActors_.Add(Candidate);
            Added++;
        }
    }

}


#undef LOCTEXT_NAMESPACE
