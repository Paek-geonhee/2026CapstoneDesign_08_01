#include "GammaTonRayVisualizer.h"
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "Engine/World.h"

FGammaTonRayVisualizer::FGammaTonRayVisualizer() {}
FGammaTonRayVisualizer::~FGammaTonRayVisualizer() {}

void FGammaTonRayVisualizer::SetPath(const GTRayPath& InPath)
{
    Path     = InPath;
    bHasPath = true;
}

void FGammaTonRayVisualizer::ClearPath() { bHasPath = false; }
void FGammaTonRayVisualizer::SetVisible(bool bInVisible) { bVisible = bInVisible; }

TStatId FGammaTonRayVisualizer::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(FGammaTonRayVisualizer, STATGROUP_Tickables);
}

void FGammaTonRayVisualizer::Tick(float /*DeltaTime*/)
{
    if (!GEditor) return;
    UWorld* W = GEditor->GetEditorWorldContext().World();
    if (!W) return;
    DrawPath(W);
}

FColor FGammaTonRayVisualizer::EventColor(GTBounceEvent Event) const
{
    switch (Event)
    {
    case GTBounceEvent::Reflect: return FColor(80,  120, 255);  // blue
    case GTBounceEvent::Diffuse: return FColor(80,  220, 220);  // cyan
    case GTBounceEvent::Flow:    return FColor::Yellow;
    case GTBounceEvent::Settle:  return FColor::Red;
    case GTBounceEvent::Escape:  return FColor(160, 160, 160);  // gray
    default:                     return FColor::White;
    }
}

