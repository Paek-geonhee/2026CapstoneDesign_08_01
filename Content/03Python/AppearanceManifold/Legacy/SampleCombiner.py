import numpy as np

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