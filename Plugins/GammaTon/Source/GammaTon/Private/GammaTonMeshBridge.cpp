#include "GammaTonMeshBridge.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Components/StaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSample.h"

// Returns the UTexture2D that feeds the BaseColor of MatIface, or nullptr.
//
// Strategy 1: trace BaseColor.Expression in the base UMaterial graph.
// Strategy 2: search all texture parameters by name (handles M_Default parents
//             where Strategy 1 finds no expression, common in Fab assets).
// Strategy 3: fall back to the first available texture parameter.
static UTexture2D* FindBaseColorTex(UMaterialInterface* MatIface)
{
    if (!MatIface) return nullptr;

    auto NameMatchesColor = [](const FString& Name) {
        static const TCHAR* Keywords[] = {
            TEXT("BaseColor"), TEXT("Base_Color"), TEXT("Base Color"),
            TEXT("Albedo"),    TEXT("Diffuse"),    TEXT("Color"),     TEXT("Colour")
        };
        for (const TCHAR* Kw : Keywords)
            if (Name.Contains(Kw, ESearchCase::IgnoreCase)) return true;
        return false;
    };

    UMaterial* Base = nullptr;
    if (auto* M  = Cast<UMaterial>(MatIface))            Base = M;
    else if (auto* MI = Cast<UMaterialInstance>(MatIface)) Base = MI->GetMaterial();

    // Strategy 1 — BaseColor input pin, direct cast
    if (Base) {
        if (UMaterialEditorOnlyData* Ed = Base->GetEditorOnlyData()) {
            if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Ed->BaseColor.Expression)) {
                UTexture* T = nullptr;
                if (MatIface->GetTextureParameterValue(FMaterialParameterInfo(P->ParameterName), T))
                    if (auto* T2D = Cast<UTexture2D>(T)) return T2D;
                if (auto* T2D = Cast<UTexture2D>(P->Texture)) return T2D;
            }
            if (auto* S = Cast<UMaterialExpressionTextureSample>(Ed->BaseColor.Expression))
                if (auto* T2D = Cast<UTexture2D>(S->Texture)) return T2D;
        }
    }

    // Strategy 2 — GetAllTextureParameterInfo + GetTextureParameterValue
    {
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Guids;
        MatIface->GetAllTextureParameterInfo(Infos, Guids);

        for (const FMaterialParameterInfo& P : Infos) {
            if (NameMatchesColor(P.Name.ToString())) {
                UTexture* T = nullptr;
                if (MatIface->GetTextureParameterValue(P, T))
                    if (auto* T2D = Cast<UTexture2D>(T)) return T2D;
            }
        }
    }

    // Strategy 3 — scan expression collection directly (handles UMaterial where
    // GetTextureParameterValue returns false for non-overridden defaults)
    if (Base) {
        if (UMaterialEditorOnlyData* Ed = Base->GetEditorOnlyData()) {
            // First pass: keyword match
            for (UMaterialExpression* Expr : Base->GetExpressionCollection().Expressions) {
                if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr)) {
                    if (NameMatchesColor(P->ParameterName.ToString()))
                        if (auto* T2D = Cast<UTexture2D>(P->Texture)) return T2D;
                }
            }
            // Second pass: any TextureSampleParameter2D
            for (UMaterialExpression* Expr : Base->GetExpressionCollection().Expressions) {
                if (auto* P = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
                    if (auto* T2D = Cast<UTexture2D>(P->Texture)) return T2D;
            }
            // Third pass: any TextureSample
            for (UMaterialExpression* Expr : Base->GetExpressionCollection().Expressions) {
                if (auto* S = Cast<UMaterialExpressionTextureSample>(Expr))
                    if (auto* T2D = Cast<UTexture2D>(S->Texture)) return T2D;
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("[GammaTon] FindBaseColorTex: no texture found in %s"), *MatIface->GetName());
    return nullptr;
}

