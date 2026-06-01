#pragma once
// Pure C++ sim core — no UE types. Uses GT_PI to avoid conflict with UE's PI macro.
#include <cmath>
#include <algorithm>
#include <vector>
#include <random>

static constexpr float GT_PI = 3.14159265358979323846f;

// ── Math ──────────────────────────────────────────────────────────────────────

struct GTVec3 {
    float x = 0, y = 0, z = 0;
    GTVec3() = default;
    GTVec3(float x, float y, float z) : x(x), y(y), z(z) {}
    GTVec3 operator+(GTVec3 b) const { return {x+b.x, y+b.y, z+b.z}; }
    GTVec3 operator-(GTVec3 b) const { return {x-b.x, y-b.y, z-b.z}; }
    GTVec3 operator*(float s)  const { return {x*s, y*s, z*s}; }
    float  dot(GTVec3 b)       const { return x*b.x + y*b.y + z*b.z; }
    GTVec3 cross(GTVec3 b)     const {
        return { y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x };
    }
    float  length()     const { return std::sqrt(x*x + y*y + z*z); }
    GTVec3 normalized() const {
        float l = length();
        return l > 1e-8f ? GTVec3{x/l, y/l, z/l} : GTVec3{0,0,0};
    }
};
inline GTVec3 operator*(float s, GTVec3 v) { return v * s; }

struct GTVec2 {
    float x = 0, y = 0;
    GTVec2() = default;
    GTVec2(float x, float y) : x(x), y(y) {}
};

inline GTVec3 GTLerp(GTVec3 a, GTVec3 b, float t) {
    return a * (1.0f-t) + b * t;
}

// ── Mesh types ────────────────────────────────────────────────────────────────

struct GTVertex {
    GTVec3 pos;
    GTVec3 normal;
    GTVec2 uv;
    GTVec2 atlas_uv;
    GTVec3 base_color = { 1.0f, 1.0f, 1.0f };  // RGB from BaseColor texture, default white
};

struct GTTriangle {
    uint32_t v0, v1, v2;
};

struct GTMesh {
    std::vector<GTVertex>   vertices;
    std::vector<GTTriangle> triangles;
};

// ── Weathering types ──────────────────────────────────────────────────────────

// Per-surface reflectance parameters — control how fast each motion
// probability decays as a γ-ton bounces (see GTMotionProbs::deteriorate).
struct GTGammaReflectance {
    float delta_s = 0.5f;  // specular decay per hit  (ks → settle)
    float delta_p = 0.0f;  // diffuse decay per hit   (kp → kf)
    float delta_f = 0.0f;  // flow decay per hit      (kf → settle)
};

// Accumulated material deposited on a surface (four aging channels).
struct GTMaterialProps {
    float sd = 0.0f;  // soiling density   (dust coverage)
    float sp = 0.0f;  // soiling pigment   (colour stain)
    float sr = 0.0f;  // soiling roughness (surface micro-texture)
    float sh = 0.0f;  // soiling humidity  (moisture / wetness)
};

struct GTSurfel {
    GTVec3             position;
    GTVec3             normal;
    GTVec2             uv;         // atlas UV centroid
    int                geom_id = -1;
    // base_reflectance: user-configured per-actor decay rates — never touched after init.
    // reflectance: live copy that updateReflectance() adjusts each iteration based on
    // accumulated material (e.g. rougher surface → higher delta_s → faster ks decay).
    GTGammaReflectance base_reflectance;
    GTGammaReflectance reflectance;
    // material tracks settled deposits on this surfel — used for:
    //   · sh feedback: wet surfaces reduce ks/kp so particles stick more easily
    //   · applyCrossChannel: sh drives sr growth, humidity decay
    //   · stain-bleeding PICKUP: surface sp/sr is absorbed by passing γ-tons
    GTMaterialProps    material;
};

// ── Texture atlas ─────────────────────────────────────────────────────────────
// 4-channel aging map — matches γ-ton carrier fields exactly.
// Memory layout (BGRA8): B=sr  G=sp  R=sd  A=sh

struct GTObjTexture {
    int width  = 512;
    int height = 512;
    std::vector<float> sd;   // soiling density   [0,1]  → R
    std::vector<float> sp;   // soiling pigment   [0,1]  → G
    std::vector<float> sr;   // soiling roughness [0,1]  → B
    std::vector<float> sh;   // soiling humidity  [0,1]  → A

