import numpy as np
from sklearn.neighbors import NearestNeighbors

import matplotlib.pyplot as plt
import ipywidgets as widgets
from PIL import Image

def resample_trajectory(
    trajectory_points,
    num_points=256
):

    seg = (
        trajectory_points[1:]
        - trajectory_points[:-1]
    )

    seg_len = np.linalg.norm(
        seg,
        axis=1
    )

    cumulative = np.concatenate([
        [0],
        np.cumsum(seg_len)
    ])

    total_len = cumulative[-1]

    sample_t = np.linspace(
        0,
        total_len,
        num_points
    )

    out = np.zeros(
        (num_points, 3),
        dtype=np.float32
    )

    for i, t in enumerate(sample_t):

        idx = np.searchsorted(
            cumulative,
            t
        ) - 1

        idx = np.clip(
            idx,
            0,
            len(seg_len)-1
        )

        t0 = cumulative[idx]
        t1 = cumulative[idx + 1]

        alpha = (
            (t - t0)
            / (t1 - t0 + 1e-8)
        )

        p0 = trajectory_points[idx]
        p1 = trajectory_points[idx + 1]

        out[i] = (
            p0 * (1 - alpha)
            + p1 * alpha
        )

    return out

def compute_weather_score_new(
    diffuse,
    specular,
    roughness
):
    """
    diffuse  : (N,3)
    specular : (N,3)
    roughness: (N,)
    """

    # -----------------------------------------
    # 1. Rust-like darkness
    # -----------------------------------------

    # WARN: Use consistent convention throught the whole project
    # TODO: reference: choose one from followings:
    # * https://scikit-image.org/docs/stable/auto_examples/color_exposure/plot_rgb_to_gray.html
    # * https://kr.mathworks.com/help/matlab/ref/rgb2gray.html
    # * If you use wikipedia page, please clarify which section your are referring 
    #   (e.g. https:/wikipedia.org/{document_name}#SectionName)
    luminance = (
        diffuse[:, 0] * 0.299 +
        diffuse[:, 1] * 0.587 +
        diffuse[:, 2] * 0.114
    )

    darkness = 1.0 - luminance

    # -----------------------------------------
    # 2. Specular attenuation
    # -----------------------------------------

    spec_strength = np.mean(specular, axis=1)

    metal_loss = 1.0 - spec_strength

    # -----------------------------------------
    # 3. Roughness increase
    # -----------------------------------------

    rough_term = roughness

    # -----------------------------------------
    # 4. Final weather score
    # -----------------------------------------

    weather_score = (
        darkness * 0.35 +
        metal_loss * 0.25 +
        rough_term * 0.40
    )

    # normalize
    weather_score = (
        weather_score - weather_score.min()
    ) / (
        weather_score.max() -
        weather_score.min() +
        1e-8
    )

    return weather_score.astype(np.float32)

def reconstruct_semantic(
    samples_7d,
    embedded,
    query_points,
    sample_weather_score,
    trajectory_t,
    K=16,
    semantic_sigma=0.45,
    future_bias=0.9
):
    """
    samples_7d           : (N,7)
    embedded             : (N,3)
    query_points         : (M,3)

    sample_weather_score : (N,)
    trajectory_t         : (M,)

    return:
        reconstructed : (M,7)
    """

    N = samples_7d.shape[0]
    M = query_points.shape[0]

    out = np.zeros((M, 7), dtype=np.float32)

    # ----------------------------------------------------
    # 1. KNN search
    # ----------------------------------------------------

    nn = NearestNeighbors(
        n_neighbors=K * 4
    )

    nn.fit(embedded)

    dists, indices = nn.kneighbors(query_points)

    # ----------------------------------------------------
    # 2. Reconstruction
    # ----------------------------------------------------

    for qi in range(M):

        candidate_idx = indices[qi]
        candidate_dist = dists[qi]

        target_t = trajectory_t[qi]

        # ---------------------------------------------
        # semantic filtering
        # ---------------------------------------------

        candidate_weather = sample_weather_score[candidate_idx]

        semantic_delta = np.abs(
            candidate_weather - target_t
        )

        semantic_weight = np.exp(
            -(semantic_delta ** 2) /
            (2 * semantic_sigma * semantic_sigma)
        )

        # ---------------------------------------------
        # future bias
        # ---------------------------------------------

        future_mask = (
            candidate_weather >= target_t
        ).astype(np.float32)

        future_weight = (
            future_mask * future_bias +
            (1.0 - future_mask) * (1.0 - future_bias)
        )

        # ---------------------------------------------
        # spatial weight
        # ---------------------------------------------

        spatial_weight = np.exp(
            -candidate_dist * 4.0
        )

        # ---------------------------------------------
        # final weight
        # ---------------------------------------------

        weight = (
            semantic_weight *
            future_weight *
            spatial_weight
        )

        # stabilize
        weight += 1e-8

        weight /= np.sum(weight)

        # ---------------------------------------------
        # reconstruction
        # ---------------------------------------------

        reconstructed = np.sum(
            samples_7d[candidate_idx] *
            weight[:, None],
            axis=0
        )

        out[qi] = reconstructed

    # ----------------------------------------------------
    # 3. Clamp
    # ----------------------------------------------------

    out[:, :6] = np.clip(out[:, :6], 0, 1)
    out[:, 6] = np.clip(out[:, 6], 0, 1)

    return out


