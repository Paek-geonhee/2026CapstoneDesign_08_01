#include "GTSimulator.h"
#include <algorithm>
#include <cmath>

// ── Transport rule helpers ────────────────────────────────────────────────────

static float sChannelVal(const GTMaterialProps& m, GTTransportChannel ch) {
    switch (ch) {
    case GTTransportChannel::SD: return m.sd;
    case GTTransportChannel::SP: return m.sp;
    case GTTransportChannel::SR: return m.sr;
    case GTTransportChannel::SH: return m.sh;
    }
    return 0.0f;
}
static void sChannelAdd(GTMaterialProps& m, GTTransportChannel ch, float v) {
    switch (ch) {
    case GTTransportChannel::SD: m.sd = std::min(1.0f, m.sd + v); break;
    case GTTransportChannel::SP: m.sp = std::min(1.0f, m.sp + v); break;
    case GTTransportChannel::SR: m.sr = std::min(1.0f, m.sr + v); break;
    case GTTransportChannel::SH: m.sh = std::min(1.0f, m.sh + v); break;
    }
}

GTSimulator::GTSimulator(std::vector<GTSurfel>&      surfels,
                         const GTRayIntersector&     intersector,
                         const std::vector<GTMesh>&  meshes,
                         const GTSimConfig&          config,
                         std::vector<GTObjTexture>*  textures)
    : surfels_(surfels), intersector_(intersector),
      meshes_(meshes), textures_(textures),
      config_(config), rng_(std::random_device{}())
{}

// ── Source selection ──────────────────────────────────────────────────────────

// Weighted random selection among a ton type's sources by intensity.
int GTSimulator::selectTypeSource(const GTTonType& type, std::mt19937& rng) const {
    if (type.sources.empty()) return 0;
    float total = 0.0f;
    for (const auto& s : type.sources) total += s.intensity;
    std::uniform_real_distribution<float> u(0.0f, total);
    float r = u(rng), accum = 0.0f;
    for (int i = 0; i < (int)type.sources.size(); i++) {
        accum += type.sources[i].intensity;
        if (r <= accum) return i;
    }
    return (int)type.sources.size() - 1;
}

// ── Sampling utilities ────────────────────────────────────────────────────────

// Build an orthonormal tangent frame (T, B) from a normal N.
// Chooses the axis least aligned with N to avoid numerical degeneracy.
void GTSimulator::buildFrame(const GTVec3& n, GTVec3& t, GTVec3& b) {
    if (std::fabs(n.x) > 0.9f)
        t = GTVec3{0,1,0}.cross(n).normalized();
    else
        t = GTVec3{1,0,0}.cross(n).normalized();
    b = n.cross(t);
}

// Uniform cone sampling around `axis` within half-angle `spread_rad`.
// Uses the standard inverse-CDF: cos_theta = 1 - u*(1 - cos_max).
GTVec3 GTSimulator::sampleCone(const GTVec3& axis, float spread_rad, std::mt19937& rng) {
    if (spread_rad <= 1e-5f) return axis.normalized();
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    float cos_max   = std::cos(spread_rad);
    float cos_theta = 1.0f - u01(rng) * (1.0f - cos_max);
    float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
    float phi       = 2.0f * GT_PI * u01(rng);
    GTVec3 t, b;
    buildFrame(axis.normalized(), t, b);
    return (t * (sin_theta * std::cos(phi)) +
            b * (sin_theta * std::sin(phi)) +
            axis.normalized() * cos_theta).normalized();
}

// Cosine-weighted hemisphere sampling (importance-samples the Lambertian BRDF).
// cos_theta = sqrt(r2) gives the cosine distribution; sin_theta follows.
// TODO : AI한테 이 수식이 나온 출처 물어봐서 출처 (논문명 또는 링크) 남겨놓기
// https://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations
GTVec3 GTSimulator::randomHemisphereDir(const GTVec3& normal, std::mt19937& rng) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    float r1 = u01(rng), r2 = u01(rng);
    float phi       = 2.0f * GT_PI * r1;
    float cos_theta = r2;                        
    float sin_theta = std::sqrt(1.0f - r2 * r2);
    GTVec3 t, b;
    buildFrame(normal, t, b);
    return (t * (sin_theta * std::cos(phi)) +
            b * (sin_theta * std::sin(phi)) +
            normal * cos_theta).normalized();
}

