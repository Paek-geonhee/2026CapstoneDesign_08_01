#include "GammaTonSourceVisualizer.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Engine/World.h"

FGammaTonSourceVisualizer::FGammaTonSourceVisualizer() {}
FGammaTonSourceVisualizer::~FGammaTonSourceVisualizer() {}

void FGammaTonSourceVisualizer::SetSource(const GTGammaSource& InSource)              { Sources_ = { InSource }; }
void FGammaTonSourceVisualizer::SetSources(const TArray<GTGammaSource>& InSources)    { Sources_ = InSources; }
void FGammaTonSourceVisualizer::SetVisible(bool bInVisible)                            { bVisible = bInVisible; }

TStatId FGammaTonSourceVisualizer::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(FGammaTonSourceVisualizer, STATGROUP_Tickables);
}

void FGammaTonSourceVisualizer::Tick(float /*DeltaTime*/)
{
    if (!GEditor) return;
    UWorld* W = GEditor->GetEditorWorldContext().World();
    if (!W) return;

    for (const GTGammaSource& S : Sources_)
    {
        switch (S.type)
        {
        case GTSourceType::AREA_TOP:    DrawAreaTop(W, S);    break;
        case GTSourceType::DIRECTIONAL: DrawDirectional(W, S); break;
        case GTSourceType::POINT:       DrawPoint(W, S);       break;
        case GTSourceType::ENVIRONMENT: DrawEnvironment(W, S); break;
        }
    }
}

// ── helpers ───────────────────────────────────────────────────────────────────

void FGammaTonSourceVisualizer::BuildFrame(const FVector& N, FVector& T, FVector& B)
{
    FVector Up = (FMath::Abs(N.Z) < 0.999f) ? FVector::UpVector : FVector::ForwardVector;
    T = FVector::CrossProduct(Up, N).GetSafeNormal();
    B = FVector::CrossProduct(N, T);
}

// ── AREA_TOP ──────────────────────────────────────────────────────────────────

void FGammaTonSourceVisualizer::DrawAreaTop(UWorld* W, const GTGammaSource& S) const
{
    FVector Center(S.center.x, S.center.y, S.center.z);
    float HX = S.area_half_x;
    float HZ = S.area_half_z;

    // Thin horizontal slab showing the emission rectangle
    DrawDebugBox(W, Center, FVector(HX, HZ, 2.0f), FQuat::Identity,
                 FColor::Cyan, false, -1.0f, 0, 2.0f);

    // 5x5 grid of downward arrows
    const int Grid = 5;
    const float ArrowLen = 100.0f;
    for (int iy = 0; iy < Grid; iy++)
    {
        for (int ix = 0; ix < Grid; ix++)
        {
            float fx = -HX + (2.0f * HX / (Grid - 1)) * ix;
            float fy = -HZ + (2.0f * HZ / (Grid - 1)) * iy;
            FVector Org = Center + FVector(fx, fy, 0.0f);
            DrawDebugDirectionalArrow(W, Org, Org + FVector(0.f, 0.f, -ArrowLen),
                                     20.0f, FColor::Cyan, false, -1.0f, 0, 1.5f);
        }
    }

    DrawDebugString(W, Center + FVector(0.f, 0.f, 40.f), TEXT("AREA_TOP"),
                    nullptr, FColor::Cyan, 0.0f);
}

// ── DIRECTIONAL ───────────────────────────────────────────────────────────────

void FGammaTonSourceVisualizer::DrawDirectional(UWorld* W, const GTGammaSource& S) const
{
    FVector Center(S.center.x, S.center.y, S.center.z);
    FVector Dir = FVector(S.direction.x, S.direction.y, S.direction.z).GetSafeNormal();
    FVector T, B;
    BuildFrame(Dir, T, B);

    float HX = S.area_half_x;
    float HZ = S.area_half_z;

    // Emission plane rectangle
    FVector C0 = Center + T * HX + B * HZ;
    FVector C1 = Center - T * HX + B * HZ;
    FVector C2 = Center - T * HX - B * HZ;
    FVector C3 = Center + T * HX - B * HZ;
    DrawDebugLine(W, C0, C1, FColor::Yellow, false, -1.0f, 0, 2.0f);
    DrawDebugLine(W, C1, C2, FColor::Yellow, false, -1.0f, 0, 2.0f);
    DrawDebugLine(W, C2, C3, FColor::Yellow, false, -1.0f, 0, 2.0f);
    DrawDebugLine(W, C3, C0, FColor::Yellow, false, -1.0f, 0, 2.0f);

    // 5x5 parallel arrows in Dir
    const int Grid = 5;
    const float ArrowLen = 150.0f;
    for (int iy = 0; iy < Grid; iy++)
    {
        for (int ix = 0; ix < Grid; ix++)
        {
            float ft = -HX + (2.0f * HX / (Grid - 1)) * ix;
            float fb = -HZ + (2.0f * HZ / (Grid - 1)) * iy;
            FVector Org = Center + T * ft + B * fb;
            DrawDebugDirectionalArrow(W, Org, Org + Dir * ArrowLen,
                                     20.0f, FColor::Yellow, false, -1.0f, 0, 1.5f);
        }
    }

    DrawDebugString(W, Center + FVector(0.f, 0.f, 40.f), TEXT("DIRECTIONAL"),
                    nullptr, FColor::Yellow, 0.0f);
}

// ── ENVIRONMENT ───────────────────────────────────────────────────────────────