def build_weathering_sequence_semantic(
    samples_7d,
    embedded,
    trajectory,
    H,
    W,
    max_step=20,
    K=8
):

    diffuse = samples_7d[:, 0:3]
    specular = samples_7d[:, 3:6]
    roughness = samples_7d[:, 6]

    # ----------------------------------------------------
    # 1. Weather Score
    # ----------------------------------------------------

    weather_score = compute_weather_score_new(
        diffuse,
        specular,
        roughness
    )

    steps = []

    # ----------------------------------------------------
    # 2. trajectory parameterization
    # ----------------------------------------------------

    traj_t = np.linspace(
        0.0,
        1.0,
        len(trajectory)
    )

    # ----------------------------------------------------
    # 3. sequence generation
    # ----------------------------------------------------

    for step in range(max_step):

        alpha = (
            step / (max_step - 1)
        )

        # trajectory index
        tidx = int(
            alpha * (len(trajectory) - 1)
        )

        query_point = trajectory[tidx]

        # 모든 픽셀을 동일 semantic state로 이동
        trajectory_start = trajectory[0]
        trajectory_end   = trajectory[-1]

        flow = (
            trajectory_end -
            trajectory_start
        )

        query_points = (
            embedded +
            flow[None,:] * alpha
        )

        trajectory_t = np.full(
            samples_7d.shape[0],
            traj_t[tidx],
            dtype=np.float32
        )

        reconstructed = reconstruct_semantic(
            samples_7d=samples_7d,
            embedded=embedded,
            query_points=query_points,
            sample_weather_score=weather_score,
            trajectory_t=trajectory_t,
            K=K
        )

        steps.append(
            reconstructed.reshape(H, W, 7)
        )
    # ----------------------------------------------------
    # 4. Export middle weathering step
    # ----------------------------------------------------

    middle_step_idx = max_step // 2

    middle_step = steps[middle_step_idx]

    # ----------------------------------------------------
    # BaseColor
    # ----------------------------------------------------

    basecolor = middle_step[:, :, 0:3]

    basecolor_img = np.clip(
        basecolor * 255.0,
        0,
        255
    ).astype(np.uint8)

    Image.fromarray(
        basecolor_img
    ).save(
        "weathering_mid_basecolor.png"
    )

    # ----------------------------------------------------
    # Specular
    # ----------------------------------------------------

    specular = middle_step[:, :, 3:6]

    specular_img = np.clip(
        specular * 255.0,
        0,
        255
    ).astype(np.uint8)

    Image.fromarray(
        specular_img
    ).save(
        "weathering_mid_specular.png"
    )

    # ----------------------------------------------------
    # Roughness
    # ----------------------------------------------------

    roughness = middle_step[:, :, 6]

    roughness_img = np.clip(
        roughness * 255.0,
        0,
        255
    ).astype(np.uint8)

    Image.fromarray(
        roughness_img,
        mode='L'
    ).save(
        "weathering_mid_roughness.png"
    )

    return steps


def visualize_weathering_sequence(
    steps
):

    max_step = len(steps) - 1

    fig, axs = plt.subplots(
        1,
        3,
         figsize=(9, 3)
    )

    plt.tight_layout()


    diffuse = steps[0][:, :, 0:3]
    specular = steps[0][:, :, 3:6]
    roughness = steps[0][:, :, 6]

    im0 = axs[0].imshow(diffuse)
    axs[0].set_title("Diffuse")

    im1 = axs[1].imshow(specular)
    axs[1].set_title("Specular")

    im2 = axs[2].imshow(
        roughness,
        cmap='gray',
        vmin=0,
        vmax=1
    )
    axs[2].set_title("Roughness")

    for ax in axs:
        ax.axis("off")


    def update(step_idx):

        step_data = steps[step_idx]

        diffuse = step_data[:, :, 0:3]
        specular = step_data[:, :, 3:6]
        roughness = step_data[:, :, 6]

        im0.set_data(diffuse)
        im1.set_data(specular)
        im2.set_data(roughness)

        fig.suptitle(
            f"Weathering Step {step_idx}",
            fontsize=16
        )

        fig.canvas.draw_idle()


    slider = widgets.IntSlider(
        value=0,
        min=0,
        max=max_step,
        step=1,
        description='Step:',
        continuous_update=True
    )

    widgets.interact(
        update,
        step_idx=slider
    )

    plt.show()