// Uniform random direction in the tangent plane (used for surface flow).
GTVec3 GTSimulator::randomTangentDir(const GTVec3& normal, std::mt19937& rng) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    float angle = u01(rng) * 2.0f * GT_PI;
    GTVec3 t, b;
    buildFrame(normal, t, b);
    return (t * std::cos(angle) + b * std::sin(angle)).normalized();
}

// ── Source sampling ───────────────────────────────────────────────────────────

void GTSimulator::sampleSource(const GTGammaSource& src, std::mt19937& rng,
                                GTVec3& out_origin, GTVec3& out_dir) const {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    float spread_rad = src.spread_deg * GT_PI / 180.0f;
    GTVec3 d = src.direction.normalized();

    switch (src.type) {
    case GTSourceType::AREA_TOP: {
        // Jittered emission over a horizontal rectangle at src.center.z
        float ox = src.center.x + (u01(rng)*2.0f - 1.0f) * src.area_half_x;
        float oy = src.center.y + (u01(rng)*2.0f - 1.0f) * src.area_half_z;
        out_origin = { ox, oy, src.center.z };
        out_dir    = sampleCone(d, spread_rad, rng);
        break;
    }
    case GTSourceType::DIRECTIONAL: {
        // Parallel beam over a plane perpendicular to the emission direction
        GTVec3 t, b;
        buildFrame(d, t, b);
        float u = (u01(rng)*2.0f - 1.0f) * src.area_half_x;
        float v = (u01(rng)*2.0f - 1.0f) * src.area_half_z;
        out_origin = src.center + t * u + b * v;
        out_dir    = sampleCone(d, spread_rad, rng);
        break;
    }
    case GTSourceType::POINT: {
        out_origin = src.center;
        out_dir    = sampleCone(d, spread_rad, rng);
        break;
    }
    case GTSourceType::ENVIRONMENT: {
        // Hemispherical environment source: γ-tons fire inward from points on a sphere.
        // area_half_x is the sphere radius; center is the scene centre.
        float theta = std::acos(1.0f - 2.0f * u01(rng));
        float phi   = 2.0f * GT_PI * u01(rng);
        GTVec3 sphere_pt = {
            std::sin(theta) * std::cos(phi),
            std::sin(theta) * std::sin(phi),
            std::cos(theta)
        };
        out_origin = src.center + sphere_pt * src.area_half_x;
        out_dir    = (GTVec3{0,0,0} - sphere_pt).normalized();  // inward
        if (src.spread_deg > 0.01f)
            out_dir = sampleCone(out_dir, spread_rad, rng);
        break;
    }
    }
}

// ── UV interpolation ──────────────────────────────────────────────────────────

// Interpolate atlas UV at the hit point using barycentric coordinates.
// Falls back to the surfel centroid UV if geometry indices are invalid.
GTVec2 GTSimulator::hitAtlasUV(const GTHitResult& hit, const GTSurfel& surfel) const {
    if (hit.geom_id < 0 || hit.geom_id >= (int)meshes_.size()) return surfel.uv;
    const GTMesh& mesh = meshes_[hit.geom_id];
    if (hit.tri_idx < 0 || hit.tri_idx >= (int)mesh.triangles.size()) return surfel.uv;
    const GTTriangle& tri = mesh.triangles[hit.tri_idx];
    float w0 = 1.0f - hit.alpha - hit.beta;
    GTVec2 uv0 = mesh.vertices[tri.v0].atlas_uv;
    GTVec2 uv1 = mesh.vertices[tri.v1].atlas_uv;
    GTVec2 uv2 = mesh.vertices[tri.v2].atlas_uv;
    return { uv0.x*w0 + uv1.x*hit.alpha + uv2.x*hit.beta,
             uv0.y*w0 + uv1.y*hit.alpha + uv2.y*hit.beta };
}

