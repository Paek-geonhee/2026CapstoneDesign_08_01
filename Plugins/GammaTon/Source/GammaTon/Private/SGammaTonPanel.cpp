#include "SGammaTonPanel.h"
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
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Editor.h"
#include "Selection.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"

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
      3, 0,0,0, 0,0,-1, 0,800,800,
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
      1, -1000,0,600, 1,0,-0.15f, 6,700,700,
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
      2, 0,0,350, 0,0,-1, 10,0,0,
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
      1, -1200,0,700, 1,0,-0.1f, 10,600,600,
      0.008f, 0.1f, 0.0f,
      FLinearColor(0.90f,0.88f,0.84f,1), FLinearColor(0.82f,0.80f,0.76f,1) },
};

static const int GNumScenarios = UE_ARRAY_COUNT(GScenarios);

// ── Destructor ────────────────────────────────────────────────────────────────

SGammaTonPanel::~SGammaTonPanel()
{
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
    return SNew(SNumericEntryBox<float>)
        .AllowSpin(false)
        .Value_Lambda([&Val]() { return TOptional<float>(Val); })
        .OnValueCommitted_Lambda([this, &Val](float v, ETextCommit::Type) { Val = v; RefreshVisualizer(); });
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
        MakeShared<FString>(TEXT("AREA_TOP (rainfall / fallout)")),
        MakeShared<FString>(TEXT("DIRECTIONAL (wind / parallel)")),
        MakeShared<FString>(TEXT("POINT (pipe drip / spotlight)")),
        MakeShared<FString>(TEXT("ENVIRONMENT (omnidirectional sphere)")),
    };
    for (int i = 0; i < GNumScenarios; i++)
        ScenarioOptions_.Add(MakeShared<FString>(GScenarios[i].Name));

    ChildSlot
    [
        SNew(SBox).MinDesiredWidth(440.f)
        [
            SNew(SScrollBox)

            // ── Scenario ──────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("ScnHeader", "── Scenario Preset ────────────────────")) ]
            + SScrollBox::Slot().Padding(12, 2)
            [
                MakeRow(TEXT("Scenario"),
                    SNew(SComboBox<TSharedPtr<FString>>)
                    .OptionsSource(&ScenarioOptions_)
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type) {
                        ScenarioIdx = ScenarioOptions_.Find(Item);
                        ApplyScenario(ScenarioIdx);
                    })
                    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
                        return SNew(STextBlock)
                            .Font(GGetKorFont(9))
                            .Text(FText::FromString(*Item));
                    })
                    .InitiallySelectedItem(ScenarioOptions_[0])
                    [
                        SNew(STextBlock)
                        .Font(GGetKorFont(9))
                        .Text_Lambda([this]() {
                            return FText::FromString(*ScenarioOptions_[ScenarioIdx]);
                        })
                    ]
                )
            ]
            // ── Scenario description panel ─────────────────────────────────────
            + SScrollBox::Slot().Padding(12, 4, 12, 8)
            [
                SNew(SBorder)
                .BorderBackgroundColor(FLinearColor(0.08f, 0.10f, 0.14f, 1.f))
                .Padding(FMargin(10.f, 7.f))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                    [
                        SNew(STextBlock)
                        .Font(GGetKorFont(8))
                        .ColorAndOpacity(FLinearColor(0.55f, 0.75f, 1.0f, 1.f))
                        .Text_Lambda([this]() {
                            int32 Idx = FMath::Clamp(ScenarioIdx, 0, GNumScenarios - 1);
                            return FText::FromString(GScenarios[Idx].Tags);
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Font(GGetKorFont(9))
                        .ColorAndOpacity(FLinearColor(0.80f, 0.80f, 0.80f, 1.f))
                        .AutoWrapText(true)
                        .Text_Lambda([this]() {
                            int32 Idx = FMath::Clamp(ScenarioIdx, 0, GNumScenarios - 1);
                            return FText::FromString(GScenarios[Idx].Desc);
                        })
                    ]
                ]
            ]

            // ── Simulation ────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("SimHeader", "── Simulation ───────────────────────────")) ]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("γ-tons / iter"),
                SNew(SSpinBox<int32>).MinValue(1).MaxValue(10000000)
                .Value_Lambda([this]() { return NTonsPerIter; })
                .OnValueChanged_Lambda([this](int32 v) { NTonsPerIter = v; })
                .ToolTipText(LOCTEXT("TipNTons",
                    "매 이터레이션마다 발사하는 γ-ton 수.\n높을수록 결과가 정밀하지만 연산 시간 증가."))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("Iterations"),
                SNew(SSpinBox<int32>).MinValue(1).MaxValue(10000000)
                .Value_Lambda([this]() { return NumIterations; })
                .OnValueChanged_Lambda([this](int32 v) { NumIterations = v; })
                .ToolTipText(LOCTEXT("TipIter",
                    "시뮬레이션 반복 횟수.\n높을수록 풍화 효과가 누적되어 강해짐."))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("Max bounces"),
                SNew(SSpinBox<int32>).MinValue(1).MaxValue(10000000)
                .Value_Lambda([this]() { return MaxBounces; })
                .OnValueChanged_Lambda([this](int32 v) { MaxBounces = v; })
                .ToolTipText(LOCTEXT("TipBounce",
                    "γ-ton 하나가 최대 반사/이동할 수 있는 횟수.\n초과 시 이탈(escape) 처리됨."))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("Deposit K"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(100000.f)
                .Value_Lambda([this]() { return DepositK; })
                .OnValueChanged_Lambda([this](float v) { DepositK = v; })
                .ToolTipText(LOCTEXT("TipDeposit",
                    "침착 강도 (전역 스케일).\n높을수록 한 번의 settle/flow에 더 많은 재질이 쌓임."))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("Flow step (cm)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(100000.f)
                .Value_Lambda([this]() { return FlowStep; })
                .OnValueChanged_Lambda([this](float v) { FlowStep = v; })
                .ToolTipText(LOCTEXT("TipFlow",
                    "표면 흐름(kf) 이벤트 시 이동 거리 (cm).\n클수록 흐름 자국이 길게 형성됨."))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("Texture size (px)"),
                SNew(SSpinBox<int32>).MinValue(1).MaxValue(10000000)
                .Value_Lambda([this]() { return TextureSize; })
                .OnValueChanged_Lambda([this](int32 v) { TextureSize = v; })
                .ToolTipText(LOCTEXT("TipTex",
                    "결과 텍스처 해상도 (픽셀).\n512~2048 권장. 높을수록 세밀하지만 VRAM 사용량 증가."))
            )]

            // ── Ton Types (P2-D) ──────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("TonTypesHdr", "── Ton Types (paper §2-D) ──────────────────")) ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("AddType", "+ Add Ton Type"))
                .OnClicked(this, &SGammaTonPanel::OnAddTonTypeClicked)
            ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SAssignNew(TonTypesContainer_, SBox)
            ]

            // ── Physics ───────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("PhysHdr", "── Physics ──────────────────────────────")) ]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("Bounce dist. (cm)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(100000.f)
                .Value_Lambda([this]() { return BounceDistance; })
                .OnValueChanged_Lambda([this](float v) { BounceDistance = v; })
                .ToolTipText(LOCTEXT("TipBDist",
                    "kp 포물선 반사의 최대 이동 거리 (cm).\n클수록 γ-ton이 더 멀리 튀어나감."))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("Parabola gravity"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(100000.f)
                .Value_Lambda([this]() { return ParabolaGravity; })
                .OnValueChanged_Lambda([this](float v) { ParabolaGravity = v; })
                .ToolTipText(LOCTEXT("TipGrav",
                    "kp 포물선 궤적의 중력 강도.\n0.5 = 완만한 호 / 2.0+ = 급격한 낙하."))
            )]

            // ── Cross-Channel ─────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("CrsHdr", "── Cross-Channel (per iteration) ────────")) ]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("sh→sr  (rust rate)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(100000.f)
                .Value_Lambda([this]() { return CrossRustFromHumidity; })
                .OnValueChanged_Lambda([this](float v) { CrossRustFromHumidity = v; })
                .ToolTipText(LOCTEXT("TipRust",
                    "습도(sh)가 녹(sr)을 촉진하는 비율.\n매 이터레이션: sr += 값 × sh\n논문 §3.4 예시: 0.015"))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("sh decay"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(100000.f)
                .Value_Lambda([this]() { return CrossHumidityDecay; })
                .OnValueChanged_Lambda([this](float v) { CrossHumidityDecay = v; })
                .ToolTipText(LOCTEXT("TipDecay",
                    "매 이터레이션마다 습도(sh)가 증발하는 비율.\n0 = 증발 없음 / 1.0 = 즉시 완전 증발.\n논문 §3.4 예시: 0.5"))
            )]
            + SScrollBox::Slot().Padding(12, 2)
            [ MakeRow(TEXT("sp covers sd  (moss)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(100000.f)
                .Value_Lambda([this]() { return CrossPigmentCoversDust; })
                .OnValueChanged_Lambda([this](float v) { CrossPigmentCoversDust = v; })
                .ToolTipText(LOCTEXT("TipMoss",
                    "색소(sp, 이끼 등)가 먼지(sd)를 덮는 비율.\n매 이터레이션: sd -= 값 × sp\n논문 §3.4 예시: 0.1"))
            )]

            // ── Per-Actor γ-Reflectance ───────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("PerActorHdr", "── Per-Actor γ-Reflectance ─────────────────")) ]
            + SScrollBox::Slot().Padding(8, 2)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("RefreshActors", "Refresh Actor List from Selection"))
                .OnClicked(this, &SGammaTonPanel::OnRefreshActorsClicked)
            ]
            + SScrollBox::Slot().Padding(12, 2)
            [
                SAssignNew(ActorReflContainer_, SBox)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("NoActors", "(Click Refresh to populate from current selection)"))
                    .ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.f))
                ]
            ]

            // ── Colors ───────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("ColHdr", "── Colors ───────────────────────────────────")) ]
            + SScrollBox::Slot().Padding(12, 2)
            [
                MakeRow(TEXT("Dust Color"),
                    SNew(SBox).HeightOverride(20.f)
                    [
                        SNew(SColorBlock)
                        .Color_Lambda([this]() { return ScenarioDustColor; })
                        .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& E) -> FReply {
                            if (E.GetEffectingButton() == EKeys::LeftMouseButton) {
                                FColorPickerArgs Args;
                                Args.bIsModal         = true;
                                Args.bUseAlpha        = false;
                                Args.InitialColor     = ScenarioDustColor;
                                Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
                                    [this](FLinearColor C) { ScenarioDustColor = C; });
                                OpenColorPicker(Args);
                            }
                            return FReply::Handled();
                        })
                        .ToolTipText(LOCTEXT("TipDustCol",
                            "먼지/소일(sd) 색상.\n"
                            "sd 값이 높은 픽셀일수록 이 색으로 Lerp됨.\n"
                            "클릭하면 색상 피커가 열림."))
                    ]
                )
            ]
            + SScrollBox::Slot().Padding(12, 2)
            [
                MakeRow(TEXT("Pigment Color"),
                    SNew(SBox).HeightOverride(20.f)
                    [
                        SNew(SColorBlock)
                        .Color_Lambda([this]() { return ScenarioPigmentColor; })
                        .OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent& E) -> FReply {
                            if (E.GetEffectingButton() == EKeys::LeftMouseButton) {
                                FColorPickerArgs Args;
                                Args.bIsModal         = true;
                                Args.bUseAlpha        = false;
                                Args.InitialColor     = ScenarioPigmentColor;
                                Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
                                    [this](FLinearColor C) { ScenarioPigmentColor = C; });
                                OpenColorPicker(Args);
                            }
                            return FReply::Handled();
                        })
                        .ToolTipText(LOCTEXT("TipPigCol",
                            "안료/이끼/녹(sp) 색상.\n"
                            "sp 값이 높은 픽셀일수록 이 색으로 Lerp됨.\n"
                            "클릭하면 색상 피커가 열림."))
                    ]
                )
            ]

            // ── Detail Textures ───────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 8, 8, 2)
            [ SNew(STextBlock).Text(LOCTEXT("TexHdr", "── Detail Textures (optional) ──────────────")) ]
            + SScrollBox::Slot().Padding(12, 2)
            [
                MakeRow(TEXT("Dust Texture"),
                    SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UTexture2D::StaticClass())
                    .AllowClear(true)
                    .ObjectPath_Lambda([this]() {
                        return DustTexture_ ? DustTexture_->GetPathName() : FString();
                    })
                    .OnObjectChanged_Lambda([this](const FAssetData& Data) {
                        DustTexture_ = Cast<UTexture2D>(Data.GetAsset());
                    })
                    .ToolTipText(LOCTEXT("TipDustTex",
                        "먼지/소일 색상에 곱해지는 디테일 텍스처.\n"
                        "비워두면 단색(DustColor)으로만 표현됨."))
                )
            ]
            + SScrollBox::Slot().Padding(12, 2)
            [
                MakeRow(TEXT("Pigment Texture"),
                    SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UTexture2D::StaticClass())
                    .AllowClear(true)
                    .ObjectPath_Lambda([this]() {
                        return PigmentTexture_ ? PigmentTexture_->GetPathName() : FString();
                    })
                    .OnObjectChanged_Lambda([this](const FAssetData& Data) {
                        PigmentTexture_ = Cast<UTexture2D>(Data.GetAsset());
                    })
                    .ToolTipText(LOCTEXT("TipPigTex",
                        "이끼/녹/얼룩 색상에 곱해지는 디테일 텍스처.\n"
                        "비워두면 단색(PigmentColor)으로만 표현됨."))
                )
            ]

            // ── Dust Visibility ───────────────────────────────────────────────
            + SScrollBox::Slot().Padding(12, 6, 12, 2)
            [
                MakeRow(TEXT("Dust Visibility"),
                    SNew(SSpinBox<float>)
                    .MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                    .Value_Lambda([this]() { return DustVisibility_; })
                    .OnValueChanged_Lambda([this](float V) { DustVisibility_ = V; })
                    .ToolTipText(LOCTEXT("TipDustVis",
                        "풍화 색조 강도 (0 = 색상 변화 없음, 1 = 완전 적용).\n"
                        "시뮬레이션 결과(sd/sp)에 곱해지는 전역 스케일."))
                )
            ]

            // ── Run ───────────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 14, 8, 4)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("RunBtn", "▶  Run GammaTon Simulation"))
                .OnClicked(this, &SGammaTonPanel::OnRunClicked)
            ]

            // ── Undo ─────────────────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 2, 8, 4)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("UndoBtn", "↩  Undo Last Simulation"))
                .OnClicked(this, &SGammaTonPanel::OnUndoClicked)
                .ToolTipText(LOCTEXT("TipUndo",
                    "마지막 Run 직전 상태로 머티리얼을 되돌립니다.\n"
                    "Run을 한 번 실행한 뒤에만 동작합니다."))
            ]

            // ── Trace Single Ray ───────────────────────────────────────────────
            + SScrollBox::Slot().Padding(8, 2, 8, 4)
            [
                SNew(SButton).HAlign(HAlign_Center)
                .Text(LOCTEXT("TraceBtn", "Trace Single Ray (Debug Visualize)"))
                .OnClicked(this, &SGammaTonPanel::OnTraceRayClicked)
            ]
            + SScrollBox::Slot().Padding(8, 8, 8, 4)
            [
                SNew(SBox).HeightOverride(220.f)
                [
                    SNew(SBorder)
                    .BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.05f, 1.f))
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot().Padding(6)
                        [
                            SAssignNew(StatusText_, SMultiLineEditableText)
                            .IsReadOnly(true)
                            .AutoWrapText(false)
                            .Text(FText::FromString(StatusMsg_))
                        ]
                    ]
                ]
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
}