def build_weathering_sequence_trajectory_projection(
    source_samples_7d,
    target_samples_7d,

    source_embedded,
    target_embedded,

    trajectory,

    H,
    W,

    current_step,
    total_step,

    K=8,
    blend_sigma=0.15,
    nearest_blend_ratio=0.7
):
    """
    Parameters
    ----------
    source_samples_7d : (N,7)
        Initial material state

    target_samples_7d : (N,7)
        Final material state

    source_embedded : (N,3)
        Embedded latent coordinates of source

    target_embedded : (N,3)
        Embedded latent coordinates of target

    trajectory : (T,3)
        Resampled manifold trajectory

    H, W : int

    current_step : int
    total_step   : int

    K : int
        Neighbor count for reconstruction

    blend_sigma : float
        Gaussian blend sigma

    nearest_blend_ratio : float
        0.0 = pure weighted blend
        1.0 = pure nearest neighbor

    Returns
    -------
    out : (H,W,7)
    """

    # --------------------------------------------------------
    # 1. Progress ratio
    # --------------------------------------------------------

    alpha = current_step / max(total_step - 1, 1)

    # --------------------------------------------------------
    # 2. Build trajectory parameterization
    # --------------------------------------------------------

    T = trajectory.shape[0]

    traj_s = np.linspace(
        0.0,
        1.0,
        T,
        dtype=np.float32
    )

    # --------------------------------------------------------
    # 3. Projection helper
    # --------------------------------------------------------

    def project_to_trajectory(points, trajectory):

        diff = (
            points[:, None, :] -
            trajectory[None, :, :]
        )

        dist2 = np.sum(diff * diff, axis=2)

        nearest_idx = np.argmin(dist2, axis=1)

        projected_s = traj_s[nearest_idx]

        return projected_s, nearest_idx

    # --------------------------------------------------------
    # 4. Source / Target projection
    # --------------------------------------------------------

    source_s, _ = project_to_trajectory(
        source_embedded,
        trajectory
    )

    target_s, _ = project_to_trajectory(
        target_embedded,
        trajectory
    )

    # --------------------------------------------------------
    # 5. Interpolate trajectory position
    # --------------------------------------------------------

    interp_s = (
        (1.0 - alpha) * source_s +
        alpha * target_s
    )

    interp_s = np.clip(
        interp_s,
        0.0,
        1.0
    )

    # --------------------------------------------------------
    # 6. Retrieve trajectory latent positions
    # --------------------------------------------------------

    interp_idx = np.round(
        interp_s * (T - 1)
    ).astype(np.int32)

    interp_idx = np.clip(
        interp_idx,
        0,
        T - 1
    )

    query_points = trajectory[interp_idx]

    # --------------------------------------------------------
    # 7. Build reconstruction KNN
    # --------------------------------------------------------

    combined_embedded = np.concatenate([
        source_embedded,
        target_embedded
    ], axis=0)

    combined_samples = np.concatenate([
        source_samples_7d,
        target_samples_7d
    ], axis=0)

    nn = NearestNeighbors(
        n_neighbors=K
    )

    nn.fit(combined_embedded)

    dists, indices = nn.kneighbors(query_points)

    # --------------------------------------------------------
    # 8. Reconstruction
    # --------------------------------------------------------

    N = query_points.shape[0]

    out = np.zeros(
        (N,7),
        dtype=np.float32
    )

    for i in range(N):

        candidate_idx = indices[i]
        candidate_dist = dists[i]

        candidate_samples = combined_samples[candidate_idx]

        # ----------------------------------------
        # Gaussian weights
        # ----------------------------------------

        weights = np.exp(
            -(candidate_dist ** 2) /
            (2 * blend_sigma * blend_sigma)
        )

        weights += 1e-8

        weights /= np.sum(weights)

        blended_sample = np.sum(
            candidate_samples *
            weights[:, None],
            axis=0
        )

        # ----------------------------------------
        # nearest sample
        # ----------------------------------------

        nearest_sample = candidate_samples[0]

        # ----------------------------------------
        # hybrid reconstruction
        # ----------------------------------------

        reconstructed = (
            nearest_sample * nearest_blend_ratio +
            blended_sample * (1.0 - nearest_blend_ratio)
        )

        out[i] = reconstructed

    # --------------------------------------------------------
    # 9. Clamp
    # --------------------------------------------------------

    out[:, :6] = np.clip(
        out[:, :6],
        0.0,
        1.0
    )

    out[:, 6] = np.clip(
        out[:, 6],
        0.0,
        1.0
    )

    # --------------------------------------------------------
    # 10. Reshape
    # --------------------------------------------------------

    out = out.reshape(H, W, 7)

    return out