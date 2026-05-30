import os
import uuid
import json

import numpy as np
from PIL import Image

from rq import get_current_job

from Modules.KNN import (
    Get_KNN_Graph_Adaptive
)

from Modules.Geodesic import (
    compute_geodesic_distance_matrix
)

from Modules.MDS import (
    Get_MDS_graph
)

from Modules.Trajectory import (
    build_weathering_trajectory
)

from AppearanceManifold.Modules._Reconstructor import (
    build_weathering_sequence_semantic
)

from Modules.Utility import (
    normalize_features
)


OUTPUT_ROOT = os.path.join(
    os.path.dirname(__file__),
    "..",
    "outputs"
)


# --------------------------------------------------
# MAIN TASK
# --------------------------------------------------

def generate_weathering_texture_task(payload):

    job = get_current_job()

    # --------------------------------------------------
    # OUTPUT DIRECTORY
    # --------------------------------------------------

    job_uuid = str(uuid.uuid4())

    output_dir = os.path.join(
        OUTPUT_ROOT,
        job_uuid
    )

    os.makedirs(output_dir, exist_ok=True)

    # --------------------------------------------------
    # INPUT
    # --------------------------------------------------

    fx_path = payload["fx_path"]

    sampling_size = payload.get(
        "sampling_size",
        128
    )

    knn_k = payload.get(
        "knn_k",
        8
    )

    max_step = payload.get(
        "max_step",
        30
    )

    job.meta["progress"] = 0.9
    job.save_meta()
    # --------------------------------------------------
    # LOAD FEATURE
    # --------------------------------------------------

    FX = np.load(fx_path)

    # --------------------------------------------------
    # NORMALIZATION
    # --------------------------------------------------

    Z = normalize_features(FX)

    job.meta["progress"] = 0.1
    job.save_meta()

    # --------------------------------------------------
    # KNN GRAPH
    # --------------------------------------------------

    src, dst, wei, wea = (
        Get_KNN_Graph_Adaptive(
            Z,
            K=knn_k
        )
    )

    job.meta["progress"] = 0.25
    job.save_meta()
    
    # --------------------------------------------------
    # GEODESIC
    # --------------------------------------------------

    N = Z.shape[0]

    D = compute_geodesic_distance_matrix(
        src,
        dst,
        wei,
        N
    )

    job.meta["progress"] = 0.45
    job.save_meta()

    # --------------------------------------------------
    # MDS
    # --------------------------------------------------

    M = Get_MDS_graph(
        D,
        3
    )

    job.meta["progress"] = 0.6
    job.save_meta()
    # --------------------------------------------------
    # TRAJECTORY
    # --------------------------------------------------

    start_idx = np.argmin(wea)
    end_idx = np.argmax(wea)

    trajectory_points, trajectory_nodes = (
        build_weathering_trajectory(
            M,
            start_idx,
            end_idx
        )
    )

    trajectory = trajectory_points

    job.meta["progress"] = 0.75
    job.save_meta()

    # --------------------------------------------------
    # RECONSTRUCTION
    # --------------------------------------------------

    steps = build_weathering_sequence_semantic(
        FX,
        M,
        trajectory,
        W=sampling_size,
        H=sampling_size,
        max_step=max_step
    )

    job.meta["progress"] = 0.9
    job.save_meta()

    # --------------------------------------------------
    # SAVE OUTPUTS
    # --------------------------------------------------

    texture_paths = []

    for i, step in enumerate(steps):

        rgb = np.clip(
            step[:, :, 0:3] * 255.0,
            0,
            255
        ).astype(np.uint8)

        texture_path = os.path.join(
            output_dir,
            f"weathering_{i:03d}.png"
        )

        Image.fromarray(rgb).save(texture_path)

        texture_paths.append(texture_path)

    # --------------------------------------------------
    # SAVE NUMPY
    # --------------------------------------------------

    np.save(
        os.path.join(output_dir, "M.npy"),
        M
    )

    np.save(
        os.path.join(output_dir, "D.npy"),
        D
    )

    np.save(
        os.path.join(output_dir, "trajectory.npy"),
        trajectory
    )

    # --------------------------------------------------
    # SAVE METADATA
    # --------------------------------------------------

    metadata = {
        "texture_paths": texture_paths,
        "trajectory_nodes": trajectory_nodes.tolist(),
        "num_steps": len(texture_paths)
    }

    metadata_path = os.path.join(
        output_dir,
        "metadata.json"
    )

    with open(metadata_path, "w") as f:
        json.dump(metadata, f, indent=4)

    job.meta["progress"] = 1.0
    job.save_meta()

    # --------------------------------------------------
    # RETURN
    # --------------------------------------------------

    return {
        "job_uuid": job_uuid,
        "output_dir": output_dir,
        "metadata_path": metadata_path,
        "texture_paths": texture_paths
    }