import numpy as np
from scipy.sparse.linalg import eigsh

import matplotlib.pyplot as plt

""" 
Description
Manifold Graph와 측지선 거리 행렬 바탕으로 Multidimentional Scaling 적용

측지선 거리값을 최대한 유지한 상태로 저차원 공간으로 축소함.
"""

def Get_MDS_graph(D, dim=3):

    """
    Params
    ----------
    D               : np.array(N,7)
    dim             : int

    return
    Z               : np.array(N,3)
    ----------
    """
    N = D.shape[0]

    D = np.asarray(D, dtype=np.float64)

    finite_mask = np.isfinite(D)

    D = D.copy()

    D[~finite_mask] = np.max(D[finite_mask])

    # =====================================================
    # Double centering WITHOUT H matrix
    # =====================================================

    D2 = D * D

    row_mean = np.mean(D2, axis=1, keepdims=True)

    col_mean = np.mean(D2, axis=0, keepdims=True)

    grand_mean = np.mean(D2)

    B = -0.5 * (D2 - row_mean - col_mean + grand_mean)

    # =====================================================
    # numerical stabilization
    # =====================================================

    np.nan_to_num(B, copy=False)

    B = 0.5 * (B + B.T)

    # =====================================================
    # TOP-K eigensolver
    # =====================================================

    evals, evecs = eigsh(B, k=dim, which='LA')

    idx = np.argsort(evals)[::-1]

    evals = evals[idx]
    evecs = evecs[:, idx]

    Z = evecs * np.sqrt(np.maximum(evals, 0))

    return Z




# --------------------------------------- deprecated -----------------------------------------

def visualize_MDS_Graph(Z, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    data_7d         : np.array(N,7)
    """
     
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')

    ax.scatter(Z[:, 0], Z[:, 1], Z[:, 2], s=2)

    plt.show()

def visualize_MDS_Graph_Edges(Z,  edge_src, edge_dst, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    edge_src        : np.array(N,N)
    edge_dst        : np.array(N,N)
    data_7d         : np.array(N,7)
    """
     
    fig = plt.figure(figsize=(10, 10))
    ax = fig.add_subplot(111, projection='3d')

    # scatter points
    ax.scatter(
        Z[:, 0],
        Z[:, 1],
        Z[:, 2],
        s=2
    )

    # draw graph edges
    for src, dst in zip(edge_src, edge_dst):

        x = [Z[src, 0], Z[dst, 0]]
        y = [Z[src, 1], Z[dst, 1]]
        z = [Z[src, 2], Z[dst, 2]]

        ax.plot(
            x,
            y,
            z,
            alpha=0.25,
            linewidth=0.5
        )

    plt.show()

def visualize_MDS_Graph_Edges_2D(Z,  edge_src, edge_dst, data_7d):
    """
    Params
    ----------
    Z               : np.array(N,3)
    edge_src        : np.array(N,N)
    edge_dst        : np.array(N,N)
    data_7d         : np.array(N,7)
    """
     
    fig = plt.figure(figsize=(10, 10))
    ax = fig.add_subplot(111, projection='3d')

    # scatter points
    ax.scatter(
        Z[:, 0],
        Z[:, 1],
        s=2
    )

    # draw graph edges
    for src, dst in zip(edge_src, edge_dst):

        x = [Z[src, 0], Z[dst, 0]]
        y = [Z[src, 1], Z[dst, 1]]

        ax.plot(
            x,
            y,
            alpha=0.25,
            linewidth=0.5
        )

    plt.show()