// Interpolate base_color at the hit point using barycentric coordinates.
// Returns {1,1,1} (white) when geometry is unavailable — keeps sp pick-up neutral.
GTVec3 GTSimulator::hitBaseColor(const GTHitResult& hit, const GTSurfel& surfel) const {
    if (hit.geom_id < 0 || hit.geom_id >= (int)meshes_.size()) return {1,1,1};
    const GTMesh& mesh = meshes_[hit.geom_id];
    if (hit.tri_idx < 0 || hit.tri_idx >= (int)mesh.triangles.size()) return {1,1,1};
    const GTTriangle& tri = mesh.triangles[hit.tri_idx];
    float w0 = 1.0f - hit.alpha - hit.beta;
    return mesh.vertices[tri.v0].base_color * w0
         + mesh.vertices[tri.v1].base_color * hit.alpha
         + mesh.vertices[tri.v2].base_color * hit.beta;
}

// ── Parabolic ray cast ────────────────────────────────────────────────────────
//
// Approximates the gravity-curved kp trajectory as piecewise linear segments
// (paper §3.1). Each step tilts the velocity downward by parabola_gravity/steps.
// Returns the first surface hit; out_arrival_dir is the segment direction at impact.
GTHitResult GTSimulator::intersectParabolic(GTVec3 origin, GTVec3 launch_dir,
                                              GTVec3& out_arrival_dir, std::mt19937& /*rng*/,
                                              std::vector<GTVec3>* out_waypoints) const {
    const int   steps    = config_.parabola_steps;
    const float step_len = config_.bounce_distance / (float)steps;
    const float grav_inc = config_.parabola_gravity / (float)steps;

    GTVec3 vel = launch_dir.normalized();
    GTVec3 pos = origin;

    for (int s = 0; s < steps; s++) {
        GTVec3 seg_end = pos + vel * step_len;
        GTVec3 seg_dir = (seg_end - pos).normalized();

        GTHitResult hit = intersector_.intersect(pos, seg_dir);
        if (hit.valid && hit.t <= step_len + 1.0f) {
            out_arrival_dir = seg_dir;
            return hit;
        }

        pos = seg_end;
        if (out_waypoints) out_waypoints->push_back(pos);

        GTVec3 raw = { vel.x, vel.y, vel.z - grav_inc };
        float  rlen = raw.length();
        if (rlen > 1e-6f) vel = { raw.x/rlen, raw.y/rlen, raw.z/rlen };
    }

    out_arrival_dir = vel;
    GTHitResult invalid;  // valid == false by default
    return invalid;
}

// ── Core tracing ──────────────────────────────────────────────────────────────