    GTObjTexture()
        : width(512), height(512),
          sd(512*512,0.f), sp(512*512,0.f), sr(512*512,0.f), sh(512*512,0.f) {}
    GTObjTexture(int w, int h)
        : width(w), height(h),
          sd(w*h,0.f), sp(w*h,0.f), sr(w*h,0.f), sh(w*h,0.f) {}

    // 3×3 Gaussian splat: distributes deposit over a pixel neighbourhood.
    // Kernel: corners=1/16, edges=2/16, center=4/16 (sum=1).
    void deposit(float u, float v, float d_sd, float d_sp, float d_sr, float d_sh) {
        int cx = (int)(u * width);
        int cy = (int)(v * height);
        static const int   offs[3]    = { -1, 0, 1 };
        static const float weights[3] = { 1.f, 2.f, 1.f };  // 1-D separable
        for (int dy = 0; dy < 3; dy++) {
            int py = cy + offs[dy];
            if (py < 0 || py >= height) continue;
            for (int dx = 0; dx < 3; dx++) {
                int px = cx + offs[dx];
                if (px < 0 || px >= width) continue;
                float w = weights[dx] * weights[dy] / 16.0f;
                int   i = py * width + px;
                sd[i] = std::min(1.0f, sd[i] + d_sd * w);
                sp[i] = std::min(1.0f, sp[i] + d_sp * w);
                sr[i] = std::min(1.0f, sr[i] + d_sr * w);
                sh[i] = std::min(1.0f, sh[i] + d_sh * w);
            }
        }
    }

    // [B] Threshold + Sigmoid: sharpens dirty/clean boundary.
    // Values at or below `threshold` → 0; values above pushed toward 1 via sigmoid.
    void applyThresholdSigmoid(float threshold, float steepness = 8.0f) {
        auto proc = [&](std::vector<float>& ch) {
            for (float& v : ch) {
                if (v <= threshold) { v = 0.0f; continue; }
                float t = (v - threshold) / (1.0f - threshold);
                float s = 1.0f / (1.0f + std::exp(-steepness * (t - 0.5f)));
                v = threshold + s * (1.0f - threshold);
            }
        };
        proc(sd); proc(sp); proc(sr); proc(sh);
    }

    // [A] Fractal noise mask: smooth organic dirty/clean patches with no hard edges.
    // Uses layered value noise (fractal/fbm) — no polygonal Voronoi artifacts.
    // scale controls patch size: 2=very large blobs, 4=medium(default), 8=small texture.
    void applyNoiseMask(int octaves, uint32_t seed, float strength, float scale = 4.0f) {
        if (strength <= 0.0f || octaves <= 0) return;

        // Deterministic integer hash → [0,1]
        auto hashf = [](int x, int y, uint32_t s) -> float {
            uint32_t n = ((uint32_t)(x * 1619 + y * 31337)) ^ s;
            n = (n ^ (n >> 13u)) * 1274126177u;
            return (float)(n & 0xffffu) / 65535.0f;
        };
        // Perlin's C2-continuous smootherstep
        auto smooth = [](float t) -> float {
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        };
        // Bilinear interpolated value noise
        auto noise2D = [&](float x, float y, uint32_t s) -> float {
            int   ix = (int)std::floor(x), iy = (int)std::floor(y);
            float fx = x - (float)ix,      fy = y - (float)iy;
            float sx = smooth(fx),          sy = smooth(fy);
            float a  = hashf(ix,   iy,   s), b = hashf(ix+1, iy,   s);
            float c  = hashf(ix,   iy+1, s), d = hashf(ix+1, iy+1, s);
            return a + (b-a)*sx + (c-a)*sy + (a-b-c+d)*sx*sy;
        };

        int N = width * height;
        for (int i = 0; i < N; i++) {
            float px = (i % width)  / (float)width  * scale;
            float py = (i / width)  / (float)height * scale;
            // Fractal sum (fBm): multiple octaves, each halved amplitude
            float val = 0.f, amp = 1.0f, freq = 1.0f, total = 0.0f;
            for (int o = 0; o < octaves; o++) {
                val   += noise2D(px * freq, py * freq, seed + (uint32_t)o * 1234u) * amp;
                total += amp;
                amp  *= 0.5f; freq *= 2.0f;
            }
            float noise = val / total;                  // [0,1] smooth organic field
            float mult  = 1.0f - noise * strength;      // [1-strength, 1]
            sd[i] *= mult; sp[i] *= mult; sr[i] *= mult; sh[i] *= mult;
        }
    }