void FGammaTonRayVisualizer::DrawPath(UWorld* W) const
{
    /*
    * This method assumes XXX methods are already called 
    so that `Path` member varaible has already been constructed
    */
    if (Path.hits.empty()) return;

    constexpr float LifeTime = -1.0f;  // redrawn every tick
    constexpr uint8 Depth    = 0;

    FVector Prev(Path.origin.x, Path.origin.y, Path.origin.z);

    // Source origin marker
    DrawDebugSphere(W, Prev, 15.0f, 8, FColor::White, false, LifeTime, Depth, 2.5f);
    DrawDebugString(W, Prev + FVector(0.f, 0.f, 35.f),
                    TEXT("SOURCE"), nullptr, FColor::White, 0.0f);

    for (int i = 0; i < (int)Path.hits.size(); i++)
    {
        const GTRayHitRecord& Rec = Path.hits[i];
        FVector HitPos(Rec.position.x, Rec.position.y, Rec.position.z);
        FColor  Col = EventColor(Rec.event);

        // Ray segment: draw parabolic arc if waypoints are available, else straight line
        if (Rec.event == GTBounceEvent::Diffuse && !Rec.parabola_pts.empty())
        {
            // Gravity-curved kp arc: Prev → waypoints → HitPos
            FVector Cur = Prev;
            for (const auto& wp : Rec.parabola_pts)
            {
                FVector WP(wp.x, wp.y, wp.z);
                DrawDebugLine(W, Cur, WP, Col, false, LifeTime, Depth, 2.5f);
                DrawDebugSphere(W, WP, 5.0f, 6, FColor(80, 200, 200), false, LifeTime, Depth, 1.0f);
                Cur = WP;
            }
            DrawDebugLine(W, Cur, HitPos, Col, false, LifeTime, Depth, 2.5f);
        }
        else
        {
            DrawDebugLine(W, Prev, HitPos, Col, false, LifeTime, Depth, 2.5f);
        }

        // Hit sphere
        DrawDebugSphere(W, HitPos, 12.0f, 8, Col, false, LifeTime, Depth, 2.0f);

        // Surface normal arrows (not for escape records)
        if (Rec.event != GTBounceEvent::Escape)
        {
            FVector N(Rec.normal.x, Rec.normal.y, Rec.normal.z);
            // geometric normal: white
            DrawDebugDirectionalArrow(W, HitPos, HitPos + N * 50.0f,
                                      12.0f, FColor::White, false, LifeTime, Depth, 1.5f);

            // shading normal (may differ from geometric on back-face hits): yellow
            FVector SN(Rec.shading_normal.x, Rec.shading_normal.y, Rec.shading_normal.z);
            if (!SN.Equals(N, 0.01f))
            {
                DrawDebugDirectionalArrow(W, HitPos, HitPos + SN * 50.0f,
                                          12.0f, FColor::Yellow, false, LifeTime, Depth, 1.5f);
            }
        }

        // Incoming direction arrow: orange (drawn backward from hit — shows ray origin side)
        {
            FVector DIn(Rec.dir_in.x, Rec.dir_in.y, Rec.dir_in.z);
            if (!DIn.IsNearlyZero())
            {
                DrawDebugDirectionalArrow(W, HitPos, HitPos - DIn * 60.0f,
                                          10.0f, FColor(255, 140, 0), false, LifeTime, Depth, 1.5f);
            }
        }

        // Outgoing direction arrow: green (only for bounce/flow events)
        if (Rec.event == GTBounceEvent::Reflect ||
            Rec.event == GTBounceEvent::Diffuse  ||
            Rec.event == GTBounceEvent::Flow)
        {
            FVector DOut(Rec.dir_out.x, Rec.dir_out.y, Rec.dir_out.z);
            if (!DOut.IsNearlyZero())
            {
                DrawDebugDirectionalArrow(W, HitPos, HitPos + DOut * 60.0f,
                                          10.0f, FColor::Green, false, LifeTime, Depth, 1.5f);
            }
        }

        // Build label text
        FString EventName = FString(ANSI_TO_TCHAR(GTBounceEventName(Rec.event)));
        bool bBackFace = (Rec.dir_in.x * Rec.normal.x +
                          Rec.dir_in.y * Rec.normal.y +
                          Rec.dir_in.z * Rec.normal.z) > 0.0f;
        FString Info = FString::Printf(
            TEXT("[%d] %s%s\n"
                 "UV=(%.3f, %.3f)\n"
                 "tex: sd=%.3f  sp=%.3f  sr=%.3f  sh=%.3f\n"
                 "ks: %.3f -> %.3f  kp: %.3f -> %.3f  kf: %.3f -> %.3f\n"
                 "geo_normal:    (%.3f, %.3f, %.3f)\n"
                 "shading_normal:(%.3f, %.3f, %.3f)\n"
                 "dir_in:  (%.3f, %.3f, %.3f)\n"
                 "dir_out: (%.3f, %.3f, %.3f)"),
            i + 1, *EventName, bBackFace ? TEXT(" [BACKFACE]") : TEXT(""),
            Rec.uv.x, Rec.uv.y,
            Rec.tex_before.sd, Rec.tex_before.sp,
            Rec.tex_before.sr, Rec.tex_before.sh,
            Rec.motion_before.ks, Rec.motion_after.ks,
            Rec.motion_before.kp, Rec.motion_after.kp,
            Rec.motion_before.kf, Rec.motion_after.kf,
            Rec.normal.x,         Rec.normal.y,         Rec.normal.z,
            Rec.shading_normal.x, Rec.shading_normal.y, Rec.shading_normal.z,
            Rec.dir_in.x,         Rec.dir_in.y,         Rec.dir_in.z,
            Rec.dir_out.x,        Rec.dir_out.y,        Rec.dir_out.z);

        if (Rec.event == GTBounceEvent::Settle || Rec.event == GTBounceEvent::Flow)
        {
            Info += FString::Printf(
                TEXT("\ndeposit: sd+%.4f  sp+%.4f  sr+%.4f  sh+%.4f"),
                Rec.deposit.sd, Rec.deposit.sp,
                Rec.deposit.sr, Rec.deposit.sh);
        }

        DrawDebugString(W, HitPos + FVector(0.f, 0.f, 28.f),
                        Info, nullptr, Col, 0.0f);

        Prev = HitPos;
    }
}
