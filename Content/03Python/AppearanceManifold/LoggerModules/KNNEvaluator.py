import numpy as np
from scipy.sparse.csgraph import connected_components
from scipy.sparse import csr_matrix

def evaluate_knn_graph(edge_src, edge_dst, edge_weight, N):

    adjacency = csr_matrix(
        (
            np.ones(len(edge_src)),
            (edge_src, edge_dst)
        ),
        shape=(N, N)
    )

    n_components, labels = connected_components(
        adjacency,
        directed=False
    )

    degree = np.bincount(edge_src, minlength=N)

    result = {
        "node_count": int(N),
        "edge_count": int(len(edge_src)),

        "connected_components": int(n_components),
        "largest_component_ratio": float(
            np.max(np.bincount(labels)) / N
        ),

        "degree_min": int(np.min(degree)),
        "degree_max": int(np.max(degree)),
        "degree_mean": float(np.mean(degree)),
        "degree_std": float(np.std(degree)),

        "edge_weight_min": float(np.min(edge_weight)),
        "edge_weight_max": float(np.max(edge_weight)),
        "edge_weight_mean": float(np.mean(edge_weight)),
        "edge_weight_std": float(np.std(edge_weight)),
    }

    return result