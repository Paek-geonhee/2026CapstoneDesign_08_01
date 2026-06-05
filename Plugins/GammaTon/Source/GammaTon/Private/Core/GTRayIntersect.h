#pragma once
#include "GTCore.h"
#include <vector>

struct GTHitResult {
    bool  valid      = false;
    float t          = 0.0f;
    int   geom_id    = -1;
    int   tri_idx    = -1;
    int   surfel_idx = -1;
    float alpha      = 0.0f;
    float beta       = 0.0f;
    GTVec3 position;
};

class GTRayIntersector {
public:
    GTRayIntersector();
    ~GTRayIntersector();

    int  addMesh(const GTMesh& mesh);  // returns geomID
    void commit();
    GTHitResult intersect(GTVec3 origin, GTVec3 dir) const;

private:
    int globalSurfelIndex(int geom_id, int prim_id) const;

    void*              device_  = nullptr;
    void*              scene_   = nullptr;
    std::vector<int>   geom_offsets_;  // surfel index start per geomID
    int                total_tris_    = 0;
};
