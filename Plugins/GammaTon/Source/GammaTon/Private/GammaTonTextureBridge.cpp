#include "GammaTonTextureBridge.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "TextureResource.h"

// Texture channel layout (PF_B8G8R8A8 / TSF_BGRA8):
//   R = sd (dust density)   G = sp (pigment)
//   B = sr (roughness)      A = sh (humidity)

// ── CreateAndSaveTexture ──────────────────────────────────────────────────────

UTexture2D* FGammaTonTextureBridge::CreateAndSaveTexture(
    const GTObjTexture& SimTex, const FString& ActorName)
{
    const int W = SimTex.width;
    const int H = SimTex.height;

    FString Safe    = ActorName.Replace(TEXT(" "), TEXT("_"));
    FString PkgPath = TEXT("/Game/GammaTon/") + Safe + TEXT("_Dust");
    UPackage* Pkg   = CreatePackage(*PkgPath);
    Pkg->FullyLoad();

    FName TexName = *FPaths::GetBaseFilename(PkgPath);
    UTexture2D* Tex = LoadObject<UTexture2D>(Pkg, *TexName.ToString());
    if (!Tex)
        Tex = NewObject<UTexture2D>(Pkg, TexName, RF_Public | RF_Standalone | RF_Transactional);

    Tex->SetPlatformData(new FTexturePlatformData());
    Tex->GetPlatformData()->SizeX = W;
    Tex->GetPlatformData()->SizeY = H;
    Tex->GetPlatformData()->SetNumSlices(1);
    Tex->GetPlatformData()->PixelFormat = PF_B8G8R8A8;

    FTexture2DMipMap* Mip = new FTexture2DMipMap();
    Tex->GetPlatformData()->Mips.Add(Mip);
    Mip->SizeX = W; Mip->SizeY = H;
    Mip->BulkData.Lock(LOCK_READ_WRITE);
    uint8* Raw = (uint8*)Mip->BulkData.Realloc(W * H * 4);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = y*W + x, bi = i*4;
            Raw[bi+0] = (uint8)(FMath::Clamp(SimTex.sr[i], 0.f, 1.f) * 255.f); // B = sr
            Raw[bi+1] = (uint8)(FMath::Clamp(SimTex.sp[i], 0.f, 1.f) * 255.f); // G = sp
            Raw[bi+2] = (uint8)(FMath::Clamp(SimTex.sd[i], 0.f, 1.f) * 255.f); // R = sd
            Raw[bi+3] = (uint8)(FMath::Clamp(SimTex.sh[i], 0.f, 1.f) * 255.f); // A = sh
        }
    }
    Mip->BulkData.Unlock();

    Tex->Source.Init(W, H, 1, 1, TSF_BGRA8);
    uint8* Src = Tex->Source.LockMip(0);
    FMemory::Memcpy(Src, Raw, W * H * 4);
    Tex->Source.UnlockMip(0);

    // AgingTex stores linear physics data (densities 0-1), not sRGB color.
    // TC_Default with SRGB=false keeps the values numerically exact in the shader.
    Tex->CompressionSettings = TC_Default;
    Tex->SRGB = false;
    Tex->UpdateResource();
    Pkg->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(Tex);
    return Tex;
}

// ── LoadTextureIntoObjTexture ─────────────────────────────────────────────────
//
// Reads a previously saved AgingTex back into a GTObjTexture accumulator.
// Called at the start of RunSimulation to continue from a prior save rather
// than resetting all deposits to zero — allows incremental multi-session runs.

void FGammaTonTextureBridge::LoadTextureIntoObjTexture(UTexture2D* Tex, GTObjTexture& Out)
{
    if (!Tex || !Tex->Source.IsValid()) return;
    const int W = Tex->Source.GetSizeX();
    const int H = Tex->Source.GetSizeY();
    if (W != Out.width || H != Out.height) return;

    uint8* Src = Tex->Source.LockMip(0);
    if (!Src) return;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int i = y*W + x, bi = i*4;
            Out.sr[i] = Src[bi+0] / 255.f;
            Out.sp[i] = Src[bi+1] / 255.f;
            Out.sd[i] = Src[bi+2] / 255.f;
            Out.sh[i] = Src[bi+3] / 255.f;
        }
    }
    Tex->Source.UnlockMip(0);
}