// Trace one γ-ton from source to final deposit or escape (paper §3).
//
// Russian Roulette at each hit: ks (hemisphere), kp (parabolic), kf (flow),
// 1-ks-kp-kf (settle). Both ks and kp use uniform hemisphere sampling per paper §3.2:
// "we regard the surface as 'diffuse' and evenly distribute outgoing directions."
void GTSimulator::traceTon(GTTon ton, const GTTonType& type, GTIterationStats& stats, std::mt19937& rng) {
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    int    src_idx = selectTypeSource(type, rng);
    GTVec3 origin, dir;
    sampleSource(type.sources.empty() ? GTGammaSource{} : type.sources[src_idx], rng, origin, dir);

    bool        has_pending = false;
    GTHitResult pending;
    GTVec3      pending_dir;

    for (int bounce = 0; bounce < config_.max_bounces; bounce++) {
        GTHitResult hit;
        if (has_pending) {
            hit = pending;
            dir = pending_dir;
            has_pending = false;
        } else {
            hit = intersector_.intersect(origin, dir);
        }

        if (!hit.valid || hit.surfel_idx < 0 || hit.surfel_idx >= (int)surfels_.size()) {
            stats.escaped++;
            stats.total_bounces += bounce;
            return;
        }

        GTSurfel& surfel = surfels_[hit.surfel_idx];
        ton.motion.deteriorate(surfel.reflectance);

        float sh_surf = surfel.material.sh;
        float ks = ton.motion.ks * (1.0f - 0.5f * sh_surf);
        float kp = ton.motion.kp * (1.0f - 0.3f * sh_surf);
        float kf = ton.motion.kf;

        float xi = u01(rng);
        GTVec3 n = (dir.dot(surfel.normal) > 0.0f) ? surfel.normal * -1.0f : surfel.normal;

        // Fixed: base-color luminance → ton pigment (encodes initial surface material)
        {
            GTVec3 hitColor = hitBaseColor(hit, surfel);
            float  colorLum = 0.299f * hitColor.x + 0.587f * hitColor.y + 0.114f * hitColor.z;
            ton.carrier.sp  = std::min(1.0f, ton.carrier.sp + colorLum * config_.pickup_k);
        }
        // User-defined PICKUP rules (paper §3.5)
        for (const auto& rule : type.rules) {
            if (rule.event != GTTransportEvent::PICKUP) continue;
            float src = (rule.from_entity == GTTransportEntity::TON)
                ? sChannelVal(ton.carrier, rule.from_channel)
                : sChannelVal(surfel.material, rule.from_channel);
            float delta = src * rule.coeff * config_.pickup_k;
            if (rule.to_entity == GTTransportEntity::TON)
                sChannelAdd(ton.carrier, rule.to_channel, delta);
            else
                sChannelAdd(surfel.material, rule.to_channel, delta);
        }

        if (xi < ks) {
            // ── ks: hemisphere uniform (paper §3.2 — "diffuse" surface model) ──
            stats.ev_reflect++;
            // Each specular bounce loses a small fraction of the carrier (particle
            // partially sticks to the surface), creating a natural density gradient
            // that diminishes with distance from the source.
            ton.carrier.sd *= 0.92f;
            ton.carrier.sp *= 0.95f;
            ton.carrier.sr *= 0.92f;
            ton.carrier.sh *= 0.88f;
            origin = hit.position + n * 0.01f;
            dir    = randomHemisphereDir(n, rng);

        } else if (xi < ks + kp) {
            // ── kp: parabolic bounce (paper §3.1 — gravity-curved trajectory) ──
            stats.ev_bounce++;
            GTVec3 launch_dir = randomHemisphereDir(n, rng);
            GTVec3 arrival_dir;
            GTHitResult ph = intersectParabolic(hit.position + n * 0.01f, launch_dir, arrival_dir, rng);
            origin = hit.position + n * 0.01f;
            if (ph.valid && ph.surfel_idx >= 0 && ph.surfel_idx < (int)surfels_.size()) {
                has_pending = true;
                pending     = ph;
                pending_dir = arrival_dir;
            } else {
                dir = launch_dir;
            }

        } else if (xi < ks + kp + kf) {
            // ── kf: surface flow — user-defined FLOW rules ────────────────────
            stats.ev_flow++;

            if (textures_ && hit.geom_id >= 0 && hit.geom_id < (int)textures_->size()) {
                float d_sd=0.0f, d_sp=0.0f, d_sr=0.0f, d_sh=0.0f;
                for (const auto& rule : type.rules) {
                    if (rule.event != GTTransportEvent::FLOW) continue;
                    float src = (rule.from_entity == GTTransportEntity::TON)
                        ? sChannelVal(ton.carrier, rule.from_channel)
                        : sChannelVal(surfel.material, rule.from_channel);
                    float delta = src * rule.coeff * config_.deposit_k;
                    if (rule.to_entity == GTTransportEntity::SURFACE) {
                        switch (rule.to_channel) {
                        case GTTransportChannel::SD: d_sd += delta; break;
                        case GTTransportChannel::SP: d_sp += delta; break;
                        case GTTransportChannel::SR: d_sr += delta; break;
                        case GTTransportChannel::SH: d_sh += delta; break;
                        }
                    } else {
                        sChannelAdd(ton.carrier, rule.to_channel, delta);
                    }
                }
                GTVec2 uv = hitAtlasUV(hit, surfel);
                (*textures_)[hit.geom_id].deposit(uv.x, uv.y, d_sd, d_sp, d_sr, d_sh);
            }

            GTVec3 gravity   = {0.0f, 0.0f, -1.0f};
            GTVec3 grav_tang = gravity - n * n.dot(gravity);
            GTVec3 flow_dir;
            if (grav_tang.length() > 0.2f)
                flow_dir = (grav_tang.normalized() * 0.7f + randomTangentDir(n, rng) * 0.3f).normalized();
            else
                flow_dir = randomTangentDir(n, rng);

            // Move along surface tangent, then cast back toward surface via -n.
            // Shooting in flow_dir (tangent) causes horizontal surfaces to never
            // re-intersect — the particle escapes upward into empty space.
            GTVec3 flow_dest = hit.position + flow_dir * config_.flow_step;
            origin = flow_dest + n * (config_.flow_step * 0.15f + 2.0f);
            dir    = n * -1.0f;

        } else {
            // ── settle — user-defined SETTLE rules ────────────────────────────
            float upward = std::max(0.0f, n.z);
            float w_sd   = 0.2f + 0.8f * upward;  // upward faces collect more dust

            // sp (pigment/moss) — 논문 Fig.9: 틈새/그늘에만 이끼
            // ① 습도 임계값: surface sh가 0.20 이상인 곳에서만 sp 침착
            // ② 방향 가중치: 위를 향한 노출면(upward) 억제 → 그늘진 수직면 선호
            const float kShThresh = 0.20f;
            float w_sp_moisture = (sh_surf < kShThresh)
                ? 0.0f
                : (sh_surf - kShThresh) / (1.0f - kShThresh);
            float w_sp = w_sp_moisture * (1.0f - upward * 0.7f);

            float d_sd=0.0f, d_sp=0.0f, d_sr=0.0f, d_sh=0.0f;
            for (const auto& rule : type.rules) {
                if (rule.event != GTTransportEvent::SETTLE) continue;
                float src = (rule.from_entity == GTTransportEntity::TON)
                    ? sChannelVal(ton.carrier, rule.from_channel)
                    : sChannelVal(surfel.material, rule.from_channel);
                float scale = rule.coeff * config_.deposit_k;
                if (rule.to_entity == GTTransportEntity::SURFACE) {
                    if (rule.to_channel == GTTransportChannel::SD)
                        scale *= w_sd;
                    else if (rule.to_channel == GTTransportChannel::SP)
                        scale *= w_sp;
                }
                float delta = src * scale;
                if (rule.to_entity == GTTransportEntity::SURFACE) {
                    switch (rule.to_channel) {
                    case GTTransportChannel::SD: d_sd += delta; break;
                    case GTTransportChannel::SP: d_sp += delta; break;
                    case GTTransportChannel::SR: d_sr += delta; break;
                    case GTTransportChannel::SH: d_sh += delta; break;
                    }
                } else {
                    sChannelAdd(ton.carrier, rule.to_channel, delta);
                }
            }

            if (textures_ && hit.geom_id >= 0 && hit.geom_id < (int)textures_->size()) {
                GTVec2 uv = hitAtlasUV(hit, surfel);
                (*textures_)[hit.geom_id].deposit(uv.x, uv.y, d_sd, d_sp, d_sr, d_sh);
            }

            surfel.material.sd = std::min(1.0f, surfel.material.sd + d_sd);
            surfel.material.sp = std::min(1.0f, surfel.material.sp + d_sp);
            surfel.material.sr = std::min(1.0f, surfel.material.sr + d_sr);
            surfel.material.sh = std::min(1.0f, surfel.material.sh + d_sh);

            stats.dust_deposited += d_sd;
            stats.ev_settle++;
            stats.settled++;
            stats.total_bounces += bounce + 1;
            return;
        }
    }
    stats.escaped++;
    stats.total_bounces += config_.max_bounces;
}

