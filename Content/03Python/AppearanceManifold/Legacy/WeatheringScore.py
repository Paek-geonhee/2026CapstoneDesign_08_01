import numpy as np

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