bool FGammaTonMeshBridge::ExtractMesh(UStaticMeshComponent* Comp, GTMesh& OutMesh, int& OutAtlasChannel) {
    if (!Comp || !Comp->GetStaticMesh()) return false;
    UStaticMesh* SM = Comp->GetStaticMesh();
    if (!SM->GetRenderData() || SM->GetRenderData()->LODResources.IsEmpty()) return false;

    const FStaticMeshLODResources& LOD   = SM->GetRenderData()->LODResources[0];
    const FPositionVertexBuffer&   PosVB = LOD.VertexBuffers.PositionVertexBuffer;
    const FStaticMeshVertexBuffer& VertVB = LOD.VertexBuffers.StaticMeshVertexBuffer;

    uint32 NumVerts      = PosVB.GetNumVertices();
    uint32 NumUVChannels = VertVB.GetNumTexCoords();

    // UV channel 1 is the lightmap / atlas channel when available.
    // Fall back to channel 0 on single-UV meshes so simulation still runs.
    int AtlasChannel = (NumUVChannels > 1) ? 1 : 0;
    OutAtlasChannel  = AtlasChannel;
    UE_LOG(LogTemp, Log, TEXT("[GammaTon] Mesh UV channels: %d  →  atlas channel: %d"), NumUVChannels, AtlasChannel);

    FTransform Transform = Comp->GetComponentTransform();

    OutMesh.vertices.reserve(NumVerts);
    for (uint32 vi = 0; vi < NumVerts; vi++) {
        FVector3f LocalPos  = PosVB.VertexPosition(vi);
        FVector   WorldPos  = Transform.TransformPosition(FVector(LocalPos));

        // VertexTangentZ returns FVector4f (XYZ = normal, W = bitangent sign).
        FVector4f TangentZ4   = VertVB.VertexTangentZ(vi);
        FVector   WorldNormal = Transform.TransformVectorNoScale(
            FVector(TangentZ4.X, TangentZ4.Y, TangentZ4.Z));
        WorldNormal.Normalize();

        FVector2f UV0     = (NumUVChannels > 0) ? VertVB.GetVertexUV(vi, 0) : FVector2f::ZeroVector;
        FVector2f AtlasUV = VertVB.GetVertexUV(vi, AtlasChannel);

        GTVertex v;
        v.pos      = GTVec3{ (float)WorldPos.X, (float)WorldPos.Y, (float)WorldPos.Z };
        v.normal   = GTVec3{ (float)WorldNormal.X, (float)WorldNormal.Y, (float)WorldNormal.Z };
        v.uv       = GTVec2{ UV0.X, UV0.Y };
        v.atlas_uv = GTVec2{ AtlasUV.X, AtlasUV.Y };
        OutMesh.vertices.push_back(v);
    }

    TArray<uint32> Indices;
    LOD.IndexBuffer.GetCopy(Indices);
    OutMesh.triangles.reserve(Indices.Num() / 3);
    for (int32 i = 0; i + 2 < Indices.Num(); i += 3) {
        GTTriangle tri;
        tri.v0 = Indices[i];
        tri.v1 = Indices[i+1];
        tri.v2 = Indices[i+2];
        OutMesh.triangles.push_back(tri);
    }

    // Bake BaseColor texture into per-vertex base_color (UV0, single lock for performance).
    // base_color defaults to {1,1,1} (white) so simulation is unchanged if no texture found.
    UMaterialInterface* MatIface = SM->GetMaterial(0);
    UTexture2D* BaseColorTex = FindBaseColorTex(MatIface);
    bool bBaked = false;
    if (BaseColorTex && BaseColorTex->Source.IsValid()) {
        const ETextureSourceFormat Fmt = BaseColorTex->Source.GetFormat();
        if (Fmt == TSF_BGRA8) {
            const int TW = BaseColorTex->Source.GetSizeX();
            const int TH = BaseColorTex->Source.GetSizeY();
            uint8* TexSrc = BaseColorTex->Source.LockMip(0);
            if (TexSrc) {
                for (auto& v : OutMesh.vertices) {
                    float u  = v.uv.x - std::floor(v.uv.x);   // wrap [0,1)
                    float vv = v.uv.y - std::floor(v.uv.y);
                    int px = FMath::Clamp((int)(u  * TW), 0, TW - 1);
                    int py = FMath::Clamp((int)(vv * TH), 0, TH - 1);
                    int bi = (py * TW + px) * 4;               // BGRA8: B=0,G=1,R=2,A=3
                    v.base_color = { TexSrc[bi+2]/255.f, TexSrc[bi+1]/255.f, TexSrc[bi+0]/255.f };
                }
                BaseColorTex->Source.UnlockMip(0);
                bBaked = true;
                UE_LOG(LogTemp, Log, TEXT("[GammaTon] BaseColor baked for %d vertices from %s"),
                    (int)OutMesh.vertices.size(), *BaseColorTex->GetName());
            }
        } else {
            UE_LOG(LogTemp, Warning,
                TEXT("[GammaTon] BaseColor tex found (%s) but source format %d is not TSF_BGRA8 — bake skipped"),
                *BaseColorTex->GetName(), (int)Fmt);
        }
    }
    if (!bBaked && !BaseColorTex) {
        UE_LOG(LogTemp, Log, TEXT("[GammaTon] BaseColor bake skipped (no compatible texture found)"));
    }

    return !OutMesh.triangles.empty();
}

FGTSceneData FGammaTonMeshBridge::BuildScene(
    const TArray<AActor*>&            Actors,
    const TArray<GTGammaReflectance>& Reflectances,
    const TArray<GTMaterialProps>&    InitialMaterials,
    GTRayIntersector&                 OutIntersector,
    int                               TextureSize)
{
    FGTSceneData Scene;

    for (int32 ai = 0; ai < Actors.Num(); ai++) {
        AActor* Actor = Actors[ai];
        if (!Actor) continue;
        UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>();
        if (!SMC) continue;

        GTMesh Mesh;
        int AtlasChannel = 0;
        if (!ExtractMesh(SMC, Mesh, AtlasChannel)) continue;

        int GeomId = (int)Scene.meshes.size();
        // Bounds-check: callers may pass empty arrays when per-actor settings are unused.
        // Default-constructed GTGammaReflectance / GTMaterialProps = no weathering bias.
        GTGammaReflectance Refl    = (ai < Reflectances.Num())    ? Reflectances[ai]    : GTGammaReflectance{};
        GTMaterialProps    InitMat = (ai < InitialMaterials.Num()) ? InitialMaterials[ai] : GTMaterialProps{};

        OutIntersector.addMesh(Mesh);

        auto SurfelVec = GTGenerateSurfels(Mesh, Refl, GeomId);
        for (auto& s : SurfelVec) {
            s.material = InitMat;  // 논문 §4: 액터별 초기 재질값 (stain-bleeding 전제조건)
            Scene.surfels.push_back(s);
        }

        Scene.textures.emplace_back(TextureSize, TextureSize);
        Scene.meshes.push_back(std::move(Mesh));
        Scene.components.Add(SMC);
        Scene.actorNames.Add(Actor->GetName());
        Scene.atlasUVChannels.Add(AtlasChannel);
    }

    if (!Scene.meshes.empty()) {
        OutIntersector.commit();
        Scene.valid = true;
    }

    return Scene;
}