// Fire one full iteration: n_tons_per_iter γ-tons, each selected by weight
// (paper §2-D: "each γ-ton type has a weight … assigned by the user").
// The selection is a standard weighted-random alias walk: draw u ∈ [0, total_weight],
// walk the type list accumulating weight until the first type whose cumulative weight
// exceeds u — gives exact per-type probability proportional to weight.
GTIterationStats GTSimulator::runIteration() {
    GTIterationStats stats;
    if (config_.ton_types.empty()) return stats;

    float total_weight = 0.0f;
    for (const auto& t : config_.ton_types) total_weight += std::max(0.0f, t.weight);
    if (total_weight <= 0.0f) return stats;
    std::uniform_real_distribution<float> wu(0.0f, total_weight);

    for (int i = 0; i < config_.n_tons_per_iter; i++) {
        float wr = wu(rng_), acc = 0.0f;
        int type_idx = (int)config_.ton_types.size() - 1;
        for (int ti = 0; ti < (int)config_.ton_types.size(); ti++) {
            acc += config_.ton_types[ti].weight;
            if (wr <= acc) { type_idx = ti; break; }
        }
        const GTTonType& tontype = config_.ton_types[type_idx];
        GTTon ton;
        ton.motion  = tontype.init_motion;
        ton.carrier = tontype.init_carrier;
        traceTon(ton, tontype, stats, rng_);
    }
    return stats;
}

