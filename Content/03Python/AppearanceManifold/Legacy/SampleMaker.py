import numpy as np
from noise import pnoise2


def make_sample_vectorized(sample_size: int):
    # -----------------------------
    # 1. Grid 생성
    # -----------------------------
    xs = np.linspace(0, 1, sample_size)
    ys = np.linspace(0, 1, sample_size)

    nx, ny = np.meshgrid(xs, ys)  # (H, W)

    # -----------------------------
    # 2. t 계산
    # -----------------------------
    d1 = np.sqrt((nx - 0.3)**2 + (ny - 0.3)**2)
    d2 = np.sqrt((nx - 0.7)**2 + (ny - 0.7)**2)

    t = np.exp(-d1 * 5.0) + np.exp(-d2 * 5.0)

    # Perlin noise (벡터화 불가 → 최소화된 loop)
    noise = np.zeros_like(nx)
    for i in range(sample_size):
        for j in range(sample_size):
            noise[i, j] = pnoise2(nx[i, j] * 3.0, ny[i, j] * 3.0)

    noise = (noise + 1.0) * 0.5

    t = 0.8 * t + 0.2 * noise
    t = np.clip(t, 0.0, 1.0)

    # -----------------------------
    # 3. Soft Cluster Weight
    # -----------------------------
    sharpness = 10.0

    def gaussian_weight(cx, cy):
        return np.exp(-((nx - cx)**2 + (ny - cy)**2) * sharpness)

    w0 = gaussian_weight(0.2, 0.3)
    w1 = gaussian_weight(0.7, 0.4)
    w2 = gaussian_weight(0.5, 0.8)

    sum_w = w0 + w1 + w2 + 1e-6
    w0 /= sum_w
    w1 /= sum_w
    w2 /= sum_w

    # -----------------------------
    # 4. Material 정의
    # -----------------------------
    A_D = 0.3 + 0.4 * t
    A_R = 0.1 + 0.8 * t
    A_S = 0.08 * (1.0 - t)

    B_D = 0.5 + 0.3 * np.sin(t * np.pi)
    B_R = 0.2 + 0.6 * t
    B_S = 0.05 * (1.0 - t * t)

    C_D = 0.4
    C_R = 0.2 + 0.7 * t
    C_S = 0.06

    # -----------------------------
    # 5. Soft Blending
    # -----------------------------
    base_diffuse = w0 * A_D + w1 * B_D + w2 * C_D
    base_roughness = w0 * A_R + w1 * B_R + w2 * C_R
    base_specular = w0 * A_S + w1 * B_S + w2 * C_S

    # -----------------------------
    # 6. Noise
    # -----------------------------
    base_roughness += np.random.uniform(-0.02, 0.02, size=base_roughness.shape)

    # -----------------------------
    # 7. Clamp
    # -----------------------------
    base_diffuse = np.clip(base_diffuse, 0.0, 1.0)
    base_roughness = np.clip(base_roughness, 0.0, 1.0)
    base_specular = np.clip(base_specular, 0.0, 1.0)

    # -----------------------------
    # 8. 7D Feature 구성
    # -----------------------------
    # (H, W, 7)
    H, W = base_diffuse.shape

    diffuse = np.stack([base_diffuse]*3, axis=-1)
    specular = np.stack([base_specular]*3, axis=-1)

    data = np.concatenate([
        diffuse,
        specular,
        base_roughness[..., None]
    ], axis=-1)

    return data.astype(np.float32)