void FGammaTonSourceVisualizer::DrawEnvironment(UWorld* W, const GTGammaSource& S) const
{
    const FVector Center(S.center.x, S.center.y, S.center.z);
    const float   R      = S.area_half_x;
    const FColor  Col    = FColor(160, 80, 255);   // purple
    const FColor  ColArw = FColor(200, 120, 255);

    // ── Wireframe sphere: 3 great circles ────────────────────────────────────
    const int Segs = 48;
    for (int plane = 0; plane < 3; plane++)
    {
        for (int i = 0; i < Segs; i++)
        {
            float A0 = (2.0f * UE_PI *  i)      / Segs;
            float A1 = (2.0f * UE_PI * (i + 1)) / Segs;
            FVector P0, P1;
            switch (plane)
            {
            case 0:  // equatorial (XY)
                P0 = Center + FVector(R * FMath::Cos(A0), R * FMath::Sin(A0), 0.f);
                P1 = Center + FVector(R * FMath::Cos(A1), R * FMath::Sin(A1), 0.f);
                break;
            case 1:  // meridian XZ
                P0 = Center + FVector(R * FMath::Cos(A0), 0.f, R * FMath::Sin(A0));
                P1 = Center + FVector(R * FMath::Cos(A1), 0.f, R * FMath::Sin(A1));
                break;
            default: // meridian YZ
                P0 = Center + FVector(0.f, R * FMath::Cos(A0), R * FMath::Sin(A0));
                P1 = Center + FVector(0.f, R * FMath::Cos(A1), R * FMath::Sin(A1));
                break;
            }
            DrawDebugLine(W, P0, P1, Col, false, -1.0f, 0, 1.5f);
        }
    }

    // ── Inward arrows: Fibonacci sphere (16 points, uniform distribution) ────
    // Produces an even spread without clustering at the poles.
    const int   NumPts     = 16;
    const float GoldenAng  = UE_PI * (3.0f - FMath::Sqrt(5.0f));   // ≈ 2.399 rad
    const float ArrowLen   = R * 0.35f;

    for (int i = 0; i < NumPts; i++)
    {
        float y     = 1.0f - (2.0f * i + 1.0f) / NumPts;           // 1 → -1
        float rXY   = FMath::Sqrt(FMath::Max(0.0f, 1.0f - y * y));
        float angle = GoldenAng * i;

        FVector SurfPt = Center + FVector(
            R * rXY * FMath::Cos(angle),
            R * rXY * FMath::Sin(angle),
            R * y);

        FVector InDir = (Center - SurfPt).GetSafeNormal();
        DrawDebugDirectionalArrow(W,
            SurfPt, SurfPt + InDir * ArrowLen,
            18.0f, ColArw, false, -1.0f, 0, 1.5f);
    }

    // ── Scene-centre dot ──────────────────────────────────────────────────────
    DrawDebugSphere(W, Center, FMath::Max(8.0f, R * 0.025f), 10, Col, false, -1.0f, 0, 2.5f);

    // ── Radius annotation ─────────────────────────────────────────────────────
    DrawDebugLine(W, Center, Center + FVector(R, 0.f, 0.f), Col, false, -1.0f, 0, 1.5f);
    DrawDebugString(W, Center + FVector(R * 0.5f, 0.f, 22.f),
        FString::Printf(TEXT("r = %.0f cm"), R),
        nullptr, Col, 0.0f);

    // ── Label ─────────────────────────────────────────────────────────────────
    DrawDebugString(W, Center + FVector(0.f, 0.f, R + 50.f),
        TEXT("ENVIRONMENT"), nullptr, Col, 0.0f);
}

// ── POINT ─────────────────────────────────────────────────────────────────────

void FGammaTonSourceVisualizer::DrawPoint(UWorld* W, const GTGammaSource& S) const
{
    FVector Center(S.center.x, S.center.y, S.center.z);
    FVector Dir = FVector(S.direction.x, S.direction.y, S.direction.z).GetSafeNormal();

    DrawDebugSphere(W, Center, 18.0f, 12, FColor::Orange, false, -1.0f, 0, 2.0f);

    float SpreadRad  = FMath::DegreesToRadians(S.spread_deg);
    float ConeLen    = 300.0f;
    float RingRadius = ConeLen * FMath::Tan(SpreadRad);

    FVector T, B;
    BuildFrame(Dir, T, B);
    FVector RingCenter = Center + Dir * ConeLen;

    // Ring at cone mouth (16 segments)
    const int Segs = 16;
    for (int i = 0; i < Segs; i++)
    {
        float A0 = (2.0f * UE_PI * i)       / Segs;
        float A1 = (2.0f * UE_PI * (i + 1)) / Segs;
        FVector P0 = RingCenter + T * (FMath::Cos(A0) * RingRadius) + B * (FMath::Sin(A0) * RingRadius);
        FVector P1 = RingCenter + T * (FMath::Cos(A1) * RingRadius) + B * (FMath::Sin(A1) * RingRadius);
        DrawDebugLine(W, P0, P1, FColor::Orange, false, -1.0f, 0, 1.5f);
    }

    // 4 generator lines apex → ring
    for (int i = 0; i < 4; i++)
    {
        float A = (2.0f * UE_PI * i) / 4.0f;
        FVector Edge = RingCenter + T * (FMath::Cos(A) * RingRadius) + B * (FMath::Sin(A) * RingRadius);
        DrawDebugLine(W, Center, Edge, FColor::Orange, false, -1.0f, 0, 2.0f);
    }

    DrawDebugDirectionalArrow(W, Center, Center + Dir * ConeLen * 0.8f,
                              30.0f, FColor::Red, false, -1.0f, 0, 2.0f);

    DrawDebugString(W, Center + FVector(0.f, 0.f, 40.f), TEXT("POINT"),
                    nullptr, FColor::Orange, 0.0f);
}