// ── InjectAgingOverlay ────────────────────────────────────────────────────────
//
// Wraps the duplicated material's existing BaseColor with:
//   Lerp(OrigBaseColor, DustColor, AgingTex.R)  → BaseColor
//   OrigRoughness + AgingTex.B × RoughnessScale → Roughness
//
// OrigBaseColor is whatever the duplicated graph already had — no replacement.

static void InjectAgingOverlay(UMaterial* Mat, int AtlasUVChannel)
{
    UMaterialEditorOnlyData* Ed = Mat->GetEditorOnlyData();
    if (!Ed) return;

    UMaterialExpression* OrigColor    = Ed->BaseColor.Expression;
    int32                OrigColorOut = Ed->BaseColor.OutputIndex;
    UMaterialExpression* OrigRough    = Ed->Roughness.Expression;
    int32                OrigRoughOut = Ed->Roughness.OutputIndex;

    auto Add = [&](auto* Expr, int edX, int edY) {
        Expr->MaterialExpressionEditorX = edX;
        Expr->MaterialExpressionEditorY = edY;
        Mat->GetExpressionCollection().AddExpression(Expr);
        return Expr;
    };

    auto* UVAtlas = Add(NewObject<UMaterialExpressionTextureCoordinate>(Mat), -1400, 500);
    UVAtlas->CoordinateIndex = AtlasUVChannel;

    // UV channel 0 for material texture sampling
    auto* UV0 = Add(NewObject<UMaterialExpressionTextureCoordinate>(Mat), -1400, 200);
    UV0->CoordinateIndex = 0;

    auto* AgingTex = Add(NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat), -1150, 500);
    AgingTex->ParameterName          = TEXT("AgingTex");
    AgingTex->Coordinates.Expression = UVAtlas;

    // Material detail textures (default = white so color × white = color — backwards compatible)
    UTexture* WhiteTex = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture"));

    auto* DustTexture = Add(NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat), -1150, 200);
    DustTexture->ParameterName          = TEXT("DustTexture");
    DustTexture->Coordinates.Expression = UV0;
    if (WhiteTex) DustTexture->Texture  = WhiteTex;

    auto* PigmentTexture = Add(NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat), -1150, 0);
    PigmentTexture->ParameterName          = TEXT("PigmentTexture");
    PigmentTexture->Coordinates.Expression = UV0;
    if (WhiteTex) PigmentTexture->Texture  = WhiteTex;

    auto* DustColor = Add(NewObject<UMaterialExpressionVectorParameter>(Mat), -1150, 350);
    DustColor->ParameterName = TEXT("DustColor");
    DustColor->DefaultValue  = FLinearColor(0.42f, 0.38f, 0.32f, 1.0f);

    auto* PigmentColor = Add(NewObject<UMaterialExpressionVectorParameter>(Mat), -1150, 150);
    PigmentColor->ParameterName = TEXT("PigmentColor");
    PigmentColor->DefaultValue  = FLinearColor(0.15f, 0.10f, 0.06f, 1.0f);

    // DustEffect = DustColor × DustTexture (§3.5 paper: color tint × detail texture)
    auto* DustEffect = Add(NewObject<UMaterialExpressionMultiply>(Mat), -900, 280);
    DustEffect->A.Expression = DustColor;
    DustEffect->B.Expression = DustTexture;

    // PigmentEffect = PigmentColor × PigmentTexture
    auto* PigmentEffect = Add(NewObject<UMaterialExpressionMultiply>(Mat), -900, 80);
    PigmentEffect->A.Expression = PigmentColor;
    PigmentEffect->B.Expression = PigmentTexture;

    auto* WetnessScale = Add(NewObject<UMaterialExpressionScalarParameter>(Mat), -200, 300);
    WetnessScale->ParameterName = TEXT("WetnessScale");
    WetnessScale->DefaultValue  = 0.4f;

    auto* RoughnessScale = Add(NewObject<UMaterialExpressionScalarParameter>(Mat), -200, 400);
    RoughnessScale->ParameterName = TEXT("RoughnessScale");
    RoughnessScale->DefaultValue  = 0.35f;

    auto* DustVisibility = Add(NewObject<UMaterialExpressionScalarParameter>(Mat), -200, 500);
    DustVisibility->ParameterName = TEXT("DustVisibility");
    DustVisibility->DefaultValue  = 1.0f;

    // DuplicateObject can fail to preserve BaseColor expression for UE4-format assets.
    // Insert a parameter fallback so ApplyToComponent can supply the texture via MID.
    if (!OrigColor) {
        auto* OrigParam = Add(NewObject<UMaterialExpressionTextureSampleParameter2D>(Mat), -1200, 700);
        OrigParam->ParameterName = TEXT("GammaTon_OrigBaseColor");
        OrigColor    = OrigParam;
        OrigColorOut = 0;
        UE_LOG(LogTemp, Log, TEXT("[GammaTon] InjectAgingOverlay: OrigColor null — inserted GammaTon_OrigBaseColor param"));
    }

    // BaseColor: Lerp(OrigColor, DustEffect, sd×vis) → Lerp(_, PigmentEffect, sp×vis) → × (1-sh)
    // DustVisibility scales both sd and sp Lerp alphas so the user can dial back color tint.
    auto* AlphaSD = Add(NewObject<UMaterialExpressionMultiply>(Mat), -650, 620);
    AlphaSD->A.Expression  = AgingTex;
    AlphaSD->A.OutputIndex = 1; // R = sd
    AlphaSD->B.Expression  = DustVisibility;

    auto* AlphaSP = Add(NewObject<UMaterialExpressionMultiply>(Mat), -400, 620);
    AlphaSP->A.Expression  = AgingTex;
    AlphaSP->A.OutputIndex = 2; // G = sp
    AlphaSP->B.Expression  = DustVisibility;

    auto* Lerp1 = Add(NewObject<UMaterialExpressionLinearInterpolate>(Mat), -650, 500);
    if (OrigColor) { Lerp1->A.Expression = OrigColor; Lerp1->A.OutputIndex = OrigColorOut; }
    Lerp1->B.Expression     = DustEffect;
    Lerp1->Alpha.Expression = AlphaSD;

    auto* Lerp2 = Add(NewObject<UMaterialExpressionLinearInterpolate>(Mat), -400, 500);
    Lerp2->A.Expression     = Lerp1;
    Lerp2->B.Expression     = PigmentEffect;
    Lerp2->Alpha.Expression = AlphaSP;

    auto* WetMul = Add(NewObject<UMaterialExpressionMultiply>(Mat), 50, 400);
    WetMul->A.Expression  = AgingTex;
    WetMul->A.OutputIndex = 4; // A = sh
    WetMul->B.Expression  = WetnessScale;

    auto* WetOM = Add(NewObject<UMaterialExpressionOneMinus>(Mat), 250, 400);
    WetOM->Input.Expression = WetMul;

    auto* ColorWet = Add(NewObject<UMaterialExpressionMultiply>(Mat), 450, 500);
    ColorWet->A.Expression = Lerp2;
    ColorWet->B.Expression = WetOM;

    Ed->BaseColor.Expression  = ColorWet;
    Ed->BaseColor.OutputIndex = 0;

    // Roughness: OrigRough + AgingTex.B × RoughnessScale
    auto* SrScaled = Add(NewObject<UMaterialExpressionMultiply>(Mat), -200, 700);
    SrScaled->A.Expression  = AgingTex;
    SrScaled->A.OutputIndex = 3; // B = sr
    SrScaled->B.Expression  = RoughnessScale;

    auto* RoughAdd = Add(NewObject<UMaterialExpressionAdd>(Mat), 50, 700);
    if (OrigRough) { RoughAdd->A.Expression = OrigRough; RoughAdd->A.OutputIndex = OrigRoughOut; }
    else           { auto* C = Add(NewObject<UMaterialExpressionConstant>(Mat), -700, 700); C->R = 0.5f; RoughAdd->A.Expression = C; }
    RoughAdd->B.Expression = SrScaled;

    Ed->Roughness.Expression  = RoughAdd;
    Ed->Roughness.OutputIndex = 0;

    Mat->PreEditChange(nullptr);
    Mat->PostEditChange();
}

