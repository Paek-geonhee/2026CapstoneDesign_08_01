import numpy as np

def evaluate_trajectory(trajectory_points):

    diffs = np.diff(trajectory_points, axis=0)

    step_lengths = np.linalg.norm(diffs, axis=1)

    total_length = np.sum(step_lengths)

    directions = diffs / (
        np.linalg.norm(diffs, axis=1, keepdims=True) + 1e-8
    )

    direction_change = np.diff(directions, axis=0)

    curvature = np.linalg.norm(direction_change, axis=1)

    result = {
        "trajectory_point_count": int(len(trajectory_points)),

        "trajectory_total_length": float(total_length),

        "step_length_min": float(np.min(step_lengths)),
        "step_length_max": float(np.max(step_lengths)),
        "step_length_mean": float(np.mean(step_lengths)),
        "step_length_std": float(np.std(step_lengths)),

        "curvature_mean": float(np.mean(curvature)),
        "curvature_std": float(np.std(curvature)),
        "curvature_max": float(np.max(curvature)),
    }

    return result