// Cross-channel material interaction (paper §3.4).
// Applied once per iteration after all γ-tons have been traced.
//
// Why two loops — textures AND surfels?
// · textures[] are the per-pixel visual accumulators written to disk as AgingTex.
//   They must be updated so the rendered output shows rust growth, sh decay, etc.
// · surfels[].material drives physics feedback: sh_surf → ks/kp clamping in traceTon,
//   sp in PICKUP rules, sr in updateReflectance. Without updating surfels here, the
//   next iteration's humidity feedback would use stale values from before cross-channel.
void GTSimulator::applyCrossChannel(const GTCrossChannelRules& rules) {
    const float rh  = rules.rust_from_humidity;
    const float hd  = rules.humidity_decay;
    const float pcd = rules.pigment_covers_dust;

    // Visual output: update per-pixel texture channels.
    if (textures_) {
        for (auto& tex : *textures_) {
            int n = tex.width * tex.height;
            for (int i = 0; i < n; i++) {
                if (rh  > 0.0f) tex.sr[i] = std::min(1.0f, tex.sr[i] + rh  * tex.sh[i]);
                if (hd  > 0.0f) tex.sh[i] = std::max(0.0f, tex.sh[i] * (1.0f - hd));
                if (pcd > 0.0f) tex.sd[i] = std::max(0.0f, tex.sd[i] - pcd * tex.sp[i]);
            }
        }
    }

    // Physics state: keep surfels in sync so the next iteration uses correct sh/sr.
    for (auto& s : surfels_) {
        if (rh  > 0.0f) s.material.sr = std::min(1.0f, s.material.sr + rh  * s.material.sh);
        if (hd  > 0.0f) s.material.sh = std::max(0.0f, s.material.sh * (1.0f - hd));
        if (pcd > 0.0f) s.material.sd = std::max(0.0f, s.material.sd - pcd * s.material.sp);
    }
}

// Update live γ-reflectance from accumulated surfel material (paper §3.4).
// base_reflectance stores the user-configured per-actor rates and is never modified;
// reflectance is recalculated from scratch each iteration so it stays proportional
// to current deposits rather than drifting upward unboundedly.
// Intuition: a rougher (higher sr) surface traps more new γ-tons → delta_s rises;
// a wetter (higher sh) surface makes parabolic/flow events less likely → delta_p/f rise.
void GTSimulator::updateReflectance() {
    for (auto& s : surfels_) {
        s.reflectance.delta_s = std::min(1.0f, s.base_reflectance.delta_s + s.material.sr * 0.3f);
        s.reflectance.delta_p = std::min(1.0f, s.base_reflectance.delta_p + s.material.sh * 0.2f);
        s.reflectance.delta_f = std::min(1.0f, s.base_reflectance.delta_f + s.material.sh * 0.1f);
    }
}

