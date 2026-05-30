#include "GTRayIntersect.h"

// UE defines 'check' as a macro; Embree uses it as a function name internally.
#pragma push_macro("check")
#undef check
#include <embree4/rtcore.h>
#pragma pop_macro("check")

GTRayIntersector::GTRayIntersector() {
    device_ = rtcNewDevice(nullptr);
    scene_  = rtcNewScene((RTCDevice)device_);
}

GTRayIntersector::~GTRayIntersector() {
    if (scene_)  rtcReleaseScene((RTCScene)scene_);
    if (device_) rtcReleaseDevice((RTCDevice)device_);
}

int GTRayIntersector::addMesh(const GTMesh& mesh) {
    RTCGeometry geom = rtcNewGeometry((RTCDevice)device_, RTC_GEOMETRY_TYPE_TRIANGLE);

    float* vb = (float*)rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
        3 * sizeof(float), mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        vb[i*3+0] = mesh.vertices[i].pos.x;
        vb[i*3+1] = mesh.vertices[i].pos.y;
        vb[i*3+2] = mesh.vertices[i].pos.z;
    }

    unsigned* ib = (unsigned*)rtcSetNewGeometryBuffer(
        geom, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
        3 * sizeof(unsigned), mesh.triangles.size());
    for (size_t i = 0; i < mesh.triangles.size(); i++) {
        ib[i*3+0] = mesh.triangles[i].v0;
        ib[i*3+1] = mesh.triangles[i].v1;
        ib[i*3+2] = mesh.triangles[i].v2;
    }

    rtcCommitGeometry(geom);
    int geomID = (int)rtcAttachGeometry((RTCScene)scene_, geom);
    rtcReleaseGeometry(geom);

    geom_offsets_.push_back(total_tris_);
    total_tris_ += (int)mesh.triangles.size();
    return geomID;
}

void GTRayIntersector::commit() {
    rtcCommitScene((RTCScene)scene_);
}

int GTRayIntersector::globalSurfelIndex(int geom_id, int prim_id) const {
    if (geom_id < 0 || geom_id >= (int)geom_offsets_.size()) return -1;
    return geom_offsets_[geom_id] + prim_id;
}

GTHitResult GTRayIntersector::intersect(GTVec3 origin, GTVec3 dir) const {
    RTCRayHit rh;
    rh.ray.org_x = origin.x; rh.ray.org_y = origin.y; rh.ray.org_z = origin.z;
    rh.ray.dir_x = dir.x;    rh.ray.dir_y = dir.y;    rh.ray.dir_z = dir.z;
    // tnear = 1e-3f avoids self-intersection with the surface the ton just left.
    // The normal offset applied before each bounce (0.01 cm) keeps this consistent.
    rh.ray.tnear = 1e-3f;
    rh.ray.tfar  = 1e30f;
    rh.ray.mask  = 0xFFFFFFFF;
    rh.ray.flags = 0;
    rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rh.hit.primID = RTC_INVALID_GEOMETRY_ID;

    RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);
    rtcIntersect1((RTCScene)scene_, &rh, &args);

    GTHitResult result;
    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return result;

    result.valid      = true;
    result.t          = rh.ray.tfar;
    result.geom_id    = (int)rh.hit.geomID;
    result.tri_idx    = (int)rh.hit.primID;
    result.alpha      = rh.hit.u;
    result.beta       = rh.hit.v;
    result.surfel_idx = globalSurfelIndex(result.geom_id, result.tri_idx);
    result.position   = { origin.x + dir.x * rh.ray.tfar,
                          origin.y + dir.y * rh.ray.tfar,
                          origin.z + dir.z * rh.ray.tfar };
    return result;
}