    // Separable Gaussian blur (kernel [1,4,6,4,1]/16 × 2 passes).
    // Smooths per-iteration noise without shifting deposit position.
    void blur(int passes = 1) {
        static const float K[5] = { 1.f, 4.f, 6.f, 4.f, 1.f };
        std::vector<float> tmp(width * height);
        auto blurCh = [&](std::vector<float>& ch) {
            for (int p = 0; p < passes; p++) {
                // horizontal pass
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        float acc = 0.0f, wsum = 0.0f;
                        for (int d = -2; d <= 2; d++) {
                            int sx = x + d;
                            if (sx < 0 || sx >= width) continue;
                            float w = K[d + 2];
                            acc += ch[y * width + sx] * w;
                            wsum += w;
                        }
                        tmp[y * width + x] = acc / wsum;
                    }
                }
                // vertical pass
                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        float acc = 0.0f, wsum = 0.0f;
                        for (int d = -2; d <= 2; d++) {
                            int sy = y + d;
                            if (sy < 0 || sy >= height) continue;
                            float w = K[d + 2];
                            acc += tmp[sy * width + x] * w;
                            wsum += w;
                        }
                        ch[y * width + x] = acc / wsum;
                    }
                }
            }
        };
        blurCh(sd); blurCh(sp); blurCh(sr); blurCh(sh);
    }
};

// ── Surfel generation ─────────────────────────────────────────────────────────

inline std::vector<GTSurfel> GTGenerateSurfels(
    const GTMesh& mesh, const GTGammaReflectance& refl, int geom_id)
{
    std::vector<GTSurfel> surfels;
    surfels.reserve(mesh.triangles.size());
    for (const auto& tri : mesh.triangles) {
        const GTVertex& v0 = mesh.vertices[tri.v0];
        const GTVertex& v1 = mesh.vertices[tri.v1];
        const GTVertex& v2 = mesh.vertices[tri.v2];
        GTVec3 e1 = v1.pos - v0.pos;
        GTVec3 e2 = v2.pos - v0.pos;
        GTSurfel s;
        s.normal   = e1.cross(e2).normalized();
        s.position = (v0.pos + v1.pos + v2.pos) * (1.0f/3.0f);
        s.uv       = { (v0.atlas_uv.x + v1.atlas_uv.x + v2.atlas_uv.x) / 3.0f,
                       (v0.atlas_uv.y + v1.atlas_uv.y + v2.atlas_uv.y) / 3.0f };
        s.geom_id  = geom_id;
        s.base_reflectance = refl;
        s.reflectance      = refl;
        s.material = {};
        // material.sp starts at 0: inherent surface colour is sampled per-hit via
        // GTSimulator::hitBaseColor (barycentric) so surfel.sp tracks only settled stain.
        surfels.push_back(s);
    }
    return surfels;
}

// ── GammaTon motion / carrier ─────────────────────────────────────────────────

// Probability triplet for the three non-settle events.
// settle probability = max(0, 1 - ks - kp - kf).
struct GTMotionProbs {
    float ks = 1.0f;  // specular reflection
    float kp = 0.0f;  // diffuse bounce
    float kf = 0.0f;  // surface flow

    float settleProbability() const { return std::max(0.0f, 1.0f - ks - kp - kf); }

    // Degrade motion probabilities after each surface hit (paper §3.2).
    // ks decays by delta_s → increases settle probability over time.
    // kp decays by delta_p; the leaked amount converts to kf (bounce → flow).
    // kf decays by delta_f → eventually settles.
    // The three values are normalised so their sum never exceeds 1.
    void deteriorate(const GTGammaReflectance& r) {
        float new_ks = std::max(ks - r.delta_s, 0.0f);
        float new_kp = std::max(kp - r.delta_p, 0.0f);
        // Paper eq.(3): k_f' = max(k_f + max(k_p - Δp, 0) - Δf, 0)
        // max(k_p - Δp, 0) == new_kp after the clamp above
        float new_kf = std::max(kf + new_kp - r.delta_f, 0.0f);
        ks = new_ks; kp = new_kp; kf = new_kf;
        float total = ks + kp + kf;
        if (total > 1.0f) { float s = 1.0f / total; ks *= s; kp *= s; kf *= s; }
    }
};

struct GTTon {
    GTMotionProbs   motion;
    GTMaterialProps carrier;
};

// ── Source types ──────────────────────────────────────────────────────────────

enum class GTSourceType { AREA_TOP, DIRECTIONAL, POINT, ENVIRONMENT };

