
import numpy as np
from scipy.spatial.distance import pdist, squareform

def evaluate_mds_embedding(M, D):

    embedded_dist = squareform(pdist(M))

    finite_mask = np.isfinite(D)

    D_valid = D[finite_mask]
    E_valid = embedded_dist[finite_mask]

    stress = np.sqrt(
        np.sum((D_valid - E_valid) ** 2)
        /
        np.sum(D_valid ** 2)
    )

    axis_var = np.var(M, axis=0)

    result = {
        "embedding_shape": M.shape,

        "axis_variance": axis_var.tolist(),

        "embedding_norm_mean": float(
            np.mean(np.linalg.norm(M, axis=1))
        ),

        "embedding_norm_std": float(
            np.std(np.linalg.norm(M, axis=1))
        ),

        "stress": float(stress),
    }

    return result