// ── Ton Types ─────────────────────────────────────────────────────────────────

GTGammaSource SGammaTonPanel::EntryToSource(const FTonTypeEntry& e) const
{
    GTGammaSource Src;
    Src.type        = (GTSourceType)e.SourceTypeIdx;
    Src.center      = { e.SrcCX, e.SrcCY, e.SrcCZ };
    Src.direction   = GTVec3{ e.SrcDX, e.SrcDY, e.SrcDZ }.normalized();
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
            E->SrcCX = src.center.x;     E->SrcCY = src.center.y;    E->SrcCZ = src.center.z;
            E->SrcDX = src.direction.x;  E->SrcDY = src.direction.y; E->SrcDZ = src.direction.z;
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
            [ SNew(SBox).WidthOverride(28.f) [ SNew(STextBlock).Text(FText::FromString(Lbl)) ] ]
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
            // Card collapse toggle ▼/▶
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
            // Type name (yellow)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
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
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
            [ SNew(STextBlock).Text(LOCTEXT("WtLbl", "Weight:")) ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [
                SNew(SSpinBox<float>)
                .MinValue(0.f).MaxValue(100.f)
                .Value_Lambda([Entry]() { return Entry->Weight; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->Weight = v; })
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(4, 0)
            [
                SNew(SButton).Text(FText::FromString(TEXT("− Remove")))
                .OnClicked_Lambda([this, ti]() -> FReply {
                    if (ti < TonTypes_.Num()) {
                        TonTypes_.RemoveAt(ti);
                        RebuildTonTypesUI();
                    }
                    return FReply::Handled();
                })
            ]
        ];

        // ── Body (hidden when collapsed) ──────────────────────────────────────
        if (!Entry->bCollapsed)
        {
            // Motion: ks / kp / kf
            CardBox->AddSlot().AutoHeight().Padding(0, 2)
            [ SNew(STextBlock).Text(LOCTEXT("MotLbl", "  Motion  ks / kp / kf")).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f)) ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("ks"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->MotionKs; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->MotionKs = v; })
                    .ToolTipText(LOCTEXT("TipKs", "반구 반사 확률 (ks)\n표면에 닿으면 임의 방향으로 반사됨.\nks + kp + kf < 1 이면 나머지 확률로 정착(settle)."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("kp"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->MotionKp; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->MotionKp = v; })
                    .ToolTipText(LOCTEXT("TipKp", "포물선 반사 확률 (kp)\n중력 영향을 받는 포물선 궤적으로 바운스.\n논문 §3.1 — Δp로 매 바운스마다 감쇠."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("kf"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->MotionKf; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->MotionKf = v; })
                    .ToolTipText(LOCTEXT("TipKf", "표면 흐름 확률 (kf)\n중력 접선 방향으로 표면을 따라 이동.\n빗물, 녹물, 이끼 흐름 등에 사용."))
                )]
            ];

            // Carrier: sd / sp / sr / sh
            CardBox->AddSlot().AutoHeight().Padding(0, 2)
            [ SNew(STextBlock).Text(LOCTEXT("CarLbl", "  Carrier  sd / sp / sr / sh")).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f)) ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("sd"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSD; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSD = v; })
                    .ToolTipText(LOCTEXT("TipSD", "먼지/소일 밀도 (sd — soiling density)\n정착 시 표면을 얼마나 더럽히는지.\n먼지, 모래, 분진 등."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("sp"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSP; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSP = v; })
                    .ToolTipText(LOCTEXT("TipSP", "색소 (sp — pigment)\n얼룩, 이끼, 산화 녹 등의 색상 침착.\n높을수록 DustColor/PigmentColor 쪽으로 강하게 물듦."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("sr"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSR; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSR = v; })
                    .ToolTipText(LOCTEXT("TipSR", "거칠기 (sr — roughness)\n높을수록 표면 텍스처가 거칠어짐.\n소금 결정, 산화, 부식 등."))
                )]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ MakeC(TEXT("sh"), SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                    .Value_Lambda([Entry]() { return Entry->CarrierSH; })
                    .OnValueChanged_Lambda([Entry](float v) { Entry->CarrierSH = v; })
                    .ToolTipText(LOCTEXT("TipSH", "습도 (sh — humidity)\n높을수록 Cross-Channel에서 녹(sr) 촉진.\n빗물, 누수, 이끼 수분 등."))
                )]
            ];

            // Source
            CardBox->AddSlot().AutoHeight().Padding(0, 2)
            [ SNew(STextBlock).Text(LOCTEXT("SrcLbl", "  Source")).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 1.f, 1.f)) ];
            CardBox->AddSlot().AutoHeight() [ MakeRow(TEXT("Type"), MakeSrcCombo()) ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f) [ MakeC(TEXT("CX"), MakePosEntryBox(Entry->SrcCX)) ]
                + SHorizontalBox::Slot().FillWidth(1.f) [ MakeC(TEXT("CY"), MakePosEntryBox(Entry->SrcCY)) ]
                + SHorizontalBox::Slot().FillWidth(1.f) [ MakeC(TEXT("CZ"), MakePosEntryBox(Entry->SrcCZ)) ]
            ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    MakeC(TEXT("DX"), SNew(SSpinBox<float>)
                        .MinValue(-1.f).MaxValue(1.f).Delta(0.05f)
                        .Value_Lambda([Entry]() { return Entry->SrcDX; })
                        .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcDX = v; RefreshVisualizer(); })
                        .ToolTipText(LOCTEXT("TipDX", "방향 벡터 X 성분 (-1 ~ 1)\n내부에서 자동 정규화됨.\n예) (1,0,0) = +X축 방향, (-1,0,0) = -X축 방향")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    MakeC(TEXT("DY"), SNew(SSpinBox<float>)
                        .MinValue(-1.f).MaxValue(1.f).Delta(0.05f)
                        .Value_Lambda([Entry]() { return Entry->SrcDY; })
                        .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcDY = v; RefreshVisualizer(); })
                        .ToolTipText(LOCTEXT("TipDY", "방향 벡터 Y 성분 (-1 ~ 1)\n내부에서 자동 정규화됨.")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    MakeC(TEXT("DZ"), SNew(SSpinBox<float>)
                        .MinValue(-1.f).MaxValue(1.f).Delta(0.05f)
                        .Value_Lambda([Entry]() { return Entry->SrcDZ; })
                        .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcDZ = v; RefreshVisualizer(); })
                        .ToolTipText(LOCTEXT("TipDZ", "방향 벡터 Z 성분 (-1 ~ 1)\n내부에서 자동 정규화됨.\n예) (0,0,-1) = 아래 방향 (기본값)")))
                ]
            ];
            CardBox->AddSlot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f) [ MakeC(TEXT("Sprd"), MakeSrcFloatBox(Entry->SrcSpread)) ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    MakeC(TEXT("HalfX"), SNew(SSpinBox<float>)
                        .MinValue(0.f).MaxValue(5000.f).Delta(1.f)
                        .Value_Lambda([Entry]() { return Entry->SrcHalfX; })
                        .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcHalfX = v; RefreshVisualizer(); })
                        .ToolTipText(LOCTEXT("TipHalfX",
                            "방출 영역 반너비 (cm)\n"
                            "· AREA_TOP: 수평 직사각형의 X 반폭\n"
                            "· DIRECTIONAL: 빔 단면 접선 반폭\n"
                            "· ENVIRONMENT: 구(sphere) 반지름\n"
                            "· POINT: 무시됨")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SBox)
                    .Visibility_Lambda([Entry]() {
                        return (Entry->SourceTypeIdx == 3)
                            ? EVisibility::Collapsed : EVisibility::Visible;
                    })
                    [
                        MakeC(TEXT("HalfZ"), SNew(SSpinBox<float>)
                            .MinValue(0.f).MaxValue(5000.f).Delta(1.f)
                            .Value_Lambda([Entry]() { return Entry->SrcHalfZ; })
                            .OnValueChanged_Lambda([this, Entry](float v) { Entry->SrcHalfZ = v; RefreshVisualizer(); })
                            .ToolTipText(LOCTEXT("TipHalfZ",
                                "방출 영역 반깊이 (cm)\n"
                                "· AREA_TOP: 수평 직사각형의 Y 반폭\n"
                                "· DIRECTIONAL: 빔 단면 종축 반폭\n"
                                "· ENVIRONMENT: 미사용 (자동 숨김)\n"
                                "· POINT: 무시됨")))
                    ]
                ]
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
            [ MakeRow(TEXT("    Δs  (ks decay)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([Entry]() { return Entry->DeltaS; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->DeltaS = v; })
                .ToolTipText(LOCTEXT("TipDeltaS",
                    "ks(반구 반사) 감쇠율 — 바운스마다 ks를 이 값만큼 줄임.\n"
                    "높을수록 입자가 빨리 정착 → 표면 근처에만 쌓임.\n"
                    "0 = 감쇠 없음 (무한 반사), 1 = 첫 충돌에서 즉시 정착.\n"
                    "권장: 0.3~0.7"))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Δp  (kp decay)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([Entry]() { return Entry->DeltaP; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->DeltaP = v; })
                .ToolTipText(LOCTEXT("TipDeltaP",
                    "kp(포물선 반사) 감쇠율 — 바운스마다 kp를 이 값만큼 줄임.\n"
                    "감쇠된 kp는 kf(표면 흐름)로 전환됨 → 흐름 자국 형성.\n"
                    "높을수록 포물선 궤적이 빨리 사라지고 흘러내림이 커짐.\n"
                    "권장: 0.0~0.3"))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    Δf  (kf decay)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.01f)
                .Value_Lambda([Entry]() { return Entry->DeltaF; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->DeltaF = v; })
                .ToolTipText(LOCTEXT("TipDeltaF",
                    "kf(표면 흐름) 감쇠율 — 흐름 이동마다 kf를 이 값만큼 줄임.\n"
                    "높을수록 흐름 자국이 짧게 끊김.\n"
                    "0 = 끊기지 않고 계속 흐름, 1 = 한 칸만 이동 후 정착.\n"
                    "권장: 0.0~0.2"))
            )]
            // ── 초기 재질값 (논문 §4 stain-bleeding) ─────────────────────────
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("InitMatHdr", "    — Initial Material —"))
                .ColorAndOpacity(FLinearColor(0.6f, 0.75f, 0.6f, 1.f))
                .ToolTipText(LOCTEXT("InitMatTip",
                    "액터 표면의 초기 재질값 (시뮬레이션 시작 전 이미 존재하는 풍화).\n"
                    "논문 §4 stain-bleeding: 체인에 이미 녹이 있어야 계단으로 번짐."))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    init sd (dust)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSD; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSD = v; })
                .ToolTipText(LOCTEXT("TipInitSD", "초기 먼지/소일 밀도. 0=없음, 1=완전히 덮임."))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    init sp (pigment)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSP; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSP = v; })
                .ToolTipText(LOCTEXT("TipInitSP", "초기 색소 (녹, 이끼 등). 체인에 이미 녹이 있으면 stain-bleeding 발생."))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    init sr (roughness)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSR; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSR = v; })
                .ToolTipText(LOCTEXT("TipInitSR", "초기 거칠기. 높으면 γ-ton을 더 잘 잡아 풍화 가속."))
            )]
            + SVerticalBox::Slot().AutoHeight()
            [ MakeRow(TEXT("    init sh (humidity)"),
                SNew(SSpinBox<float>).MinValue(0.f).MaxValue(1.f).Delta(0.05f)
                .Value_Lambda([Entry]() { return Entry->InitSH; })
                .OnValueChanged_Lambda([Entry](float v) { Entry->InitSH = v; })
                .ToolTipText(LOCTEXT("TipInitSH", "초기 습도. 높으면 이끼/녹 성장 가속 (Cross-Channel)."))
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

FReply SGammaTonPanel::OnRunClicked()
{
    TArray<AActor*> Actors;
    for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
        if (AActor* A = Cast<AActor>(*It)) Actors.Add(A);

    if (Actors.IsEmpty()) {
        SetStatus(TEXT("No actors selected."));
        return FReply::Handled();
    }

    SetStatus(FString::Printf(TEXT("Extracting %d actor(s)..."), Actors.Num()));

    GTRayIntersector           Intersector;
    TArray<GTGammaReflectance> PerActorRefl    = BuildPerActorRefl(Actors.Num());
    TArray<GTMaterialProps>    PerActorInitMat = BuildPerActorInitMat(Actors.Num());

    FGTSceneData Scene = FGammaTonMeshBridge::BuildScene(Actors, PerActorRefl, PerActorInitMat, Intersector, TextureSize);
    if (!Scene.valid) {
        SetStatus(TEXT("No valid Static Mesh components found."));
        return FReply::Handled();
    }

    // Pre-load existing textures for accumulation
    int Preloaded = 0;
    for (int i = 0; i < (int)Scene.textures.size(); i++) {
        FString Safe   = Scene.actorNames[i].Replace(TEXT(" "), TEXT("_"));
        FString TexRef = TEXT("/Game/GammaTon/") + Safe + TEXT("_Dust.") + Safe + TEXT("_Dust");
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
                DustTexture_, PigmentTexture_, DustVisibility_);
            Applied++;
        }
        // Export BaseColor / Specular / Roughness PNGs for Manifold
        FString Dir = FGammaTonTextureBridge::ExportManifoldPNGs(
            Scene.textures[i], Scene.components[i], Name,
            ScenarioDustColor, ScenarioPigmentColor,
            DustVisibility_);
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
            Log += FString::Printf(
                TEXT("  Source : %s  center=(%.0f,%.0f,%.0f)  dir=(%.2f,%.2f,%.2f)  spread=%.1f deg\n"),
                SrcNames[SrcIdx],
                E.SrcCX, E.SrcCY, E.SrcCZ,
                E.SrcDX, E.SrcDY, E.SrcDZ, E.SrcSpread);
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

    SetStatus(FString::Printf(TEXT("Reverted %d actor(s) to pre-simulation material."), Restored));
    return FReply::Handled();
}

void SGammaTonPanel::SetStatus(const FString& Msg) {
    StatusMsg_ = Msg;
    if (StatusText_.IsValid()) StatusText_->SetText(FText::FromString(Msg));
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

    GTRayIntersector           Intersector;
    TArray<GTGammaReflectance> PerActorRefl    = BuildPerActorRefl(Actors.Num());
    TArray<GTMaterialProps>    PerActorInitMat = BuildPerActorInitMat(Actors.Num());

    FGTSceneData Scene = FGammaTonMeshBridge::BuildScene(Actors, PerActorRefl, PerActorInitMat, Intersector, TextureSize);
    if (!Scene.valid) {
        SetStatus(TEXT("No valid Static Mesh components found."));
        return FReply::Handled();
    }

    // Load prior textures so tex_before values reflect accumulated state.
    for (int i = 0; i < (int)Scene.textures.size(); i++) {
        FString Safe   = Scene.actorNames[i].Replace(TEXT(" "), TEXT("_"));
        FString TexRef = TEXT("/Game/GammaTon/") + Safe + TEXT("_Dust.") + Safe + TEXT("_Dust");
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

#undef LOCTEXT_NAMESPACE
