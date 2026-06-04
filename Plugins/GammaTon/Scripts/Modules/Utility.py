import numpy as np
from PIL import Image
import matplotlib.pyplot as plt


def load_and_downsample(basecolor_path, specular_path, roughness_path, target_size=256):
    """
    target_size: int or (h, w)
    """

    # -----------------------------
    # 1. 이미지 로드
    # -----------------------------
    base = Image.open(basecolor_path).convert("RGB")
    spec = Image.open(specular_path).convert("RGB")
    rough = Image.open(roughness_path).convert("L")  # 1채널

    # -----------------------------
    # 2. 리사이즈 (핵심)
    # -----------------------------
    base = base.resize((target_size, target_size), Image.BILINEAR)
    spec = spec.resize((target_size, target_size), Image.BILINEAR)
    rough = rough.resize((target_size, target_size), Image.BILINEAR)

    # -----------------------------
    # 3. numpy 변환 + normalize
    # -----------------------------
    base_np = np.asarray(base).astype(np.float32) / 255.0
    spec_np = np.asarray(spec).astype(np.float32) / 255.0
    rough_np = np.asarray(rough).astype(np.float32) / 255.0

    # -----------------------------
    # 4. 출력
    # -----------------------------
    return base_np, spec_np, rough_np

def combine_to_7d_tensor(base_np, spec_np, rough_np):
    """
    base_np:  (H, W, 3)
    spec_np:  (H, W, 3)
    rough_np: (H, W)

    return:
        (H, W, 7)
    """

    # -----------------------------
    # 1. shape 검증
    # -----------------------------
    assert base_np.shape[:2] == spec_np.shape[:2] == rough_np.shape[:2]

    # -----------------------------
    # 2. roughness 차원 확장
    # -----------------------------
    rough_expanded = rough_np[..., np.newaxis]  # (H, W, 1)

    # -----------------------------
    # 3. 채널 기준 결합
    # -----------------------------
    data_7d = np.concatenate(
        [base_np, spec_np, rough_expanded],
        axis=2
    )

    return data_7d

def combine_to_7d(diffuse, specular, roughness):
    """
    diffuse: (H, W, 3)
    specular: (H, W, 3)
    roughness: (H, W)
    """

    H, W, _ = diffuse.shape

    # -----------------------------
    # 1. reshape
    # -----------------------------
    d = diffuse.reshape(-1, 3)
    s = specular.reshape(-1, 3)
    r = roughness.reshape(-1, 1)

    # -----------------------------
    # 2. concatenate
    # -----------------------------
    data_7d = np.concatenate([d, s, r], axis=1)

    return data_7d  # (N, 7)

def compute_weather_score(samples_7d):

    """
    samples_7d: (N,7)

    Returns
    -------
    weather_score : (N,)
    """

    diffuse = samples_7d[:, 0:3]
    specular = samples_7d[:, 3:6]
    roughness = samples_7d[:, 6]

    # =====================================================
    # 1. Brightness
    # =====================================================

    brightness = diffuse.mean(axis=1)

    # =====================================================
    # 2. Saturation
    # =====================================================

    max_c = diffuse.max(axis=1)
    min_c = diffuse.min(axis=1)

    saturation = (
        (max_c - min_c)
        / (max_c + 1e-6)
    )

    # =====================================================
    # 3. Specular intensity
    # =====================================================

    spec_intensity = specular.mean(axis=1)

    # =====================================================
    # 4. Roughness
    # =====================================================

    rough_term = np.sqrt(
        np.clip(roughness, 0, 1)
    )

    # =====================================================
    # 5. Weather score
    # =====================================================

    weather_score = (
        0.40 * rough_term +
        0.30 * (1.0 - spec_intensity) +
        0.20 * (1.0 - saturation) +
        0.10 * (1.0 - brightness)
    )

    # normalize
    weather_score -= weather_score.min()

    weather_score /= (
        weather_score.max() + 1e-8
    )

    return weather_score.astype(np.float32)

def normalize_features(X):

    X = X.astype(np.float32)

    min_val = np.min(X, axis=0, keepdims=True)
    max_val = np.max(X, axis=0, keepdims=True)

    return (X - min_val) / (max_val - min_val + 1e-8)



def visualize_MDS_Graph_With_Trajectory(
    Z,
    edge_src,
    edge_dst,
    trajectory,
    data_7d=None,
    elev=30,
    azim=120,
    scatter_size=3,
    edge_alpha=0.15,
    edge_width=0.4,
    trajectory_width=4
):
    """
    Params
    ----------
    Z               : np.array(N,3)
        Embedded manifold points

    edge_src        : np.array(E)
    edge_dst        : np.array(E)

    trajectory      : np.array(T,3)
        Trajectory points in embedded space

    data_7d         : unused (reserved)

    elev            : float
    azim            : float

    scatter_size    : float
    edge_alpha      : float
    edge_width      : float
    trajectory_width: float
    """

    fig = plt.figure(figsize=(10, 10))

    ax = fig.add_subplot(
        111,
        projection='3d'
    )

    # =====================================================
    # 1. manifold scatter
    # =====================================================

    ax.scatter(
        Z[:, 0],
        Z[:, 1],
        Z[:, 2],
        s=scatter_size,
        alpha=0.15
    )

    # =====================================================
    # 2. graph edges
    # =====================================================

    for src, dst in zip(edge_src, edge_dst):

        x = [Z[src, 0], Z[dst, 0]]
        y = [Z[src, 1], Z[dst, 1]]
        z = [Z[src, 2], Z[dst, 2]]

        ax.plot(
            x,
            y,
            z,
            alpha=edge_alpha,
            linewidth=edge_width
        )

    # =====================================================
    # 3. trajectory line
    # =====================================================

    ax.plot(
        trajectory[:, 0],
        trajectory[:, 1],
        trajectory[:, 2],
        linewidth=trajectory_width,
        c ="red"
    )

    # =====================================================
    # 4. trajectory start point
    # =====================================================

    ax.scatter(
        trajectory[0, 0],
        trajectory[0, 1],
        trajectory[0, 2],
        s=120,
        marker='o'
    )

    # =====================================================
    # 5. trajectory end point
    # =====================================================

    ax.scatter(
        trajectory[-1, 0],
        trajectory[-1, 1],
        trajectory[-1, 2],
        s=120,
        marker='^'
    )

    # =====================================================
    # 6. camera
    # =====================================================

    ax.view_init(
        elev=elev,
        azim=azim
    )

    plt.show()