struct GTGammaSource {
    GTSourceType type        = GTSourceType::AREA_TOP;
    GTVec3       center      = {0.0f, 0.0f, 1400.0f};  // Z-up (UE coords, cm)
    GTVec3       direction   = {0.0f, 0.0f, -1.0f};    // downward
    float        spread_deg  = 5.0f;
    float        area_half_x = 500.0f;  // cm
    float        area_half_z = 500.0f;  // cm
    float        intensity   = 1.0f;
};

// ── Transport rules (paper §2 / §3.5 user-defined γ-transport) ───────────────
// A rule: `to.channel += from.channel * coeff * global_k`
// Applied at the specified event during γ-ton tracing.
// global_k = pickup_k for PICKUP, deposit_k for FLOW/SETTLE.

enum class GTTransportChannel : int { SD = 0, SP = 1, SR = 2, SH = 3 };
enum class GTTransportEntity  : int { TON = 0, SURFACE = 1 };
enum class GTTransportEvent   : int { PICKUP = 0, FLOW = 1, SETTLE = 2 };

struct GTTransportRule {
    GTTransportEvent   event        = GTTransportEvent::SETTLE;
    GTTransportEntity  to_entity    = GTTransportEntity::SURFACE;
    GTTransportChannel to_channel   = GTTransportChannel::SD;
    GTTransportEntity  from_entity  = GTTransportEntity::TON;
    GTTransportChannel from_channel = GTTransportChannel::SD;
    float              coeff        = 1.0f;
};

// Default rules that replicate the hardcoded pickup/deposit behaviour.
inline std::vector<GTTransportRule> GTDefaultTransportRules() {
    using E = GTTransportEvent; using En = GTTransportEntity; using Ch = GTTransportChannel;
    return {
        // PICKUP: ton absorbs surface pigment and humidity on every contact
        { E::PICKUP, En::TON, Ch::SP, En::SURFACE, Ch::SP, 1.0f },
        { E::PICKUP, En::TON, Ch::SH, En::SURFACE, Ch::SH, 1.0f },
        // FLOW: partial deposit during surface flow
        { E::FLOW,   En::SURFACE, Ch::SP, En::TON, Ch::SP, 0.25f },
        { E::FLOW,   En::SURFACE, Ch::SH, En::TON, Ch::SH, 0.25f },
        // SETTLE: full deposit when ton comes to rest
        { E::SETTLE, En::SURFACE, Ch::SD, En::TON, Ch::SD, 1.0f },
        { E::SETTLE, En::SURFACE, Ch::SP, En::TON, Ch::SP, 1.0f },
        { E::SETTLE, En::SURFACE, Ch::SR, En::TON, Ch::SR, 1.0f },
        { E::SETTLE, En::SURFACE, Ch::SH, En::TON, Ch::SH, 1.0f },
    };
}

// ── Ton type ──────────────────────────────────────────────────────────────────
// A fully self-contained γ-ton class.  Multiple types allow simultaneous
// rain + moss, dust + salt-spray etc. (paper §3.5 complex scenarios).
struct GTTonType {
    std::string              name         = "Ton Type";
    float                    weight       = 1.0f;          // emission weight vs other types
    GTMotionProbs            init_motion  = { 1.0f, 0.0f, 0.0f };
    GTMaterialProps          init_carrier = { 1.0f, 0.0f, 0.0f, 0.0f };
    std::vector<GTGammaSource>   sources  = { GTGammaSource{} };
    std::vector<GTTransportRule> rules    = GTDefaultTransportRules();
};

// ── Simulation config ─────────────────────────────────────────────────────────

struct GTSimConfig {
    int   n_tons_per_iter = 30000;  // γ-tons emitted per iteration
    int   max_bounces     = 10;     // hard cap; ton escapes if exceeded
    float flow_step       = 30.0f;  // cm moved per surface-flow event
    float deposit_k       = 0.50f;  // global deposit scale factor
    float pickup_k        = 0.10f;  // γ-transport pick-up rate (surface → ton carrier)

    // kp (parabolic bounce) parameters (paper §3.1)
    float bounce_distance  = 50.0f;  // cm — max travel of one kp bounce
    // parabola_gravity: z-velocity decrement per step relative to launch speed.
    // 0.5 → total downward pull = 0.5× launch speed over all steps (gentle arc).
    float parabola_gravity = 0.5f;
    int   parabola_steps   = 5;      // piecewise linear segments per parabola

    // One or more γ-ton types; each is emitted in proportion to its weight.
    std::vector<GTTonType>  ton_types = { GTTonType{} };