// ── Debug single-ray trace ────────────────────────────────────────────────────
//
// Identical physics to traceTon() but records each bounce into GTRayPath
// and does NOT call texture.deposit() — read-only with respect to scene state.
GTRayPath GTSimulator::traceTonDebug() {
    std::mt19937 dbg_rng(std::random_device{}());
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    GTRayPath path;
    path.escaped = true;

    const GTTonType& type = config_.ton_types.empty()
        ? GTTonType{}
        : config_.ton_types[0];

    GTTon ton;
    ton.motion  = type.init_motion;
    ton.carrier = type.init_carrier;

    int    src_idx = selectTypeSource(type, dbg_rng);
    GTVec3 origin, dir;
    sampleSource(type.sources.empty() ? GTGammaSource{} : type.sources[src_idx], dbg_rng, origin, dir);
    path.origin = origin;

    bool        has_pending = false;
    GTHitResult pending;
    GTVec3      pending_dir;

    for (int bounce = 0; bounce < config_.max_bounces; bounce++) {
        GTHitResult hit;
        if (has_pending) {
            hit = pending;
            dir = pending_dir;
            has_pending = false;
        } else {
            hit = intersector_.intersect(origin, dir);
        }

        if (!hit.valid || hit.surfel_idx < 0 || hit.surfel_idx >= (int)surfels_.size()) {
            GTRayHitRecord esc;
            esc.position = origin + dir * 5000.0f;
            esc.event    = GTBounceEvent::Escape;
            path.hits.push_back(esc);
            return path;
        }

        GTSurfel& surfel = surfels_[hit.surfel_idx];

        GTRayHitRecord rec;
        rec.position   = hit.position;
        rec.normal     = surfel.normal;
        rec.surfel_idx = hit.surfel_idx;
        rec.geom_id    = hit.geom_id;
        rec.uv         = hitAtlasUV(hit, surfel);

        if (textures_ && hit.geom_id >= 0 && hit.geom_id < (int)textures_->size()) {
            const GTObjTexture& tex = (*textures_)[hit.geom_id];
            int px = std::clamp((int)(rec.uv.x * tex.width),  0, tex.width  - 1);
            int py = std::clamp((int)(rec.uv.y * tex.height), 0, tex.height - 1);
            int ti = py * tex.width + px;
            rec.tex_before = { tex.sd[ti], tex.sp[ti], tex.sr[ti], tex.sh[ti] };
        }

        rec.motion_before = ton.motion;
        ton.motion.deteriorate(surfel.reflectance);
        rec.motion_after = ton.motion;

        float sh_surf = surfel.material.sh;
        float ks = ton.motion.ks * (1.0f - 0.5f * sh_surf);
        float kp = ton.motion.kp * (1.0f - 0.3f * sh_surf);
        float kf = ton.motion.kf;

        float xi = u01(dbg_rng);
        GTVec3 n = (dir.dot(surfel.normal) > 0.0f) ? surfel.normal * -1.0f : surfel.normal;
        rec.dir_in       = dir;
        rec.shading_normal = n;

        // Fixed: base-color luminance → ton pigment
        {
            GTVec3 hitColor = hitBaseColor(hit, surfel);
            float  colorLum = 0.299f * hitColor.x + 0.587f * hitColor.y + 0.114f * hitColor.z;
            ton.carrier.sp  = std::min(1.0f, ton.carrier.sp + colorLum * config_.pickup_k);
        }
        // User-defined PICKUP rules
        for (const auto& rule : type.rules) {
            if (rule.event != GTTransportEvent::PICKUP) continue;
            float src = (rule.from_entity == GTTransportEntity::TON)
                ? sChannelVal(ton.carrier, rule.from_channel)
                : sChannelVal(surfel.material, rule.from_channel);
            float delta = src * rule.coeff * config_.pickup_k;
            if (rule.to_entity == GTTransportEntity::TON)
                sChannelAdd(ton.carrier, rule.to_channel, delta);
            else
                sChannelAdd(surfel.material, rule.to_channel, delta);
        }

        if (xi < ks) {
            // ks: hemisphere uniform (paper §3.2)
            rec.event   = GTBounceEvent::Reflect;
            GTVec3 newDir = randomHemisphereDir(n, dbg_rng);
            rec.dir_out = newDir;
            origin = hit.position + n * 0.01f;
            dir    = newDir;

        } else if (xi < ks + kp) {
            // kp: parabolic bounce — collect waypoints for arc visualization
            rec.event = GTBounceEvent::Diffuse;
            GTVec3 launch_dir = randomHemisphereDir(n, dbg_rng);
            GTVec3 arrival_dir;
            GTHitResult ph = intersectParabolic(
                hit.position + n * 0.01f, launch_dir, arrival_dir, dbg_rng,
                &rec.parabola_pts);
            rec.dir_out = launch_dir;
            origin = hit.position + n * 0.01f;
            if (ph.valid && ph.surfel_idx >= 0 && ph.surfel_idx < (int)surfels_.size()) {
                has_pending = true;
                pending     = ph;
                pending_dir = arrival_dir;
            } else {
                dir = launch_dir;
            }

        } else if (xi < ks + kp + kf) {
            rec.event = GTBounceEvent::Flow;
            // Compute FLOW rule deposits for debug record
            {
                float d_sd=0.0f, d_sp=0.0f, d_sr=0.0f, d_sh=0.0f;
                for (const auto& rule : type.rules) {
                    if (rule.event != GTTransportEvent::FLOW) continue;
                    float src = (rule.from_entity == GTTransportEntity::TON)
                        ? sChannelVal(ton.carrier, rule.from_channel)
                        : sChannelVal(surfel.material, rule.from_channel);
                    float delta = src * rule.coeff * config_.deposit_k;
                    if (rule.to_entity == GTTransportEntity::SURFACE) {
                        switch (rule.to_channel) {
                        case GTTransportChannel::SD: d_sd += delta; break;
                        case GTTransportChannel::SP: d_sp += delta; break;
                        case GTTransportChannel::SR: d_sr += delta; break;
                        case GTTransportChannel::SH: d_sh += delta; break;
                        }
                    }
                }
                rec.deposit = { d_sd, d_sp, d_sr, d_sh };
            }

            GTVec3 gravity   = { 0.0f, 0.0f, -1.0f };
            GTVec3 grav_tang = gravity - n * n.dot(gravity);
            GTVec3 flow_dir;
            if (grav_tang.length() > 0.2f)
                flow_dir = (grav_tang.normalized() * 0.7f + randomTangentDir(n, dbg_rng) * 0.3f).normalized();
            else
                flow_dir = randomTangentDir(n, dbg_rng);
            rec.dir_out = flow_dir;
            // Mirror traceTon fix: lift off surface, shoot back into it via -n
            GTVec3 flow_dest = hit.position + flow_dir * config_.flow_step;
            origin = flow_dest + n * (config_.flow_step * 0.15f + 2.0f);
            dir    = n * -1.0f;

        } else {
            rec.event = GTBounceEvent::Settle;
            float upward = std::max(0.0f, n.z);
            float w_sd   = 0.2f + 0.8f * upward;

            const float kShThresh = 0.20f;
            float w_sp_moisture = (sh_surf < kShThresh)
                ? 0.0f
                : (sh_surf - kShThresh) / (1.0f - kShThresh);
            float w_sp = w_sp_moisture * (1.0f - upward * 0.7f);

            float d_sd=0.0f, d_sp=0.0f, d_sr=0.0f, d_sh=0.0f;
            for (const auto& rule : type.rules) {
                if (rule.event != GTTransportEvent::SETTLE) continue;
                float src = (rule.from_entity == GTTransportEntity::TON)
                    ? sChannelVal(ton.carrier, rule.from_channel)
                    : sChannelVal(surfel.material, rule.from_channel);
                float scale = rule.coeff * config_.deposit_k;
                if (rule.to_entity == GTTransportEntity::SURFACE) {
                    if (rule.to_channel == GTTransportChannel::SD)
                        scale *= w_sd;
                    else if (rule.to_channel == GTTransportChannel::SP)
                        scale *= w_sp;
                }
                float delta = src * scale;
                if (rule.to_entity == GTTransportEntity::SURFACE) {
                    switch (rule.to_channel) {
                    case GTTransportChannel::SD: d_sd += delta; break;
                    case GTTransportChannel::SP: d_sp += delta; break;
                    case GTTransportChannel::SR: d_sr += delta; break;
                    case GTTransportChannel::SH: d_sh += delta; break;
                    }
                }
            }
            rec.deposit = { d_sd, d_sp, d_sr, d_sh };
        }

        path.hits.push_back(rec);

        if (rec.event == GTBounceEvent::Settle) {
            path.escaped = false;
            return path;
        }
    }

    GTRayHitRecord esc;
    esc.position = origin + dir * 5000.0f;
    esc.event    = GTBounceEvent::Escape;
    path.hits.push_back(esc);
    return path;
}
