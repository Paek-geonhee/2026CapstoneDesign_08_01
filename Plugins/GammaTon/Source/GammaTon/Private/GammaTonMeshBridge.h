#pragma once
#include "CoreMinimal.h"
#include "Core/GTCore.h"
#include "Core/GTRayIntersect.h"

class UStaticMeshComponent;
class AActor;

struct FGTSceneData {
    std::vector<GTMesh>             meshes;
    std::vector<GTSurfel>           surfels;
    // textures / components / actorNames / atlasUVChannels cover only target meshes
    // (indices 0..NumTargetMeshes-1). Occluder meshes follow in meshes[] / surfels[]
    // but have no corresponding texture or component entry.
    std::vector<GTObjTexture>       textures;
    TArray<UStaticMeshComponent*>   components;
    TArray<FString>                 actorNames;
    TArray<int>                     atlasUVChannels;   // per-mesh UV channel used for dust
    int32                           NumTargetMeshes = 0;
    bool valid = false;
};

class FGammaTonMeshBridge {
public:
    // Extract geometry from selected actors. Returns false if no valid actors.
    // reflectances[i] / initialMaterials[i] are applied to target_actors[i].
    // occluder_actors participate in ray intersection but receive no texture output.
    static FGTSceneData BuildScene(
        const TArray<AActor*>&            target_actors,
        const TArray<AActor*>&            occluder_actors,
        const TArray<GTGammaReflectance>& reflectances,
        const TArray<GTMaterialProps>&    initialMaterials,
        GTRayIntersector&                 outIntersector,
        int                               textureSize = 512);

private:
    static bool ExtractMesh(UStaticMeshComponent* comp,
                             GTMesh&               outMesh,
                             int&                  outAtlasChannel);
};
