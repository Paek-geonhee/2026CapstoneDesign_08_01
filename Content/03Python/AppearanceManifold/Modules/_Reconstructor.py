import numpy as np

# ============================================================
# Trajectory Projection
# ============================================================


def find_nearest_trajectory_index(feature_7d, trajectory_samples):
    """
    Parameters
    ----------
    feature_7d : (7,)

    trajectory_samples : (T,7)

    Returns
    -------
    idx : int
    """

    dist = np.linalg.norm(trajectory_samples - feature_7d[None, :], axis=1)
    return int(np.argmin(dist))


# ============================================================
# Weathering Interpolation
# ============================================================


def build_weathering_interpolation_path(TexA_7d, TexB_7d, samples_7d, path, alpha):
    """
    Trajectory-based semantic interpolation.

    Parameters
    ----------
    TexA_7d : (H,W,7)

    TexB_7d : (H,W,7)

    samples_7d : (N,7)

    path : (T,)

    alpha : float
        0.0 -> TexA
        1.0 -> TexB

    Returns
    -------
    result : (H,W,7)
    """

    # --------------------------------------------------------
    # Validation
    # --------------------------------------------------------

    if TexA_7d.ndim != 3:
        raise ValueError(
            f"TexA_7d must be (H,W,7), got {TexA_7d.shape}"
        )

    if TexB_7d.ndim != 3:
        raise ValueError(
            f"TexB_7d must be (H,W,7), got {TexB_7d.shape}"
        )

    if samples_7d.ndim != 2:
        raise ValueError(
            f"samples_7d must be (N,7), got {samples_7d.shape}"
        )

    H, W, C = TexA_7d.shape

    if C != 7:
        raise ValueError(
            f"Expected 7 channels, got {C}"
        )

    # --------------------------------------------------------
    # Clamp alpha
    # --------------------------------------------------------

    alpha = np.clip(alpha, 0.0, 1.0)

    # --------------------------------------------------------
    # Trajectory samples
    # --------------------------------------------------------

    trajectory_samples = samples_7d[path]
    T = len(path)
    result = np.zeros_like(TexA_7d, dtype=np.float32)

    # --------------------------------------------------------
    # Per-pixel interpolation
    # --------------------------------------------------------

    for y in range(H):

        for x in range(W):

            pixelA = TexA_7d[y, x]
            pixelB = TexB_7d[y, x]

            # ------------------------------------------------
            # Projection
            # ------------------------------------------------

            idxA = find_nearest_trajectory_index(pixelA, trajectory_samples)
            idxB = find_nearest_trajectory_index(pixelB, trajectory_samples)

            # ------------------------------------------------
            # Interpolate trajectory index
            # ------------------------------------------------

            interp_idx = (idxB - idxA) * alpha + idxA
            

            interp_idx = int(np.clip(np.round(interp_idx), 0, T - 1))

            # ------------------------------------------------
            # Semantic trajectory states
            # ------------------------------------------------

            sampleA = trajectory_samples[idxA]
            sampleB = trajectory_samples[idxB]
            sampleInterp = trajectory_samples[interp_idx]
            # ------------------------------------------------
            # Bidirectional semantic transfer
            # ------------------------------------------------

            resultA = pixelA + (sampleInterp - sampleA)
            resultB = pixelB - (sampleB - sampleInterp)
            
            # ------------------------------------------------
            # Final blend
            # ------------------------------------------------

            reconstructed = (1.0 - alpha) * resultA + alpha * resultB

            # ------------------------------------------------
            # Stabilization
            # ------------------------------------------------

            reconstructed[:6] = np.clip(reconstructed[:6], 0.0, 1.0)
            reconstructed[6] = np.clip(reconstructed[6], 0.0, 1.0)

            result[y, x] = reconstructed

    return result

# ============================================================
# Texture Export
# ============================================================

from PIL import Image


def export_texture_set(texture_7d, output_prefix="weathering"):
    """
    texture_7d : (H,W,7)
    """

    # --------------------------------------------------------
    # BaseColor
    # --------------------------------------------------------

    basecolor = texture_7d[:, :, 0:3]
    basecolor_img = np.clip(basecolor * 255.0, 0, 255).astype(np.uint8)
    Image.fromarray(basecolor_img).save(f"{output_prefix}_basecolor.png")

    # --------------------------------------------------------
    # Specular
    # --------------------------------------------------------

    specular = texture_7d[:, :, 3:6]
    specular_img = np.clip(specular * 255.0, 0, 255).astype(np.uint8)
    Image.fromarray(specular_img).save(f"{output_prefix}_specular.png")

    # --------------------------------------------------------
    # Roughness
    # --------------------------------------------------------

    roughness = texture_7d[:, :, 6]
    roughness_img = np.clip(roughness * 255.0, 0, 255).astype(np.uint8)
    Image.fromarray(roughness_img, mode='L').save(f"{output_prefix}_roughness.png")


# ============================================================
# Example Usage
# ============================================================

if __name__ == "__main__":

    # --------------------------------------------------------
    # Example placeholders
    # --------------------------------------------------------

    # samples_7d : (N,7)
    # embedded : (N,3)
    # edge_src, edge_dst, edge_weight
    # TexA_7d : (H,W,7)
    # TexB_7d : (H,W,7)

    pass