// ── ApplyToComponent ──────────────────────────────────────────────────────────

void FGammaTonTextureBridge::ApplyToComponent(
    UStaticMeshComponent* Comp, UTexture2D* AgingTexture,
    const FString& ActorName, FLinearColor DustColor, FLinearColor PigmentColor,
    int AtlasUVChannel, UTexture2D* DustTexture, UTexture2D* PigmentTexture,
    float DustVisibility)
{
    if (!Comp || !AgingTexture) return;

    // Read from the component (respects per-actor material overrides in the level).
    // Persist the original material path in an actor tag so re-runs don't pick up our own MID.
    UMaterialInterface* OrigIface = nullptr;
    {
        static const FString TagPrefix = TEXT("GammaTon_OrigMat=");
        AActor* Owner = Comp->GetOwner();
        if (Owner) {
            for (const FName& Tag : Owner->Tags) {
                FString S = Tag.ToString();
                if (S.StartsWith(TagPrefix)) {
                    OrigIface = LoadObject<UMaterialInterface>(nullptr, *S.Mid(TagPrefix.Len()));
                    break;
                }
            }
        }
        if (!OrigIface) {
            OrigIface = Comp->GetMaterial(0);
            // Persist path so the next run skips a MID we applied
            if (OrigIface && !Cast<UMaterialInstanceDynamic>(OrigIface) && Owner) {
                FName NewTag = *(TagPrefix + OrigIface->GetPathName());
                if (!Owner->Tags.Contains(NewTag))
                    Owner->Tags.Add(NewTag);
            }
        }
    }

    UMaterial* OrigBase = nullptr;
    if (auto* M  = Cast<UMaterial>(OrigIface))            OrigBase = M;
    else if (auto* MI = Cast<UMaterialInstance>(OrigIface)) OrigBase = MI->GetMaterial();

    FString Safe   = ActorName.Replace(TEXT(" "), TEXT("_"));
    FString MatPkg = TEXT("/Game/GammaTon/") + Safe + TEXT("_DustMat");
    FString MatRef = MatPkg + TEXT(".") + Safe + TEXT("_DustMat");
    FName   MatFName = *(Safe + TEXT("_DustMat"));

    // Cache check: valid if DustMat has AgingTex, DustTexture AND a wired BaseColor.
    // Stale caches (missing DustTexture) trigger a regeneration for multi-texture blending.
    UMaterial* DustMat = LoadObject<UMaterial>(nullptr, *MatRef);
    if (DustMat) {
        bool bHasAgingTex = false, bHasDustTex = false, bHasDustVis = false;
        for (UMaterialExpression* Expr : DustMat->GetExpressionCollection().Expressions) {
            if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr)) {
                if (P->ParameterName == TEXT("AgingTex"))    bHasAgingTex = true;
                if (P->ParameterName == TEXT("DustTexture")) bHasDustTex  = true;
            }
            if (auto* S = Cast<UMaterialExpressionScalarParameter>(Expr))
                if (S->ParameterName == TEXT("DustVisibility")) bHasDustVis = true;
        }

        UMaterialEditorOnlyData* CachedEd = DustMat->GetEditorOnlyData();
        bool bHasBaseColor = CachedEd && (CachedEd->BaseColor.Expression != nullptr);

        if (!bHasAgingTex || !bHasDustTex || !bHasDustVis || !bHasBaseColor) {
            UE_LOG(LogTemp, Log, TEXT("[GammaTon] Cached DustMat outdated — regenerating"));
            DustMat = nullptr;
        }
    }

    if (!DustMat) {
        UPackage* Pkg = CreatePackage(*MatPkg);
        Pkg->FullyLoad();
        if (OrigBase) {
            // Force-initialize UMaterialEditorOnlyData before duplicating.
            // UE4-format assets create this lazily; without this call the
            // duplicate's GetEditorOnlyData() returns an empty object with
            // no expressions, leaving OrigColor null after duplication.
            OrigBase->GetEditorOnlyData();
            DustMat = DuplicateObject<UMaterial>(OrigBase, Pkg, MatFName);
        } else {
            DustMat = NewObject<UMaterial>(Pkg, MatFName, RF_Public | RF_Standalone | RF_Transactional);
        }
        DustMat->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        InjectAgingOverlay(DustMat, AtlasUVChannel);
        Pkg->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(DustMat);
        UE_LOG(LogTemp, Log, TEXT("[GammaTon] Created DustMat for %s"), *ActorName);
    }

    UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(DustMat, Comp);

    // Copy MI parameter overrides so the duplicated base material gets the right textures.
    if (UMaterialInstance* OrigMI = Cast<UMaterialInstance>(OrigIface)) {
        TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Guids;

        OrigMI->GetAllTextureParameterInfo(Infos, Guids);
        for (auto& P : Infos) { UTexture* V = nullptr; if (OrigMI->GetTextureParameterValue(P, V) && V) MID->SetTextureParameterValue(P.Name, V); }

        Infos.Reset(); Guids.Reset();
        OrigMI->GetAllVectorParameterInfo(Infos, Guids);
        for (auto& P : Infos) { FLinearColor V; if (OrigMI->GetVectorParameterValue(P, V)) MID->SetVectorParameterValue(P.Name, V); }

        Infos.Reset(); Guids.Reset();
        OrigMI->GetAllScalarParameterInfo(Infos, Guids);
        for (auto& P : Infos) { float V = 0.f; if (OrigMI->GetScalarParameterValue(P, V)) MID->SetScalarParameterValue(P.Name, V); }
    }

    // If InjectAgingOverlay inserted GammaTon_OrigBaseColor (OrigColor was null),
    // find the original BaseColor texture and supply it via MID.
    {
        bool bNeedsOrigBase = false;
        for (UMaterialExpression* Expr : DustMat->GetExpressionCollection().Expressions)
            if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                if (P->ParameterName == TEXT("GammaTon_OrigBaseColor")) { bNeedsOrigBase = true; break; }

        if (bNeedsOrigBase) {
            auto NameMatchesColor = [](const FString& Name) {
                static const TCHAR* Kws[] = {
                    TEXT("BaseColor"), TEXT("Base_Color"), TEXT("Base Color"),
                    TEXT("Albedo"), TEXT("Diffuse"), TEXT("Color"), TEXT("Colour")
                };
                for (const TCHAR* Kw : Kws)
                    if (Name.Contains(Kw, ESearchCase::IgnoreCase)) return true;
                return false;
            };

            UTexture* OrigTex = nullptr;

            // Pass 1 — parameter API with keyword filter (works for MI and most UMaterials)
            {
                TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Guids;
                OrigIface->GetAllTextureParameterInfo(Infos, Guids);
                for (auto& P : Infos)
                    if (NameMatchesColor(P.Name.ToString()))
                        if (OrigIface->GetTextureParameterValue(P, OrigTex) && OrigTex) break;
            }

            // Pass 2 — expression collection, keyword filter (handles UE4-format UMaterial
            // where GetTextureParameterValue returns false for non-overridden defaults)
            if (!OrigTex && OrigBase) {
                for (UMaterialExpression* Expr : OrigBase->GetExpressionCollection().Expressions)
                    if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                        if (NameMatchesColor(P->ParameterName.ToString()))
                            if (auto* T2D = Cast<UTexture2D>(P->Texture)) { OrigTex = T2D; break; }
            }

            // Pass 3 — any TextureSampleParameter2D (last resort)
            if (!OrigTex && OrigBase) {
                for (UMaterialExpression* Expr : OrigBase->GetExpressionCollection().Expressions)
                    if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                        if (auto* T2D = Cast<UTexture2D>(P->Texture)) { OrigTex = T2D; break; }
            }

            // Pass 4 — any TextureSample (non-parameter fallback)
            if (!OrigTex && OrigBase) {
                for (UMaterialExpression* Expr : OrigBase->GetExpressionCollection().Expressions)
                    if (auto* S = Cast<UMaterialExpressionTextureSample>(Expr))
                        if (auto* T2D = Cast<UTexture2D>(S->Texture)) { OrigTex = T2D; break; }
            }

            if (OrigTex)
                MID->SetTextureParameterValue(TEXT("GammaTon_OrigBaseColor"), OrigTex);
            UE_LOG(LogTemp, Log, TEXT("[GammaTon] GammaTon_OrigBaseColor = %s"),
                OrigTex ? *OrigTex->GetName() : TEXT("NULL"));
        }
    }

    // Aging parameters last so they win any name collision.
    MID->SetTextureParameterValue(TEXT("AgingTex"),       AgingTexture);
    MID->SetVectorParameterValue(TEXT("DustColor"),       DustColor);
    MID->SetVectorParameterValue(TEXT("PigmentColor"),    PigmentColor);
    MID->SetScalarParameterValue(TEXT("DustVisibility"),  FMath::Clamp(DustVisibility, 0.f, 1.f));
    if (DustTexture)    MID->SetTextureParameterValue(TEXT("DustTexture"),    DustTexture);
    if (PigmentTexture) MID->SetTextureParameterValue(TEXT("PigmentTexture"), PigmentTexture);
    Comp->SetMaterial(0, MID);

    UE_LOG(LogTemp, Log, TEXT("[GammaTon] Applied aging to %s"), *ActorName);
}

