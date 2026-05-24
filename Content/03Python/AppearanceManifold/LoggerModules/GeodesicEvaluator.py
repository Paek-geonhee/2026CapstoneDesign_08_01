import numpy as np

def evaluate_geodesic_matrix(D):

    finite_mask = np.isfinite(D)

    finite_values = D[finite_mask]

    inf_count = np.sum(~finite_mask)

    upper_tri = D[np.triu_indices_from(D, k=1)]
    upper_tri = upper_tri[np.isfinite(upper_tri)]

    result = {
        "shape": D.shape,

        "finite_ratio": float(np.mean(finite_mask)),
        "inf_count": int(inf_count),

        "distance_min": float(np.min(upper_tri)),
        "distance_max": float(np.max(upper_tri)),
        "distance_mean": float(np.mean(upper_tri)),
        "distance_std": float(np.std(upper_tri)),

        "distance_percentile_50": float(np.percentile(upper_tri, 50)),
        "distance_percentile_90": float(np.percentile(upper_tri, 90)),
        "distance_percentile_99": float(np.percentile(upper_tri, 99)),
    }

    return result