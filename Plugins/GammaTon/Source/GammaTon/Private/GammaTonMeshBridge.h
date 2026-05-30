#pragma once
#include "CoreMinimal.h"
#include "Core/GTCore.h"
#include "Core/GTRayIntersect.h"

class UStaticMeshComponent;
class AActor;

struct FGTSceneData {
    std::vector<GTMesh>             meshes;
    std::vector<GTSurfel>           surfels;
    std::vector<GTObjTexture>       textures;
    TArray<UStaticMeshComponent*>   components;
    TArray<FString>                 actorNames;
    TArray<int>                     atlasUVChannels;   // per-mesh UV channel used for dust
    bool valid = false;
};

class FGammaTonMeshBridge {
public:
    // Extract geometry from selected actors. Returns false if no valid actors.
    // reflectances[i] is applied to actors[i]; falls back to GTGammaReflectance{} if shorter.
    static FGTSceneData BuildScene(
        const TArray<AActor*>&            actors,
        const TArray<GTGammaReflectance>& reflectances,
        const TArray<GTMaterialProps>&    initialMaterials,
        GTRayIntersector&                 outIntersector,
        int                               textureSize = 512);

private:
    static bool ExtractMesh(UStaticMeshComponent* comp,
                             GTMesh&               outMesh,
                             int&                  outAtlasChannel);
};