// ── ExportManifoldPNGs helpers ────────────────────────────────────────────────

// Find a texture in a material by keyword list (same strategy as FindBaseColorTex).
static UTexture2D* sManifoldFindTex(UMaterialInterface* Mat,
                                    std::initializer_list<const TCHAR*> Keywords)
{
    if (!Mat) return nullptr;
    UMaterial* Base = nullptr;
    if (auto* M  = Cast<UMaterial>(Mat))            Base = M;
    else if (auto* MI = Cast<UMaterialInstance>(Mat)) Base = MI->GetMaterial();

    auto Matches = [&](const FString& Name) {
        for (const TCHAR* Kw : Keywords)
            if (Name.Contains(Kw, ESearchCase::IgnoreCase)) return true;
        return false;
    };

    // Pass 1 — texture parameter API
    {
        TArray<FMaterialParameterInfo> Infos; TArray<FGuid> Guids;
        Mat->GetAllTextureParameterInfo(Infos, Guids);
        for (auto& P : Infos)
            if (Matches(P.Name.ToString())) {
                UTexture* T = nullptr;
                if (Mat->GetTextureParameterValue(P, T))
                    if (auto* T2D = Cast<UTexture2D>(T)) return T2D;
            }
    }
    // Pass 2 — expression collection
    if (Base)
        for (UMaterialExpression* Expr : Base->GetExpressionCollection().Expressions)
            if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                if (Matches(P->ParameterName.ToString()))
                    if (auto* T2D = Cast<UTexture2D>(P->Texture)) return T2D;

    return nullptr;
}

