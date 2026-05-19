import numpy as np
import matplotlib.pyplot as plt
from scipy.sparse.csgraph import dijkstra
from scipy.sparse import coo_matrix


def build_weathering_trajectory(embedded, edge_src, edge_dst, edge_weight, start_idx, end_idx):
    """
    Parameters
    ----------
    embedded            : (N,3)

    edge_src            : (E,)
    edge_dst            : (E,)
    edge_weight         : (E,)

    start_idx           : int
    end_idx             : int

    Returns
    -------
    trajectory_points   : (T,3)
    trajectory_indices  : (T,)
    """

    N = embedded.shape[0]


    graph = coo_matrix((edge_weight,(edge_src, edge_dst)), shape=(N, N)).tocsr()

    dist, predecessors = dijkstra(graph,directed=True,indices=start_idx,return_predecessors=True)

    path = []

    current = end_idx

    while current != start_idx:
        if current == -9999:
            raise ValueError("Trajectory path disconnected")

        path.append(current)

        current = predecessors[current]

    path.append(start_idx)

    path.reverse()

    path = np.array(path)

    trajectory_points = embedded[path]

    return trajectory_points, path

def resample_trajectory(
    trajectory_points,
    num_points=64
):
    
    """
    Parameters
    ----------
    trajectory_points   : (T, 3)
    num_points          : int

    Returns
    -------
    out                 : (T, 3)
    """

    seg = trajectory_points[1:]- trajectory_points[:-1]

    seg_len = np.linalg.norm(seg, axis=1)

    cumulative = np.concatenate([[0],np.cumsum(seg_len)])

    total_len = cumulative[-1]

    sample_t = np.linspace(0,total_len,num_points)

    out = np.zeros((num_points, 3),dtype=np.float32)

    for i, t in enumerate(sample_t):

        idx = np.searchsorted(cumulative, t) - 1

        idx = np.clip(idx, 0, len(seg_len)-1)

        t0 = cumulative[idx]
        t1 = cumulative[idx + 1]

        alpha = (t - t0) / (t1 - t0 + 1e-8)
        

        p0 = trajectory_points[idx]
        p1 = trajectory_points[idx + 1]

        out[i] = p0 * (1 - alpha)+ p1 * alpha

    return out

def visualize_trajectory(
    embedded,
    trajectory,
    elev=30,
    azim=120
):
    """
    Parameters
    ----------
    embedded            : (N, 3)
    trajectory          : (T, 3)
    elev                : int
    azim                : int

    Returns
    -------
    None
    """

    fig = plt.figure(figsize=(8,8))

    ax = fig.add_subplot(111, projection='3d')

    # 1. MDS scatter
    ax.scatter(embedded[:,0],embedded[:,1],embedded[:,2],s=2,alpha=0.3,c = "red")

    # 2. trajectory line
    ax.plot(trajectory[:,0], trajectory[:,1], trajectory[:,2], linewidth=4)

    # 3. start / end point
    ax.scatter(trajectory[0,0], trajectory[0,1], trajectory[0,2], s=30, marker='o')
    ax.scatter(trajectory[-1,0], trajectory[-1,1], trajectory[-1,2], s=30,marker='^')

    ax.view_init(elev=elev,azim=azim)

    plt.show()