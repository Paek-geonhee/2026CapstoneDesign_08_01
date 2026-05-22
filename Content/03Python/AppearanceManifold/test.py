import numpy as np
import build.geodesic_backend as gb

edge_src = np.array(
    [0, 0, 1],
    dtype=np.int32
)

edge_dst = np.array(
    [1, 2, 2],
    dtype=np.int32
)

edge_weight = np.array(
    [1.0, 2.0, 1.0],
    dtype=np.float32
)

D = gb.compute_geodesic_distance_matrix(
    edge_src,
    edge_dst,
    edge_weight,
    3
)

print(D)