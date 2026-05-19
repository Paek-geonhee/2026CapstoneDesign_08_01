import numpy as np
from PIL import Image


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