    // [D] Probabilistic deposit: fraction of settling tons that actually leave a deposit.
    // 1.0 = all tons deposit (default); 0.3 = only 30% deposit → sparse, patchy result.
    float probabilistic_deposit = 1.0f;
};

// ── Post-process config (applied to GTObjTexture after all iterations) ────────
struct GTPostProcessConfig {
    // [D] Probabilistic deposit — forwarded into GTSimConfig before simulation
    float probabilistic_deposit = 1.0f;   // 1.0=always, 0.3=30% chance

    // [B] Threshold + Sigmoid — dirty/clean boundary sharpening
    bool  useThreshold      = false;
    float threshold         = 0.15f;
    float sigmoid_steepness = 8.0f;

    // [A] Fractal noise masking — smooth organic patch division (no polygonal edges)
    bool     useNoiseMask     = false;
    int      noise_octaves    = 4;      // detail levels: 2=smooth large blobs, 6=detailed
    float    noise_strength   = 0.7f;   // max suppression [0=none, 1=fully suppress darkest areas]
    float    noise_scale      = 4.0f;   // patch frequency: 2=huge patches, 8=small patches
    uint32_t noise_seed       = 42;
};

// ── Cross-channel material rules (paper §3.4) ─────────────────────────────────
// Applied once per iteration after all γ-tons are traced.
// Coefficients match the paper's concrete examples; default 0 = disabled.
struct GTCrossChannelRules {
    // sh drives sr growth: rust/biofilm forms where moisture accumulates
    // Paper rust example: 0.015
    float rust_from_humidity   = 0.0f;
    // sh evaporation per iteration (multiplicative decay)
    // Paper example: 0.5 (halved each iteration)
    float humidity_decay       = 0.0f;
    // sp (pigment/moss) suppresses sd (dust/dirt): overgrowth buries underlying material
    // Paper moss example: 0.1
    float pigment_covers_dust  = 0.0f;
};

// ── Stats ─────────────────────────────────────────────────────────────────────

struct GTIterationStats {
    int   settled        = 0;
    int   escaped        = 0;
    int   total_bounces  = 0;
    float dust_deposited = 0.0f;
    int   ev_reflect     = 0;
    int   ev_bounce      = 0;
    int   ev_flow        = 0;
    int   ev_settle      = 0;

    float avgBounces() const {
        int n = settled + escaped;
        return n > 0 ? (float)total_bounces / n : 0.0f;
    }
    void accumulate(const GTIterationStats& o) {
        settled += o.settled; escaped += o.escaped;
        total_bounces += o.total_bounces; dust_deposited += o.dust_deposited;
        ev_reflect += o.ev_reflect; ev_bounce += o.ev_bounce;
        ev_flow += o.ev_flow; ev_settle += o.ev_settle;
    }
};

// ── Debug ray path ─────────────────────────────────────────────────────────────

enum class GTBounceEvent { Reflect, Diffuse, Flow, Settle, Escape };

inline const char* GTBounceEventName(GTBounceEvent e) {
    switch (e) {
    case GTBounceEvent::Reflect: return "Specular Reflect";
    case GTBounceEvent::Diffuse: return "Diffuse Bounce";
    case GTBounceEvent::Flow:    return "Surface Flow";
    case GTBounceEvent::Settle:  return "Settle";
    case GTBounceEvent::Escape:  return "Escaped";
    default:                     return "Unknown";
    }
}

// One intersection event along a single γ-ton's traced path.
struct GTRayHitRecord {
    GTVec3          position;
    GTVec3          normal;           // geometric surface normal
    GTVec3          shading_normal;   // normal used for shading (flipped on back-face hits)
    GTVec3          dir_in;           // incident ray direction at this bounce
    GTVec3          dir_out;          // outgoing ray direction (zero for Settle/Escape)
    GTVec2          uv;
    int             surfel_idx   = -1;
    int             geom_id      = -1;
    GTBounceEvent   event        = GTBounceEvent::Escape;
    GTMotionProbs   motion_before;
    GTMotionProbs   motion_after;
    GTMaterialProps tex_before;
    GTMaterialProps deposit;
    // Populated only for GTBounceEvent::Diffuse (kp parabolic bounce).
    // Stores the intermediate arc waypoints between the launch and landing positions,
    // allowing the debug visualizer to draw the gravity-curved trajectory.
    std::vector<GTVec3> parabola_pts;
};

// Complete path of one debug γ-ton from emission to final outcome.
struct GTRayPath {
    GTVec3                      origin;
    bool                        escaped = true;
    std::vector<GTRayHitRecord> hits;
};