// Lock a BGRA8 texture source and return a raw read pointer.
// Caller must call UnlockMip(0) after use. Returns nullptr on failure.
static const uint8* sLockBGRA8(UTexture2D* Tex, int& OutW, int& OutH)
{
    if (!Tex || !Tex->Source.IsValid()) return nullptr;
    if (Tex->Source.GetFormat() != TSF_BGRA8) return nullptr;
    OutW = Tex->Source.GetSizeX();
    OutH = Tex->Source.GetSizeY();
    return Tex->Source.LockMip(0);
}

// Sample a locked BGRA8 buffer at normalized UV (wraps, nearest-neighbor).
static FLinearColor sSampleBGRA8(const uint8* Data, int TW, int TH, float u, float v)
{
    u -= FMath::Floor(u);
    v -= FMath::Floor(v);
    int px = FMath::Clamp((int)(u * TW), 0, TW - 1);
    int py = FMath::Clamp((int)(v * TH), 0, TH - 1);
    int bi = (py * TW + px) * 4;
    return FLinearColor(Data[bi+2]/255.f, Data[bi+1]/255.f, Data[bi+0]/255.f, 1.f);
}

static float sLuminance(FLinearColor C) { return C.R*0.299f + C.G*0.587f + C.B*0.114f; }

// ── ExportManifoldPNGs ────────────────────────────────────────────────────────
//
// Composites original PBR textures with GammaTon AgingTex and saves 3 PNGs:
//   BaseColor = Lerp(Lerp(Orig, DustColor, sd), PigmentColor, sp) × (1-sh×wet)
//   Specular  = OrigSpecular × (1 - sd×0.3 - sp×0.2)
//   Roughness = OrigRoughness + sr × RoughnessScale
//
// Original textures are sampled at the same normalized pixel position as the
// AgingTex pixel. Both textures are downsampled to 128×128 by Manifold so
// minor UV-space mismatch is acceptable.

