#pragma once
#include "GTCore.h"
#include "GTRayIntersect.h"

class GTSimulator {
public:
    GTSimulator(std::vector<GTSurfel>&       surfels,
                const GTRayIntersector&      intersector,
                const std::vector<GTMesh>&   meshes,
                const GTSimConfig&           config,
                std::vector<GTObjTexture>*   textures);

    GTIterationStats runIteration();

    // Update per-surfel γ-reflectance from accumulated material (paper §3.4).
    // Uses each surfel's base_reflectance (set at scene build from per-actor values).
    void updateReflectance();

    // Apply cross-channel material interactions (paper §3.4).
    // Updates both textures (visual) and surfel materials (simulation feedback).
    void applyCrossChannel(const GTCrossChannelRules& rules);

    // Trace one γ-ton for debugging: records the full path without modifying textures.
    GTRayPath traceTonDebug();

private:
    // Source selection
    int  selectTypeSource(const GTTonType& type, std::mt19937& rng) const;
    void sampleSource(const GTGammaSource& src, std::mt19937& rng,
                      GTVec3& out_origin, GTVec3& out_dir) const;

    // Core tracing loop — one γ-ton path (type carries its own rules and sources)
    void traceTon(GTTon ton, const GTTonType& type, GTIterationStats& stats, std::mt19937& rng);

    // UV and base-color interpolation at the hit point using barycentric coords
    GTVec2 hitAtlasUV  (const GTHitResult& hit, const GTSurfel& surfel) const;
    GTVec3 hitBaseColor(const GTHitResult& hit, const GTSurfel& surfel) const;

    // Piecewise-linear parabolic ray cast (paper §3.1 kp trajectory).
    // Returns the first hit along the gravity-curved path; out_arrival_dir holds
    // the direction of the segment that hit. Returns invalid hit if path escapes.
    // If out_waypoints != nullptr, intermediate segment endpoints are appended for
    // debug visualization of the gravity-curved arc.
    GTHitResult intersectParabolic(GTVec3 origin, GTVec3 launch_dir,
                                   GTVec3& out_arrival_dir, std::mt19937& rng,
                                   std::vector<GTVec3>* out_waypoints = nullptr) const;

    // Sampling utilities (no member access — pure functions)
    static void   buildFrame(const GTVec3& n, GTVec3& t, GTVec3& b);
    static GTVec3 sampleCone(const GTVec3& axis, float spread_rad, std::mt19937& rng);
    static GTVec3 randomHemisphereDir(const GTVec3& normal, std::mt19937& rng);
    static GTVec3 randomTangentDir(const GTVec3& normal, std::mt19937& rng);

    std::vector<GTSurfel>&       surfels_;
    const GTRayIntersector&      intersector_;
    const std::vector<GTMesh>&   meshes_;
    std::vector<GTObjTexture>*   textures_;
    GTSimConfig                  config_;
    std::mt19937                 rng_;
};
