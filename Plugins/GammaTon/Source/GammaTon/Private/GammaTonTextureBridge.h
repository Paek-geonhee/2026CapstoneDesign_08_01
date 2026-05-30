#pragma once
#include "CoreMinimal.h"
#include "Core/GTCore.h"

class UStaticMeshComponent;
class UTexture2D;

class FGammaTonTextureBridge {
public:
    // Create/overwrite an RGBA texture asset at /Game/GammaTon/{ActorName}_Dust.
    // Channels: R=sd(dust), G=sp(pigment), B=sr(roughness), A=sh(humidity)
    static UTexture2D* CreateAndSaveTexture(
        const GTObjTexture& SimTex,
        const FString&      ActorName);

    // Read a previously saved texture back into GTObjTexture for accumulation.
    // No-op if texture is missing or if size has changed between runs.
    static void LoadTextureIntoObjTexture(UTexture2D* Tex, GTObjTexture& Out);

    // Duplicate the actor's original material, inject aging blend nodes, apply via MID.
    // DustTexture / PigmentTexture: optional detail textures; nullptr keeps white default.
    static void ApplyToComponent(UStaticMeshComponent* Comp,
                                 UTexture2D*           AgingTexture,
                                 const FString&        ActorName,
                                 FLinearColor          DustColor,
                                 FLinearColor          PigmentColor,
                                 int                   AtlasUVChannel  = 1,
                                 UTexture2D*           DustTexture     = nullptr,
                                 UTexture2D*           PigmentTexture  = nullptr,
                                 float                 DustVisibility  = 1.0f);

    // Export BaseColor / Specular / Roughness PNGs for Manifold input.
    // Rasterizes Mesh triangles in atlas UV1 space to build a per-texel UV0 map,
    // then composites original PBR textures from Comp's material with AgingTex
    // using bilinear sampling at the correct UV0 position.
    // Saved to {ProjectSavedDir}/GammaTon/Manifold/{Safe_ActorName}_{channel}.png
    // Returns the export directory path, or empty string on failure.
    static FString ExportManifoldPNGs(const GTObjTexture&   SimTex,
                                      const GTMesh&         Mesh,
                                      UStaticMeshComponent* Comp,
                                      const FString&        ActorName,
                                      FLinearColor          DustColor,
                                      FLinearColor          PigmentColor,
                                      float                 DustVisibility = 1.0f,
                                      UTexture2D*           DustTexture    = nullptr,
                                      UTexture2D*           PigmentTexture = nullptr,
                                      float                 WetnessScale   = 0.4f,
                                      float                 RoughnessScale = 0.35f);
};