FString FGammaTonTextureBridge::ExportManifoldPNGs(
    const GTObjTexture&   SimTex,
    UStaticMeshComponent* Comp,
    const FString&        ActorName,
    FLinearColor          DustColor,
    FLinearColor          PigmentColor,
    float                 DustVisibility,
    float                 WetnessScale,
    float                 RoughnessScale)
{
    const int W = SimTex.width;
    const int H = SimTex.height;
    const int N = W * H;

    IImageWrapperModule* IWM = FModuleManager::LoadModulePtr<IImageWrapperModule>("ImageWrapper");
    if (!IWM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[GammaTon] ImageWrapper module not found — Manifold PNG export skipped."));
        return FString();
    }

    // ── Prepare output directory ──────────────────────────────────────────────
    FString Safe    = ActorName.Replace(TEXT(" "), TEXT("_"));
    FString SaveDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GammaTon"), TEXT("Manifold"));
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    if (!PF.DirectoryExists(*SaveDir)) PF.CreateDirectoryTree(*SaveDir);

    auto WritePNG = [&](const TArray<uint8>& Pixels, ERGBFormat Fmt, const FString& Suffix)
    {
        TSharedPtr<IImageWrapper> Wrapper = IWM->CreateImageWrapper(EImageFormat::PNG);
        if (!Wrapper.IsValid()) return;
        Wrapper->SetRaw(Pixels.GetData(), Pixels.Num(), W, H, Fmt, 8);
        const TArray64<uint8>& Compressed = Wrapper->GetCompressed();
        TArray<uint8> FileData;
        FileData.Append(Compressed.GetData(), (int32)Compressed.Num());
        FFileHelper::SaveArrayToFile(FileData, *FPaths::Combine(SaveDir, Safe + Suffix));
    };

    // ── Resolve original PBR textures from the component's material ───────────
    UMaterialInterface* MatIface = Comp ? Comp->GetMaterial(0) : nullptr;

    UTexture2D* OrigBC   = sManifoldFindTex(MatIface,
        { TEXT("BaseColor"), TEXT("Base_Color"), TEXT("Albedo"), TEXT("Diffuse"), TEXT("Color") });
    UTexture2D* OrigSpec = sManifoldFindTex(MatIface,
        { TEXT("Specular"), TEXT("Spec"), TEXT("Metallic"), TEXT("Metal") });
    UTexture2D* OrigRough= sManifoldFindTex(MatIface,
        { TEXT("Roughness"), TEXT("Rough"), TEXT("Gloss") });

    // Lock source data (read-only) — nullptr if texture missing or non-BGRA8
    int BCW=0, BCH=0, SpW=0, SpH=0, RoW=0, RoH=0;
    const uint8* BCData   = sLockBGRA8(OrigBC,    BCW, BCH);
    const uint8* SpData   = sLockBGRA8(OrigSpec,  SpW, SpH);
    const uint8* RoData   = sLockBGRA8(OrigRough, RoW, RoH);

    // Fallback colors when original textures are unavailable
    const FLinearColor FallbackBC  (0.75f, 0.75f, 0.75f);  // light gray
    const float        FallbackSpec = 0.5f;
    const float        FallbackRo   = 0.3f;

    // ── BaseColor (RGBA, 4 bpp) ───────────────────────────────────────────────
    {
        TArray<uint8> Pixels;
        Pixels.SetNumUninitialized(N * 4);
        for (int i = 0; i < N; i++)
        {
            float u  = (i % W + 0.5f) / W;
            float v  = (i / W + 0.5f) / H;
            float sd = FMath::Clamp(SimTex.sd[i], 0.f, 1.f);
            float sp = FMath::Clamp(SimTex.sp[i], 0.f, 1.f);
            float sh = FMath::Clamp(SimTex.sh[i], 0.f, 1.f);

            FLinearColor Orig = BCData ? sSampleBGRA8(BCData, BCW, BCH, u, v) : FallbackBC;
            FLinearColor C    = FMath::Lerp(Orig, DustColor,    sd * DustVisibility);
            C                 = FMath::Lerp(C,    PigmentColor, sp * DustVisibility);
            C                 = C * (1.0f - sh * WetnessScale);

            Pixels[i*4+0] = (uint8)(FMath::Clamp(C.R, 0.f, 1.f) * 255.f);
            Pixels[i*4+1] = (uint8)(FMath::Clamp(C.G, 0.f, 1.f) * 255.f);
            Pixels[i*4+2] = (uint8)(FMath::Clamp(C.B, 0.f, 1.f) * 255.f);
            Pixels[i*4+3] = 255;
        }
        WritePNG(Pixels, ERGBFormat::RGBA, TEXT("_basecolor.png"));
    }

    // ── Specular (RGBA, 4 bpp) ────────────────────────────────────────────────
    {
        TArray<uint8> Pixels;
        Pixels.SetNumUninitialized(N * 4);
        for (int i = 0; i < N; i++)
        {
            float u    = (i % W + 0.5f) / W;
            float v    = (i / W + 0.5f) / H;
            float sd   = FMath::Clamp(SimTex.sd[i], 0.f, 1.f);
            float sp   = FMath::Clamp(SimTex.sp[i], 0.f, 1.f);
            float orig = SpData ? sLuminance(sSampleBGRA8(SpData, SpW, SpH, u, v)) : FallbackSpec;
            float spec = FMath::Clamp(orig - sd * 0.3f - sp * 0.2f, 0.f, 1.f);
            uint8 B    = (uint8)(spec * 255.f);
            Pixels[i*4+0] = B; Pixels[i*4+1] = B; Pixels[i*4+2] = B; Pixels[i*4+3] = 255;
        }
        WritePNG(Pixels, ERGBFormat::RGBA, TEXT("_specular.png"));
    }

    // ── Roughness (grayscale, 1 bpp) ──────────────────────────────────────────
    {
        TArray<uint8> Pixels;
        Pixels.SetNumUninitialized(N);
        for (int i = 0; i < N; i++)
        {
            float u    = (i % W + 0.5f) / W;
            float v    = (i / W + 0.5f) / H;
            float orig = RoData ? sLuminance(sSampleBGRA8(RoData, RoW, RoH, u, v)) : FallbackRo;
            float val  = FMath::Clamp(orig + SimTex.sr[i] * RoughnessScale, 0.f, 1.f);
            Pixels[i]  = (uint8)(val * 255.f);
        }
        WritePNG(Pixels, ERGBFormat::Gray, TEXT("_roughness.png"));
    }

    // Unlock source data
    if (BCData   && OrigBC)    OrigBC->Source.UnlockMip(0);
    if (SpData   && OrigSpec)  OrigSpec->Source.UnlockMip(0);
    if (RoData   && OrigRough) OrigRough->Source.UnlockMip(0);

    UE_LOG(LogTemp, Log, TEXT("[GammaTon] Manifold PNGs saved → %s"), *SaveDir);
    return SaveDir;
}
